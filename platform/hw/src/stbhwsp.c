/*******************************************************************************
 * Copyright © 2014 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2007 Ocean Blue Software Ltd
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
 * @brief   Set Top Box - STB810 Hardware Layer, Serial Port (Debug) functions
 * @file    stbhwsp.c
 * @date    October 2007
 * @author  Dave Carter
 */
//#define DBG_ADD_TIMING

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* third party header files */
/* DVBCore header files*/
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwc.h"

/*---constant definitions for this file---------------------------------------*/
#define BAUD_RATE_LUT_SIZE 8

/*---macro definitions for this file------------------------------------------*/

/*---local typedef structs for this file--------------------------------------*/

/*---local (static) variable declarations for this file-----------------------*/
/*   (internal variables declared static to make them local) */
static char debug_msg_buff[1024];

/*---local function prototypes for this file----------------------------------*/
/*   (internal functions declared static to make them local) */

/*---local function definitions-----------------------------------------------*/


/*---global function definitions----------------------------------------------*/


/**
 * @brief   Write debug string to serial/debug port. <CR><LF> characters will be automatically
 *          added to the end of the string
 * @param   format string & format
 */
void STB_SPDebugWrite(const char *format, ... )
{
   va_list vparams;
#ifdef DBG_ADD_TIMING
   U16BIT len;
#endif

   ASSERT(format != NULL);

   va_start(vparams, format);
   vsprintf(debug_msg_buff, format, vparams);
   va_end(vparams);

#ifdef DBG_ADD_TIMING
   len = strlen(debug_msg_buff);
   sprintf(&debug_msg_buff[len], " [%d]",STB_OSGetClockMilliseconds());
#endif

   printf("%s\n", debug_msg_buff);
   fflush(stdout);
}

/**
 * @brief   Writes debug string to the serial port without <CR><LF>
 * @param   format string & format
 */
void STB_SPDebugNoCnWrite(const char *format, ... )
{
   va_list vparams;

   FUNCTION_START(STB_SPDebugWrite);

   ASSERT(format != NULL);

   va_start(vparams, format);
   vsprintf(debug_msg_buff, format, vparams);
   va_end(vparams);

   printf("%s", debug_msg_buff);
   fflush(stdout);

   FUNCTION_FINISH(STB_SPDebugWrite);
}

/**
 * @brief   Report Assertion failure
 * @param   file name of source file
 * @param   line line number of source file
 * @param   eval_str evaluation string that failed
 */
void STB_SPDebugAssertFail(const char *file, int line, const char *eval_str)
{
   FUNCTION_START(STB_SPDebugAssertFail);
   STB_SPDebugNoCnWrite("ASSERT FAILURE at %s:%d (%s)\n",file,line,eval_str);
   FUNCTION_FINISH(STB_SPDebugAssertFail);
}

#if 0
/* Stub functions not used on this platform to maintain API */
void STB_SPInitialise(void)
{
   FUNCTION_START(STB_SPInitialise);
   FUNCTION_FINISH(STB_SPInitialise);
}

BOOLEAN STB_SPOpen(U32BIT baud, E_STB_SP_DATA_MODE data, E_STB_SP_STOP_MODE stop, E_STB_SP_HANDSHAKING handshaking)
{
   FUNCTION_START(STB_SPOpen);
   USE_UNWANTED_PARAM(baud);
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(stop);
   USE_UNWANTED_PARAM(handshaking);
   FUNCTION_FINISH(STB_SPOpen);
   return(TRUE);
}

void STB_SPClose(void)
{
   FUNCTION_START(STB_SPClose);
   FUNCTION_FINISH(STB_SPClose);
}

U32BIT STB_SPEnquireBaud(U32BIT baud)
{
   FUNCTION_START(STB_SPEnquireBaud);
   FUNCTION_FINISH(STB_SPEnquireBaud);
   return(baud);
}

void STB_SPWrite(U8BIT *data, U32BIT len, U32BIT timeout, U32BIT *written)
{
   FUNCTION_START(STB_SPWrite);
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(len);
   USE_UNWANTED_PARAM(timeout);
   USE_UNWANTED_PARAM(written);
   FUNCTION_FINISH(STB_SPWrite);
}

void STB_SPRead(U8BIT *data, U32BIT len, U32BIT timeout, U32BIT *read)
{
   FUNCTION_START(STB_SPRead);
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(len);
   USE_UNWANTED_PARAM(timeout);
   USE_UNWANTED_PARAM(read);
   FUNCTION_FINISH(STB_SPRead);
}

BOOLEAN STB_SPOpenAux(U32BIT baud, E_STB_SP_DATA_MODE data, E_STB_SP_STOP_MODE stop,
   E_STB_SP_HANDSHAKING handshaking)
{
   USE_UNWANTED_PARAM(baud);
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(stop);
   USE_UNWANTED_PARAM(handshaking);
   return(FALSE);
}

void STB_SPCloseAux(void)
{
}

void STB_SPWriteAux(U8BIT *data, U32BIT len, U32BIT timeout, U32BIT *written)
{
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(len);
   USE_UNWANTED_PARAM(timeout);
   USE_UNWANTED_PARAM(written);
}

void STB_SPReadAux(U8BIT *data, U32BIT len, U32BIT timeout, U32BIT *read)
{
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(len);
   USE_UNWANTED_PARAM(timeout);
   USE_UNWANTED_PARAM(read);
}

#endif

/******************************************************************************
** End of file
*******************************************************************************/
