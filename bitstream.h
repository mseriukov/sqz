#pragma once
#ifndef bitstream_header_included
#define bitstream_header_included

#include <stdint.h>
#include <errno.h>

typedef struct {
    uint8_t* data;
    size_t   capacity;
    size_t   bytes; // number of bytes written
    size_t   read;  // number of bytes read
    uint64_t b64;   // bit shifting buffer
    int32_t  bits;  // bit count inside b64
    int32_t  padding;
} bitstream_type;

typedef struct {
    void    (*create)(bitstream_type* bs, void* data, size_t capacity);
    errno_t (*write_bit)(bitstream_type* bs, int32_t bit);
    errno_t (*read_bit)(bitstream_type* bs, bool *bit);
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
        for (int i = 0; i < 8 && r == 0; i++) {
            if (bs->bytes == bs->capacity) { r = E2BIG; }
            bs->data[bs->bytes++] = (bs->b64 >> ((7 - i) * 8)) & 0xFF;
        }
        bs->bits = 0;
        bs->b64 = 0;
    }
    return r;
}

static errno_t bitstream_read_bit(bitstream_type* bs, bool *bit) {
    errno_t r = 0;
    if (bs->bits == 0) {
        bs->b64 = 0;
        for (int i = 0; i < 8 && r == 0; i++) {
            if (bs->read == bs->bytes) {
                r = E2BIG;
            } else {
                const uint64_t byte = (bs->data[bs->read] & 0xFF);
                bs->b64 |= byte << ((7 - i) * 8);
                bs->read++;
            }
        }
        bs->bits = 64;
    }
    bool b = ((int64_t)bs->b64) < 0; // same as (bs->b64 >> 63) & 1;
    bs->b64 <<= 1;
    bs->bits--;
    *bit = (bool)b;
    return r;
}

static void bitstream_create(bitstream_type* bs, void* data, size_t capacity) {
    assert(bs->data != null);
    memset(bs, 0x00, sizeof(*bs));
    bs->data = (uint8_t*)data;
    bs->capacity  = capacity;
}

static void bitstream_dispose(bitstream_type* bs) {
    memset(bs, 0x00, sizeof(*bs));
}

bitstream_interface bitstream = {
    .create    = bitstream_create,
    .write_bit = bitstream_write_bit,
    .read_bit  = bitstream_read_bit,
    .dispose   = bitstream_dispose
};

#endif // bitstream_implementation