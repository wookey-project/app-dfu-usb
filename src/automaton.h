#ifndef AUTOMATON_H_
#define AUTOMATON_H_

typedef enum {
    DFUUSB_STATE_INIT = 0,
    DFUUSB_STATE_IDLE,
    DFUUSB_STATE_GETHEADER,
    DFUUSB_STATE_AUTH,
    DFUUSB_STATE_DWNLOAD,
    DFUUSB_STATE_ERROR
} t_dfuusb_state;

t_dfuusb_state
get_task_state(void);

const char*
get_state_name(t_dfuusb_state state);

void
set_task_state(t_dfuusb_state state);

const char *get_state_name(t_dfuusb_state state);


#endif/*!AUTOMATON_H_*/
