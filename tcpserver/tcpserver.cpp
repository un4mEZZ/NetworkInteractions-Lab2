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

#define MAX_CLIENTS 100     // Максимальное число одновременно подключённых клиентов
#define BUFFER_SIZE 4096    // Размер входного буфера для каждого клиента
#define RESPONSE_OK "ok"    // Ответ, который отправляется клиенту после приёма сообщения

struct client_info {
    int socket;
    struct sockaddr_in address;
    char address_str[INET_ADDRSTRLEN + 10];     // IP:порт клиента в виде строки
    char recv_buffer[BUFFER_SIZE];              // Буфер для накопления входящих данных
    int recv_len;                               // Кол-во данных в буфере
    int put_received;                           // Флаг, что от клиента уже получена команда "put"
    int get_received;                           // Флаг, что от клиента уже получена команда "get"
};


// Устанавливает сокет в неблокирующий режим
int set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Гарантированная отправка всех данных
int send_all(int sock, const char *buf, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(sock, buf + total, len - total, 0);
        if (sent <= 0) return -1;
        total += sent;
    }
    return 0;
}

// Создание, настройка и запуск прослушивающего сокета
int setup_listening_socket(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }
    int opt = 1;
    // Разрешаем повторно использовать адрес и порт (SO_REUSEADDR)
    // Это нужно для того, чтобы при перезапуске сервера не ждать,
    // пока ОС освободит порт из состояния TIME_WAIT.
    // Без этой опции bind() может завершиться ошибкой EADDRINUSE,
    // если порт ещё «занят» предыдущим процессом.
    // Аргументы:
    // listen_fd  — дескриптор сокета,
    // SOL_SOCKET — уровень параметров (общие параметры сокета),
    // SO_REUSEADDR — конкретный параметр (разрешить повторное использование адреса),
    // &opt — указатель на переменную с ненулевым значением (включить опцию),
    // sizeof(opt) — размер этой переменной.
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (set_non_blocking(listen_fd) < 0) {
        perror("fcntl");
        close(listen_fd);
        return -1;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    // BIND
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    // LISTEN
    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }
    printf("Listening on port %d\n", port);
    return listen_fd;
}


// Отключение клиента и удаление его из списка
void remove_client(struct client_info *clients, struct pollfd *pfds, int *count, int idx) {
    close(clients[idx].socket);
    // Сдвигаем массивы клиентов и poll-структур
    memmove(&clients[idx], &clients[idx+1], (*count - idx - 1) * sizeof(struct client_info));
    memmove(&pfds[idx], &pfds[idx+1], (*count - idx - 1) * sizeof(struct pollfd));
    (*count)--;
}

// Обработка накопленных данных клиента
void process_client_buffer(struct client_info *cli, FILE *log_file, int *should_stop) {
    int offset = 0;
    while (offset < cli->recv_len) {

        // Если ещё не получили "put" или "get", проверяем их
        if (!cli->put_received && !cli->get_received) {
            if (cli->recv_len - offset >= 3) {
                if (memcmp(cli->recv_buffer + offset, "put", 3) == 0) {
                    cli->put_received = 1;
                    printf("[SERVER] Received 'put' from %s\n", cli->address_str);
                    offset += 3;
                    continue;
                } else if (memcmp(cli->recv_buffer + offset, "get", 3) == 0) {
                    cli->get_received = 1;
                    printf("[SERVER] Received 'get' from %s\n", cli->address_str);
                    offset += 3;

                    // отправляем содержимое msg.txt клиенту
                    FILE *in = fopen("msg.txt", "r");
                    if (in) {
                        char line[4096];
                        unsigned int msg_num = 0;
                        while (fgets(line, sizeof(line), in)) {
                            // формат: IP:port dd.mm.yyyy AA phone message
                            char ipport[64], date[16], phone[32], message[4096];
                            short aa;
                            if (sscanf(line, "%63s %15s %hd %31s %[^\n]", ipport, date, &aa, phone, message) == 5) {
                                unsigned char day, month;
                                unsigned short year;
                                if (sscanf(date, "%hhu.%hhu.%hu", &day, &month, &year) != 3) continue;

                                uint32_t num_net = htonl(msg_num++);
                                unsigned short year_net = htons(year);
                                short aa_net = htons(aa);

                                send_all(cli->socket, (char*)&num_net, 4);
                                send_all(cli->socket, (char*)&day, 1);
                                send_all(cli->socket, (char*)&month, 1);
                                send_all(cli->socket, (char*)&year_net, 2);
                                send_all(cli->socket, (char*)&aa_net, 2);

                                char phone_field[12] = {0};
                                strncpy(phone_field, phone, 12);
                                send_all(cli->socket, phone_field, 12);

                                send_all(cli->socket, message, strlen(message) + 1);
                                printf("sent line %d\n", msg_num);
                            }
                        }
                        fclose(in);
                    }

                    // закрываем соединение после отправки
                    shutdown(cli->socket, SHUT_RDWR);
                    return;
                }
            }
            break;
        }

        // Обработка "put"
        if (cli->put_received) {
            if (cli->recv_len - offset < 4 + 1 + 1 + 2 + 2 + 12 + 1)
                break;

            // Парсим сообщение
            unsigned int msg_num;
            memcpy(&msg_num, cli->recv_buffer + offset, 4);
            offset += 4;

            unsigned char day = cli->recv_buffer[offset++];
            unsigned char month = cli->recv_buffer[offset++];
            unsigned short year;
            memcpy(&year, cli->recv_buffer + offset, 2);
            offset += 2;
            short aa;
            memcpy(&aa, cli->recv_buffer + offset, 2);
            offset += 2;

            char phone[13];
            memcpy(phone, cli->recv_buffer + offset, 12);
            phone[12] = '\0';
            offset += 12;

            // Ищем нулевой байт конца строки
            int msg_start = offset;
            int found_null = 0;
            while (offset < cli->recv_len) {
                if (cli->recv_buffer[offset] == '\0') {
                    found_null = 1;
                    offset++;
                    break;
                }
                offset++;
            }
            if (!found_null) {
                // Сообщение ещё не полностью пришло — ждём
                offset = msg_start - (4+1+1+2+2+12);
                break;
            }

            // Логируем и отправляем подтверждение
            char *msg = cli->recv_buffer + msg_start;
            printf("[SERVER] Received message #%u from %s\n", ntohl(msg_num), cli->address_str);
            fprintf(log_file, "%s%02u.%02u.%04u %d %s %s\n",
                    cli->address_str, day, month, ntohs(year),
                    ntohs(aa), phone, msg);
            fflush(log_file);

            if (send_all(cli->socket, RESPONSE_OK, 2) == 0) {
                printf("[SERVER] Sent OK to %s\n", cli->address_str);
            } else {
                printf("[SERVER] Failed to send OK to %s\n", cli->address_str);
            }

            if (strcmp(msg, "stop") == 0) {
                printf("[SERVER] Received 'stop', shutting down\n");
                *should_stop = 1;
            }
        }
    }

    // Сдвигаем оставшиеся непрочитанные данные в начале буфера
    if (offset > 0) {
        memmove(cli->recv_buffer, cli->recv_buffer + offset, cli->recv_len - offset);
        cli->recv_len -= offset;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("Invalid port\n");
        return 1;
    }

    // Создаём прослушивающий сокет
    int listen_fd = setup_listening_socket(port);
    if (listen_fd < 0) return 1;

    // Открываем файл для записи принятых сообщений
    FILE *log_file = fopen("msg.txt", "w");
    if (!log_file) {
        perror("fopen");
        close(listen_fd);
        return 1;
    }

    struct client_info clients[MAX_CLIENTS];
    struct pollfd pfds[MAX_CLIENTS + 1];
    int client_count = 0;
    int should_stop = 0;

    // Главный цикл работы сервера
    while (!should_stop) {
        // Добавляем серверный сокет в список poll
        pfds[client_count].fd = listen_fd;
        pfds[client_count].events = POLLIN;
        int nfds = client_count + 1;

        // Добавляем клиентские сокеты в poll
        for (int i = 0; i < client_count; i++) {
            pfds[i].fd = clients[i].socket;
            pfds[i].events = POLLIN;
        }

        // Ждём событий
        int ret = poll(pfds, nfds, 1000);
        if (ret < 0) {
            perror("poll");
            break;
        }

        // Новое подключение
        if (pfds[client_count].revents & POLLIN) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (struct sockaddr*)&caddr, &clen);
            if (cfd >= 0 && client_count < MAX_CLIENTS) {
                set_non_blocking(cfd);
                clients[client_count].socket = cfd;
                clients[client_count].address = caddr;
                inet_ntop(AF_INET, &caddr.sin_addr, clients[client_count].address_str, INET_ADDRSTRLEN);
                char portbuf[8];
                sprintf(portbuf, ":%d ", ntohs(caddr.sin_port));
                strcat(clients[client_count].address_str, portbuf);
                clients[client_count].recv_len = 0;
                clients[client_count].put_received = 0;
                clients[client_count].get_received = 0;
                client_count++;
            } else {
                close(cfd);
            }
        }

        // Обработка данных от клиентов
        for (int i = 0; i < client_count; i++) {
            if (pfds[i].revents & (POLLHUP | POLLERR)) {
                remove_client(clients, pfds, &client_count, i);
                i--;
                continue;
            }
            if (pfds[i].revents & POLLIN) {
                int n = recv(clients[i].socket, 
                             clients[i].recv_buffer + clients[i].recv_len,
                             BUFFER_SIZE - clients[i].recv_len, 0);
                if (n <= 0) {
                    remove_client(clients, pfds, &client_count, i);
                    i--;
                    continue;
                }
                clients[i].recv_len += n;
                process_client_buffer(&clients[i], log_file, &should_stop);
                if (should_stop) break;
            }
        }
    }

    for (int i = 0; i < client_count; i++) {
        close(clients[i].socket);
    }
    close(listen_fd);
    fclose(log_file);
    return 0;
}