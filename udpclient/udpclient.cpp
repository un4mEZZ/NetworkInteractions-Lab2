#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#define MAX_MSG_SIZE        1472  // 1500 - 20 - 8
#define FIXED_FIELDS_SIZE   23    // 4+4+2+12+1
#define MAX_MESSAGE_FIELD   (MAX_MSG_SIZE - FIXED_FIELDS_SIZE)
#define TIMEOUT_MS          100
#define MAX_ACKS            20

// Structure to hold message information
struct message {
    int msg_num;
    char line[4096];
    int sent;
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host:port> <filename>\n", argv[0]);
        return 1;
    }

    // Parse host:port
    char *host = strtok(argv[1], ":");
    char *port_str = strtok(NULL, ":");
    if (!host || !port_str) {
        fprintf(stderr, "Invalid host:port format\n");
        return 1;
    }
    int port = atoi(port_str);

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    // Set up server address
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid host address\n");
        close(sock);
        return 1;
    }

    // Open input file
    FILE *file = fopen(argv[2], "r");
    if (!file) {
        fprintf(stderr, "Failed to open file %s: %s\n", argv[2], strerror(errno));
        close(sock);
        return 1;
    }

    // Read messages from file
    struct message messages[1000];
    int message_count = 0;
    char line[4096];
    while (fgets(line, sizeof(line), file) && message_count < 1000) {
        if (strlen(line) <= 1) continue;        // Skip empty lines
        messages[message_count].msg_num = message_count;
        strncpy(messages[message_count].line, line, sizeof(messages[message_count].line)-1);
        messages[message_count].line[sizeof(messages[message_count].line)-1] = '\0';
        messages[message_count].sent = 0;
        message_count++;
    }
    fclose(file);

    if (message_count == 0) {
        fprintf(stderr, "No valid messages in file\n");
        close(sock);
        return 1;
    }

    // Track acknowledged messages
    int acks[MAX_ACKS];
    int ack_count = 0;
    int target_acks = message_count < 20 ? message_count : 20;

    // Set up select for timeout
    struct timeval timeout;
    fd_set read_fds;

    // Main loop
    while (ack_count < target_acks) {
        // Send unacknowledged messages
        for (int i = 0; i < message_count; i++) {
            if (messages[i].sent) continue;

            // Parse line
            char date[11], phone[13], message_text[4096];
            short aa;
            if (sscanf(messages[i].line, "%10s %hd %12s %[^\n]", date, &aa, phone, message_text) != 4) {
                fprintf(stderr, "Invalid line format: %s", messages[i].line);
                continue;
            }

            // Parse date
            unsigned char day, month;
            unsigned short year;
            if (sscanf(date, "%hhu.%hhu.%hu", &day, &month, &year) != 3) {
                fprintf(stderr, "Invalid date format: %s\n", date);
                continue;
            }

            // Prepare datagram
            size_t msg_len = strlen(message_text);
            if (msg_len > MAX_MESSAGE_FIELD) {
                msg_len = MAX_MESSAGE_FIELD;
                message_text[msg_len] = '\0';
            }

            char buffer[MAX_MSG_SIZE];
            int offset = 0;
            unsigned int msg_num_net = htonl(messages[i].msg_num);
            memcpy(buffer + offset, &msg_num_net, 4); offset += 4;
            buffer[offset++] = day;
            buffer[offset++] = month;
            unsigned short year_net = htons(year);
            memcpy(buffer + offset, &year_net, 2); offset += 2;
            short aa_net = htons(aa);
            memcpy(buffer + offset, &aa_net, 2); offset += 2;
            memcpy(buffer + offset, phone, 12); offset += 12;
            memcpy(buffer + offset, message_text, msg_len);
            offset += msg_len;
            buffer[offset++] = '\0';

            // Send datagram
            if (sendto(sock, buffer, offset, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("sendto");
                continue;
            }
            printf("Sent message %d\n", messages[i].msg_num);
            messages[i].sent = 1;
        }

        // Check for acknowledgments
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MS * 1000;

        int ready = select(sock + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) { perror("select"); break; }

        if (ready > 0 && FD_ISSET(sock, &read_fds)) {
            //ack_buffer = 80
            char ack_buffer[MAX_ACKS * 4];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            int bytes = recvfrom(sock, ack_buffer, sizeof(ack_buffer), 0, (struct sockaddr*)&from_addr, &from_len);
            if (bytes < 0) { perror("recvfrom"); continue; }
            if (bytes % 4 != 0) { fprintf(stderr, "Invalid ACK size: %d\n", bytes); continue; }

            // Process acknowledgments
            int new_acks = bytes / 4;
            for (int i = 0; i < new_acks; i++) {
                unsigned int ack_num = ntohl(*(unsigned int*)(ack_buffer + i * 4));
                int found = 0;
                for (int j = 0; j < ack_count; j++) {
                    if (acks[j] == ack_num) { found = 1; break; }
                }
                if (!found && ack_count < MAX_ACKS) {
                    acks[ack_count++] = ack_num;
                    printf("Received ACK for message %d\n", ack_num);
                    for (int j = 0; j < message_count; j++) {
                        if (messages[j].msg_num == ack_num) {
                            messages[j].sent = 1;
                            break;
                        }
                    }
                }
            }
        } else {
            // Timeout: reset sent status for unacknowledged messages
            for (int i = 0; i < message_count; i++) {
                int acked = 0;
                for (int j = 0; j < ack_count; j++) {
                    if (acks[j] == messages[i].msg_num) { acked = 1; break; }
                }
                if (!acked) messages[i].sent = 0;       // Resend unacknowledged messages
            }
        }
    }

    close(sock);
    return 0;
}