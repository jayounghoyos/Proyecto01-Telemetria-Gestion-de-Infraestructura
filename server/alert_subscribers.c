#include "alert_subscribers.h"
#include "socket_helpers.h"
#include <pthread.h>
#include <stdio.h>

#define MAX_SUBSCRIBERS 32

static int subscriber_fds[MAX_SUBSCRIBERS];   /* 0 = free slot (fd 0 is never a client) */
static pthread_mutex_t subscribers_mutex = PTHREAD_MUTEX_INITIALIZER;

int subscribers_add(int socket_fd) {
    int result = -1;
    pthread_mutex_lock(&subscribers_mutex);
    for (int i = 0; i < MAX_SUBSCRIBERS && result < 0; i++)
        if (subscriber_fds[i] == 0) { subscriber_fds[i] = socket_fd; result = 0; }
    pthread_mutex_unlock(&subscribers_mutex);
    return result;
}

void subscribers_remove(int socket_fd) {
    pthread_mutex_lock(&subscribers_mutex);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++)
        if (subscriber_fds[i] == socket_fd) subscriber_fds[i] = 0;
    pthread_mutex_unlock(&subscribers_mutex);
}

/* Called from the UDP thread, outside the registry mutex. A failed send
 * (operator gone) only drops that subscriber. */
void subscribers_broadcast(const Alert *alert) {
    char message[MAX_LINE];
    snprintf(message, sizeof message, "ALERT|%s|%s|%.2f", alert->node_id, alert->alert_type, alert->value);
    pthread_mutex_lock(&subscribers_mutex);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (subscriber_fds[i] == 0) continue;
        if (send_text_line(subscriber_fds[i], message) < 0) subscriber_fds[i] = 0;
        else printf("[alert] pushed to subscriber fd=%d: %s\n", subscriber_fds[i], message);
    }
    pthread_mutex_unlock(&subscribers_mutex);
}
