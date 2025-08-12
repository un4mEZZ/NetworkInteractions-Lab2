#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CLIENTS     100
#define MAX_ACKS        20
#define CLIENT_TIMEOUT  30
#define MAX_MSG_SIZE    1472  // MTU Ethernet 1500 - 20 (IPv4) - 8 (UDP)
#define MIN_MSG_SIZE    23    // 4 + 4 + 2 + 12 + 1
#define LOG_FILE_NAME   "msg.txt"
#define MAX_LOG_BYTES   64

typedef struct {
    struct sockaddr_in addr;                // Адрес клиента (IP+порт)
    unsigned int msg_nums[MAX_ACKS];        // Номера последних сообщений от клиента
    int msg_count;                          // Сколько номеров хранится сейчас
    time_t last_seen;                       // Когда последний раз получали данные
    char addr_str[INET_ADDRSTRLEN + 10];    // IP:порт
} ClientInfo;

static int init_winsock(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

static void cleanup_winsock(void) {
    WSACleanup();
}

// Set non-blocking mode (Win)
static int set_non_blocking(SOCKET sock) {
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode);
}

static void close_all_sockets(SOCKET* sockets, int count) {
    for (int i = 0; i < count; i++) closesocket(sockets[i]);
}

static FILE* open_log_file(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) fprintf(stderr, "Failed to open log file %s\n", filename);
    return f;
}

// DEBUG Log raw buffer
static void log_raw_buffer(FILE* f, const char* buf, int len, const char* addr_str) {
    fprintf(f, "%sRaw datagram (%d bytes): ", addr_str, len);
    for (int i = 0; i < len && i < MAX_LOG_BYTES; i++)
        fprintf(f, "%02x ", (unsigned char)buf[i]);
    if (len > MAX_LOG_BYTES) fprintf(f, "...");
    fprintf(f, "\n");
    fflush(f);
}

static int find_client(ClientInfo* clients, int count, struct sockaddr_in* addr) {
    for (int i = 0; i < count; i++) {
        if (memcmp(&clients[i].addr, addr, sizeof(*addr)) == 0)
            return i;
    }
    return -1;
}

// Remove timed out clients (CLIENT_TIMEOUT sec)
static void remove_timed_out_clients(ClientInfo* clients, int* count, time_t now) {
    for (int i = 0; i < *count; i++) {
        if (now - clients[i].last_seen > CLIENT_TIMEOUT) {
            fprintf(stderr, "%sClient timed out\n", clients[i].addr_str);
            memmove(&clients[i], &clients[i + 1], (*count - i - 1) * sizeof(ClientInfo));
            (*count)--;
            i--;
        }
    }
}

static void send_ack(SOCKET sock, ClientInfo* client, struct sockaddr_in* addr, socklen_t addr_len, FILE* log_file) {
    char ack_buf[MAX_ACKS * 4];
    int ack_count = 0;

    // Copy msg nums in buf (network byte order)
    for (int j = 0; j < client->msg_count && ack_count < MAX_ACKS; j++) {
        unsigned int ack_num = htonl(client->msg_nums[j]);
        memcpy(ack_buf + ack_count * 4, &ack_num, 4);
        ack_count++;
    }

    if (sendto(sock, ack_buf, ack_count * 4, 0, (struct sockaddr*)addr, addr_len) < 0) {
        fprintf(stderr, "sendto error for client %s: %d\n", client->addr_str, WSAGetLastError());
        //fprintf(log_file, "%sFailed to send ACK\n", client->addr_str);
    }
    else {
        //fprintf(log_file, "%sSent ACK for %d messages\n", client->addr_str, ack_count);
        printf("Sent ACK for %d messages to %s\n", ack_count, client->addr_str);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <start_port> <end_port>\n", argv[0]);
        return 1;
    }

    int start_port = atoi(argv[1]);
    int end_port = atoi(argv[2]);
    if (start_port <= 0 || end_port > 65535 || start_port > end_port) {
        fprintf(stderr, "Invalid port range\n");
        return 1;
    }

    if (!init_winsock()) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    // Mem for sockets
    int port_count = end_port - start_port + 1;
    SOCKET* sockets = (SOCKET*)malloc(port_count * sizeof(SOCKET));
    if (!sockets) {
        fprintf(stderr, "Memory allocation failed\n");
        cleanup_winsock();
        return 1;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    SOCKET max_fd = 0;

    // Create and init sockets
    for (int i = 0; i < port_count; i++) {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockets[i] == INVALID_SOCKET) {
            fprintf(stderr, "Socket creation failed for port %d\n", start_port + i);
            close_all_sockets(sockets, i);
            free(sockets);
            cleanup_winsock();
            return 1;
        }
        set_non_blocking(sockets[i]);

        struct sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(start_port + i);

        // bind socket
        if (bind(sockets[i], (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            fprintf(stderr, "Bind failed for port %d\n", start_port + i);
            close_all_sockets(sockets, i + 1);
            free(sockets);
            cleanup_winsock();
            return 1;
        }
        FD_SET(sockets[i], &read_fds);
        if (sockets[i] > max_fd) max_fd = sockets[i];
        printf("Listening on port %d\n", start_port + i);
    }

    FILE* log_file = open_log_file(LOG_FILE_NAME);
    if (!log_file) {
        close_all_sockets(sockets, port_count);
        free(sockets);
        cleanup_winsock();
        return 1;
    }

    // Init clients pseudo-db
    ClientInfo clients[MAX_CLIENTS] = { 0 };
    int client_count = 0;
    int stop_flag = 0;

    // Stop-loop
    while (!stop_flag) {
        fd_set rfds = read_fds;
        struct timeval tv = { 0, 100000 }; // 100 ms
        int ready = select((int)max_fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) break;

        // Remove timed out clients
        time_t now = time(NULL);
        remove_timed_out_clients(clients, &client_count, now);

        if (ready > 0) {
            for (int i = 0; i < port_count; i++) {
                if (!FD_ISSET(sockets[i], &rfds)) continue;

                char buf[MAX_MSG_SIZE + 1];
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                // Recieve datagram
                int bytes = recvfrom(sockets[i], buf, sizeof(buf), 0, (struct sockaddr*)&client_addr, &addr_len);
                if (bytes <= 0) continue;

                // Shorten size of DG if greater than MTU size
                if (bytes > MAX_MSG_SIZE) {
                    fprintf(stderr, "Truncated packet from %s:%d (%d bytes)\n",
                        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), bytes);
                    bytes = MAX_MSG_SIZE;
                }

                char addr_str[INET_ADDRSTRLEN + 10];
                snprintf(addr_str, sizeof(addr_str), "%s:%d ", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                //log_raw_buffer(log_file, buf, bytes, addr_str);

                if (bytes == 3 && strncmp(buf, "put", 3) == 0) {
                    sendto(sockets[i], buf, 0, 0, (struct sockaddr*)&client_addr, addr_len);
                    continue;
                }
                if (bytes < MIN_MSG_SIZE) continue;

                // Get msg_num
                unsigned int msg_num = ntohl(*(unsigned int*)buf);
                char* message = buf + 22;

                // CRLF and LF cut
                size_t len = strlen(message);
                while (len > 0 && (message[len - 1] == '\n' || message[len - 1] == '\r' || message[len - 1] == ' ')) {
                    message[len - 1] = '\0';
                    len--;
                }

                // Find client in list
                int idx = find_client(clients, client_count, &client_addr);
                if (idx == -1 && client_count < MAX_CLIENTS) {
                    idx = client_count++;
                    clients[idx].addr = client_addr;
                    clients[idx].msg_count = 0;
                    snprintf(clients[idx].addr_str, sizeof(clients[idx].addr_str), "%s", addr_str);
                }
                if (idx == -1) continue;

                clients[idx].last_seen = now;

                // Check for duplicate
                int duplicate = 0;
                for (int j = 0; j < clients[idx].msg_count; j++) {
                    if (clients[idx].msg_nums[j] == msg_num) { duplicate = 1; break; }
                }

                if (!duplicate && clients[idx].msg_count < MAX_ACKS) {
                    clients[idx].msg_nums[clients[idx].msg_count++] = msg_num;
                    fprintf(log_file, "%s%s\n", addr_str, message);
                    fflush(log_file);

                    if (strcmp(message, "stop") == 0) {
                        printf("Stop command received from %s\n", addr_str);
                        stop_flag = 1;
                    }
                }

                send_ack(sockets[i], &clients[idx], &client_addr, addr_len, log_file);
            }
        }
    }

    close_all_sockets(sockets, port_count);
    free(sockets);
    fclose(log_file);
    cleanup_winsock();
    return 0;
}