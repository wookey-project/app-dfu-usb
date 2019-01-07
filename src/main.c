/**
 * @file main.c
 *
 * \brief Main of dummy
 *
 */

#include "api/syscall.h"
#include "api/print.h"
#include "wookey_ipc.h"
#include "usb.h"
#include "dfu.h"
#include "handlers.h"
#include "main.h"
#include "usb_control.h"
#include "api/malloc.h"


#define USB_BUF_SIZE 4096

static uint8_t usb_buf[USB_BUF_SIZE] = { 0 };


static uint8_t id_dfucrypto = 0;



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
//    const char * test = "hello, I'm usb\n";
    volatile e_syscall_ret ret = 0;
//    uint32_t size = 256;
    uint8_t id;

    struct sync_command      ipc_sync_cmd;

    dma_shm_t dmashm_rd;
    dma_shm_t dmashm_wr;
#if 0
    int i = 0;
    uint64_t tick = 0;
#endif

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
    } while (ret == SYS_E_BUSY);

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
    } while (ret == SYS_E_BUSY);

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
    } while (ret == SYS_E_BUSY);
    printf("Crypto informed.\n");

    /*******************************************
     * End of init sequence, let's initialize devices
     *******************************************/
    dfu_init(dfu_handler_write, dfu_handler_read, dfu_handler_eof, (uint8_t**)&usb_buf, USB_BUF_SIZE);


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
                    /* FIXME: real header length should be passed in arg */
                    break;
                }
                case MAGIC_DFU_HEADER_INVALID:
                {
                    /* error !*/
                    printf("Error ! Invalid header ! refusing to continue update\n");
                    if (sync_command_ack.state == SYNC_BADFILE) {
                        dfu_leave_session_with_error(ERRFILE);
                    }
                    set_task_state(DFUUSB_STATE_ERROR);
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
    }

    /* should return to do_endoftask() */
    return 0;
}
