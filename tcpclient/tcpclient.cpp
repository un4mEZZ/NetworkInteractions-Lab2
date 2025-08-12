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
#include <stdbool.h>

// Initialize Winsock 2.2
int init_wsa() {
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
}

void deinit_wsa() {
    WSACleanup();
}

// Garanteed send of len bytes into socket (repeat send() until every byte)
int send_all(SOCKET sock, const char* buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) return -1;
        total_sent += sent;
    }
    return 0;
}

// Garanteed receive of len bytes from socket (repeat recv() until get needed size)
int recv_all(SOCKET sock, char* buf, int len) {
    int total_recv = 0;
    while (total_recv < len) {
        int recvd = recv(sock, buf + total_recv, len - total_recv, 0);
        if (recvd <= 0) return -1;
        total_recv += recvd;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host:port> <filename>\n", argv[0]);
        return 1;
    }
 
    char hostport[256];
    strncpy(hostport, argv[1], sizeof(hostport) - 1);
    hostport[sizeof(hostport) - 1] = '\0';

    char* host = strtok(hostport, ":");
    char* port = strtok(NULL, ":");
    if (!host || !port) {
        fprintf(stderr, "Invalid host:port format\n");
        return 1;
    }

    if (!init_wsa()) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    // Create TCP-socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed\n");
        deinit_wsa();
        return 1;
    }

    // Init struct with servre addr
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(port));
    addr.sin_addr.s_addr = inet_addr(host);

    // Connect to server with 10 attempts (timeout 100 ms)
    int attempts = 0;
    bool connected = false;
    while (!connected && attempts < 10) {
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            connected = true;
            printf("[CLIENT] Connection success\n");
        }
        else {
            attempts++;
            printf("[CLIENT] Connection failed, retry %d/10\n", attempts);
            Sleep(100);
        }
    }
    if (!connected) {
        fprintf(stderr, "[CLIENT] Failed to connect after 10 attempts\n");
        closesocket(sock);
        deinit_wsa();
        return 1;
    }

    // Send "put" to server
    if (send_all(sock, "put", 3) < 0) {
        fprintf(stderr, "[CLIENT] Failed to send 'put'\n");
        closesocket(sock);
        deinit_wsa();
        return 1;
    }
    printf("[CLIENT] 'put' sent successfully\n");

    FILE* file = fopen(argv[2], "r");
    if (!file) {
        fprintf(stderr, "[CLIENT] Failed to open file: %s\n", argv[2]);
        closesocket(sock);
        deinit_wsa();
        return 1;
    }

    char line[1024];
    unsigned int msg_count = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strlen(line) <= 1) continue;    // Skip empty lines
        //Sleep(50);                        // Sleep to test poll

        char date[11], phone[13], message[1000];
        short aa;
        // Parse line: date, AA, phone, Message
        if (sscanf(line, "%10s %hd %12s %[^\n]", date, &aa, phone, message) != 4) {
            fprintf(stderr, "[CLIENT] Invalid line: %s", line);
            continue;
        }

        // Date = day, month, year
        unsigned char day, month;
        unsigned short year;
        if (sscanf(date, "%hhu.%hhu.%hu", &day, &month, &year) != 3) {
            fprintf(stderr, "[CLIENT] Invalid date format: %s\n", date);
            continue;
        }

        // Use network byte order
        unsigned int msg_num = htonl(msg_count);
        unsigned short year_net = htons(year);
        short aa_net = htons(aa);

        // Make a packet to send
        char sendbuf[2048];
        int pos = 0;

        memcpy(sendbuf + pos, &msg_num, 4); pos += 4;       // msg num
        memcpy(sendbuf + pos, &day, 1); pos += 1;           // day
        memcpy(sendbuf + pos, &month, 1); pos += 1;         // month
        memcpy(sendbuf + pos, &year_net, 2); pos += 2;      // year
        memcpy(sendbuf + pos, &aa_net, 2); pos += 2;        // AA
        memcpy(sendbuf + pos, phone, 12); pos += 12;        // phone
        size_t mlen = strlen(message);
        memcpy(sendbuf + pos, message, mlen); pos += mlen;  // Message
        sendbuf[pos++] = '\0';                              // null term (0x00)

        // Send packet to server
        if (send_all(sock, sendbuf, pos) < 0) {
            fprintf(stderr, "[CLIENT] Failed to send message %u\n", msg_count);
            fclose(file);
            closesocket(sock);
            deinit_wsa();
            return 1;
        }
        //printf("[CLIENT] Message %u sent\n", msg_count);
        msg_count++;
    }

    fclose(file);

    // Receive "ok" from server (every client)
    char okbuf[2];
    for (unsigned int i = 0; i < msg_count; i++) {
        if (recv_all(sock, okbuf, 2) < 0 || okbuf[0] != 'o' || okbuf[1] != 'k') {
            fprintf(stderr, "[CLIENT] Invalid or no OK response received for msg %u\n", i);
            closesocket(sock);
            deinit_wsa();
            return 1;
        }
        //printf("[CLIENT] OK received for msg %u\n", i);
    }

    printf("[CLIENT] All messages acknowledged, closing connection\n");
    closesocket(sock);
    deinit_wsa();
    return 0;
}