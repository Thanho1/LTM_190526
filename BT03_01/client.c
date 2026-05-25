/*
 * file_client - File Server Client (Multithreading)
 *
 * Y tuong:
 *   - Thread phu: lien tuc nhan du lieu tu server (dung de nhan noi dung file).
 *   - Thread chinh: nhan danh sach file, cho nguoi dung nhap ten file, gui len server.
 *   - Dung bien toan cuc recv_done de bao hieu thread phu dung lai khi xong.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define PORT      8080
#define BUF_SIZE  4096

int recv_line(int sock, char *buf, int size) {
    int total = 0;
    char c;
    while (total < size - 1) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return n;
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[total++] = c;
    }
    buf[total] = '\0';
    return total;
}

/* Tham so truyen vao thread nhan file */
typedef struct {
    int  sock;
    char filename[256];
    long filesize;
} RecvArgs;

/* Thread phu: nhan noi dung file va luu xuong dia */
void *recv_file_thread(void *params) {
    RecvArgs *args = (RecvArgs *)params;

    FILE *fp = fopen(args->filename, "wb");
    if (!fp) { perror("fopen"); return NULL; }

    long remaining = args->filesize;
    char buf[BUF_SIZE];
    while (remaining > 0) {
        int to_read = remaining < BUF_SIZE ? (int)remaining : BUF_SIZE;
        int n = recv(args->sock, buf, to_read, 0);
        if (n <= 0) break;
        fwrite(buf, 1, n, fp);
        remaining -= n;
    }
    fclose(fp);

    printf("[Thread] Luu file thanh cong: %s (%ld bytes)\n",
           args->filename, args->filesize);
    return NULL;
}

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) { perror("socket"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    addr.sin_port        = htons(PORT);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("connect"); close(sock); return 1;
    }
    printf("[Client] Connected to %s:%d\n\n", SERVER_IP, PORT);

    /* Buoc 1: Nhan dong dau OK N hoac ERROR */
    char line[512];
    recv_line(sock, line, sizeof(line));

    if (strncmp(line, "ERROR", 5) == 0) {
        printf("[Client] Server: %s\n", line);
        close(sock);
        return 0;
    }

    int file_count = 0;
    sscanf(line, "OK %d", &file_count);
    printf("=== Danh sach file tren server (%d file) ===\n", file_count);

    /* Nhan danh sach ten file */
    while (1) {
        recv_line(sock, line, sizeof(line));
        if (strlen(line) == 0) break;
        printf("  - %s\n", line);
    }
    printf("\n");

    /* Buoc 2: Vong lap cho den khi tai thanh cong */
    while (1) {
        printf("Nhap ten file muon tai: ");
        char input[256];
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\r\n")] = '\0';
        if (strlen(input) == 0) continue;

        /* Gui ten file */
        char request[270];
        snprintf(request, sizeof(request), "%s\r\n", input);
        send(sock, request, strlen(request), 0);

        /* Nhan phan hoi */
        recv_line(sock, line, sizeof(line));

        if (strncmp(line, "ERROR", 5) == 0) {
            printf("[Client] %s. Vui long nhap lai.\n\n", line);
            continue;
        }

        /* Phan tich kich thuoc */
        long fsize = 0;
        sscanf(line, "OK %ld", &fsize);
        printf("[Client] File size: %ld bytes. Bat dau nhan...\n", fsize);

        /* Tao thread phu nhan file */
        RecvArgs args;
        args.sock = sock;
        args.filesize = fsize;
        strncpy(args.filename, input, 255);

        pthread_t tid;
        pthread_create(&tid, NULL, recv_file_thread, &args);
        pthread_join(tid, NULL);   /* Cho thread nhan xong moi thoat */

        printf("[Client] Hoan thanh tai file: %s\n", input);
        break;
    }

    close(sock);
    return 0;
}