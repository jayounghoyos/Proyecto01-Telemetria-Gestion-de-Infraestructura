#include "node_registry.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static RegistrySnapshot registry;
static pthread_mutex_t  registry_mutex = PTHREAD_MUTEX_INITIALIZER;

void registry_init(void) {
    memset(&registry, 0, sizeof registry);
    registry.started_at = time(NULL);
}

static TelemetryNode *find_node(const char *node_id) {
    for (int i = 0; i < MAX_NODES; i++)
        if (registry.nodes[i].in_use && strcmp(registry.nodes[i].node_id, node_id) == 0)
            return &registry.nodes[i];
    return NULL;
}

int registry_register_node(const char *node_id) {
    pthread_mutex_lock(&registry_mutex);
    int result = 0;
    if (!find_node(node_id)) {
        result = -1;
        for (int i = 0; i < MAX_NODES; i++) {
            TelemetryNode *slot = &registry.nodes[i];
            if (slot->in_use) continue;
            memset(slot, 0, sizeof *slot);
            strcpy(slot->node_id, node_id);
            slot->in_use = 1;
            slot->last_sequence = -1;
            printf("[registry] node registered: %s\n", node_id);
            result = 0;
            break;
        }
    }
    pthread_mutex_unlock(&registry_mutex);
    return result;
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

static void add_alert(const char *node_id, const char *variable_name, const char *suffix, double value) {
    Alert *alert = &registry.alerts[registry.total_alerts % MAX_ALERTS];
    alert->timestamp = time(NULL);
    strcpy(alert->node_id, node_id);
    snprintf(alert->alert_type, sizeof alert->alert_type, "%s_%s", variable_name, suffix);
    alert->value = value;
    registry.total_alerts++;
    printf("[alert] %s %s %.2f\n", node_id, alert->alert_type, value);
}

int registry_record_telemetry(const char *node_id, long sequence, const Measurement *values, int count) {
    pthread_mutex_lock(&registry_mutex);
    registry.datagrams_total++;
    TelemetryNode *node = find_node(node_id);
    if (!node) {
        registry.datagrams_unknown_node++;
        pthread_mutex_unlock(&registry_mutex);
        return -1;
    }

    /* UDP loss detection: a jump in seq means datagrams that never arrived. */
    if (node->last_sequence >= 0 && sequence > node->last_sequence + 1)
        node->datagrams_lost += sequence - node->last_sequence - 1;
    if (sequence > node->last_sequence) node->last_sequence = sequence;
    node->datagrams_received++;
    node->last_seen = time(NULL);

    /* Alert only when a value crosses its threshold: if the previous
     * measurement was already anomalous, do not repeat the alert. */
    for (int i = 0; i < count; i++) {
        const Measurement *current = &values[i];
        if (is_anomalous_value(current) && !was_anomalous(node, current->name)) {
            if (strcmp(current->name, "STATUS") == 0) add_alert(node_id, "STATUS", "FAIL", 0);
            else add_alert(node_id, current->name, "HIGH", current->value);
        }
    }
    node->measurement_count = count;
    memcpy(node->measurements, values, sizeof(Measurement) * (size_t)count);
    pthread_mutex_unlock(&registry_mutex);
    return 0;
}

int node_is_active(const TelemetryNode *node, time_t now) {
    return node->in_use && node->last_seen && (now - node->last_seen) < ACTIVE_TIMEOUT;
}

void registry_copy_snapshot(RegistrySnapshot *out) {
    pthread_mutex_lock(&registry_mutex);
    *out = registry;
    pthread_mutex_unlock(&registry_mutex);
}
