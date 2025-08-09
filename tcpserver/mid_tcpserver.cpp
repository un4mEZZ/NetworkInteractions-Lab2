#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

int set_non_block_mode(int s) {
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("Invalid port number\n");
        return 1;
    }

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) {
        printf("Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    int opt = 1;
    if (setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("Setsockopt failed: %s\n", strerror(errno));
        close(ls);
        return 1;
    }

    if (set_non_block_mode(ls) < 0) {
        printf("Failed to set non-blocking mode: %s\n", strerror(errno));
        close(ls);
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(ls, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Bind failed: %s\n", strerror(errno));
        close(ls);
        return 1;
    }

    if (listen(ls, SOMAXCONN) < 0) {
        printf("Listen failed: %s\n", strerror(errno));
        close(ls);
        return 1;
    }

    printf("Server listening on port %d\n", port);

    int cs[MAX_CLIENTS] = {0};
    struct pollfd pfd[MAX_CLIENTS + 1];
    struct sockaddr_in client_addr[MAX_CLIENTS];
    char client_info[MAX_CLIENTS][INET_ADDRSTRLEN + 10];
    int client_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        pfd[i].fd = -1;
        pfd[i].events = POLLIN | POLLOUT;
    }
    pfd[MAX_CLIENTS].fd = ls;
    pfd[MAX_CLIENTS].events = POLLIN;

    FILE *msg_file = fopen("msg.txt", "w");
    if (!msg_file) {
        printf("Failed to open msg.txt: %s\n", strerror(errno));
        close(ls);
        return 1;
    }

    char buffer[BUFFER_SIZE];
    int should_stop = 0;

    while (!should_stop) {
        int ev_cnt = poll(pfd, MAX_CLIENTS + 1, 1000);
        if (ev_cnt < 0) {
            printf("Poll error: %s\n", strerror(errno));
            break;
        }

        if (ev_cnt > 0) {
            for (int i = 0; i < client_count; i++) {
                if (pfd[i].revents & (POLLHUP | POLLERR)) {
                    fprintf(msg_file, "%sClient disconnected\n", client_info[i]);
                    fflush(msg_file);
                    close(cs[i]);
                    pfd[i].fd = -1;
                    memmove(&cs[i], &cs[i + 1], (client_count - i - 1) * sizeof(int));
                    memmove(&pfd[i], &pfd[i + 1], (client_count - i - 1) * sizeof(struct pollfd));
                    memmove(&client_info[i], &client_info[i + 1], (client_count - i - 1) * sizeof(client_info[0]));
                    client_count--;
                    i--;
                    continue;
                }

                if (pfd[i].revents & POLLIN) {
                    int bytes = recv(cs[i], buffer, BUFFER_SIZE - 1, 0);
                    if (bytes > 0) {
                        // Ensure minimum message size (4 + 4 + 2 + 12 + 1 = 23 bytes)
                        if (bytes < 23) {
                            printf("Received incomplete message from %s\n", client_info[i]);
                            continue;
                        }

                        // Parse message: skip 4-byte index, 4-byte date, 2-byte AA, 12-byte phone
                        char *message = buffer + 4 + 4 + 2 + 12;
                        int message_len = bytes - (4 + 4 + 2 + 12);
                        if (message[message_len - 1] != '\0') {
                            printf("Message from %s not null-terminated\n", client_info[i]);
                            continue;
                        }

                        // Check for "stop" message
                        if (strcmp(message, "stop") == 0) {
                            send(cs[i], "ok", 2, 0);
                            fprintf(msg_file, "%sstop\n", client_info[i]);
                            fflush(msg_file);
                            should_stop = 1;
                        } else {
                            send(cs[i], "ok", 2, 0);
                            fprintf(msg_file, "%s%s\n", client_info[i], message);
                            fflush(msg_file);
                        }
                    }
                }
            }

            if (pfd[MAX_CLIENTS].revents & POLLIN) {
                struct sockaddr_in client;
                socklen_t client_len = sizeof(client);
                int new_client = accept(ls, (struct sockaddr*)&client, &client_len);

                if (new_client >= 0 && client_count < MAX_CLIENTS) {
                    if (set_non_block_mode(new_client) < 0) {
                        printf("Failed to set non-blocking mode for client: %s\n", strerror(errno));
                        close(new_client);
                        continue;
                    }

                    cs[client_count] = new_client;
                    pfd[client_count].fd = new_client;
                    pfd[client_count].events = POLLIN | POLLOUT;
                    client_addr[client_count] = client;

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client.sin_addr, ip_str, INET_ADDRSTRLEN);
                    snprintf(client_info[client_count], sizeof(client_info[client_count]),
                            "%s:%d ", ip_str, ntohs(client.sin_port));

                    client_count++;
                } else if (new_client >= 0) {
                    close(new_client);
                }
            }
        }
    }

    for (int i = 0; i < client_count; i++) {
        if (cs[i] >= 0) {
            close(cs[i]);
        }
    }
    close(ls);
    fclose(msg_file);
    return 0;
}
