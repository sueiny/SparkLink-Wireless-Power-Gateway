/*
 * 最小 IPC 发送工具：读取二进制文件，写入 Unix Socket。
 * 用法: ipc_send <socket_path> <binary_file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <socket_path> <binary_file>\n", argv[0]);
        return 1;
    }

    const char *sock_path = argv[1];
    const char *file_path = argv[2];

    /* 读取文件 */
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    off_t file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char *buf = malloc(file_size);
    read(fd, buf, file_size);
    close(fd);
    fprintf(stderr, "[SEND] read %ld bytes from %s\n", (long)file_size, file_path);

    /* 连接 socket */
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }
    fprintf(stderr, "[SEND] connected to %s\n", sock_path);

    /* 发送数据 */
    ssize_t sent = write(sock, buf, file_size);
    fprintf(stderr, "[SEND] sent %zd bytes\n", sent);

    close(sock);
    free(buf);
    fprintf(stderr, "[SEND] done\n");
    return 0;
}
