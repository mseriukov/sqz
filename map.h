#ifndef map_header_included
#define map_header_included

#include <stdint.h>

typedef uint8_t map_entry_t[256]; // data[0] number of bytes [2..255]

typedef struct {
    map_entry_t* entry;
    int32_t n; // entry[n]
    int32_t entries;
    int32_t max_chain;
    int32_t max_bytes;
} map_t;

typedef struct {
    void (*init)(map_t* m, map_entry_t entry[], int32_t n);
    const void* (*get)(const map_t* m, const void* data, uint8_t bytes);
    void (*put)(map_t* m, const void* data, uint8_t bytes);
    void (*clear)(map_t *m);
} map_if;

extern map_if map;

#endif // map_header_included

#if defined(map_implementation) && !defined(map_implemented)

#define map_implemented

#include <string.h>
#ifndef swear
#include <assert.h>
#define swear(...) assert(__VA_ARGS__)
#endif
#ifndef null
#define null ((void*)0)
#endif

static uint64_t map_hash64(const uint8_t* data, int64_t bytes) {
    uint64_t hash  = 0xcbf29ce484222325; // FNV_offset_basis for 64-bit
    uint64_t prime = 0x100000001b3;      // FNV_prime for 64-bit
    swear(bytes > 0);
    for (int64_t i = 0; i < bytes; i++) {
        hash ^= (uint64_t)data[i];
        hash *= prime;
    }
    return hash;
}

static void map_init(map_t* m, map_entry_t entry[], int32_t n) {
    swear(16 < n && n < 1024 * 1024);
    m->n = n;
    m->entry = entry;
    for (int32_t i = 0; i < m->n; i++) { m->entry[i][0] = 0; }
    m->entries = 0;
    m->max_chain = 0;
    m->max_bytes = 0;
}

static const uint8_t* map_get(const map_t* m, const uint8_t* data, uint8_t bytes) {
    uint64_t hash = map_hash64(data, bytes);
    size_t i = (size_t)hash % m->n;
    while (m->entry[i][0] > 0) {
        if (m->entry[i][0] == bytes && memcmp(&m->entry[i][1], data, bytes) == 0) {
            return &m->entry[i][1];
        }
        i = (i + 1) % m->n;
    }
    return null;
}

static void map_put(map_t* m, const uint8_t* data, uint8_t bytes) {
    swear(2 <= bytes && bytes < sizeof(m->entry[0]));
    swear(m->entries < m->n * 3 / 4);
    uint64_t hash = map_hash64(data, bytes);
    size_t i = (size_t)hash % m->n;
    int32_t rehash = 0;
    while (m->entry[i][0] > 0) {
        if (m->entry[i][0] == bytes &&
            memcmp(&m->entry[i][1], data, bytes) == 0) {
            return; // already exists
        }
        rehash++;
        i = (i + 1) % m->n;
    }
    if (rehash > m->max_chain) { m->max_chain = rehash; }
    if (bytes  > m->max_bytes) { m->max_bytes = bytes; }
    m->entry[i][0] = bytes;
    memcpy(&m->entry[i][1], data, bytes);
    m->entries++;
}

static void map_clear(map_t *m) {
    for (int32_t i = 0; i < m->n; i++) {
        m->entry[i][0] = 0;
    }
    m->entries = 0;
    m->max_chain = 0;
}

map_if map = {
    .init  = map_init,
    .get   = map_get,
    .put   = map_put,
    .clear = map_clear
};

#endif // map_implementation

