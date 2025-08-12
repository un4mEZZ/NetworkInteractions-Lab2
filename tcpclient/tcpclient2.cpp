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
#include <stdint.h>

int init_wsa() {
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
}
void deinit_wsa() {
    WSACleanup();
}

int send_all(SOCKET sock, const char* buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) return -1;
        total_sent += sent;
    }
    return 0;
}

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
    if (argc != 4 || strcmp(argv[2], "get") != 0) {
        fprintf(stderr, "Usage: %s <host:port> get <outputfile>\n", argv[0]);
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

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed\n");
        deinit_wsa();
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(port));
    addr.sin_addr.s_addr = inet_addr(host);

    int attempts = 0;
    bool connected = false;
    while (!connected && attempts < 10) {
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            connected = true;
            printf("[CLIENT2] Connected to server\n");
        }
        else {
            attempts++;
            printf("[CLIENT2] Connection failed, retry %d/10\n", attempts);
            Sleep(100);
        }
    }
    if (!connected) {
        fprintf(stderr, "[CLIENT2] Failed to connect after 10 attempts\n");
        closesocket(sock);
        deinit_wsa();
        return 1;
    }

    // Отправляем "get"
    if (send_all(sock, "get", 3) < 0) {
        fprintf(stderr, "[CLIENT2] Failed to send 'get'\n");
        closesocket(sock);
        deinit_wsa();
        return 1;
    }

    // Получаем IP:Port сервера для записи в файл
    struct sockaddr_in serv_addr;
    int len = sizeof(serv_addr);
    getpeername(sock, (struct sockaddr*)&serv_addr, &len);
    char server_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &serv_addr.sin_addr, server_ip, sizeof(server_ip));
    int server_port = ntohs(serv_addr.sin_port);

    FILE* fout = fopen(argv[3], "w");
    if (!fout) {
        fprintf(stderr, "[CLIENT2] Failed to open output file: %s\n", argv[3]);
        closesocket(sock);
        deinit_wsa();
        return 1;
    }

// Приём сообщений
    while (1) {
        uint32_t msg_num_net;
        int r = recv(sock, (char*)&msg_num_net, 4, MSG_WAITALL);
        if (r <= 0) break; // соединение закрыто или ошибка

        unsigned char day, month;
        unsigned short year_net, aa_net;
        char phone[13];

        if (recv_all(sock, (char*)&day, 1) < 0) break;
        if (recv_all(sock, (char*)&month, 1) < 0) break;
        if (recv_all(sock, (char*)&year_net, 2) < 0) break;
        if (recv_all(sock, (char*)&aa_net, 2) < 0) break;
        if (recv_all(sock, phone, 12) < 0) break;
        phone[12] = '\0';

        // читаем текст сообщения до \0
        char msgbuf[4096];
        int pos = 0;
        while (1) {
            char ch;
            int rr = recv(sock, &ch, 1, 0);
            if (rr <= 0) { pos = 0; break; } // ошибка или закрытие соединения
            msgbuf[pos++] = ch;
            if (ch == '\0' || pos >= (int)sizeof(msgbuf) - 1) break;
        }
        if (pos == 0) break; // оборвали посередине

        // Пишем в файл
        fprintf(fout, "%s:%d %s\n", server_ip, server_port, msgbuf);
    }


    fclose(fout);
    closesocket(sock);
    deinit_wsa();
    printf("[CLIENT2] Finished receiving messages\n");
    return 0;
}
