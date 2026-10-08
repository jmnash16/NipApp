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
 * @brief   Set Top Box - Linux Op.System Interface for Task Mechanism.
 * @file    stbos_task.c
 * @date    Sep 2006
 * @author  Jon Eldridge
 */
//#define DEBUG_PRINTING_ENABLED

// compiler library header files
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

/* STB Header Files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwos.h"

#include "stbhwdef.h"
#include "stbhwc.h"

/* Comment Out to Enable Function Debug Tracking */

/* Local MACRO Definitions */

/*!- Select-Deselect Local Debug Text Output */
#ifndef  TASK_DEBUG
#define  TASK_DBG(X)
#else
#define  TASK_DBG(X)    STB_SPDebugWrite X
#endif

#define MAX_TASKS 64
#define MAX_DEPTH 64
#define MAX_COUNT (1 << 5)
#define MSK_COUNT (MAX_COUNT - 1)

/* Local ENUM/TYPE Definitions */

/* Local VARIABLE  Declarations */
static pthread_mutex_t sleep_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sleep_cond = PTHREAD_COND_INITIALIZER;

/* Local PROTOTYPE Declarations */

/**
 * @brief   Create a New Task to the calling process. Upon success, the
 *          created task runs on its own stack, as part of the calling
 *          process context.  Task termination must be achieved by calling
 *          STB_OSDestroyTask()
 * @param   function task entry point
 * @param   param user defined parameter passed when task is started
 * @param   stack stack size
 * @param   priority task priority, min 0, max 15
 * @param   name task name
 * @return  handle of task
 */
void* STB_OSCreateTask(void (*function)(void *), void *param, U32BIT stack, U8BIT priority, U8BIT *name)
{
   pthread_attr_t attr;
   struct sched_param parm;
   pthread_t handle;
   int err;

   FUNCTION_START(STB_OSCreateTask);
   USE_UNWANTED_PARAM(name);

   // Create a set of default creation attributes
   pthread_attr_init(&attr);

   if (stack > 0)
   {
      pthread_attr_setstacksize(&attr, stack);
   }

   // Ensure thread is detached so resources are freed on exit
//   pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
//   pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED );
   err = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
   if (err == 0)
   {
      /* Although the policy returns min and max thread priorities of 1 and 99,
       * NXP have said that only values from 1-20 should be used as these are
       * the ones supposedly defined for POSIX, though I can't find out whether
       * this is true. As the range is very similar to the OBS range, we just
       * add 1 to the OBS value and use that. */
      parm.sched_priority = priority + 1;
      pthread_attr_setschedparam(&attr, &parm);
   }

   DBGPRINT("name=%s",name)
   // Create the task
   err = pthread_create(&handle, &attr, (void *(*)(void *))function, param);
   if (err != 0)
   {
      TASK_DBG(("pthread_create FAILED for %s, err=%d (%s)", name, err, strerror(err)));
      handle = 0;
   }

   // Destroy the creation attributes
   pthread_attr_destroy(&attr);


   FUNCTION_FINISH(STB_OSCreateTask);

   return((void *)handle);
}

/**
 * @brief   Delete Task must be called upon termination of each task as it
 *          frees all OS specific resources allocated for the specific task.
 */
void STB_OSDestroyTask(void *DestroyTaskPtr)
{
   void *status;

   pthread_join((pthread_t)DestroyTaskPtr, &status);
   pthread_cancel((pthread_t)DestroyTaskPtr);
}

/**
 * @brief   Lock the calling task. Prevents task scheduler from preempting
 *          calling task and should always be used as a pair with
 *          STB_OSTaskUnlock() to create a critical region of code execution
 *          that cannot be interrupted by another task.
 */
void STB_OSTaskLock(void)
{
}

/**
 * @brief   Unlock the calling task. Allows task scheduler to preempting
 *          calling task and should always be used as a pair with
 *          STB_OSTaskLock() to create a critical region of code execution
 *          that cannot be interrupted by another task.
 */
void STB_OSTaskUnlock(void)
{
}

/**
 * @brief   Delay Task for Specifed Time Period.
 * @param   timeout delay in milliSeconds.
 */
void STB_OSTaskDelay(U16BIT timeout)
{
   struct timespec t;

   t.tv_sec = timeout / 1000;
   t.tv_nsec = ((U32BIT)timeout % 1000) * 1000000L;

   while ((nanosleep(&t, &t) < 0) && (errno == EINTR))
      ;
}

/**
 * @brief   Put Calling Task to Sleep. This is the equivalent of a Task Reschedule which is useful
 *          when a task wants to release the CPU on an application preemption point.
 */
void  STB_OSTaskSleep(void)
{
   int status;

   /* Lock the mutex */
   pthread_mutex_lock(&sleep_mutex);

   /* Wait on the cond variable */
   status = pthread_cond_wait(&sleep_cond, &sleep_mutex);
   STB_SPDebugWrite("%s: pthread_cond_wait=%d", __FUNCTION__, status);

   pthread_mutex_unlock(&sleep_mutex);
}

/**
 * @brief   Wake up a task that was put to sleep when it called STB_OSTaskSleep().
 */
void STB_OSTaskWakeUp(void)
{
   STB_SPDebugWrite("%s", __FUNCTION__);
   pthread_mutex_lock(&sleep_mutex);
   pthread_cond_broadcast(&sleep_cond);
   pthread_mutex_unlock(&sleep_mutex);
}


/**
 * @brief   Set a New Priority Level for Specified Task.
 * @param   task task whose priority is to be changed
 * @param   priority new priority
 * @return  Old task priority
 */
U8BIT STB_OSTaskPriority(void *task, U8BIT priority)
{
   struct sched_param parm;

   FUNCTION_START(STB_OSTaskPriority);

   ASSERT(task != NULL);

   memset(&parm, 0, sizeof(parm));

   parm.sched_priority = priority;
   pthread_setschedparam(*((pthread_t *)task), SCHED_FIFO, &parm);

   FUNCTION_FINISH(STB_OSTaskPriority);

   return(0);
}

/**
 * @brief   Suspend the calling task
 */
void  STB_OSTaskSuspend(void)
{
   sched_yield();
}

/**
 * @brief   Returns the handle of the current task
 * @return  Handle of current task
 */
void* STB_OSGetCurrentTask(void)
{
   return((void *)pthread_self());
}

