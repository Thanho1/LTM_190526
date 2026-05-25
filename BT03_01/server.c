/*
 * file_server - Multithreading File Server
 *
 * Y tuong:
 *   1. Client ket noi -> tao 1 thread moi xu ly client do (pthread_create + pthread_detach).
 *   2. Thread kiem tra thu muc files:
 *      - Khong co file: gui "ERROR No files to download\r\n" -> dong ket noi luon.
 *      - Co file: gui "OK N\r\n" + danh sach ten file + "\r\n".
 *   3. Thread nhan ten file tu client:
 *      - Ton tai: gui "OK <size>\r\n" + noi dung file -> dong ket noi.
 *      - Khong ton tai: gui "ERROR File not found\r\n" -> cho client gui lai.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>

#define PORT      8080
#define BACKLOG   5
#define BUF_SIZE  256
#define FILES_DIR "./files"
#define PATH_SIZE 512

void *client_thread(void *params);

int main(void) {
    mkdir(FILES_DIR, 0755);

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) { perror("socket"); return 1; }

    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr))) { perror("bind"); return 1; }
    if (listen(listener, BACKLOG)) { perror("listen"); return 1; }

    printf("[Server] File server listening on port %d...\n", PORT);
    printf("[Server] Serving files from: %s\n", FILES_DIR);

    while (1) {
        int *client = malloc(sizeof(int));
        *client = accept(listener, NULL, NULL);
        if (*client == -1) { free(client); continue; }

        printf("[Server] New client connected: %d\n", *client);

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, client);
        pthread_detach(tid);
    }

    close(listener);
    return 0;
}

int recv_line(int sock, char *buf, int size) {
    int total = 0;
    char c;
    while (total < size - 1) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return n;
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[total++] = c;
    }
    buf[total] = '\0';
    return total;
}

/* Dem so file trong thu muc, tra ve so luong */
int count_files(void) {
    DIR *dir = opendir(FILES_DIR);
    if (!dir) return 0;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
        if (entry->d_type == DT_REG) count++;
    closedir(dir);
    return count;
}

void send_file_list(int client) {
    int count = count_files();

    /* Khong co file -> bao loi -> dong ket noi */
    if (count == 0) {
        send(client, "ERROR No files to download\r\n", 28, 0);
        return;
    }

    /* Gui dong dau OK N */
    char header[64];
    snprintf(header, sizeof(header), "OK %d\r\n", count);
    send(client, header, strlen(header), 0);

    /* Gui tung ten file */
    DIR *dir = opendir(FILES_DIR);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char line[PATH_SIZE];
            snprintf(line, sizeof(line), "%s\r\n", entry->d_name);
            send(client, line, strlen(line), 0);
        }
    }
    closedir(dir);

    /* Ket thuc danh sach */
    send(client, "\r\n", 2, 0);
}

void send_file(int client, const char *filename) {
    char path[PATH_SIZE];
    snprintf(path, sizeof(path), "%s/%s", FILES_DIR, filename);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        send(client, "ERROR File not found\r\n", 22, 0);
        return;
    }

    char header[64];
    snprintf(header, sizeof(header), "OK %ld\r\n", (long)st.st_size);
    send(client, header, strlen(header), 0);

    FILE *fp = fopen(path, "rb");
    if (!fp) { send(client, "ERROR Cannot open file\r\n", 24, 0); return; }

    char buf[4096];
    int n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        send(client, buf, n, 0);
    fclose(fp);

    printf("[Thread %lu] Sent file: %s (%ld bytes)\n",
           pthread_self(), filename, (long)st.st_size);
}

void *client_thread(void *params) {
    int client = *(int *)params;
    free(params);

    /* Neu khong co file: gui ERROR roi dong ket noi luon */
    if (count_files() == 0) {
        send(client, "ERROR No files to download\r\n", 28, 0);
        printf("[Thread %lu] No files, closing client %d\n",
               pthread_self(), client);
        close(client);
        return NULL;
    }

    /* Co file: gui danh sach */
    send_file_list(client);

    /* Nhan ten file tu client, xu ly den khi thanh cong */
    char buf[BUF_SIZE];
    while (1) {
        int n = recv_line(client, buf, sizeof(buf));
        if (n <= 0) break;
        if (strlen(buf) == 0) continue;

        printf("[Thread %lu] Client %d requested: \"%s\"\n",
               pthread_self(), client, buf);

        send_file(client, buf);

        /* Neu file ton tai -> da gui xong -> dong ket noi */
        char path[PATH_SIZE];
        snprintf(path, sizeof(path), "%s/%s", FILES_DIR, buf);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) break;
        /* Neu file khong ton tai -> tiep tuc vong lap cho client gui lai */
    }

    close(client);
    return NULL;
}