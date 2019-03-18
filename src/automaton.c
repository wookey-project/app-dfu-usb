#include "main.h"
#include "api/types.h"
#include "api/stdio.h"
#include "api/nostd.h"
#include "automaton.h"


static const char *dfuusb_states[] = {
    "DFUUSB_STATE_INIT",
    "DFUUSB_STATE_IDLE",
    "DFUUSB_STATE_GETHEADER",
    "DFUUSB_STATE_AUTH",
    "DFUUSB_STATE_DWNLOAD",
    "DFUUSB_STATE_ERROR"
};

volatile t_dfuusb_state current_state = DFUUSB_STATE_IDLE;

t_dfuusb_state get_task_state(void)
{
    return current_state;
}

const char *get_state_name(t_dfuusb_state state)
{
    return dfuusb_states[state];
}

void set_task_state(t_dfuusb_state state)
{
    printf("state: %s => %s\n", get_state_name(current_state), get_state_name(state));
    current_state = state;
}
