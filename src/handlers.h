#ifndef DFUUSB_HANDLERS_H_
#define DFUUSB_HANDLERS_H_

#include "api/types.h"

uint8_t dfu_handler_post_auth(void);

uint8_t dfu_handler_write(uint8_t ** volatile data, const uint16_t data_size, uint16_t blocknum);

uint8_t dfu_handler_read(uint8_t *data, uint16_t size);

void    dfu_handler_eof(void);


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
