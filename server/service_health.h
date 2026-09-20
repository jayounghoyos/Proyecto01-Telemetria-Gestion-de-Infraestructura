#ifndef SERVICE_HEALTH_H
#define SERVICE_HEALTH_H
void service_heartbeat(int service); /* 0 TCP listener, 1 UDP receiver */
int service_healthy(int service);
#endif
