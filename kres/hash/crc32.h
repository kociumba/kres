#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t crc32(const void* buf, size_t size);

uint32_t calculate_crc32c(uint32_t crc32c, const unsigned char* buffer, unsigned int length);

#ifdef __cplusplus
}
#endif

#endif  // CRC32_H
