#ifndef SERVER_THREADS_H
#define SERVER_THREADS_H
/* Each server runs in its own thread. The argument points to the fd created in main. */
void *udp_telemetry_server(void *listening_fd);
void *tcp_command_server(void *listening_fd);
#endif
