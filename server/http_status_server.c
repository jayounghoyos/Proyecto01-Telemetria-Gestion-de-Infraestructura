#include "server_threads.h"
#include "node_registry.h"
#include "response_format.h"
#include "socket_helpers.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define HTTP_BODY_SIZE 16384

static void append(char *body, size_t body_size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

static void append(char *body, size_t body_size, const char *format, ...) {
    size_t used = strlen(body);
    if (used >= body_size) return;
    va_list args;
    va_start(args, format);
    vsnprintf(body + used, body_size - used, format, args);
    va_end(args);
}

/* JSON body for /status, /nodes and /alerts. */
static void build_json(const char *path, const RegistrySnapshot *snapshot, char *body, size_t body_size) {
    char text[MAX_LINE];
    time_t now = time(NULL);
    body[0] = '\0';

    if (strcmp(path, "/status") == 0) {
        append(body, body_size, "{\"uptime\":%ld,\"registered\":%d,\"active\":%d,\"udp_rx\":%ld,\"udp_lost\":%ld,\"alerts\":%d}",
               (long)(now - snapshot->started_at), count_registered_nodes(snapshot),
               count_active_nodes(snapshot, now), snapshot->datagrams_total,
               sum_lost_datagrams(snapshot), snapshot->total_alerts);
    } else if (strcmp(path, "/nodes") == 0) {
        append(body, body_size, "[");
        int first = 1;
        for (int i = 0; i < MAX_NODES; i++) {
            const TelemetryNode *node = &snapshot->nodes[i];
            if (!node->in_use) continue;
            append(body, body_size, "%s{\"id\":\"%s\",\"active\":%s,\"last_seen\":%ld,\"measurements\":\"%s\"}",
                   first ? "" : ",", node->node_id, node_is_active(node, now) ? "true" : "false",
                   (long)node->last_seen, format_measurements(node, text, sizeof text));
            first = 0;
        }
        append(body, body_size, "]");
    } else { /* /alerts */
        append(body, body_size, "[");
        int total = snapshot->total_alerts, shown = total < MAX_ALERTS ? total : MAX_ALERTS;
        for (int index = total - shown; index < total; index++) {
            const Alert *alert = &snapshot->alerts[index % MAX_ALERTS];
            append(body, body_size, "%s{\"ts\":%ld,\"node\":\"%s\",\"type\":\"%s\",\"value\":%.2f}",
                   index == total - shown ? "" : ",", (long)alert->timestamp, alert->node_id, alert->alert_type, alert->value);
        }
        append(body, body_size, "]");
    }
}

/* Minimal HTML page with the five views the assignment asks for. */
static void build_html(const RegistrySnapshot *snapshot, char *body, size_t body_size) {
    char text[MAX_LINE];
    time_t now = time(NULL);
    body[0] = '\0';
    append(body, body_size,
           "<!doctype html><html><head><meta charset='utf-8'><meta http-equiv='refresh' content='5'>"
           "<title>Telemetry</title></head><body>"
           "<h1>Telemetry server</h1><p>Status: ONLINE, uptime %ld s, registered nodes: %d, active: %d, "
           "UDP datagrams: %ld (lost %ld)</p>",
           (long)(now - snapshot->started_at), count_registered_nodes(snapshot),
           count_active_nodes(snapshot, now), snapshot->datagrams_total, sum_lost_datagrams(snapshot));

    append(body, body_size, "<h2>Nodes and last measurements</h2><table border='1'><tr><th>Node</th><th>State</th><th>Ago (s)</th><th>Measurements</th></tr>");
    for (int i = 0; i < MAX_NODES; i++) {
        const TelemetryNode *node = &snapshot->nodes[i];
        if (!node->in_use) continue;
        append(body, body_size, "<tr><td>%s</td><td>%s</td><td>%ld</td><td>%s</td></tr>", node->node_id,
               node_is_active(node, now) ? "ACTIVE" : "INACTIVE",
               node->last_seen ? (long)(now - node->last_seen) : -1L,
               node->last_seen ? format_measurements(node, text, sizeof text) : "-");
    }
    append(body, body_size, "</table><h2>Recent alerts (%d)</h2><table border='1'><tr><th>Time</th><th>Node</th><th>Type</th><th>Value</th></tr>",
           snapshot->total_alerts);
    int total = snapshot->total_alerts, shown = total < 20 ? total : 20;
    for (int index = total - 1; index >= total - shown; index--) {
        const Alert *alert = &snapshot->alerts[index % MAX_ALERTS];
        strftime(text, sizeof text, "%H:%M:%S", localtime(&alert->timestamp));
        append(body, body_size, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%.2f</td></tr>", text, alert->node_id, alert->alert_type, alert->value);
    }
    append(body, body_size, "</table><p>JSON: <a href='/status'>/status</a> <a href='/nodes'>/nodes</a> <a href='/alerts'>/alerts</a></p></body></html>");
}

/* Serves one request: reads the request line, replies and closes. */
static void serve_http_request(int client_fd) {
    char request_line[MAX_LINE], path[64] = "/";
    static __thread char body[HTTP_BODY_SIZE];
    char header[256];

    if (receive_text_line(client_fd, request_line, sizeof request_line) <= 0) return;
    sscanf(request_line, "GET %63s", path);
    while (receive_text_line(client_fd, request_line, sizeof request_line) > 1) { }   /* skip headers */

    RegistrySnapshot snapshot;
    registry_copy_snapshot(&snapshot);
    const char *content_type = "application/json";
    int status = 200;

    if (strcmp(path, "/") == 0) { content_type = "text/html; charset=utf-8"; build_html(&snapshot, body, sizeof body); }
    else if (!strcmp(path, "/status") || !strcmp(path, "/nodes") || !strcmp(path, "/alerts")) build_json(path, &snapshot, body, sizeof body);
    else { status = 404; strcpy(body, "{\"error\":\"not found\"}"); }

    snprintf(header, sizeof header,
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
             status, status == 200 ? "OK" : "Not Found", content_type, strlen(body));
    send_full_buffer(client_fd, header, strlen(header));
    send_full_buffer(client_fd, body, strlen(body));
}

void *http_status_server(void *listening_fd) {
    int listener_fd = *(int *)listening_fd;
    printf("[http] listening on port %d\n", HTTP_PORT);
    for (;;) {
        int client_fd = accept(listener_fd, NULL, NULL);
        if (client_fd < 0) { perror("accept(http)"); continue; }
        serve_http_request(client_fd);
        close(client_fd);
    }
    return NULL;
}
