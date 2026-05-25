/*******************************************************************************
 * @file    time_server.c
 * @brief   Time Server sử dụng multithread TCP Socket
 * @date
 *******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 256

void *client_thread(void *params);
void get_time_by_format(char *format, char *result);

int main()
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listener == -1)
    {
        perror("socket() failed");
        return 1;
    }

    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               &(int){1}, sizeof(int));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)))
    {
        perror("bind() failed");
        close(listener);
        return 1;
    }

    if (listen(listener, 5))
    {
        perror("listen() failed");
        close(listener);
        return 1;
    }

    printf("Time Server listening on port %d...\n", PORT);

    while (1)
    {
        int client = accept(listener, NULL, NULL);

        if (client < 0)
            continue;

        printf("New client connected: %d\n", client);

        pthread_t tid;

        int *pclient = malloc(sizeof(int));
        *pclient = client;

        pthread_create(&tid, NULL, client_thread, pclient);
        pthread_detach(tid);
    }

    close(listener);
    return 0;
}

void *client_thread(void *params)
{
    int client = *(int *)params;
    free(params);

    char buf[BUFFER_SIZE];

    while (1)
    {
        int len = recv(client, buf, sizeof(buf) - 1, 0);

        if (len <= 0)
            break;

        buf[len] = 0;

        printf("Received from client %d: %s\n", client, buf);

        char command[50];
        char format[50];

        int ret = sscanf(buf, "%s %s", command, format);

        if (ret != 2 || strcmp(command, "GET_TIME") != 0)
        {
            char *msg = "ERROR: Invalid command\n";
            send(client, msg, strlen(msg), 0);
            continue;
        }

        char result[100];

        get_time_by_format(format, result);

        if (strcmp(result, "INVALID") == 0)
        {
            char *msg = "ERROR: Unsupported format\n";
            send(client, msg, strlen(msg), 0);
        }
        else
        {
            strcat(result, "\n");
            send(client, result, strlen(result), 0);
        }
    }

    printf("Client %d disconnected\n", client);

    close(client);
    return NULL;
}

void get_time_by_format(char *format, char *result)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);

    if (strcmp(format, "dd/mm/yyyy") == 0)
    {
        strftime(result, 100, "%d/%m/%Y", tm_info);
    }
    else if (strcmp(format, "dd/mm/yy") == 0)
    {
        strftime(result, 100, "%d/%m/%y", tm_info);
    }
    else if (strcmp(format, "mm/dd/yyyy") == 0)
    {
        strftime(result, 100, "%m/%d/%Y", tm_info);
    }
    else if (strcmp(format, "mm/dd/yy") == 0)
    {
        strftime(result, 100, "%m/%d/%y", tm_info);
    }
    else
    {
        strcpy(result, "INVALID");
    }
}