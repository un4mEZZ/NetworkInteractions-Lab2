UDP client files:

udpclient.cpp - код основного клиента Linux.
Отправляет на UDP-сервер сообщения из файла.
Запуск: udpclient.exe <server_ip>:<port> <src_file>

udpserveremul.rb - эмулятор UDP-сервера.
Запуск: ruby udpserveremul.rb <port>

Тесты:
file1.txt - Message длиной 1500 байт (должен обрезаться)
