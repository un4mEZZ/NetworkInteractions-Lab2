TCP client files:

tcpclient.cpp - код основного клиента (put) Windows.
Отправляет на TCP-сервер сообщения из файла.
Запуск: tcpclient.exe <server_ip>:<port> <src_file>

tcpclient2.cpp - код клиента из доп.задания (get) Windows.
Отправляет на TCP-сервер запрос получение сообщений из файла msg.txt сервера и вывод их в файл.
Запуск: tcpclient2.exe <server_ip>:<port> get <dest_file>
Пример вывода - get.txt

tcpseveremul.rb - эмулятор TCP-сервера.
Запуск: ruby tcpserveremul.rb <port>

tcp_cl_multi.bat - запуск нескольких клинетов (проверка poll).
Адрес настраивается в файле.

Тесты:
file1.txt - Message длиной 1500 байт (должен обрезаться)
test_invalid.txt - DOP Проверка некорректных строк
Для tcp_cl_multi.bat:
test2.txt - 23 сообщения
test1.txt = test3.txt - 23 сообщения + stop
