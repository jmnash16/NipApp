/*******************************************************************************
 * Copyright © 2014 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2006 Ocean Blue Software Ltd
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
 * @brief   Set Top Box - System interface for events
 * @file    stbos_event.c
 * @date    May 2006
 * @author  Ocean Blue
 */

/* STB Header Files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwos.h"

/*!- Local MACRO Definitions */

/*!- Select-Deselect Local Debug Text Output */
/*#define  EVENT_DEBUG*/

#ifndef  EVENT_DEBUG
#define  EVT_DBG(X)
#else
#define  EVT_DBG(X)    STB_SPDebugWrite X
#endif


/* Local CONSTANT  Definitions */

/* Local ENUM/TYPE Definitions */

/* Local VARIABLE  Declarations */

static void (*callback_func)(BOOLEAN, U16BIT, U16BIT, void *, U32BIT) = NULL;

/* Local PROTOTYPE Declarations */


/**
 * @brief   Register the function that will be called when STB_OSSendEvent is used.
 * @param   func callback function
 */
void  STB_OSRegisterCallback(void (*func)(BOOLEAN, U16BIT, U16BIT, void *, U32BIT))
{
   FUNCTION_START(STB_OSRegisterCallback);

   callback_func = func;

   EVT_DBG(("STB_OSRegisterCallback: Event handler = %p", func));

   FUNCTION_FINISH(STB_OSRegisterCallback);
}

/**
 * @brief   Send an event by calling the registered callback function
 * @param   repeat TRUE if the event is a repeat of the last event
 * @param   event_class event class
 * @param   event_type event identifier.
 * @param   data pointer to the data associated with the event
 * @param   data_size size of the data pointed by data_pointer
 */
void STB_OSSendEvent(BOOLEAN repeat, U16BIT event_class, U16BIT event_type, void *data, U32BIT data_size)
{
   FUNCTION_START(STB_OSSendEvent);

   if (callback_func != NULL)
   {
      EVT_DBG(("STB_OSSendEvent(%u, 0x%04x, 0x%04x, %p, %lu)",
               repeat, event_class, event_type, data, data_size));

      (*callback_func)(repeat, event_class, event_type, data, data_size);
   }
   else
   {
      EVT_DBG(("STB_OSSendEvent(%u, 0x%04x, 0x%04x, %p, %lu): No event handler installed!",
               repeat, event_class, event_type, data, data_size));
   }

   FUNCTION_FINISH(STB_OSSendEvent);
}

