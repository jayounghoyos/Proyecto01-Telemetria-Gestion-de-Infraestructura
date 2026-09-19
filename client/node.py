"""Simulated telemetry node.

1. Registers with the server over TCP (HELLO|<id>) so it is identified.
2. Sends periodic measurements over UDP (TELEMETRY|<id>|<seq>|TEMP=..;HUM=..).
3. Handles SIGINT/SIGTERM so it can be stopped cleanly.
"""
import argparse
import random
import signal
import socket
import sys
import time

import telep

RECONNECT_DELAY_SECONDS = 3

# Variable -> (mean, standard deviation) of the simulated normal distribution
SIMULATED_VARIABLES = {
    "TEMP":  (24.0, 1.5),    # degrees C
    "HUM":   (60.0, 4.0),    # % relative humidity
    "POWER": (120.0, 15.0),  # W consumption
    "VIB":   (0.5, 0.2),     # mm/s vibration
}


def parse_arguments():
    parser = argparse.ArgumentParser(description="Simulated IoT node (TELEP/1.0)")
    parser.add_argument("--id", required=True, help="unique identifier, e.g. NODE01")
    parser.add_argument("--host", default=telep.DEFAULT_HOST, help="server DNS name")
    parser.add_argument("--interval", type=float, default=2.0, help="seconds between measurements")
    parser.add_argument("--spike", action="append", default=[], metavar="VAR=VALUE",
                        help="force a fixed value, e.g. --spike TEMP=45 (raises an alert)")
    parser.add_argument("--fail-status", action="store_true", help="report STATUS=FAIL")
    return parser.parse_args()


def register_with_retries(node_id, hostname):
    """Retries HELLO until the server answers; returns the resolved IP."""
    while True:
        try:
            with telep.TelepConnection(hostname) as connection:
                server_ip = connection.socket.getpeername()[0]
                reply = connection.request("HELLO", node_id)
                if reply[0][0] != "OK":
                    print(f"[{node_id}] registration rejected: {reply[0]}", file=sys.stderr)
                    sys.exit(1)
                print(f"[{node_id}] registered at {hostname} ({server_ip}) -> {reply[0]}")
                return server_ip
        except (OSError, ConnectionError) as error:
            print(f"[{node_id}] could not register ({error}); retrying in {RECONNECT_DELAY_SECONDS}s")
            time.sleep(RECONNECT_DELAY_SECONDS)


def take_measurements(forced_values, fail_status):
    measurements = {name: random.gauss(mean, deviation) for name, (mean, deviation) in SIMULATED_VARIABLES.items()}
    measurements.update(forced_values)
    measurements["STATUS"] = "FAIL" if fail_status else "OK"
    return measurements


def stop_on_sigterm(*_):
    """docker stop / kill send SIGTERM: treat it like Ctrl-C."""
    raise KeyboardInterrupt


def main():
    sys.stdout.reconfigure(line_buffering=True)   # logs visible even when piped to a file
    signal.signal(signal.SIGTERM, stop_on_sigterm)
    args = parse_arguments()
    forced_values = {}
    for spike in args.spike:
        name, value = spike.split("=", 1)
        forced_values[name.upper()] = float(value)

    server_ip = register_with_retries(args.id, args.host)
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)     # UDP socket creation
    sequence = 0
    try:
        while True:
            measurements = take_measurements(forced_values, args.fail_status)
            datagram = telep.encode_message("TELEMETRY", args.id, sequence, telep.encode_measurements(measurements))
            try:
                udp_socket.sendto(datagram, (server_ip, telep.UDP_PORT))  # UDP send
                print(f"[{args.id}] seq={sequence} {datagram.decode().strip()}")
            except OSError as error:
                print(f"[{args.id}] UDP send error: {error}", file=sys.stderr)
            sequence += 1
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print(f"\n[{args.id}] stopping; datagrams transmitted: {sequence}")
    finally:
        udp_socket.close()                                             # close


if __name__ == "__main__":
    main()
