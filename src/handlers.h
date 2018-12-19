#ifndef DFUUSB_HANDLERS_H_
#define DFUUSB_HANDLERS_H_

#include "api/types.h"

uint8_t dfu_handler_post_auth(void);

uint8_t dfu_handler_write(uint8_t ** volatile data, const uint16_t data_size, uint16_t blocknum);

uint8_t dfu_handler_read(uint8_t *data, uint16_t size);

#endif/*!DFUUSB_HANDLERS_H_*/
