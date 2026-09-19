#include "config.h"
#include <stdio.h>

/* Entry point. For now it only prints the configuration;
 * the UDP, TCP and HTTP services are added in the next steps. */
int main(void) {
    printf("TELEP/1.0 telemetry server\n");
    printf("  TCP commands : %d\n  UDP telemetry: %d\n  HTTP web     : %d\n", TCP_PORT, UDP_PORT, HTTP_PORT);
    return 0;
}
