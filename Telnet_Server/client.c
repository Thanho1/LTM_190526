/*******************************************************************************
 * @file    telnet_client.c
 * @brief   Telnet Client - kết nối đến telnet_server
 *          - Thread recv: nhận và in dữ liệu từ server liên tục
 *          - Main thread: đọc input từ bàn phím và gửi lên server
 * @compile gcc telnet_client.c -o telnet_client -lpthread
 * @run     ./telnet_client [ip] [port]
 *          ./telnet_client 127.0.0.1 2323
 *******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 2323
#define BUF_LEN 2048

volatile int running = 1;

/* ==================== THREAD NHẬN DỮ LIỆU ==================== */

/**
 * @brief Liên tục nhận dữ liệu từ server và in ra màn hình
 */
void *recv_thread(void *params)
{
    int sock = *(int *)params;
    char buf[BUF_LEN];

    while (running)
    {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0)
        {
            if (running)
                printf("\n[!] Server da dong ket noi.\n");
            running = 0;
            break;
        }
        buf[len] = '\0';

        /* Lọc ký tự \r để hiển thị gọn trên terminal Linux */
        for (int i = 0; i < len; i++)
        {
            if (buf[i] != '\r')
                putchar(buf[i]);
        }
        fflush(stdout);
    }
    return NULL;
}

/* ==================== HÀM MAIN ==================== */

int main(int argc, char *argv[])
{
    const char *ip = (argc >= 2) ? argv[1] : DEFAULT_IP;
    int port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1)
    {
        perror("socket()");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);

    printf("Dang ket noi den %s:%d ...\n", ip, port);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)))
    {
        perror("connect()");
        close(sock);
        return 1;
    }

    printf("Ket noi thanh cong! (Go 'exit' de thoat)\n\n");

    /* Khởi động thread nhận */
    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &sock);
    pthread_detach(tid);

    /* Vòng lặp gửi lệnh */
    char buf[BUF_LEN];
    while (running)
    {
        if (fgets(buf, sizeof(buf), stdin) == NULL)
            break;

        /* Gửi lên server (giữ nguyên '\n') */
        send(sock, buf, strlen(buf), 0);

        /* Kiểm tra lệnh thoát phía client */
        char trimmed[BUF_LEN];
        strncpy(trimmed, buf, sizeof(trimmed) - 1);
        trimmed[strcspn(trimmed, "\r\n")] = '\0';

        if (strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "logout") == 0)
        {
            running = 0;
            break;
        }
    }

    sleep(1); // Chờ recv_thread in nốt phản hồi cuối
    close(sock);
    printf("\n[!] Da ngat ket noi.\n");
    return 0;
}