/*******************************************************************************
 * Copyright © 2014 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2004 Ocean Blue Software Ltd
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
 * @brief   Blank description
 * @file    celo.h
 * @date    01/12/2004
 * @author  Ocean Blue
 */
/* (c) Copyright Ocean Blue Software Ltd. - ALL RIGHTS RESERVED             */
/* This document is the CONFIDENTIAL property of Ocean Blue Software Ltd.   */
/****************************************************************************/
/*                                                                          */
/*   FILE:    celo.h                                                        */
/*   DATE:    18/12/08                                                      */
/*   PURPOSE: Header File for CI+  Cryptographic Functions                  */
/*                                                                          */
/****************************************************************************/
#ifndef __CELO_H
#define __CELO_H

#include "techtype.h"

/*--------------------------------------------------------------------------*/
/* Symbol Definitions                                                       */
/*--------------------------------------------------------------------------*/

#define CE_AES128_KEY_SIZE    16
#define CE_AES128_BLOCK_SIZE  16
#define CE_SHA1_DIGEST_SIZE   20
#define CE_SHA256_DIGEST_SIZE 32

/*--------------------------------------------------------------------------*/
/* Macros Definitions                                                       */
/*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*/
/* Structures Definitions                                                   */
/*--------------------------------------------------------------------------*/

typedef struct aes128_key
{
   U32BIT w[44];
} S_AES128_KEY;

/*--------------------------------------------------------------------------*/
/* Function Prototypes                                                      */
/*--------------------------------------------------------------------------*/

/*!**************************************************************************
 * @brief    Convert raw AES-128 key to encryption key
 * @param    key - raw AES-128 key (128 bits = 16 bytes)
 * @param    aes_key - encryption key
 ****************************************************************************/
void CE_Aes128SetEncryptKey(U8BIT *key, S_AES128_KEY *aes_key);

/*!**************************************************************************
 * @brief    Apply AES-128 encryption on a block
 * @param    plaintext - buffer to encrypt (128 bits = 16 bytes)
 * @param    key - encryption key
 * @param    ciphertext - result of encryption (can be the same as plaintext)
 ****************************************************************************/
void CE_Aes128Encrypt(U8BIT *plaintext, S_AES128_KEY *key, U8BIT *ciphertext);

/*!**************************************************************************
 * @brief    Convert raw AES-128 key to decryption key
 * @param    key - raw AES-128 key (128 bits = 16 bytes)
 * @param    aes_key - decryption key
 ****************************************************************************/
void CE_Aes128SetDecryptKey(U8BIT *key, S_AES128_KEY *ce_key);

/*!**************************************************************************
 * @brief    Apply AES-128 decryption on a buffer
 * @param    ciphertext - buffer to decrypt (128 bits = 16 bytes)
 * @param    key - decryption key
 * @param    plaintext - result of decryption (can be the same as ciphertext)
 ****************************************************************************/
void CE_Aes128Decrypt(U8BIT *ciphertext, S_AES128_KEY *key, U8BIT *plaintext);

/*!**************************************************************************
 * @brief    Apply AES-128-CBC encryption on a buffer
 * @param    plaintext - buffer to encrypt
 * @param    length - length of buffer in bytes
 * @param    key - encryption key
 * @param    iv - initialisation vector
 * @param    ciphertext - result of encryption (can be the same as plaintext)
 ****************************************************************************/
void CE_Aes128EncryptCbc(U8BIT *plaintext, U32BIT length, S_AES128_KEY *key,
   U8BIT *iv, U8BIT *ciphertext);

/*!**************************************************************************
 * @brief    Apply AES-128-CBC decryption on a buffer
 * @param    ciphertext - buffer to decrypt
 * @param    length - length of buffer in bytes
 * @param    key - decryption key
 * @param    iv - initialisation vector
 * @param    plaintext - result of decryption (can be the same as ciphertext)
 ****************************************************************************/
void CE_Aes128DecryptCbc(U8BIT *ciphertext, U32BIT length, S_AES128_KEY *key,
   U8BIT *iv, U8BIT *plaintext);

/*!**************************************************************************
 * @brief    Calculate SHA-1 digest of a buffer
 * @param    buffer - data buffer
 * @param    length - length of buffer in bytes
 * @param    digest - SHA-1 digest of buffer
 ****************************************************************************/
void CE_Sha1(U8BIT *message, U32BIT length, U8BIT *digest);

/*!**************************************************************************
 * @brief    Calculate SHA-256 digest of a buffer
 * @param    buffer - data buffer
 * @param    length - length of buffer in bytes
 * @param    digest - SHA-256 digest of buffer
 ****************************************************************************/
void CE_Sha256(U8BIT *message, U32BIT length, U8BIT *digest);

#endif   /* !__CELO_H */
