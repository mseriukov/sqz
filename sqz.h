#ifndef sqz_header_included
#define sqz_header_included

#include <errno.h>
#include <math.h>
#include <stdint.h>

// Naive LZ77 implementation inspired by CharGPT discussion
// and my personal passion to compressors in 198x

typedef struct sqz_s sqz_t;

typedef struct sqz_s {
    // `that` see: https://gist.github.com/leok7v/8d118985d3236b0069d419166f4111cf
    void*    that;  // caller supplied data
    errno_t  error; // sticky; for read()/write() compress() and decompress()
    // caller supplied read()/write() must error via .error field
    uint64_t (*read)(sqz_t*); //  reads 64 bits
    void     (*write)(sqz_t*, uint64_t b64); // writes 64 bits
    uint64_t written;
} sqz_t;

typedef struct sqz_if {
    // `window_bits` is a log2 of window size in bytes must be in range [10..20]
    void (*write_header)(sqz_t* sqz, size_t bytes, uint8_t window_bits);
    void (*compress)(sqz_t* sqz, const uint8_t* data, size_t bytes,
                     uint8_t window_bits);
    void (*read_header)(sqz_t* sqz, size_t *bytes, uint8_t *window_bits);
    void (*decompress)(sqz_t* sqz, uint8_t* data, size_t bytes,
                       uint8_t window_bits);
    // Writing and reading envelope of source data `bytes` and
    // `window_bits` is caller's responsibility.
} sqz_if;

extern sqz_if sqz;

#endif // sqz_header_included

#if defined(sqz_implementation) && !defined(sqz_implemented)

#define sqz_implemented

#ifndef null
#define null ((void*)0) // like null_ptr better than NULL (0)
#endif

#ifndef countof
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef assert
#define assert(...) do {} while (0)
#endif

#define sqz_if_error_return(lz);do {                    \
    if (lz->error) { return; }                          \
} while (0)

#define sqz_return_invalid(lz) do {                     \
    lz->error = EINVAL;                                 \
    return;                                             \
} while (0)

static inline uint32_t sqz_bit_count(size_t v) {
    uint32_t count = 0;
    while (v) { count++; v >>= 1; }
    return count;
}

static inline void sqz_write_bit(sqz_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bit) {
    if (*bp == 64 && lz->error == 0) {
        lz->write(lz, *b64);
        *b64 = 0;
        *bp = 0;
        if (lz->error == 0) { lz->written += 8; }
    }
    *b64 |= bit << *bp;
    (*bp)++;
}

static inline void sqz_write_bits(sqz_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bits, uint32_t n) {
    assert(n <= 64);
    while (n > 0) {
        sqz_write_bit(lz, b64, bp, bits & 1);
        bits >>= 1;
        n--;
    }
}

static inline void sqz_write_number(sqz_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bits, uint8_t base) {
    do {
        sqz_write_bits(lz, b64, bp, bits, base);
        bits >>= base;
        sqz_write_bit(lz, b64, bp, bits != 0); // continue bit
    } while (bits != 0);
}

static inline void sqz_flush(sqz_t* lz, uint64_t b64, uint32_t bp) {
    if (bp > 0 && lz->error == 0) {
        lz->write(lz, b64);
        if (lz->error == 0) { lz->written += 8; }
    }
}

static void sqz_write_header(sqz_t* lz, size_t bytes, uint8_t window_bits) {
    sqz_if_error_return(lz);
    if (window_bits < 10 || window_bits > 20) { sqz_return_invalid(lz); }
    lz->write(lz, (uint64_t)bytes);
    sqz_if_error_return(lz);
    lz->write(lz, (uint64_t)window_bits);
}

static uint64_t pos_freq[64 * 1024];
static uint64_t len_freq[64 * 1024];
static map_entry_t word_entries[512 * 1024];
static map_entry_t pos_entries[512 * 1024];
static map_entry_t len_entries[512 * 1024];

static map_t word;  // word word
static map_t lens; // different bytes encountered
static map_t poss; // different pos encountered

static double sqz_entropy(uint64_t freq[], int32_t n) {
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

static void sqz_compress(sqz_t* lz, const uint8_t* data, size_t bytes,
        uint8_t window_bits) {
memset(pos_freq, 0x00, sizeof(pos_freq));
memset(len_freq, 0x00, sizeof(pos_freq));
map.init(&word, word_entries, countof(word_entries));
map.init(&lens, len_entries, countof(len_entries));
map.init(&poss, pos_entries, countof(pos_entries));
    sqz_if_error_return(lz);
    if (window_bits < 10 || window_bits > 20) { sqz_return_invalid(lz); }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    uint64_t b64 = 0;
    uint32_t bp = 0;
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
            sqz_write_bits(lz, &b64, &bp, 0b11, 2); // flags
            sqz_if_error_return(lz);
            sqz_write_number(lz, &b64, &bp, pos, base);
            sqz_if_error_return(lz);
            sqz_write_number(lz, &b64, &bp, len, base);
            sqz_if_error_return(lz);
if (len < window) { len_freq[len]++; }
if (pos < window) { pos_freq[pos]++; }
// printf("\"%.*s\"\n", bytes, &data[i]);
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
                sqz_write_bit(lz, &b64, &bp, 0); // flags
                sqz_if_error_return(lz);
                // ASCII byte < 0x80 with 8th bit set to `0`
                sqz_write_bits(lz, &b64, &bp, b, 7);
                sqz_if_error_return(lz);
            } else {
                sqz_write_bit(lz, &b64, &bp, 1); // flag: 1
                sqz_write_bit(lz, &b64, &bp, 0); // flag: 0
                sqz_if_error_return(lz);
                // only 7 bit because 8th bit is `1`
                sqz_write_bits(lz, &b64, &bp, b, 7);
                sqz_if_error_return(lz);
            }
            i++;
        }
    }
    sqz_flush(lz, b64, bp);
    double len_bits = sqz_entropy(len_freq, (int32_t)window);
    double pos_bits = sqz_entropy(pos_freq, (int32_t)window);
    printf("bits len: %.2f pos: %.2f words: %d "
           "max chain: %d max bytes: %d #len: %d #pos: %d\n",
        len_bits, pos_bits, word.entries, word.max_chain, word.max_bytes,
        lens.entries, poss.entries);
}

static inline uint64_t sqz_read_bit(sqz_t* lz, uint64_t* b64, uint32_t* bp) {
    if (*bp == 0) { *b64 = lz->read(lz); }
    uint64_t bit = (*b64 >> *bp) & 1;
    *bp = *bp == 63 ? 0 : *bp + 1;
    return bit;
}

static inline uint64_t sqz_read_bits(sqz_t* lz, uint64_t* b64,
        uint32_t* bp, uint32_t n) {
    assert(n <= 64);
    uint64_t bits = 0;
    for (uint32_t i = 0; i < n && lz->error == 0; i++) {
        uint64_t bit = sqz_read_bit(lz, b64, bp);
        bits |= bit << i;
    }
    return bits;
}

static inline uint64_t sqz_read_number(sqz_t* lz, uint64_t* b64,
        uint32_t* bp, uint8_t base) {
    uint64_t bits = 0;
    uint64_t bit = 0;
    uint32_t shift = 0;
    do {
        bits |= (sqz_read_bits(lz, b64, bp, base) << shift);
        shift += base;
        bit = sqz_read_bit(lz, b64, bp);
    } while (bit && lz->error == 0);
    return bits;
}

static void sqz_read_header(sqz_t* lz, size_t *bytes, uint8_t *window_bits) {
    sqz_if_error_return(lz);
    *bytes = (size_t)lz->read(lz);
    *window_bits = (uint8_t)lz->read(lz);
    if (*window_bits < 10 || *window_bits > 20) { sqz_return_invalid(lz); }
}

static void sqz_decompress(sqz_t* lz, uint8_t* data, size_t bytes,
        uint8_t window_bits) {
    sqz_if_error_return(lz);
    uint64_t b64 = 0;
    uint32_t bp = 0;
    if (window_bits < 10 || window_bits > 20) { sqz_return_invalid(lz); }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    size_t i = 0; // output data[i]
    while (i < bytes) {
        uint64_t bit0 = sqz_read_bit(lz, &b64, &bp);
        sqz_if_error_return(lz);
        if (bit0) {
            uint64_t bit1 = sqz_read_bit(lz, &b64, &bp);
            sqz_if_error_return(lz);
            if (bit1) {
                uint64_t pos = sqz_read_number(lz, &b64, &bp, base);
                sqz_if_error_return(lz);
                uint64_t len = sqz_read_number(lz, &b64, &bp, base);
                sqz_if_error_return(lz);
                assert(0 < pos && pos < window);
                if (!(0 < pos && pos < window)) { sqz_return_invalid(lz); }
                assert(0 < len);
                if (len == 0) { sqz_return_invalid(lz); }
                // Cannot do memcpy() here because of possible overlap.
                // memcpy() may read more than one byte at a time.
                uint8_t* s = data - (size_t)pos;
                const size_t n = i + (size_t)len;
                while (i < n) { data[i] = s[i]; i++; }
            } else { // byte >= 0x80
                uint64_t b = sqz_read_bits(lz, &b64, &bp, 7);
                sqz_if_error_return(lz);
                data[i] = (uint8_t)b | 0x80;
                i++;
            }
        } else { // literal byte
            uint64_t b = sqz_read_bits(lz, &b64, &bp, 7); // ASCII byte < 0x80
            sqz_if_error_return(lz);
            data[i] = (uint8_t)b;
            i++;
        }
    }
}

sqz_if sqz = {
    .write_header = sqz_write_header,
    .compress     = sqz_compress,
    .read_header  = sqz_read_header,
    .decompress   = sqz_decompress,
};

#endif // sqz_implementation
