#include "handlers.h"
#include "api/types.h"
#include "api/print.h"
#include "api/syscall.h"
#include "wookey_ipc.h"
#include "main.h"

#include "crc32.h"

#define DFU_HEADER_LEN 136

#define DFU_USB_DEBUG 0
#define CRC 0

#if CRC
static uint32_t crc32_buf = 0xffffffff;
#endif

#if CRC
static inline check_crc(uint8_t *buf, uint16_t data_size)
{
    int size = data_size;
    crc32_buf = crc32((unsigned char*)buf, size, crc32_buf);
    printf("buf: %x, bufsize: %d, crc32: %x\n", buf, data_size, ~crc32_buf);
    printf("buf[0-3]: %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
    if (data_size > 16) {
        printf("buf[%d-%d]: %x %x %x %x\n",
                data_size - 4, data_size,
                buf[data_size - 4], buf[data_size - 3], buf[data_size - 2], buf[data_size - 1]);
    }
}
#endif


/* this is the DFU header than need to be sent to SMART for verification */
static char dfu_header[DFU_HEADER_LEN] = { 0 };

/* when starting, dfu_header is empty, waiting for the host to send it */
static uint8_t current_header_offset = 0;

/* authenticate header with smart */
static inline bool dfu_authenticate_header(void)
{
    uint8_t offset = 0;
    struct sync_command_data sync_command_rw;

    do {
        /* residual data to send to smart */
        uint8_t residual = DFU_HEADER_LEN - offset;

        sync_command_rw.magic = MAGIC_DFU_HEADER_SEND;
        sync_command_rw.state = SYNC_DONE;

        /* copying at most 32 bytes in the IPC structure */
        memcpy(sync_command_rw.data.u8,
                &dfu_header[offset],
                residual < 32 ? residual : 32);
        sync_command_rw.data_size = residual < 32 ? residual : 32;

        /* sending the IPC */
        sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
                sizeof(struct sync_command_data),
                (char*)&sync_command_rw);

        /* updating the current buffer offset */
        offset += (residual < 32 ? residual : 32);
    } while (offset < DFU_HEADER_LEN);

    /* waiting for Smart response (VALID or INVALID header */
    uint8_t id = get_dfucrypto_id();
    logsize_t size = sizeof(struct sync_command_data);
    sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&sync_command_rw);

    if (sync_command_rw.magic == MAGIC_DFU_HEADER_VALID) {
        return true;
    }
    return false;
}


uint8_t dfu_handler_write(uint8_t ** volatile data, const uint16_t data_size, uint16_t blocknum)
{
    t_dfuusb_state state = get_task_state();
    struct sync_command_data sync_command_rw;

#if DFU_USB_DEBUG
    printf("writing data (@: %x) size: %d in flash\n", data, data_size);
#endif

#if CRC
    check_crc((uint8_t*)data, data_size);
#endif

    switch (state) {
        case DFUUSB_STATE_IDLE:
        {
            if (data_size >= DFU_HEADER_LEN) {
                /* header has been sent in one time */
                memcpy(dfu_header, (uint8_t*)data, DFU_HEADER_LEN);
                set_task_state(DFUUSB_STATE_AUTH);
                /* asking smart for header authentication */
                if (!dfu_authenticate_header()) {
                    set_task_state(DFUUSB_STATE_ERROR);
                    return 1;
                }
                set_task_state(DFUUSB_STATE_DWNLOAD);


                /* if authentication ok and there is still data in the buffer,
                 * request its copy to flash */
                if (data_size > DFU_HEADER_LEN) {
                    /* FIXME: before requesting DMA copy, the buffer content
                     * MUST be moved to the buffer start, deleting the header
                     * from the buffer, as the DMA can't change its 
                     * copy start-address !!! */

                    /* sending DMA request for the whole buffer to Crypto */
                    sync_command_rw.magic = MAGIC_DATA_WR_DMA_REQ;
                    sync_command_rw.state = SYNC_ASK_FOR_DATA;
                    sync_command_rw.data_size = 2;
                    sync_command_rw.data.u16[0] = data_size - DFU_HEADER_LEN;
                    sync_command_rw.data.u16[1] = blocknum;

                    sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
                            sizeof(struct sync_command_data),
                            (char*)&sync_command_rw);
                }
            } else {
                /* header must be generated with multiple chunks */
                memcpy(dfu_header, (uint8_t*)data, data_size);
                current_header_offset += data_size;
                set_task_state(DFUUSB_STATE_GETHEADER);
                /* continuing during next call... */
            }
            break;
        }
        case DFUUSB_STATE_GETHEADER:
        {
            /*
             * generating the header buffer and looping on USB DFU input
             * chunk read while the header buffer is not fullfill. When done,
             * passing to DFUUSB_STATE_AUTH state, waiting for Smart to
             * authenticate the header.
             * This state is reached only when transfer size is smaller than
             * DFU header buffer size and require multiple chunk reads
             */
            if (data_size >= DFU_HEADER_LEN - (current_header_offset)) {
                /* enough bytes received to fullfill the header */
                memcpy(&dfu_header[current_header_offset], (uint8_t*)data, DFU_HEADER_LEN);
                set_task_state(DFUUSB_STATE_AUTH);
                if (!dfu_authenticate_header()) {
                    set_task_state(DFUUSB_STATE_ERROR);
                    return 1;
                }
                set_task_state(DFUUSB_STATE_DWNLOAD);

                /* if authentication ok and there is still data in the buffer,
                 * request its copy to flash */
                if (data_size > DFU_HEADER_LEN) {
                    /* FIXME: before requesting DMA copy, the buffer content
                     * MUST be moved to the buffer start, deleting the header
                     * from the buffer, as the DMA can't change its 
                     * copy start-address !!! */

                    /* sending DMA request for the whole buffer to Crypto */
                    sync_command_rw.magic = MAGIC_DATA_WR_DMA_REQ;
                    sync_command_rw.state = SYNC_ASK_FOR_DATA;
                    sync_command_rw.data_size = 2;
                    sync_command_rw.data.u16[0] = data_size - DFU_HEADER_LEN;
                    sync_command_rw.data.u16[1] = blocknum;

                    sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
                            sizeof(struct sync_command_data),
                            (char*)&sync_command_rw);
                }
            } else {
                /* */
                memcpy(&dfu_header[current_header_offset], (uint8_t*)data, data_size);
                current_header_offset += data_size;
                /* continuing during next call... */
            }
            break;
        }
        case DFUUSB_STATE_DWNLOAD:
        {
            /* sending DMA request for the whole buffer to Crypto */
            sync_command_rw.magic = MAGIC_DATA_WR_DMA_REQ;
            sync_command_rw.state = SYNC_ASK_FOR_DATA;
            sync_command_rw.data_size = 2;
            sync_command_rw.data.u16[0] = data_size;
            sync_command_rw.data.u16[1] = blocknum;

            sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
                    sizeof(struct sync_command_data),
                    (char*)&sync_command_rw);

            break;
        }
        default: {
            printf("Error! write callback should not be called in %x state\n", get_task_state());
            break;
        }
    }
    data = data;

    return 0;
}

uint32_t flash_block = 0;
uint8_t dfu_handler_read(uint8_t *data, uint16_t data_size)
{
    struct sync_command_data sync_command_rw;
#if DFU_USB_DEBUG
    printf("reading data (@: %x) size: %d from flash\n", data, data_size);
#endif
    data = data;
    data_size = data_size;

    memset(data, flash_block, data_size);
    flash_block++;
    sync_command_rw.magic = MAGIC_DATA_RD_DMA_REQ;
    sync_command_rw.state = SYNC_ASK_FOR_DATA;
    sync_command_rw.data_size = 1;
    sync_command_rw.data.u16[0] = data_size;
// fixme no field for DFU... ?    sync_command_rw.sector_size = data_size;

    sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(), sizeof(struct sync_command_data), (char*)&sync_command_rw);


    return 0;
}
