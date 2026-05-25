/*******************************************************************************
 * @file    03_02_server.c
 * @brief   Server chat nhóm 2 người - ghép cặp client và chuyển tiếp tin nhắn
 *          Hiển thị: "TenNguoiGui: tin nhan" ở phía người nhận
 *******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define NAME_LEN 32
#define BUF_LEN  512

/* ==================== CẤU TRÚC DỮ LIỆU ==================== */

typedef struct {
    int  sock;
    char name[NAME_LEN];
} Client;

typedef struct {
    Client client1;
    Client client2;
} ChatPair;

/* ==================== BIẾN TOÀN CỤC ==================== */

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Client đang chờ ghép cặp */
static int  waiting_sock = -1;
static char waiting_name[NAME_LEN] = {0};

/* ==================== KHAI BÁO HÀM ==================== */

void *chat_thread(void *params);
void *client_handler(void *params);

/* ==================== HÀM MAIN ==================== */

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) { perror("socket()"); return 1; }

    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(8080);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)))  { perror("bind()");   return 1; }
    if (listen(listener, 5))                                      { perror("listen()"); return 1; }

    printf("=================================================\n");
    printf("  Server chat nhom 2 nguoi dang lang nghe...\n");
    printf("  Port: 8080\n");
    printf("=================================================\n");

    while (1) {
        int *csock = malloc(sizeof(int));
        *csock = accept(listener, NULL, NULL);
        if (*csock < 0) { free(csock); continue; }

        printf("[SERVER] Client moi ket noi: fd = %d\n", *csock);
        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, csock);
        pthread_detach(tid);
    }
    close(listener);
    return 0;
}

/* ==================== XỬ LÝ CLIENT MỚI ==================== */

void *client_handler(void *params) {
    int sock = *(int *)params;
    free(params);

    /* --- Hỏi tên người dùng --- */
    const char *ask = "Nhap ten cua ban: ";
    send(sock, ask, strlen(ask), 0);

    char name[NAME_LEN] = {0};
    int len = recv(sock, name, sizeof(name) - 1, 0);
    if (len <= 0) { close(sock); return NULL; }

    /* Xóa ký tự newline nếu có */
    name[strcspn(name, "\r\n")] = '\0';
    if (strlen(name) == 0) snprintf(name, NAME_LEN, "Client%d", sock);

    printf("[SERVER] Client %d dat ten: %s\n", sock, name);

    pthread_mutex_lock(&queue_mutex);

    if (waiting_sock == -1) {
        /* Chưa có ai chờ → vào hàng đợi */
        waiting_sock = sock;
        strncpy(waiting_name, name, NAME_LEN - 1);
        pthread_mutex_unlock(&queue_mutex);

        printf("[SERVER] %s (fd=%d) dang cho cap...\n", name, sock);
        char msg[128];
        snprintf(msg, sizeof(msg), "[SERVER] Xin chao %s! Dang cho nguoi chat thu 2...\n", name);
        send(sock, msg, strlen(msg), 0);

    } else {
        /* Đã có người chờ → ghép cặp */
        ChatPair *pair = malloc(sizeof(ChatPair));
        pair->client1.sock = waiting_sock;
        strncpy(pair->client1.name, waiting_name, NAME_LEN - 1);
        pair->client2.sock = sock;
        strncpy(pair->client2.name, name, NAME_LEN - 1);

        waiting_sock = -1;
        memset(waiting_name, 0, NAME_LEN);
        pthread_mutex_unlock(&queue_mutex);

        printf("[SERVER] Ghep cap: %s (fd=%d) <-> %s (fd=%d)\n",
               pair->client1.name, pair->client1.sock,
               pair->client2.name, pair->client2.sock);

        /* Thông báo cho cả 2 biết tên đối phương */
        char msg1[128], msg2[128];
        snprintf(msg1, sizeof(msg1),
                 "[SERVER] Da ghep cap! Ban dang chat voi: %s\n", pair->client2.name);
        snprintf(msg2, sizeof(msg2),
                 "[SERVER] Da ghep cap! Ban dang chat voi: %s\n", pair->client1.name);
        send(pair->client1.sock, msg1, strlen(msg1), 0);
        send(pair->client2.sock, msg2, strlen(msg2), 0);

        pthread_t tid;
        pthread_create(&tid, NULL, chat_thread, pair);
        pthread_detach(tid);
    }

    return NULL;
}

/* ==================== RELAY TIN NHẮN ==================== */

typedef struct {
    int  from_sock;
    char from_name[NAME_LEN];   // Tên người GỬI
    int  to_sock;
    char to_name[NAME_LEN];     // Tên người NHẬN
    int             *alive;
    pthread_mutex_t *alive_mutex;
} RelayArgs;

void *relay_one_way(void *params) {
    RelayArgs *args = (RelayArgs *)params;
    char raw[BUF_LEN];
    char formatted[BUF_LEN + NAME_LEN + 4];

    while (1) {
        int len = recv(args->from_sock, raw, sizeof(raw) - 1, 0);
        if (len <= 0) {
            printf("[SERVER] %s (fd=%d) da ngat ket noi.\n",
                   args->from_name, args->from_sock);
            break;
        }
        raw[len] = '\0';

        if (strncmp(raw, "exit", 4) == 0) {
            printf("[SERVER] %s gui lenh thoat.\n", args->from_name);
            break;
        }

        /* Xóa newline cuối để format gọn */
        raw[strcspn(raw, "\r\n")] = '\0';

        /*
         * Định dạng tin nhắn gửi sang phía đối phương:
         *   "TenNguoiGui: noi dung\n"
         */
        int flen = snprintf(formatted, sizeof(formatted),
                            "%s: %s\n", args->from_name, raw);

        send(args->to_sock, formatted, flen, 0);

        /* In log ở server */
        printf("[%s -> %s]: %s\n", args->from_name, args->to_name, raw);
    }

    pthread_mutex_lock(args->alive_mutex);
    *(args->alive) = 0;
    pthread_mutex_unlock(args->alive_mutex);

    shutdown(args->from_sock, SHUT_RDWR);
    shutdown(args->to_sock,   SHUT_RDWR);
    return NULL;
}

void *chat_thread(void *params) {
    ChatPair *pair = (ChatPair *)params;
    int  c1 = pair->client1.sock,  c2 = pair->client2.sock;
    char n1[NAME_LEN], n2[NAME_LEN];
    strncpy(n1, pair->client1.name, NAME_LEN - 1);
    strncpy(n2, pair->client2.name, NAME_LEN - 1);
    free(params);

    int alive = 1;
    pthread_mutex_t alive_mutex = PTHREAD_MUTEX_INITIALIZER;

    RelayArgs *a1 = malloc(sizeof(RelayArgs));
    a1->from_sock = c1;  strncpy(a1->from_name, n1, NAME_LEN - 1);
    a1->to_sock   = c2;  strncpy(a1->to_name,   n2, NAME_LEN - 1);
    a1->alive = &alive;  a1->alive_mutex = &alive_mutex;

    RelayArgs *a2 = malloc(sizeof(RelayArgs));
    a2->from_sock = c2;  strncpy(a2->from_name, n2, NAME_LEN - 1);
    a2->to_sock   = c1;  strncpy(a2->to_name,   n1, NAME_LEN - 1);
    a2->alive = &alive;  a2->alive_mutex = &alive_mutex;

    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, relay_one_way, a1);
    pthread_create(&tid2, NULL, relay_one_way, a2);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    const char *bye = "[SERVER] Nguoi chat kia da roi. Ket noi ket thuc.\n";
    send(c1, bye, strlen(bye), 0);
    send(c2, bye, strlen(bye), 0);

    close(c1); close(c2);
    printf("[SERVER] Phien chat giua %s va %s da ket thuc.\n", n1, n2);

    pthread_mutex_destroy(&alive_mutex);
    free(a1); free(a2);
    return NULL;
}