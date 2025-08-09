#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define MAX_CLIENTS 100
// NOTE 1) max size is no more than 536 bytes
#define MAX_MESSAGE_SIZE 1032 // 4 (index) + 4 (date) + 2 (AA) + 12 (phone) + 1000 (message) + 1 (null)
#define MIN_MESSAGE_SIZE 23 // 4 (index) + 4 (date) + 2 (AA) + 12 (phone) + 1 (null)
#define MAX_ACKS 20
#define CLIENT_TIMEOUT_SECONDS 30

// Structure to hold client information
struct client_info {
    struct sockaddr_in addr;
    unsigned int message_nums[MAX_ACKS];
    int message_count;
    time_t last_seen;
    char address_str[INET_ADDRSTRLEN + 10]; // IP:port
};

int init_winsock() {
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
}

void deinit_winsock() {
    WSACleanup();
}

int set_non_blocking(SOCKET sock) {
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode);
}

FILE* open_log_file(const char* filename) {
    FILE* log_file = fopen(filename, "w");
    if (!log_file) {
        fprintf(stderr, "Failed to open %s: %d\n", filename, WSAGetLastError());
    }
    return log_file;
}

// NOTE 2) why and how?
void log_raw_buffer(FILE* log_file, const char* buffer, int bytes, const char* address_str) {
    fprintf(log_file, "%sRaw datagram (%d bytes): ", address_str, bytes);
    for (int i = 0; i < bytes && i < 64; i++) { // Log up to 64 bytes
        fprintf(log_file, "%02x ", (unsigned char)buffer[i]);
    }
    if (bytes > 64) fprintf(log_file, "...");
    fprintf(log_file, "\n");
    fflush(log_file);
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

    // Calculate port count and allocate sockets dynamically
    int port_count = end_port - start_port + 1;
    SOCKET* sockets = (SOCKET*)malloc(port_count * sizeof(SOCKET));
    if (!sockets) {
        fprintf(stderr, "Failed to allocate memory for sockets\n");
        deinit_winsock();
        return 1;
    }

    // Initialize sockets
    fd_set read_fds;
    FD_ZERO(&read_fds);
    SOCKET max_fd = 0;
    for (int i = 0; i < port_count; i++) {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockets[i] == INVALID_SOCKET) {
            fprintf(stderr, "Failed to create socket for port %d: %d\n", start_port + i, WSAGetLastError());
            for (int j = 0; j < i; j++) closesocket(sockets[j]);
            free(sockets);
            deinit_winsock();
            return 1;
        }
        if (set_non_blocking(sockets[i]) != 0) {
            fprintf(stderr, "Failed to set non-blocking mode for port %d: %d\n", start_port + i, WSAGetLastError());
            for (int j = 0; j <= i; j++) closesocket(sockets[j]);
            free(sockets);
            deinit_winsock();
            return 1;
        }

        struct sockaddr_in server_addr = { 0 };
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(start_port + i);

        if (bind(sockets[i], (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            fprintf(stderr, "Bind failed for port %d: %d\n", start_port + i, WSAGetLastError());
            for (int j = 0; j <= i; j++) closesocket(sockets[j]);
            free(sockets);
            deinit_winsock();
            return 1;
        }
        FD_SET(sockets[i], &read_fds);
        if (sockets[i] > max_fd) max_fd = sockets[i];
        printf("Listening on port %d\n", start_port + i);
    }

    FILE* log_file = open_log_file("msg.txt");
    if (!log_file) {
        for (int i = 0; i < port_count; i++) closesocket(sockets[i]);
        free(sockets);
        deinit_winsock();
        return 1;
    }

    struct client_info clients[MAX_CLIENTS] = { 0 };
    int client_count = 0;
    int should_stop = 0;

    while (!should_stop) {
        fd_set read_fds_copy = read_fds;
        struct timeval timeout = { 0, 100000 }; // 100ms
        int ready = select((int)max_fd + 1, &read_fds_copy, NULL, NULL, &timeout);
        if (ready < 0) {
            fprintf(stderr, "select error: %d\n", WSAGetLastError());
            break;
        }

        time_t now = time(NULL);
        // Remove timed-out clients
        for (int i = 0; i < client_count; i++) {
            if (now - clients[i].last_seen > CLIENT_TIMEOUT_SECONDS) {
                fprintf(stderr, "%sClient timed out\n", clients[i].address_str);
                memmove(&clients[i], &clients[i + 1], (client_count - i - 1) * sizeof(struct client_info));
                client_count--;
                i--;
            }
        }

        if (ready > 0) {
            for (int i = 0; i < port_count; i++) {
                if (!FD_ISSET(sockets[i], &read_fds_copy)) continue;

                char buffer[MAX_MESSAGE_SIZE];
                struct sockaddr_in client_addr;
                int addr_len = sizeof(client_addr);
                int bytes = recvfrom(sockets[i], buffer, MAX_MESSAGE_SIZE, 0, (struct sockaddr*)&client_addr, &addr_len);
                if (bytes < 0) {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        fprintf(stderr, "recvfrom error: %d\n", WSAGetLastError());
                    }
                    continue;
                }

                char address_str[INET_ADDRSTRLEN + 10];
                snprintf(address_str, sizeof(address_str), "%s:%d ", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                // Log raw datagram for debugging
                log_raw_buffer(log_file, buffer, bytes, address_str);

                // Handle "put" message
                if (bytes == 3 && strncmp(buffer, "put", 3) == 0) {
                    printf("Received PUT from %s\n", address_str);
                    // Send empty acknowledgment for "put"
                    if (sendto(sockets[i], buffer, 0, 0, (struct sockaddr*)&client_addr, addr_len) < 0) {
                        fprintf(stderr, "sendto error for PUT from %s: %d\n", address_str, WSAGetLastError());
                    }
                    continue;
                }

                // Validate message (minimal)
                if (bytes < MIN_MESSAGE_SIZE) {
                    fprintf(stderr, "%sIncomplete message, bytes = %d\n", address_str, bytes);
                    continue;
                }

                int message_end = -1;
                for (int j = MIN_MESSAGE_SIZE - 1; j < bytes; j++) {
                    if (buffer[j] == '\0') {
                        message_end = j;
                        break;
                    }
                }
                if (message_end == -1 || message_end >= MAX_MESSAGE_SIZE) {
                    fprintf(stderr, "%sNo null terminator or message too long\n", address_str);
                    continue;
                }

                // Extract message components
                unsigned int msg_num = ntohl(*(unsigned int*)buffer);
                char* message = buffer + 22;

                // Find or create client
                int client_index = -1;
                for (int j = 0; j < client_count; j++) {
                    if (memcmp(&clients[j].addr, &client_addr, sizeof(client_addr)) == 0) {
                        client_index = j;
                        break;
                    }
                }
                if (client_index == -1 && client_count < MAX_CLIENTS) {
                    client_index = client_count++;
                    clients[client_index].addr = client_addr;
                    clients[client_index].message_count = 0;
                    strcpy(clients[client_index].address_str, address_str);
                }
                if (client_index == -1) {
                    fprintf(log_file, "%sClient limit reached, ignoring message\n", address_str);
                    continue;
                }

                clients[client_index].last_seen = now;

                // Check for duplicate message
                int is_duplicate = 0;
                for (int j = 0; j < clients[client_index].message_count; j++) {
                    if (clients[client_index].message_nums[j] == msg_num) {
                        is_duplicate = 1;
                        break;
                    }
                }

                // Log and process non-duplicate messages
                if (!is_duplicate && clients[client_index].message_count < MAX_ACKS) {
                    clients[client_index].message_nums[clients[client_index].message_count++] = msg_num;
                    fprintf(log_file, "%s%s\n", address_str, message);
                    fprintf(log_file, "%sLogged message %u\n", address_str, msg_num);
                    fflush(log_file);
                    printf("Logged message %u from %s\n", msg_num, address_str);
                    if (strcmp(message, "stop") == 0) should_stop = 1;
                }
                else if (is_duplicate) {
                    fprintf(log_file, "%sDuplicate message %u\n", address_str, msg_num);
                    printf("Duplicate message %u from %s\n", msg_num, address_str);
                }

                // Send acknowledgment
                char ack_buffer[MAX_ACKS * 4];
                int ack_count = 0;
                for (int j = 0; j < clients[client_index].message_count && ack_count < MAX_ACKS; j++) {
                    unsigned int ack_num = htonl(clients[client_index].message_nums[j]);
                    memcpy(ack_buffer + ack_count * 4, &ack_num, 4);
                    ack_count++;
                }
                if (sendto(sockets[i], ack_buffer, ack_count * 4, 0, (struct sockaddr*)&client_addr, addr_len) < 0) {
                    fprintf(stderr, "sendto error for message %u from %s: %d\n", msg_num, address_str, WSAGetLastError());
                    fprintf(log_file, "%sFailed to send ACK for message %u: %d\n", address_str, msg_num, WSAGetLastError());
                }
                else {
                    printf("Sent ACK for %d messages to %s\n", ack_count, address_str);
                    fprintf(log_file, "%sSent ACK for %d messages\n", address_str, ack_count);
                }
            }
        }
    }

    // Cleanup
    for (int i = 0; i < port_count; i++) closesocket(sockets[i]);
    free(sockets);
    if (log_file) fclose(log_file);
    deinit_winsock();
    return 0;
}