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
 * @brief   Header file - Platform Specific Hardware Definitions
 * @file    stbhwdef.h
 * @date    27/02/2001
 */

#ifndef _STBHWDEF_H
#define _STBHWDEF_H

//---Constant and macro definitions for public use-----------------------------
#define KBYTES 1024

#define SD_WIDTH                 720
#define SD_HEIGHT                576
#define HD_WIDTH                 1280
#define HD_HEIGHT                720
#define FULL_HD_WIDTH            1920
#define FULL_HD_HEIGHT           1080

// physical properties of the lcd screen
#define SCREEN_WIDTH             FULL_HD_WIDTH
#define SCREEN_HEIGHT            FULL_HD_HEIGHT

#ifdef OSD_16_BIT
#define SCREEN_COLOUR_DEPTH      16
#else
#define SCREEN_COLOUR_DEPTH      32
#endif

#define DEFAULT_VIDEO_FORMAT     VIDEO_FORMAT_AUTO

#define SUBTITLES_AND_GRAPHICS   FALSE

#define HD_SCALE_W(x)            (((x * SCREEN_WIDTH) + (FULL_HD_WIDTH / 2)) / FULL_HD_WIDTH)
#define HD_SCALE_H(x)            (((x * SCREEN_HEIGHT) + (FULL_HD_HEIGHT / 2)) / FULL_HD_HEIGHT)

/* Manufacturer string required by MHEG */
#define MANUFACTURER   "OBSLNX100"

/* Task config */
#define MHEG_TASK_PRIORITY    6

// PATH SPECIFIC CONFIGURATION
#if 1 /* NippApp: two file tuner paths, so two carriers can be served at once */
#define NUM_TUNER_PATHS       2
#else
#define NUM_TUNER_PATHS       3
#endif

#define NUM_DEMUX_PATHS       3
#define NUM_RECORDERS         (NUM_DEMUX_PATHS - 2)   /* One live demux and one playback */
#define NUM_DECODE_PATHS      1
#define NUM_CI_SLOTS          1

#define NUM_TUNER_RETRIES     1

// NV SPECIFIC CONFIGURATION
#define NV_DEVICE_SIZE           (128 * KBYTES)    /* Amount of NV flash used by the application */

//---Enumerations for public use-----------------------------------------------

//---Global type defs for public use-------------------------------------------

//---Global Function prototypes for public use---------------------------------

#endif
