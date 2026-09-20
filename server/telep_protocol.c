#include "telep_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
int parse_uint(const char *text, long max, long *out) {
    if (!*text) return -1;
    for (const char *c = text; *c; c++) if (*c < '0' || *c > '9') return -1;
    errno = 0; char *end;
    long value = strtol(text, &end, 10);
    if (errno || *end || value < 0 || value > max) return -1;
    *out = value; return 0;
}
int is_valid_session(const char *text) {
    if (strlen(text) != 32) return 0;
    for (int i = 0; i < 32; i++) if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return 0;
    return 1;
}
int parse_message(const char *line, ParsedMessage *m) {
    size_t n = strlen(line);
    if (!n || n >= MAX_LINE) return -1;
    for (size_t i = 0; i < n; i++) if ((unsigned char)line[i] < 33 || (unsigned char)line[i] > 126) return -1;
    strcpy(m->raw_copy, line); m->command = m->raw_copy; m->arg_count = 0;
    char *part = m->raw_copy, *next;
    while ((next = strchr(part, '|'))) {
        *next = 0;
        if (!*part || !next[1] || m->arg_count == MAX_ARGS) return -1;
        part = next + 1; m->args[m->arg_count++] = part;
    }
    return 0;
}
int parse_measurements(const char *field, Measurement *out, int max) {
    if (!*field || strlen(field) >= MAX_LINE) return -1;
    char copy[MAX_LINE]; strcpy(copy, field);
    int count = 0; char *pair = copy;
    while (pair) {
        char *next = strchr(pair, ';');
        if (next) *next++ = 0;
        char *equals = strchr(pair, '=');
        if (!equals || equals == pair || !equals[1] || count == max) return -1;
        *equals = 0; const char *value_text = equals + 1;
        for (int i = 0; i < count; i++) if (!strcmp(out[i].name, pair)) return -1;
        double value, low = 0, high = 0;
        if (!strcmp(pair, "STATUS")) {
            if (strcmp(value_text, "OK") && strcmp(value_text, "FAIL")) return -1;
            value = !strcmp(value_text, "OK");
        } else {
            if (!strcmp(pair, "TEMP")) { low = -100; high = 200; }
            else if (!strcmp(pair, "HUM")) high = 100;
            else if (!strcmp(pair, "POWER")) high = 1000000;
            else if (!strcmp(pair, "VIB")) high = 1000;
            else return -1;
            for (const char *c = value_text; *c; c++) if (!strchr("0123456789+-.eE", *c)) return -1;
            errno = 0; char *end;
            value = strtod(value_text, &end);
            if (errno || end == value_text || *end || !isfinite(value) || value < low || value > high) return -1;
        }
        strcpy(out[count].name, pair); out[count++].value = value;
        pair = next;
    }
    return count >= 3 ? count : -1;
}

int is_valid_node_id(const char *node_id) {
    size_t length = strlen(node_id);
    if (length == 0 || length >= MAX_NODE_ID) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)node_id[i];
        if (!isalnum(c) && c != '_' && c != '-') return 0;
    }
    return 1;
}

void build_error_response(char *out, size_t out_size, int error_code) {
    const char *error_text = "UNKNOWN";
    switch (error_code) {
        case ERR_BAD_FORMAT:   error_text = "BAD_FORMAT";   break;
        case ERR_UNKNOWN_CMD:  error_text = "UNKNOWN_CMD";  break;
        case ERR_UNKNOWN_NODE: error_text = "UNKNOWN_NODE"; break;
        case ERR_TOO_LONG:     error_text = "TOO_LONG";     break;
        case ERR_ID_IN_USE: error_text = "ID_IN_USE"; break;
        case ERR_SERVER_FULL:  error_text = "SERVER_FULL";  break;
    }
    snprintf(out, out_size, "ERR|%d|%s", error_code, error_text);
}
