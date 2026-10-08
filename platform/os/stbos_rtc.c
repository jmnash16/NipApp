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
 * @brief   Set Top Box - System Interface for Real Time Clock facility.
 * @file    stbos_rtc.c
 * @date    July 2006
 * @author  Ocean Blue
 */

#include <sys/time.h>

/* STB Header Files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "types_int.h"


/*!- Local MACRO Definitions */

/*!- Select-Deselect Local Debug Text Output */
/*#define  RTC_DEBUG*/

#ifndef  RTC_DEBUG
#define  RTC_DBG(X)
#else
#define  RTC_DBG(X)    STB_SPDebugWrite X
#endif


/* Local CONSTANT  Definitions */

#define RTC_TICKS_PER_SEC       1000

/* Local ENUM/TYPE Definitions */

/* Local VARIABLE  Declarations */

/* UTC Time in Seconds */
static U32BIT utc_seconds = 0;

/* System Time from when the Time was Last Set */
static U32BIT sync_time = 0;

/* Local PROTOTYPE Declarations */

static U32BIT SysBootTime(void);

/**
 * @brief   Allows setting of initial boot time.
 */
void STB_OSInitialise(void)
{
   /* Call SysBootTime at the very beginning to initialise the static
      variable first_tv inside of it */
   SysBootTime();
}

/**
 * @brief   Set the time in seconds since midnight 1-1-1970
 * @param   num_seconds time in seconds
 */
void STB_OSSetClockRTC(U32BIT num_seconds)
{
   FUNCTION_START(STB_OSSetClockRTC);

   utc_seconds = num_seconds;

   /* Save the system time at the point the clock has been set */
   sync_time = SysBootTime();

   RTC_DBG(("STB_OSSetClockRTC: Time set to %u secs at %u msecs", num_seconds, sync_time));

   FUNCTION_FINISH(STB_OSSetClockRTC);
}

/**
 * @brief   Returns the current time in seconds. This is calculated by using
 *          the set UTC time and adding the difference between the system boot
 *          time when it was set (sync_time) and the system boot time now.
 * @return  The current time in seconds since midnight 1-1-1970.
 */
U32BIT STB_OSGetClockRTC(void)
{
   U32BIT time_now;

   FUNCTION_START(STB_OSGetClockRTC);

   /* Actual time is given by the value saved in
    * utc_seconds with the difference between the time
    * when it was set (sync_time) and the time now.   */
   time_now = utc_seconds + ((SysBootTime() - sync_time) / RTC_TICKS_PER_SEC);

   FUNCTION_FINISH(STB_OSGetClockRTC);

   return time_now;
}

/**
 * @brief   Get Difference between Given Time and Current Time.
 * @param   timestamp Given Clock Value to Compare Against.
 * @return  Time Difference in MilliSeconds.
 */
U32BIT STB_OSGetClockDiff(U32BIT timestamp)
{
   U32BIT diff;

   FUNCTION_START(STB_OSGetClockDiff);

   /*!- Calculate Difference between current time & given value */
   diff = SysBootTime() - timestamp;

   RTC_DBG(("STB_OSGetClockDiff: Timestamp = %u msecs, diff = %u msecs", timestamp, diff));

   FUNCTION_FINISH(STB_OSGetClockDiff);

   return diff;
}

/*!**************************************************************************
 * @brief    Get Number of Clock Ticks per Second.
 * @return   Number of Ticks.
 ****************************************************************************/
U32BIT STB_OSGetClockPerSec(void)
{
   FUNCTION_START(STB_OSGetClockPerSec);

   FUNCTION_FINISH(STB_OSGetClockPerSec);

   return(RTC_TICKS_PER_SEC);
}

/**
 * @brief   Get Current Computer Clock Time.
 * @return  Time in Milliseconds.
 */
U32BIT STB_OSGetClockMilliseconds(void)
{
   U32BIT millisecs;

   FUNCTION_START(STB_OSGetClockMilliseconds);

   millisecs = SysBootTime();

   RTC_DBG(("STB_OSGetClockMilliseconds: %u msecs", millisecs));

   FUNCTION_FINISH(STB_OSGetClockMilliseconds);

   return millisecs;
}

/**
 * @brief   Set the time in seconds since midnight 1-1-1970 in GMT
 * @param   num_seconds time in seconds
 */
void STB_OSSetClockGMT(U32BIT num_seconds)
{
   FUNCTION_START(STB_OSSetClockGMT);
   USE_UNWANTED_PARAM(num_seconds);
   FUNCTION_FINISH(STB_OSSetClockGMT);
}

/**
 * @brief   Returns the system time in seconds
 * @return  system time in seconds
 */
U32BIT STB_OSGetClockGMT(void)
{
   struct timeval tv;

   FUNCTION_START(STB_OSGetClockGMT);

   gettimeofday(&tv, NULL);

   FUNCTION_FINISH(STB_OSGetClockGMT);

   return(tv.tv_sec);
}


/**
 * @brief   Get Current Computer Clock Time.
 * @return  Time in Microseconds.
 */
U64BIT STB_OSGetClockMicroseconds(void)
{
   struct timeval tv;
   U64BIT microsecs;

   FUNCTION_START(STB_OSGetClockMicroseconds);

   gettimeofday(&tv, NULL);

   microsecs = tv.tv_sec;
   microsecs = (microsecs * 1000000L) + tv.tv_usec;

   FUNCTION_FINISH(STB_OSGetClockMicroseconds);

   return microsecs;
}

/****************************************************************************
 *  Local Procedures
 ****************************************************************************/

/*!**************************************************************************
 * @brief    Get the time in milliseconds since the system was booted.
 * @return   Time in milliseconds since system booted
 * @warning  None.
 * @bug      None.
 ****************************************************************************/
static U32BIT SysBootTime(void)
{
   struct timeval tv;
   U32BIT sec, msec, usec;
   static BOOLEAN first_call = TRUE;
   static struct timeval first_tv;

   if (first_call)
   {
      gettimeofday(&first_tv, NULL);
      first_call = FALSE;
   }

   gettimeofday(&tv, NULL);
   sec = tv.tv_sec - first_tv.tv_sec;
   if (tv.tv_usec < first_tv.tv_usec)
   {
      usec = tv.tv_usec + 1000000 - first_tv.tv_usec;
      --sec;
   }
   else
   {
      usec = tv.tv_usec - first_tv.tv_usec;
   }

   msec = sec * 1000 + usec / 1000;

   return msec;
}

