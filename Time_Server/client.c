/*******************************************************************************
 * @file    time_client.c
 * @brief   Time Client sử dụng multithread TCP Socket
 * @date
 *******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 256

void *receive_thread(void *params);

int main() {

    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (client == -1) {
        perror("socket() failed");
        return 1;
    }

    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("connect() failed");
        close(client);
        return 1;
    }

    printf("Connected to Time Server...\n");

    printf("\n========== HUONG DAN SU DUNG ==========\n");

    printf("Cac lenh hop le:\n\n");

    printf("1. GET_TIME dd/mm/yyyy\n");
    printf("   Vi du: GET_TIME dd/mm/yyyy\n");
    printf("   Ket qua: 20/05/2026\n\n");

    printf("2. GET_TIME dd/mm/yy\n");
    printf("   Vi du: GET_TIME dd/mm/yy\n");
    printf("   Ket qua: 20/05/26\n\n");

    printf("3. GET_TIME mm/dd/yyyy\n");
    printf("   Vi du: GET_TIME mm/dd/yyyy\n");
    printf("   Ket qua: 05/20/2026\n\n");

    printf("4. GET_TIME mm/dd/yy\n");
    printf("   Vi du: GET_TIME mm/dd/yy\n");
    printf("   Ket qua: 05/20/26\n\n");

    printf("Nhap 'exit' de thoat chuong trinh.\n");

    printf("=======================================\n\n");

    pthread_t tid;
    pthread_create(&tid, NULL, receive_thread, &client);

    char buf[BUFFER_SIZE];

    while (1) {

        printf("Nhap lenh: ");
        fflush(stdout);

        fgets(buf, sizeof(buf), stdin);

        send(client, buf, strlen(buf), 0);

        if (strncmp(buf, "exit", 4) == 0)
            break;
    }

    close(client);

    return 0;
}

void *receive_thread(void *params) {

    int client = *(int *)params;

    char buf[BUFFER_SIZE];

    while (1) {

        int len = recv(client, buf, sizeof(buf) - 1, 0);

        if (len <= 0)
            break;

        buf[len] = 0;

        printf("\r");
        printf("Server response: %s\n", buf);

        fflush(stdout);
    }

    return NULL;
}