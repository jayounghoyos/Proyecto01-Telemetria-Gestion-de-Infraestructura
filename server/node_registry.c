#include "node_registry.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static RegistrySnapshot registry;
static unsigned char seen[MAX_NODES][SESSION_LIMIT / 8];
static pthread_mutex_t  registry_mutex = PTHREAD_MUTEX_INITIALIZER;

void registry_init(void) {
    memset(&registry, 0, sizeof registry);
    registry.started_at = time(NULL);
    struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
    registry.boot_id = now.tv_sec * 1000000000L + now.tv_nsec;
}

static TelemetryNode *find_node(const char *node_id) {
    for (int i = 0; i < MAX_NODES; i++)
        if (registry.nodes[i].in_use && strcmp(registry.nodes[i].node_id, node_id) == 0)
            return &registry.nodes[i];
    return NULL;
}

/* Creates the node in the first free slot. Requires the mutex. NULL if there is no room. */
static TelemetryNode *create_node(const char *node_id) {
    for (int i = 0; i < MAX_NODES; i++) {
        TelemetryNode *slot = &registry.nodes[i];
        if (slot->in_use) continue;
        memset(slot, 0, sizeof *slot);
        strcpy(slot->node_id, node_id);
        slot->in_use = 1;
        slot->last_sequence = -1;
        return slot;
    }
    return NULL;
}

/* An ID belongs to one session for this server lifetime. Same-session HELLO is idempotent. */
int registry_register_node(const char *node_id, const char *session) {
    pthread_mutex_lock(&registry_mutex);
    TelemetryNode *node = find_node(node_id);
    int result = 0;
    if (node && strcmp(node->session, session)) result = ERR_ID_IN_USE;
    else if (!node) {
        node = create_node(node_id);
        if (!node) result = ERR_SERVER_FULL;
        else strcpy(node->session, session);
    }
    pthread_mutex_unlock(&registry_mutex); return result;
}
void registry_invalid_datagram(void) {
    pthread_mutex_lock(&registry_mutex); registry.datagrams_invalid++; pthread_mutex_unlock(&registry_mutex);
}
static void update_loss(TelemetryNode *node) {
    long received_in_report = 0;
    int index = (int)(node - registry.nodes);
    for (long seq = 0; seq < node->attempts; seq++)
        received_in_report += !!(seen[index][seq / 8] & (1u << (seq % 8)));
    node->datagrams_lost = node->sent > received_in_report ? node->sent - received_in_report : 0;
}
int registry_report(const char *id, const char *session, long attempts, long sent, long omitted, long errors, int final) {
    pthread_mutex_lock(&registry_mutex);
    TelemetryNode *n = find_node(id);
    int result = 0;
    if (!n || strcmp(n->session, session)) result = ERR_UNKNOWN_NODE;
    else if (attempts != sent + omitted + errors || attempts < n->attempts || sent < n->sent ||
             omitted < n->omitted || errors < n->send_errors ||
             (final && n->last_sequence >= attempts) ||
             (n->final_report && (attempts != n->attempts || sent != n->sent ||
              omitted != n->omitted || errors != n->send_errors || !final))) result = ERR_BAD_FORMAT;
    else {
        long received = 0;
        int index = (int)(n - registry.nodes);
        for (long seq = 0; seq < attempts; seq++)
            received += !!(seen[index][seq / 8] & (1u << (seq % 8)));
        if (received > sent) { pthread_mutex_unlock(&registry_mutex); return ERR_BAD_FORMAT; }
        n->attempts = attempts; n->sent = sent; n->omitted = omitted; n->send_errors = errors;
        n->report_seen = 1; n->final_report = final; update_loss(n);
    }
    pthread_mutex_unlock(&registry_mutex); return result;
}

/* Upper threshold per variable (config.h). 0 = the variable never raises an alert. */
static double alert_threshold(const char *variable_name) {
    if (strcmp(variable_name, "TEMP")  == 0) return TEMP_MAX;
    if (strcmp(variable_name, "HUM")   == 0) return HUM_MAX;
    if (strcmp(variable_name, "POWER") == 0) return POWER_MAX;
    if (strcmp(variable_name, "VIB")   == 0) return VIB_MAX;
    return 0;
}

static int is_anomalous_value(const Measurement *measurement) {
    if (strcmp(measurement->name, "STATUS") == 0) return measurement->value == 0.0;
    double limit = alert_threshold(measurement->name);
    return limit > 0 && measurement->value > limit;
}

/* Looks up the last stored value of that variable and tells whether it was already in alert. */
static int was_anomalous(const TelemetryNode *node, const char *variable_name) {
    for (int i = 0; i < node->measurement_count; i++)
        if (strcmp(node->measurements[i].name, variable_name) == 0)
            return is_anomalous_value(&node->measurements[i]);
    return 0;
}

static Alert *add_alert(const char *node_id, const char *variable_name, const char *suffix, double value) {
    Alert *alert = &registry.alerts[registry.total_alerts % MAX_ALERTS];
    alert->timestamp = time(NULL);
    strcpy(alert->node_id, node_id);
    snprintf(alert->alert_type, sizeof alert->alert_type, "%s_%s", variable_name, suffix);
    alert->value = value;
    registry.total_alerts++;
    printf("[alert] %s %s %.2f\n", node_id, alert->alert_type, value);
    return alert;
}

int registry_record_telemetry(const char *node_id, const char *session, long sequence, const Measurement *values, int count, Alert *new_alerts) {
    int new_alert_count = 0;
    pthread_mutex_lock(&registry_mutex);
    registry.datagrams_total++;
    TelemetryNode *node = find_node(node_id);
    if (!node || strcmp(node->session, session)) {
        registry.datagrams_unknown_node++;
        pthread_mutex_unlock(&registry_mutex); return -1;
    }
    if (node->final_report && sequence >= node->attempts) {
        registry.datagrams_invalid++; pthread_mutex_unlock(&registry_mutex); return -1;
    }
    unsigned char *byte = &seen[node - registry.nodes][sequence / 8];
    unsigned char mask = (unsigned char)(1u << (sequence % 8));
    if (*byte & mask) {
        node->duplicates++; pthread_mutex_unlock(&registry_mutex); return 0;
    }
    *byte |= mask;
    node->datagrams_received++;
    node->last_seen = time(NULL);
    update_loss(node);
    if (sequence < node->last_sequence) {
        node->reordered++; pthread_mutex_unlock(&registry_mutex); return 0;
    }
    node->last_sequence = sequence;

    /* Alert only when a value crosses its threshold: if the previous
     * measurement was already anomalous, do not repeat the alert. */
    for (int i = 0; i < count; i++) {
        const Measurement *current = &values[i];
        if (is_anomalous_value(current) && !was_anomalous(node, current->name)) {
            Alert *alert = strcmp(current->name, "STATUS") == 0
                ? add_alert(node_id, "STATUS", "FAIL", 0)
                : add_alert(node_id, current->name, "HIGH", current->value);
            new_alerts[new_alert_count++] = *alert;
        }
    }
    node->measurement_count = count;
    memcpy(node->measurements, values, sizeof(Measurement) * (size_t)count);
    pthread_mutex_unlock(&registry_mutex);
    return new_alert_count;
}

int node_is_active(const TelemetryNode *node, time_t now) {
    return node->in_use && node->last_seen && (now - node->last_seen) < ACTIVE_TIMEOUT;
}

void registry_copy_snapshot(RegistrySnapshot *out) {
    pthread_mutex_lock(&registry_mutex);
    *out = registry;
    pthread_mutex_unlock(&registry_mutex);
}
