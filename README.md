# Distributed Telemetry Platform

Course project for Telematics (Internet: Architecture and Protocols). The goal is a platform
where simulated IoT nodes report measurements to a central server and operators can query the
state of the nodes and get alerts when a value goes out of range.

Components:

- central server in C (server/), to be deployed in Docker on AWS EC2
- telemetry nodes and operator client in Python (coming next)
- a web page to check the server state, served by the server on port 8080

## Running the server

    make -C server && ./server/telemetry_server

It listens on 5000/tcp (commands), 5001/udp (telemetry) and 8080/tcp (web). Without the
Python clients you can try it with netcat:

    printf 'HELLO|NODE01\nBYE\n' | nc -q1 localhost 5000
    echo 'TELEMETRY|NODE01|1|TEMP=45.2;HUM=61;STATUS=OK' | nc -u -w0 localhost 5001
    printf 'GET_STATUS\nGET_ALERTS\nBYE\n' | nc -q1 localhost 5000
    curl localhost:8080/status

The assignment is in docs/Proyecto Telematica 2026-2.pdf. Design so far:

- docs/ARCHITECTURE.md - architecture and diagrams
- docs/PROTOCOL.md - TELEP/1.0, the text protocol between nodes, operators and server

Ports decided for the protocol: 5001/udp for telemetry, 5000/tcp for commands, 8080/tcp for
the web page.
