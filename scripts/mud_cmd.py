"""
mud_cmd.py - Send commands to the running MUD as Claude (or any character).

Usage:
    python mud_cmd.py "command1" "command2" ...

Example:
    python mud_cmd.py "setoverseer Kast" "who"
"""

import socket, time, sys

HOST = '127.0.0.1'
PORT = 8000
NAME = 'Claude'
PASS = 'AA-2Dmsan9rKJkIVX92ptw'


def recv_all(s):
    data = b''
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except:
        pass
    # Strip ANSI escape codes for cleaner output
    import re
    text = data.decode('latin-1', errors='replace')
    text = re.sub(r'\x1b\[[0-9;]*m', '', text)
    text = re.sub(r'\xff.', '', text)  # strip telnet IAC sequences
    return text


def send(s, cmd, delay=1.5):
    s.send((cmd + '\r\n').encode())
    time.sleep(delay)
    return recv_all(s)


def run_commands(commands, name=NAME, password=PASS):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    s.settimeout(3)

    time.sleep(1)
    recv_all(s)           # banner
    send(s, name)         # username
    send(s, password)     # password (consumes MOTD)

    results = []
    for cmd in commands:
        out = send(s, cmd)
        results.append((cmd, out))
        print(f'>>> {cmd}')
        print(out.strip())
        print()

    send(s, 'quit')
    s.close()
    return results


if __name__ == '__main__':
    cmds = sys.argv[1:] if len(sys.argv) > 1 else ['who']
    run_commands(cmds)
