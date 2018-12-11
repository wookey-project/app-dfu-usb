#include "handlers.h"
#include "api/types.h"
#include "api/print.h"
#include "api/syscall.h"
#include "wookey_ipc.h"
#include "main.h"


/*
 * FIXME: this should be replaced by the data_sector_block identifier as given by the host
 * in the DFU protocol. This information specifies the chunk identifier of the file, from
 * which the position in the flash (and the associated IV) can be calculated.
 */
static uint32_t block_num = 0;

uint8_t dfu_handler_write(uint8_t ** volatile data, uint16_t data_size)
{
    printf("writing data (@: %x) size: %x in flash\n", data, data_size);

    struct dataplane_command dataplane_command_rw;
    struct dataplane_command dataplane_command_ack;
    uint8_t id;
    logsize_t size;

    dataplane_command_rw.magic = MAGIC_DATA_WR_DMA_REQ;
    dataplane_command_rw.num_sectors = block_num++;
// fixme no field for DFU... ?    dataplane_command_rw.sector_size = data_size;

    sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(), sizeof(struct dataplane_command), (char*)&dataplane_command_rw);

    id = get_dfucrypto_id();
    size = sizeof(struct dataplane_command);
    sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&dataplane_command_ack);
    if (dataplane_command_ack.magic != MAGIC_DATA_WR_DMA_ACK) {
        goto err;
    }
    return 0;
err:
    return 1;
}

uint8_t dfu_handler_read(uint8_t *data, uint16_t data_size)
{
    printf("reading data (@: %x) size: %x from flash\n", data, data_size);
    return 0;
}
