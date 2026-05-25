/*******************************************************************************
 * @file    03_02_client.c
 * @brief   Client chat nhóm 2 người
 *          - Người gửi thấy:   "Ban: tin nhan"
 *          - Người nhận thấy:  "TenNguoiGui: tin nhan"
 *******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

volatile int running = 1;

/* ==================== THREAD NHẬN TIN NHẮN ==================== */

/**
 * Nhận dữ liệu từ server và in ra màn hình.
 * Server đã format sẵn: "TenNguoiGui: noidung\n"
 * hoặc tin nhắn hệ thống: "[SERVER] ..."
 */
void *recv_thread(void *params) {
    int sock = *(int *)params;
    char buf[600];

    while (running) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            printf("\n[!] Mat ket noi den server.\n");
            running = 0;
            break;
        }
        buf[len] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }
    return NULL;
}

/* ==================== HÀM MAIN ==================== */

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) { perror("socket()"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port        = htons(8080);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("connect()"); close(sock); return 1;
    }

    printf("=================================================\n");
    printf("  Ket noi den server thanh cong!\n");
    printf("  Go 'exit' de thoat.\n");
    printf("=================================================\n");

    /* Khởi động thread nhận song song (để nhận prompt tên từ server) */
    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &sock);
    pthread_detach(tid);

    char buf[512];
    int  is_first = 1;   // Lần nhập đầu tiên = nhập tên

    while (running) {
        if (fgets(buf, sizeof(buf), stdin) == NULL) break;

        /* Gửi lên server */
        send(sock, buf, strlen(buf), 0);

        if (is_first) {
            /* Lần đầu là nhập tên, không hiển thị lại */
            is_first = 0;
            continue;
        }

        /* Kiểm tra lệnh thoát */
        if (strncmp(buf, "exit", 4) == 0) {
            printf("[!] Ban da thoat.\n");
            running = 0;
            break;
        }

        /* Hiển thị lại tin nhắn mình vừa gửi với nhãn "Ban:" */
        char trimmed[512];
        strncpy(trimmed, buf, sizeof(trimmed) - 1);
        trimmed[strcspn(trimmed, "\r\n")] = '\0';
        printf("Ban: %s\n", trimmed);
        fflush(stdout);
    }

    close(sock);
    return 0;
}