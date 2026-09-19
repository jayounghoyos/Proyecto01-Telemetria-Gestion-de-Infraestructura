"""Operator client: interactive console that queries the server over TCP.

Uses one persistent TCP connection (TelepConnection) for the whole session.
"""
import argparse
import sys
import time

import telep

MENU = """
=== TELEP/1.0 operator ===
 1) Registered / active nodes       (GET_NODES)
 2) Last measurements of all nodes  (GET_NODES + GET_LAST)
 3) Query one node                  (GET_LAST|<id>)
 4) Alerts                          (GET_ALERTS)
 5) System status                   (GET_STATUS)
 6) Send a raw command
 0) Quit                            (BYE)
"""


def format_timestamp(epoch_text):
    return time.strftime("%H:%M:%S", time.localtime(int(epoch_text)))


def print_reply_error(reply):
    print(f"  ! error {reply[1]}: {reply[2]}")


def show_nodes(connection):
    reply = connection.request("GET_NODES")
    if reply[0][0] != "OK":
        return print_reply_error(reply[0])
    print(f"  {reply[0][1]} registered nodes")
    for node_id, state, seconds_ago in reply[1:]:
        print(f"  {node_id:<12} {state:<9} last data {seconds_ago} s ago")


def show_last_measurement(connection, node_id):
    reply = connection.request("GET_LAST", node_id)[0]
    if reply[0] != "OK":
        return print_reply_error(reply)
    _, node_id, timestamp, field = reply
    if field == "NO_DATA":
        print(f"  {node_id:<12} no measurements yet")
        return
    measurements = " ".join(f"{name}={value}" for name, value in telep.decode_measurements(field).items())
    print(f"  {node_id:<12} {format_timestamp(timestamp)}  {measurements}")


def show_all_measurements(connection):
    reply = connection.request("GET_NODES")
    if reply[0][0] != "OK":
        return print_reply_error(reply[0])
    for node_id, _state, _age in reply[1:]:
        show_last_measurement(connection, node_id)


def show_alerts(connection):
    reply = connection.request("GET_ALERTS")
    if reply[0][0] != "OK":
        return print_reply_error(reply[0])
    print(f"  {reply[0][1]} alerts")
    for timestamp, node_id, alert_type, value in reply[1:]:
        print(f"  {format_timestamp(timestamp)}  {node_id:<12} {alert_type:<12} {value}")


def show_status(connection):
    reply = connection.request("GET_STATUS")[0]
    if reply[0] != "OK":
        return print_reply_error(reply)
    for name, value in telep.decode_measurements(reply[1]).items():
        print(f"  {name:<12} {value}")


def send_raw_command(connection):
    raw = input("  command (e.g. GET_LAST|NODE01): ").strip()
    if not raw:
        return
    command, *fields = raw.split(telep.SEPARATOR)
    for line in connection.request(command, *fields):
        print("  <", telep.SEPARATOR.join(line))


def main():
    parser = argparse.ArgumentParser(description="Operator client (TELEP/1.0)")
    parser.add_argument("--host", default=telep.DEFAULT_HOST, help="server DNS name")
    parser.add_argument("--port", type=int, default=telep.TCP_PORT)
    args = parser.parse_args()

    try:
        connection = telep.TelepConnection(args.host, args.port)
        server_ip = connection.connect()
    except OSError as error:
        print(f"could not connect to {args.host}:{args.port}: {error}", file=sys.stderr)
        sys.exit(1)
    print(f"connected to {args.host} ({server_ip}:{args.port})")
    if connection.subscribe():
        print("subscribed: new alerts arrive on this connection (ALERT|...)")

    actions = {
        "1": show_nodes,
        "2": show_all_measurements,
        "3": lambda c: show_last_measurement(c, input("  node id: ").strip()),
        "4": show_alerts,
        "5": show_status,
        "6": send_raw_command,
    }
    try:
        while True:
            for _, node_id, alert_type, value in connection.take_alerts():
                print(f"  !! ALERT received: {node_id} {alert_type} {value}")
            print(MENU)
            choice = input("option: ").strip()
            if choice == "0":
                break
            action = actions.get(choice)
            if action is None:
                print("  invalid option")
                continue
            try:
                action(connection)
            except (OSError, ConnectionError) as error:
                print(f"  ! connection lost: {error}", file=sys.stderr)
                break
    except (KeyboardInterrupt, EOFError):
        print()
    finally:
        connection.close()
        print("disconnected")


if __name__ == "__main__":
    main()
