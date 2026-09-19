#ifndef NODE_REGISTRY_H
#define NODE_REGISTRY_H
#include "config.h"
#include "telep_protocol.h"
#include <time.h>

/* In-memory server state: registered nodes and alerts.
 * Static arrays guarded by a single mutex inside node_registry.c. */

typedef struct {
    int         in_use;
    char        node_id[MAX_NODE_ID];
    time_t      last_seen;                   /* 0 = no telemetry yet */
    Measurement measurements[MAX_VARS];
    int         measurement_count;
    long        last_sequence;               /* last UDP seq seen; -1 = none */
    long        datagrams_received;
    long        datagrams_lost;              /* gaps detected in seq */
    long        resyncs;                     /* node restarts or duplicated ids */
} TelemetryNode;

typedef struct {
    time_t timestamp;
    char   node_id[MAX_NODE_ID];
    char   alert_type[MAX_VAR_NAME + 8];     /* TEMP_HIGH, STATUS_FAIL, ... */
    double value;
} Alert;

typedef struct {
    TelemetryNode nodes[MAX_NODES];
    Alert         alerts[MAX_ALERTS];        /* ring buffer: index = total_alerts % MAX_ALERTS */
    int           total_alerts;
    time_t        started_at;
    long          datagrams_total;
    long          datagrams_unknown_node;
} RegistrySnapshot;

void registry_init(void);
int  registry_register_node(const char *node_id);                        /* 0 ok, -1 no room */
int  registry_record_telemetry(const char *node_id, long sequence,
                               const Measurement *values, int count);    /* 0 ok, -1 unknown node */
int  node_is_active(const TelemetryNode *node, time_t now);
void registry_copy_snapshot(RegistrySnapshot *out);                      /* copy under the mutex */

#endif
