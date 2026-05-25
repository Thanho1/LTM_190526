#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define PORT 8080
#define THREAD_POOL_SIZE 8
#define QUEUE_SIZE 64
#define BUF_LEN 4096
#define WEB_ROOT "www"

/* ===== CIRCULAR QUEUE ===== */

typedef struct
{
    int data[QUEUE_SIZE];
    int head, tail, count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} SocketQueue;

static SocketQueue queue;
static pthread_t pool[THREAD_POOL_SIZE];

void queue_init(SocketQueue *q)
{
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void queue_push(SocketQueue *q, int sock)
{
    pthread_mutex_lock(&q->mutex);
    while (q->count == QUEUE_SIZE)
        pthread_cond_wait(&q->not_full, &q->mutex);
    q->data[q->tail] = sock;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

int queue_pop(SocketQueue *q)
{
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->mutex);
    int sock = q->data[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return sock;
}

/* ===== HTTP HELPERS ===== */

void get_http_date(char *buf, int size)
{
    time_t now = time(NULL);
    strftime(buf, size, "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
}

const char *get_mime(const char *path)
{
    const char *e = strrchr(path, '.');
    if (!e)
        return "text/plain";
    if (!strcmp(e, ".html"))
        return "text/html";
    if (!strcmp(e, ".css"))
        return "text/css";
    if (!strcmp(e, ".js"))
        return "application/javascript";
    if (!strcmp(e, ".png"))
        return "image/png";
    if (!strcmp(e, ".jpg") || !strcmp(e, ".jpeg"))
        return "image/jpeg";
    if (!strcmp(e, ".ico"))
        return "image/x-icon";
    return "text/plain";
}

void send_error(int sock, int code, const char *reason, const char *body)
{
    char date[64], res[1024];
    get_http_date(date, sizeof(date));
    int len = snprintf(res, sizeof(res),
                       "HTTP/1.1 %d %s\r\nDate: %s\r\nContent-Type: text/html\r\n"
                       "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
                       code, reason, date, strlen(body), body);
    send(sock, res, len, 0);
}

void send_file(int sock, const char *filepath, const char *mime)
{
    struct stat st;
    if (stat(filepath, &st) < 0)
    {
        send_error(sock, 500, "Internal Server Error", "<h1>500</h1>");
        return;
    }
    char date[64], header[512];
    get_http_date(date, sizeof(date));
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\nDate: %s\r\nContent-Type: %s\r\n"
                        "Content-Length: %ld\r\nConnection: close\r\n\r\n",
                        date, mime, (long)st.st_size);
    send(sock, header, hlen, 0);

    FILE *fp = fopen(filepath, "rb");
    if (!fp)
        return;
    char buf[BUF_LEN];
    int n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        send(sock, buf, n, 0);
    fclose(fp);
}

/* ===== REQUEST HANDLER ===== */

void handle_request(int sock)
{
    char buf[BUF_LEN] = {0};
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    if (len <= 0)
        return;

    char method[8], path[256], version[16];
    sscanf(buf, "%7s %255s %15s", method, path, version);

    if (strcmp(method, "GET") != 0)
    {
        send_error(sock, 405, "Method Not Allowed", "<h1>405</h1>");
        return;
    }

    if (strcmp(path, "/") == 0)
        strcpy(path, "/index.html");

    if (strstr(path, ".."))
    {
        send_error(sock, 403, "Forbidden", "<h1>403 Forbidden</h1>");
        return;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s%s", WEB_ROOT, path);
    printf("[HTTP] %s %s\n", method, filepath);

    struct stat st;
    if (stat(filepath, &st) < 0 || S_ISDIR(st.st_mode))
    {
        char body[512];
        snprintf(body, sizeof(body),
                 "<html><body><h1>404 Not Found</h1>"
                 "<p>Khong tim thay: %s</p></body></html>",
                 path);
        send_error(sock, 404, "Not Found", body);
        return;
    }

    send_file(sock, filepath, get_mime(filepath));
}

/* ===== WORKER THREAD ===== */

void *worker_thread(void *params)
{
    while (1)
    {
        int sock = queue_pop(&queue);
        handle_request(sock);
        close(sock);
    }
    return NULL;
}

/* ===== MAIN ===== */

int main()
{
    // Tạo thư mục www và index.html mặc định nếu chưa có
    mkdir(WEB_ROOT, 0755);
    char index_path[64];
    snprintf(index_path, sizeof(index_path), "%s/index.html", WEB_ROOT);
    struct stat st;
    if (stat(index_path, &st) != 0)
    {
        FILE *fp = fopen(index_path, "w");
        if (fp)
        {
            fprintf(fp,
                    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                    "<title>HTTP Server</title></head><body>"
                    "<h1>Xin chao cac ban!</h1>"
                    "<p>HTTP Server dang chay voi Pre-threading (%d workers).</p>"
                    "</body></html>\n",
                    THREAD_POOL_SIZE);
            fclose(fp);
        }
    }

    // Khởi tạo queue và thread pool
    queue_init(&queue);
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_create(&pool[i], NULL, worker_thread, NULL);

    // Tạo listener socket
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)))
    {
        perror("bind()");
        return 1;
    }
    if (listen(listener, THREAD_POOL_SIZE * 2))
    {
        perror("listen()");
        return 1;
    }

    printf("HTTP Server (Pre-threading) - port %d - %d workers\n", PORT, THREAD_POOL_SIZE);
    printf("Web root : %s/\n", WEB_ROOT);
    printf("Truy cap : http://127.0.0.1:%d\n", PORT);

    // Vòng lặp chính: accept → đẩy vào queue
    while (1)
    {
        int client = accept(listener, NULL, NULL);
        if (client < 0)
        {
            perror("accept()");
            continue;
        }
        queue_push(&queue, client);
    }

    close(listener);
    return 0;
}