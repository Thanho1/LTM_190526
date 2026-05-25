/*******************************************************************************
 * @file    telnet_server.c
 * @brief   Telnet Server dùng poll() + multithread
 *          - Xác thực user/pass từ file users.txt
 *          - Nhận lệnh từ client, thực thi và trả kết quả
 * @compile gcc telnet_server.c -o telnet_server -lpthread
 * @run     ./telnet_server
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

/* ==================== HẰNG SỐ ==================== */
#define PORT 2323 // Port telnet (telnet chuẩn = 23, dùng 2323 để không cần root)
#define MAX_CLIENTS 32
#define BUF_LEN 1024
#define DB_FILE "users.txt"
#define OUT_FILE "out.txt"
#define MAX_LOGIN_TRY 3 // Số lần thử đăng nhập tối đa

/* ==================== CẤU TRÚC DỮ LIỆU ==================== */

typedef struct
{
    int sock;          // Socket fd (-1 = slot trống)
    int authenticated; // 0 = chưa login, 1 = đã login
} ClientInfo;

/* ==================== BIẾN TOÀN CỤC ==================== */

static ClientInfo clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ==================== HÀM TIỆN ÍCH ==================== */

/**
 * @brief Gửi chuỗi đến client
 */
void send_str(int sock, const char *msg)
{
    send(sock, msg, strlen(msg), 0);
}

/**
 * @brief Nhận 1 dòng từ client (kết thúc bởi '\n')
 *        Trả về số byte đọc được, 0 nếu ngắt kết nối
 */
int recv_line(int sock, char *buf, int size)
{
    int total = 0;
    char c;
    while (total < size - 1)
    {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0)
            return 0; // Ngắt kết nối
        if (c == '\n')
            break;
        if (c == '\r')
            continue; // Bỏ qua '\r' (telnet gửi \r\n)
        buf[total++] = c;
    }
    buf[total] = '\0';
    return total;
}

/**
 * @brief Kiểm tra user/pass trong file DB_FILE
 * @return 1 nếu hợp lệ, 0 nếu sai
 */
int check_login(const char *username, const char *password)
{
    FILE *fp = fopen(DB_FILE, "r");
    if (!fp)
    {
        perror("[SERVER] Khong mo duoc file " DB_FILE);
        return 0;
    }

    char line[256];
    char db_user[64], db_pass[64];
    int found = 0;

    while (fgets(line, sizeof(line), fp))
    {
        /* Bỏ newline */
        line[strcspn(line, "\r\n")] = '\0';

        /* Bỏ dòng trống hoặc comment (#) */
        if (strlen(line) == 0 || line[0] == '#')
            continue;

        if (sscanf(line, "%63s %63s", db_user, db_pass) == 2)
        {
            if (strcmp(db_user, username) == 0 &&
                strcmp(db_pass, password) == 0)
            {
                found = 1;
                break;
            }
        }
    }

    fclose(fp);
    return found;
}

/**
 * @brief Thực thi lệnh shell, ghi kết quả vào OUT_FILE, đọc lại và gửi cho client
 */
void execute_command(int sock, const char *cmd)
{
    /* Tạo chuỗi: "cmd > out.txt 2>&1" để bắt cả stderr */
    /* Dung file rieng cho moi socket tranh race condition */
    char out_file[64];
    snprintf(out_file, sizeof(out_file), "out_%d.txt", sock);

    char full_cmd[BUF_LEN + 64];
    snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, out_file);

    /* Thực thi lệnh */
    int ret = system(full_cmd);

    /* Đọc file kết quả và gửi về client */
    FILE *fp = fopen(out_file, "r");
    if (!fp)
    {
        send_str(sock, "[ERROR] Khong doc duoc ket qua lenh.\r\n");
        return;
    }

    char buf[BUF_LEN];
    int sent_any = 0;

    while (fgets(buf, sizeof(buf), fp))
    {
        /* Thêm '\r' trước '\n' cho chuẩn telnet */
        int len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
        {
            buf[len - 1] = '\0';
            send_str(sock, buf);
            send_str(sock, "\r\n");
        }
        else
        {
            send_str(sock, buf);
        }
        sent_any = 1;
    }
    fclose(fp);

    if (!sent_any)
    {
        if (ret == 0)
            send_str(sock, "[OK] Lenh thuc hien thanh cong (khong co output).\r\n");
        else
            send_str(sock, "[ERROR] Lenh that bai hoac khong co output.\r\n");
    }

    /* Xóa file tạm */
    remove(out_file);
}

/**
 * @brief Xóa client khỏi danh sách
 */
void remove_client(int sock)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].sock == sock)
        {
            close(clients[i].sock);
            clients[i].sock = -1;
            clients[i].authenticated = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    printf("[SERVER] Client fd=%d da ngat ket noi.\n", sock);
}

/* ==================== THREAD XỬ LÝ TỪNG CLIENT ==================== */

/**
 * @brief Thread riêng cho mỗi client
 *
 *  Giai đoạn 1: Xác thực (tối đa MAX_LOGIN_TRY lần)
 *    - Hỏi username → password
 *    - Tra cứu file users.txt
 *    - Đúng → chuyển giai đoạn 2 | Sai → thử lại / ngắt
 *
 *  Giai đoạn 2: Nhận và thực thi lệnh
 *    - Nhận lệnh từ client
 *    - system("lenh > out.txt 2>&1")
 *    - Đọc out.txt gửi kết quả về client
 *    - "logout" / "exit" → ngắt kết nối
 */
void *client_thread(void *params)
{
    int sock = *(int *)params;
    free(params);

    char username[64] = {0};
    char password[64] = {0};
    char buf[BUF_LEN];

    /* ======== GIAI ĐOẠN 1: XÁC THỰC ======== */
    send_str(sock,
             "================================================\r\n"
             "          TELNET SERVER - DANG NHAP             \r\n"
             "================================================\r\n");

    int login_ok = 0;
    int try_count = 0;

    while (try_count < MAX_LOGIN_TRY)
    {
        try_count++;

        /* Hỏi username */
        send_str(sock, "Username: ");
        if (recv_line(sock, username, sizeof(username)) == 0)
            goto disconnect;

        /* Hỏi password */
        send_str(sock, "Password: ");
        if (recv_line(sock, password, sizeof(password)) == 0)
            goto disconnect;

        printf("[SERVER] fd=%d dang nhap: user='%s'\n", sock, username);

        if (check_login(username, password))
        {
            login_ok = 1;
            char welcome[256];
            snprintf(welcome, sizeof(welcome),
                     "\r\n[OK] Dang nhap thanh cong! Chao mung %s.\r\n"
                     "Go 'logout' hoac 'exit' de thoat.\r\n"
                     "------------------------------------------------\r\n",
                     username);
            send_str(sock, welcome);
            printf("[SERVER] fd=%d dang nhap thanh cong: user='%s'\n", sock, username);
            break;
        }
        else
        {
            char err[128];
            int remaining = MAX_LOGIN_TRY - try_count;
            if (remaining > 0)
                snprintf(err, sizeof(err),
                         "[LOI] Sai tai khoan hoac mat khau! Con %d lan thu.\r\n\r\n",
                         remaining);
            else
                snprintf(err, sizeof(err),
                         "[LOI] Qua so lan thu! Dong ket noi.\r\n");
            send_str(sock, err);
            printf("[SERVER] fd=%d dang nhap that bai lan %d.\n", sock, try_count);
        }
    }

    if (!login_ok)
        goto disconnect;

    /* ======== GIAI ĐOẠN 2: NHẬN VÀ THỰC THI LỆNH ======== */
    while (1)
    {
        /* Hiện prompt */
        char prompt[80];
        snprintf(prompt, sizeof(prompt), "%s> ", username);
        send_str(sock, prompt);

        /* Nhận lệnh */
        if (recv_line(sock, buf, sizeof(buf)) == 0)
            break;

        /* Bỏ qua lệnh rỗng */
        if (strlen(buf) == 0)
            continue;

        printf("[SERVER] fd=%d [%s] lenh: %s\n", sock, username, buf);

        /* Kiểm tra lệnh thoát */
        if (strcmp(buf, "logout") == 0 || strcmp(buf, "exit") == 0)
        {
            send_str(sock, "[OK] Tam biet! Dong ket noi.\r\n");
            break;
        }

        /* Chặn lệnh nguy hiểm */
        if (strstr(buf, "rm ") != NULL ||
            strstr(buf, "mkfs") != NULL ||
            strstr(buf, "dd ") != NULL ||
            strstr(buf, ":(){ ") != NULL)
        {
            send_str(sock, "[TU CHOI] Lenh nay khong duoc phep thuc thi.\r\n");
            continue;
        }

        /* Thực thi lệnh và gửi kết quả */
        send_str(sock, "--- KET QUA ---\r\n");
        execute_command(sock, buf);
        send_str(sock, "--- HET ---\r\n");
    }

disconnect:
    remove_client(sock);
    return NULL;
}

/* ==================== POLL THREAD — GIÁM SÁT LISTENER ==================== */

void *poll_thread(void *params)
{
    int listener = *(int *)params;
    free(params);

    struct pollfd pfd = {.fd = listener, .events = POLLIN};

    while (1)
    {
        int ret = poll(&pfd, 1, -1);
        if (ret < 0)
        {
            perror("poll()");
            break;
        }

        if (!(pfd.revents & POLLIN))
            continue;

        int *csock = malloc(sizeof(int));
        *csock = accept(listener, NULL, NULL);
        if (*csock < 0)
        {
            perror("accept()");
            free(csock);
            continue;
        }

        /* Tìm slot trống */
        pthread_mutex_lock(&clients_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].sock == -1)
            {
                slot = i;
                break;
            }
        }
        if (slot == -1)
        {
            pthread_mutex_unlock(&clients_mutex);
            send(*csock, "[ERROR] Server day! Thu lai sau.\r\n", 33, 0);
            close(*csock);
            free(csock);
            continue;
        }
        clients[slot].sock = *csock;
        clients[slot].authenticated = 0;
        pthread_mutex_unlock(&clients_mutex);

        printf("[SERVER] Client moi ket noi: fd=%d\n", *csock);

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, csock);
        pthread_detach(tid);
    }
    return NULL;
}

/* ==================== HÀM MAIN ==================== */

int main()
{
    /* Khởi tạo bảng clients */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].sock = -1;
        clients[i].authenticated = 0;
    }

    /* Kiểm tra file users.txt */
    FILE *fp = fopen(DB_FILE, "r");
    if (!fp)
    {
        fprintf(stderr, "[ERROR] Khong tim thay file '%s'!\n"
                        "        Tao file voi dinh dang: username password\n",
                DB_FILE);
        return 1;
    }
    fclose(fp);

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1)
    {
        perror("socket()");
        return 1;
    }

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
    if (listen(listener, 10))
    {
        perror("listen()");
        return 1;
    }

    printf("=================================================\n");
    printf("  Telnet Server dang chay tren port %d\n", PORT);
    printf("  File co so du lieu: %s\n", DB_FILE);
    printf("  So lan thu dang nhap toi da: %d\n", MAX_LOGIN_TRY);
    printf("  Ket noi: telnet 127.0.0.1 %d\n", PORT);
    printf("=================================================\n");

    int *lp = malloc(sizeof(int));
    *lp = listener;
    pthread_t ptid;
    pthread_create(&ptid, NULL, poll_thread, lp);
    pthread_join(ptid, NULL);

    close(listener);
    return 0;
}