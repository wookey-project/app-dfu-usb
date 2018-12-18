#ifndef MAIN_H_
#define MAIN_H_

typedef enum {
    DFUUSB_STATE_INIT,
    DFUUSB_STATE_IDLE,
    DFUUSB_STATE_GETHEADER,
    DFUUSB_STATE_AUTH,
    DFUUSB_STATE_DWNLOAD,
    DFUUSB_STATE_ERROR
} t_dfuusb_state;

t_dfuusb_state
get_task_state(void);

void
set_task_state(t_dfuusb_state state);

uint8_t
get_dfucrypto_id(void);

#endif/*!MAIN_H_*/
