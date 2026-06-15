#include "sle_multi_client.h"
#include "sle_app_config.h"
#include "notify_printer.h"
#include "ipc_sender.h"
#include "ipc_cmd_receiver.h"
#include "sle_cmd_handler.h"
#include "mock_data_generator.h"

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define STACK_RAW_LOG_PATH "/tmp/sle_stack_raw.log"
#define TICK_INTERVAL_SEC  1
#define CMD_SOCKET_PATH    "\0/var/run/gateway/sle_cmd.sock"  /* 抽象命名空间 */
#define CMD_SOCKET_PATH_LEN 30  /* 含首字节 \0 */

static sigset_t g_wait_signals;

typedef enum {
    SLE_APP_MODE_MOCK,
    SLE_APP_MODE_REAL,
    SLE_APP_MODE_HYBRID,
} sle_app_mode_t;

static const char *mode_name(sle_app_mode_t mode)
{
    switch (mode) {
    case SLE_APP_MODE_MOCK:
        return "mock";
    case SLE_APP_MODE_REAL:
        return "real";
    case SLE_APP_MODE_HYBRID:
        return "hybrid";
    default:
        return "unknown";
    }
}

static void print_usage(const char *program)
{
    fprintf(stderr, "usage: %s [--mode mock|real|hybrid]\n", program);
    fprintf(stderr, "  mock   : use mock data only (default)\n");
    fprintf(stderr, "  real   : use real SLE manager only\n");
    fprintf(stderr, "  hybrid : use mock data and real SLE manager\n");
}

static bool parse_mode_value(const char *value, sle_app_mode_t *mode)
{
    if (value == NULL || mode == NULL) {
        return false;
    }
    if (strcmp(value, "mock") == 0) {
        *mode = SLE_APP_MODE_MOCK;
        return true;
    }
    if (strcmp(value, "real") == 0) {
        *mode = SLE_APP_MODE_REAL;
        return true;
    }
    if (strcmp(value, "hybrid") == 0) {
        *mode = SLE_APP_MODE_HYBRID;
        return true;
    }
    return false;
}

static bool parse_args(int argc, char **argv, sle_app_mode_t *mode)
{
    if (mode == NULL) {
        return false;
    }
    *mode = SLE_APP_MODE_MOCK;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *mode_value = NULL;

        if (strcmp(arg, "--mode") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[SLE][ERROR] --mode requires mock|real|hybrid\n");
                return false;
            }
            mode_value = argv[++i];
        } else if (strncmp(arg, "--mode=", 7) == 0) {
            mode_value = arg + 7;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "[SLE][ERROR] unknown argument: %s\n", arg);
            print_usage(argv[0]);
            return false;
        }

        if (mode_value != NULL && !parse_mode_value(mode_value, mode)) {
            fprintf(stderr, "[SLE][ERROR] invalid mode: %s\n", mode_value);
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
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

static int setup_signal_wait(void)
{
    /*
     * 在创建任何 worker 前屏蔽 SIGINT/SIGTERM。
     * 后续线程会继承这个 signal mask，因此不会由随机 worker 接收退出信号。
     * main 线程随后用 sigtimedwait() 同步等待退出信号或超时。
     */
    sigemptyset(&g_wait_signals);
    sigaddset(&g_wait_signals, SIGINT);
    sigaddset(&g_wait_signals, SIGTERM);
    return pthread_sigmask(SIG_BLOCK, &g_wait_signals, NULL);
}

int main(int argc, char **argv)
{
    int exit_code = 1;
    bool notify_printer_started = false;
    bool sle_manager_started = false;
    bool mock_generator_started = false;
    bool cmd_receiver_started = false;
    sle_app_mode_t mode = SLE_APP_MODE_MOCK;
    bool use_mock = true;
    bool use_real_sle = false;

    if (!parse_args(argc, argv, &mode)) {
        return 2;
    }
    use_mock = (mode == SLE_APP_MODE_MOCK || mode == SLE_APP_MODE_HYBRID);
    use_real_sle = (mode == SLE_APP_MODE_REAL || mode == SLE_APP_MODE_HYBRID);

    /* 忽略 SIGPIPE，避免 IPC 对端断开时进程崩溃 */
    signal(SIGPIPE, SIG_IGN);

    sle_app_config_t config;
    sle_app_config_init_defaults(&config);
    redirect_stack_stdout();

    if (setup_signal_wait() != 0) {
        fprintf(stderr, "[SLE][ERROR] setup signal wait failed\n");
        return 1;
    }

    fprintf(stderr, "[SLE][STATUS] sle_data_app start mode=%s raw_log=%s\n",
        mode_name(mode), STACK_RAW_LOG_PATH);
    sle_app_config_print(&config);

    /*
     * 启动顺序：
     * 1. IPC sender 初始化（非阻塞，首次发送时才真正连接）。
     * 2. notify printer 启动，保证后续 notify 入队后有 worker 消费。
     * 3. 根据 --mode 启动模拟数据生成器和/或真实 SLE manager。
     */
    ipc_sender_init();

    /* 启动命令接收器（独立线程，监听 gatewayd 下发的命令） */
    sle_cmd_handler_init();
    if (ipc_cmd_receiver_init(CMD_SOCKET_PATH, CMD_SOCKET_PATH_LEN, sle_cmd_handler_process) != 0) {
        fprintf(stderr, "[SLE][WARN] cmd receiver init failed, command dispatch disabled\n");
    } else {
        cmd_receiver_started = true;
    }

    if (notify_printer_start() != 0) {
        fprintf(stderr, "[SLE][ERROR] notify printer init failed\n");
        goto cleanup;
    }
    notify_printer_started = true;

    if (use_mock) {
        if (mock_data_generator_init() != 0) {
            fprintf(stderr, "[SLE][ERROR] mock data generator init failed\n");
            goto cleanup;
        }
        if (mock_data_generator_start() != 0) {
            fprintf(stderr, "[SLE][ERROR] mock data generator start failed\n");
            goto cleanup;
        }
        mock_generator_started = true;
    } else {
        fprintf(stderr, "[SLE][INFO] mock data generator disabled by mode=%s\n", mode_name(mode));
    }

    if (use_real_sle) {
        if (sle_manager_init(&config) != 0) {
            fprintf(stderr, "[SLE][ERROR] SLE manager init failed\n");
            goto cleanup;
        }
        sle_manager_started = true;
    } else {
        fprintf(stderr, "[SLE][INFO] skipping SLE manager, using mock data only\n");
    }

    /*
     * 主循环：用 sigtimedwait 替代独立 maintenance 线程。
     * 每秒超时返回一次。
     * 收到 SIGINT/SIGTERM 时 sigtimedwait 返回信号编号，退出循环。
     */
    while (1) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += TICK_INTERVAL_SEC;

        int sig = sigtimedwait(&g_wait_signals, NULL, &ts);
        if (sig > 0) {
            fprintf(stderr, "[SLE][STATUS] received signal %d\n", sig);
            break;
        }
        /* 超时 = 1 秒到了，SLE 管理器未初始化时跳过 tick */
        if (sle_manager_started) {
            sle_manager_tick();
        }
    }

    exit_code = 0;

cleanup:
    /*
     * 退出顺序必须和启动顺序相反：
     * 1. 先停 sle manager，停止扫描并关闭协议栈回调来源。
     * 2. 停模拟数据生成器。
     * 3. 再停 notify printer，drain 已经入队的 RX 日志。
     */
    if (sle_manager_started) {
        sle_manager_deinit();
    }
    if (mock_generator_started) {
        mock_data_generator_stop();
    }
    if (cmd_receiver_started) {
        ipc_cmd_receiver_deinit();
        sle_cmd_handler_deinit();
    }
    if (notify_printer_started) {
        notify_printer_stop();
    }
    ipc_sender_deinit();
    fprintf(stderr, "[SLE][STATUS] sle_data_app exit\n");
    return exit_code;
}
