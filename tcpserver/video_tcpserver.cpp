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
    //?????????????????????????
    char address_str[INET_ADDRSTRLEN + 10]; // IP:port
};

// Set socket to non-blocking mode
int set_non_blocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
}

// Create and configure listening socket
int setup_listening_socket(int port, int listen_socket) {

    // 1. Create listen_socket
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        printf("Failed to create socket: %s\n", strerror(errno));
        return 1;
    }
    // 2. Setsocktopt
    int opt = 1;
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("Setsockopt failed: %s\n", strerror(errno));
        close(listen_socket);
        return 1;
    }

    // 3. Set listen_socket in non-blocking mode
    if (set_non_blocking(listen_socket) < 0) {
        printf("Failed to set non-blocking mode: %s\n", strerror(errno));
        close(listen_socket);
        return 1;
    }

    // 4. Listen_socket address (server_addr)
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    // 5. Link listen_socket and server_addr
    if (bind(listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Bind failed: %s\n", strerror(errno));
        close(listen_socket);
        return 1;
    }

    // 6. Start listening 
    if (listen(listen_socket, SOMAXCONN) < 0) {
        printf("Listen failed: %s\n", strerror(errno));
        close(listen_socket);
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
        poll_fds[i].events = POLLIN | POLLOUT;
    }
    poll_fds[MAX_CLIENTS].fd = listen_socket;
    poll_fds[MAX_CLIENTS].events = POLLIN;
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
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    snprintf(clients[*client_count].address_str, sizeof(clients[*client_count].address_str),
             "%s:%d ", ip_str, ntohs(client_addr.sin_port));

    poll_fds[*client_count].fd = new_socket;
    poll_fds[*client_count].events = POLLIN | POLLOUT;
    (*client_count)++;
    return 1;
}
/*
// Process client message
int process_message(int client_socket, char *buffer, int bytes, const char *client_address_str, FILE *log_file, int *should_stop) {
    if (bytes < MIN_MESSAGE_SIZE) {
        printf("Bytes = %d\n", bytes);
        printf("Received incomplete message from %s\n", client_address_str);
        return 0;
    }
    else {printf("Received complete message\n"); }
*/
int process_message(int client_socket, char *buffer, int bytes, const char *client_address_str, FILE *log_file, int *should_stop) {
    printf("Bytes = %d\n", bytes);
    if (bytes < MIN_MESSAGE_SIZE) {
        
        if (buffer[0]=='p' && buffer[1]=='u' && buffer[2]=='t') {
            printf("Recieved PUT from %s\n", client_address_str);
            return 0;
        } else {
            printf("Received incomplete message from %s\n", client_address_str);
            return 0;
        }
    }
    else {printf("Received complete message\n"); }
//_________________
    char *message = buffer + 4 + 4 + 2 + 12; // Skip index (4), date (4), AA (2), phone (12)
    int message_len = bytes - (4 + 4 + 2 + 12);
    printf("message_len=%d\n", message_len);
    if (message[message_len - 1] != '\0') {
        printf("Message from %s not null-terminated\n", client_address_str);
        return 0;
    }

    if (send(client_socket, RESPONSE_OK, strlen(RESPONSE_OK), 0) < 0) {
        printf("Failed to send response to %s: %s\n", client_address_str, strerror(errno));
        return 0;
    }
    else {
        printf("OK sent to %s: %s\n", client_address_str, strerror(errno));
    }
    fprintf(log_file, "%s%s\n", client_address_str, message);
    fflush(log_file);

    if (strcmp(message, "stop") == 0) {
        *should_stop = 1;
    }

    return 1;
}

// Remove disconnected client
void remove_client(struct client_info *clients, struct pollfd *poll_fds, int *client_count, int index, FILE *log_file) {
    fprintf(log_file, "%sClient disconnected\n", clients[index].address_str);
    fflush(log_file);
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

    // 1. Parse params
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("Invalid port number\n");
        return 1;
    }

    // 2. Create, start and configure listening socket
    int listen_socket;
    if (setup_listening_socket(port, &listen_socket) != 0) {
        return 1;
    }

    struct client_info clients[MAX_CLIENTS] = {0};
    struct pollfd poll_fds[MAX_CLIENTS + 1];
    int client_count = 0;
    init_poll_fds(poll_fds, listen_socket);

    FILE *log_file = open_log_file("msg.txt");
    if (!log_file) {
        close(listen_socket);
        return 1;
    }

    char buffer[BUFFER_SIZE];
    int should_stop = 0;



    while (!should_stop) {
        int event_count = poll(poll_fds, MAX_CLIENTS + 1, 1000);
        if (event_count < 0) {
            printf("Poll error: %s\n", strerror(errno));
            break;
        }

        if (event_count > 0) {
            for (int i = 0; i < client_count; i++) {
                if (poll_fds[i].revents & (POLLHUP | POLLERR)) {
                    remove_client(clients, poll_fds, &client_count, i, log_file);
                    i--;
                    continue;
                }

                if (poll_fds[i].revents & POLLIN) {
                    //int bytes = recv(clients[i].socket, buffer, BUFFER_SIZE - 1, 0);
                    int bytes = recv(clients[i].socket, buffer, BUFFER_SIZE, 0);
                    if (bytes > 0) {
                        process_message(clients[i].socket, buffer, bytes, clients[i].address_str, log_file, &should_stop);
                    }
                }
            }

            if (poll_fds[MAX_CLIENTS].revents & POLLIN) {
                accept_client(listen_socket, clients, poll_fds, &client_count);
            }
        }
    }

    cleanup(listen_socket, clients, client_count, log_file);
    return 0;
}
