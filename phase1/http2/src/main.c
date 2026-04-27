#include "http2/server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void h2_usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--host 127.0.0.1] [--port 8080] [--ready-file path] [--max-connections n]\n", argv0);
}

static int h2_parse_u16(const char *text, uint16_t *out)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > 65535ul) {
        return -1;
    }
    *out = (uint16_t)value;
    return 0;
}

static int h2_parse_unsigned(const char *text, unsigned *out)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }
    *out = (unsigned)value;
    return 0;
}

int main(int argc, char **argv)
{
    h2_server_config config;
    int pos;

    config.host = "127.0.0.1";
    config.port = 8080u;
    config.ready_file = NULL;
    config.max_connections = 0u;
    pos = 1;
    while (pos < argc) {
        if (strcmp(argv[pos], "--host") == 0 && pos + 1 < argc) {
            config.host = argv[pos + 1];
            pos += 2;
        } else if (strcmp(argv[pos], "--port") == 0 && pos + 1 < argc) {
            if (h2_parse_u16(argv[pos + 1], &config.port) != 0) {
                h2_usage(argv[0]);
                return 2;
            }
            pos += 2;
        } else if (strcmp(argv[pos], "--ready-file") == 0 && pos + 1 < argc) {
            config.ready_file = argv[pos + 1];
            pos += 2;
        } else if (strcmp(argv[pos], "--max-connections") == 0 && pos + 1 < argc) {
            if (h2_parse_unsigned(argv[pos + 1], &config.max_connections) != 0) {
                h2_usage(argv[0]);
                return 2;
            }
            pos += 2;
        } else if (strcmp(argv[pos], "--help") == 0) {
            h2_usage(argv[0]);
            return 0;
        } else {
            h2_usage(argv[0]);
            return 2;
        }
    }
    if (h2_server_run(&config) != 0) {
        perror("h2d");
        return 1;
    }
    return 0;
}
