#ifndef ALERT_SUBSCRIBERS_H
#define ALERT_SUBSCRIBERS_H
#include "node_registry.h"

/* Operators that sent SUBSCRIBE. Every new alert is pushed to them over their
 * TCP connection as ALERT|<node>|<type>|<value>. */
int  subscribers_add(int socket_fd);          /* -1 if there is no room */
void subscribers_remove(int socket_fd);
void subscribers_broadcast(const Alert *alert);

#endif
