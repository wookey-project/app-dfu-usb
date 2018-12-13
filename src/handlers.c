#include "handlers.h"
#include "api/types.h"
#include "api/print.h"
#include "api/syscall.h"
#include "wookey_ipc.h"
#include "main.h"

#include "crc32.h"

#define DFU_USB_DEBUG 0
#define CRC 0

#if CRC
static uint32_t crc32_buf = 0xffffffff;
#endif
/*
 * FIXME: this should be replaced by the data_sector_block identifier as given by the host
 * in the DFU protocol. This information specifies the chunk identifier of the file, from
 * which the position in the flash (and the associated IV) can be calculated.
 */
static uint32_t block_num = 0;

uint8_t dfu_handler_write(uint8_t ** volatile data, const uint16_t data_size __attribute__((unused)))
{
#if DFU_USB_DEBUG
    printf("writing data (@: %x) size: %x in flash\n", data, data_size);
#endif
    data = data;

#if CRC
    uint8_t *buf = (uint8_t*)data;
    int size = data_size;
    crc32_buf = crc32((unsigned char*)buf, size, crc32_buf);
    printf("buf: %x, bufsize: %d, crc32: %x\n", buf, data_size, ~crc32_buf);
    printf("buf[0-3]: %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
    if (data_size > 16) {
        printf("buf[%d-%d]: %x %x %x %x\n",
                data_size - 4, data_size,
                buf[data_size - 4], buf[data_size - 3], buf[data_size - 2], buf[data_size - 1]);
    }
#endif

    struct dataplane_command dataplane_command_rw;

    dataplane_command_rw.magic = MAGIC_DATA_WR_DMA_REQ;
    dataplane_command_rw.num_sectors = block_num++;
// fixme no field for DFU... ?    dataplane_command_rw.sector_size = data_size;

    sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(), sizeof(struct dataplane_command), (char*)&dataplane_command_rw);

    return 0;
}

uint8_t dfu_handler_read(uint8_t *data, uint16_t data_size)
{
#if DFU_USB_DEBUG
    printf("reading data (@: %x) size: %x from flash\n", data, data_size);
#endif
    data = data;
    data_size = data_size;
    return 0;
}
