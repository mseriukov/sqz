#ifndef bitstream_header_included
#define bitstream_header_included

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct bitstream_struct {
    FILE*    file; // file and (data,capacity) is exclusive
    uint8_t* data;
    uint64_t capacity; // data[capacity]
    uint64_t bytes; // number of bytes written
    uint64_t read;  // number of bytes read
    uint64_t b64;   // bit shifting buffer
    int32_t  bits;  // bit count inside b64
    int32_t  padding;
} bitstream_type;

typedef struct {
    void    (*create)(bitstream_type* bs, void* data, size_t capacity);
    errno_t (*write_bit)(bitstream_type* bs, int32_t bit);
    errno_t (*write_bits)(bitstream_type* bs, uint64_t data, int32_t bits);
    errno_t (*read_bit)(bitstream_type* bs, bool *bit);
    errno_t (*read_bits)(bitstream_type* bs, uint64_t *data, int32_t bits);
    errno_t (*flush)(bitstream_type* bs); // write trailing zeros
    void    (*dispose)(bitstream_type* bs);
} bitstream_interface;

extern bitstream_interface bitstream;

#endif // bitstream_header_included

#if defined(bitstream_implementation) && !defined(bitstream_implemented)

#define bitstream_implemented

static errno_t bitstream_write_bit(bitstream_type* bs, int32_t bit) {
    errno_t r = 0;
    bs->b64 <<= 1;
    bs->b64 |= (bit & 1);
    bs->bits++;
    if (bs->bits == 64) {
        if (bs->data != null && bs->capacity > 0) {
            assert(bs->file == null);
            for (int i = 0; i < 8 && r == 0; i++) {
                if (bs->bytes == bs->capacity) {
                    r = E2BIG;
                } else {
                    bs->data[bs->bytes++] = (bs->b64 >> ((7 - i) * 8)) & 0xFF;
                }
            }
        } else {
            assert(bs->data == null && bs->capacity == 0);
            size_t written = fwrite(&bs->b64, 1, 8, bs->file);
            r = written == 8 ? 0 : errno;
            if (r == 0) { bs->bytes += 8; }
        }
        bs->bits = 0;
        bs->b64 = 0;
    }
    return r;
}

static errno_t bitstream_write_bits(bitstream_type* bs, uint64_t data, int32_t bits) {
    assert(0 < bits && bits <= 64);
    errno_t r = 0;
    while (bits > 0 && r == 0) {
        r = bitstream_write_bit(bs, data & 1);
        bits--;
        data >>= 1;
    }
    return r;
}

static errno_t bitstream_read_bit(bitstream_type* bs, bool *bit) {
    errno_t r = 0;
    if (bs->bits == 0) {
        bs->b64 = 0;
        if (bs->data != null && bs->bytes > 0) {
            assert(bs->file == null);
            for (int i = 0; i < 8 && r == 0; i++) {
                if (bs->read == bs->bytes) {
                    r = E2BIG;
                } else {
                    const uint64_t byte = (bs->data[bs->read] & 0xFF);
                    bs->b64 |= byte << ((7 - i) * 8);
                    bs->read++;
                }
            }
        } else {
            assert(bs->data == null && bs->bytes == 0);
            size_t read = fread(&bs->b64, 1, 8, bs->file);
            r = read == 8 ? 0 : errno;
            if (r == 0) { bs->read += 8; }
        }
        bs->bits = 64;
    }
    bool b = ((int64_t)bs->b64) < 0; // same as (bs->b64 >> 63) & 1;
    bs->b64 <<= 1;
    bs->bits--;
    *bit = (bool)b;
    return r;
}

static errno_t bitstream_read_bits(bitstream_type* bs, uint64_t *data, int32_t bits) {
    assert(0 < bits && bits <= 64);
    errno_t r = 0;
    bool bit = 0;
    uint64_t d = 0;
    for (int32_t b = 0; b < bits && r == 0; b++) {
        r = bitstream_read_bit(bs, &bit);
        if (bit) { d |= ((uint64_t)bit) << b; }
    }
    if (r == 0) { *data = d; }
    return r;
}

static void bitstream_create(bitstream_type* bs, void* data, size_t capacity) {
    assert(bs->data != null);
    memset(bs, 0x00, sizeof(*bs));
    bs->data = (uint8_t*)data;
    bs->capacity  = capacity;
}

static errno_t bitstream_flush(bitstream_type* bs) {
    errno_t r = 0;
    while (bs->bits > 0 && r == 0) { r = bitstream_write_bit(bs, 0); }
    return r;
}

static void bitstream_dispose(bitstream_type* bs) {
    memset(bs, 0x00, sizeof(*bs));
}

bitstream_interface bitstream = {
    .create     = bitstream_create,
    .write_bit  = bitstream_write_bit,
    .write_bits = bitstream_write_bits,
    .read_bit   = bitstream_read_bit,
    .read_bits  = bitstream_read_bits,
    .flush      = bitstream_flush,
    .dispose    = bitstream_dispose
};

#endif // bitstream_implementation