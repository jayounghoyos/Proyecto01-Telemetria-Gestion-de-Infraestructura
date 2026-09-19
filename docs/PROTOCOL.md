# TELEP/1.0 protocol

TELEP (Telemetry Exchange Protocol) is the application protocol we designed for this project.
It is text based: every message is one line of ASCII, fields are separated by a vertical bar
and the line ends with a newline character. A message never exceeds 256 bytes.

    COMMAND|argument1|argument2|...

Measurements travel in a single field as name=value pairs separated by semicolons, for
example TEMP=24.80;HUM=60.10;POWER=120.50;VIB=0.30;STATUS=OK. STATUS is OK or FAIL, the rest
are numbers with a decimal point. A node id is 1 to 15 characters: letters, digits, _ or -.

## Transport

Telemetry goes over UDP on port 5001. It is sent every few seconds and losing one datagram is
acceptable because the next one replaces it. Every other message goes over TCP on port 5000
because it has to arrive complete, in order and be answered.

## Telemetry (UDP, node to server, no reply)

    TELEMETRY|NODE03|17|TEMP=24.80;HUM=60.10;POWER=118.20;VIB=0.41;STATUS=OK

The third field is a sequence number that the node increments on every datagram. The server
uses it to detect gaps and count lost datagrams. A malformed datagram is dropped and logged.

## Commands (TCP, request and reply)

The TCP connection stays open; a client sends several commands and ends with BYE. Every reply
starts with OK or ERR. Replies that contain a list send OK|n, then n lines, then a line END.

HELLO|node_id
: registers a node. Reply: OK|REGISTERED

GET_STATUS
: general state. Reply: OK|uptime=95;registered=5;active=5;udp_rx=151;udp_lost=2;alerts=2

GET_NODES
: registered nodes. Reply: OK|n followed by one line per node, node_id|ACTIVE|seconds_since_last_data
(or INACTIVE; -1 if the node never sent data), then END. A node is active if it sent data in
the last 15 seconds.

GET_LAST|node_id
: last measurement of a node. Reply: OK|node_id|epoch|measurements, or OK|node_id|0|NO_DATA

GET_ALERTS
: alert history (up to the last 128, oldest first). Reply: OK|n, then epoch|node_id|type|value
per alert, then END. Alert types are TEMP_HIGH, HUM_HIGH, POWER_HIGH, VIB_HIGH (value above
the threshold configured in the server) and STATUS_FAIL.

BYE
: orderly close. Reply: OK|BYE, then the server closes the socket.

## Error codes

    ERR|100|BAD_FORMAT     empty line, missing argument or invalid node id
    ERR|101|UNKNOWN_CMD    command not recognised
    ERR|102|UNKNOWN_NODE   GET_LAST for a node that is not registered
    ERR|103|TOO_LONG       line longer than 256 bytes (the whole line is discarded)

An error never closes the connection; the client can keep sending commands.

## Example session

    > HELLO|NODE01
    < OK|REGISTERED
      (UDP) TELEMETRY|NODE01|0|TEMP=23.90;HUM=61.20;POWER=115.00;VIB=0.35;STATUS=OK
      (UDP) TELEMETRY|NODE01|1|TEMP=45.10;HUM=60.80;POWER=117.40;VIB=0.33;STATUS=OK
    > GET_NODES
    < OK|1
    < NODE01|ACTIVE|1
    < END
    > GET_ALERTS
    < OK|1
    < 1789690366|NODE01|TEMP_HIGH|45.10
    < END
    > GET_LAST|NODE99
    < ERR|102|UNKNOWN_NODE
    > BYE
    < OK|BYE
