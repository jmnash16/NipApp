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
 * @brief   NippApp: handler for events sent by DVBCore
 * @file    events.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <stdio.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stberc.h"

#include "nippapp.h"

/*---global function definitions---------------------------------------------*/

void NIPPAPP_EventHandler(U32BIT event, void *event_data, U32BIT data_size)
{
   USE_UNWANTED_PARAM(event_data);
   USE_UNWANTED_PARAM(data_size);

   /* Search, tuning and NIP events are handled from milestone 2; for now they are logged */
   switch (event)
   {
      case STB_EVENT_TUNE_LOCKED:
         printf("NippApp: tuner locked\n");
         break;
      case STB_EVENT_TUNE_NOTLOCKED:
         printf("NippApp: tuner not locked\n");
         break;
      case STB_EVENT_SEARCH_SUCCESS:
         printf("NippApp: search finished\n");
         break;
      case STB_EVENT_SEARCH_FAIL:
         printf("NippApp: search failed\n");
         break;
      default:
         break;
   }
}

/******************************************************************************
 * End of file
 *****************************************************************************/
