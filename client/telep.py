"""Shared TELEP/2.0 protocol module for nodes and operators.

ASCII text messages: fields separated by '|' and terminated by '\\n'.
Measurements travel as NAME=value pairs separated by ';'.
"""
import os
import select
import socket
import time

DEFAULT_HOST = os.environ.get("TELEP_SERVER_HOST", "localhost")
TCP_PORT = 5000
UDP_PORT = 5001
SEPARATOR = "|"
MAX_LINE = 1024
END_OF_LIST = "END"
ALERT_PREFIX = "ALERT" + SEPARATOR
DEFAULT_TIMEOUT_SECONDS = 5.0


def resolve_server_address(hostname):
    """Resolves the server's DNS name to an IPv4 address. Raises socket.gaierror if it does not exist."""
    address_info = socket.getaddrinfo(hostname, None, socket.AF_INET, socket.SOCK_STREAM)
    return address_info[0][4][0]


def encode_message(command, *fields):
    """Builds 'CMD|field1|field2\\n' ready to send."""
    parts = [command, *map(str, fields)]
    if any(not part or any(c in part for c in "|\r\n\x00") for part in parts):
        raise ValueError("empty field or protocol delimiter")
    line = SEPARATOR.join(parts)
    if len(line) + 1 > MAX_LINE:
        raise ValueError(f"message exceeds {MAX_LINE} bytes")
    return (line + "\n").encode("ascii")


def decode_message(line):
    """'OK|NODE01|...' -> ['OK', 'NODE01', ...] (without the newline)."""
    return line.rstrip("\r\n").split(SEPARATOR)


def encode_measurements(measurements):
    """{'TEMP': 24.8, 'STATUS': 'OK'} -> 'TEMP=24.80;STATUS=OK'"""
    parts = []
    for name, value in measurements.items():
        text = value if isinstance(value, str) else f"{value:.2f}"
        parts.append(f"{name}={text}")
    return ";".join(parts)


def decode_measurements(field):
    """'TEMP=24.80;STATUS=OK' -> {'TEMP': '24.80', 'STATUS': 'OK'}"""
    if not field or "=" not in field:
        return {}
    return dict(pair.split("=", 1) for pair in field.split(";"))


class TelepConnection:
    """Persistent TCP connection to the server. Sends a command and reads the reply.

    List replies (GET_NODES, GET_ALERTS) span several lines and end with END.
    After SUBSCRIBE the server can push ALERT|... lines at any moment; they are
    kept apart in pending_alerts so they never get mixed with a command reply.
    """

    def __init__(self, hostname=DEFAULT_HOST, port=TCP_PORT, timeout=DEFAULT_TIMEOUT_SECONDS):
        self.hostname = hostname
        self.port = port
        self.timeout = timeout
        self.socket = None
        self.buffer = bytearray()
        self.pending_alerts = []

    def connect(self):
        server_ip = resolve_server_address(self.hostname)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)   # socket creation
        self.socket.settimeout(self.timeout)
        self.socket.connect((server_ip, self.port))                        # connection
        self.buffer.clear()
        return server_ip

    def close(self):
        if self.socket:
            self.socket.close()
            self.socket = None
        self.buffer.clear()

    def _extract(self):
        index = self.buffer.find(b"\n")
        if index < 0:
            if len(self.buffer) >= MAX_LINE:
                raise ConnectionError("oversized server frame")
            return None
        if index >= MAX_LINE:
            raise ConnectionError("oversized server frame")
        raw = bytes(self.buffer[:index])
        del self.buffer[:index + 1]
        try:
            return raw.decode("ascii").rstrip("\r")
        except UnicodeDecodeError as error:
            raise ConnectionError("invalid server encoding") from error

    def _receive(self):
        data = self.socket.recv(4096)
        if not data:
            raise ConnectionError("the server closed the connection")
        self.buffer.extend(data)

    def read_line(self):
        deadline = time.monotonic() + self.timeout
        while True:
            line = self._extract()
            if line is not None:
                if not line.startswith(ALERT_PREFIX):
                    return line
                self.pending_alerts.append(decode_message(line))
                continue
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.socket], [], [], remaining)[0]:
                raise TimeoutError("reply deadline exceeded")
            self._receive()

    def request(self, command, *fields):
        """Sends a command and returns the list of decoded reply lines."""
        self.socket.sendall(encode_message(command, *fields))              # send
        response = [decode_message(self.read_line())]
        if command in ("GET_NODES", "GET_ALERTS", "STATS") and response[0][0] == "OK":
            while True:
                line = self.read_line()
                if line == END_OF_LIST:
                    break
                response.append(decode_message(line))
        return response

    def subscribe(self):
        """Asks the server to push new alerts on this connection."""
        return self.request("SUBSCRIBE")[0][0] == "OK"

    def take_alerts(self):
        # Drain complete buffered frames as well as kernel-ready bytes; never wait for a partial line.
        for _ in range(256):
            line = self._extract()
            if line is not None:
                if not line.startswith(ALERT_PREFIX):
                    raise ConnectionError("unexpected unsolicited response")
                self.pending_alerts.append(decode_message(line))
            elif select.select([self.socket], [], [], 0)[0]:
                self._receive()
            else:
                break
        alerts, self.pending_alerts = self.pending_alerts, []
        return alerts

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *exc_info):
        self.close()


def send_one_request(command, *fields, hostname=DEFAULT_HOST, port=TCP_PORT):
    """Shortcut: open a connection, send one command, close. Handy for short scripts."""
    with TelepConnection(hostname, port) as connection:
        return connection.request(command, *fields)


if __name__ == "__main__":
    # Quick self-test against a running server: python3 telep.py [host]
    import sys
    host = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_HOST
    print("DNS:", host, "->", resolve_server_address(host))
    print("GET_STATUS ->", send_one_request("GET_STATUS", hostname=host))
    print("GET_NODES  ->", send_one_request("GET_NODES", hostname=host))
    print("FOO        ->", send_one_request("FOO", hostname=host))
