#include "handlers.h"
#include "api/print.h"


uint8_t dfu_handler_write(uint8_t ** volatile data, uint16_t size)
{
    printf("writing data (@: %x) size: %x in flash\n", data, size);
    return 0;
}

uint8_t dfu_handler_read(uint8_t *data, uint16_t size)
{
    printf("reading data (@: %x) size: %x from flash\n", data, size);
    return 0;
}
