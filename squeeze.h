#ifndef squeeze_header_included
#define squeeze_header_included

#include <errno.h>
#include <stdint.h>

typedef struct bitstream_struct bitstream_type;

typedef struct {
    errno_t error; // sticky
    bitstream_type* bs;
} squeeze_type;

typedef struct {
    // `window_bits` is a log2 of window size in bytes in range [10..20]
    void (*write_header)(squeeze_type* s, uint64_t bytes, uint8_t window_bits);
    void (*compress)(squeeze_type* s, const uint8_t* data, size_t bytes,
                     uint8_t window_bits);
    void (*read_header)(squeeze_type* s, uint64_t *bytes,
                        uint8_t *window_bits);
    void (*decompress)(squeeze_type* s, uint8_t* data, size_t bytes,
                       uint8_t window_bits);
    // Writing and reading envelope of source b64 `bytes` and
    // `window_bits` is caller's responsibility.
} squeeze_interface;

extern squeeze_interface sqz;

#endif // squeeze_header_included

#if defined(squeeze_implementation) && !defined(squeeze_implemented)

#define squeeze_implemented

#include "bitstream.h"

#ifndef null
#define null ((void*)0) // like null_ptr better than NULL (0)
#endif

#ifndef countof
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef assert
#include <assert.h>
#endif

#define squeeze_if_error_return(s);do { \
    if (s->error) { return; }       \
} while (0)

#define squeeze_return_invalid(s) do {  \
    s->error = EINVAL;              \
    return;                         \
} while (0)

static inline void squeeze_write_bit(squeeze_type* s, bool bit) {
    if (s->error == 0) {
        s->error = bitstream.write_bit(s->bs, bit);
    }
}

static inline void squeeze_write_bits(squeeze_type* s,
                                      uint64_t b64, uint8_t bits) {
    if (s->error == 0) {
        s->error = bitstream.write_bits(s->bs, b64, bits);
    }
}

static inline void squeeze_write_number(squeeze_type* s,
                                        uint64_t bits, uint8_t base) {
    while (s->error == 0 && bits != 0) {
        squeeze_write_bits(s, bits, base);
        bits >>= base;
        squeeze_write_bit(s, bits != 0); // continue bit
    }
}

static inline void squeeze_flush(squeeze_type* s) {
    if (s->error == 0) { s->error = bitstream.flush(s->bs); }
}

static void squeeze_write_header(squeeze_type* s, uint64_t bytes,
                                 uint8_t window_bits) {
    if (window_bits < 10 || window_bits > 20) { squeeze_return_invalid(s); }
    if (s->error == 0) {
        enum { bits64 = sizeof(uint64_t) * 8 };
        s->error = bitstream.write_bits(s->bs, (uint64_t)bytes, bits64);
    }
    if (s->error == 0) {
        enum { bits8 = sizeof(uint8_t) * 8 };
        s->error = bitstream.write_bits(s->bs, window_bits, bits8);
    }
}

static uint64_t pos_freq[64 * 1024];
static uint64_t len_freq[64 * 1024];
static map_entry_t word_entries[512 * 1024];
static map_entry_t pos_entries[512 * 1024];
static map_entry_t len_entries[512 * 1024];

static map_type word;  // word word
static map_type lens; // different bytes encountered
static map_type poss; // different pos encountered

static double squeeze_entropy(uint64_t freq[], int32_t n) {
    double total = 0;
    double aha_entropy = 0.0;
    for (int32_t i = 0; i < n; i++) { total += (double)freq[i]; }
    for (int32_t i = 0; i < n; i++) {
        if (freq[i] > 0) {
            double p_i = (double)freq[i] / total;
            aha_entropy += p_i * log2(p_i);
        }
    }
    return -aha_entropy;
}

static void squeeze_compress(squeeze_type* s, const uint8_t* data, uint64_t bytes,
        uint8_t window_bits) {
memset(pos_freq, 0x00, sizeof(pos_freq));
memset(len_freq, 0x00, sizeof(pos_freq));
map.init(&word, word_entries, countof(word_entries));
map.init(&lens, len_entries, countof(len_entries));
map.init(&poss, pos_entries, countof(pos_entries));
    squeeze_if_error_return(s);
    if (window_bits < 10 || window_bits > 20) { squeeze_return_invalid(s); }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    size_t i = 0;
    while (i < bytes) {
        // bytes and position of longest matching sequence
        size_t len = 0;
        size_t pos = 0;
        if (i >= 1) {
            size_t j = i - 1;
            size_t min_j = i > window ? i - window : 0;
            while (j > min_j) {
                assert((i - j) < window);
                const size_t n = bytes - i;
                size_t k = 0;
                while (k < n && data[j + k] == data[i + k]) {
                    k++;
                }
                if (k > len) {
                    len = k;
                    pos = i - j;
                }
                j--;
            }
        }
        if (len > 2) {
            assert(0 < pos && pos < window);
            assert(0 < len);
            squeeze_write_bits(s, 0b11, 2); // flags
            squeeze_if_error_return(s);
            squeeze_write_number(s, pos, base);
            squeeze_if_error_return(s);
            squeeze_write_number(s, len, base);
            squeeze_if_error_return(s);
if (len < window) { len_freq[len]++; }
if (pos < window) { pos_freq[pos]++; }
// printf("\"%.*s\"\n", bytes, &b64[i]);
if (len < countof(word.entry[0])) {
    map.put(&word, &data[i], (uint8_t)len);
}
map.put(&lens, (uint8_t*)&len, (uint8_t)sizeof(len));
map.put(&poss, (uint8_t*)&pos, (uint8_t)sizeof(pos));
            i += len;
        } else {
            const uint8_t b = data[i];
            // European texts are predominantly spaces and small ASCII letters:
            if (b < 0x80) {
                squeeze_write_bit(s, 0); // flags
                squeeze_if_error_return(s);
                // ASCII byte < 0x80 with 8th bit set to `0`
                squeeze_write_bits(s, b, 7);
                squeeze_if_error_return(s);
            } else {
                squeeze_write_bit(s, 1); // flag: 1
                squeeze_write_bit(s, 0); // flag: 0
                squeeze_if_error_return(s);
                // only 7 bit because 8th bit is `1`
                squeeze_write_bits(s, b, 7);
                squeeze_if_error_return(s);
            }
            i++;
        }
    }
    squeeze_flush(s);
    double len_bits = squeeze_entropy(len_freq, (int32_t)window);
    double pos_bits = squeeze_entropy(pos_freq, (int32_t)window);
    printf("bits len: %.2f pos: %.2f words: %d "
           "max chain: %d max bytes: %d #len: %d #pos: %d\n",
        len_bits, pos_bits, word.entries, word.max_chain, word.max_bytes,
        lens.entries, poss.entries);
}

static inline uint64_t squeeze_read_bit(squeeze_type* s) {
    bool bit = 0;
    if (s->error == 0) { s->error = bitstream.read_bit(s->bs, &bit); }
    return (uint64_t)bit;
}

static inline uint64_t squeeze_read_bits(squeeze_type* s, uint32_t n) {
    assert(n <= 64);
    uint64_t bits = 0;
    if (s->error == 0) { s->error = bitstream.read_bits(s->bs, &bits, n); }
    return bits;
}

static inline uint64_t squeeze_read_number(squeeze_type* s, uint8_t base) {
    uint64_t bits = 0;
    uint64_t bit = 0;
    uint32_t shift = 0;
    while (s->error == 0) {
        bits |= (squeeze_read_bits(s, base) << shift);
        shift += base;
        bit = squeeze_read_bit(s);
        if (!bit) { break; }
    }
    return bits;
}

static void squeeze_read_header(squeeze_type* s, uint64_t *bytes,
                                uint8_t *window_bits) {
    squeeze_if_error_return(s);
    *bytes = (size_t)squeeze_read_bits(s, sizeof(uint64_t) * 8);
    squeeze_if_error_return(s);
    *window_bits = (uint8_t)squeeze_read_bits(s, sizeof(uint8_t) * 8);
    squeeze_if_error_return(s);
    if (*window_bits < 10 || *window_bits > 20) { squeeze_return_invalid(s); }
}

static void squeeze_decompress(squeeze_type* s, uint8_t* data, uint64_t bytes,
        uint8_t window_bits) {
    squeeze_if_error_return(s);
    if (window_bits < 10 || window_bits > 20) { squeeze_return_invalid(s); }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    size_t i = 0; // output b64[i]
    while (i < bytes) {
        uint64_t bit0 = squeeze_read_bit(s);
        squeeze_if_error_return(s);
        if (bit0) {
            uint64_t bit1 = squeeze_read_bit(s);
            squeeze_if_error_return(s);
            if (bit1) {
                uint64_t pos = squeeze_read_number(s, base);
                squeeze_if_error_return(s);
                uint64_t len = squeeze_read_number(s, base);
                squeeze_if_error_return(s);
                assert(0 < pos && pos < window);
                if (!(0 < pos && pos < window)) { squeeze_return_invalid(s); }
                assert(0 < len);
                if (len == 0) { squeeze_return_invalid(s); }
                // Cannot do memcpy() here because of possible overlap.
                // memcpy() may read more than one byte at a time.
                uint8_t* d = data - (size_t)pos;
                const size_t n = i + (size_t)len;
                while (i < n) { data[i] = d[i]; i++; }
            } else { // byte >= 0x80
                uint64_t b = squeeze_read_bits(s, 7);
                squeeze_if_error_return(s);
                data[i] = (uint8_t)b | 0x80;
                i++;
            }
        } else { // literal byte
            uint64_t b = squeeze_read_bits(s, 7); // ASCII byte < 0x80
            squeeze_if_error_return(s);
            data[i] = (uint8_t)b;
            i++;
        }
    }
}

squeeze_interface sqz = {
    .write_header = squeeze_write_header,
    .compress     = squeeze_compress,
    .read_header  = squeeze_read_header,
    .decompress   = squeeze_decompress,
};

#endif // squeeze_implementation
