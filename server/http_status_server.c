#include "server_threads.h"
#include "node_registry.h"
#include "response_format.h"
#include "socket_helpers.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include "service_health.h"

#define HTTP_BODY_SIZE 65536

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
        char status[MAX_LINE];
        format_system_status(snapshot, status, sizeof status);
        append(body, body_size, "{");
        char *save, *field = strtok_r(status, ";", &save);
        int first = 1;
        while (field) {
            char *eq = strchr(field, '='); *eq = 0;
            append(body, body_size, "%s\"%s\":%s", first ? "" : ",", field, eq+1);
            first=0; field=strtok_r(NULL,";",&save);
        }
        append(body,body_size,"}");
    } else if (strcmp(path, "/nodes") == 0) {
        append(body, body_size, "[");
        int first = 1;
        for (int i = 0; i < MAX_NODES; i++) {
            const TelemetryNode *node = &snapshot->nodes[i];
            if (!node->in_use) continue;
            append(body, body_size, "%s{\"id\":\"%s\",\"active\":%s,\"last_seen\":%ld,\"measurements\":\"%s\"}",
                   first ? "" : ",", node->node_id, node_is_active(node, now) ? "true" : "false",
                   (long)node->last_seen, format_measurements(node, text, sizeof text));
            /* Append the same session statistics exposed by STATS. */
            size_t used = strlen(body); if (used && body[used-1] == '}') body[used-1] = 0;
            char stats[MAX_LINE]; format_node_stats(node, stats, sizeof stats);
            append(body,body_size,",\"session\":\"%s\",\"statistics\":{",node->session);
            char *save, *field=strtok_r(stats,";",&save); int first_stat=1;
            while (field) {
                char *eq=strchr(field,'='); *eq=0;
                append(body,body_size,"%s\"%s\":%s",first_stat ? "" : ",",field,eq+1);
                first_stat=0; field=strtok_r(NULL,";",&save);
            }
            append(body,body_size,"}}");
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
           "<h1>Telemetry server</h1><p>TCP loop: %s, UDP loop: %s, uptime %ld s, registered nodes: %d, active: %d, "
           "Parsed UDP datagrams: %ld (estimated missing after sender reports: %ld)</p>",
           service_healthy(0) ? "responsive" : "stalled", service_healthy(1) ? "responsive" : "stalled",
           (long)(now - snapshot->started_at), count_registered_nodes(snapshot),
           count_active_nodes(snapshot, now), snapshot->datagrams_total, sum_lost_datagrams(snapshot));

    char status[MAX_LINE];
    format_system_status(snapshot,status,sizeof status);
    append(body,body_size,"<h2>Measured status and counters</h2><pre>%s</pre>",status);
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
        struct tm local;
        localtime_r(&alert->timestamp, &local);
        strftime(text, sizeof text, "%H:%M:%S", &local);
        append(body, body_size, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%.2f</td></tr>", text, alert->node_id, alert->alert_type, alert->value);
    }
    append(body, body_size, "</table><p>JSON: <a href='/status'>/status</a> <a href='/nodes'>/nodes</a> <a href='/alerts'>/alerts</a></p></body></html>");
}

/* At most 16 workers. All headers share a 2-second absolute deadline. */
static pthread_mutex_t workers_lock = PTHREAD_MUTEX_INITIALIZER;
static int workers;
static void http_reply(int fd, int status, const char *type, const char *body) {
    char header[256];
    snprintf(header, sizeof header,
        "HTTP/1.1 %d Response\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, type, strlen(body));
    if (send_full_buffer(fd, header, strlen(header)) == 0)
        send_full_buffer(fd, body, strlen(body));
}
static void serve_http_request(int fd) {
    char line[1024], method[16], path[64], version[16], extra;
    long deadline = monotonic_ms() + 2000;
    int n = receive_line_until(fd, line, sizeof line, deadline);
    if (n <= 0) { http_reply(fd, n == -2 ? 431 : 408, "text/plain", "Incomplete request"); return; }
    if (sscanf(line, "%15s %63s %15s %c", method, path, version, &extra) != 3 ||
        (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) {
        http_reply(fd, 400, "text/plain", "Bad request"); return;
    }
    int bytes = n, lines = 0;
    for (;;) {
        n = receive_line_until(fd, line, sizeof line, deadline);
        if (n == 1) break;
        if (n <= 0) { http_reply(fd, n == -2 ? 431 : 408, "text/plain", "Incomplete headers"); return; }
        bytes += n;
        if (++lines > 64 || bytes > 8192) { http_reply(fd, 431, "text/plain", "Headers too large"); return; }
        if (!strchr(line, ':')) { http_reply(fd, 400, "text/plain", "Bad header"); return; }
    }
    if (strcmp(method, "GET")) { http_reply(fd, 405, "text/plain", "GET required"); return; }
    RegistrySnapshot snapshot;
    registry_copy_snapshot(&snapshot);
    char *body = malloc(HTTP_BODY_SIZE);
    if (!body) { http_reply(fd, 503, "text/plain", "Capacity"); return; }
    int status = 200;
    const char *type = "application/json";
    if (!strcmp(path, "/")) { type = "text/html; charset=utf-8"; build_html(&snapshot, body, HTTP_BODY_SIZE); }
    else if (!strcmp(path, "/status") || !strcmp(path, "/nodes") || !strcmp(path, "/alerts")) {
        build_json(path, &snapshot, body, HTTP_BODY_SIZE);
        if (!strcmp(path, "/status") && (!service_healthy(0) || !service_healthy(1))) status = 503;
    } else { status = 404; strcpy(body, "{\"error\":\"not found\"}"); }
    http_reply(fd, status, type, body);
    free(body);
}
static void *http_worker(void *arg) {
    int fd = *(int *)arg; free(arg);
    serve_http_request(fd); close(fd);
    pthread_mutex_lock(&workers_lock); workers--; pthread_mutex_unlock(&workers_lock);
    return NULL;
}
void *http_status_server(void *listening_fd) {
    int listener = *(int *)listening_fd;
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) continue;
        pthread_mutex_lock(&workers_lock);
        if (workers >= 16) { pthread_mutex_unlock(&workers_lock); close(fd); continue; }
        workers++; pthread_mutex_unlock(&workers_lock);
        int *arg = malloc(sizeof *arg);
        pthread_t thread;
        if (arg) *arg = fd;
        if (!arg || pthread_create(&thread, NULL, http_worker, arg)) {
            free(arg); close(fd);
            pthread_mutex_lock(&workers_lock); workers--; pthread_mutex_unlock(&workers_lock);
        } else pthread_detach(thread);
    }
    return NULL;
}
