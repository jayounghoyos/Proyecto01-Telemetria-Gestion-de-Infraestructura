#include "config.h"
#include "node_registry.h"
#include "server_threads.h"
#include "socket_helpers.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* Entry point: creates the listening sockets and starts one thread per service.
 * SIGPIPE is ignored so a client closing mid-send cannot kill the process. */
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);      /* line-buffered logs, visible in docker logs */
    registry_init();

    int udp_fd = create_udp_socket(UDP_PORT);
    int tcp_fd  = create_tcp_listener(TCP_PORT);
    int http_fd = create_tcp_listener(HTTP_PORT);
    if (udp_fd < 0 || tcp_fd < 0 || http_fd < 0) return EXIT_FAILURE;

    pthread_t udp_thread, tcp_thread, http_thread;
    pthread_create(&udp_thread,  NULL, udp_telemetry_server, &udp_fd);
    pthread_create(&tcp_thread,  NULL, tcp_command_server,   &tcp_fd);
    pthread_create(&http_thread, NULL, http_status_server,   &http_fd);
    printf("[main] TELEP/1.0 server ready (tcp %d, udp %d, http %d)\n", TCP_PORT, UDP_PORT, HTTP_PORT);

    pthread_join(udp_thread, NULL);        /* threads run forever; Ctrl-C ends the process */
    return EXIT_SUCCESS;
}
