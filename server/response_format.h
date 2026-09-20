#ifndef RESPONSE_FORMAT_H
#define RESPONSE_FORMAT_H
#include "node_registry.h"
#include <stddef.h>

/* Text formatting shared by the TCP and HTTP replies. */
char *format_measurements(const TelemetryNode *node, char *out, size_t out_size); /* TEMP=24.80;HUM=60.10 */
char *format_system_status(const RegistrySnapshot *snapshot, char *out, size_t out_size);
const TelemetryNode *snapshot_find_node(const RegistrySnapshot *snapshot, const char *node_id);
int   count_registered_nodes(const RegistrySnapshot *snapshot);
int   count_active_nodes(const RegistrySnapshot *snapshot, time_t now);
long  sum_lost_datagrams(const RegistrySnapshot *snapshot);

char *format_node_stats(const TelemetryNode *n, char *out, size_t size);
#endif
