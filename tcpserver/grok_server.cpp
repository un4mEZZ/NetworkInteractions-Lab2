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
#define MIN_MESSAGE_SIZE 23 // 4 (index) + 4 (date) + 2 (AA) + 12 (phone) + 1 (null)
#define RESPONSE_OK "ok"

// Structure to hold client information
struct client_info {
    int socket;
    struct sockaddr_in address;
    char address_str[INET_ADDRSTRLEN + 10]; // IP:port
    char buffer[BUFFER_SIZE]; // Per-client buffer
    int buffer_len; // Current length of data in buffer
};

// Set socket to non-blocking mode
int set_non_blocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
}

// Create and configure listening socket
int setup_listening_socket(int port, int *listen_socket) {
    *listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (*listen_socket < 0) {
        printf("Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    int opt = 1;
    if (setsockopt(*listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("Setsockopt failed: %s\n", strerror(errno));
        close(*listen_socket);
        return 1;
    }

    if (set_non_blocking(*listen_socket) < 0) {
        printf("Failed to set non-blocking mode: %s\n", strerror(errno));
        close(*listen_socket);
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(*listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Bind failed: %s\n", strerror(errno));
        close(*listen_socket);
        return 1;
    }

    if (listen(*listen_socket, SOMAXCONN) < 0) {
        printf("Listen failed: %s\n", strerror(errno));
        close(*listen_socket);
        return 1;
    }

    printf("Server listening on port %d\n", port);
    return 0;
}

// Open log file
FILE* open_log_file(const char *filename) {
    FILE *log_file = fopen(filename, "w");
    if (!log_file) {
        printf("Failed to open %s: %s\n", filename, strerror(errno));
    }
    return log_file;
}

// Initialize poll descriptors
void init_poll_fds(struct pollfd *poll_fds, int listen_socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        poll_fds[i].fd = -1;
        poll_fds[i].events = POLLIN;
    }
    poll_fds[MAX_CLIENTS].fd = listen_socket;
    poll_fds[MAX_CLIENTS].events = POLLIN;
    printf("Poll initialization done\n");
}

// Accept new client connection
int accept_client(int listen_socket, struct client_info *clients, struct pollfd *poll_fds, int *client_count) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int new_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &client_len);

    if (new_socket < 0) return 0;

    if (*client_count >= MAX_CLIENTS) {
        close(new_socket);
        return 0;
    }

    if (set_non_blocking(new_socket) < 0) {
        printf("Failed to set non-blocking mode for client: %s\n", strerror(errno));
        close(new_socket);
        return 0;
    }

    clients[*client_count].socket = new_socket;
    clients[*client_count].address = client_addr;
    clients[*client_count].buffer_len = 0;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    snprintf(clients[*client_count].address_str, sizeof(clients[*client_count].address_str),
             "%s:%d ", ip_str, ntohs(client_addr.sin_port));

    poll_fds[*client_count].fd = new_socket;
    poll_fds[*client_count].events = POLLIN;
    (*client_count)++;
    printf("Accepted client %s\n", clients[*client_count - 1].address_str);
    return 1;
}

// Process client message
int process_message(int client_socket, struct client_info *client, FILE *log_file, int *should_stop) {
    char *buffer = client->buffer;
    int bytes = client->buffer_len;

    // Check for "put" message (3 bytes)
    if (bytes >= 3 && strncmp(buffer, "put", 3) == 0) {
        printf("Received PUT from %s\n", client->address_str);
        client->buffer_len = 0; // Clear buffer after processing "put"
        return 0;
    }

    // Ensure minimum message size
    if (bytes < MIN_MESSAGE_SIZE) {
        printf("Incomplete message from %s, bytes = %d\n", client->address_str, bytes);
        return 0;
    }

    // Find null terminator to ensure complete message
    int message_end = -1;
    for (int i = MIN_MESSAGE_SIZE - 1; i < bytes; i++) {
        if (buffer[i] == '\0') {
            message_end = i;
            break;
        }
    }

    if (message_end == -1) {
        printf("No null terminator found in message from %s\n", client->address_str);
        return 0;
    }

    // Extract message components
    unsigned int msg_num = ntohl(*(unsigned int*)buffer);
    unsigned char day = buffer[4];
    unsigned char month = buffer[5];
    unsigned short year = ntohs(*(unsigned short*)(buffer + 6));
    short aa = ntohs(*(short*)(buffer + 8));
    char phone[13] = {0};
    strncpy(phone, buffer + 10, 12);
    char *message = buffer + 22;

    // Validate message components
    if (day < 1 || day > 31 || month < 1 || month > 12 || year > 9999) {
        printf("Invalid date format from %s\n", client->address_str);
        return 0;
    }

    if (phone[0] != '+') {
        printf("Invalid phone format from %s\n", client->address_str);
        return 0;
    }

    // Send "ok" response
    if (send(client_socket, RESPONSE_OK, strlen(RESPONSE_OK), 0) < 0) {
        printf("Failed to send response to %s: %s\n", client->address_str, strerror(errno));
        return 0;
    }
    printf("OK sent to %s\n", client->address_str);

    // Log message in format: IP:port Message
    fprintf(log_file, "%s%s\n", client->address_str, message);
    fflush(log_file);

    // Check for stop condition
    if (strcmp(message, "stop") == 0) {
        *should_stop = 1;
    }

    // Shift remaining data in buffer
    int remaining = bytes - (message_end + 1);
    if (remaining > 0) {
        memmove(buffer, buffer + message_end + 1, remaining);
    }
    client->buffer_len = remaining;

    return 1;
}

// Remove disconnected client
void remove_client(struct client_info *clients, struct pollfd *poll_fds, int *client_count, int index, FILE *log_file) {
    //fprintf(log_file, "%sClient disconnected\n", clients[index].address_str);
    //fflush(log_file);
    close(clients[index].socket);
    poll_fds[index].fd = -1;
    memmove(&clients[index], &clients[index + 1], (*client_count - index - 1) * sizeof(struct client_info));
    memmove(&poll_fds[index], &poll_fds[index + 1], (*client_count - index - 1) * sizeof(struct pollfd));
    (*client_count)--;
}

// Clean up resources
void cleanup(int listen_socket, struct client_info *clients, int client_count, FILE *log_file) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket >= 0) {
            close(clients[i].socket);
        }
    }
    close(listen_socket);
    if (log_file) fclose(log_file);
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

    int listen_socket;
    if (setup_listening_socket(port, &listen_socket) != 0) {
        return 1;
    }
    printf("Listening socket setup complete\n");

    FILE *log_file = open_log_file("msg.txt");
    if (!log_file) {
        close(listen_socket);
        return 1;
    }
    printf("Log file 'msg.txt' opened\n");

    struct client_info clients[MAX_CLIENTS] = {0};
    struct pollfd poll_fds[MAX_CLIENTS + 1];
    int client_count = 0;
    init_poll_fds(poll_fds, listen_socket);

    int should_stop = 0;

    while (!should_stop) {
        int event_count = poll(poll_fds, MAX_CLIENTS + 1, 1000);
        if (event_count < 0) {
            printf("Poll error: %s\n", strerror(errno));
            break;
        }

        if (event_count > 0) {
            // Handle client events
            for (int i = 0; i < client_count; i++) {
                if (poll_fds[i].revents & (POLLHUP | POLLERR)) {
                    remove_client(clients, poll_fds, &client_count, i, log_file);
                    i--;
                    continue;
                }

                if (poll_fds[i].revents & POLLIN) {
                    int bytes = recv(clients[i].socket, clients[i].buffer + clients[i].buffer_len,
                                    BUFFER_SIZE - clients[i].buffer_len, 0);
                    if (bytes <= 0) {
                        if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                            remove_client(clients, poll_fds, &client_count, i, log_file);
                            i--;
                        }
                        continue;
                    }

                    clients[i].buffer_len += bytes;
                    printf("Received %d bytes from %s, total buffered: %d\n",
                           bytes, clients[i].address_str, clients[i].buffer_len);

                    // Process all complete messages in buffer
                    while (clients[i].buffer_len > 0) {
                        int result = process_message(clients[i].socket, &clients[i], log_file, &should_stop);
                        if (result == 0 && clients[i].buffer_len >= BUFFER_SIZE) {
                            printf("Buffer full for %s, discarding data\n", clients[i].address_str);
                            clients[i].buffer_len = 0;
                        }
                        if (result == 0) break; // No complete message yet
                    }
                }
            }

            // Accept new connections
            if (poll_fds[MAX_CLIENTS].revents & POLLIN) {
                accept_client(listen_socket, clients, poll_fds, &client_count);
            }
        }
    }

    cleanup(listen_socket, clients, client_count, log_file);
    return 0;
}
