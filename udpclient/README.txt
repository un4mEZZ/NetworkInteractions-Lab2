UDP client files:

udpclient.cpp - код основного клиента Linux.
Отправляет на UDP-сервер сообщения из файла.
Запуск: udpclient.exe <server_ip>:<port> <src_file>

udpserveremul.rb - эмулятор UDP-сервера.
Запуск: ruby udpserveremul.rb <port>

udp_cl_multi.sh - запуск нескольких клинетов (проверка select).
Адрес настраивается в файле.

Тесты:
file1.txt - Message длиной 1500 байт (должен обрезаться)
test_invalid.txt - DOP Проверка некорректных строк
Для udp_cl_multi.sh:
test2.txt - 23 сообщения
test1.txt = test3.txt - 23 сообщения + stop
