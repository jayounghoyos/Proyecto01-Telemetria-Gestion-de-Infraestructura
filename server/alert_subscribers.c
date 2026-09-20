#include "alert_subscribers.h"
#include "socket_helpers.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#define SUBSCRIBERS 32
#define QUEUE_SIZE 32
typedef struct { int fd, head, count; char lines[QUEUE_SIZE][MAX_LINE]; } Subscriber;
static Subscriber slots[SUBSCRIBERS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static long dropped;
int subscribers_add(int fd) {
    pthread_mutex_lock(&lock);
    int free_slot = -1;
    for (int i = 0; i < SUBSCRIBERS; i++) {
        if (slots[i].fd == fd) { pthread_mutex_unlock(&lock); return 0; }
        if (!slots[i].fd) free_slot = i;
    }
    if (free_slot >= 0) slots[free_slot] = (Subscriber){.fd = fd};
    pthread_mutex_unlock(&lock); return free_slot < 0 ? -1 : 0;
}
void subscribers_remove(int fd) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < SUBSCRIBERS; i++) if (slots[i].fd == fd) slots[i].fd = 0;
    pthread_mutex_unlock(&lock);
}
long subscribers_dropped(void) {
    pthread_mutex_lock(&lock); long n = dropped; pthread_mutex_unlock(&lock); return n;
}
/* Producer only enqueues. No send, waiting for a client, or close/reuse of its fd. */
void subscribers_broadcast(const Alert *alert) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < SUBSCRIBERS; i++) {
        Subscriber *s = &slots[i];
        if (!s->fd) continue;
        if (s->count == QUEUE_SIZE) {
            shutdown(s->fd, SHUT_RDWR); s->fd = 0; dropped++; continue;
        }
        snprintf(s->lines[(s->head + s->count) % QUEUE_SIZE], MAX_LINE,
                 "ALERT|%s|%s|%.2f", alert->node_id, alert->alert_type, alert->value);
        s->count++;
    }
    pthread_mutex_unlock(&lock);
}
/* Called only by the connection's sole writer, between complete command replies. */
int subscribers_drain(int fd) {
    for (int n = 0; n < QUEUE_SIZE; n++) {
        char line[MAX_LINE] = "";
        pthread_mutex_lock(&lock);
        for (int i = 0; i < SUBSCRIBERS; i++) {
            Subscriber *s = &slots[i];
            if (s->fd != fd || !s->count) continue;
            strcpy(line, s->lines[s->head]); s->head = (s->head + 1) % QUEUE_SIZE; s->count--; break;
        }
        pthread_mutex_unlock(&lock);
        if (!*line) return 0;
        if (send_text_line(fd, line) < 0) return -1;
    }
    return 0;
}
