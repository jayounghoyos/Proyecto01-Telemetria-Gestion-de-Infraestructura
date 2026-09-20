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
    char        session[MAX_SESSION];
    long        duplicates, reordered;
    long        attempts, sent, omitted, send_errors;
    int         report_seen, final_report;
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
    long          boot_id;
    time_t        started_at;
    long          datagrams_total;
    long          datagrams_unknown_node;
    long          datagrams_invalid;
} RegistrySnapshot;

void registry_init(void);
int  registry_register_node(const char *node_id, const char *session);                        /* 0 OK, otherwise protocol error code */
/* Records the measurement and leaves the alerts it generated in new_alerts (room for
 * MAX_VARS). Returns how many there were, or -1 if there is no room for the node. */
int  registry_record_telemetry(const char *node_id, const char *session, long sequence,
                               const Measurement *values, int count, Alert *new_alerts);
int  node_is_active(const TelemetryNode *node, time_t now);
void registry_copy_snapshot(RegistrySnapshot *out);                      /* copy under the mutex */

int registry_report(const char *id, const char *session, long attempts, long sent, long omitted, long errors, int final);
void registry_invalid_datagram(void);
#endif
