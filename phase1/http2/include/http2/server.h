#ifndef HTTP2_SERVER_H
#define HTTP2_SERVER_H

#include <stdint.h>

typedef struct h2_server_config {
    const char *host;
    uint16_t port;
    const char *ready_file;
    unsigned max_connections;
} h2_server_config;

int h2_server_run(const h2_server_config *config);

#endif
