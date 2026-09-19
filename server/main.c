#include "config.h"
#include "node_registry.h"
#include "server_threads.h"
#include "socket_helpers.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* Entry point: creates the UDP socket and starts the telemetry receiver thread. */
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);      /* line-buffered logs, visible in docker logs */
    registry_init();

    int udp_fd = create_udp_socket(UDP_PORT);
    if (udp_fd < 0) return EXIT_FAILURE;

    pthread_t udp_thread;
    pthread_create(&udp_thread, NULL, udp_telemetry_server, &udp_fd);
    printf("[main] TELEP/1.0 server ready (udp %d)\n", UDP_PORT);

    pthread_join(udp_thread, NULL);        /* the thread runs forever; Ctrl-C ends the process */
    return EXIT_SUCCESS;
}
