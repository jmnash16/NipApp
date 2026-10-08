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
 * @brief   Set Top Box - Operating system Interface for Queues
 * @file    stbos_queue.c
 * @date    Oct 2006
 * @author  Ocean Blue
 */

#include <stdio.h>
#include <sys/errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

/* STB Header Files */
#include "techtype.h"
#include "dbgfuncs.h"

#include "stbhwos.h"
#include "stbhwmem.h"
#include "stbhwc.h"

/* Local MACRO Definitions */

/*!- Select-Deselect Local Debug Text Output */
/*#define   QUEUE_DEBUG*/

#ifndef  QUEUE_DEBUG
#define  QUEUE_DBG(X)
#else
#define  QUEUE_DBG(X)    STB_SPDebugWrite X
#endif

/* Local CONSTANT  Definitions */

/* Local ENUM/TYPE Definitions */

typedef struct queue
{
   void *array;
   void *read_ptr;
   void *write_ptr;
   void *end_ptr;

   U16BIT elem_size;
   U16BIT queue_size;
   U16BIT elem_count;

   pthread_mutex_t mutex;
   pthread_cond_t cond;
} S_QUEUE;

/* Local VARIABLE  Declarations */

/* Local PROTOTYPE Declarations */
static void CalcAbstime(U32BIT timeout, struct timespec *ts);
static BOOLEAN WaitNoTimeout(S_QUEUE *queue);
static BOOLEAN WaitTimeout(S_QUEUE *queue, struct timespec *abstime);


/**
 * @brief   Create Queue of given number of messages and size of message
 * @param   msg_size  - Queue Message Packet Size
 * @param   num_msgs  - Queue Message Depth in Packets
 * @return  Queue Handle - Number for success, NULL upon failure.
 */
void* STB_OSCreateQueue(U16BIT msg_size, U16BIT num_msgs)
{
   int rc;
   S_QUEUE *queue;

   FUNCTION_START(STB_OSCreateQueue);

   queue = (S_QUEUE *)STB_MEMGetSysRAM(sizeof(S_QUEUE));
   if (queue != NULL)
   {
      rc = pthread_mutex_init(&queue->mutex, NULL);
      if (rc == 0)
      {
         rc = pthread_cond_init(&queue->cond, NULL);
         if (rc == 0)
         {
            queue->elem_size = msg_size;
            queue->queue_size = num_msgs;
            queue->elem_count = 0;
            queue->array = STB_MEMGetSysRAM(queue->elem_size * queue->queue_size);
            if (queue->array != NULL)
            {
               //memset(queue->array, 0xbd, queue->elem_size * queue->queue_size);
               queue->read_ptr = queue->array;
               queue->write_ptr = queue->array;
               queue->end_ptr = (char *)queue->array + queue->elem_size * queue->queue_size;
            }
            else
            {
               pthread_mutex_destroy(&queue->mutex);
               pthread_cond_destroy(&queue->cond);
               STB_MEMFreeSysRAM(queue);
               queue = NULL;
            }
         }
         else
         {
            pthread_mutex_destroy(&queue->mutex);
            STB_MEMFreeSysRAM(queue);
            queue = NULL;
         }
      }
      else
      {
         STB_MEMFreeSysRAM(queue);
         queue = NULL;
      }
   }

   FUNCTION_FINISH(STB_OSCreateQueue);

   return queue;
}

/**
 * @brief   Destroy Queue
 * @param   queue_handle - Unique Queue Handle Identifier Variable Address.
 * @return  TRUE for success, FALSE upon failure.
 */
BOOLEAN STB_OSDestroyQueue(void *queue_handle)
{
   S_QUEUE *queue = queue_handle;
   BOOLEAN success;

   FUNCTION_START(STB_OSDestroyQueue);

   if (queue != NULL)
   {
      pthread_mutex_destroy(&queue->mutex);
      pthread_cond_destroy(&queue->cond);
      STB_MEMFreeSysRAM(queue->array);
      STB_MEMFreeSysRAM(queue);
      success = TRUE;
   }
   else
   {
      success = FALSE;
   }

   FUNCTION_FINISH(STB_OSDestroyQueue);

   return success;
}

/**
 * @brief   Read a message from a queue
 * @param   queue - Queue Handle
 * @param   data - User's Read Message Buffer Start Address.
 * @param   msg_size - Message Packet Size in Bytes.
 * @param   timeout - timeout in milliseconds
 * @return  TRUE for success, FALSE upon failure.
 */
BOOLEAN STB_OSReadQueue(void *queue_handle, void *data, U16BIT msg_size, U16BIT timeout)
{
   S_QUEUE *queue = queue_handle;
   struct timespec abstime;
   BOOLEAN success;

   FUNCTION_START(STB_OSReadQueue);

   ASSERT(queue != NULL);
   ASSERT(msg_size == queue->elem_size);
   ASSERT(data != NULL);

   USE_UNWANTED_PARAM(msg_size);

   pthread_mutex_lock(&queue->mutex);

   success = TRUE;
   if (timeout == TIMEOUT_NOW)
   {
      /* No timeout */
      if (queue->elem_count == 0)
      {
         success = FALSE;
      }
   }
   else if (timeout == TIMEOUT_NEVER)
   {
      while ((queue->elem_count == 0) && success)
      {
         success = WaitNoTimeout(queue);
      }
   }
   else
   {
      CalcAbstime(timeout, &abstime);

      /* success = not timed-out yet */
      while (queue->elem_count == 0 && success)
      {
         success = WaitTimeout(queue, &abstime);
      }
   }

   if (success)
   {
      /* There is something in the queue */
      memcpy(data, queue->read_ptr, queue->elem_size);
      queue->read_ptr = (char *)queue->read_ptr + queue->elem_size;
      if (queue->read_ptr == queue->end_ptr)
      {
         queue->read_ptr = queue->array;
      }
      --queue->elem_count;
      pthread_cond_broadcast(&queue->cond);
   }

   pthread_mutex_unlock(&queue->mutex);

   FUNCTION_FINISH(STB_OSReadQueue);

   return success;
}

/**
 * @brief   Write a message to the queue
 * @param   queue - Queue Handle
 * @param   data - message to be queued
 * @param   msg_size - size of message in bytes
 * @param   timeout - timeout in milliseconds
 * @return  TRUE for success, FALSE upon failure.
 */
BOOLEAN STB_OSWriteQueue(void *queue_handle, void *data, U16BIT msg_size, U16BIT timeout)
{
   S_QUEUE *queue = queue_handle;
   struct timespec abstime;
   BOOLEAN success;

   FUNCTION_START(STB_OSWriteQueue);

   ASSERT(queue != NULL);
   ASSERT(msg_size == queue->elem_size);
   ASSERT(data != NULL);

   USE_UNWANTED_PARAM(msg_size);

   pthread_mutex_lock(&queue->mutex);

   success = TRUE;
   if (timeout == TIMEOUT_NOW)
   {
      /* No timeout */
      if (queue->elem_count == queue->queue_size)
      {
         success = FALSE;
      }
   }
   else if (timeout == TIMEOUT_NEVER)
   {
      while ((queue->elem_count == queue->queue_size) && success)
      {
         success = WaitNoTimeout(queue);
      }
   }
   else
   {
      CalcAbstime(timeout, &abstime);

      /* success = not timed-out yet */
      while (queue->elem_count == queue->queue_size && success)
      {
         success = WaitTimeout(queue, &abstime);
      }
   }

   if (success)
   {
      /* There is space in the queue */
      memcpy(queue->write_ptr, data, queue->elem_size);
      queue->write_ptr = (char *)queue->write_ptr + queue->elem_size;
      if (queue->write_ptr == queue->end_ptr)
      {
         queue->write_ptr = queue->array;
      }
      ++queue->elem_count;
      pthread_cond_broadcast(&queue->cond);
   }

   pthread_mutex_unlock(&queue->mutex);

   FUNCTION_FINISH(STB_OSWriteQueue);

   return success;
}

/****************************************************************************
 *  Local Procedures
 */
static void CalcAbstime(U32BIT timeout, struct timespec *ts)
{
   U32BIT nsec;
   U32BIT msec;
   U32BIT sec;
   struct timeval tv;

   gettimeofday(&tv, NULL);

   ts->tv_sec = tv.tv_sec;
   ts->tv_nsec = tv.tv_usec * 1000;

   sec = timeout / 1000;                  /* Seconds in timeout */
   msec = timeout - sec * 1000;           /* Milliseconds only (less than a second) */
   nsec = ts->tv_nsec + msec * 1000000L;  /* Total nanoseconds - between 0 and 2 seconds */
   if (nsec > 1000000000L)
   {
      /* nsec is no more than 2 seconds, make it less than one */
      nsec -= 1000000000L;
      ++sec;
   }

   ts->tv_sec += sec;
   ts->tv_nsec = nsec;
}

static BOOLEAN WaitNoTimeout(S_QUEUE *queue)
{
   BOOLEAN signalled;
   int rc;

   rc = pthread_cond_wait(&queue->cond, &queue->mutex);
   if (rc == 0)
   {
      signalled = TRUE;
   }
   else
   {
      signalled = FALSE;
   }

   return signalled;
}

static BOOLEAN WaitTimeout(S_QUEUE *queue, struct timespec *abstime)
{
   BOOLEAN signalled;
   int rc;

   rc = pthread_cond_timedwait(&queue->cond, &queue->mutex, abstime);
   if (rc == 0)
   {
      signalled = TRUE;
   }
   else
   {
      signalled = FALSE;
   }

   return signalled;
}

