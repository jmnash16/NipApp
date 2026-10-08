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
 * @brief   Demux for NippApp: demux paths that accept no filters yet, since no tuner delivers
 *          data. Milestone 2 replaces this with section and PID filtering of the TS input.
 * @file    stbhwdmx.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <string.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwdef.h"
#include "stbhwdmx.h"

/*---local typedef structs for this file--------------------------------------*/
typedef struct
{
   U16BIT caps;
   E_STB_DMX_DEMUX_SOURCE source;
   U8BIT param;
} S_DEMUX;

/*---local (static) variable declarations for this file-----------------------*/
static U8BIT num_paths = 0;
static S_DEMUX demuxes[NUM_DEMUX_PATHS];

/*---global function definitions---------------------------------------------*/

void STB_DMXInitialise(U8BIT paths, BOOLEAN inc_pes_collection)
{
   U8BIT i;

   FUNCTION_START(STB_DMXInitialise);

   USE_UNWANTED_PARAM(inc_pes_collection);

   num_paths = (paths <= NUM_DEMUX_PATHS) ? paths : NUM_DEMUX_PATHS;
   memset(demuxes, 0, sizeof(demuxes));

   /* Every demux can monitor SI from a tuner; none decodes or records */
   for (i = 0; i < num_paths; i++)
   {
      demuxes[i].caps = DMX_CAPS_MONITOR_SI;
   }
   if (num_paths > 0)
   {
      demuxes[0].caps |= DMX_CAPS_LIVE;
   }

   FUNCTION_FINISH(STB_DMXInitialise);
}

U16BIT STB_DMXGetCapabilities(U8BIT path)
{
   return (path < num_paths) ? demuxes[path].caps : 0;
}

void STB_DMXSetDemuxSource(U8BIT path, E_STB_DMX_DEMUX_SOURCE source, U8BIT param)
{
   if (path < num_paths)
   {
      demuxes[path].source = source;
      demuxes[path].param = param;
   }
}

void STB_DMXGetDemuxSource(U8BIT path, E_STB_DMX_DEMUX_SOURCE *source, U8BIT *param)
{
   if (path < num_paths)
   {
      *source = demuxes[path].source;
      *param = demuxes[path].param;
   }
}

U16BIT STB_DMXGrabPIDFilter(U8BIT path, U16BIT pid, FILTER_CALLBACK func)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pid);
   USE_UNWANTED_PARAM(func);
   return STB_DMX_PID_FILTER_INVALID;
}

void STB_DMXReleasePIDFilter(U8BIT path, U16BIT pfilt_id)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pfilt_id);
}

void STB_DMXStartPIDFilter(U8BIT path, U16BIT pfilt_id)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pfilt_id);
}

void STB_DMXStopPIDFilter(U8BIT path, U16BIT pfilt_id)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pfilt_id);
}

BOOLEAN STB_DMXCopyPIDFilterSect(U8BIT path, U8BIT *buffer, U16BIT size, U16BIT pfilt_id)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(buffer);
   USE_UNWANTED_PARAM(size);
   USE_UNWANTED_PARAM(pfilt_id);
   return FALSE;
}

void STB_DMXSkipPIDFilterSect(U8BIT path, U16BIT pfilt_id)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pfilt_id);
}

U16BIT STB_DMXGrabSectFilter(U8BIT path, U16BIT pfilt_id)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pfilt_id);
   return STB_DMX_SECT_FILTER_INVALID;
}

void STB_DMXReleaseSectFilter(U8BIT path, U16BIT sfilt_id)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(sfilt_id);
}

void STB_DMXSetupSectFilter(U8BIT path, U16BIT sfilt_id, U8BIT *match_ptr, U8BIT *mask_ptr,
   U8BIT not_equal_byte_index, BOOLEAN crc)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(sfilt_id);
   USE_UNWANTED_PARAM(match_ptr);
   USE_UNWANTED_PARAM(mask_ptr);
   USE_UNWANTED_PARAM(not_equal_byte_index);
   USE_UNWANTED_PARAM(crc);
}

/*---Decoding and teletext: NippApp decodes nothing---------------------------------------------------*/

void STB_DMXChangeDecodePIDs(U8BIT path, U16BIT pcr_pid, U16BIT video_pid, U16BIT audio_pid,
   U16BIT text_pid, U16BIT data_pid, U16BIT ad_pid)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pcr_pid);
   USE_UNWANTED_PARAM(video_pid);
   USE_UNWANTED_PARAM(audio_pid);
   USE_UNWANTED_PARAM(text_pid);
   USE_UNWANTED_PARAM(data_pid);
   USE_UNWANTED_PARAM(ad_pid);
}

void STB_DMXChangeTextPID(U8BIT path, U16BIT text_pid)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(text_pid);
}

void STB_DMXReadTextPES(U8BIT path, U8BIT **buffer, U32BIT *num_bytes)
{
   USE_UNWANTED_PARAM(path);
   *buffer = NULL;
   *num_bytes = 0;
}

/******************************************************************************
 * End of file
 *****************************************************************************/
