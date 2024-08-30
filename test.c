#include "rt.h"

#define assert(b, ...) rt_assert(b, __VA_ARGS__)
#define printf(...)    rt_printf(__VA_ARGS__)
#define println(...)   rt_println(__VA_ARGS__)

#include "map.h"
#include "sqz.h"

enum { window_bits = 11 };

static uint64_t file_read(sqz_t* lz) {
    uint64_t buffer = 0;
    if (lz->error == 0) { // sticky
        FILE* f = (FILE*)lz->that;
        const size_t bytes = sizeof(buffer);
        if (fread(&buffer, 1, bytes, f) != bytes) {
            // reading past end of file does not set errno
            lz->error = errno == 0 ? EBADF : errno;
        }
    }
    return buffer;
}

static void file_write(sqz_t* lz, uint64_t buffer) {
    if (lz->error == 0) {
        FILE* f = (FILE*)lz->that;
        const size_t bytes = sizeof(buffer);
        if (fwrite(&buffer, 1, bytes, f) != bytes) {
            lz->error = errno;
        }
    }
}

static bool file_exist(const char* filename) {
    struct stat st = {0};
    return stat(filename, &st) == 0;
}

static const char* input_file;

static errno_t compress(const char* fn, const uint8_t* data, size_t bytes) {
    FILE* out = null; // compressed file
    errno_t r = fopen_s(&out, fn, "wb") != 0;
    if (r != 0 || out == null) {
        rt_println("Failed to create \"%s\": %s", fn, strerror(r));
        return r;
    }
    sqz_t lz = {
        .that = (void*)out,
        .write = file_write
    };
    sqz.write_header(&lz, bytes, window_bits);
    sqz.compress(&lz, data, bytes, window_bits);
    rt_assert(lz.error == 0);
    r = fclose(out) == 0 ? 0 : errno; // e.g. overflow writing buffered output
    if (r != 0) {
        rt_println("Failed to flush on file close: %s", strerror(r));
    } else {
        r = lz.error;
        if (r) {
            rt_println("Failed to compress: %s", strerror(r));
        } else {
            double percent = lz.written * 100.0 / bytes;
            if (input_file != null) {
                rt_println("%7lld -> %7lld %5.1f%% of \"%s\"",
                            bytes, lz.written, percent, input_file);
            } else {
                rt_println("%7lld -> %7lld %5.1f%%",
                            bytes, lz.written, percent);
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
        rt_println("Failed to open \"%s\"", fn);
        return r;
    }
    sqz_t lz = {
        .that = (void*)in,
        .read = file_read
    };
    size_t bytes = 0;
    uint8_t window_bits = 0;
    sqz.read_header(&lz, &bytes, &window_bits);
    rt_assert(lz.error == 0 && bytes == size && window_bits == window_bits);
    uint8_t* data = (uint8_t*)malloc(bytes + 1);
    if (data == null) {
        rt_println("Failed to allocate memory for decompressed data");
        fclose(in);
        return ENOMEM;
    }
    data[bytes] = 0x00;
    sqz.decompress(&lz, data, bytes, window_bits);
    fclose(in);
    rt_assert(lz.error == 0);
    if (lz.error == 0) {
        const bool same = size == bytes && memcmp(input, data, bytes) == 0;
        rt_assert(same);
        if (!same) {
            rt_println("compress() and decompress() are not the same");
            // ENODATA is not original posix error but is OpenGroup error
            r = ENODATA; // or EIO
        } else if (bytes < 128) {
            rt_println("decompressed: %s", data);
        }
    }
    free(data);
    if (r != 0) {
        rt_println("Failed to decompress");
    }
    return r;
}

static errno_t file_size(FILE* f, size_t* size) {
    // on error returns (fpos_t)-1 and sets errno
    fpos_t pos = 0;
    if (fgetpos(f, &pos) != 0) { return errno; }
    if (fseek(f, 0, SEEK_END) != 0) { return errno; }
    fpos_t eof = 0;
    if (fgetpos(f, &eof) != 0) { return errno; }
    if (fseek(f, 0, SEEK_SET) != 0) { return errno; }
    if ((uint64_t)eof > SIZE_MAX) { return E2BIG; }
    *size = (size_t)eof;
    return 0;
}

static errno_t read_fully(FILE* f, const uint8_t* *data, size_t *bytes) {
    size_t size = 0;
    errno_t r = file_size(f, &size);
    if (r != 0) { return r; }
    if (size > SIZE_MAX) { return E2BIG; }
    uint8_t* p = (uint8_t*)malloc(size); // does set errno on failure
    if (p == null) { return errno; }
    if (fread(p, 1, size, f) != (size_t)size) { free(p); return errno; }
    *data = p;
    *bytes = (size_t)size;
    return 0;
}

static errno_t read_whole_file(const char* fn, const uint8_t* *data, size_t *bytes) {
    FILE* f = null;
    errno_t r = fopen_s(&f, fn, "rb");
    if (r != 0) {
        rt_println("Failed to open file \"%s\": %s", fn, strerror(r));
        return r;
    }
    r = read_fully(f, data, bytes); // to the heap
    if (r != 0) {
        rt_println("Failed to read file \"%s\": %s", fn, strerror(r));
        fclose(f);
        return r;
    }
    return fclose(f) == 0 ? 0 : errno;
}

static errno_t test(const uint8_t* data, size_t bytes) {
    const char* compressed = "~compressed~.bin";
    errno_t r = compress(compressed, data, bytes);
    if (r == 0) {
        r = verify(compressed, data, bytes);
    }
    (void)remove(compressed);
    return r;
}

static errno_t test_compression(const char* fn) {
    uint8_t* data = null;
    size_t bytes = 0;
    errno_t r = read_whole_file(fn, &data, &bytes);
    if (r != 0) { return r; }
    input_file = fn;
    return test(data, bytes);
}

static errno_t locate_test_folder(void) {
    // on Unix systems with "make" executable usually resided
    // and is run from root of repository... On Windows with
    // MSVC it is buried inside bin/... folder depths
    // on X Code in MacOS it can be completely out of tree.
    // So we need to find the test files.
    for (;;) {
        if (file_exist("test/bible.txt")) { return 0; }
        if (chdir("..") != 0) { return errno; }
    }
}

int main(int argc, const char* argv[]) {
    const char* exe = argv[0]; // executable filepath or name
    (void)argc; // unused
    errno_t r = locate_test_folder();
/*
    if (r == 0) {
        uint8_t data[4 * 1024] = {0};
        r = test(data, sizeof(data));
        // lz77 deals with run length encoding in amazing overlapped way
        for (int32_t i = 0; i < sizeof(data); i += 4) {
            memcpy(data + i, "\x01\x02\x03\x04", 4);
        }
        r = test(data, sizeof(data));
    }
*/
    if (r == 0) {
        const char* data = "Hello World Hello.World Hello World";
        size_t bytes = strlen((const char*)data);
        r = test((const uint8_t*)data, bytes);
    }
    if (r == 0 && file_exist(__FILE__)) {
        r = test_compression(__FILE__);
    }
    // bits len: 3.01 pos: 10.73 words: 91320 lens: 112
    if (r == 0 && file_exist("test/bible.txt")) {
        r = test_compression("test/bible.txt");
    }
    // len: 2.33 pos: 10.78 words: 12034 lens: 40
    if (r == 0 && file_exist("test/hhgttg.txt")) {
        r = test_compression("test/hhgttg.txt");
    }
    if (r == 0 && file_exist("test/confucius.txt")) {
        r = test_compression("test/confucius.txt");
    }
    if (r == 0 && file_exist("test/laozi.txt")) {
        r = test_compression("test/laozi.txt");
    }
    if (r == 0 && file_exist("test/sqlite3.c")) {
        r = test_compression("test/sqlite3.c");
    }
    if (r == 0 && file_exist("test/arm64.elf")) {
        r = test_compression("test/arm64.elf");
    }
    if (r == 0 && file_exist("test/x64.elf")) {
        r = test_compression("test/x64.elf");
    }
    if (r == 0 && file_exist("test/mandrill.png")) {
        r = test_compression("test/mandrill.png");
    }
    if (r == 0 && file_exist(exe)) {
        r = test_compression(exe);
    }
    return r;
}

#define map_implementation
#include "map.h"

#define bitstream_implementation
#include "bitstream.h"

#define huffman_implementation
#include "huffman.h"

#define sqz_implementation
#include "sqz.h"


