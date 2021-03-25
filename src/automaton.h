/*
 *
 * Copyright 2019 The wookey project team <wookey@ssi.gouv.fr>
 *   - Ryad     Benadjila
 *   - Arnauld  Michelizza
 *   - Mathieu  Renard
 *   - Philippe Thierry
 *   - Philippe Trebuchet
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * the Free Software Foundation; either version 3 of the License, or (at
 * ur option) any later version.
 *
 * This package is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this package; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

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
