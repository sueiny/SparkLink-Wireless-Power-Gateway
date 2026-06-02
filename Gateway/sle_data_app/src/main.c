#include "sle_multi_client.h"
#include "sle_app_config.h"
#include "notify_printer.h"
#include "ipc_sender.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define STACK_RAW_LOG_PATH "/tmp/sle_stack_raw.log"
#define TICK_INTERVAL_SEC  1

static sigset_t g_wait_signals;

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
    (void)argc;
    (void)argv;
    int exit_code = 1;
    bool notify_printer_started = false;
    bool sle_manager_started = false;

    sle_app_config_t config;
    sle_app_config_init_defaults(&config);
    redirect_stack_stdout();

    if (setup_signal_wait() != 0) {
        fprintf(stderr, "[SLE][ERROR] setup signal wait failed\n");
        return 1;
    }

    fprintf(stderr, "[SLE][STATUS] sle_data_app start raw_log=%s\n", STACK_RAW_LOG_PATH);
    sle_app_config_print(&config);

    /*
     * 启动顺序：
     * 1. IPC sender 初始化（非阻塞，首次发送时才真正连接）。
     * 2. notify printer 启动，保证后续 notify 入队后有 worker 消费。
     * 3. sle manager 启动协议栈、扫描和 SDK callbacks。
     */
    ipc_sender_init();

    if (notify_printer_start() != 0) {
        fprintf(stderr, "[SLE][ERROR] notify printer init failed\n");
        goto cleanup;
    }
    notify_printer_started = true;

    if (sle_manager_init(&config) != 0) {
        fprintf(stderr, "[SLE][ERROR] sle manager init failed\n");
        goto cleanup;
    }
    sle_manager_started = true;

    /*
     * 主循环：用 sigtimedwait 替代独立 maintenance 线程。
     * 每秒超时返回一次，执行 sle_manager_tick() 处理连接超时。
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
        /* 超时 = 1 秒到了，执行周期维护 */
        sle_manager_tick();
    }

    exit_code = 0;

cleanup:
    /*
     * 退出顺序必须和启动顺序相反：
     * 1. 先停 sle manager，停止扫描并关闭协议栈回调来源。
     * 2. 再停 notify printer，drain 已经入队的 RX 日志。
     */
    if (sle_manager_started) {
        sle_manager_deinit();
    }
    if (notify_printer_started) {
        notify_printer_stop();
    }
    ipc_sender_deinit();
    fprintf(stderr, "[SLE][STATUS] sle_data_app exit\n");
    return exit_code;
}
