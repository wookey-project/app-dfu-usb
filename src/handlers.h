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

#ifndef DFUUSB_HANDLERS_H_
#define DFUUSB_HANDLERS_H_

#include "libc/types.h"

uint8_t dfu_handler_post_auth(void);

static inline int dfu_crypto_chunk_size_sanity_check(uint16_t dfu_sz, uint16_t crypto_sz){
        if((dfu_sz == 0) || (crypto_sz == 0)){
                goto err;
        }
        if(crypto_sz / dfu_sz < 1){
                goto err;
        }
        if(crypto_sz % dfu_sz != 0){
                goto err;
        }
        return 0;
err:
        return -1;
}

#endif/*!DFUUSB_HANDLERS_H_*/
