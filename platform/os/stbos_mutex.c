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
 * @brief   Set Top Box - Operating System Interface for Mutexes.
 * @file    stbos_mutex.c
 * @date    10 November 2006
 * @author  Ocean Blue
 */

#ifndef NDEBUG
#define DEBUG_ASSERT_STANDARD
#endif

/*---includes for this file--------------------------------------------------*/

/* compiler library header files */
#include <stdio.h>
#include <pthread.h>
#include <time.h>

/* third party header files */

/* DVBCore header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwmem.h"
#include "stbhwc.h"

/*!- Select-Deselect Local Debug Text Output */
/*#define  MUTEX_DEBUG*/

#ifndef  MUTEX_DEBUG
#define  MUTEX_DBG(X)
#else
#define  MUTEX_DBG(X)    STB_SPDebugWrite X
#endif

#ifdef STATIC_MEM_MTX
#define MAX_MUTEXES     330
#define INVALID_COUNT   0xffffffff
#endif


/*---constant definitions for this file--------------------------------------*/
struct mutex_s
{
   pthread_mutex_t lock;
   pthread_t thread_id;
   U32BIT lock_count;
};
/*---local typedef structs for this file-------------------------------------*/
typedef struct mutex_s mutex_t;

#ifdef STATIC_MEM_MTX
static mutex_t mutexes[MAX_MUTEXES];
static mutex_t *mtx_end_ptr;
#endif
static unsigned int mtx_count = 0;

/*---local (static) variable declarations for this file----------------------*/

/*   (internal variables declared static to make them local) */

/*---local function prototypes for this file---------------------------------*/
/*   (internal functions declared static to make them local) */

/*---global function definitions---------------------------------------------*/

/**
 * @brief   Create a mutex.
 * @return  Newly created mutex, or NULL.
 */
void* STB_OSCreateMutex(void)
{
   mutex_t *mutex_handle;
   pthread_mutexattr_t mutex_attr_ptr;
#ifdef STATIC_MEM_MTX
   int it;
#endif

   FUNCTION_START(STB_OSCreateMutex);

#ifdef STATIC_MEM_MTX
   if (mtx_count == 0)
   {
      /* Only execute on first initialisation */
      for (it = 0; it != MAX_MUTEXES; it++)
      {
         mutexes[it].lock_count = INVALID_COUNT;
      }
      mutex_handle = mutexes;
      mtx_end_ptr = mutex_handle + MAX_MUTEXES;
   }
   else
   {
      mutex_handle = NULL;
      for (it = 0; it != MAX_MUTEXES; it++)
      {
         if (mutexes[it].lock_count == INVALID_COUNT)
         {
            mutex_handle = mutexes + it;
            break;
         }
      }
   }
#else
   mutex_handle = (mutex_t *)STB_MEMGetSysRAM(sizeof(mutex_t));
#endif

   if (mutex_handle != NULL)
   {
      mtx_count++;
      pthread_mutexattr_init(&mutex_attr_ptr);
      if (pthread_mutex_init(&mutex_handle->lock, &mutex_attr_ptr) == 0)
      {
         mutex_handle->thread_id = 0;
         mutex_handle->lock_count = 0;
      }
   }

#ifdef STATIC_MEM_MTX
   MUTEX_DBG(("STB_OSCreateMutex: Created mutex 0x%x cnt=%u it=%d", mutex_handle, mtx_count, it));
#else
   MUTEX_DBG(("STB_OSCreateMutex: Created mutex 0x%x cnt=%u", mutex_handle, mtx_count));
#endif

   FUNCTION_FINISH(STB_OSCreateMutex);

   return (void *) mutex_handle;
}

/**
 * @brief   Lock a mutex (a.k.a. 'enter', 'wait' or 'get').
 * @param   mutex_var The mutex to lock.
 */
void STB_OSMutexLock(void *mutex_var)
{
   mutex_t *mutex_handle;

   FUNCTION_START(STB_OSMutexLock);

   //MUTEX_DBG(("STB_OSMutexLock: Locking mutex 0x%x", mutex));

   if (mutex_var != NULL)
   {
      mutex_handle = (mutex_t *) mutex_var;
   #ifndef STATIC_MEM_MTX
      ASSERT(mutex_var != (void *)0x0);
   #else
      ASSERT(mutex_handle >= mutexes);
      ASSERT(mutex_handle < mtx_end_ptr);
      ASSERT(mutex_handle->lock_count < 0x20 );
   #endif

      if (pthread_equal(mutex_handle->thread_id, pthread_self()) != 0)
      {
         /* This thread already owns the mutex so just increment the lock count */
         mutex_handle->lock_count++;
      }
      else
      {
         /* The mutex is either already owned by another thread or isn't owned at all
            so just attempt to acquire it. */
         pthread_mutex_lock((pthread_mutex_t *) &mutex_handle->lock);

         mutex_handle->thread_id = pthread_self();
         mutex_handle->lock_count = 1;
      }
   }
#ifdef  MUTEX_DEBUG
   else
   {
      MUTEX_DBG(("%s: NULL mutex", __FUNCTION__));
   }
#endif

   FUNCTION_FINISH(STB_OSMutexLock);
}

/**
 * @brief   Unlock a mutex (a.k.a. 'leave', 'signal' or 'release')
 * @param   mutex_var The mutex to unlock.
 */
void STB_OSMutexUnlock(void *mutex_var)
{
   mutex_t *mutex_handle;

   FUNCTION_START(STB_OSMutexUnlock);

   //MUTEX_DBG(("STB_OSMutexUnlock: Unlocking mutex 0x%x", mutex_var));

   if (mutex_var != NULL)
   {
      mutex_handle = (mutex_t *) mutex_var;

   #ifndef STATIC_MEM_MTX
      ASSERT(mutex_var != (void *)0x0);
   #else
      ASSERT(mutex_handle >= mutexes);
      ASSERT(mutex_handle < mtx_end_ptr);
      ASSERT(mutex_handle->lock_count < 0x20 );
   #endif

      /* if the same thread as original lock, decrement counter */
      if (pthread_equal(mutex_handle->thread_id, pthread_self()) != 0)
      {
         mutex_handle->lock_count--;

         if (mutex_handle->lock_count == 0)
         {
            /* Mutex has now been released by this thread */
            mutex_handle->thread_id = 0;
            pthread_mutex_unlock((pthread_mutex_t *) &mutex_handle->lock);
         }
      }
      else
      {
         //MUTEX_DBG(("STB_OSMutexUnlock: Thread doesn't own the mutex"));
      }
   }
#ifdef  MUTEX_DEBUG
   else
   {
      MUTEX_DBG(("%s: NULL mutex", __FUNCTION__));
   }
#endif

   FUNCTION_FINISH(STB_OSMutexUnlock);
}

/**
 * @brief   Delete a mutex.
 * @param   mutex_var The mutex to delete.
 */
void STB_OSDeleteMutex(void *mutex_var)
{
   mutex_t *mutex_handle;

   FUNCTION_START(STB_OSDeleteMutex);

   MUTEX_DBG(("STB_OSDeleteMutex: Deleting mutex 0x%x", mutex_var));

   if (mutex_var != NULL)
   {
      mutex_handle = (mutex_t *) mutex_var;

   #ifndef STATIC_MEM_MTX
      ASSERT(mutex_var != (void *)0x0);
   #else
      ASSERT(mutex_handle >= mutexes);
      ASSERT(mutex_handle < mtx_end_ptr);
   #endif

      mtx_count--;

      pthread_mutex_destroy((pthread_mutex_t *) &mutex_handle->lock);

   #ifdef STATIC_MEM_MTX
      mutex_handle->lock_count = INVALID_COUNT;
   #else
      STB_MEMFreeSysRAM(mutex_var);
   #endif
   }
#ifdef  MUTEX_DEBUG
   else
   {
      MUTEX_DBG(("%s: NULL mutex", __FUNCTION__));
   }
#endif

   FUNCTION_FINISH(STB_OSDeleteMutex);
}

/*---local function definitions----------------------------------------------*/

/****************************************************************************
** End of file
*****************************************************************************/
