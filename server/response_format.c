#include "response_format.h"
#include <stdio.h>
#include <string.h>

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

char *format_system_status(const RegistrySnapshot *snapshot, char *out, size_t out_size) {
    time_t now = time(NULL);
    snprintf(out, out_size,
             "uptime=%ld;registered=%d;active=%d;udp_rx=%ld;udp_lost=%ld;udp_unknown=%ld;alerts=%d",
             (long)(now - snapshot->started_at), count_registered_nodes(snapshot),
             count_active_nodes(snapshot, now), snapshot->datagrams_total,
             sum_lost_datagrams(snapshot), snapshot->datagrams_unknown_node, snapshot->total_alerts);
    return out;
}
