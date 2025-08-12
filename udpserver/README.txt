UDP server files:

udpserver.cpp - код сервера Windows.
Запуск: udpserver.exe <start_port> <end_port>
Прослушивает порты от start_port до end_port включительно.
Записывает входящие сообщения от клиентов в msg.txt.

udpclientemul.rb - эмулятор UDP-клиента.
Запуск: ruby udpclientemul.rb <server_ip>:<port> <file>