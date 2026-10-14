/*******************************************************************************
 * Copyright © 2026 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 *
 * This file is part of a DTVKit Software Component
 * You are permitted to copy, modify or distribute this file subject to the terms
 * of the DTVKit 1.0 Licence which can be found in licence.txt or at www.dtvkit.org
 *
 * THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 *******************************************************************************/
/**
 * @brief   NippApp content decoding: broadcast files can be sent compressed, as the FDT's
 *          Content-Encoding says (RFC 6726 clause 3.4.1: ZLIB, DEFLATE or GZIP). SES sends its
 *          content guide schedule as gzip. NippApp serves and keeps the decoded content, so that
 *          DVB-I documents can still be rewritten and every client can read them.
 * @file    encoding.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

/* STB header files */
#include "techtype.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
/* The most a file may decode to */
#define MAX_DECODED_SIZE      (64 * 1024 * 1024)

/*---global function definitions---------------------------------------------*/

BOOLEAN ENC_Decode(const char *encoding, const U8BIT *data, U32BIT len, U8BIT **decoded, U32BIT *decoded_len)
{
   z_stream stream;
   U8BIT *out, *bigger;
   size_t size;
   int window_bits;
   int ret;

   *decoded = NULL;
   *decoded_len = 0;

   /* zlib and gzip are recognised from their headers; DEFLATE is raw deflate */
   if ((strcasecmp(encoding, "gzip") == 0) || (strcasecmp(encoding, "x-gzip") == 0) ||
       (strcasecmp(encoding, "zlib") == 0))
   {
      window_bits = 15 + 32;
   }
   else if (strcasecmp(encoding, "deflate") == 0)
   {
      window_bits = -15;
   }
   else
   {
      return FALSE;
   }

   memset(&stream, 0, sizeof(stream));
   if (inflateInit2(&stream, window_bits) != Z_OK)
   {
      return FALSE;
   }

   size = (len < MAX_DECODED_SIZE / 8) ? (size_t)len * 8 + 1024 : MAX_DECODED_SIZE;
   out = (U8BIT *)malloc(size + 1);
   stream.next_in = (Bytef *)data;
   stream.avail_in = len;
   ret = Z_OK;
   while ((out != NULL) && (ret == Z_OK))
   {
      stream.next_out = out + stream.total_out;
      stream.avail_out = (uInt)(size - stream.total_out);
      ret = inflate(&stream, Z_NO_FLUSH);
      if ((ret == Z_OK) && (stream.avail_out == 0))
      {
         if (size >= MAX_DECODED_SIZE)
         {
            ret = Z_MEM_ERROR;
            break;
         }
         size = (size * 2 < MAX_DECODED_SIZE) ? size * 2 : MAX_DECODED_SIZE;
         bigger = (U8BIT *)realloc(out, size + 1);
         if (bigger == NULL)
         {
            ret = Z_MEM_ERROR;
            break;
         }
         out = bigger;
      }
      else if ((ret == Z_BUF_ERROR) && (stream.avail_in == 0))
      {
         /* truncated */
         break;
      }
   }
   inflateEnd(&stream);

   if ((out == NULL) || (ret != Z_STREAM_END))
   {
      free(out);
      return FALSE;
   }
   out[stream.total_out] = '\0';
   *decoded = out;
   *decoded_len = (U32BIT)stream.total_out;
   return TRUE;
}

/******************************************************************************
 * End of file
 *****************************************************************************/
