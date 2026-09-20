#include "response_format.h"
#include <stdio.h>
#include <string.h>
#include "service_health.h"
#include "alert_subscribers.h"

char *format_measurements(const TelemetryNode *node, char *out, size_t out_size) {
    size_t written = 0;
    out[0] = '\0';
    for (int i = 0; i < node->measurement_count && written < out_size; i++) {
        const Measurement *m = &node->measurements[i];
        const char *separator = i ? ";" : "";
        int count = strcmp(m->name, "STATUS") == 0
            ? snprintf(out + written, out_size - written, "%s%s=%s", separator, m->name, m->value ? "OK" : "FAIL")
            : snprintf(out + written, out_size - written, "%s%s=%.2f", separator, m->name, m->value);
        if (count < 0) break;
        written += (size_t)count;
    }
    return out;
}

const TelemetryNode *snapshot_find_node(const RegistrySnapshot *snapshot, const char *node_id) {
    for (int i = 0; i < MAX_NODES; i++)
        if (snapshot->nodes[i].in_use && strcmp(snapshot->nodes[i].node_id, node_id) == 0)
            return &snapshot->nodes[i];
    return NULL;
}

int count_registered_nodes(const RegistrySnapshot *snapshot) {
    int total = 0;
    for (int i = 0; i < MAX_NODES; i++) total += snapshot->nodes[i].in_use;
    return total;
}

int count_active_nodes(const RegistrySnapshot *snapshot, time_t now) {
    int total = 0;
    for (int i = 0; i < MAX_NODES; i++) total += node_is_active(&snapshot->nodes[i], now);
    return total;
}

long sum_lost_datagrams(const RegistrySnapshot *snapshot) {
    long total = 0;
    for (int i = 0; i < MAX_NODES; i++)
        if (snapshot->nodes[i].in_use) total += snapshot->nodes[i].datagrams_lost;
    return total;
}

char *format_node_stats(const TelemetryNode *n, char *out, size_t size) {
    snprintf(out, size,
        "attempts=%ld;sent=%ld;omitted=%ld;send_errors=%ld;unique=%ld;duplicates=%ld;reordered=%ld;loss_estimated=%ld;report_seen=%d;final_report=%d",
        n->attempts, n->sent, n->omitted, n->send_errors, n->datagrams_received,
        n->duplicates, n->reordered, n->datagrams_lost, n->report_seen, n->final_report);
    return out;
}
char *format_system_status(const RegistrySnapshot *s, char *out, size_t size) {
    long attempts=0, sent=0, omitted=0, errors=0, unique=0, duplicates=0, reordered=0;
    int reports=0, finals=0;
    for (int i=0; i<MAX_NODES; i++) {
        const TelemetryNode *n = &s->nodes[i];
        if (!n->in_use) continue;
        attempts+=n->attempts; sent+=n->sent; omitted+=n->omitted; errors+=n->send_errors;
        unique+=n->datagrams_received; duplicates+=n->duplicates; reordered+=n->reordered;
        reports+=n->report_seen; finals+=n->final_report;
    }
    snprintf(out, size,
        "boot_id=%ld;uptime=%ld;registered=%d;active=%d;tcp_ok=%d;udp_ok=%d;udp_rx=%ld;udp_invalid=%ld;udp_unknown=%ld;attempts=%ld;sent=%ld;omitted=%ld;send_errors=%ld;unique=%ld;duplicates=%ld;reordered=%ld;loss_estimated=%ld;reports=%d;final_reports=%d;alerts=%d;slow_disconnected=%ld",
        s->boot_id, (long)(time(NULL)-s->started_at), count_registered_nodes(s), count_active_nodes(s,time(NULL)),
        service_healthy(0), service_healthy(1), s->datagrams_total, s->datagrams_invalid,
        s->datagrams_unknown_node, attempts, sent, omitted, errors, unique, duplicates,
        reordered, sum_lost_datagrams(s), reports, finals, s->total_alerts, subscribers_dropped());
    return out;
}
