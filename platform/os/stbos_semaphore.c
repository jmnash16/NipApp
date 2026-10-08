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
 * @brief   Set Top Box - System Interface for Semaphore Mechanism.
 * @file    stbos_semaphore.c
 * @date    May 2006
 * @author  Ocean Blue
 */

#include <stdio.h>
#include <stdlib.h>

#include <semaphore.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>

/* STB Header Files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwc.h"
#include "stbhwos.h"
#include "stbhwmem.h"

/* Local MACRO Definitions */

/*!- Select-Deselect Local Debug Text Output */
/*#define  SEMA_DEBUG*/

#ifndef  SEMA_DEBUG
#define  SEMA_DBG(X)
#else
#define  SEMA_DBG(X)    STB_SPDebugWrite X
#endif

/* Local CONSTANT  Definitions */

/* Local ENUM/TYPE Definitions */

/* Local VARIABLE  Declarations */

/* Local PROTOTYPE Declarations */

/**
 * @brief   Create a Semaphore
 * @return  Seamphore Handle Address upon success, or NULL upon failure.
 */
void* STB_OSCreateSemaphore(void)
{
   sem_t *sem;
   int err;

   FUNCTION_START(STB_OSCreateSemaphore);

   sem = (sem_t *)STB_MEMGetSysRAM(sizeof(sem_t));

   if (sem != NULL)
   {
      /* Initialise the semaphore with a count of 1 and  allow it to be shared with
       * other threads but not other processes */
      err = sem_init(sem, 0, 1);

      if (err != 0)
      {
         STB_MEMFreeSysRAM(sem);
         sem = NULL;

         SEMA_DBG(("STB_OSCreateSemaphore: Failed to init semaphore, error %d", errno));
      }
   }

   FUNCTION_FINISH(STB_OSCreateSemaphore);

   return((void *)sem);
}

/**
 * @brief   Create a counting semaphore.
 * @param   value - initial value for semaphore.
 * @return  Seamphore handle upon success, or NULL upon failure.
 */
void* STB_OSCreateCountSemaphore(U32BIT value)
{
   sem_t *sem;
   int err;

   FUNCTION_START(STB_OSCreateCountSemaphore);

   sem = (sem_t *)STB_MEMGetSysRAM(sizeof(sem_t));

   if (sem != NULL)
   {
      /* Initialise the semaphore with a count of 1 and  allow it to be shared with
       * other threads but not other processes */
      err = sem_init(sem, 0, value);

      if (err != 0)
      {
         STB_MEMFreeSysRAM(sem);
         sem = NULL;

         SEMA_DBG(("STB_OSCreateCountSemaphore: Failed to init semaphore, error %d", errno));
      }
   }

   FUNCTION_FINISH(STB_OSCreateCountSemaphore);

   return((void *)sem);
}

/**
 * @brief   Initialise a counting semaphore
 * @param   semaphore - Semaphore handle.
 * @param   value - New value for semaphore.
 * @warning This is a very dangerous function, and should be used
 *           very carefully.
 */
void STB_OSInitCountSemaphore(void *semaphore, U32BIT value)
{
   USE_UNWANTED_PARAM(semaphore);
   ASSERT(value == 0);
   USE_UNWANTED_PARAM(value);
}

/**
 * @brief   Delete a Semaphore
 * @param   semaphore - Semaphore handle.
 * @return  TRUE for success, FALSE upon failure.
 */
void STB_OSDeleteSemaphore(void *semaphore)
{
   int err;

   FUNCTION_START(STB_OSDeleteSemaphore);

   if (semaphore != NULL)
   {
      err = sem_destroy(semaphore);

      if (err != 0)
      {
         SEMA_DBG(("STB_OSDeleteSemaphore: Failed to destroy semaphore 0x%x, error %d", semaphore, errno));
      }
      STB_MEMFreeSysRAM(semaphore);
   }

   FUNCTION_FINISH(STB_OSDeleteSemaphore);
}

/**
 * @brief   Signal a Semaphore to Release it by decrementing its counter.
 * @param   semaphore Semaphore handle.
 */
void STB_OSSemaphoreSignal(void *semaphore)
{
   int err;

   FUNCTION_START(STB_OSSemaphoreSignal);

   if (semaphore != NULL)
   {
      err = sem_post((sem_t *)semaphore);
      if (err != 0)
      {
         SEMA_DBG(("STB_OSSemaphoreSignal: Failed to unlock semaphore 0x%x, error %d", semaphore, errno));
      }
   }
#ifdef SEMA_DEBUG
   else
   {
      SEMA_DBG(("%s: NULL semaphore", __FUNCTION__));
   }
#endif

   FUNCTION_FINISH(STB_OSSemaphoreSignal);
}

/**
 * @brief   Wait on Semaphore Indefinity or Until Released.
 * @param   semaphore Semaphore handle.
 * @return  TRUE for success, FALSE upon failure.
 */
void STB_OSSemaphoreWait(void *semaphore)
{
   int err;

   FUNCTION_START(STB_OSSemaphoreWait);

   if (semaphore != NULL)
   {
      err = sem_wait((sem_t *)semaphore);

      if (err != 0)
      {
         SEMA_DBG(("STB_OSSemaphoreWait: Failed to lock semaphore 0x%x, error %d", semaphore, errno));
      }
   }
#ifdef SEMA_DEBUG
   else
   {
      SEMA_DBG(("%s: NULL semaphore", __FUNCTION__));
   }
#endif

   FUNCTION_FINISH(STB_OSSemaphoreWait);
}

/**
 * @brief   Wait on Semaphore for Set Time Period in an Attempt to Acquire.
 * @param   semaphore Semaphore handle.
 * @param   timeout Time Period to Wait in milliseconds.
 * @return  TRUE for success, FALSE upon failure.
 */
BOOLEAN STB_OSSemaphoreWaitTimeout(void *semaphore, U16BIT timeout)
{
   struct timeval tv;
   struct timespec abs_timeout;
   U32BIT usec;
   int err;
   BOOLEAN result = TRUE;

   FUNCTION_START(STB_OSSemaphoreWaitTimeout);

   if (semaphore != NULL)
   {
      gettimeofday(&tv, NULL);
      usec = tv.tv_usec + (1000 * timeout);
      while (usec >= 1000000)
      {
         usec -= 1000000;
         tv.tv_sec++;
      }
      abs_timeout.tv_sec = tv.tv_sec;
      abs_timeout.tv_nsec = 1000 * usec;

      do
      {
         err = sem_timedwait((sem_t *)semaphore, &abs_timeout );
      }
      while (err == -1 && errno == EINTR);   /* Restart when interrupted by handler */

      if (err != 0)
      {
         result = FALSE;
         if (errno != ETIMEDOUT)
         {
            SEMA_DBG(("STB_OSSemaphoreWait: Failed to lock semaphore 0x%x, error %d", semaphore, errno));
         }
      }
   }
#ifdef SEMA_DEBUG
   else
   {
      SEMA_DBG(("%s: NULL semaphore", __FUNCTION__));
   }
#endif

   FUNCTION_FINISH(STB_OSSemaphoreWaitTimeout);

   return result;
}

