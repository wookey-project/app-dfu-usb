#ifndef CRC32_H_
#define CRC32_H_

#include "api/types.h"

unsigned int
crc32 (const unsigned char *buf, int len, unsigned int init);

#endif/*!CRC32_H_*/
