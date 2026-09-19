#include "server_threads.h"
#include "alert_subscribers.h"
#include "node_registry.h"
#include "response_format.h"
#include "socket_helpers.h"
#include "telep_protocol.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int  socket_fd;
    char peer_text[32];
} ClientConnection;

static void reply_error(int socket_fd, int error_code) {
    char response[64];
    build_error_response(response, sizeof response, error_code);
    send_text_line(socket_fd, response);
}

static void reply_status(ClientConnection *client) {
    RegistrySnapshot snapshot;
    char status_text[MAX_LINE], response[MAX_LINE * 2];
    registry_copy_snapshot(&snapshot);
    snprintf(response, sizeof response, "OK|%s", format_system_status(&snapshot, status_text, sizeof status_text));
    send_text_line(client->socket_fd, response);
}

/* OK|<n> followed by one line <id>|ACTIVE/INACTIVE|<seconds_since_last_data> per node and END */
static void reply_node_list(ClientConnection *client) {
    RegistrySnapshot snapshot;
    char response[MAX_LINE];
    registry_copy_snapshot(&snapshot);
    time_t now = time(NULL);

    snprintf(response, sizeof response, "OK|%d", count_registered_nodes(&snapshot));
    send_text_line(client->socket_fd, response);
    for (int i = 0; i < MAX_NODES; i++) {
        const TelemetryNode *node = &snapshot.nodes[i];
        if (!node->in_use) continue;
        long seconds_ago = node->last_seen ? (long)(now - node->last_seen) : -1L;
        snprintf(response, sizeof response, "%s|%s|%ld", node->node_id,
                 node_is_active(node, now) ? "ACTIVE" : "INACTIVE", seconds_ago);
        send_text_line(client->socket_fd, response);
    }
    send_text_line(client->socket_fd, "END");
}

static void reply_last_measurement(ClientConnection *client, const char *node_id) {
    RegistrySnapshot snapshot;
    char measurements_text[MAX_LINE], response[MAX_LINE * 2];
    registry_copy_snapshot(&snapshot);
    const TelemetryNode *node = snapshot_find_node(&snapshot, node_id);
    if (!node) { reply_error(client->socket_fd, ERR_UNKNOWN_NODE); return; }

    if (!node->last_seen)
        snprintf(response, sizeof response, "OK|%s|0|NO_DATA", node->node_id);
    else
        snprintf(response, sizeof response, "OK|%s|%ld|%s", node->node_id, (long)node->last_seen,
                 format_measurements(node, measurements_text, sizeof measurements_text));
    send_text_line(client->socket_fd, response);
}

/* OK|<n> followed by <ts>|<id>|<type>|<value> per alert (chronological) and END */
static void reply_alert_list(ClientConnection *client) {
    RegistrySnapshot snapshot;
    char response[MAX_LINE];
    registry_copy_snapshot(&snapshot);

    int total = snapshot.total_alerts;
    int shown = total < MAX_ALERTS ? total : MAX_ALERTS;
    snprintf(response, sizeof response, "OK|%d", shown);
    send_text_line(client->socket_fd, response);
    for (int index = total - shown; index < total; index++) {
        const Alert *alert = &snapshot.alerts[index % MAX_ALERTS];
        snprintf(response, sizeof response, "%ld|%s|%s|%.2f",
                 (long)alert->timestamp, alert->node_id, alert->alert_type, alert->value);
        send_text_line(client->socket_fd, response);
    }
    send_text_line(client->socket_fd, "END");
}

/* Dispatches one command. Returns 1 if the connection must close (BYE), 0 otherwise. */
static int handle_command(ClientConnection *client, const char *line) {
    ParsedMessage message;
    if (parse_message(line, &message) < 0) { reply_error(client->socket_fd, ERR_BAD_FORMAT); return 0; }
    const char *command = message.command;
    const char *first_arg = message.arg_count >= 1 ? message.args[0] : NULL;

    if (strcmp(command, "BYE") == 0) { send_text_line(client->socket_fd, "OK|BYE"); return 1; }
    if (strcmp(command, "GET_STATUS") == 0) { reply_status(client); return 0; }
    if (strcmp(command, "GET_NODES") == 0)  { reply_node_list(client); return 0; }
    if (strcmp(command, "GET_ALERTS") == 0) { reply_alert_list(client); return 0; }
    if (strcmp(command, "SUBSCRIBE") == 0) {
        if (subscribers_add(client->socket_fd) < 0) reply_error(client->socket_fd, ERR_SERVER_FULL);
        else send_text_line(client->socket_fd, "OK|SUBSCRIBED");
        return 0;
    }

    /* The remaining commands need a valid node_id as first argument. */
    if (!first_arg || !is_valid_node_id(first_arg)) {
        int known = !strcmp(command, "HELLO") || !strcmp(command, "GET_LAST");
        reply_error(client->socket_fd, known ? ERR_BAD_FORMAT : ERR_UNKNOWN_CMD);
        return 0;
    }
    if (strcmp(command, "HELLO") == 0) {
        if (registry_register_node(first_arg) < 0) reply_error(client->socket_fd, ERR_SERVER_FULL);
        else { printf("[tcp] %s registers node %s\n", client->peer_text, first_arg); send_text_line(client->socket_fd, "OK|REGISTERED"); }
        return 0;
    }
    if (strcmp(command, "GET_LAST") == 0) { reply_last_measurement(client, first_arg); return 0; }

    reply_error(client->socket_fd, ERR_UNKNOWN_CMD);
    return 0;
}

/* One thread per client: reads lines until BYE, disconnection or error. An invalid
 * line only produces an ERR; it never closes the server. */
static void *serve_client(void *connection) {
    ClientConnection *client = connection;
    char line[MAX_LINE];
    printf("[tcp] connected %s\n", client->peer_text);
    for (;;) {
        int result = receive_text_line(client->socket_fd, line, sizeof line);
        if (result == 0 || result == -1) break;
        if (result == -2) { reply_error(client->socket_fd, ERR_TOO_LONG); continue; }
        if (handle_command(client, line)) break;
    }
    printf("[tcp] disconnected %s\n", client->peer_text);
    subscribers_remove(client->socket_fd);
    close(client->socket_fd);
    free(client);
    return NULL;
}

void *tcp_command_server(void *listening_fd) {
    int listener_fd = *(int *)listening_fd;
    printf("[tcp] listening for commands on port %d\n", TCP_PORT);
    for (;;) {
        struct sockaddr_in peer_address;
        socklen_t peer_length = sizeof peer_address;
        int client_fd = accept(listener_fd, (struct sockaddr *)&peer_address, &peer_length);
        if (client_fd < 0) { perror("accept"); continue; }

        ClientConnection *client = malloc(sizeof *client);
        client->socket_fd = client_fd;
        describe_peer(&peer_address, client->peer_text, sizeof client->peer_text);

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, serve_client, client) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(client);
            continue;
        }
        pthread_detach(client_thread);
    }
    return NULL;
}
