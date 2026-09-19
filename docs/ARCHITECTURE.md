# Architecture

The platform has three parts: telemetry nodes and operator clients written in Python, and a
central server written in C that runs inside Docker on an AWS EC2 instance. Clients find the
server through a DNS name, never through a fixed IP.

![Figure 1. Platform architecture](diagrams/01-architecture.png)

Figure 1. Nodes send telemetry over UDP (port 5001); operators send queries and receive alerts
over TCP (port 5000); a browser reads the status page over HTTP (port 8080).

## Why UDP for telemetry and TCP for everything else

Telemetry is sent every couple of seconds. If one datagram is lost the next one replaces it, so
reliability is not worth the cost of a connection per node. Each datagram carries a sequence
number, which lets the server count how many were lost.

Registration, operator queries and alerts are different: they must arrive complete, in order
and be acknowledged, so they go over TCP. Alerts are pushed by the server on the operator's
open TCP connection, which is what the assignment calls critical information.

## Concurrency

![Figure 2. Server threads](diagrams/02-server-threads.png)

Figure 2. The server uses POSIX threads: one for UDP, one that accepts TCP connections and
starts a thread per client, and one for HTTP. They share the node registry through a single
mutex. Readers copy a snapshot and build their reply outside the lock, so nodes never wait for
a slow operator. A client that disconnects or sends garbage only ends its own thread.

## Alert flow

![Figure 3. Alert flow](diagrams/03-alert-flow.png)

Figure 3. A measurement above a threshold creates an alert once (not on every datagram while
the value stays high). Subscribed operators receive it immediately; anyone can also ask for the
history with GET_ALERTS or read /alerts in the browser.

## Deployment

![Figure 4. Network and deployment](diagrams/04-network-deployment.png)

Figure 4. In AWS Academy the public IP changes every time the instance starts. A small systemd
service updates the DuckDNS record on boot, Docker restarts the container, and the clients keep
using the same name. Ports 22, 5000/tcp, 5001/udp and 8080 are opened in the security group.
