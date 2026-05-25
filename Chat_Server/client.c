/*******************************************************************************
 * @file    chat_client.c
 * @brief   Client cho chat server nhiều người
 *          - Đăng ký theo cú pháp "client_id: client_name"
 *          - Gửi/nhận tin nhắn song song bằng 2 thread
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
    printf("  Ket noi den chat server thanh cong!\n");
    printf("  Dang ky: client_id: client_name\n");
    printf("  Go 'exit' de thoat.\n");
    printf("=================================================\n");

    /* Thread nhận chạy song song */
    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &sock);
    pthread_detach(tid);

    char buf[512];
    while (running) {
        if (fgets(buf, sizeof(buf), stdin) == NULL) break;

        send(sock, buf, strlen(buf), 0);

        if (strncmp(buf, "exit", 4) == 0) {
            running = 0;
            break;
        }
    }

    close(sock);
    return 0;
}