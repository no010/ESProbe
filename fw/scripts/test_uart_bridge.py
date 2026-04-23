#!/usr/bin/env python3
"""
Test script for ESProbe TCP-to-UART bridge.

The bridge listens on TCP port 1234 (default). After connection:
  1. The first packet of digits (1~7 bytes) is interpreted as a baudrate
     command, e.g. sending b"115200" switches the UART to 115200 baud.
  2. Everything afterwards is transparently forwarded:
       TCP TX  -> UART TX (GPIO4 on ESP32-C3)
       UART RX -> TCP RX  (GPIO5 on ESP32-C3)

Hardware setup for loopback test:
  - Short GPIO4 (UART1 TX) and GPIO5 (UART1 RX) on the ESProbe board.
  - In this mode every byte sent from TCP should echo back.

Usage examples:
  python test_uart_bridge.py --host 192.168.1.100 --loopback
  python test_uart_bridge.py --host 192.168.1.100 --baud 921600 --throughput 10
"""

import argparse
import socket
import sys
import time

DEFAULT_HOST = "192.168.1.100"
DEFAULT_PORT = 1234
DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 5.0


def connect(host: str, port: int, timeout: float = DEFAULT_TIMEOUT):
    """Establish TCP connection to the UART bridge."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect((host, port))
    print(f"[+] Connected to {host}:{port}")
    return sock


def set_baudrate(sock: socket.socket, baud: int):
    """Send baudrate command as the first packet."""
    cmd = str(baud).encode()
    if not (1 <= len(cmd) <= 7):
        raise ValueError("Baudrate string length must be 1~7 digits")
    if not cmd.isdigit():
        raise ValueError("Baudrate must be pure digits")
    sock.sendall(cmd)
    print(f"[>] Sent baudrate command: {baud}")
    # Small delay to let firmware apply the new baudrate
    time.sleep(0.2)


def send_recv(sock: socket.socket, data: bytes, timeout: float = 2.0) -> bytes:
    """Send data and read back whatever is available within timeout."""
    sock.settimeout(timeout)
    sock.sendall(data)
    received = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            received += chunk
        except socket.timeout:
            break
    return received


def test_loopback(sock: socket.socket, iterations: int = 100):
    """Loopback test: send random-ish data and verify echo."""
    print(f"[*] Loopback test: {iterations} rounds")
    failures = 0
    for i in range(iterations):
        payload = bytes([i % 256] * 64) + b"ESProbe\n"
        received = send_recv(sock, payload, timeout=0.5)
        if received != payload:
            failures += 1
            print(f"[!] Mismatch round {i}: sent {len(payload)}B, recv {len(received)}B")
            if failures >= 3:
                print("[-] Too many failures, aborting loopback test")
                return False
    print(f"[+] Loopback test passed ({iterations} rounds, 0 failures)")
    return True


def test_throughput(sock: socket.socket, duration_sec: int = 10):
    """Throughput test: flood data and measure bytes/sec."""
    payload = b"A" * 512  # match firmware UART_BUF_SIZE
    print(f"[*] Throughput test: flooding for {duration_sec}s")

    sock.setblocking(False)
    tx_total = 0
    rx_total = 0
    t0 = time.time()

    while time.time() - t0 < duration_sec:
        try:
            sock.sendall(payload)
            tx_total += len(payload)
        except (BlockingIOError, OSError):
            pass

        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                rx_total += len(chunk)
            except (BlockingIOError, OSError):
                break

    elapsed = time.time() - t0
    print(f"[+] TX: {tx_total} bytes  RX: {rx_total} bytes  Time: {elapsed:.1f}s")
    print(f"    TX throughput: {tx_total * 8 / elapsed / 1000:.1f} kbps")
    print(f"    RX throughput: {rx_total * 8 / elapsed / 1000:.1f} kbps")
    return tx_total, rx_total


def interactive_terminal(sock: socket.socket):
    """Simple interactive terminal (half-duplex line mode)."""
    print("[*] Interactive mode. Type lines to send, Ctrl+C to quit.")
    sock.settimeout(0.1)
    try:
        while True:
            line = input("> ")
            if not line:
                continue
            sock.sendall((line + "\r\n").encode())
            # Drain RX buffer
            time.sleep(0.05)
            try:
                while True:
                    chunk = sock.recv(1024)
                    if not chunk:
                        break
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.flush()
            except socket.timeout:
                pass
    except KeyboardInterrupt:
        print("\n[*] Interrupted by user")


def main():
    parser = argparse.ArgumentParser(description="ESProbe TCP-UART bridge tester")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Target IP (default: 192.168.1.100)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="TCP port (default: 1234)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baudrate to set (default: 115200)")
    parser.add_argument("--loopback", action="store_true", help="Run loopback echo test")
    parser.add_argument("--throughput", type=int, metavar="SEC", help="Run throughput test for N seconds")
    parser.add_argument("--interactive", action="store_true", help="Interactive terminal mode")
    parser.add_argument("--no-baud-cmd", action="store_true", help="Skip sending baudrate command on connect")
    args = parser.parse_args()

    sock = connect(args.host, args.port)

    try:
        if not args.no_baud_cmd:
            set_baudrate(sock, args.baud)

        if args.loopback:
            ok = test_loopback(sock)
            sys.exit(0 if ok else 1)

        if args.throughput:
            test_throughput(sock, args.throughput)
            return

        if args.interactive:
            interactive_terminal(sock)
            return

        # Default: simple ping-pong
        print("[*] Default test: send/receive a greeting")
        greeting = b"Hello ESProbe UART Bridge\n"
        received = send_recv(sock, greeting)
        print(f"[>] Sent: {greeting}")
        print(f"[<] Received: {received}")
        if greeting in received:
            print("[+] Echo OK (GPIO4/5 may be shorted)")
        else:
            print("[!] No echo received (expected if TX/RX are not looped)")

    finally:
        sock.close()
        print("[*] Disconnected")


if __name__ == "__main__":
    main()
