#ifndef TELEP_PROTOCOL_H
#define TELEP_PROTOCOL_H
#include "config.h"
#include <stddef.h>

/* TELEP/2.0: ASCII text, fields separated by '|', line ends with '\n'.
 *   COMMAND|arg1|arg2|...
 * Measurements travel in one field as "NAME=value" pairs separated by ';'. */

typedef struct {
    char  raw_copy[MAX_LINE];      /* mutable copy that command and args point into */
    char *command;
    char *args[MAX_ARGS];
    int   arg_count;
} ParsedMessage;

typedef struct {
    char   name[MAX_VAR_NAME];     /* TEMP, HUM, POWER, VIB, STATUS */
    double value;                  /* STATUS: 1.0 = OK, 0.0 = FAIL */
} Measurement;

/* Error codes for the reply ERR|<code>|<text> */
#define ERR_BAD_FORMAT   100
#define ERR_UNKNOWN_CMD  101
#define ERR_UNKNOWN_NODE 102
#define ERR_TOO_LONG     103
#define ERR_SERVER_FULL  104
#define ERR_ID_IN_USE    105

int  parse_message(const char *line, ParsedMessage *message);             /* 0 ok, -1 empty line */
int  parse_measurements(const char *field, Measurement *out, int max);    /* count, -1 invalid format */
int  is_valid_node_id(const char *node_id);                               /* [A-Za-z0-9_-]{1,15} */
void build_error_response(char *out, size_t out_size, int error_code);

int parse_uint(const char *text, long max, long *out);
int is_valid_session(const char *text);
#endif
