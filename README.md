# Distributed Telemetry Platform

Course project for Telematics (Internet: Architecture and Protocols). The goal is a platform
where simulated IoT nodes report measurements to a central server and operators can query the
state of the nodes and get alerts when a value goes out of range.

Planned components:

- central server in C, to be deployed in Docker on AWS EC2
- telemetry nodes and operator client in Python
- a small web page to check the server state

The assignment is in docs/Proyecto Telematica 2026-2.pdf. Design so far:

- docs/ARCHITECTURE.md - architecture and diagrams
- docs/PROTOCOL.md - TELEP/1.0, the text protocol between nodes, operators and server

Ports decided for the protocol: 5001/udp for telemetry, 5000/tcp for commands, 8080/tcp for
the web page.
