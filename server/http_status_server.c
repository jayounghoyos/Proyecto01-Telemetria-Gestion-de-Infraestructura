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
#include "config.h"
#include "status_page_css.h"

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

/* Uptime as "42 s", "3 min 05 s" or "2 h 07 min". */
static void format_uptime(long seconds, char *out, size_t size) {
    if (seconds < 60) snprintf(out, size, "%ld s", seconds);
    else if (seconds < 3600) snprintf(out, size, "%ldm %02lds", seconds / 60, seconds % 60);
    else snprintf(out, size, "%ldh %02ldm", seconds / 3600, (seconds % 3600) / 60);
}

/* Same thresholds the server uses to raise alerts (config.h), so a value that
 * is highlighted on the page is one that has produced, or will produce, an alert. */
static int measurement_is_hot(const char *name, const char *value) {
    if (!strcmp(name, "STATUS")) return strcmp(value, "OK") != 0;
    char *end;
    double number = strtod(value, &end);
    if (end == value) return 0;
    if (!strcmp(name, "TEMP"))  return number > TEMP_MAX;
    if (!strcmp(name, "HUM"))   return number > HUM_MAX;
    if (!strcmp(name, "POWER")) return number > POWER_MAX;
    if (!strcmp(name, "VIB"))   return number > VIB_MAX;
    return 0;
}

/* "TEMP=24.1;HUM=60;STATUS=OK" -> one chip per variable. */
static void append_chips(char *body, size_t body_size, const char *measurements) {
    char copy[MAX_LINE], *save;
    snprintf(copy, sizeof copy, "%s", measurements);
    append(body, body_size, "<div class='chips'>");
    for (char *field = strtok_r(copy, ";", &save); field; field = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(field, '=');
        if (!eq) continue;
        *eq = '\0';
        append(body, body_size, "<span class='chip%s'><b>%s</b>%s</span>",
               measurement_is_hot(field, eq + 1) ? " hot" : "", field, eq + 1);
    }
    append(body, body_size, "</div>");
}

/* HTML page with the five views the assignment asks for: server state, registered
 * nodes, active nodes, last measurements and recent alerts. Styling lives in
 * status_page_css.h. Node ids are validated to [A-Za-z0-9_-] on entry, so they are
 * safe to print without escaping. */
static void build_html(const RegistrySnapshot *snapshot, char *body, size_t body_size) {
    char text[MAX_LINE], uptime[32];
    time_t now = time(NULL);
    body[0] = '\0';
    int tcp_ok = service_healthy(0), udp_ok = service_healthy(1);
    int registered = count_registered_nodes(snapshot), active = count_active_nodes(snapshot, now);
    format_uptime((long)(now - snapshot->started_at), uptime, sizeof uptime);

    append(body, body_size,
           "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<meta http-equiv='refresh' content='5'><title>Telemetry</title><style>%s</style></head>"
           "<body><div class='wrap'>", STATUS_PAGE_CSS);

    append(body, body_size,
           "<header class='top'><div><h1>Telemetry server</h1>"
           "<p class='sub'>Live view of the platform. Refreshes every 5 seconds.</p></div>"
           "<div class='health'><span class='badge%s'>TCP loop %s</span>"
           "<span class='badge%s'>UDP loop %s</span></div></header>",
           tcp_ok ? "" : " bad", tcp_ok ? "responsive" : "stalled",
           udp_ok ? "" : " bad", udp_ok ? "responsive" : "stalled");

    append(body, body_size,
           "<div class='kpis'>"
           "<div class='kpi'><span>Registered nodes</span><strong>%d</strong></div>"
           "<div class='kpi'><span>Active nodes</span><strong>%d</strong></div>"
           "<div class='kpi%s'><span>Alerts raised</span><strong>%d</strong></div>"
           "<div class='kpi'><span>Uptime</span><strong>%s</strong></div>"
           "<div class='kpi'><span>Parsed UDP datagrams</span><strong>%ld</strong></div>"
           "<div class='kpi%s' title='Estimated missing after sender reports'>"
           "<span>Estimated missing</span><strong>%ld</strong></div></div>",
           registered, active, snapshot->total_alerts > 0 ? " hot" : "", snapshot->total_alerts,
           uptime, snapshot->datagrams_total,
           sum_lost_datagrams(snapshot) > 0 ? " hot" : "", sum_lost_datagrams(snapshot));

    append(body, body_size, "<section><h2>Nodes and last measurements <small>%d registered, %d active</small></h2>"
                            "<div class='card'>", registered, active);
    if (registered == 0) {
        append(body, body_size, "<div class='empty'>No nodes have reported yet.</div>");
    } else {
        append(body, body_size, "<div class='scroll'><table class='stack'><thead><tr><th>Node</th><th>State</th>"
                                "<th>Last seen</th><th>Measurements</th></tr></thead><tbody>");
        for (int i = 0; i < MAX_NODES; i++) {
            const TelemetryNode *node = &snapshot->nodes[i];
            if (!node->in_use) continue;
            int is_active = node_is_active(node, now);
            append(body, body_size, "<tr><td class='id'>%s</td><td><span class='badge%s'>%s</span></td>",
                   node->node_id, is_active ? "" : " warn", is_active ? "active" : "inactive");
            if (node->last_seen) {
                append(body, body_size, "<td class='num'>%ld s ago</td><td>", (long)(now - node->last_seen));
                append_chips(body, body_size, format_measurements(node, text, sizeof text));
            } else {
                append(body, body_size, "<td class='muted'>-</td><td class='muted'>No data yet");
            }
            append(body, body_size, "</td></tr>");
        }
        append(body, body_size, "</tbody></table></div>");
    }
    append(body, body_size, "</div></section>");

    append(body, body_size, "<section><h2>Recent alerts <small>%d in total, server time</small></h2><div class='card'>",
           snapshot->total_alerts);
    int total = snapshot->total_alerts, shown = total < 20 ? total : 20;
    if (shown == 0) {
        append(body, body_size, "<div class='empty'>No alerts so far.</div>");
    } else {
        append(body, body_size, "<div class='scroll'><table><thead><tr><th>Time</th><th>Node</th>"
                                "<th>Type</th><th>Value</th></tr></thead><tbody>");
        for (int index = total - 1; index >= total - shown; index--) {
            const Alert *alert = &snapshot->alerts[index % MAX_ALERTS];
            struct tm local;
            localtime_r(&alert->timestamp, &local);
            strftime(text, sizeof text, "%H:%M:%S", &local);
            append(body, body_size, "<tr><td class='num'>%s</td><td class='id'>%s</td>"
                                    "<td><span class='badge bad plain'>%s</span></td><td class='num'>%.2f</td></tr>",
                   text, alert->node_id, alert->alert_type, alert->value);
        }
        append(body, body_size, "</tbody></table></div>");
    }
    append(body, body_size, "</div></section>");

    /* Every counter of the STATS view, one per cell, instead of one long line. */
    char status[MAX_LINE], *save;
    format_system_status(snapshot, status, sizeof status);
    append(body, body_size, "<section><details class='card' open><summary>Measured status and counters</summary><dl class='kv'>");
    for (char *field = strtok_r(status, ";", &save); field; field = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(field, '=');
        if (!eq) continue;
        *eq = '\0';
        append(body, body_size, "<div><dt>%s</dt><dd>%s</dd></div>", field, eq + 1);
    }
    append(body, body_size, "</dl></details></section>");

    append(body, body_size, "<nav class='links'>JSON <a href='/status'>/status</a> <a href='/nodes'>/nodes</a> "
                            "<a href='/alerts'>/alerts</a></nav></div></body></html>");
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
