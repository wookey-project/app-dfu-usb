#ifndef CRC32_H_
#define CRC32_H_

#include "api/types.h"

/*
 * @brief Linux-compatible CRC32 implementation
 *
 * Return the CRC32 of the current buffer
 * @param buf  the buffer on which the CRC32 is calculated
 * @param len  the buffer len
 * @param init when calculating CRC32 on successive chunk to get back
 *             the CRC32 of the whole input content, contains the previous
 *             chunk CRC32, or 0xffffffff for the first one
 */
uint32_t
crc32 (const unsigned char *buf, int len, uint32_t init);

#endif/*!CRC32_H_*/
