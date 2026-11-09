/*
 * Host tests for the portable SHA-256 and CRC-32.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "checksum.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void sha_hex(const void *data, size_t len, char out[65])
{
    Sha256 ctx;
    unsigned char digest[32];

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
    sha256_hex(digest, out);
}

static void expect_sha(const char *hex, const void *data, size_t len)
{
    char got[65];

    sha_hex(data, len, got);
    if (strcmp(got, hex) != 0) {
        fprintf(stderr, "FAIL sha: got %s want %s\n", got, hex);
        failures++;
    }
}

static void test_sha256_vectors(void)
{
    static const char fips[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    char million[1000];
    Sha256 ctx;
    unsigned char digest[32];
    char hex[65];
    int i;

    expect_sha("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
               "", 0);
    expect_sha("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
               "abc", 3);
    expect_sha("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
               fips, strlen(fips));
    expect_sha("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
               64);

    memset(million, 'a', sizeof(million));
    sha256_init(&ctx);
    for (i = 0; i < 1000; i++)
        sha256_update(&ctx, million, sizeof(million));
    sha256_final(&ctx, digest);
    sha256_hex(digest, hex);
    CHECK(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
}

static void test_sha256_chunk_boundaries(void)
{
    static const char msg[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const size_t chunks[] = { 1, 55, 56, 63, 64, 65, 127 };
    char want[65];
    size_t c;

    sha_hex(msg, strlen(msg), want);
    for (c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
        Sha256 ctx;
        unsigned char digest[32];
        char got[65];
        size_t off = 0;
        size_t step = chunks[c];

        sha256_init(&ctx);
        while (off < strlen(msg)) {
            size_t n = strlen(msg) - off;
            if (n > step)
                n = step;
            sha256_update(&ctx, msg + off, n);
            off += n;
        }
        sha256_final(&ctx, digest);
        sha256_hex(digest, got);
        if (strcmp(got, want) != 0) {
            fprintf(stderr, "FAIL chunk %u: %s != %s\n",
                    (unsigned)step, got, want);
            failures++;
        }
    }
}

static void test_crc32(void)
{
    static const char *s = "123456789";
    uint32_t one = crc32_bytes(s, 9);
    uint32_t chunked = 0;

    CHECK(one == 0xCBF43926u);
    chunked = crc32_update(0, s, 1);
    chunked = crc32_update(chunked, s + 1, 3);
    chunked = crc32_update(chunked, s + 4, 5);
    CHECK(chunked == one);
}

int main(void)
{
    test_sha256_vectors();
    test_sha256_chunk_boundaries();
    test_crc32();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all checksum tests passed\n");
    return 0;
}
