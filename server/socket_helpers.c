#include "socket_helpers.h"
#include "config.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <time.h>

static int bind_to_port(int socket_fd, int port) {
    struct sockaddr_in local_address;
    memset(&local_address, 0, sizeof local_address);
    local_address.sin_family      = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);     /* every interface */
    local_address.sin_port        = htons((unsigned short)port);
    if (bind(socket_fd, (struct sockaddr *)&local_address, sizeof local_address) < 0) {
        perror("bind");
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

int create_tcp_listener(int port) {
    int listener_fd = socket(AF_INET, SOCK_STREAM, 0);      /* TCP socket creation */
    if (listener_fd < 0) { perror("socket(TCP)"); return -1; }

    int reuse_address = 1;                                   /* restart without waiting for TIME_WAIT */
    setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof reuse_address);

    if (bind_to_port(listener_fd, port) < 0) return -1;
    if (listen(listener_fd, TCP_BACKLOG) < 0) {              /* listen */
        perror("listen");
        close(listener_fd);
        return -1;
    }
    return listener_fd;
}

int create_udp_socket(int port) {
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);             /* UDP socket creation */
    if (udp_fd < 0) { perror("socket(UDP)"); return -1; }
    return bind_to_port(udp_fd, port);
}

long monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

int wait_readable(int fd, int timeout_ms) {
    struct pollfd p = {fd, POLLIN, 0};
    int result;
    do { result = poll(&p, 1, timeout_ms); } while (result < 0 && errno == EINTR);
    return result;
}

int send_full_buffer(int fd, const char *buffer, size_t length) {
    long deadline = monotonic_ms() + 1000;
    while (length) {
        long remaining = deadline - monotonic_ms();
        if (remaining <= 0) return -1;
        struct pollfd p = {fd, POLLOUT, 0};
        int ready = poll(&p, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return -1;
        ssize_t sent = send(fd, buffer, length, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (sent <= 0) return -1;
        buffer += sent;
        length -= (size_t)sent;
    }
    return 0;
}

/* Exactly one owner thread writes each connection. Assemble newline before sending. */
int send_text_line(int fd, const char *text) {
    char frame[MAX_LINE + 1];
    size_t n = strlen(text);
    if (n >= MAX_LINE) return -1;
    memcpy(frame, text, n);
    frame[n] = '\n';
    int result = send_full_buffer(fd, frame, n + 1);
    if (result < 0) shutdown(fd, SHUT_RDWR);
    return result;
}

/* Absolute deadline defeats clients sending one byte just before each timeout. */
int receive_line_until(int fd, char *buffer, size_t capacity, long deadline) {
    size_t n = 0;
    int invalid = 0, overflow = 0;
    for (;;) {
        long remaining = deadline - monotonic_ms();
        if (remaining <= 0 || wait_readable(fd, (int)remaining) <= 0) return -1;
        unsigned char c;
        ssize_t count = recv(fd, &c, 1, MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count <= 0) return (int)count;
        if (c == '\n') break;
        if (c == '\r') { /* CR allowed only immediately before LF */
            if (n + 1 < capacity) buffer[n++] = (char)c; else overflow = 1;
            continue;
        }
        if (c < 32 || c > 126) invalid = 1;
        if (n + 1 < capacity) buffer[n++] = (char)c; else overflow = 1;
    }
    if (n && buffer[n-1] == '\r') n--;
    buffer[n] = 0;
    if (strchr(buffer, '\r')) invalid = 1;
    return overflow ? -2 : invalid ? -3 : (int)n + 1;
}

int receive_text_line(int fd, char *buffer, size_t capacity) {
    return receive_line_until(fd, buffer, capacity, monotonic_ms() + 3000);
}

const char *describe_peer(const struct sockaddr_in *peer_address, char *out, size_t out_size) {
    char ip_text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_address->sin_addr, ip_text, sizeof ip_text);
    snprintf(out, out_size, "%s:%d", ip_text, ntohs(peer_address->sin_port));
    return out;
}
