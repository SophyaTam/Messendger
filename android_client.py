#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Android-клиент мессенджера (Python + Termux)
Поддержка UTF-8 и AES-128 шифрования
Все команды: /msg, /list, /quit, /help, /history,
             /thread, /reply, /forward,
             /group create/join/msg, /register
"""

import socket
import sys
import threading
from Crypto.Cipher import AES
import binascii

SERVER_PORT = 7777        # Порт сервера
sock = None               # Сокет соединения
running = True            # Флаг работы клиента
last_sender = ""          # Последний отправитель для /reply

# Ключ и вектор инициализации AES-128 (должны совпадать с сервером)
KEY = b'messenger2026key'  # 16 байт
IV = KEY                   # Упрощённо: IV = ключ

def encrypt_hex(plaintext):
    """Зашифровать текст в hex-строку (AES-128-CBC)"""
    try:
        raw = plaintext.encode('utf-8')  # Текст → байты UTF-8
        # PKCS7 padding: дополняем до кратности 16 байт
        pad_len = 16 - (len(raw) % 16)
        raw = raw + bytes([pad_len] * pad_len)
        cipher = AES.new(KEY, AES.MODE_CBC, IV)   # Создаём шифр
        encrypted = cipher.encrypt(raw)             # Шифруем
        return binascii.hexlify(encrypted).decode()  # Байты → hex-строка
    except:
        return None

def decrypt_hex(hex_str):
    """Расшифровать hex-строку (AES-128-CBC)"""
    try:
        raw = binascii.unhexlify(hex_str)    # Hex-строка → байты
        cipher = AES.new(KEY, AES.MODE_CBC, IV)  # Создаём шифр
        plain = cipher.decrypt(raw)           # Расшифровываем
        pad_len = plain[-1]                    # Убираем PKCS7 padding
        return plain[:-pad_len].decode('utf-8', errors='replace')
    except:
        return None

def receive_messages():
    global running, last_sender
    while running:
        try:
            data = sock.recv(4096)    # Принимаем данные
            if not data:
                print("\n[CLIENT] Server disconnected")
                running = False
                break
                 # Декодируем и убираем \r (для Termux)
            text = data.decode('utf-8', errors='replace').replace('\r', '')
            lines = text.split('\n')
            for line in lines:
                if not line:
                    continue
                     # Расшифровка зашифрованных сообщений
                if line.startswith("ENC:"):
                    decrypted = decrypt_hex(line[4:])
                    if decrypted:
                     # Сохраняем отправителя для /reply
                        if decrypted.startswith("[От ") or decrypted.startswith("[From "):
                            end_bracket = decrypted.index(']')
                            prefix, sender = decrypted[1:end_bracket].split(' ', 1)
                            last_sender = sender
                        print(f"\r{decrypted}")
                    continue
                    # Уведомление о выключении сервера
                if line.startswith("SERVER_SHUTDOWN"):
                    print(f"\r[Server] {line}")
                    running = False
                    break
                     # Ответы сервера
                if line.startswith("OK") and len(line) <= 4:
                    print("\r[OK]")
                elif line.startswith("ERROR"):
                    print(f"\r[Error] {line[6:]}")
                elif line == "HISTORY_BEGIN":
                    print("\n=== History ===")
                elif line == "HISTORY_END":
                    print("=== End ===")
                elif line == "HISTORY_EMPTY":
                    print("\n(empty)")
                elif line == "OFFLINE_BEGIN":
                    print("\n=== Missed messages ===")
                elif line == "OFFLINE_END":
                    print("=== End ===")
                elif line == "BYE":
                    print("[Server] Bye!")
                    running = False
                    break
                elif line == "UNKNOWN Unknown command":  # Игнорируем
                    pass
                else:
                    print(f"\r{line}")
        except:
            running = False
            break

def main():
    global sock
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"   # IP сервера из аргумента командной строки или localhost

    # Подключаемся к серверу
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, SERVER_PORT))
    except:
        print(f"Failed to connect to {host}:{SERVER_PORT}")
        return

    print(f"[CLIENT] Connected to {host}:{SERVER_PORT}")

    # Аутентификация
    logged_in = False
    while not logged_in:
        login = input("Login (/register user pass): ").strip()
        if login.startswith("/register "):
         # Регистрация нового пользователя
            sock.sendall(f"REGISTER {login[10:]}\n".encode())
            resp = sock.recv(256).decode().strip()
            print(f"[Server] {resp}")
            continue
        pwd = input("Password: ").strip()
         # Отправка LOGIN
        sock.sendall(f"LOGIN {login} {pwd}\n".encode())
        resp = sock.recv(256).decode().strip()
        print(f"[Server] {resp}")
        if resp.startswith("OK"):
            logged_in = True


     # Запуск потока приёма сообщений
    threading.Thread(target=receive_messages, daemon=True).start()

    # Справка по командам
    print("\nCommands: /msg, /list, /quit, /help, /history, /thread")
    print("  /reply, /forward, /group create/join/msg, /register")

    # Главный цикл ввода команд
    while running:
        try:
            cmd = input("> ").strip()
        except EOFError:
            break
        if not cmd:
            continue
        # Выход
        if cmd in ("/quit", "/exit"):
            sock.sendall(b"EXIT\n")
            break
        # Список онлайн
        elif cmd == "/list":
            sock.sendall(b"LIST\n")
            # Отправка личного сообщения (шифруется)
        elif cmd.startswith("/msg "):
            parts = cmd[5:].split(' ', 1)
            if len(parts) == 2:
                enc = encrypt_hex(parts[1])
                if enc:
                    sock.sendall(f"ENC:SEND {parts[0]} {enc}\n".encode())
         # Ответ на последнее сообщение (шифруется)
        elif cmd.startswith("/reply "):
            if last_sender:
                enc = encrypt_hex(cmd[7:])
                if enc:
                    sock.sendall(f"ENC:SEND {last_sender} {enc}\n".encode())
            else:
                print("No message to reply to.")
         # Пересылка
        elif cmd.startswith("/forward "):
            sock.sendall(f"FORWARD {cmd[9:]}\n".encode())
         # Ответ в тред (шифруется)
        elif cmd.startswith("/thread "):
            parts = cmd[8:].split(' ', 1)
            if len(parts) == 2:
                enc = encrypt_hex(parts[1])
                if enc:
                    sock.sendall(f"ENC:THREAD {parts[0]} {enc}\n".encode())
         # Создание группы
        elif cmd.startswith("/group create "):
            sock.sendall(f"GROUP_CREATE {cmd[14:]}\n".encode())
          # Вход в группу
        elif cmd.startswith("/group join "):
            sock.sendall(f"GROUP_JOIN {cmd[12:]}\n".encode())
         # Сообщение в группу
        elif cmd.startswith("/group msg "):
            parts = cmd[11:].split(' ', 1)
            if len(parts) == 2:
                sock.sendall(f"GROUP_MSG {parts[0]} {parts[1]}\n".encode())
          # История
        elif cmd == "/history":
            sock.sendall(b"HISTORY\n")
            # Справка
        elif cmd == "/help":
            print("Commands: /msg, /list, /quit, /help, /history, /thread")
            print("  /reply, /forward, /group create/join/msg, /register")
        else:
            print("Unknown command. /help for help.")

    sock.close()
    print("[CLIENT] Done.")

if __name__ == "__main__":
    main()