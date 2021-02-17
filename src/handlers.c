#include "handlers.h"
#include "libc/types.h"
#include "libc/stdio.h"
#include "libc/nostd.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "wookey_ipc.h"
#include "main.h"
#include "libfw.h"
#include "dfu.h"

#define DFU_HEADER_LEN 256

#define DFU_MAX_CHUNK_LEN 65536

#define DFU_USB_DEBUG 0

extern volatile uint16_t crypto_chunk_size;
extern volatile uint16_t dfu_usb_chunk_size;

/* this is the DFU header than need to be sent to SMART for verification */
static uint8_t dfu_header[DFU_HEADER_LEN] = { 0 };

/* when starting, dfu_header is empty, waiting for the host to send it */
static uint16_t current_header_offset = 0;

static volatile uint16_t current_data_size = 0;
static volatile uint16_t current_blocknum = 0;
static volatile uint16_t current_crypto_block_num = 1;

static volatile bool is_last_block = false;

/***********************************************************
 * DFU header and application level protocol implementation
 **********************************************************/
volatile bool dfu_reset_asked = false;
void dfu_reset_device(void)
{
	dfu_reset_asked = true;
}

/* Sanity check that we are asked for proper pseudo-sequential crypto blocks.
 */
static int dnload_transfers_sanity_check(uint32_t curr_block_index, uint16_t curr_transfer_size){
	if(dfu_crypto_chunk_size_sanity_check(dfu_usb_chunk_size, crypto_chunk_size)){
		printf("Error: sanity check on DFU (%d) and crypto (%d) chunk sizes failed!\n", dfu_usb_chunk_size, crypto_chunk_size);
		goto err;
	}
	/* There is no reason to get the header here ... */
	uint32_t curr_block_offset = (uint32_t)curr_block_index * (uint32_t)dfu_usb_chunk_size;
	if(curr_block_offset < (uint32_t)crypto_chunk_size){
		printf("Error: sanity check failed, sending block %d in crypto header!\n", curr_block_index);
		goto err;
	}
	/* We have to be aligned on the dfu_usb_chunk_size except for the last transfer! */
	if((curr_transfer_size != dfu_usb_chunk_size) && (is_last_block == true)){
		printf("Error: sanity check on DFU (%d) and current chunk (%d) sizes failed!\n", dfu_usb_chunk_size, curr_transfer_size);
		goto err;
	}
	else if(curr_transfer_size != dfu_usb_chunk_size){
		is_last_block = true;
	}
	/* Check that we are asked to decrypt a dfu block inside a crypto block where we have started a decrypt session ... */
	if((curr_block_offset % (uint32_t)crypto_chunk_size) == 0){
		current_crypto_block_num = curr_block_offset / crypto_chunk_size;
	}
	else{
		if((curr_block_offset / crypto_chunk_size) != current_crypto_block_num){
			printf("Error: sanity check on DFU block numbers failed! (current=%d, not in current decrypt session started at block %d))\n", curr_block_index, current_crypto_block_num);
			goto err;
		}
	}

	return 0;
err:
	return -1;
}

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
                (residual < 32) ? residual : 32);
        sync_command_rw.data_size = (residual < 32) ? residual : 32;

        /* sending the IPC */
        sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
                sizeof(struct sync_command_data),
                (char*)&sync_command_rw);

        /* updating the current buffer offset */
        offset += ((residual < 32) ? residual : 32);
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
    /* Sanity check on the chunk size */
    if(header.chunksize > DFU_MAX_CHUNK_LEN){

        struct sync_command_data sync_command;
#if DFU_USB_DEBUG
        printf("Max chunk size %d exceeds limit %d!\n", header.chunksize, DFU_MAX_CHUNK_LEN);
#endif
        /* corrupted header received, response through reset request to security monitor */
        sync_command.magic = MAGIC_REBOOT_REQUEST;
        sync_command.state = SYNC_WAIT;
        sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(),
                    sizeof(struct sync_command),
                    (char*)&sync_command);
    }
    if (bytes_received >= header.chunksize) {
#if DFU_USB_DEBUG
        printf("first crypto chunk received ! bytes read: %x / %x\n", bytes_received, header.chunksize);
#endif
        return true;
    }
#if DFU_USB_DEBUG
    printf("first crypto chunk not received ! bytes read: %x / %x\n", bytes_received, header.chunksize);
#endif
    return false;
}


/***********************************************************
 * DFU API backend access implementation
 * INFO: these functions are required by libDFU to access
 * storage backend (read, write, eof info). Their absence
 * will lead to link error (missing symbol).
 **********************************************************/

uint8_t dfu_backend_write(uint8_t * volatile data,
                          const uint16_t      data_size,
                          uint16_t            blocknum)
{
    t_dfuusb_state state;
    struct sync_command_data sync_command_rw;
    current_data_size = data_size;
    current_blocknum  = blocknum;

#if DFU_USB_DEBUG
    printf("writing data (block: %d) size: %d\n", blocknum, data_size);
#endif

    /* If we were in the middle of a transfer, and we receive block 0 again, this means
     * that we have to reset our state machine.
     */
    if(blocknum == 0){
#if DFU_USB_DEBUG
        printf("DFU block 0 received, resetting state machine to IDLE\n");
#endif
    	bytes_received = 0;
        /* Reinit our variable handling the possible last block */
        is_last_block = false;
        current_crypto_block_num = 1;
	set_task_state(DFUUSB_STATE_IDLE);
    }

    bytes_received += data_size;
    state = get_task_state();
    switch (state) {
        case DFUUSB_STATE_IDLE:
        {
            if (data_size >= DFU_HEADER_LEN) {
                /* header has been sent in one time */
                memcpy(dfu_header, data, DFU_HEADER_LEN);
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
                memcpy(dfu_header, data, data_size);
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
            if (data_size >= (DFU_HEADER_LEN - (current_header_offset))) {
                /* enough bytes received to fullfill the header */
                if (!header_full) {
                    memcpy(&dfu_header[current_header_offset], data, DFU_HEADER_LEN - current_header_offset);
                    current_header_offset += (DFU_HEADER_LEN - current_header_offset);
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
                memcpy(&dfu_header[current_header_offset], data, data_size);
                current_header_offset += data_size;
                dfu_store_finished();
                /* continuing during next call... */
            }
            break;
        }
        case DFUUSB_STATE_DWNLOAD:
        {
	    /* Sanity check */
	    if(dnload_transfers_sanity_check(blocknum, data_size)){
		printf("Error: sanity check error when performing DFUUSB_STATE_DWNLOAD. Block %d of size %d is refused!\n", blocknum, data_size);
		break;
	    }
            /* sending DMA request for the whole buffer to Crypto */
            sync_command_rw.magic = MAGIC_DATA_WR_DMA_REQ;
            sync_command_rw.state = SYNC_ASK_FOR_DATA;
            sync_command_rw.data_size = 2;
            sync_command_rw.data.u16[0] = data_size;
	    /* The block number we send is the block number where we have discarded the header */
	    if(blocknum < (crypto_chunk_size / dfu_usb_chunk_size)){
		/* Sanity check (even if it should have been performed earlier, better safe than sorry ...) */
		printf("Error: sanity check error on block number %d\n", blocknum);
		break;
	    }
            sync_command_rw.data.u16[1] = blocknum - (crypto_chunk_size / dfu_usb_chunk_size);

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

uint8_t dfu_backend_read(uint8_t *data, uint16_t data_size)
{
    struct sync_command_data sync_command_rw;

#if DFU_USB_DEBUG
    printf("reading data (@: %x) size: %d from flash\n", data, data_size);
#endif

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

void dfu_backend_eof(void)
{
    struct sync_command sync_command;

    /* Sanity check on the current state ... */
    if(get_task_state() != DFUUSB_STATE_DWNLOAD){
#if DFU_USB_DEBUG
       printf("Error: dfu_backend_eof while not in DFUUSB_STATE_DWNLOAD \n");
#endif
       return;
    }


#if DFU_USB_DEBUG
    printf("sendinf EOF to flash\n");
#endif

    sync_command.magic = MAGIC_DFU_DWNLOAD_FINISHED;
    sync_command.state = SYNC_DONE;
// fixme no field for DFU... ?    sync_command_rw.sector_size = data_size;

    sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(), sizeof(struct sync_command), (char*)&sync_command);

    return;
}
