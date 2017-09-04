/*
 *  Hardware aes collector for the STM32F4 family
 *******************************************************************************
 * Copyright (c) 2017, STMicroelectronics
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include <string.h>
#include "mbedtls/aes.h"

#if defined(MBEDTLS_AES_ALT)

#if defined(TARGET_STM32L486xG)
//the following defines are provided to maintain compatibility between STM32 families
#define __HAL_RCC_CRYP_CLK_ENABLE    __HAL_RCC_AES_CLK_ENABLE
#define __HAL_RCC_CRYP_FORCE_RESET   __HAL_RCC_AES_FORCE_RESET
#define __HAL_RCC_CRYP_RELEASE_RESET __HAL_RCC_AES_RELEASE_RESET
#define CRYP                         AES
#endif

static int aes_set_key( mbedtls_aes_context *ctx, const unsigned char *key, unsigned int keybits )
{
    switch( keybits ) {
        case 128:
            ctx->hcryp_aes.Init.KeySize = CRYP_KEYSIZE_128B;
            memcpy(ctx->aes_key, key, 16);
            break;
        case 192:
#if defined (TARGET_STM32L486xG)
            return(MBEDTLS_ERR_AES_INVALID_KEY_LENGTH);
#else
            ctx->hcryp_aes.Init.KeySize = CRYP_KEYSIZE_192B;
            memcpy(ctx->aes_key, key, 24);
            break;
#endif

        case 256:
            ctx->hcryp_aes.Init.KeySize = CRYP_KEYSIZE_256B;
            memcpy(ctx->aes_key, key, 32);
            break;
       default : return( MBEDTLS_ERR_AES_INVALID_KEY_LENGTH );
    }

    /* Deinitializes the CRYP peripheral */
    if (HAL_CRYP_DeInit(&ctx->hcryp_aes) == HAL_ERROR)
        return (HAL_ERROR);

    ctx->hcryp_aes.Init.DataType = CRYP_DATATYPE_8B;
    ctx->hcryp_aes.Instance = CRYP;
    /* Enable CRYP clock */
    __HAL_RCC_CRYP_CLK_ENABLE();

    ctx->hcryp_aes.Init.pKey = ctx->aes_key;
#if defined (TARGET_STM32L486xG)
    ctx->hcryp_aes.Init.KeyWriteFlag = CRYP_KEY_WRITE_ENABLE;
#endif
    if (HAL_CRYP_Init(&ctx->hcryp_aes) == HAL_ERROR)
        return (HAL_ERROR);

    /* allow multi-instance of CRYP use: save context for CRYP HW module CR */
    ctx->ctx_save_cr = ctx->hcryp_aes.Instance->CR;
    return(0);

}

/* Implementation that should never be optimized out by the compiler */
static void mbedtls_zeroize( void *v, size_t n )
{
    volatile unsigned char *p = (unsigned char*)v; while( n-- ) *p++ = 0;
}


void mbedtls_aes_init( mbedtls_aes_context *ctx )
{
    memset( ctx, 0, sizeof( mbedtls_aes_context ) );

}


void mbedtls_aes_free( mbedtls_aes_context *ctx )
{
    if( ctx == NULL )
        return;
    /* Force the CRYP Periheral Clock Reset */
    __HAL_RCC_CRYP_FORCE_RESET();

    /* Release the CRYP Periheral Clock Reset */
    __HAL_RCC_CRYP_RELEASE_RESET();

    mbedtls_zeroize( ctx, sizeof( mbedtls_aes_context ) );
}


int mbedtls_aes_setkey_enc( mbedtls_aes_context *ctx, const unsigned char *key,
                            unsigned int keybits )
{
    int ret_val = 0;
    ret_val = aes_set_key(ctx, key, keybits);
    return(ret_val);
}

int mbedtls_aes_setkey_dec( mbedtls_aes_context *ctx, const unsigned char *key,
                            unsigned int keybits )
{
    int ret_val = 0;
    ret_val = aes_set_key(ctx, key, keybits);
    return( ret_val );
}


int mbedtls_aes_crypt_ecb( mbedtls_aes_context *ctx,
                    int mode,
                    const unsigned char input[16],
                    unsigned char output[16] )
{

    /* allow multi-instance of CRYP use: restore context for CRYP hw module */
    ctx->hcryp_aes.Instance->CR = ctx->ctx_save_cr;
    ctx->hcryp_aes.Phase = HAL_CRYP_PHASE_READY;

    if(mode == MBEDTLS_AES_DECRYPT) { /* AES decryption */
        ctx->hcryp_aes.Init.DataType = CRYP_DATATYPE_8B;
        ctx->hcryp_aes.Init.pKey = ctx->aes_key;
        mbedtls_aes_decrypt( ctx, input, output );
    } else { /* AES encryption */
        ctx->hcryp_aes.Init.DataType = CRYP_DATATYPE_8B;
        ctx->hcryp_aes.Init.pKey = ctx->aes_key;
        mbedtls_aes_encrypt( ctx, input, output );
    }
    /* allow multi-instance of CRYP use: save context for CRYP HW module CR */
    ctx->ctx_save_cr = ctx->hcryp_aes.Instance->CR;

    return( 0 );
}

#if defined(MBEDTLS_CIPHER_MODE_CBC)
#if defined (TARGET_STM32L486xG)
static int st_cbc_restore_context(mbedtls_aes_context *ctx){
    uint32_t tickstart;
    tickstart = HAL_GetTick();
    while((ctx->hcryp_aes.Instance->SR & AES_SR_BUSY) != 0){
        if ((HAL_GetTick() - tickstart) > ST_AES_TIMEOUT) {
            return 1; // timeout: CRYP processor is busy
        }
    }
    /* allow multi-instance of CRYP use: restore context for CRYP hw module */
    ctx->hcryp_aes.Instance->CR = ctx->ctx_save_cr;
    return 0;
}
static int st_cbc_save_context(mbedtls_aes_context *ctx){
    uint32_t tickstart;
    
    tickstart = HAL_GetTick();
    while((ctx->hcryp_aes.Instance->SR & AES_SR_BUSY) != 0){
        if ((HAL_GetTick() - tickstart) > ST_AES_TIMEOUT) {
            return 1; // timeout: CRYP processor is busy
        }
    }
    /* allow multi-instance of CRYP use: save context for CRYP HW module CR */
    ctx->ctx_save_cr = ctx->hcryp_aes.Instance->CR;

    return 0;
}
static int st_hal_cryp_cbc( mbedtls_aes_context *ctx, uint32_t opmode, size_t length, 
                            unsigned char iv[16], uint8_t *input, uint8_t *output) 
{
    int status = 0;
    ctx->hcryp_aes.Init.pInitVect = &iv[0]; // used in process, not in the init
    /* At this moment only, we know we have CBC mode: Re-initialize AES
        IP with proper parameters and apply key and IV for multi context usecase */
    if (HAL_CRYP_DeInit(&ctx->hcryp_aes) != HAL_OK)
        return HAL_ERROR;
    ctx->hcryp_aes.Init.OperatingMode = opmode;
    ctx->hcryp_aes.Init.ChainingMode = CRYP_CHAINMODE_AES_CBC;
    ctx->hcryp_aes.Init.KeyWriteFlag = CRYP_KEY_WRITE_ENABLE;
    if (HAL_CRYP_Init(&ctx->hcryp_aes) != HAL_OK)
        return HAL_ERROR;

    status =  HAL_CRYPEx_AES(&ctx->hcryp_aes, input, length, output, 10);

    return status;
}
#else
static int st_cbc_restore_context(mbedtls_aes_context *ctx){
    uint32_t tickstart;
    tickstart = HAL_GetTick();
    while((ctx->hcryp_aes.Instance->SR & (CRYP_SR_IFEM | CRYP_SR_OFNE | CRYP_SR_BUSY)) != CRYP_SR_IFEM){
        if ((HAL_GetTick() - tickstart) > ST_AES_TIMEOUT) {
            return 1; // timeout: CRYP processor is busy
        }
    }
    ctx->hcryp_aes.Instance->CR &= ~CRYP_CR_CRYPEN;
    /* save initvector for multi-instance use of CRYP */
    ctx->hcryp_aes.Instance->IV1RR = ctx->save_iv[3];
    ctx->hcryp_aes.Instance->IV1LR = ctx->save_iv[2];
    ctx->hcryp_aes.Instance->IV0RR = ctx->save_iv[1];
    ctx->hcryp_aes.Instance->IV0LR = ctx->save_iv[0];
    ctx->hcryp_aes.Phase = HAL_CRYP_PHASE_READY;
    /* allow multi-instance of CRYP use: restore context for CRYP hw module */
    ctx->hcryp_aes.Instance->CR = ctx->ctx_save_cr;
    return 0;
}
static int st_cbc_save_context(mbedtls_aes_context *ctx){
    uint32_t tickstart;
    tickstart = HAL_GetTick();
    while((ctx->hcryp_aes.Instance->SR & (CRYP_SR_IFEM | CRYP_SR_OFNE | CRYP_SR_BUSY)) != CRYP_SR_IFEM){
        if ((HAL_GetTick() - tickstart) > ST_AES_TIMEOUT) {
            return 1; // timeout: CRYP processor is busy
        }
    }
    /* allow multi-instance of CRYP use: save context for CRYP HW module CR */
    ctx->ctx_save_cr = ctx->hcryp_aes.Instance->CR;
    ctx->hcryp_aes.Instance->CR &= ~CRYP_CR_CRYPEN;
    /* save initvector for multi-instance use of CRYP */
    ctx->save_iv[3] = ctx->hcryp_aes.Instance->IV1RR;
    ctx->save_iv[2] = ctx->hcryp_aes.Instance->IV1LR;
    ctx->save_iv[1] = ctx->hcryp_aes.Instance->IV0RR;
    ctx->save_iv[0] = ctx->hcryp_aes.Instance->IV0LR;
    if ((ctx->ctx_save_cr & CRYP_CR_CRYPEN) == CRYP_CR_CRYPEN) {
        ctx->hcryp_aes.Instance->CR &= CRYP_CR_CRYPEN;
    }
    return 0;
}
#endif /* TARGET_STM32L486xG */
int mbedtls_aes_crypt_cbc( mbedtls_aes_context *ctx,
                    int mode,
                    size_t length,
                    unsigned char iv[16],
                    const unsigned char *input,
                    unsigned char *output )
{
    int status = 0;
    uint32_t *iv_ptr = (uint32_t *)&iv[0];
    if( length % 16 )
        return( MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH );
    ctx->hcryp_aes.Init.pInitVect = &iv[0];
    status |= st_cbc_restore_context(ctx);
#if defined (TARGET_STM32L486xG)
    if( mode == MBEDTLS_AES_DECRYPT ) {
        status |= st_hal_cryp_cbc(ctx, CRYP_ALGOMODE_KEYDERIVATION_DECRYPT, length, iv, (uint8_t *)input, (uint8_t *)output);
        // update IV
        uint32_t tickstart;
        tickstart = HAL_GetTick();
        while((ctx->hcryp_aes.Instance->SR & AES_SR_BUSY) != 0){
            if ((HAL_GetTick() - tickstart) > ST_AES_TIMEOUT) {
                return 1; // timeout: CRYP processor is busy
            }
        }
        ctx->ctx_save_cr = ctx->hcryp_aes.Instance->CR;
        ctx->hcryp_aes.Instance->CR &= ~AES_CR_EN;
        *iv_ptr++ = ctx->hcryp_aes.Instance->IVR3;
        *iv_ptr++ = ctx->hcryp_aes.Instance->IVR2;
        *iv_ptr++ = ctx->hcryp_aes.Instance->IVR1;
        *iv_ptr++ = ctx->hcryp_aes.Instance->IVR0;

    } else {
        status |= st_hal_cryp_cbc(ctx, CRYP_ALGOMODE_ENCRYPT, length, iv, (uint8_t *)input, (uint8_t *)output);
        memcpy( iv, output, 16 );
    }
#else
    
    if( mode == MBEDTLS_AES_DECRYPT ) {
        status |= HAL_CRYP_AESCBC_Decrypt(&ctx->hcryp_aes, (uint8_t *)input, length, (uint8_t *)output, 10);
    } else {
        status |= HAL_CRYP_AESCBC_Encrypt(&ctx->hcryp_aes, (uint8_t *)input, length, (uint8_t *)output, 10);
    }
#endif
    status |= st_cbc_save_context(ctx);

    return( status );
}
#endif /* MBEDTLS_CIPHER_MODE_CBC */

#if defined(MBEDTLS_CIPHER_MODE_CFB)
int mbedtls_aes_crypt_cfb128( mbedtls_aes_context *ctx,
                       int mode,
                       size_t length,
                       size_t *iv_off,
                       unsigned char iv[16],
                       const unsigned char *input,
                       unsigned char *output )
{
    int c;
    size_t n = *iv_off;

    if( mode == MBEDTLS_AES_DECRYPT ) {
        while( length-- ) {
            if( n == 0 )
                mbedtls_aes_crypt_ecb( ctx, MBEDTLS_AES_ENCRYPT, iv, iv );

            c = *input++;
            *output++ = (unsigned char)( c ^ iv[n] );
            iv[n] = (unsigned char) c;

            n = ( n + 1 ) & 0x0F;
        }
    } else {
        while( length-- ) {
            if( n == 0 )
                mbedtls_aes_crypt_ecb( ctx, MBEDTLS_AES_ENCRYPT, iv, iv );

            iv[n] = *output++ = (unsigned char)( iv[n] ^ *input++ );

            n = ( n + 1 ) & 0x0F;
        }
    }

    *iv_off = n;

    return( 0 );
}


int mbedtls_aes_crypt_cfb8( mbedtls_aes_context *ctx,
                    int mode,
                    size_t length,
                    unsigned char iv[16],
                    const unsigned char *input,
                    unsigned char *output )
{
    unsigned char c;
    unsigned char ov[17];

    while( length-- ) {
        memcpy( ov, iv, 16 );
        mbedtls_aes_crypt_ecb( ctx, MBEDTLS_AES_ENCRYPT, iv, iv );

        if( mode == MBEDTLS_AES_DECRYPT )
            ov[16] = *input;

        c = *output++ = (unsigned char)( iv[0] ^ *input++ );

        if( mode == MBEDTLS_AES_ENCRYPT )
            ov[16] = c;

        memcpy( iv, ov + 1, 16 );
    }

    return( 0 );
}

#endif /*MBEDTLS_CIPHER_MODE_CFB */

#if defined(MBEDTLS_CIPHER_MODE_CTR)
int mbedtls_aes_crypt_ctr( mbedtls_aes_context *ctx,
                       size_t length,
                       size_t *nc_off,
                       unsigned char nonce_counter[16],
                       unsigned char stream_block[16],
                       const unsigned char *input,
                       unsigned char *output )
{
    int c, i;
    size_t n = *nc_off;

    while( length-- )
    {
        if( n == 0 ) {
            mbedtls_aes_crypt_ecb( ctx, MBEDTLS_AES_ENCRYPT, nonce_counter, stream_block );

            for( i = 16; i > 0; i-- )
                if( ++nonce_counter[i - 1] != 0 )
                    break;
        }
        c = *input++;
        *output++ = (unsigned char)( c ^ stream_block[n] );

        n = ( n + 1 ) & 0x0F;
    }

    *nc_off = n;

    return( 0 );
}
#endif /* MBEDTLS_CIPHER_MODE_CTR */

void mbedtls_aes_encrypt( mbedtls_aes_context *ctx,
                          const unsigned char input[16],
                          unsigned char output[16] )
{
    if (HAL_CRYP_AESECB_Encrypt(&ctx->hcryp_aes, (uint8_t *)input, 16, (uint8_t *)output, 10) !=0) {
        // error found to be returned
    }

}

void mbedtls_aes_decrypt( mbedtls_aes_context *ctx,
                          const unsigned char input[16],
                          unsigned char output[16] )
{
    if(HAL_CRYP_AESECB_Decrypt(&ctx->hcryp_aes, (uint8_t *)input, 16, (uint8_t *)output, 10)) {
        // error found to be returned
    }
}


#endif /*MBEDTLS_AES_ALT*/
