/*******************************************************************************
 * @file    chat_server.c
 * @brief   Chat server nhiều người dùng poll() + multithread
 *          - Nhận kết nối từ nhiều client
 *          - Hỏi tên theo cú pháp "client_id: client_name"
 *          - Broadcast tin nhắn: "YYYY/MM/DD HH:MM:SSAm/PM id: message"
 *******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <time.h>

/* ==================== HẰNG SỐ ==================== */
#define PORT        8080
#define MAX_CLIENTS 64
#define BUF_LEN     512
#define NAME_LEN    32
#define ID_LEN      16

/* ==================== CẤU TRÚC DỮ LIỆU ==================== */

typedef struct {
    int  sock;              // Socket fd (-1 = slot trống)
    char id[ID_LEN];        // client_id
    char name[NAME_LEN];    // client_name
    int  registered;        // 0: chưa đăng ký tên, 1: đã đăng ký
} ClientInfo;

/* ==================== BIẾN TOÀN CỤC ==================== */

static ClientInfo   clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ==================== HÀM TIỆN ÍCH ==================== */

/**
 * @brief Lấy timestamp hiện tại dạng "YYYY/MM/DD HH:MM:SSAM/PM"
 */
void get_timestamp(char *buf, int size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y/%m/%d %I:%M:%S%p", t);
}

/**
 * @brief Tìm slot trống trong mảng clients
 * @return index nếu tìm thấy, -1 nếu đầy
 */
int find_free_slot() {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].sock == -1) return i;
    return -1;
}

/**
 * @brief Broadcast tin nhắn đến tất cả client đã đăng ký,
 *        trừ client có socket = exclude_sock
 */
void broadcast(const char *msg, int exclude_sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sock != -1 &&
            clients[i].registered   &&
            clients[i].sock != exclude_sock)
        {
            send(clients[i].sock, msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/**
 * @brief Xóa client khỏi danh sách và đóng socket
 */
void remove_client(int sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sock == sock) {
            printf("[SERVER] Client '%s' (%s) da ngat ket noi.\n",
                   clients[i].id, clients[i].name);
            close(clients[i].sock);
            clients[i].sock       = -1;
            clients[i].registered = 0;
            memset(clients[i].id,   0, ID_LEN);
            memset(clients[i].name, 0, NAME_LEN);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* ==================== THREAD XỬ LÝ TỪNG CLIENT ==================== */

/**
 * @brief   Thread riêng cho mỗi client.
 *          Bước 1: Hỏi tên đến khi nhận đúng cú pháp "id: name"
 *          Bước 2: Nhận tin nhắn → broadcast đến mọi client khác
 */
void *client_thread(void *params) {
    int sock = *(int *)params;
    free(params);

    char buf[BUF_LEN];
    char my_id[ID_LEN]     = {0};
    char my_name[NAME_LEN] = {0};

    /* -------- BƯỚC 1: Đăng ký tên -------- */
    const char *ask = "[SERVER] Nhap thong tin theo cu phap: client_id: client_name\n"
                      "         (Vi du: abc: Nguyen Van A)\n> ";
    send(sock, ask, strlen(ask), 0);

    while (1) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) { remove_client(sock); return NULL; }
        buf[len] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';   // Xóa newline

        /*
         * Parse cú pháp "client_id: client_name"
         * Tìm ": " đầu tiên làm dấu phân cách
         */
        char *sep = strstr(buf, ": ");
        if (sep == NULL || sep == buf) {
            const char *err = "[SERVER] Sai cu phap! Vui long nhap lai: client_id: client_name\n> ";
            send(sock, err, strlen(err), 0);
            continue;
        }

        /* Tách id và name */
        int id_len = sep - buf;
        if (id_len <= 0 || id_len >= ID_LEN) {
            const char *err = "[SERVER] client_id qua dai hoac rong! Nhap lai:\n> ";
            send(sock, err, strlen(err), 0);
            continue;
        }

        char *name_part = sep + 2;  // Bỏ qua ": "
        /* Kiểm tra name không chứa khoảng trắng (viết liền) */
        if (strlen(name_part) == 0 || strchr(name_part, ' ') != NULL) {
            const char *err = "[SERVER] client_name phai la xau viet lien (khong co khoang trang)!\n> ";
            send(sock, err, strlen(err), 0);
            continue;
        }

        /* Lưu id và name */
        strncpy(my_id,   buf,       id_len);
        strncpy(my_name, name_part, NAME_LEN - 1);
        my_id[id_len] = '\0';

        /* Kiểm tra id đã tồn tại chưa */
        int duplicate = 0;
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock != -1 && clients[i].registered &&
                strcmp(clients[i].id, my_id) == 0)
            {
                duplicate = 1; break;
            }
        }
        if (!duplicate) {
            /* Cập nhật thông tin vào bảng clients */
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].sock == sock) {
                    strncpy(clients[i].id,   my_id,   ID_LEN - 1);
                    strncpy(clients[i].name, my_name, NAME_LEN - 1);
                    clients[i].registered = 1;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (duplicate) {
            const char *err = "[SERVER] client_id nay da ton tai! Chon id khac:\n> ";
            send(sock, err, strlen(err), 0);
            continue;
        }

        /* Đăng ký thành công */
        char welcome[128];
        snprintf(welcome, sizeof(welcome),
                 "[SERVER] Chao mung %s (%s)! Ban co the bat dau chat.\n",
                 my_name, my_id);
        send(sock, welcome, strlen(welcome), 0);
        printf("[SERVER] Client dang ky: id='%s' name='%s' fd=%d\n",
               my_id, my_name, sock);

        /* Thông báo cho các client khác */
        char join_msg[128];
        snprintf(join_msg, sizeof(join_msg),
                 "[SERVER] %s (%s) da tham gia phong chat!\n", my_name, my_id);
        broadcast(join_msg, sock);
        break;
    }

    /* -------- BƯỚC 2: Nhận và broadcast tin nhắn -------- */
    while (1) {
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) break;
        buf[len] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';

        if (strlen(buf) == 0) continue;

        /* Lệnh thoát */
        if (strcmp(buf, "exit") == 0) break;

        /* Format tin nhắn: "YYYY/MM/DD HH:MM:SSAM/PM id: message" */
        char timestamp[32];
        get_timestamp(timestamp, sizeof(timestamp));

        char msg[BUF_LEN + 64];
        snprintf(msg, sizeof(msg), "%s %s: %s\n", timestamp, my_id, buf);

        /* In log ở server */
        printf("[CHAT] %s", msg);

        /* Broadcast đến mọi client khác */
        broadcast(msg, sock);

        /* Gửi lại cho chính mình với nhãn "(ban)" */
        char self_msg[BUF_LEN + 80];
        snprintf(self_msg, sizeof(self_msg),
                 "%s %s (ban): %s\n", timestamp, my_id, buf);
        send(sock, self_msg, strlen(self_msg), 0);
    }

    /* Thông báo thoát cho mọi người */
    char leave_msg[128];
    snprintf(leave_msg, sizeof(leave_msg),
             "[SERVER] %s (%s) da roi phong chat.\n", my_name, my_id);
    broadcast(leave_msg, sock);

    remove_client(sock);
    return NULL;
}

/* ==================== THREAD POLL — QUẢN LÝ KẾT NỐI ==================== */

/**
 * @brief   Thread dùng poll() để theo dõi listener socket.
 *          Khi có kết nối mới → accept → tạo client_thread.
 *          (poll() ở đây giám sát listener; mỗi client có thread riêng)
 */
typedef struct { int listener; } PollArgs;

void *poll_thread(void *params) {
    int listener = ((PollArgs *)params)->listener;
    free(params);

    struct pollfd pfd;
    pfd.fd     = listener;
    pfd.events = POLLIN;

    while (1) {
        int ret = poll(&pfd, 1, -1);   // Chờ vô thời hạn
        if (ret < 0) { perror("poll()"); break; }

        if (pfd.revents & POLLIN) {
            int *csock = malloc(sizeof(int));
            *csock = accept(listener, NULL, NULL);
            if (*csock < 0) { free(csock); perror("accept()"); continue; }

            pthread_mutex_lock(&clients_mutex);
            int slot = find_free_slot();
            if (slot == -1) {
                pthread_mutex_unlock(&clients_mutex);
                const char *full = "[SERVER] Phong chat day! Thu lai sau.\n";
                send(*csock, full, strlen(full), 0);
                close(*csock); free(csock);
                continue;
            }
            clients[slot].sock       = *csock;
            clients[slot].registered = 0;
            pthread_mutex_unlock(&clients_mutex);

            printf("[SERVER] Client moi ket noi: fd=%d\n", *csock);

            pthread_t tid;
            pthread_create(&tid, NULL, client_thread, csock);
            pthread_detach(tid);
        }
    }
    return NULL;
}

/* ==================== HÀM MAIN ==================== */

int main() {
    /* Khởi tạo bảng clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sock       = -1;
        clients[i].registered = 0;
    }

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) { perror("socket()"); return 1; }

    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)))
        { perror("bind()"); return 1; }
    if (listen(listener, 10))
        { perror("listen()"); return 1; }

    printf("=================================================\n");
    printf("  Chat Server dang chay tren port %d\n", PORT);
    printf("  Su dung poll() + multithread\n");
    printf("  Cu phap dang ky: client_id: client_name\n");
    printf("=================================================\n");

    /* Khởi động poll thread */
    PollArgs *pargs = malloc(sizeof(PollArgs));
    pargs->listener = listener;

    pthread_t ptid;
    pthread_create(&ptid, NULL, poll_thread, pargs);
    pthread_join(ptid, NULL);   // Chờ mãi

    close(listener);
    return 0;
}