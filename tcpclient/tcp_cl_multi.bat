@echo off
REM ===============================
REM Конфигурация
set SERVER_IP=192.168.112.128
set SERVER_PORT=9000

REM Проверяем, что tcpclient.exe существует
if not exist tcpclient.exe (
    echo [ERROR] tcpclient.exe not found in current directory
    pause
    exit /b 1
)

REM Проверяем, что тестовые файлы существуют
if not exist test1.txt (
    echo [ERROR] test1.txt not found
    pause
    exit /b 1
)
if not exist test2.txt (
    echo [ERROR] test2.txt not found
    pause
    exit /b 1
)
if not exist test3.txt (
    echo [ERROR] test3.txt not found
    pause
    exit /b 1
)

REM ===============================
REM Запуск клиентов с задержкой отправки
REM Мы используем дополнительный параметр "delay",
REM который клиентский код будет распознавать как паузу между сообщениями.

echo === Запускаем клиентов для теста poll ===

start "" /B cmd /C "tcpclient.exe %SERVER_IP%:%SERVER_PORT% test1.txt"
start "" /B cmd /C "tcpclient.exe %SERVER_IP%:%SERVER_PORT% test2.txt"
start "" /B cmd /C "tcpclient.exe %SERVER_IP%:%SERVER_PORT% test3.txt"

REM ===============================
REM Ждем завершения клиентов
echo [INFO] Ожидание завершения клиентов...
timeout /t 10 >nul

REM ===============================
REM Показ лога сервера (если сервер запущен на этой же машине)
if exist msg.txt (
    echo === Содержимое msg.txt ===
    type msg.txt
) else (
    echo [WARN] Лог сервера msg.txt не найден локально.
)

echo === Тест завершён ===
pause
