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
 * @brief   Set Top Box - STB810 Hardware Layer, Platform functions
 * @file    stbhwplatform.c
 * @date    September 2007
 * @author  Dave Carter
 */

//#define DEBUG_PRINTING_ENABLED
/*#define  PLATFORM_DEBUG*/

/*---includes for this file--------------------------------------------------*/
/* compiler library header files */
#include <string.h>

/* third party header files */

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwdef.h"

/*---macro definitions for this file-----------------------------------------*/
#ifndef  PLATFORM_DEBUG
#define  PLAT_DBG(X)
#else
#define  PLAT_DBG(X)    STB_SPDebugWrite X
#endif

/*---constant definitions for this file--------------------------------------*/
#define  PLATFORM_OSD_AREA_MIN         1
#define  PLATFORM_OSD_AREA_NUM         3
#define  PLATFORM_OSD_AREA_MAX         24
#define  PLATFORM_HANDSET_ID           HANDSET_ID_PHILIPS_STB200

/*---local typedef structs for this file-------------------------------------*/

/*---local (static) variable declarations for this file----------------------*/
static U16BIT customer_ident = 0x0000;
static U16BIT hardware_ident = 0x0000;
static U8BIT oui[3] = {0x00, 0x01, 0x5a};
static const char c_sql_filename[] = "database.sql";

/*---local function prototypes for this file---------------------------------*/

/*---local function definitions----------------------------------------------*/

/*---global function definitions---------------------------------------------*/

/**
 * @brief   Queries the number of demux paths available
 * @return  The number of demux paths
 */
U8BIT  STB_HWGetDemuxPaths(void)
{
   U8BIT demux_paths;

   FUNCTION_START(STB_HWGetDemuxPaths);

   demux_paths = NUM_DEMUX_PATHS;

   FUNCTION_FINISH(STB_HWGetDemuxPaths);

   return demux_paths;
}

/**
 * @brief   Returns the platform hardware identifier code
 * @return  The platform id
 */
U16BIT  STB_HWGetHwId(void)
{
   U16BIT hwid_result = 0;

   FUNCTION_START(STB_HWGetHwId);

   hwid_result = hardware_ident;

   FUNCTION_FINISH(STB_HWGetHwId);

   return hwid_result;
}

/**
 * @brief   Returns the platform customer identifier code
 * @return  The platform id
 */
U16BIT  STB_HWGetCustomerId(void)
{
   U16BIT cusid_result = 0;

   FUNCTION_START(STB_HWGetCustomerId);

   cusid_result = customer_ident;

   FUNCTION_FINISH(STB_HWGetCustomerId);
   return cusid_result;
}

/**
 * @brief   Return the OUI for this device
 */
U8BIT* STB_HWGetOUI(void)
{
   FUNCTION_START(STB_HWGetOUI);
   FUNCTION_FINISH(STB_HWGetOUI);
   return(oui);
}

/**
 * @brief   Returns the number of front end (Tuner) paths on the platform
 * @return  The number of tuner paths
 */
U8BIT  STB_HWGetTunerPaths(void)
{
   U8BIT tuner_paths;

   FUNCTION_START(STB_HWGetTunerPaths);

   tuner_paths = NUM_TUNER_PATHS;

   FUNCTION_FINISH(STB_HWGetTunerPaths);

   return tuner_paths;
}

/**
 * @brief   Returns the number of recordings that can take place at the same time
 * @return  The number of recordings
 */
U8BIT STB_HWGetNumRecorders(void)
{
   U8BIT num_recs;

   FUNCTION_START(STB_HWGetNumRecorders);

   num_recs = NUM_RECORDERS;

   FUNCTION_FINISH(STB_HWGetNumRecorders);

   return num_recs;
}

/**
 * @brief   Returns the number of audio decoding paths on the platform
 * @return  The number of audio decode paths
 */
U8BIT STB_HWGetAudioDecodePaths(void)
{
   U8BIT decode_paths;

   FUNCTION_START(STB_HWGetAudioDecodePaths);

   decode_paths = NUM_DECODE_PATHS;

   FUNCTION_FINISH(STB_HWGetAudioDecodePaths);

   return decode_paths;
}

/**
 * @brief   Returns the number of video decoding paths on the platform
 * @return  The number of video decode paths
 */
U8BIT  STB_HWGetVideoDecodePaths(void)
{
   U8BIT decode_paths;

   FUNCTION_START(STB_HWGetVideoDecodePaths);

   decode_paths = NUM_DECODE_PATHS;

   FUNCTION_FINISH(STB_HWGetVideoDecodePaths);

   return decode_paths;
}

/**
 * @brief   Returns the serial number of the Set Top Box
 * @return  The serial number
 */
U32BIT STB_HWGetBoxSerialNumber(void)
{
   FUNCTION_START(STB_HWGetBoxSerialNumber);
   FUNCTION_FINISH(STB_HWGetBoxSerialNumber);
   return 0;
}

/**
 * @brief   Reset the board
 */
void STB_OSResetCPU(void)
{
}

/**
 * @brief   Returns the serial number of the Set Top Box
 * @return  The serial number
 */
U8BIT STB_HWGetNumCISlots(void)
{
   return(NUM_CI_SLOTS);
}

/**
 * @brief   Gives the name (optionally including path) of the SQL database file
 * @param   pathname - array into which the full path will be written
 * @param   max_pathname_len - size of the pathname array
 * @return  TRUE if the pathname is returned successfully.
 */
BOOLEAN STB_GetSqlFileName(U8BIT *pathname, U16BIT max_pathname_len)
{
   int len;
   BOOLEAN result;
   FUNCTION_START(STB_GetSqlFileName);
   len = strlen(c_sql_filename);
   DBGPRINT("len = %d, max= %d", len, max_pathname_len);
   if (len < max_pathname_len)
   {
      strcpy((char *)pathname, c_sql_filename);
      result = TRUE;
   }
   else
   {
      result = FALSE;
   }
   FUNCTION_FINISH(STB_GetSqlFileName);
   return result;
}

