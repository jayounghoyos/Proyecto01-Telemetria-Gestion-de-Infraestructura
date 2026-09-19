#include "server_threads.h"
#include "node_registry.h"
#include "socket_helpers.h"
#include "telep_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Receives TELEMETRY|<node_id>|<seq>|TEMP=..;HUM=.. over UDP. No reply (best effort).
 * A malformed datagram is logged and dropped; the server never exits. */
void *udp_telemetry_server(void *listening_fd) {
    int udp_fd = *(int *)listening_fd;
    char datagram[MAX_LINE], peer_text[32];
    struct sockaddr_in sender_address;
    socklen_t sender_length;

    printf("[udp] listening for telemetry on port %d\n", UDP_PORT);
    for (;;) {
        sender_length = sizeof sender_address;
        ssize_t received = recvfrom(udp_fd, datagram, sizeof datagram - 1, 0,
                                    (struct sockaddr *)&sender_address, &sender_length);
        if (received < 0) { perror("recvfrom"); continue; }
        datagram[received] = '\0';
        datagram[strcspn(datagram, "\r\n")] = '\0';

        ParsedMessage message;
        Measurement values[MAX_VARS];
        int value_count = 0;
        int well_formed = parse_message(datagram, &message) == 0
                       && strcmp(message.command, "TELEMETRY") == 0
                       && message.arg_count == 3
                       && is_valid_node_id(message.args[0])
                       && (value_count = parse_measurements(message.args[2], values, MAX_VARS)) > 0;
        if (!well_formed) {
            printf("[udp] dropped from %s: \"%s\"\n", describe_peer(&sender_address, peer_text, sizeof peer_text), datagram);
            continue;
        }
        long sequence = strtol(message.args[1], NULL, 10);
        if (registry_record_telemetry(message.args[0], sequence, values, value_count) < 0)
            printf("[udp] no room to register node %s\n", message.args[0]);
    }
    return NULL;
}
