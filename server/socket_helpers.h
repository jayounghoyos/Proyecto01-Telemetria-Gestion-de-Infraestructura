#ifndef SOCKET_HELPERS_H
#define SOCKET_HELPERS_H
#include <netinet/in.h>
#include <stddef.h>

/* Socket operations shared by the three servers (TCP, UDP, HTTP). */

long monotonic_ms(void);
int wait_readable(int fd, int timeout_ms);
int receive_line_until(int fd, char *buffer, size_t capacity, long deadline);

int create_tcp_listener(int port);   /* socket() + bind() + listen(); -1 on failure */
int create_udp_socket(int port);     /* socket() + bind();            -1 on failure */

int send_full_buffer(int socket_fd, const char *buffer, size_t length);
int send_text_line(int socket_fd, const char *text);   /* appends '\n' */

/* Reads up to '\n' (not included). Returns:
 *   >0 length+1, 0 if the peer closed, -1 error, -2 line longer than the buffer (discarded). */
int receive_text_line(int socket_fd, char *buffer, size_t buffer_size);

/* Writes "ip:port" of the remote peer into out and returns it. */
const char *describe_peer(const struct sockaddr_in *peer_address, char *out, size_t out_size);

#endif
