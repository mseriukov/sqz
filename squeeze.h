#ifndef squeeze_header_included
#define squeeze_header_included

#include <errno.h>
#include <stdint.h>

#include "bitstream.h"
#include "huffman.h"
#include "map.h"

const uint8_t* input_data; // TODO: remove

enum {
    squeeze_min_win_bits = 10,
    squeeze_max_win_bits = 20,
    squeeze_min_map_bits =  8,
    squeeze_max_map_bits = 20,
    squeeze_min_len_bits =  4,
    squeeze_max_len_bits =  8
};

typedef struct {
    errno_t error; // sticky
    map_type map;  // `words` dictionary
    map_entry_t* map_entries;
    huffman_tree_type dic; // `map` keys tree
    huffman_tree_type sym; // 0..255 ASCII characters
    huffman_tree_type pos; // positions tree of 1^win_bits
    huffman_tree_type len; // length [2..255] tree
    huffman_node_type* dic_nodes;
    huffman_node_type* sym_nodes;
    huffman_node_type* pos_nodes;
    huffman_node_type* len_nodes;
    bitstream_type*    bs;
} squeeze_type;

#if 0

#define squeeze_size_mul(name, count) (                                       \
    ((uint64_t)(count) >= ((SIZE_MAX / 4) / (uint64_t)sizeof(name))) ?        \
    0 : (size_t)((uint64_t)sizeof(name) * (uint64_t)(count))                  \
)

#define squeeze_size_implementation(win_bits, map_bits, len_bits) (             \
    (sizeof(squeeze_type)) +                                                    \
    squeeze_size_mul(map_entry_t, (1ULL << (map_bits))) +                       \
    /* dic_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, ((1ULL << (map_bits)) * 2ULL - 1ULL)) + \
    /* sym_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, (256ULL * 2ULL - 1ULL)) +               \
    /* pos_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, ((1ULL << (win_bits)) * 2ULL - 1ULL)) + \
    /* len_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, ((1ULL << (len_bits) * 2ULL - 1ULL))    \
)

#define squeeze_sizeof(win_bits, map_bits, len_bits) (                          \
    (sizeof(size_t) == sizeof(uint64_t)) &&                                     \
    (squeeze_min_win_bits <= (win_bits)) &&                                     \
                             ((win_bits) <= squeeze_max_win_bits) &&            \
    (squeeze_min_map_bits <= (map_bits)) &&                                     \
                            ((map_bits) <= squeeze_max_map_bits) &&             \
    (squeeze_min_len_bits <= (len_bits)) &&                                     \
                            ((len_bits) <= squeeze_max_len_bits))) ?            \
    (size_t)squeeze_size_implementation((win_bits), (map_bits), (len_bits)) : 0 \
)
#endif

#define squeeze_size_mul(name, count) (                                       \
    ((uint64_t)(count) >= ((SIZE_MAX / 4) / (uint64_t)sizeof(name))) ?        \
    0 : (size_t)((uint64_t)sizeof(name) * (uint64_t)(count))                  \
)

#define squeeze_size_implementation(win_bits, map_bits, len_bits) (             \
    (sizeof(squeeze_type)) +                                                    \
    squeeze_size_mul(map_entry_t, (1ULL << (map_bits))) +                       \
    /* dic_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, ((1ULL << (map_bits)) * 2ULL - 1ULL)) + \
    /* sym_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, (256ULL * 2ULL - 1ULL)) +               \
    /* pos_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, ((1ULL << (win_bits)) * 2ULL - 1ULL)) + \
    /* len_nodes: */                                                            \
    squeeze_size_mul(huffman_node_type, ((1ULL << (len_bits)) * 2ULL - 1ULL))   \
)

#define squeeze_sizeof(win_bits, map_bits, len_bits) (                          \
    (sizeof(size_t) == sizeof(uint64_t)) &&                                     \
    (squeeze_min_win_bits <= (win_bits)) &&                                     \
                             ((win_bits) <= squeeze_max_win_bits) &&            \
    (squeeze_min_map_bits <= (map_bits)) &&                                     \
                            ((map_bits) <= squeeze_max_map_bits) &&             \
    (squeeze_min_len_bits <= (len_bits)) &&                                     \
                            ((len_bits) <= squeeze_max_len_bits) ?              \
    (size_t)squeeze_size_implementation((win_bits), (map_bits), (len_bits)) : 0 \
)

typedef struct {
    // `win_bits` is a log2 of window size in bytes in range
    // [squeeze_min_win_bits..squeeze_max_win_bits]
    void (*write_header)(bitstream_type* bs, uint64_t bytes,
                         uint8_t win_bits, uint8_t map_bits, uint8_t len_bits);
    void (*compress)(squeeze_type* s, const uint8_t* data, size_t bytes);
    void (*read_header)(bitstream_type* bs, uint64_t *bytes,
                        uint8_t *win_bits, uint8_t *map_bits, uint8_t *len_bits);
    void (*decompress)(squeeze_type* s, uint8_t* data, size_t bytes);
} squeeze_interface;

extern squeeze_interface squeeze;

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

#define squeeze_if_error_return(s) do { \
    if (s->error) { return; }           \
} while (0)

#define squeeze_return_invalid(s) do {  \
    s->error = EINVAL;                  \
    return;                             \
} while (0)

static inline void squeeze_write_bit(squeeze_type* s, bool bit) {
    if (s->error == 0) {
        bitstream.write_bit(s->bs, bit);
        s->error = s->bs->error;
    }
}

static inline void squeeze_write_bits(squeeze_type* s,
                                      uint64_t b64, uint8_t bits) {
    if (s->error == 0) {
        bitstream.write_bits(s->bs, b64, bits);
        s->error = s->bs->error;
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

static inline void squeeze_write_huffman(squeeze_type* s, huffman_tree_type* t,
                                         int32_t i) {
    assert(t != null && t->node != null);
    assert(0 <= i && i < t->n); // leaf symbol
    assert(1 <= t->node[i].bits && t->node[i].bits < 64);
    squeeze_write_bits(s, t->node[i].path, (uint8_t)t->node[i].bits);
    huffman.inc_frequency(t, i); // after the path is written
}

static inline void squeeze_flush(squeeze_type* s) {
    if (s->error == 0) {
        bitstream.flush(s->bs);
        s->error = s->bs->error;
    }
}

static void squeeze_write_header(bitstream_type* bs, uint64_t bytes,
                                 uint8_t win_bits, uint8_t map_bits,
                                 uint8_t len_bits) {
    if (win_bits < squeeze_min_win_bits || win_bits > squeeze_max_win_bits ||
        map_bits < squeeze_min_map_bits || map_bits > squeeze_max_map_bits ||
        len_bits < squeeze_min_len_bits || len_bits > squeeze_max_len_bits) {
        bs->error = EINVAL;
    } else {
        enum { bits64 = sizeof(uint64_t) * 8 };
        bitstream.write_bits(bs, (uint64_t)bytes, bits64);
        enum { bits8 = sizeof(uint8_t) * 8 };
        bitstream.write_bits(bs, win_bits, bits8);
        bitstream.write_bits(bs, map_bits, bits8);
        bitstream.write_bits(bs, len_bits, bits8);
    }
}

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

static void squeeze_add_to_dictionary(squeeze_type* s, const uint8_t* word,
                                      uint64_t bytes) {
    size_t word_bytes = bytes < countof(s->map_entries[0]) - 1 ?
                        bytes : countof(s->map_entries[0]) - 1;
    assert(word_bytes <= 0xFF);
    int32_t wix = map.put(&s->map, word, (uint8_t)word_bytes);
    if (wix >= 0) {
        huffman.inc_frequency(&s->dic, wix);
if (s->map.entries <= 0) {
        printf("[%d] ", wix);
        if (bytes < 64) {
            bool ascii = true;
            for (int i = 0; i < (int)bytes && ascii; i++) {
                ascii &= ((0x20 <= word[i] && word[i] <= 0x7F) ||
                          (word[i] == '\t' || word[i] == '\r' || word[i] == '\n'));
            }
            if (ascii) {
                printf(" \"");
                for (int i = 0; i < (int)bytes; i++) { printf("%c", 0x20 <= word[i] && word[i] <= 0x7F ? word[i] : '?'); }
                printf("\"");
            } else {
                for (int i = 0; i < (int)bytes; i++) { printf("%02X", word[i]); }
            }
        } else {
        }
        printf(" bytes: %lld\n", bytes);
}
    }
}

static int write_count;

static void squeeze_compress(squeeze_type* s, const uint8_t* data, uint64_t bytes) {
    squeeze_if_error_return(s);
    const uint8_t win_bits = huffman.log2_of_pow2(s->pos.n);
    const uint8_t len_bits = huffman.log2_of_pow2(s->len.n);
    if (win_bits < 10 || win_bits > 20) { squeeze_return_invalid(s); }
    const size_t window = ((size_t)1U) << win_bits;
    const uint8_t base = (win_bits - 4) / 2;
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
            squeeze_write_bits(s, 0b11, 2); // flags
            squeeze_if_error_return(s);
//          squeeze_write_number(s, len, base);
            if (len < (1ULL << len_bits)) {
                squeeze_write_huffman(s, &s->len, (int32_t)len);
            } else {
                squeeze_write_huffman(s, &s->len, 0);
                squeeze_if_error_return(s);
                squeeze_write_number(s, len, base);
            }
            squeeze_if_error_return(s);
//          squeeze_write_number(s, pos, base);
            squeeze_write_huffman(s, &s->pos, (int32_t)pos);
            squeeze_if_error_return(s);
            squeeze_add_to_dictionary(s, &data[i], len);
            i += len;
        } else {
            int32_t best = map.best(&s->map, &data[i], bytes - i);
//best = -1;
            if (best >= 0) {
                assert(best < INT32_MAX);
                assert(map.bytes(&s->map, best) >= 3);
//              printf("best[%d] bytes:%d bits:%d\n",
//                  best, map.bytes(&s->map, best), s->dic.node[best].bits);
                squeeze_write_bits(s, 0b11, 2); // flags
                squeeze_if_error_return(s);
                // len == 1 indicates that it's a dictionary word
                squeeze_write_huffman(s, &s->len, 1);
                squeeze_if_error_return(s);
if (write_count < 0) {
                printf("best[%d] bytes:%d bits:%d map.entries:%d\n",
                    best, map.bytes(&s->map, best), s->dic.node[best].bits, s->map.entries);
                for (int j = 0; j < s->dic.node[best].bits; j++) {
                    printf("%d", (s->dic.node[best].path & (1ULL << j)) != 0);
                }
                printf("\n");
}
write_count++;
                assert(s->dic.node[best].bits <= 0xFF);
                squeeze_write_huffman(s, &s->dic, (int32_t)best);
                squeeze_if_error_return(s);
                i += map.bytes(&s->map, best);
            } else {
                const uint8_t b = data[i];
                // European texts are predominantly spaces and small ASCII letters:
                if (b < 0x80) {
                    squeeze_write_bit(s, 0); // flags
                    squeeze_if_error_return(s);
                    // ASCII byte < 0x80 with 8th bit set to `0`
//                  squeeze_write_bits(s, b, 7);
                    squeeze_write_huffman(s, &s->sym, b);
                    squeeze_if_error_return(s);
                } else {
                    squeeze_write_bit(s, 1); // flag: 1
                    squeeze_write_bit(s, 0); // flag: 0
                    squeeze_if_error_return(s);
                    // only 7 bit because 8th bit is `1`
//                  squeeze_write_bits(s, b, 7);
                    squeeze_write_huffman(s, &s->sym, b);
                    squeeze_if_error_return(s);
                }
                i++;
            }
        }
    }
    squeeze_flush(s);
    printf("dic: %d\n", write_count);
//  double len_bits = squeeze_entropy(len_freq, (int32_t)window);
//  double pos_bits = squeeze_entropy(pos_freq, (int32_t)window);
//  printf("bits len: %.2f pos: %.2f words: %d "
//         "max chain: %d max bytes: %d #len: %d #pos: %d\n",
//      len_bits, pos_bits, word.entries, word.max_chain, word.max_bytes,
//      lens.entries, poss.entries);
#if 0
    printf("words: %d max chain: %d max bytes: %d\n",
        s->map.entries, s->map.max_chain, s->map.max_bytes);
#endif
}

static inline uint64_t squeeze_read_bit(squeeze_type* s) {
    bool bit = 0;
    if (s->error == 0) {
        bit = bitstream.read_bit(s->bs);
        s->error = s->bs->error;
    }
    return bit;
}

static inline uint64_t squeeze_read_bits(squeeze_type* s, uint32_t n) {
    assert(n <= 64);
    uint64_t bits = 0;
    if (s->error == 0) {
        bits = bitstream.read_bits(s->bs, n);
    }
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

static inline uint64_t squeeze_read_huffman(squeeze_type* s, huffman_tree_type* t) {
    const int32_t m = t->n * 2 - 1;
    int32_t i = m - 1; // root
    bool bit = squeeze_read_bit(s);
    while (s->error == 0) {
        i = bit ? t->node[i].rix : t->node[i].lix;
        assert(0 <= i && i < m);
        if (t->node[i].lix < 0 && t->node[i].rix < 0) { break; } // leaf
        bit = squeeze_read_bit(s);
    }
    assert(0 <= i && i < t->n); // leaf symbol
    huffman.inc_frequency(t, i);
    return (uint64_t)i;
}

static void squeeze_read_header(bitstream_type* bs, uint64_t *bytes,
                                uint8_t *win_bits, uint8_t *map_bits,
                                uint8_t *len_bits) {
    uint64_t b  = bitstream.read_bits(bs, sizeof(uint64_t) * 8);
    uint64_t wb = bitstream.read_bits(bs, sizeof(uint8_t) * 8);
    uint64_t mb = bitstream.read_bits(bs, sizeof(uint8_t) * 8);
    uint64_t lb = bitstream.read_bits(bs, sizeof(uint8_t) * 8);
    if (bs->error == 0) {
        if (wb < squeeze_min_win_bits || wb > squeeze_max_win_bits) {
            bs->error = EINVAL;
        } else if (mb < squeeze_min_map_bits || mb > squeeze_max_map_bits) {
            bs->error = EINVAL;
        } else if (lb < 4 || lb > 8) {
            bs->error = EINVAL;
        } else if (bs->error == 0) {
            *bytes = b;
            *win_bits = (uint8_t)wb;
            *map_bits = (uint8_t)mb;
            *len_bits = (uint8_t)lb;
        }
    }
}

static void squeeze_decompress(squeeze_type* s, uint8_t* data, uint64_t bytes) {
    squeeze_if_error_return(s);
    const uint8_t win_bits = huffman.log2_of_pow2(s->pos.n);
    if (win_bits < 10 || win_bits > 20) { squeeze_return_invalid(s); }
    const size_t window = ((size_t)1U) << win_bits;
    const uint8_t base = (win_bits - 4) / 2;
    size_t i = 0; // output b64[i]
    while (i < bytes) {
        uint64_t bit0 = squeeze_read_bit(s);
        squeeze_if_error_return(s);
        if (bit0) {
            uint64_t bit1 = squeeze_read_bit(s);
            squeeze_if_error_return(s);
            if (bit1) {
//              uint64_t len = squeeze_read_number(s, base);
                uint64_t len = squeeze_read_huffman(s, &s->len);
                squeeze_if_error_return(s);
                if (len == 1) {
                    uint64_t wix = squeeze_read_huffman(s, &s->dic);
                    squeeze_if_error_return(s);
static int read_count;
if (read_count < 4) {
    printf("[%d] bytes: %d map.entries: %d\n", wix, map.bytes(&s->map, (int32_t)wix), s->map.entries);
    for (int j = 0; j < s->dic.node[wix].bits; j++) {
        printf("%d", (s->dic.node[wix].path & (1ULL << j)) != 0);
    }
    printf("\n");
}
read_count++;
                    assert(wix < (uint64_t)s->map.n);
if (map.bytes(&s->map, (int32_t)wix) < 3) {
    printf("[%d] bytes: %d map.entries: %d\n", wix, map.bytes(&s->map, (int32_t)wix), s->map.entries);
    for (int j = 0; j < s->dic.node[wix].bits; j++) {
        printf("%d", (s->dic.node[wix].path & (1ULL << j)) != 0);
    }
    printf("\n");
}
                    assert(map.bytes(&s->map, (int32_t)wix) >= 3);
                    size_t n = map.bytes(&s->map, (int32_t)wix);
                    assert(i + n <= bytes);
                    const uint8_t* d = (const uint8_t*)map.data(&s->map, (int32_t)wix);
                    for (size_t j = 0; j < n; j++) {
                        data[i] = d[j];
                        assert(input_data[i] == data[i]);
                        i++;
                    }
                } else {
                    if (len == 0) { len = squeeze_read_number(s, base); }
    //              uint64_t pos = squeeze_read_number(s, base);
                    uint64_t pos = squeeze_read_huffman(s, &s->pos);
                    squeeze_if_error_return(s);
                    assert(0 < pos && pos < window);
                    if (!(0 < pos && pos < window)) { squeeze_return_invalid(s); }
                    assert(2 <= len);
                    if (len < 2) { squeeze_return_invalid(s); }
                    // Cannot do memcpy() here because of possible overlap.
                    // memcpy() may read more than one byte at a time.
                    uint8_t* d = data - (size_t)pos;
                    const size_t n = i + (size_t)len;
                    uint8_t* w = d + i;
                    while (i < n) { data[i] = d[i]; assert(input_data[i] == data[i]); i++; }
                    squeeze_add_to_dictionary(s, w, len);
                }
            } else { // byte >= 0x80
                uint64_t b = squeeze_read_huffman(s, &s->sym);
//              uint64_t b = squeeze_read_bits(s, 7);
                squeeze_if_error_return(s);
                data[i] = (uint8_t)b | 0x80;
assert(input_data[i] == data[i]);
                i++;
            }
        } else { // literal byte (ASCII byte < 0x80)
//          uint64_t b = squeeze_read_bits(s, 7);
            uint64_t b = squeeze_read_huffman(s, &s->sym);
            squeeze_if_error_return(s);
            data[i] = (uint8_t)b;
assert(input_data[i] == data[i]);
            i++;
        }
    }
}

squeeze_interface squeeze = {
    .write_header = squeeze_write_header,
    .compress     = squeeze_compress,
    .read_header  = squeeze_read_header,
    .decompress   = squeeze_decompress,
};

#endif // squeeze_implementation
