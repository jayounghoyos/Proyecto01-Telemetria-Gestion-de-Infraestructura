#include "service_health.h"
#include "socket_helpers.h"
#include <pthread.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static long beats[2];
void service_heartbeat(int service) {
    pthread_mutex_lock(&lock); beats[service] = monotonic_ms(); pthread_mutex_unlock(&lock);
}
int service_healthy(int service) {
    pthread_mutex_lock(&lock);
    int healthy = beats[service] && monotonic_ms() - beats[service] < 3000;
    pthread_mutex_unlock(&lock); return healthy;
}
