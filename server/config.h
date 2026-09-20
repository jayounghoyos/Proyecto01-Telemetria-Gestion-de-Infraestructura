#ifndef CONFIG_H
#define CONFIG_H

/* Ports (registered range 1024-49151) */
#define TCP_PORT   5000   /* operators + node registration (HELLO) */
#define UDP_PORT   5001   /* periodic telemetry                    */
#define HTTP_PORT  8080   /* web status page                       */

/* TELEP/2.0 limits: frame includes its LF terminator. */
#define MAX_LINE     1024  /* bytes per message, including '\n' */
#define MAX_ARGS     8
#define MAX_NODE_ID  16
#define MAX_VARS     8
#define MAX_VAR_NAME 8

/* Bounded in-memory registry; connection workers allocate separately. */
#define MAX_NODES       64
#define MAX_ALERTS      128  /* ring buffer */
#define ACTIVE_TIMEOUT  15   /* seconds without telemetry => node inactive */
#define MAX_SESSION 33
#define SESSION_LIMIT 65536
#define TCP_BACKLOG     16

/* Alert thresholds: value > threshold => <VAR>_HIGH */
#define TEMP_MAX   40.0
#define HUM_MAX    85.0
#define POWER_MAX  500.0
#define VIB_MAX    7.0

#endif
