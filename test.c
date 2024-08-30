#include "rt.h"

#undef assert
#undef swear
#undef countof
#undef min
#undef max
#undef swap

#define assert(b, ...) rt_assert(b, __VA_ARGS__)
#define swear(b, ...)  rt_swear(b, __VA_ARGS__) // release build assert
#define printf(...)    rt_printf(__VA_ARGS__)
#define println(...)   rt_println(__VA_ARGS__)  // same as printf("...\n", ...)
#define countof(a)     rt_countof(a)
#define min(x, y)      rt_min(x, y)
#define max(x, y)      rt_max(x, y)
#define swap(a, b)     rt_swap(a, b)

#include "bitstream.h"
#include "map.h"
#include "squeeze.h"
#include "file.h"

enum { window_bits = 11 };

static errno_t compress(const char* from, const char* to,
                        const uint8_t* data, uint64_t bytes) {
    FILE* out = null; // compressed file
    errno_t r = fopen_s(&out, to, "wb") != 0;
    if (r != 0 || out == null) {
        printf("Failed to create \"%s\": %s\n", to, strerror(r));
        return r;
    }
    bitstream_type bs = { .file = out };
    squeeze_type s = { .bs = &bs };
    sqz.write_header(&s, bytes, window_bits);
    sqz.compress(&s, data, bytes, window_bits);
    assert(s.error == 0);
    r = fclose(out) == 0 ? 0 : errno; // error writing buffered output
    if (r != 0) {
        printf("Failed to flush on file close: %s\n", strerror(r));
    } else {
        r = s.error;
        if (r != 0) {
            printf("Failed to compress: %s\n", strerror(r));
        } else {
            const uint64_t written = s.bs->bytes;
            double percent = written * 100.0 / bytes;
            if (from != null) {
                printf("%7lld -> %7lld %5.1f%% of \"%s\"\n", bytes, written,
                                                             percent, from);
            } else {
                printf("%7lld -> %7lld %5.1f%%\n", bytes, written, percent);
            }
        }
    }
    return r;
}

static errno_t verify(const char* fn, const uint8_t* input, size_t size) {
    // decompress and compare
    FILE* in = null; // compressed file
    errno_t r = fopen_s(&in, fn, "rb");
    if (r != 0 || in == null) {
        printf("Failed to open \"%s\"\n", fn);
        return r;
    }
    bitstream_type bs = { .file = in };
    squeeze_type s = { .bs = &bs };
    uint64_t bytes = 0;
    uint8_t window_bits = 0;
    sqz.read_header(&s, &bytes, &window_bits);
    assert(s.error == 0 && bytes == size && window_bits == window_bits);
    uint8_t* data = (uint8_t*)malloc((size_t)bytes);
    if (data == null) {
        printf("Failed to allocate memory for decompressed data\n");
        fclose(in);
        return ENOMEM;
    }
    sqz.decompress(&s, data, bytes, window_bits);
    fclose(in);
    assert(s.error == 0);
    if (s.error == 0) {
        const bool same = size == bytes && memcmp(input, data, bytes) == 0;
        assert(same);
        if (!same) {
            printf("compress() and decompress() are not the same\n");
            // ENODATA is not original posix error but is OpenGroup error
            r = ENODATA; // or EIO
        } else if (bytes < 128) {
            printf("decompressed: %.*s\n", (unsigned int)bytes, data);
        }
    }
    free(data);
    if (r != 0) {
        printf("Failed to decompress\n");
    }
    return r;
}

const char* compressed = "~compressed~.bin";

static errno_t test(const char* fn, const uint8_t* data, size_t bytes) {
    errno_t r = compress(fn, compressed, data, bytes);
    if (r == 0) {
        r = verify(compressed, data, bytes);
    }
    (void)remove(compressed);
    return r;
}

static errno_t test_compression(const char* fn) {
    uint8_t* data = null;
    size_t bytes = 0;
    errno_t r = file.read_fully(fn, &data, &bytes);
    if (r != 0) { return r; }
    return test(fn, data, bytes);
}

static errno_t locate_test_folder(void) {
    // on Unix systems with "make" executable usually resided
    // and is run from root of repository... On Windows with
    // MSVC it is buried inside bin/... folder depths
    // on X Code in MacOS it can be completely out of tree.
    // So we need to find the test files.
    for (;;) {
        if (file.exist("test/bible.txt")) { return 0; }
        if (file.chdir("..") != 0) { return errno; }
    }
}

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv; // unused
    errno_t r = locate_test_folder();
    if (r == 0) {
        uint8_t data[4 * 1024] = {0};
        r = test(null, data, sizeof(data));
        // lz77 deals with run length encoding in amazing overlapped way
        for (int32_t i = 0; i < sizeof(data); i += 4) {
            memcpy(data + i, "\x01\x02\x03\x04", 4);
        }
        r = test(null, data, sizeof(data));
    }
    if (r == 0) {
        const char* data = "Hello World Hello.World Hello World";
        size_t bytes = strlen((const char*)data);
        r = test(null, (const uint8_t*)data, bytes);
    }
#if 1
    if (r == 0 && file.exist(__FILE__)) {
        r = test_compression(__FILE__);
    }
    // bits len: 3.01 pos: 10.73 words: 91320 lens: 112
    if (r == 0 && file.exist("test/bible.txt")) {
        r = test_compression("test/bible.txt");
    }
    // len: 2.33 pos: 10.78 words: 12034 lens: 40
    if (r == 0 && file.exist("test/hhgttg.txt")) {
        r = test_compression("test/hhgttg.txt");
    }
    if (r == 0 && file.exist("test/confucius.txt")) {
        r = test_compression("test/confucius.txt");
    }
    if (r == 0 && file.exist("test/laozi.txt")) {
        r = test_compression("test/laozi.txt");
    }
    if (r == 0 && file.exist("test/sqlite3.c")) {
        r = test_compression("test/sqlite3.c");
    }
    if (r == 0 && file.exist("test/arm64.elf")) {
        r = test_compression("test/arm64.elf");
    }
    if (r == 0 && file.exist("test/x64.elf")) {
        r = test_compression("test/x64.elf");
    }
    if (r == 0 && file.exist("test/mandrill.bmp")) {
        r = test_compression("test/mandrill.bmp");
    }
    if (r == 0 && file.exist("test/mandrill.png")) {
        r = test_compression("test/mandrill.png");
    }
    // argv[0] executable filepath (Windows) or possibly name (Unix)
    if (r == 0 && file.exist(argv[0])) {
        r = test_compression(argv[0]);
    }
#endif
    return r;
}

#define map_implementation
#include "map.h"

#define bitstream_implementation
#include "bitstream.h"

#define huffman_implementation
#include "huffman.h"

#define file_implementation
#include "file.h"

#define squeeze_implementation
#include "squeeze.h"


