/*
 * checksum.h - portable SHA-256 and CRC-32 (clean-room, MIT).
 */
#ifndef PSP_NET_CHECKSUM_H
#define PSP_NET_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t total_len;
    unsigned char block[64];
    uint32_t block_len;
} Sha256;

void sha256_init(Sha256 *ctx);
void sha256_update(Sha256 *ctx, const void *data, size_t len);
void sha256_final(Sha256 *ctx, unsigned char out[32]);
void sha256_hex(const unsigned char digest[32], char out[65]);

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t crc32_bytes(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PSP_NET_CHECKSUM_H */
