#define _CRT_SECURE_NO_WARNINGS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <string.h>

#include <stdlib.h>

int init() {
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
}

void deinit() {
    WSACleanup();
}

int sock_err(const char* function, int s) {
    int err;
    err = WSAGetLastError();
    fprintf(stderr, "%s: socket error: %d\n", function, err);
    return -1;
}

void s_close(int s) {
    closesocket(s);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host:port> <filename>\n", argv[0]);
        return 1;
    }

    // Parse host:port
    char* host = strtok(argv[1], ":");
    char* port = strtok(NULL, ":");
    if (!host || !port) {
        fprintf(stderr, "Invalid host:port format\n");
        return 1;
    }

    // Initialize Winsock
    if (!init()) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    // Create socket
    int my_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (my_socket < 0) {
        sock_err("socket", my_socket);
        deinit();
        return 1;
    }

    // Resolve host
    struct addrinfo hints, * res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        s_close(my_socket);
        deinit();
        return 1;
    }

    // Connect with retries
    int attempts = 0;
    while (attempts < 10) {
        if (connect(my_socket, res->ai_addr, res->ai_addrlen) == 0) {
            break;
        }
        attempts++;
        Sleep(100);
    }
    if (attempts == 10) {
        fprintf(stderr, "Failed to connect after 10 attempts\n");
        freeaddrinfo(res);
        s_close(my_socket);
        deinit();
        return 1;
    }
    freeaddrinfo(res);

    // Open file
    FILE* file = fopen(argv[2], "r");
    if (!file) {
        fprintf(stderr, "Failed to open file %s\n", argv[2]);
        s_close(my_socket);
        deinit();
        return 1;
    }

    // Send initial "put" message
    const char* init_msg = "put";
    if (send(my_socket, init_msg, 3, 0) < 0) {
        fprintf(stderr, "Failed to send initial message\n");
        fclose(file);
        s_close(my_socket);
        deinit();
        return 1;
    }

    // Process file
    char line[1024];
    unsigned int msg_count = 0;
    while (fgets(line, sizeof(line), file)) {
        // Skip empty lines
        if (strlen(line) <= 1) continue;

        // Parse line
        char date[11], phone[13], message[1000];
        short aa;
        if (sscanf(line, "%10s %hd %12s %[^\n]", date, &aa, phone, message) != 4) {
            fprintf(stderr, "Invalid line format: %s", line);
            continue;
        }

        // Parse date
        unsigned char day, month;
        unsigned short year;
        if (sscanf(date, "%hhu.%hhu.%hu", &day, &month, &year) != 3) {
            fprintf(stderr, "Invalid date format: %s\n", date);
            continue;
        }

        // Prepare message
        unsigned int msg_num = htonl(msg_count);
        unsigned short year_net = htons(year);
        short aa_net = htons(aa);

        if (send(my_socket, (const char*)&msg_num, 4, 0) < 0) {
            fprintf(stderr, "Failed to send message number\n");
            fclose(file);
            s_close(my_socket);
            deinit();
            return 1;
        }

        // Send date
        if (send(my_socket, (const char*)&day, 1, 0) < 0 ||
            send(my_socket, (const char*)&month, 1, 0) < 0 ||
            send(my_socket, (const char*)&year_net, 2, 0) < 0) {
            fprintf(stderr, "Failed to send date\n");
            fclose(file);
            s_close(my_socket);
            deinit();
            return 1;
        }

        // Send AA
        if (send(my_socket, (const char*)&aa_net, 2, 0) < 0) {
            fprintf(stderr, "Failed to send AA\n");
            fclose(file);
            s_close(my_socket);
            deinit();
            return 1;
        }

        // Send phone
        if (send(my_socket, phone, 12, 0) < 0) {
            fprintf(stderr, "Failed to send phone\n");
            fclose(file);
            s_close(my_socket);
            deinit();
            return 1;
        }

        // Send message with null terminator
        size_t msg_len = strlen(message);
        if (send(my_socket, message, msg_len, 0) < 0 || send(my_socket, "\0", 1, 0) < 0) {
            fprintf(stderr, "Failed to send message\n");
            fclose(file);
            s_close(my_socket);
            deinit();
            return 1;
        }

        msg_count++;
    }

    fclose(file);

    // Receive OK responses
    char buffer[2];
    for (unsigned int i = 0; i < msg_count; i++) {
        if (recv(my_socket, buffer, 2, 0) != 2 || buffer[0] != 'o' || buffer[1] != 'k') {
            fprintf(stderr, "Invalid or no OK response received\n");
            s_close(my_socket);
            deinit();
            return 1;
        }
    }

    // Close connection
    s_close(my_socket);
    deinit();
    return 0;
}