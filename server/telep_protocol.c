#include "telep_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parse_message(const char *line, ParsedMessage *message) {
    strncpy(message->raw_copy, line, MAX_LINE - 1);
    message->raw_copy[MAX_LINE - 1] = '\0';
    message->arg_count = 0;

    char *tokenizer_state;
    message->command = strtok_r(message->raw_copy, "|", &tokenizer_state);
    if (!message->command || !*message->command) return -1;

    char *field;
    while (message->arg_count < MAX_ARGS && (field = strtok_r(NULL, "|", &tokenizer_state)))
        message->args[message->arg_count++] = field;
    return 0;
}

/* "TEMP=24.8;HUM=60.1;STATUS=OK" -> Measurement[] */
int parse_measurements(const char *field, Measurement *out, int max) {
    char field_copy[MAX_LINE];
    strncpy(field_copy, field, sizeof field_copy - 1);
    field_copy[sizeof field_copy - 1] = '\0';

    int count = 0;
    char *tokenizer_state;
    char *pair = strtok_r(field_copy, ";", &tokenizer_state);
    while (pair && count < max) {
        char *equals_sign = strchr(pair, '=');
        if (!equals_sign || equals_sign == pair) return -1;
        *equals_sign = '\0';
        const char *name = pair, *value_text = equals_sign + 1;
        if (strlen(name) >= MAX_VAR_NAME || *value_text == '\0') return -1;

        char *parse_end;
        double value = strtod(value_text, &parse_end);
        if (*parse_end != '\0') {                     /* not a number: only STATUS accepts text */
            if (strcmp(name, "STATUS") != 0) return -1;
            value = strcmp(value_text, "OK") == 0 ? 1.0 : 0.0;
        }
        strcpy(out[count].name, name);
        out[count].value = value;
        count++;
        pair = strtok_r(NULL, ";", &tokenizer_state);
    }
    return count;
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
    }
    snprintf(out, out_size, "ERR|%d|%s", error_code, error_text);
}
