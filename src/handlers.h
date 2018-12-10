#ifndef DFUUSB_HANDLERS_H_
#define DFUUSB_HANDLERS_H_

#include "api/types.h"

uint8_t dfu_handler_write(uint8_t ** volatile data, uint16_t size);

uint8_t dfu_handler_read(uint8_t *data, uint16_t size);

#endif/*!DFUUSB_HANDLERS_H_*/