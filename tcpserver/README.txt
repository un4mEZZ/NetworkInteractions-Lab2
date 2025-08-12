TCP server files:

tcpclient.cpp - код сервера Linux.
Запуск: tcpserver.exe <port>

Записывает входящие сообщения от клиентов в msg.txt.

tcpclientemul.rb - эмулятор TCP-клиента.
Запуск: ruby tcpclientemul.rb <server_ip>:<port> <file>