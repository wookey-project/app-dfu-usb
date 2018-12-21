#include "handlers.h"
#include "api/types.h"
#include "api/print.h"
#include "api/syscall.h"
#include "wookey_ipc.h"
#include "main.h"
#include "libfw.h"
#include "dfu.h"

#define DFU_HEADER_LEN 256

#define DFU_USB_DEBUG 1
#define CRC 0

/* this is the DFU header than need to be sent to SMART for verification */
static uint8_t dfu_header[DFU_HEADER_LEN] = { 0 };

/* when starting, dfu_header is empty, waiting for the host to send it */
static uint16_t current_header_offset = 0;

static uint16_t current_data_size = 0;
static uint16_t current_blocknum = 0;

/* authenticate header with smart */
static inline void dfu_init_header_authentication(void)
{
    uint16_t offset = 0;
    uint16_t residual = 0;
    struct sync_command_data sync_command_rw;

#if DFU_USB_DEBUG
    printf("printing header before sending...\n");
    firmware_print_header((firmware_header_t *)dfu_header);
    printf("end of header printing...\n");
#endif
    do {
        /* residual data to send to smart */
        residual = DFU_HEADER_LEN - offset;

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

    /* finishing with a ZLP IPC to smart, in order to inform it that the
     * header transmission is terminated */
    sync_command_rw.magic = MAGIC_DFU_HEADER_SEND;
    sync_command_rw.state = SYNC_DONE;
    sync_command_rw.data_size = 0;
    sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
            sizeof(struct sync_command_data),
            (char*)&sync_command_rw);
}


/*
 * This handler is called when smart has finished its check of the firmware
 * header. Depending on the result, it may lead to continuing the download or
 * to an error state (invalid header)
 */
uint8_t dfu_handler_post_auth(void)
{
    struct sync_command_data sync_command_rw;
    /* if authentication ok and there is still data in the buffer,
     * request its copy to flash */
    if ((current_data_size + current_header_offset) > DFU_HEADER_LEN) {
        /* enough bytes received to fullfill the header */
        /* FIXME: before requesting DMA copy, the buffer content
         * MUST be moved to the buffer start, deleting the header
         * from the buffer, as the DMA can't change its 
         * copy start-address !!! */

        /* sending DMA request for the whole buffer to Crypto */
        sync_command_rw.magic = MAGIC_DATA_WR_DMA_REQ;
        sync_command_rw.state = SYNC_ASK_FOR_DATA;
        sync_command_rw.data_size = 2;
        /* FIXME: residual size to be calculated */
        sync_command_rw.data.u16[0] = current_data_size - (DFU_HEADER_LEN - current_header_offset);
        sync_command_rw.data.u16[1] = current_blocknum;

        sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
                sizeof(struct sync_command_data),
                (char*)&sync_command_rw);

        /* reinit for next download */
        current_header_offset = 0;
    }
    return 0;
 
}

static volatile bool header_full = false;

static volatile uint32_t bytes_received = 0;

bool first_chunk_received(void)
{
    /* cryptographic chunks must be at least of the same size
     * as the firmware header, as this header contains the size
     * of the cryptographic chunk we need to parse. By default,
     * while this header is not yet fully read from USB, we
     * consider that the first cryptographic chunk is *not*
     * fully received */
    if (!header_full) {
        return false;
    }
    firmware_header_t header;
    firmware_parse_header(dfu_header, DFU_HEADER_LEN, 0, &header, NULL);
    if (bytes_received >= header.chunksize) {
#if DFU_USB_DEBUG
        printf("first crypto chunk received ! bytes read: %x\n", bytes_received);
#endif
        return true;
    }
#if DFU_USB_DEBUG
    printf("first crypto chunk not received ! bytes read: %x\n", bytes_received);
#endif
    return false;
}

uint8_t dfu_handler_write(uint8_t ** volatile data,
                          const uint16_t      data_size,
                          uint16_t            blocknum)
{
    t_dfuusb_state state = get_task_state();
    struct sync_command_data sync_command_rw;
    current_data_size = data_size;
    current_blocknum  = blocknum;

    bytes_received += data_size;

#if DFU_USB_DEBUG
//    printf("writing data (@: %x) size: %d in flash\n", data, data_size);
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
                header_full = true;
                /* asking smart for header authentication */
                if (first_chunk_received()) {
                    set_task_state(DFUUSB_STATE_AUTH);
                    dfu_init_header_authentication();
                } else {
                    /* going to GETHEADER to finish crypto chunk reception */
                    set_task_state(DFUUSB_STATE_GETHEADER);
                    dfu_store_finished();
                }
            } else {
                /* header must be generated with multiple chunks */
                memcpy(dfu_header, (uint8_t*)data, data_size);
                current_header_offset += data_size;
                set_task_state(DFUUSB_STATE_GETHEADER);
                dfu_store_finished();
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
                if (!header_full) {
                    memcpy(&dfu_header[current_header_offset], (uint8_t*)data, DFU_HEADER_LEN - current_header_offset);
                    current_header_offset += DFU_HEADER_LEN - current_header_offset;
                    header_full = true;
                    dfu_store_finished();
                }
                if (first_chunk_received()) {
                    set_task_state(DFUUSB_STATE_AUTH);
                    dfu_init_header_authentication();
                } else {
                    dfu_store_finished();
                }
            } else {
                memcpy(&dfu_header[current_header_offset], (uint8_t*)data, data_size);
                current_header_offset += data_size;
                dfu_store_finished();
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
