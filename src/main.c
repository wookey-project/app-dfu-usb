/**
 * @file main.c
 *
 * \brief Main of dummy
 *
 */

#include "libc/syscall.h"
#include "libc/stdio.h"
#include "libc/nostd.h"
#include "libc/string.h"
#include "wookey_ipc.h"
#include "usb.h"
#include "dfu.h"
#include "handlers.h"
#include "main.h"
#include "usb_control.h"
#include "libc/malloc.h"


#define USB_BUF_SIZE 4096
#define DFU_USB_DEBUG 0

extern volatile bool dfu_reset_asked;

static void main_thread_dfu_reset_device(void)
{
    e_syscall_ret ret;

    dfu_reset_asked = false;

    struct sync_command ipc_sync_cmd;
    memset((void*)&ipc_sync_cmd, 0, sizeof(struct sync_command));

    ipc_sync_cmd.magic = MAGIC_REBOOT_REQUEST;
    ret = sys_ipc(IPC_SEND_SYNC, get_dfucrypto_id(), sizeof(struct sync_command), (char*)&ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
# if USB_APP_DEBUG
        printf("%s:%d Oops ! ret = %d\n", __func__, __LINE__, ret);
#endif
    }
    while (1) {
        /* voluntary freeze, in our case, as this reset order request
         * reboot */
        continue;
    }
    return;
}


/* NOTE: alignment due to DMA */
__attribute__((aligned(4))) static uint8_t usb_buf[USB_BUF_SIZE] = { 0 };


static uint8_t id_dfucrypto = 0;

/* DFU and crypto chunk sizes.
 * We must have sizeof(crypto_chunk) = multiple of sizeof(DFU_chunk).
 */
volatile uint16_t crypto_chunk_size = 0;
volatile uint16_t dfu_usb_chunk_size = 0;

uint8_t get_dfucrypto_id(void)
{
    return id_dfucrypto;
}

/*
 * We use the local -fno-stack-protector flag for main because
 * the stack protection has not been initialized yet.
 */
int _main(uint32_t task_id)
{
    volatile e_syscall_ret ret = 0;
    uint8_t id;

    struct sync_command      ipc_sync_cmd;

    dma_shm_t dmashm_rd;
    dma_shm_t dmashm_wr;

    printf("Hello ! I'm usb, my id is %x\n", task_id);

    ret = sys_init(INIT_GETTASKID, "dfucrypto", &id_dfucrypto);
    printf("dfucrypto is task %x !\n", id_dfucrypto);


    /* early init DFU stack */
    dfu_early_init();

    /*********************************************
     * Declaring DMA Shared Memory with Crypto
     *********************************************/
    dmashm_rd.target = id_dfucrypto;
    dmashm_rd.source = task_id;
    dmashm_rd.address = (physaddr_t)usb_buf;
    dmashm_rd.size = USB_BUF_SIZE;
    /* Crypto DMA will read from this buffer */
    dmashm_rd.mode = DMA_SHM_ACCESS_RD;

    dmashm_wr.target = id_dfucrypto;
    dmashm_wr.source = task_id;
    dmashm_wr.address = (physaddr_t)usb_buf;
    dmashm_wr.size = USB_BUF_SIZE;
    /* Crypto DMA will write into this buffer */
    dmashm_wr.mode = DMA_SHM_ACCESS_WR;

    printf("Declaring DMA_SHM for FLASH read flow\n");
    ret = sys_init(INIT_DMA_SHM, &dmashm_rd);
    printf("sys_init returns %s !\n", strerror(ret));

    printf("Declaring DMA_SHM for FLASH write flow\n");
    ret = sys_init(INIT_DMA_SHM, &dmashm_wr);
    printf("sys_init returns %s !\n", strerror(ret));

    /* initialize the DFU stack with two buffers of 4096 bits length each. */

    /*******************************************
     * End of init
     *******************************************/

    ret = sys_init(INIT_DONE);
    printf("sys_init DONE returns %x !\n", ret);


    /*******************************************
     * let's syncrhonize with other tasks
     *******************************************/
    logsize_t size = sizeof (struct sync_command);

    printf("sending end_of_init syncrhonization to dfucrypto\n");
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_CMD;
    ipc_sync_cmd.state = SYNC_READY;

    do {
      ret = sys_ipc(IPC_SEND_SYNC, id_dfucrypto, size, (const char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("Oops ! ret = %d\n", ret);
      } else {
          printf("end of end_of_init synchro.\n");
      }
    } while (ret != SYS_E_DONE);

    /* Now wait for Acknowledge from Smart */
    id = id_dfucrypto;

    do {
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("ack from dfucrypto: Oops ! ret = %d\n", ret);
      } else {
          printf("Aclknowledge from dfucrypto ok\n");
      }
    } while (ret != SYS_E_DONE);
    if (   ipc_sync_cmd.magic == MAGIC_TASK_STATE_RESP
        && ipc_sync_cmd.state == SYNC_ACKNOWLEDGE) {
        printf("dfucrypto has acknowledge end_of_init, continuing\n");
    }

    /*******************************************
     * Starting end_of_cryp synchronization
     *******************************************/

    printf("waiting end_of_cryp syncrhonization from dfucrypto\n");

    id = id_dfucrypto;
    size = sizeof(struct sync_command);

    do {
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&ipc_sync_cmd);
    } while (ret != SYS_E_DONE);

    if (   ipc_sync_cmd.magic == MAGIC_TASK_STATE_CMD
        && ipc_sync_cmd.state == SYNC_READY) {
        printf("dfucrypto module is ready\n");
    }

    /* Initialize USB device */
    wmalloc_init();
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_RESP;
    ipc_sync_cmd.state = SYNC_READY;

    size = sizeof(struct sync_command);
    do {
      ret = sys_ipc(IPC_SEND_SYNC, id_dfucrypto, size, (char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("sending Sync ready to dfucrypto: Oops ! ret = %d\n", ret);
      } else {
          printf("sending sync ready to dfucrypto ok\n");
      }
    } while (ret != SYS_E_DONE);

    // take some time to finish all sync ipc...
    sys_sleep(1000, SLEEP_MODE_INTERRUPTIBLE);

    /*******************************************
     * Sharing DMA SHM address and size with dfucrypto
     *******************************************/
    struct dmashm_info {
        uint32_t addr;
        uint16_t size;
    };
    struct dmashm_info dmashm_info;

    dmashm_info.addr = (uint32_t)usb_buf;
    dmashm_info.size = USB_BUF_SIZE;

    printf("informing dfucrypto about DMA SHM...\n");
    do {
      ret = sys_ipc(IPC_SEND_SYNC, id_dfucrypto, sizeof(struct dmashm_info), (char*)&dmashm_info);
    } while (ret != SYS_E_DONE);
    printf("Crypto informed.\n");

    /*******************************************
     * End of init sequence, let's initialize devices
     *******************************************/
    dfu_usb_chunk_size = USB_BUF_SIZE;
    dfu_init((uint8_t**)&usb_buf, USB_BUF_SIZE);


    /*******************************************
     * Starting USB listener
     *******************************************/

    printf("USB main loop starting\n");

    struct sync_command_data sync_command_ack = { 0 };
    id = id_dfucrypto;
    size = sizeof(struct sync_command_data);

    /* end of initialization, starting main loop */
    set_task_state(DFUUSB_STATE_IDLE);

    while (1) {
        /* detecting end of store (if a previous store request has been
         * executed by the store handler. This is an asyncrhonous end of
         * store management
         */

        if ((sys_ipc(IPC_RECV_ASYNC, &id, &size, (char*)&sync_command_ack)) == SYS_E_DONE) {
            switch (sync_command_ack.magic) {
                case MAGIC_DATA_WR_DMA_ACK:
                {
                    dfu_store_finished();
                    break;
                }
                case MAGIC_DATA_RD_DMA_ACK:
                {
                    uint16_t bytes_read = sync_command_ack.data.u16[0];
                    dfu_load_finished(bytes_read);
                    break;
                }
                case MAGIC_DFU_HEADER_VALID:
                {
                    set_task_state(DFUUSB_STATE_DWNLOAD);
                    dfu_store_finished();
                    /* Get the crypto header length here */
                    if(sync_command_ack.data_size != 1){
                        /* Wrong size */
                        printf("Error: error during MAGIC_DFU_HEADER_VALID IPC with dfusmart ...\n");
                        dfu_leave_session_with_error(ERRFILE);
                        set_task_state(DFUUSB_STATE_IDLE);
                    }
                    else{
                        crypto_chunk_size = sync_command_ack.data.u16[0];
#if DFU_USB_DEBUG
                        printf("Received %d as crypto chunk size from dfusmart!\n", crypto_chunk_size);
#endif
                        /* Sanity check */
                        if(dfu_crypto_chunk_size_sanity_check(dfu_usb_chunk_size, crypto_chunk_size)){
                            printf("Error: crypto chunk size %d is not a multiple of DFU chunk size %d\n", crypto_chunk_size, dfu_usb_chunk_size);
                            dfu_leave_session_with_error(ERRFILE);
                            set_task_state(DFUUSB_STATE_IDLE);
                        }
                    }
                    break;
                }
                case MAGIC_DFU_HEADER_INVALID:
                {
                    /* error !*/
                    printf("Error! Invalid header! refusing to continue update\n");
                    if (sync_command_ack.state == SYNC_BADFILE) {
                        dfu_store_finished();
                        dfu_leave_session_with_error(ERRFILE);
                        set_task_state(DFUUSB_STATE_IDLE);
                    } else {
                        dfu_store_finished();
                        dfu_leave_session_with_error(ERRFILE);
                        set_task_state(DFUUSB_STATE_IDLE);
                    }
                    break;
                }
                default:
                {
                    printf("Error! unknown IPC magic received: %x\n", sync_command_ack.magic);
                    set_task_state(DFUUSB_STATE_ERROR);
                    break;
                }
            }
        } else {
            sys_sleep(10, SLEEP_MODE_INTERRUPTIBLE);
        }

        /* executing the DFU automaton */
        dfu_exec_automaton();
	if(dfu_reset_asked == true){
	    main_thread_dfu_reset_device();
	}
    }

    /* should return to do_endoftask() */
    return 0;
}
