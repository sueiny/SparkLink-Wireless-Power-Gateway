#include "sle_multi_client.h"
#include "sle_app_config.h"
#include "notify_printer.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CONFIG_PATH "/userdata/gateway/config/sle_data_app.json"
#define STACK_RAW_LOG_PATH "/tmp/sle_stack_raw.log"
#define MAINTENANCE_INTERVAL_SECONDS 1

typedef struct {
    pthread_t thread;        /* 维护线程句柄；stop 时用 pthread_join 等它退出 */
    volatile bool running;   /* stop 会置 false，worker 下一轮循环看到后退出 */
    bool started;            /* 记录线程是否真的创建成功，避免 join 未创建的线程 */
} maintenance_manager_t;

static maintenance_manager_t g_maintenance_manager;
static sigset_t g_wait_signals;

static const char *parse_config_path(int argc, char **argv);
static void redirect_stack_stdout(void);
static int setup_signal_wait(void);
static int wait_for_stop_signal(void);
static int maintenance_manager_start(void);
static void maintenance_manager_stop(void);
static void *maintenance_manager_worker(void *arg);

/* --------------------------------------------------------------------------
 * Config and process setup
 * -------------------------------------------------------------------------- */

static const char *parse_config_path(int argc, char **argv)
{
    /* 支持板端调试时通过 --config 指定不同配置文件；默认走部署路径。 */
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0) {
            return argv[i + 1];
        }
    }
    return DEFAULT_CONFIG_PATH;
}

static void redirect_stack_stdout(void)
{
    /*
     * SDK 和部分 printf 走 stdout，统一重定向到 raw log。
     * 终端可观察日志使用 stderr，避免和协议栈原始日志混在一起。
     */
    int fd = open(STACK_RAW_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[SLE][WARN] open stack raw log failed path=%s\n", STACK_RAW_LOG_PATH);
        return;
    }
    fflush(stdout);
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

/* --------------------------------------------------------------------------
 * Signal lifecycle
 * -------------------------------------------------------------------------- */

static int setup_signal_wait(void)
{
    /*
     * 在创建任何 worker 前屏蔽 SIGINT/SIGTERM。
     * 后续线程会继承这个 signal mask，因此不会由随机 worker 接收退出信号。
     * main 线程随后用 sigwait() 同步等待退出信号，退出路径就只有一条。
     */
    sigemptyset(&g_wait_signals);
    sigaddset(&g_wait_signals, SIGINT);
    sigaddset(&g_wait_signals, SIGTERM);
    return pthread_sigmask(SIG_BLOCK, &g_wait_signals, NULL);
}

static int wait_for_stop_signal(void)
{
    /*
     * main 线程在这里阻塞，不再承担周期维护工作。
     * timeout/kill/Ctrl+C 到来时 sigwait 返回，然后 main 进入 cleanup。
     */
    int signum = 0;
    int ret = sigwait(&g_wait_signals, &signum);
    if (ret == 0) {
        fprintf(stderr, "[SLE][STATUS] received signal %d\n", signum);
    }
    return ret;
}

/* --------------------------------------------------------------------------
 * Maintenance manager lifecycle
 * -------------------------------------------------------------------------- */

/* 启动长期维护线程；它只负责周期调用 sle_manager_tick()。 */
static int maintenance_manager_start(void)
{
    memset(&g_maintenance_manager, 0, sizeof(g_maintenance_manager));
    g_maintenance_manager.running = true;
    int ret = pthread_create(&g_maintenance_manager.thread, NULL, maintenance_manager_worker, NULL);
    if (ret != 0) {
        g_maintenance_manager.running = false;
        return -1;
    }
    g_maintenance_manager.started = true;
    return 0;
}

/* 
 * 停止维护线程。
 * pthread_join 的意义：等 maintenance_manager_worker 真正退出后再 deinit SLE。
 * 否则 worker 可能在 SLE 资源释放过程中继续调用 sle_manager_tick()，造成竞态。
 */
static void maintenance_manager_stop(void)
{
    if (!g_maintenance_manager.started) {
        return;
    }
    g_maintenance_manager.running = false;
    pthread_join(g_maintenance_manager.thread, NULL);
    memset(&g_maintenance_manager, 0, sizeof(g_maintenance_manager));
}

/* --------------------------------------------------------------------------
 * Thread entry points
 * -------------------------------------------------------------------------- */

static void *maintenance_manager_worker(void *arg)
{
    (void)arg;
    /*
     * 所有连接超时、stale 检查都在这个线程里跑。
     * main 线程只负责等待退出信号，SDK 回调线程只处理协议栈事件。
     */
    while (g_maintenance_manager.running) {
        sleep(MAINTENANCE_INTERVAL_SECONDS);
        if (!g_maintenance_manager.running) {
            break;
        }
        sle_manager_tick();
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Main lifecycle
 * -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int exit_code = 1;
    /* 这些 started 标志用于失败路径反向清理，只清理已经启动成功的模块。 */
    bool notify_log_manager_started = false;
    bool sle_manager_started = false;
    bool maintenance_manager_started = false;

    const char *config_path = parse_config_path(argc, argv);
    sle_app_config_t config;
    sle_app_config_init_defaults(&config);
    sle_app_config_load(config_path, &config);
    redirect_stack_stdout();

    if (setup_signal_wait() != 0) {
        fprintf(stderr, "[SLE][ERROR] setup signal wait failed\n");
        return 1;
    }

    fprintf(stderr, "[SLE][STATUS] sle_data_app start raw_log=%s\n", STACK_RAW_LOG_PATH);

    /*
     * 启动顺序：
     * 1. notify log manager 先启动，保证后续 notify 入队后有 worker 消费。
     * 2. sle manager 启动协议栈、扫描和 SDK callbacks。
     * 3. maintenance manager 最后启动，开始周期检查连接流程超时。
     */
    if (notify_printer_start() != 0) {
        fprintf(stderr, "[SLE][ERROR] notify log manager init failed\n");
        goto cleanup;
    }
    notify_log_manager_started = true;

    if (sle_manager_init(&config) != 0) {
        fprintf(stderr, "[SLE][ERROR] sle manager init failed\n");
        goto cleanup;
    }
    sle_manager_started = true;

    if (maintenance_manager_start() != 0) {
        fprintf(stderr, "[SLE][ERROR] maintenance manager start failed\n");
        goto cleanup;
    }
    maintenance_manager_started = true;

    (void)wait_for_stop_signal();
    exit_code = 0;

cleanup:
    /*
     * 退出顺序必须和启动顺序相反：
     * 1. 先停 maintenance manager，避免它在 SLE teardown 时继续 tick。
     * 2. 再停 sle manager，停止扫描并关闭协议栈回调来源。
     * 3. 最后停 notify log manager，drain 已经入队的 RX 日志。
     */
    if (maintenance_manager_started) {
        maintenance_manager_stop();
    }
    if (sle_manager_started) {
        sle_manager_deinit();
    }
    if (notify_log_manager_started) {
        notify_printer_stop();
    }
    fprintf(stderr, "[SLE][STATUS] sle_data_app exit\n");
    return exit_code;
}
