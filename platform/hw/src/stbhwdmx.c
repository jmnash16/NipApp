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
 * @brief   Demux for NippApp: section filtering of the TS packets the file tuner delivers.
 *
 *          Sections are reassembled on the tuner's feeder thread (DMX_FeedPackets) and handed
 *          to a delivery task per demux path, which calls DVBCore's filter callbacks. DVBCore
 *          reads the section with STB_DMXCopyPIDFilterSect from inside the callback.
 *          There is no audio, video or teletext PES handling: NippApp decodes nothing.
 *
 *          Filter IDs follow the scheme of App's demux: a PID filter ID is
 *          (filter index << 8) | (handler index << 4), and a section filter ID adds the
 *          section filter's slot (0-15) to the ID of the PID filter it was grabbed on.
 * @file    stbhwdmx.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwdef.h"
#include "stbhwos.h"
#include "stbhwdmx.h"
#include "nipphw.h"

/*---constant definitions for this file---------------------------------------*/
#define MAX_PID_FILTERS          32
#define MAX_HANDLERS_PER_PID     8
#define MAX_SECTION_FILTERS      16
#define SECTION_FILTER_LENGTH    8

#define MAX_SECTION_SIZE         4096        /* a private section, including its header */
#define PID_NOT_USED             0xffff
#define CC_INVALID               0xff

/* Completed sections waiting for delivery, per demux path. When full, the tuner waits. */
#define QUEUE_SIZE               2048
/* A TS packet can complete at most this many (tiny) sections */
#define MAX_SECTIONS_PER_PACKET  64

#define DMX_TASK_STACK_SIZE      8192
#define DMX_TASK_PRIORITY        12

/*---local typedef structs for this file--------------------------------------*/
typedef struct
{
   BOOLEAN allocated;
   BOOLEAN setup;
   BOOLEAN check_crc;
   U8BIT handler;
   U8BIT match[SECTION_FILTER_LENGTH];
   U8BIT mask[SECTION_FILTER_LENGTH];
} S_SECT_FILTER;

typedef struct
{
   U16BIT pid;
   U32BIT generation;                  /* changes on every grab and release, so stale sections are dropped */
   BOOLEAN started;

   FILTER_CALLBACK func[MAX_HANDLERS_PER_PID];
   U8BIT start_count[MAX_HANDLERS_PER_PID];
   S_SECT_FILTER sect[MAX_SECTION_FILTERS];

   /* Reassembly, on the tuner thread */
   U8BIT cc;
   BOOLEAN collecting;
   U16BIT length;                      /* bytes collected */
   U16BIT needed;                      /* total section size, 0 until the header has been seen */
   U8BIT buffer[MAX_SECTION_SIZE];

   /* Delivery, on the delivery task */
   U8BIT *delivery_data;
   U16BIT delivery_size;
} S_PID_FILTER;

typedef struct
{
   U8BIT filter;
   U32BIT generation;
   U16BIT size;
   U8BIT *data;
} S_SECTION;

typedef struct
{
   U8BIT path;
   U16BIT caps;
   E_STB_DMX_DEMUX_SOURCE source;
   U8BIT source_param;

   pthread_mutex_t mutex;              /* filter configuration and reassembly state */
   S_PID_FILTER filters[MAX_PID_FILTERS];

   pthread_mutex_t queue_mutex;
   pthread_cond_t queue_not_empty;
   pthread_cond_t queue_not_full;
   S_SECTION queue[QUEUE_SIZE];
   U16BIT queue_head;
   U16BIT queue_count;
} S_DEMUX;

/*---local (static) variable declarations for this file-----------------------*/
static U8BIT num_paths = 0;
static S_DEMUX *demuxes = NULL;
static U32BIT crc_table[256];

/*---local function prototypes for this file----------------------------------*/
static void InitCrcTable(void);
static BOOLEAN SectionCrcOk(const U8BIT *data, U16BIT size);
static S_PID_FILTER* GetFilter(S_DEMUX *dmx, U16BIT id);
static void ResetReassembly(S_PID_FILTER *filter);
static BOOLEAN TableIdWanted(S_PID_FILTER *filter, U8BIT table_id);
static BOOLEAN SectionMatches(S_PID_FILTER *filter, U8BIT handler, const U8BIT *data, U16BIT size,
   BOOLEAN crc_ok);
static void HandlePacket(S_DEMUX *dmx, U8BIT *packet, S_SECTION *done, U16BIT *num_done);
static U16BIT CollectBytes(S_PID_FILTER *filter, U8BIT index, const U8BIT *data, U16BIT len,
   S_SECTION *done, U16BIT *num_done);
static void QueueSections(S_DEMUX *dmx, S_SECTION *sections, U16BIT num);
static void DeliveryTask(void *param);

/*---global function definitions---------------------------------------------*/

void STB_DMXInitialise(U8BIT paths, BOOLEAN inc_pes_collection)
{
   U8BIT i;
   U8BIT f;
   S_DEMUX *dmx;

   FUNCTION_START(STB_DMXInitialise);

   USE_UNWANTED_PARAM(inc_pes_collection);

   InitCrcTable();

   num_paths = (paths <= NUM_DEMUX_PATHS) ? paths : NUM_DEMUX_PATHS;
   demuxes = (S_DEMUX *)calloc(num_paths, sizeof(S_DEMUX));
   if (demuxes == NULL)
   {
      num_paths = 0;
   }

   for (i = 0; i < num_paths; i++)
   {
      dmx = &demuxes[i];
      dmx->path = i;

      /* Every demux can monitor SI from a tuner; none decodes or records */
      dmx->caps = DMX_CAPS_MONITOR_SI;
      if (i == 0)
      {
         dmx->caps |= DMX_CAPS_LIVE;
      }
      dmx->source = DMX_TUNER;
      dmx->source_param = 0;

      pthread_mutex_init(&dmx->mutex, NULL);
      pthread_mutex_init(&dmx->queue_mutex, NULL);
      pthread_cond_init(&dmx->queue_not_empty, NULL);
      pthread_cond_init(&dmx->queue_not_full, NULL);

      for (f = 0; f < MAX_PID_FILTERS; f++)
      {
         dmx->filters[f].pid = PID_NOT_USED;
      }

      STB_OSCreateTask(DeliveryTask, dmx, DMX_TASK_STACK_SIZE, DMX_TASK_PRIORITY, (U8BIT *)"NippDmx");
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
      pthread_mutex_lock(&demuxes[path].mutex);
      demuxes[path].source = source;
      demuxes[path].source_param = param;
      pthread_mutex_unlock(&demuxes[path].mutex);
   }
}

void STB_DMXGetDemuxSource(U8BIT path, E_STB_DMX_DEMUX_SOURCE *source, U8BIT *param)
{
   if (path < num_paths)
   {
      *source = demuxes[path].source;
      *param = demuxes[path].source_param;
   }
}

U16BIT STB_DMXGrabPIDFilter(U8BIT path, U16BIT pid, FILTER_CALLBACK func)
{
   S_DEMUX *dmx;
   S_PID_FILTER *filter;
   U16BIT id;
   U8BIT f;
   U8BIT h;
   U8BIT s;

   FUNCTION_START(STB_DMXGrabPIDFilter);

   id = STB_DMX_PID_FILTER_INVALID;

   if (path < num_paths)
   {
      dmx = &demuxes[path];
      pthread_mutex_lock(&dmx->mutex);

      /* Clients filtering the same PID share a filter, each with its own handler */
      for (f = 0; (f < MAX_PID_FILTERS) && (dmx->filters[f].pid != pid); f++)
      {
      }

      if (f < MAX_PID_FILTERS)
      {
         filter = &dmx->filters[f];
         for (h = 0; (h < MAX_HANDLERS_PER_PID) && (filter->func[h] != NULL); h++)
         {
         }
         if (h < MAX_HANDLERS_PER_PID)
         {
            filter->func[h] = func;
            filter->start_count[h] = 0;
            id = (U16BIT)((f << 8) | (h << 4));
         }
      }
      else
      {
         for (f = 0; (f < MAX_PID_FILTERS) && (dmx->filters[f].pid != PID_NOT_USED); f++)
         {
         }
         if (f < MAX_PID_FILTERS)
         {
            filter = &dmx->filters[f];
            filter->pid = pid;
            filter->generation++;
            filter->started = FALSE;
            memset(filter->func, 0, sizeof(filter->func));
            memset(filter->start_count, 0, sizeof(filter->start_count));
            for (s = 0; s < MAX_SECTION_FILTERS; s++)
            {
               filter->sect[s].allocated = FALSE;
               filter->sect[s].setup = FALSE;
            }
            filter->func[0] = func;
            ResetReassembly(filter);
            id = (U16BIT)(f << 8);
         }
      }

      pthread_mutex_unlock(&dmx->mutex);
   }

   FUNCTION_FINISH(STB_DMXGrabPIDFilter);

   return id;
}

void STB_DMXReleasePIDFilter(U8BIT path, U16BIT pfilt_id)
{
   S_DEMUX *dmx;
   S_PID_FILTER *filter;
   U8BIT handler;
   U8BIT i;
   BOOLEAN in_use;

   FUNCTION_START(STB_DMXReleasePIDFilter);

   if (path < num_paths)
   {
      dmx = &demuxes[path];
      pthread_mutex_lock(&dmx->mutex);

      filter = GetFilter(dmx, pfilt_id);
      handler = (pfilt_id >> 4) & 0x0f;
      if ((filter != NULL) && (handler < MAX_HANDLERS_PER_PID))
      {
         filter->func[handler] = NULL;
         filter->start_count[handler] = 0;

         in_use = FALSE;
         for (i = 0; i < MAX_HANDLERS_PER_PID; i++)
         {
            if (filter->func[i] != NULL)
            {
               in_use = TRUE;
            }
         }
         if (!in_use)
         {
            filter->pid = PID_NOT_USED;
            filter->generation++;
            filter->started = FALSE;
            for (i = 0; i < MAX_SECTION_FILTERS; i++)
            {
               filter->sect[i].allocated = FALSE;
               filter->sect[i].setup = FALSE;
            }
         }
      }

      pthread_mutex_unlock(&dmx->mutex);
   }

   FUNCTION_FINISH(STB_DMXReleasePIDFilter);
}

U16BIT STB_DMXGrabSectFilter(U8BIT path, U16BIT pfilt_id)
{
   S_DEMUX *dmx;
   S_PID_FILTER *filter;
   U16BIT id;
   U8BIT s;

   FUNCTION_START(STB_DMXGrabSectFilter);

   id = STB_DMX_SECT_FILTER_INVALID;

   if (path < num_paths)
   {
      dmx = &demuxes[path];
      pthread_mutex_lock(&dmx->mutex);

      filter = GetFilter(dmx, pfilt_id);
      if (filter != NULL)
      {
         for (s = 0; (s < MAX_SECTION_FILTERS) && filter->sect[s].allocated; s++)
         {
         }
         if (s < MAX_SECTION_FILTERS)
         {
            memset(&filter->sect[s], 0, sizeof(S_SECT_FILTER));
            filter->sect[s].allocated = TRUE;
            filter->sect[s].handler = (pfilt_id >> 4) & 0x0f;
            id = (U16BIT)((pfilt_id & 0xfff0) | s);
         }
      }

      pthread_mutex_unlock(&dmx->mutex);
   }

   FUNCTION_FINISH(STB_DMXGrabSectFilter);

   return id;
}

void STB_DMXReleaseSectFilter(U8BIT path, U16BIT sfilt_id)
{
   S_DEMUX *dmx;
   S_PID_FILTER *filter;

   FUNCTION_START(STB_DMXReleaseSectFilter);

   if (path < num_paths)
   {
      dmx = &demuxes[path];
      pthread_mutex_lock(&dmx->mutex);

      filter = GetFilter(dmx, sfilt_id);
      if (filter != NULL)
      {
         filter->sect[sfilt_id & 0x0f].allocated = FALSE;
         filter->sect[sfilt_id & 0x0f].setup = FALSE;
      }

      pthread_mutex_unlock(&dmx->mutex);
   }

   FUNCTION_FINISH(STB_DMXReleaseSectFilter);
}

void STB_DMXSetupSectFilter(U8BIT path, U16BIT sfilt_id, U8BIT *match_ptr, U8BIT *mask_ptr,
   U8BIT not_equal_byte_index, BOOLEAN crc)
{
   S_DEMUX *dmx;
   S_PID_FILTER *filter;
   S_SECT_FILTER *sect;

   FUNCTION_START(STB_DMXSetupSectFilter);

   USE_UNWANTED_PARAM(not_equal_byte_index);

   if (path < num_paths)
   {
      dmx = &demuxes[path];
      pthread_mutex_lock(&dmx->mutex);

      filter = GetFilter(dmx, sfilt_id);
      if (filter != NULL)
      {
         sect = &filter->sect[sfilt_id & 0x0f];
         memcpy(sect->match, match_ptr, SECTION_FILTER_LENGTH);
         memcpy(sect->mask, mask_ptr, SECTION_FILTER_LENGTH);
         sect->check_crc = crc;
         sect->setup = TRUE;
      }

      pthread_mutex_unlock(&dmx->mutex);
   }

   FUNCTION_FINISH(STB_DMXSetupSectFilter);
}

void STB_DMXStartPIDFilter(U8BIT path, U16BIT pfilt_id)
{
   S_DEMUX *dmx;
   S_PID_FILTER *filter;
   U8BIT handler;

   FUNCTION_START(STB_DMXStartPIDFilter);

   if (path < num_paths)
   {
      dmx = &demuxes[path];
      pthread_mutex_lock(&dmx->mutex);

      filter = GetFilter(dmx, pfilt_id);
      handler = (pfilt_id >> 4) & 0x0f;
      if ((filter != NULL) && (handler < MAX_HANDLERS_PER_PID))
      {
         if (!filter->started)
         {
            ResetReassembly(filter);
            filter->started = TRUE;
         }
         filter->start_count[handler]++;
      }

      pthread_mutex_unlock(&dmx->mutex);
   }

   FUNCTION_FINISH(STB_DMXStartPIDFilter);
}

void STB_DMXStopPIDFilter(U8BIT path, U16BIT pfilt_id)
{
   S_DEMUX *dmx;
   S_PID_FILTER *filter;
   U8BIT handler;
   U8BIT i;
   BOOLEAN running;

   FUNCTION_START(STB_DMXStopPIDFilter);

   if (path < num_paths)
   {
      dmx = &demuxes[path];
      pthread_mutex_lock(&dmx->mutex);

      filter = GetFilter(dmx, pfilt_id);
      handler = (pfilt_id >> 4) & 0x0f;
      if ((filter != NULL) && (handler < MAX_HANDLERS_PER_PID))
      {
         if (filter->start_count[handler] > 0)
         {
            filter->start_count[handler]--;
         }

         running = FALSE;
         for (i = 0; i < MAX_HANDLERS_PER_PID; i++)
         {
            if (filter->start_count[i] != 0)
            {
               running = TRUE;
            }
         }
         if (!running)
         {
            filter->started = FALSE;
            ResetReassembly(filter);
         }
      }

      pthread_mutex_unlock(&dmx->mutex);
   }

   FUNCTION_FINISH(STB_DMXStopPIDFilter);
}

BOOLEAN STB_DMXCopyPIDFilterSect(U8BIT path, U8BIT *buffer, U16BIT size, U16BIT pfilt_id)
{
   S_PID_FILTER *filter;
   U16BIT bytes;
   BOOLEAN copied;

   FUNCTION_START(STB_DMXCopyPIDFilterSect);

   copied = FALSE;

   /* Only called from a filter callback, on the delivery task, which owns delivery_data */
   if (path < num_paths)
   {
      filter = GetFilter(&demuxes[path], pfilt_id);
      if ((filter != NULL) && (filter->delivery_data != NULL))
      {
         bytes = (filter->delivery_size < size) ? filter->delivery_size : size;
         memcpy(buffer, filter->delivery_data, bytes);
         copied = TRUE;
      }
   }

   FUNCTION_FINISH(STB_DMXCopyPIDFilterSect);

   return copied;
}

void STB_DMXSkipPIDFilterSect(U8BIT path, U16BIT pfilt_id)
{
   /* Sections not copied are discarded after the callback returns */
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(pfilt_id);
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

/*---NippApp platform functions-----------------------------------------------*/

void DMX_FeedPackets(U8BIT tuner_path, U8BIT *packets, U32BIT num_packets)
{
   S_DEMUX *dmx;
   S_SECTION done[MAX_SECTIONS_PER_PACKET];
   U16BIT num_done;
   U32BIT p;
   U8BIT i;

   for (i = 0; i < num_paths; i++)
   {
      dmx = &demuxes[i];
      if ((dmx->source != DMX_TUNER) || (dmx->source_param != tuner_path))
      {
         continue;
      }

      for (p = 0; p < num_packets; p++)
      {
         num_done = 0;

         pthread_mutex_lock(&dmx->mutex);
         HandlePacket(dmx, packets + (p * TS_PACKET_SIZE), done, &num_done);
         pthread_mutex_unlock(&dmx->mutex);

         /* Queued outside the filter lock: a full queue makes the tuner wait for the
          * delivery task, which needs the lock to deliver */
         if (num_done != 0)
         {
            QueueSections(dmx, done, num_done);
         }
      }
   }
}

/*---local function definitions-----------------------------------------------*/

static void InitCrcTable(void)
{
   U32BIT i;
   U32BIT c;
   U8BIT bit;

   for (i = 0; i < 256; i++)
   {
      c = i << 24;
      for (bit = 0; bit < 8; bit++)
      {
         c = (c & 0x80000000) ? ((c << 1) ^ 0x04c11db7) : (c << 1);
      }
      crc_table[i] = c;
   }
}

/* MPEG-2 CRC32 over the whole section, CRC included, is zero when the section is intact.
 * Sections without the syntax indicator (TDT, some private sections) have no CRC. */
static BOOLEAN SectionCrcOk(const U8BIT *data, U16BIT size)
{
   U32BIT crc;
   U16BIT i;

   if ((data[1] & 0x80) == 0)
   {
      return TRUE;
   }

   crc = 0xffffffff;
   for (i = 0; i < size; i++)
   {
      crc = (crc << 8) ^ crc_table[((crc >> 24) ^ data[i]) & 0xff];
   }

   return (crc == 0);
}

static S_PID_FILTER* GetFilter(S_DEMUX *dmx, U16BIT id)
{
   U8BIT f = (id >> 8) & 0xff;

   if ((f < MAX_PID_FILTERS) && (dmx->filters[f].pid != PID_NOT_USED))
   {
      return &dmx->filters[f];
   }
   return NULL;
}

static void ResetReassembly(S_PID_FILTER *filter)
{
   filter->cc = CC_INVALID;
   filter->collecting = FALSE;
   filter->length = 0;
   filter->needed = 0;
}

/* Whether any section filter on this PID could want a section with this table_id, so that
 * unwanted sections are not copied */
static BOOLEAN TableIdWanted(S_PID_FILTER *filter, U8BIT table_id)
{
   U8BIT s;

   for (s = 0; s < MAX_SECTION_FILTERS; s++)
   {
      if (filter->sect[s].allocated &&
          (!filter->sect[s].setup || ((filter->sect[s].mask[0] & (table_id ^ filter->sect[s].match[0])) == 0)))
      {
         return TRUE;
      }
   }
   return FALSE;
}

/* Byte 0 of the match/mask is the table_id and bytes 1 and 2 the table_id_extension, as in
 * App's demux. A handler receives a section when any of its section filters matches. */
static BOOLEAN SectionMatches(S_PID_FILTER *filter, U8BIT handler, const U8BIT *data, U16BIT size,
   BOOLEAN crc_ok)
{
   S_SECT_FILTER *sect;
   U8BIT s;
   U8BIT diff;

   for (s = 0; s < MAX_SECTION_FILTERS; s++)
   {
      sect = &filter->sect[s];
      if (!sect->allocated || (sect->handler != handler))
      {
         continue;
      }
      if (!crc_ok && sect->check_crc)
      {
         continue;
      }
      if (!sect->setup)
      {
         return TRUE;
      }

      diff = sect->mask[0] & (data[0] ^ sect->match[0]);
      if (size >= 5)
      {
         diff |= sect->mask[1] & (data[3] ^ sect->match[1]);
         diff |= sect->mask[2] & (data[4] ^ sect->match[2]);
      }
      if (diff == 0)
      {
         return TRUE;
      }
   }
   return FALSE;
}

static void HandlePacket(S_DEMUX *dmx, U8BIT *packet, S_SECTION *done, U16BIT *num_done)
{
   S_PID_FILTER *filter;
   U16BIT pid;
   U8BIT afc;
   U8BIT cc;
   U8BIT f;
   U8BIT *payload;
   U16BIT len;
   U16BIT pointer;
   U16BIT used;

   /* Packets with the transport error indicator set are dropped */
   if ((packet[0] != 0x47) || ((packet[1] & 0x80) != 0))
   {
      return;
   }

   pid = ((packet[1] & 0x1f) << 8) | packet[2];
   afc = (packet[3] >> 4) & 0x3;
   if ((afc & 0x1) == 0)
   {
      return;
   }

   for (f = 0; f < MAX_PID_FILTERS; f++)
   {
      if ((dmx->filters[f].pid == pid) && dmx->filters[f].started)
      {
         break;
      }
   }
   if (f == MAX_PID_FILTERS)
   {
      return;
   }
   filter = &dmx->filters[f];

   /* Continuity: a repeated packet is ignored, a lost one abandons the section being collected */
   cc = packet[3] & 0x0f;
   if (filter->cc != CC_INVALID)
   {
      if (cc == ((filter->cc - 1) & 0x0f))
      {
         return;
      }
      if (cc != filter->cc)
      {
         filter->collecting = FALSE;
      }
   }
   filter->cc = (cc + 1) & 0x0f;

   payload = packet + 4;
   len = TS_PACKET_SIZE - 4;
   if ((afc & 0x2) != 0)
   {
      if (packet[4] >= len)
      {
         return;
      }
      payload += packet[4] + 1;
      len -= packet[4] + 1;
   }

   if ((packet[1] & 0x40) == 0)
   {
      /* Continuation of the section being collected; anything after its end is stuffing */
      if (filter->collecting)
      {
         CollectBytes(filter, f, payload, len, done, num_done);
      }
      return;
   }

   /* A section starts in this packet: the pointer field gives the end of the previous one */
   pointer = payload[0];
   payload++;
   len--;
   if (pointer > len)
   {
      filter->collecting = FALSE;
      return;
   }
   if (filter->collecting)
   {
      CollectBytes(filter, f, payload, pointer, done, num_done);
   }
   filter->collecting = FALSE;
   payload += pointer;
   len -= pointer;

   /* New sections, until stuffing or the end of the packet */
   while ((len > 0) && (payload[0] != 0xff) && (*num_done < MAX_SECTIONS_PER_PACKET))
   {
      filter->length = 0;
      filter->needed = 0;
      filter->collecting = TableIdWanted(filter, payload[0]);
      if (len < 3)
      {
         /* The length field continues in the next packet */
         if (filter->collecting)
         {
            CollectBytes(filter, f, payload, len, done, num_done);
         }
         break;
      }

      used = 3 + (((payload[1] & 0x0f) << 8) | payload[2]);
      if (filter->collecting)
      {
         CollectBytes(filter, f, payload, (used < len) ? used : len, done, num_done);
      }
      if (used >= len)
      {
         /* Not complete: the rest arrives in the following packets */
         filter->collecting = filter->collecting && (used > len);
         break;
      }
      payload += used;
      len -= used;
   }
}

/* Adds bytes to the section being collected, and completes it when all have arrived.
 * Returns the number of bytes used. */
static U16BIT CollectBytes(S_PID_FILTER *filter, U8BIT index, const U8BIT *data, U16BIT len,
   S_SECTION *done, U16BIT *num_done)
{
   U16BIT take;
   U16BIT used;
   S_SECTION *section;

   used = 0;
   while ((used < len) && filter->collecting)
   {
      if (filter->needed == 0)
      {
         /* Header not complete yet: take bytes up to the length field */
         take = 3 - filter->length;
      }
      else
      {
         take = filter->needed - filter->length;
      }
      if (take > (len - used))
      {
         take = len - used;
      }

      memcpy(filter->buffer + filter->length, data + used, take);
      filter->length += take;
      used += take;

      if ((filter->needed == 0) && (filter->length >= 3))
      {
         filter->needed = 3 + (((filter->buffer[1] & 0x0f) << 8) | filter->buffer[2]);
         if (filter->needed > MAX_SECTION_SIZE)
         {
            filter->collecting = FALSE;
            break;
         }
      }

      if ((filter->needed != 0) && (filter->length == filter->needed))
      {
         filter->collecting = FALSE;
         if (*num_done < MAX_SECTIONS_PER_PACKET)
         {
            section = &done[*num_done];
            section->data = (U8BIT *)malloc(filter->length);
            if (section->data != NULL)
            {
               memcpy(section->data, filter->buffer, filter->length);
               section->size = filter->length;
               section->filter = index;
               section->generation = filter->generation;
               (*num_done)++;
            }
         }
      }
   }

   return used;
}

static void QueueSections(S_DEMUX *dmx, S_SECTION *sections, U16BIT num)
{
   U16BIT i;

   pthread_mutex_lock(&dmx->queue_mutex);
   for (i = 0; i < num; i++)
   {
      while (dmx->queue_count == QUEUE_SIZE)
      {
         pthread_cond_wait(&dmx->queue_not_full, &dmx->queue_mutex);
      }
      dmx->queue[(dmx->queue_head + dmx->queue_count) % QUEUE_SIZE] = sections[i];
      dmx->queue_count++;
   }
   pthread_cond_signal(&dmx->queue_not_empty);
   pthread_mutex_unlock(&dmx->queue_mutex);
}

static void DeliveryTask(void *param)
{
   S_DEMUX *dmx = (S_DEMUX *)param;
   S_SECTION section;
   S_PID_FILTER *filter;
   FILTER_CALLBACK func;
   BOOLEAN crc_ok;
   U8BIT h;

   while (TRUE)
   {
      pthread_mutex_lock(&dmx->queue_mutex);
      while (dmx->queue_count == 0)
      {
         pthread_cond_wait(&dmx->queue_not_empty, &dmx->queue_mutex);
      }
      section = dmx->queue[dmx->queue_head];
      dmx->queue_head = (dmx->queue_head + 1) % QUEUE_SIZE;
      dmx->queue_count--;
      pthread_cond_signal(&dmx->queue_not_full);
      pthread_mutex_unlock(&dmx->queue_mutex);

      crc_ok = SectionCrcOk(section.data, section.size);

      pthread_mutex_lock(&dmx->mutex);
      filter = &dmx->filters[section.filter];
      if ((filter->generation == section.generation) && filter->started)
      {
         filter->delivery_data = section.data;
         filter->delivery_size = section.size;

         for (h = 0; h < MAX_HANDLERS_PER_PID; h++)
         {
            /* The filter may be stopped or released by a callback */
            if ((filter->generation != section.generation) || !filter->started)
            {
               break;
            }
            func = filter->func[h];
            if ((func != NULL) && (filter->start_count[h] != 0) &&
                SectionMatches(filter, h, section.data, section.size, crc_ok))
            {
               /* Callbacks set filters up and stop them, so the lock is not held */
               pthread_mutex_unlock(&dmx->mutex);
               func(dmx->path, section.size, (U16BIT)((section.filter << 8) | (h << 4)));
               pthread_mutex_lock(&dmx->mutex);
            }
         }

         filter->delivery_data = NULL;
         filter->delivery_size = 0;
      }
      pthread_mutex_unlock(&dmx->mutex);

      free(section.data);
   }
}

/******************************************************************************
 * End of file
 *****************************************************************************/
