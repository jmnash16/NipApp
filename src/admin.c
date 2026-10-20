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
 * @brief   NippApp admin API: the gateway's state as JSON for /api/status. The player never uses
 *          it; it is for whoever runs the gateway.
 * @file    admin.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* STB header files */
#include "techtype.h"
#include "ap_dbacc.h"
#include "nip_mgr.h"
#include "nipphw.h"

#include "nippapp.h"

/*---local typedef structs for this file--------------------------------------*/

/* The document being written */
typedef struct
{
   char *text;
   size_t len;
   size_t size;
   BOOLEAN failed;
   BOOLEAN first;             /* no item yet in the array being written */
   BOOLEAN in_stream;         /* a stream's "receiving" array is open */
} S_JSON;

/*---local function prototypes for this file----------------------------------*/
static void Append(S_JSON *j, const char *format, ...);
static void AppendString(S_JSON *j, const char *text);
static void AppendTime(S_JSON *j, const char *name, time_t when);
static void AddStream(void *user_data, const S_DVBNIP_STREAM *stream);
static void AddPlaying(void *user_data, const S_DVBNIP_PLAYING *service);
static void AddNetwork(void *user_data, const char *name);

/*---global function definitions---------------------------------------------*/

char* ADMIN_StatusJson(void)
{
   S_JSON j;
   const S_SOURCE *source;
   BOOLEAN running, completed, locked;
   time_t finished, since;
   S32BIT source_index;
   U32BIT bytes, files, max_bytes;
   U32BIT wall_seconds;
   U16BIT wall_ms;
   double utc;
   U16BIT i;

   memset(&j, 0, sizeof(j));

   NIPPAPP_GetScanState(&running, &completed, &finished, &since);
   Append(&j, "{\"version\":");
   AppendString(&j, NIPPAPP_VERSION);
   Append(&j, ",\"uptimeSeconds\":%ld", (long)(time(NULL) - since));
   Append(&j, ",\"scan\":{\"running\":%s,", running ? "true" : "false");
   if (finished != 0)
   {
      Append(&j, "\"lastCompleted\":%s,", completed ? "true" : "false");
   }
   AppendTime(&j, "lastFinished", finished);
   Append(&j, "}");

   /* the broadcast clock, as /time gives it */
   if (DVBNIP_GetWallClock(&wall_seconds, &wall_ms))
   {
      Append(&j, ",");
      AppendTime(&j, "broadcastTime", (time_t)wall_seconds);
   }
   else if (TUN_GetBroadcastTime(&utc))
   {
      Append(&j, ",");
      AppendTime(&j, "broadcastTime", (time_t)utc);
   }

   Append(&j, ",\"sources\":[");
   for (i = 0; i < TUN_GetNumSources(); i++)
   {
      source = TUN_GetSource(i);
      Append(&j, "%s{\"index\":%u,\"frequencyMHz\":%u,\"polarisation\":\"%c\",\"symbolRateKsps\":%u,"
         "\"kind\":\"%s\",\"loop\":%s,\"file\":", (i == 0) ? "" : ",", (unsigned)i,
         (unsigned)source->freq_mhz, source->horizontal ? 'H' : 'V', (unsigned)source->symbol_rate,
         (source->kind == SOURCE_KIND_PCAP) ? "pcap" : "ts", source->loop ? "true" : "false");
      AppendString(&j, source->file);
      Append(&j, "}");
   }

   Append(&j, "],\"tuners\":[");
   for (i = 0; i < TUN_GetNumTuners(); i++)
   {
      if (TUN_GetTunerState((U8BIT)i, &locked, &source_index))
      {
         Append(&j, "%s{\"path\":%u,\"locked\":%s,\"source\":", (i == 0) ? "" : ",", (unsigned)i,
            locked ? "true" : "false");
         Append(&j, (source_index < 0) ? "null}" : "%ld}", (long)source_index);
      }
   }

   /* maxPerCarrier is null for every channel on the carrier */
   Append(&j, "],\"prebuffer\":{\"enabled\":%s,\"maxPerCarrier\":", (NIPPAPP_GetPrebuffer() != 0) ? "true" : "false");
   Append(&j, (NIPPAPP_GetPrebuffer() == DVBNIP_PREBUFFER_ALL) ? "null}" : "%u}", (unsigned)NIPPAPP_GetPrebuffer());

   Append(&j, ",\"streams\":[");
   j.first = TRUE;
   DVBNIP_ListStreams(&j, AddStream, AddPlaying);
   if (j.in_stream)
   {
      Append(&j, "]}");
   }

   DVBNIP_GetStoreUsage(&bytes, &files, &max_bytes);
   Append(&j, "],\"fileStore\":{\"bytes\":%lu,\"files\":%lu,\"maxBytes\":%lu}",
      (unsigned long)bytes, (unsigned long)files, (unsigned long)max_bytes);
   Append(&j, ",\"channels\":%u", (unsigned)ADB_GetNumServicesInList(ADB_SERVICE_LIST_ALL, FALSE));

   Append(&j, ",\"networks\":[");
   j.first = TRUE;
   REG_ListNetworks(&j, AddNetwork);
   Append(&j, "]}\n");

   if (j.failed)
   {
      free(j.text);
      return NULL;
   }
   return j.text;
}

/*---local function definitions-----------------------------------------------*/

static void Append(S_JSON *j, const char *format, ...)
{
   va_list args;
   char *bigger;
   int n;

   if (j->failed)
   {
      return;
   }
   for (;;)
   {
      va_start(args, format);
      n = vsnprintf((j->text == NULL) ? NULL : j->text + j->len, j->size - j->len, format, args);
      va_end(args);
      if (n < 0)
      {
         j->failed = TRUE;
         return;
      }
      if ((j->text != NULL) && ((size_t)n < j->size - j->len))
      {
         j->len += (size_t)n;
         return;
      }
      bigger = (char *)realloc(j->text, j->size + (size_t)n + 4096);
      if (bigger == NULL)
      {
         j->failed = TRUE;
         return;
      }
      j->text = bigger;
      j->size += (size_t)n + 4096;
   }
}

/* A JSON string: quoted, with quotes, backslashes and control characters escaped */
static void AppendString(S_JSON *j, const char *text)
{
   const unsigned char *p;

   Append(j, "\"");
   for (p = (const unsigned char *)text; (p != NULL) && (*p != '\0'); p++)
   {
      if ((*p == '"') || (*p == '\\'))
      {
         Append(j, "\\%c", *p);
      }
      else if (*p < 0x20)
      {
         Append(j, "\\u%04x", *p);
      }
      else
      {
         Append(j, "%c", *p);
      }
   }
   Append(j, "\"");
}

/* "name":"2025-02-11T14:20:56Z", or "name":null for 0 */
static void AppendTime(S_JSON *j, const char *name, time_t when)
{
   struct tm tm;
   char text[32];

   Append(j, "\"%s\":", name);
   if ((when == 0) || (gmtime_r(&when, &tm) == NULL))
   {
      Append(j, "null");
      return;
   }
   strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &tm);
   AppendString(j, text);
}

static void AddStream(void *user_data, const S_DVBNIP_STREAM *stream)
{
   S_JSON *j = (S_JSON *)user_data;

   if (j->in_stream)
   {
      Append(j, "]}");
   }
   Append(j, "%s{\"network\":%u,\"carrier\":%u,\"service\":%u,\"tuned\":%s,\"noTuner\":%s,"
      "\"noSignal\":%s,\"entries\":%u,\"prebuffered\":%u,\"receiving\":[", j->first ? "" : ",",
      (unsigned)stream->nid, (unsigned)stream->tid, (unsigned)stream->svcid, stream->tuned ? "true" : "false",
      stream->no_tuner ? "true" : "false", stream->no_signal ? "true" : "false", (unsigned)stream->services,
      (unsigned)stream->prebuffered);
   j->first = FALSE;
   j->in_stream = TRUE;
}

static void AddPlaying(void *user_data, const S_DVBNIP_PLAYING *service)
{
   S_JSON *j = (S_JSON *)user_data;

   /* no comma straight after the array's '[' */
   if (j->failed)
   {
      return;
   }
   Append(j, "%s{\"path\":", (j->text[j->len - 1] == '[') ? "" : ",");
   AppendString(j, (const char *)service->path);
   Append(j, ",\"clients\":%u,\"idleSeconds\":%lu,\"bytes\":%lu,\"prebuffered\":%s}",
      (unsigned)service->clients, (unsigned long)service->idle_seconds, (unsigned long)service->memory_usage,
      service->prebuffered ? "true" : "false");
}

static void AddNetwork(void *user_data, const char *name)
{
   S_JSON *j = (S_JSON *)user_data;

   Append(j, j->first ? "" : ",");
   AppendString(j, name);
   j->first = FALSE;
}

/******************************************************************************
 * End of file
 *****************************************************************************/
