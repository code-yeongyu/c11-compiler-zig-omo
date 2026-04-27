#ifndef HTTP2_HPACK_STATIC_H
#define HTTP2_HPACK_STATIC_H

#include <stdint.h>

#define H2_HPACK_STATIC_TABLE_LEN 61u

typedef struct h2_hpack_static_entry {
    const char *name;
    const char *value;
} h2_hpack_static_entry;

const h2_hpack_static_entry *h2_hpack_static_get(uint32_t index);

#endif
