/*******************************************************************************
 * Copyright © 2014 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2011 Ocean Blue Software Ltd
 *
 * This file is part of a DTVKit Software Component
 * You are permitted to copy, modify or distribute this file subject to the terms
 * of the DTVKit 1.0 Licence which can be found in licence.txt or at www.dtvkit.org
 * 
 * THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * If you or your organisation is not a member of DTVKit then you have access
 * to this source code outside of the terms of the licence agreement
 * and you are expected to delete this and any associated files immediately.
 * Further information on DTVKit, membership and terms can be found at www.dtvkit.org
 *******************************************************************************/
/**
 * @brief   Encryption / decryption functions
 * @file    stbhwcrypt.c
 * @date    28/01/2011
 * @author  Steve Ford
 */

/*#define CRYPT_DEBUG*/

/*---includes for this file--------------------------------------------------*/
/* compiler library header files */
#include <stdio.h>
#include <string.h>

/* third party header files */

/* DVBCore header files */
#include <techtype.h>
#include <dbgfuncs.h>

#include "celo.h"

/*---constant definitions for this file--------------------------------------*/
#ifdef CRYPT_DEBUG
   #define DBG(x)      STB_SPDebugWrite x
#else
   #define DBG(x)
#endif

#if 0
#ifdef FUNCTION_START
#undef FUNCTION_START
#endif
#define FUNCTION_START(x)     printf(">>> %s\n", # x);

#ifdef FUNCTION_FINISH
#undef FUNCTION_FINISH
#endif
#define FUNCTION_FINISH(x)     printf("<<< %s\n", # x);
#endif

#define BLOCK_SIZE_BYTES   16
#define BLOCK_SIZE_MASK    ~0xfL

/*---local typedef structs for this file-------------------------------------*/

/*---local (static) variable declarations for this file----------------------*/

/*---local function prototypes for this file---------------------------------*/

/*---global function definitions---------------------------------------------*/

/**
 * @brief   Initialises encryption/decryption
 * @return  TRUE if initialisation is successful, FALSE otherwise
 */
BOOLEAN STB_CRYPTInitialise(void)
{
   FUNCTION_START(STB_CRYPTInitialise);
   FUNCTION_FINISH(STB_CRYPTInitialise);
   return TRUE;
}

/**
 * @brief   Apply AES-128 encryption on the buffer using the given key.
 *          If the length of the buffer is not a multiple of 128 bits the last
 *          incomplete block will not be encrypted.
 * @param   src buffer to be encrypted
 * @param   dest buffer to store encrypted data
 * @param   length length of buffer in bytes
 * @param   key AES key (16 bytes - 128 bits)
 * @note    src and dest may point to the same buffer
 */
void STB_CRYPTAesEncrypt(U8BIT *src, U8BIT *dest, U32BIT length, U8BIT *key)
{
   U32BIT block_length, offset;
   S_AES128_KEY aes_key;

   FUNCTION_START(STB_CRYPTAesEncrypt);

   CE_Aes128SetEncryptKey(key, &aes_key);

   block_length = length & BLOCK_SIZE_MASK;
   for (offset = 0; offset < block_length; offset += BLOCK_SIZE_BYTES)
   {
      CE_Aes128Encrypt(src, &aes_key, dest);
      src += BLOCK_SIZE_BYTES;
      dest += BLOCK_SIZE_BYTES;
   }

   /* Clear variables on the stack */
   memset(&aes_key, 0xbd, sizeof aes_key);
   block_length = offset = 0xbdbd;
   USE_UNWANTED_PARAM(block_length);
   USE_UNWANTED_PARAM(offset);

   FUNCTION_FINISH(STB_CRYPTAesEncrypt);
}

/**
 * @brief   Apply AES-128 decryption on the buffer using the given key.
 *          If the length of the buffer is not a multiple of 128 bits the last
 *          incomplete block will not be decrypted.
 * @param   src buffer to be decrypted
 * @param   dest buffer to store decrypted data
 * @param   length length of buffer in bytes
 * @param   key AES key (16 bytes - 128 bits)
 * @note    src and dest may point to the same buffer
 */
void STB_CRYPTAesDecrypt(U8BIT *src, U8BIT *dest, U32BIT length, U8BIT *key)
{
   U32BIT block_length, offset;
   S_AES128_KEY aes_key;

   FUNCTION_START(STB_CRYPTAesDecrypt);

   CE_Aes128SetDecryptKey(key, &aes_key);

   block_length = length & BLOCK_SIZE_MASK;
   for (offset = 0; offset < block_length; offset += BLOCK_SIZE_BYTES)
   {
      CE_Aes128Decrypt(src, &aes_key, dest);
      src += BLOCK_SIZE_BYTES;
      dest += BLOCK_SIZE_BYTES;
   }

   /* Clear variables on the stack */
   memset(&aes_key, 0xbd, sizeof aes_key);
   block_length = offset = 0xbdbd;
   USE_UNWANTED_PARAM(block_length);
   USE_UNWANTED_PARAM(offset);

   FUNCTION_FINISH(STB_CRYPTAesDecrypt);
}

/**
 * @brief   Apply AES-128-CBC encryption on a buffer. The result of the encryption is stored in the
 *          input buffer.
 * @param   src buffer to be encrypted
 * @param   dest buffer to store encrypted data
 * @param   length length of buffer in bytes
 * @param   key AES key (128 bits = 16 bytes)
 * @param   iv AES initialisation vector (128 bits = 16 bytes)
 */
void STB_CRYPTAesCbcEncrypt(U8BIT *src, U8BIT *dest, U32BIT length, U8BIT *key, U8BIT *iv)
{
   S_AES128_KEY aes_key;
   U8BIT iv_copy[BLOCK_SIZE_BYTES];

   FUNCTION_START(STB_CRYPTAesCbcEncrypt);

   if (length % 16 == 0)
   {
      CE_Aes128SetEncryptKey(key, &aes_key);

      /* The contents of iv will be modified so take a copy and use that */
      memcpy(iv_copy, iv, BLOCK_SIZE_BYTES);

      CE_Aes128EncryptCbc(src, length, &aes_key, iv_copy, dest);

      /* Clear variable on the stack */
      memset(&aes_key, 0xbd, sizeof aes_key);
   }

   FUNCTION_FINISH(STB_CRYPTAesCbcEncrypt);
}

/**
 * @brief   Apply AES-128 decryption on a buffer. The result of the decryption is stored in the
 *          input buffer.
 * @param   src buffer to be decrypted
 * @param   dest buffer to store decrypted data
 * @param   length length of buffer in bytes
 * @param   key AES key (128 bits = 16 bytes)
 * @param   iv AES initialisation vector (128 bits = 16 bytes)
 */
void STB_CRYPTAesCbcDecrypt(U8BIT *src, U8BIT *dest, U32BIT length, U8BIT *key, U8BIT *iv)
{
   S_AES128_KEY aes_key;
   U8BIT iv_copy[BLOCK_SIZE_BYTES];

   FUNCTION_START(STB_CRYPTAesCbcDecrypt);

   if (length % 16 == 0)
   {
      CE_Aes128SetDecryptKey(key, &aes_key);

      /* The contents of iv will be modified so take a copy and use that */
      memcpy(iv_copy, iv, BLOCK_SIZE_BYTES);

      CE_Aes128DecryptCbc(src, length, &aes_key, iv_copy, dest);

      /* Clear variable on the stack */
      memset(&aes_key, 0xbd, sizeof aes_key);
   }

   FUNCTION_FINISH(STB_CRYPTAesCbcDecrypt);
}

/*---local function definitions----------------------------------------------*/


/****
 * End of file
 */

