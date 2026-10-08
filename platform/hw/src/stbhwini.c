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
 * @brief   Set Top Box - STB810 Hardware Layer, Top level initialisation
 * @file    stbhwini.c
 * @date    September 2007
 * @author  Dave Carter
 */
//#define STACKDUMPS

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#ifdef STACKDUMPS
#define _GNU_SOURCE
#include <memory.h>
#include <stdio.h>
#define __USE_GNU
#include <ucontext.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* third party header files */

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwdef.h"
#include "stbhwc.h"
#include "stbhwini.h"
#include "stbhwos.h"
#include "stbhwav.h"
#include "stbhwosd.h"
#include "stbhwfp.h"
#include "stbhwmem.h"
#include "stbhwdmx.h"
#include "stbhwtun.h"
#include "stbhwdsk.h"
#ifdef COMMON_INTERFACE
//#include "sim_cam.h"
#endif

void STB_EBUInitialise(void);

/*---constant definitions for this file---------------------------------------*/

/*---local typedef structs for this file--------------------------------------*/

/*---local (static) variable declarations for this file-----------------------*/
static E_HW_STANDBY_STATE hw_standby_state = HW_STANDBY_ON;


/*---global variable definitions----------------------------------------------*/


/*---local function prototypes for this file----------------------------------*/
#ifdef STACKDUMPS
static void segv_handler(int signum, siginfo_t *info, void *ptr);
#endif

/*---external function prototypes for this file-------------------------------*/

/*---global function definitions---------------------------------------------*/
/**
 * @brief   Initialise the platform layer by calling the platform layer initialisation functions
 *          that are appropriate for this platform.
 * @param   hw_subt - The subtitling mode (always DVB for this platform)
 */
void STB_HWInitialise(E_HW_SUBT_CONTROL_MASK hw_subt)
{
#ifdef STACKDUMPS
   struct sigaction action;
#endif

   FUNCTION_START(STB_HWInitialise);

   STB_MEMInitialiseRAM();

#ifdef STACKDUMPS
   memset(&action, 0, sizeof(action));
   action.sa_sigaction = segv_handler;
   action.sa_flags = SA_RESTART | SA_SIGINFO;
   if (sigaction(SIGSEGV, &action, NULL) < 0)
   {
      perror("sigaction");
      return 0;
   }
#endif
   /* NippApp: signals are handled by main(), which shuts DVBCore down cleanly */

//   STB_FPInitialise();
   STB_DSKInitialise();

   if (hw_subt == HW_SUBT_EBU)
   {
      STB_DMXInitialise(NUM_DEMUX_PATHS, FALSE);
      STB_EBUInitialise();
   }
   else
   {
      STB_DMXInitialise(NUM_DEMUX_PATHS, TRUE);
   }
   /* printf("OSD\r\n");fflush(stdout);*/
   /* max regions parameter not needed, this platform allocates dynamically */
   STB_OSDInitialise(0);

   /* printf("AV\r\n");fflush(stdout); */
   STB_AVInitialise(NUM_DECODE_PATHS, NUM_DECODE_PATHS);

   STB_MEMInitialiseNVM();

   STB_TuneInitialise(NUM_TUNER_PATHS);

#ifdef COMMON_INTERFACE
//   SIM_Init();
#endif

   /*printf("DONE\r\n");fflush(stdout);*/
   FUNCTION_FINISH(STB_HWInitialise);
}

/**
 * @brief   Used to transition the system between the various standby states.
 *          It's assumed that the system can move to any state from any of the other states.
 * @param   state state the system is requesting to move to
 * @return  TRUE if the system has or will move to the requested state, FALSE otherwise
 */
BOOLEAN STB_HWSetStandbyState(E_HW_STANDBY_STATE state)
{
   BOOLEAN retval;

   FUNCTION_START(STB_HWSetStandbyState);

   if (hw_standby_state != state)
   {
      hw_standby_state = state;
      retval = TRUE;
   }
   else
   {
      retval = FALSE;
   }

   FUNCTION_FINISH(STB_HWSetStandbyState);

   return(retval);
}

/**
 * @brief   Returns the current standby state of the system.
 *          STANDBY_LOWPOWER should never be returned by this function because in this state
 *          DVBCore is assumed not to be active
 * @return  current standby state
 */
E_HW_STANDBY_STATE STB_HWGetStandbyState(void)
{
   FUNCTION_START(STB_HWGetStandbyState);
   FUNCTION_FINISH(STB_HWGetStandbyState);
   return(hw_standby_state);
}

/**
 * @brief   Used to set the wake up time for when the system is going to be put into the low power
 *          standby state. Not all the supplied parameters are expected to be used, just those
 *          that are needed by the system to ensure the system wakes up at the required time.
 * @param   mjd wake up date in Modified Julian Date format
 * @param   year the year in full (e.g. 2016) the system is to wake up
 * @param   month month number (January=1, December=12) the system is to wake up
 * @param   day day number (1 to 28, 29, 30 or 31) the system is to wake up
 * @param   hours time in hours (0 to 23) the system is to wake up
 * @param   minutes time in minutes (0 to 59) the system is to wake up
 * @param   time_in_mins the time in minutes from the current date/time to the wake up time
 */
void STB_HWSetWakeUpTime(U16BIT mjd, U16BIT year, U8BIT month, U8BIT day, U8BIT hours,
   U8BIT minutes, U32BIT time_in_mins)
{
   FUNCTION_START(STB_HWSetWakeUpTime);
   USE_UNWANTED_PARAM(mjd);
   USE_UNWANTED_PARAM(year);
   USE_UNWANTED_PARAM(month);
   USE_UNWANTED_PARAM(day);
   USE_UNWANTED_PARAM(hours);
   USE_UNWANTED_PARAM(minutes);
   USE_UNWANTED_PARAM(time_in_mins);
   FUNCTION_FINISH(STB_HWSetWakeUpTime);
}

/**
 * @brief   Returns what caused the system to be powered on. This is used to work out what to
 *          do after being powered on (e.g. if due to user interaction then the system needs to
 *          enter the HW_STANDBY_ON state, whereas if it's due to the wake up timer then the system
 *          needs to work out what it needs to be do next, which may be to start a recording for
 *          a PVR type system, in which case it will enter the HW_STANDBY_ACTIVE state).
 * @return  the cause of the power on
 */
E_HW_WAKEUP_TYPE STB_HWGetWakeUpType(void)
{
   FUNCTION_START(STB_HWGetWakeUpType);
   FUNCTION_FINISH(STB_HWGetWakeUpType);
   return(HW_WAKEUP_UNKNOWN);
}

/**
 * @brief   Perform any operations required before entering standby
 * @deprecated This function is no longer used by DVBCore; STB_HWSetStandbyState is used instead.
 */
void STB_HWEnterStandby(void)
{
}

/**
 * @brief   Perform any operations required when coming out of standby
 * @deprecated This function is no longer used by DVBCore; STB_HWSetStandbyState is used instead.
 */
void STB_HWLeaveStandby(void)
{
}

/*---local function definitions-----------------------------------------------*/

/***************************************************************************/
#ifdef STACKDUMPS
/***************************************************************************//**
 * @brief     Dump out a stack backtrace upon segv
 ******************************************************************************/
static void segv_handler(int signum, siginfo_t *info, void *ptr)
{
   size_t i;

   void *bt[16];
   char **strings;
   size_t sz;

   if (signum == SIGSEGV)
   {
      fprintf(stderr, "Segmentation Fault!\n");
      fprintf(stderr, "Something tried to access address [%p] and it wasn't supposed to!\n", info->si_addr);
   }
   else
   {
      fprintf(stderr, "Got signal %d\n", signum);
   }

   fprintf(stderr, "This is what lead up to it:\n");
   sz = backtrace(bt, 16);
   if (sz > 1)
   {
      /* overwrite sigaction with caller's address */
//       bt[1] = (void*) ucontext->uc_mcontext.pc;

      strings = backtrace_symbols(bt, sz);

      /* not interested in 1st stack frame (points here) */
      for (i = 1; i < sz; ++i)
      {
         fprintf(stderr, "  %s\n", strings[i]);
      }
      free(strings);
   }
   else
   {
      fprintf(stderr, "  Empty stack\n");
   }
   fprintf(stderr, "End of stack trace\n");
   fflush(stderr);

   exit(-1);
}

#endif


/******************************************************************************
 * End of file
 *****************************************************************************/
