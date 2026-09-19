"""Shared TELEP/1.0 protocol module for nodes and operators.

ASCII text messages: fields separated by '|' and terminated by '\\n'.
Measurements travel as NAME=value pairs separated by ';'.
"""
import os
import select
import socket

DEFAULT_HOST = os.environ.get("TELEP_SERVER_HOST", "localhost")
TCP_PORT = 5000
UDP_PORT = 5001
SEPARATOR = "|"
MAX_LINE = 256
END_OF_LIST = "END"
ALERT_PREFIX = "ALERT" + SEPARATOR
DEFAULT_TIMEOUT_SECONDS = 5.0


def resolve_server_address(hostname):
    """Resolves the server's DNS name to an IPv4 address. Raises socket.gaierror if it does not exist."""
    address_info = socket.getaddrinfo(hostname, None, socket.AF_INET, socket.SOCK_STREAM)
    return address_info[0][4][0]


def encode_message(command, *fields):
    """Builds 'CMD|field1|field2\\n' ready to send."""
    line = SEPARATOR.join([command, *map(str, fields)])
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
        self.reader = None
        self.pending_alerts = []

    def connect(self):
        server_ip = resolve_server_address(self.hostname)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)   # socket creation
        self.socket.settimeout(self.timeout)
        self.socket.connect((server_ip, self.port))                        # connection
        self.reader = self.socket.makefile("r", encoding="ascii", newline="\n")
        return server_ip

    def close(self):
        if self.socket:
            try:
                self.socket.sendall(encode_message("BYE"))
                self.reader.readline()
            except OSError:
                pass
            self.socket.close()                                            # close
            self.socket = None

    def read_line(self):
        """Reads one line from the server; pushed alerts are stored apart and skipped."""
        while True:
            line = self.reader.readline()                                  # receive
            if not line:
                raise ConnectionError("the server closed the connection")
            if not line.startswith(ALERT_PREFIX):
                return line.rstrip("\r\n")
            self.pending_alerts.append(decode_message(line))

    def request(self, command, *fields):
        """Sends a command and returns the list of decoded reply lines."""
        self.socket.sendall(encode_message(command, *fields))              # send
        response = [decode_message(self.read_line())]
        if command in ("GET_NODES", "GET_ALERTS") and response[0][0] == "OK":
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
        """Returns and clears the pushed alerts, reading first whatever is waiting on the socket."""
        while select.select([self.socket], [], [], 0)[0]:
            line = self.reader.readline()
            if not line:
                break
            if line.startswith(ALERT_PREFIX):
                self.pending_alerts.append(decode_message(line))
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
