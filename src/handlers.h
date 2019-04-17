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
