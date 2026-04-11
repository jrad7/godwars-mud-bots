#!/usr/bin/env python3
"""
LLM-to-MUD Bridge: Connects a local LLM to the Dystopia MUD via telnet.
The LLM receives MUD output and decides what commands to send.
"""

import argparse
import json
import re
import socket
import time
import urllib.request

# --- Configuration ---
MUD_HOST = "localhost"
MUD_PORT = 8000
LLM_HOST = "localhost"
LLM_PORT = 8002
CHAR_NAME = "Qwenchat"
CHAR_PASSWORD = "qwenchat"

# How long to wait for more MUD output before sending to LLM (seconds)
READ_TIMEOUT = 2.0
# Delay between sending commands to MUD (seconds)
COMMAND_DELAY = 0.5
# Max conversation history entries to keep (pairs of user/assistant messages)
# Keep low for small context models like glm-4-9b-chat (4096 tokens)
MAX_HISTORY = 10
# Max characters of MUD output to include per message
MAX_MUD_OUTPUT = 1000

SYSTEM_PROMPT = """You are playing a text-based MUD (Multi-User Dungeon) called Dystopia. You are a player character interacting with the game world.

IMPORTANT RULES:
1. Respond ONLY with the MUD command you want to execute, one per line.
2. Do NOT include any explanation, reasoning, or commentary - ONLY commands.
3. Common commands: look, north, south, east, west, up, down, kill <target>, score, recall, equipment, train, help
4. Fight monsters, you want to slay all enemies!
5. If you're stuck or confused, try: look or recall
6. NEVER send more than 1 command at once!
8. The command 'train' can be used to spend experience points, for example, 'train hp all'.
9. DO NOT fight the Executioner.
10. If lost, recall, then go down, to kill enemies in heaven for experience.


Your goal to get experience points and pick a class!

To pick a class do the following:
1. Kill monsters to gain XP.
2. Spend XP on health points, 'train hp all'.
3. When over 2000 health points, 'train avatar'.
4. once an avatar, type 'selfclass', read the output, and pick a class you want to play.
5. To become the class, once avatar, type 'selfclass <class_name>'.

"""


def strip_ansi(text):
    """Remove ANSI escape sequences and MUD color codes from text."""
    # Strip ANSI escape sequences
    text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)
    # Strip MUD-specific color codes like #R, #G, #n, #0-#7, etc.
    text = re.sub(r'#[0-9a-zA-Z]', '', text)
    # Strip other control characters (but keep newlines, carriage returns)
    text = re.sub(r'[\x00-\x09\x0b\x0c\x0e-\x1f\x7f]', '', text)
    return text


def connect_to_mud_with(host, port):
    """Establish a telnet connection to the MUD."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    sock.setblocking(False)
    print(f"[Bridge] Connected to MUD at {host}:{port}")
    return sock


def read_mud(sock, timeout=READ_TIMEOUT):
    """Read all available output from the MUD, waiting for a pause in output."""
    output = b""
    deadline = time.time() + timeout
    last_data_time = time.time()

    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            break

        try:
            chunk = sock.recv(4096)
            if not chunk:
                raise ConnectionError("MUD closed the connection")
            output += chunk
            last_data_time = time.time()
            # Extend deadline a bit when we get data, to catch multi-part output
            deadline = max(deadline, last_data_time + 0.5)
        except BlockingIOError:
            # No data available right now
            if output and (time.time() - last_data_time) > 0.5:
                # We have some output and haven't received anything for 0.5s
                break
            time.sleep(0.05)
        except ConnectionError:
            raise

    text = output.decode('utf-8', errors='replace')
    return text


def send_mud(sock, command):
    """Send a command to the MUD."""
    sock.sendall((command + "\n").encode('utf-8'))


def query_llm(history, llm_host=LLM_HOST, llm_port=LLM_PORT, model="default"):
    """Send conversation history to the local LLM and get a response."""
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(history)

    payload = {
        "model": model,
        "messages": messages,
        "max_tokens": 100,
        "temperature": 0.7,
    }

    data = json.dumps(payload).encode('utf-8')
    req = urllib.request.Request(
        f"http://{llm_host}:{llm_port}/v1/chat/completions",
        data=data,
        headers={"Content-Type": "application/json"},
    )

    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            result = json.loads(resp.read().decode('utf-8'))
            return result["choices"][0]["message"]["content"].strip()
    except urllib.error.HTTPError as e:
        body = e.read().decode('utf-8', errors='replace') if e.fp else ''
        print(f"[Bridge] LLM HTTP {e.code}: {body[:200]}")
        if e.code == 400 or 'token' in body.lower() or 'length' in body.lower():
            raise TokenLimitError()
        return "look"
    except Exception as e:
        print(f"[Bridge] LLM error: {e}")
        return "look"  # Safe fallback command


class TokenLimitError(Exception):
    """Raised when the LLM rejects a request due to token limits."""
    pass


def parse_commands(llm_response):
    """Extract MUD commands from LLM response, filtering out commentary."""
    lines = llm_response.strip().split('\n')
    commands = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        # Skip lines that look like commentary rather than commands
        if line.startswith(('*', '-', '(', '[', '#', '//')):
            continue
        # Skip very long lines (probably explanations, not commands)
        if len(line) > 200:
            continue
        commands.append(line)
        if len(commands) >= 3:
            break
    return commands if commands else ["look"]


def handle_login(sock, char_name, char_password):
    """Handle the MUD login sequence."""
    print("[Bridge] Starting login sequence...")

    # Read initial banner/prompt
    output = read_mud(sock, timeout=5.0)
    print(f"[MUD] {strip_ansi(output)[:200]}")

    # Send character name
    print(f"[Bridge] Sending name: {char_name}")
    send_mud(sock, char_name)
    time.sleep(1)

    # Read password prompt
    output = read_mud(sock, timeout=5.0)
    clean = strip_ansi(output)
    print(f"[MUD] {clean[:200]}")

    # Send password
    print("[Bridge] Sending password...")
    send_mud(sock, char_password)
    time.sleep(1)

    # Read MOTD / login messages / initial room
    output = read_mud(sock, timeout=5.0)
    clean = strip_ansi(output)
    print(f"[MUD] {clean[:500]}")

    return clean


def main():
    parser = argparse.ArgumentParser(description="LLM-to-MUD Bridge")
    parser.add_argument("--mud-host", default=MUD_HOST, help="MUD hostname")
    parser.add_argument("--mud-port", type=int, default=MUD_PORT, help="MUD port")
    parser.add_argument("--llm-host", default=LLM_HOST, help="LLM API hostname")
    parser.add_argument("--llm-port", type=int, default=LLM_PORT, help="LLM API port")
    parser.add_argument("--model", default="glm-4-9b-chat", help="LLM model name to request")
    parser.add_argument("--name", default=CHAR_NAME, help="Character name")
    parser.add_argument("--password", default=CHAR_PASSWORD, help="Character password")
    args = parser.parse_args()

    sock = connect_to_mud_with(args.mud_host, args.mud_port)
    conversation_history = []

    try:
        # Login
        initial_output = handle_login(sock, args.name, args.password)
        conversation_history.append({
            "role": "user",
            "content": f"[You have just logged into the MUD. Here is what you see:]\n{initial_output}"
        })

        # Enable LLM brief mode (structured output for AI consumption)
        send_mud(sock, "llmbrief")
        time.sleep(0.5)
        output = read_mud(sock, timeout=READ_TIMEOUT)
        print(f"[Bridge] LLM mode: {strip_ansi(output).strip()[:100]}")

        print("[Bridge] Login complete. Starting game loop...")
        print("=" * 60)

        # Main game loop
        while True:
            # Ask LLM what to do
            try:
                response = query_llm(conversation_history, llm_host=args.llm_host, llm_port=args.llm_port, model=args.model)
            except TokenLimitError:
                print("[Bridge] Token limit hit — resetting conversation history")
                conversation_history = []
                send_mud(sock, "look")
                time.sleep(COMMAND_DELAY)
                output = read_mud(sock, timeout=READ_TIMEOUT)
                clean = strip_ansi(output)
                if clean.strip():
                    conversation_history.append({"role": "user", "content": clean.strip()[:MAX_MUD_OUTPUT]})
                continue
            commands = parse_commands(response)

            print(f"[LLM] {response}")
            print(f"[Bridge] Sending commands: {commands}")

            # Record LLM's response
            conversation_history.append({
                "role": "assistant",
                "content": response
            })

            # Send each command to MUD
            all_output = ""
            for cmd in commands:
                send_mud(sock, cmd)
                time.sleep(COMMAND_DELAY)

                # Read MUD response after each command
                output = read_mud(sock, timeout=READ_TIMEOUT)
                clean = strip_ansi(output)
                if clean.strip():
                    all_output += clean + "\n"
                    print(f"[MUD] {clean[:500]}")

            # If no output was received, do a brief extra wait
            if not all_output.strip():
                time.sleep(1.0)
                output = read_mud(sock, timeout=READ_TIMEOUT)
                clean = strip_ansi(output)
                if clean.strip():
                    all_output = clean
                    print(f"[MUD] {clean[:500]}")

            if not all_output.strip():
                all_output = "(No visible response from the MUD)"

            # Truncate and add MUD output to history
            trimmed = all_output.strip()
            if len(trimmed) > MAX_MUD_OUTPUT:
                trimmed = trimmed[:MAX_MUD_OUTPUT] + "\n[...output truncated...]"
            conversation_history.append({
                "role": "user",
                "content": trimmed
            })

            # Trim history to stay within context limits
            if len(conversation_history) > MAX_HISTORY:
                conversation_history = conversation_history[-MAX_HISTORY:]

            print("-" * 40)

    except KeyboardInterrupt:
        print("\n[Bridge] Shutting down...")
        try:
            send_mud(sock, "quit")
            time.sleep(0.5)
        except:
            pass
    except ConnectionError as e:
        print(f"[Bridge] Connection lost: {e}")
    finally:
        sock.close()
        print("[Bridge] Disconnected.")


if __name__ == "__main__":
    main()
