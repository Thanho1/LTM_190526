#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 8080
#define BUF_LEN 4096

int main(int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : "/";
    const char *ip = (argc >= 3) ? argv[2] : DEFAULT_IP;
    int port = (argc >= 4) ? atoi(argv[3]) : DEFAULT_PORT;

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

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)))
    {
        perror("connect()");
        close(sock);
        return 1;
    }

    // Gửi HTTP GET request
    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\nHost: %s:%d\r\n"
             "User-Agent: SimpleClient/1.0\r\nConnection: close\r\n\r\n",
             path, ip, port);

    printf("===== REQUEST =====\n%s", request);
    send(sock, request, strlen(request), 0);

    // Nhận và in response
    printf("===== RESPONSE =====\n");
    char buf[BUF_LEN];
    int n, total = 0;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0)
    {
        buf[n] = '\0';
        // Lọc \r để hiển thị gọn
        for (int i = 0; i < n; i++)
            if (buf[i] != '\r')
                putchar(buf[i]);
        total += n;
    }

    printf("\n===== Tong: %d bytes =====\n", total);
    close(sock);
    return 0;
}