/*
 * 最小 IPC 发送工具：读取二进制文件，写入 Unix Socket。
 * 用法: ipc_send <socket_path> <binary_file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>

static int connect_unix_socket(int sock, const char *sock_path, int abstract)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (abstract) {
        const char *name = sock_path[0] == '@' ? sock_path + 1 : sock_path;
        size_t name_len = strlen(name);
        if (name_len > sizeof(addr.sun_path) - 1) {
            fprintf(stderr, "socket path too long for abstract namespace: %s\n", name);
            return -1;
        }
        addr.sun_path[0] = '\0';
        memcpy(addr.sun_path + 1, name, name_len);
        socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);
        return connect(sock, (struct sockaddr *)&addr, addr_len);
    }

    if (strlen(sock_path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "socket path too long: %s\n", sock_path);
        return -1;
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);
    socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1);
    return connect(sock, (struct sockaddr *)&addr, addr_len);
}

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

    /* 连接 socket：先按普通文件系统 socket 连接，失败后自动兼容 gatewayd 使用的抽象 socket。 */
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    int use_abstract = sock_path[0] == '@';
    if (connect_unix_socket(sock, sock_path, use_abstract) < 0) {
        if (!use_abstract) {
            int connected = 0;
            close(sock);
            sock = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock < 0) { perror("socket"); free(buf); return 1; }

            if (connect_unix_socket(sock, sock_path, 1) == 0) {
                connected = 1;
                fprintf(stderr, "[SEND] filesystem socket failed, connected as abstract socket\n");
            } else if (sock_path[0] == '/') {
                close(sock);
                sock = socket(AF_UNIX, SOCK_STREAM, 0);
                if (sock < 0) { perror("socket"); free(buf); return 1; }
                if (connect_unix_socket(sock, sock_path + 1, 1) == 0) {
                    connected = 1;
                    fprintf(stderr, "[SEND] filesystem socket failed, connected as stripped abstract socket\n");
                }
            }

            if (!connected) {
                perror("connect filesystem/abstract");
                close(sock);
                free(buf);
                return 1;
            }
        } else {
            perror("connect abstract");
            close(sock);
            free(buf);
            return 1;
        }
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
