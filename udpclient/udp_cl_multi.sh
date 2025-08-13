#!/bin/bash

# ------------------------------
# Настройки
SERVER_IP="192.168.112.1"
SERVER_PORT=9000
CLIENT_BIN="./udpclient"

# Файлы для теста (по одному на клиента)
FILES=("test1.txt" "test2.txt" "test3.txt")

# ------------------------------
# Проверка наличия клиента
if [[ ! -x "$CLIENT_BIN" ]]; then
    echo "Ошибка: бинарник клиента '$CLIENT_BIN' не найден или не исполняемый."
    exit 1
fi

# ------------------------------
# Запуск нескольких клиентов
echo "Запуск ${#FILES[@]} клиентов на сервер ${SERVER_IP}:${SERVER_PORT}..."

for file in "${FILES[@]}"; do
    if [[ ! -f "$file" ]]; then
        echo "Ошибка: файл '$file' не найден!"
        exit 1
    fi
    "$CLIENT_BIN" "${SERVER_IP}:${SERVER_PORT}" "$file" &
    PIDS+=($!)
done

# ------------------------------
# Ожидание завершения всех клиентов
for pid in "${PIDS[@]}"; do
    wait "$pid"
done

echo "Все клиенты завершили работу."

