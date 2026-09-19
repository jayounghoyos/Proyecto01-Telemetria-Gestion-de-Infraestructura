#include "socket_helpers.h"
#include "config.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

int send_full_buffer(int socket_fd, const char *buffer, size_t length) {
    while (length > 0) {
        ssize_t sent = send(socket_fd, buffer, length, MSG_NOSIGNAL);   /* send */
        if (sent <= 0) { perror("send"); return -1; }
        buffer += sent;
        length -= (size_t)sent;
    }
    return 0;
}

int send_text_line(int socket_fd, const char *text) {
    if (send_full_buffer(socket_fd, text, strlen(text)) < 0) return -1;
    return send_full_buffer(socket_fd, "\n", 1);
}

int receive_text_line(int socket_fd, char *buffer, size_t buffer_size) {
    size_t length = 0;
    int line_too_long = 0;
    for (;;) {
        char received_char;
        ssize_t count = recv(socket_fd, &received_char, 1, 0);          /* receive byte by byte */
        if (count == 0) return 0;                                       /* peer closed the connection */
        if (count < 0)  { perror("recv"); return -1; }
        if (received_char == '\n') break;
        if (received_char == '\r') continue;
        if (length + 1 < buffer_size) buffer[length++] = received_char;
        else line_too_long = 1;                                         /* keep reading to discard */
    }
    buffer[length] = '\0';
    return line_too_long ? -2 : (int)length + 1;
}

const char *describe_peer(const struct sockaddr_in *peer_address, char *out, size_t out_size) {
    char ip_text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_address->sin_addr, ip_text, sizeof ip_text);
    snprintf(out, out_size, "%s:%d", ip_text, ntohs(peer_address->sin_port));
    return out;
}
