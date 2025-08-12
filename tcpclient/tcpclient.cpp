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
#include <ctype.h>

// Ethernet MTU и расчёт MSS (не учитываем опции IP/TCP)
#define ETHERNET_MTU 1500
#define IP_HEADER_LEN 20
#define TCP_HEADER_LEN 20
#define MSS (ETHERNET_MTU - IP_HEADER_LEN - TCP_HEADER_LEN) // 1460

// Инициализация библиотеки Winsock 2.2
int init_wsa() {
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
}

void deinit_wsa() {
    WSACleanup();
}

// Гарантированная отправка len байт в сокет (повторяем send(), пока всё не уйдёт)
int send_all(SOCKET sock, const char* buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) return -1;
        total_sent += sent;
    }
    return 0;
}

// Гарантированное получение len байт из сокета (повторяем recv(), пока не наберём нужный объём)
int recv_all(SOCKET sock, char* buf, int len) {
    int total_recv = 0;
    while (total_recv < len) {
        int recvd = recv(sock, buf + total_recv, len - total_recv, 0);
        if (recvd <= 0) return -1;
        total_recv += recvd;
    }
    return 0;
}

bool is_digits_only(const char* s) {
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) return false;
    }
    return true;
}

bool is_printable_string(const char* s) {
    for (; *s; s++) {
        if (!isprint((unsigned char)*s)) return false;
    }
    return true;
}

bool validate_line_fields(const char* date, short aa, const char* phone, const char* message) {
    unsigned char day, month;
    unsigned short year;

    // Проверка даты
    if (sscanf(date, "%hhu.%hhu.%hu", &day, &month, &year) != 3) return false;
    if (day < 1 || day > 31) return false;
    if (month < 1 || month > 12) return false;
    if (year < 1000 || year > 9999) return false; // диапазон года

    // Проверка AA — диапазон short
    if (aa < -32768 || aa > 32767) return false;

    // Проверка телефона: начинается с '+', длина <= 12, остальное — цифры
    size_t plen = strlen(phone);
    if (plen < 2 || plen > 12) return false;  // минимум "+X", максимум 12 символов
    if (phone[0] != '+') return false;
    if (!is_digits_only(phone + 1)) return false;

    // Проверка текста сообщения
    size_t mlen = strlen(message);
    if (mlen == 0) return false; // не пустое
    if (!is_printable_string(message)) return false; // все символы печатные

    return true;
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

    // Создание TCP-сокета
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

    // Отправляем серверу команду "put"
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

    char line[4096];
    unsigned int msg_count = 0;

    // Предвычислим фиксированную длину "заголовочной" части в нашем протоколе:
    // msg_num (4) + day (1) + month (1) + year (2) + aa (2) + phone (12)
    const int header_len = 4 + 1 + 1 + 2 + 2 + 12; // = 22
    if (header_len + 1 > MSS) {
        fprintf(stderr, "[CLIENT] Protocol header too large for MSS (%d)\n", MSS);
        fclose(file);
        closesocket(sock);
        deinit_wsa();
        return 1;
    }
    const int max_message_bytes = MSS - header_len - 1; // оставить байт для '\0'

    while (fgets(line, sizeof(line), file)) {
        if (strlen(line) <= 1) continue;    // Пропускаем пустые строки

        char date[11], phone_in[64], message_in[4096];
        short aa;
        // Парсим строку: дата, число AA, телефон, текст сообщения
        // DOP
        if (sscanf(line, "%10s %hd %63s %[^\n]", date, &aa, phone_in, message_in) != 4) {
            fprintf(stderr, "[CLIENT] Invalid line format: %s", line);
            continue;
        }

        if (!validate_line_fields(date, aa, phone_in, message_in)) {
            fprintf(stderr, "[CLIENT] Skipping invalid line: %s", line);
            continue;
        }

        unsigned char day, month;
        unsigned short year;
        if (sscanf(date, "%hhu.%hhu.%hu", &day, &month, &year) != 3) {
            fprintf(stderr, "[CLIENT] Invalid date format: %s\n", date);
            continue;
        }

        unsigned int msg_num = htonl(msg_count);
        unsigned short year_net = htons(year);
        short aa_net = htons(aa);

        // Формируем пакет для отправки
        // Выделяем буфер размером MSS + небольшой запас
        char sendbuf[MSS + 32];
        int pos = 0;

        memcpy(sendbuf + pos, &msg_num, 4); pos += 4;       // Номер сообщения
        memcpy(sendbuf + pos, &day, 1); pos += 1;           // День
        memcpy(sendbuf + pos, &month, 1); pos += 1;         // Месяц
        memcpy(sendbuf + pos, &year_net, 2); pos += 2;      // Год
        memcpy(sendbuf + pos, &aa_net, 2); pos += 2;        // Поле AA

        // Телефон — фиксированное 12 байтное поле. Копируем и дополняем нулями, если короче.
        char phone_field[12];
        memset(phone_field, 0, sizeof(phone_field));
        // Копируем не больше 12 байт
        strncpy(phone_field, phone_in, sizeof(phone_field));
        memcpy(sendbuf + pos, phone_field, 12); pos += 12;

        // Оставшееся место для текста сообщения
        size_t mlen_in = strlen(message_in);
        size_t mlen_to_send = mlen_in;
        if ((int)mlen_to_send > max_message_bytes) {
            mlen_to_send = (size_t)max_message_bytes;
            // можно сообщить об обрезке
            printf("[CLIENT] Message %u truncated from %zu to %d bytes to fit MSS\n",
                msg_count, mlen_in, max_message_bytes);
        }

        // Копируем часть (или весь) текст сообщения и ставим '\0'
        memcpy(sendbuf + pos, message_in, mlen_to_send); pos += (int)mlen_to_send;
        sendbuf[pos++] = '\0';

        // Теперь pos — количество байт для отправки и гарантированно <= MSS
        if (pos > MSS) {
            fprintf(stderr, "[CLIENT] Internal error: packet size %d exceeds MSS %d\n", pos, MSS);
            fclose(file);
            closesocket(sock);
            deinit_wsa();
            return 1;
        }

        if (send_all(sock, sendbuf, pos) < 0) {
            fprintf(stderr, "[CLIENT] Failed to send message %u\n", msg_count);
            fclose(file);
            closesocket(sock);
            deinit_wsa();
            return 1;
        }
        msg_count++;
    }

    fclose(file);

    // Получаем подтверждения "ok" от сервера для каждого отправленного сообщения
    char okbuf[2];
    for (unsigned int i = 0; i < msg_count; i++) {
        if (recv_all(sock, okbuf, 2) < 0 || okbuf[0] != 'o' || okbuf[1] != 'k') {
            fprintf(stderr, "[CLIENT] Invalid or no OK response received for msg %u\n", i);
            closesocket(sock);
            deinit_wsa();
            return 1;
        }
    }

    printf("[CLIENT] All messages acknowledged, closing connection\n");
    closesocket(sock);
    deinit_wsa();
    return 0;
}
