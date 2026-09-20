#include "server_threads.h"
#include "alert_subscribers.h"
#include "node_registry.h"
#include "socket_helpers.h"
#include "telep_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "service_health.h"

/* Receives TELEMETRY|<node_id>|<session>|<seq>|TEMP=..;HUM=.. over UDP. No reply (best effort).
 * A malformed datagram is logged and dropped; the server never exits. */
void *udp_telemetry_server(void *listening_fd) {
    int udp_fd = *(int *)listening_fd;
    char datagram[MAX_LINE + 1], peer_text[32];
    struct sockaddr_in sender_address;
    socklen_t sender_length;

    printf("[udp] listening for telemetry on port %d\n", UDP_PORT);
    for (;;) {
        service_heartbeat(1);
        if (wait_readable(udp_fd, 250) <= 0) continue;
        sender_length = sizeof sender_address;
        ssize_t received = recvfrom(udp_fd, datagram, MAX_LINE + 1, MSG_TRUNC,
                                    (struct sockaddr *)&sender_address, &sender_length);
        if (received < 0) { perror("recvfrom"); continue; }
        if (received <= 0 || received > MAX_LINE || datagram[received - 1] != '\n' ||
            memchr(datagram, 0, (size_t)received) || memchr(datagram, '\n', (size_t)received - 1)) {
            registry_invalid_datagram(); continue;
        }
        datagram[received] = '\0';
        datagram[received - 1] = '\0';

        ParsedMessage message;
        Measurement values[MAX_VARS];
        int value_count = 0;
        long sequence;
        int well_formed = parse_message(datagram, &message) == 0
                       && strcmp(message.command, "TELEMETRY") == 0
                       && message.arg_count == 4
                       && is_valid_node_id(message.args[0])
                       && is_valid_session(message.args[1])
                       && parse_uint(message.args[2], SESSION_LIMIT - 1, &sequence) == 0
                       && (value_count = parse_measurements(message.args[3], values, MAX_VARS)) > 0;
        if (!well_formed) {
            registry_invalid_datagram();
            printf("[udp] dropped from %s: \"%s\"\n", describe_peer(&sender_address, peer_text, sizeof peer_text), datagram);
            continue;
        }
        Alert new_alerts[MAX_VARS];
        int new_alert_count = registry_record_telemetry(message.args[0], message.args[1], sequence, values, value_count, new_alerts);
        if (new_alert_count < 0) printf("[udp] rejected session/sequence for %s\n", message.args[0]);
        for (int i = 0; i < new_alert_count; i++) subscribers_broadcast(&new_alerts[i]);
    }
    return NULL;
}
