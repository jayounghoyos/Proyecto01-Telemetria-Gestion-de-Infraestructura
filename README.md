# Distributed Telemetry Platform

Course project for Telematics (Internet: Architecture and Protocols). The goal is a platform
where simulated IoT nodes report measurements to a central server and operators can query the
state of the nodes and get alerts when a value goes out of range.

Components:

- central server in C (server/), to be deployed in Docker on AWS EC2
- telemetry nodes and operator clients in Python, standard library only (client/)
- a web page to check the server state, served by the server on port 8080

## Running the server

    make -C server && ./server/telemetry_server

It listens on 5000/tcp (commands), 5001/udp (telemetry) and 8080/tcp (web). Without the
Python clients you can try it with netcat:

    printf 'HELLO|NODE01\nBYE\n' | nc -q1 localhost 5000
    echo 'TELEMETRY|NODE01|1|TEMP=45.2;HUM=61;STATUS=OK' | nc -u -w0 localhost 5001
    printf 'GET_STATUS\nGET_ALERTS\nBYE\n' | nc -q1 localhost 5000
    curl localhost:8080/status

## Running the server in Docker

    docker compose up -d --build
    docker compose logs -f

The image is built in two stages (gcc to compile, debian-slim to run) and publishes
5000/tcp, 5001/udp and 8080/tcp. The container restarts on its own if the machine reboots.

## Running the clients

Python 3.8 or newer. The server name comes from --host or the TELEP_SERVER_HOST variable
(default localhost); the clients resolve it with DNS, there is no IP in the code.

    ./client/run_nodes.sh 5                          # five simulated nodes in the background
    python3 client/node.py --id NODE09 --spike TEMP=45   # a node that raises an alert
    python3 client/operator_cli.py                   # operator, console menu; receives alerts as they happen
    python3 client/operator_gui.py                   # operator, tkinter window
    ./client/stop_nodes.sh

Node options: --interval seconds between measurements, --spike VAR=VALUE to force a value,
--fail-status to report STATUS=FAIL. Thresholds are in server/config.h (TEMP > 40, HUM > 85,
POWER > 500, VIB > 7).

The assignment is in docs/Proyecto Telematica 2026-2.pdf. Design so far:

- docs/ARCHITECTURE.md - architecture and diagrams
- docs/PROTOCOL.md - TELEP/1.0, the text protocol between nodes, operators and server

Ports decided for the protocol: 5001/udp for telemetry, 5000/tcp for commands, 8080/tcp for
the web page.
