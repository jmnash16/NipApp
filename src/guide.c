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
 * @brief   NippApp content guide endpoint (TS 103 876 Annex D): a DVB-I client asks the content
 *          guide source of a broadcast service list, whose ScheduleInfoEndpoint is the NIP Content
 *          Guide Manifest (http://dvb.gw/ses.com/dvbi/cg/manifest.xml), for a service's schedule
 *          (?sid=<ServiceIDRef>&start=<s>&end=<s>) or now and next (?sid=...&now_next=true),
 *          clause D.6.2.
 *
 *          The manifest's guide sessions name object carousels by their tags (TagRef); the carousels
 *          carry TV-Anytime documents (TVAMain) already in DVB-I's content guide form. So do the
 *          carousels of the stream's NIP Service Guide Manifest (urn:dvb:metadata:nativeip:
 *          ServiceGuide), which carry now and next. The answer is a TVAMain with the service's events
 *          from all of them and the programmes they refer to.
 *
 *          A schedule query returns the events that start in [start, end), and with inclusive=true
 *          also those that started before start and end after it. Now and next are the event on at
 *          the broadcast time and the one after it.
 *
 *          The documents are kept parsed, and parsed again when their content changes. Asking for
 *          them tunes their carrier and joins their carousels; until they have arrived the answer is
 *          503 with Retry-After.
 * @file    guide.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

/* STB header files */
#include "techtype.h"
#include "stbheap.h"
#include "nip_mgr.h"
#include "nipphw.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
#define CG_MANIFEST_TYPE      "application/xml+dvb-nip-cgm"
#define SG_MANIFEST_LOCATION  "urn:dvb:metadata:nativeip:ServiceGuide"
#define DVB_GW                "http://dvb.gw"
#define MAX_DOCS              64
#define MAX_PATH_LEN          1024
#define MAX_SID_LEN           256
#define MAX_EVENTS            4096
#define GUIDE_RETRY_S         10
#define SEGMENT_S             10800      /* schedule windows start and end on whole 3 hours */
#define DAY_S                 86400
#define WINDOW_EVENTS         10         /* now_next=window: up to this many events before and after */

/* What is asked for */
enum { ASK_SCHEDULE, ASK_NOW_NEXT, ASK_WINDOW };

/*---local typedef structs for this file--------------------------------------*/

/* A guide document kept parsed */
typedef struct
{
   char path[MAX_PATH_LEN];
   U32BIT hash;
   U32BIT length;
   xmlDocPtr doc;
   BOOLEAN seen;              /* still among the documents the manifests name */
} S_GUIDE_DOC;

/* An event selected for the answer */
typedef struct
{
   time_t start;
   time_t end;
   xmlNodePtr event;          /* ScheduleEvent */
   xmlChar *crid;             /* Program/@crid */
   S_GUIDE_DOC *from;
} S_EVENT;

/* The documents an answer is made from */
typedef struct
{
   S_GUIDE_DOC *docs[MAX_DOCS];
   int count;
   BOOLEAN pending;           /* a document named isn't here yet */
} S_SOURCES;

/*---local (static) variable declarations for this file-----------------------*/
static pthread_mutex_t guide_mutex = PTHREAD_MUTEX_INITIALIZER;
static S_GUIDE_DOC cache[MAX_DOCS];

/*---local function prototypes for this file----------------------------------*/
static BOOLEAN QueryValue(const char *query, const char *name, char *value, size_t size);
static BOOLEAN IsElement(xmlNodePtr node, const char *name);
static time_t ParseDateTime(const char *text);
static long ParseDuration(const char *text);
static U32BIT Hash(const U8BIT *data, U32BIT len);
static BOOLEAN CopyDecoded(const char *path, U8BIT **data, U32BIT *len, char **filetype);
static void AddTagRefs(const char *manifest_path, S_SOURCES *sources);
static void AddDocument(const char *path, S_SOURCES *sources);
static void AddResource(void *user_data, const U8BIT *resource);
static int CompareEvents(const void *a, const void *b);
static char* BuildAnswer(S_SOURCES *sources, const char *sid, int ask, time_t from, time_t to,
   BOOLEAN inclusive, U32BIT *len);
static time_t BroadcastTime(void);
static BOOLEAN ParseUnixTime(const char *text, time_t *t);

/*---global function definitions---------------------------------------------*/

int GUIDE_Query(const char *path, const char *query, char **body, U32BIT *len)
{
   S_SOURCES sources;
   U8BIT *data;
   U32BIT data_len;
   char *filetype;
   char sid[MAX_SID_LEN];
   char value[64];
   BOOLEAN inclusive;
   time_t from = 0, to = 0, today;
   int i, status, ask;

   *body = NULL;
   *len = 0;
   if ((query == NULL) || !QueryValue(query, "sid", sid, sizeof(sid)))
   {
      return GUIDE_NOT_A_QUERY;
   }

   /* the path must be a content guide manifest; one a SIF lists that hasn't arrived yet is being
    * received now (asking for it tunes its carrier) */
   if (!CopyDecoded(path, &data, &data_len, &filetype))
   {
      return DVBNIP_IsPathSignalled((const U8BIT *)path) ? 503 : GUIDE_NOT_A_QUERY;
   }
   free(data);
   status = (strncasecmp(filetype, CG_MANIFEST_TYPE, strlen(CG_MANIFEST_TYPE)) == 0) ? 0 : GUIDE_NOT_A_QUERY;
   free(filetype);
   if (status != 0)
   {
      return status;
   }

   /* now_next=true or window (TS 103 770 clause 6.5.3); otherwise a window of the schedule, whose
    * times are whole 3 hours, 6 or 12 hours apart, within 28 days of today (clause 6.5.2.1) */
   ask = ASK_SCHEDULE;
   if (QueryValue(query, "now_next", value, sizeof(value)))
   {
      if (strcmp(value, "true") == 0)
      {
         ask = ASK_NOW_NEXT;
      }
      else if (strcmp(value, "window") == 0)
      {
         ask = ASK_WINDOW;
      }
      else
      {
         return 400;
      }
   }
   inclusive = QueryValue(query, "inclusive", value, sizeof(value)) && (strcmp(value, "true") == 0);
   if (ask == ASK_SCHEDULE)
   {
      if (!QueryValue(query, "start", value, sizeof(value)) || !ParseUnixTime(value, &from) ||
          !QueryValue(query, "end", value, sizeof(value)) || !ParseUnixTime(value, &to) ||
          ((from % SEGMENT_S) != 0) || ((to % SEGMENT_S) != 0) ||
          (((to - from) != 2 * SEGMENT_S) && ((to - from) != 4 * SEGMENT_S)))
      {
         return 400;
      }
      today = BroadcastTime() / DAY_S * DAY_S;
      if ((from < today - 28 * DAY_S) || (to > today + DAY_S + 28 * DAY_S))
      {
         return 400;
      }
   }

   pthread_mutex_lock(&guide_mutex);
   memset(&sources, 0, sizeof(sources));
   for (i = 0; i < MAX_DOCS; i++)
   {
      cache[i].seen = FALSE;
   }
   AddTagRefs(path, &sources);
   AddTagRefs(SG_MANIFEST_LOCATION, &sources);

   if (sources.count == 0)
   {
      status = sources.pending ? 503 : 404;
   }
   else
   {
      *body = BuildAnswer(&sources, sid, ask, from, to, inclusive, len);
      status = (*body != NULL) ? 200 : 500;
   }
   pthread_mutex_unlock(&guide_mutex);
   return status;
}

int GUIDE_RetryAfter(void)
{
   return GUIDE_RETRY_S;
}

/*---local function definitions-----------------------------------------------*/

/* A Unix time: digits only */
static BOOLEAN ParseUnixTime(const char *text, time_t *t)
{
   char *end;
   long long value;

   if ((*text < '0') || (*text > '9'))
   {
      return FALSE;
   }
   value = strtoll(text, &end, 10);
   if ((*end != '\0') || (value <= 0))
   {
      return FALSE;
   }
   *t = (time_t)value;
   return TRUE;
}

/* A query parameter's value, %XX decoded; the first if it is given more than once */
static BOOLEAN QueryValue(const char *query, const char *name, char *value, size_t size)
{
   const char *p, *end;
   size_t name_len = strlen(name);
   size_t n;
   unsigned hex;

   for (p = query; *p != '\0'; p = (*end == '&') ? end + 1 : end)
   {
      end = p + strcspn(p, "&");
      if ((strncmp(p, name, name_len) == 0) && (p[name_len] == '='))
      {
         p += name_len + 1;
         for (n = 0; (p < end) && (n + 1 < size); n++)
         {
            if ((*p == '%') && (end - p >= 3) && (sscanf(p + 1, "%2x", &hex) == 1))
            {
               value[n] = (char)hex;
               p += 3;
            }
            else
            {
               value[n] = (*p == '+') ? ' ' : *p;
               p++;
            }
         }
         value[n] = '\0';
         return TRUE;
      }
   }
   return FALSE;
}

static BOOLEAN IsElement(xmlNodePtr node, const char *name)
{
   return (node->type == XML_ELEMENT_NODE) && (xmlStrcmp(node->name, BAD_CAST name) == 0);
}

/* xs:dateTime, e.g. 2025-02-11T09:40:00Z or 2025-02-11T10:40:00.000+01:00; 0 if it doesn't parse */
static time_t ParseDateTime(const char *text)
{
   struct tm tm;
   int year, mon, day, hour, min, sec, off_h, off_m, used;
   char sign;
   const char *zone;
   time_t t;

   if (sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &mon, &day, &hour, &min, &sec, &used) != 6)
   {
      return 0;
   }
   memset(&tm, 0, sizeof(tm));
   tm.tm_year = year - 1900;
   tm.tm_mon = mon - 1;
   tm.tm_mday = day;
   tm.tm_hour = hour;
   tm.tm_min = min;
   tm.tm_sec = sec;
   t = timegm(&tm);

   zone = text + used;
   while ((*zone == '.') || ((*zone >= '0') && (*zone <= '9')))
   {
      zone++;
   }
   if ((sscanf(zone, "%c%2d:%2d", &sign, &off_h, &off_m) == 3) && ((sign == '+') || (sign == '-')))
   {
      t += ((sign == '+') ? -1 : 1) * (off_h * 3600 + off_m * 60);
   }
   return t;
}

/* xs:duration, e.g. PT1H30M or P1DT2H; -1 if it doesn't parse (years and months aren't used) */
static long ParseDuration(const char *text)
{
   long total = 0;
   double value;
   int used;
   BOOLEAN in_time = FALSE;

   if (*text++ != 'P')
   {
      return -1;
   }
   while (*text != '\0')
   {
      if (*text == 'T')
      {
         in_time = TRUE;
         text++;
         continue;
      }
      if (sscanf(text, "%lf%n", &value, &used) != 1)
      {
         return -1;
      }
      text += used;
      switch (*text)
      {
         case 'D': total += (long)(value * 86400); break;
         case 'H': total += (long)(value * 3600); break;
         case 'M': if (!in_time) return -1; total += (long)(value * 60); break;
         case 'S': total += (long)value; break;
         default: return -1;
      }
      text++;
   }
   return total;
}

static U32BIT Hash(const U8BIT *data, U32BIT len)
{
   U32BIT hash = 2166136261u;
   U32BIT i;

   for (i = 0; i < len; i++)
   {
      hash = (hash ^ data[i]) * 16777619u;
   }
   return hash;
}

/* A file from the NIP library, decoded if it was sent compressed; also starts receiving it */
static BOOLEAN CopyDecoded(const char *path, U8BIT **data, U32BIT *len, char **filetype)
{
   U8BIT *raw, *type, *encoding, *decoded;
   U32BIT raw_len, decoded_len;
   BOOLEAN ok;

   *data = NULL;
   *filetype = NULL;
   if (!DVBNIP_CopyFileEncoded((const U8BIT *)path, &raw, &raw_len, &type, &encoding))
   {
      return FALSE;
   }
   ok = TRUE;
   if (encoding != NULL)
   {
      ok = ENC_Decode((const char *)encoding, raw, raw_len, &decoded, &decoded_len);
      STB_FreeMemory(encoding);
   }
   else
   {
      decoded = (U8BIT *)malloc(raw_len + 1);
      ok = (decoded != NULL);
      if (ok)
      {
         memcpy(decoded, raw, raw_len);
         decoded[raw_len] = '\0';
         decoded_len = raw_len;
      }
   }
   STB_FreeMemory(raw);
   if (ok)
   {
      *filetype = strdup((type != NULL) ? (const char *)type : "");
      ok = (*filetype != NULL);
      if (!ok)
      {
         free(decoded);
      }
   }
   if (type != NULL)
   {
      STB_FreeMemory(type);
   }
   if (!ok)
   {
      return FALSE;
   }
   *data = decoded;
   *len = decoded_len;
   return TRUE;
}

/* Asks the NIP library for a path, which tunes its carrier and joins its session */
static void StartReceiving(const char *path)
{
   U8BIT *data, *type;
   U32BIT len;

   if (DVBNIP_CopyFile((const U8BIT *)path, &data, &len, &type))
   {
      STB_FreeMemory(data);
      if (type != NULL)
      {
         STB_FreeMemory(type);
      }
   }
}

/* The documents of the carousels a guide manifest's TagRefs name */
static void AddTagRefs(const char *manifest_path, S_SOURCES *sources)
{
   U8BIT *data;
   U32BIT len;
   char *filetype;
   xmlDocPtr doc;
   xmlNodePtr node;
   xmlChar *text;
   const char *path;

   if (!CopyDecoded(manifest_path, &data, &len, &filetype))
   {
      return;
   }
   free(filetype);
   doc = xmlReadMemory((const char *)data, (int)len, NULL, NULL, XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
   free(data);
   if (doc == NULL)
   {
      return;
   }
   for (node = xmlDocGetRootElement(doc); node != NULL; )
   {
      if (IsElement(node, "TagRef"))
      {
         text = xmlNodeGetContent(node);
         if (text != NULL)
         {
            path = (const char *)text;
            if (strncmp(path, DVB_GW, strlen(DVB_GW)) == 0)
            {
               path += strlen(DVB_GW);
            }
            /* the tag's carousel: its files once its configuration is known; asking for the tag
             * tunes its carrier and joins it */
            if (!DVBNIP_ListCarouselResources((const U8BIT *)path, sources, AddResource))
            {
               StartReceiving(path);
               sources->pending = TRUE;
            }
            xmlFree(text);
         }
      }
      /* depth first through the document */
      if ((node->children != NULL) && (node->type == XML_ELEMENT_NODE))
      {
         node = node->children;
      }
      else
      {
         while ((node != NULL) && (node->next == NULL))
         {
            node = node->parent;
            if ((node != NULL) && (node->type == XML_DOCUMENT_NODE))
            {
               node = NULL;
            }
         }
         if (node != NULL)
         {
            node = node->next;
         }
      }
   }
   xmlFreeDoc(doc);
}

/* A carousel's file: guide documents are XML (the others are its images) */
static void AddResource(void *user_data, const U8BIT *resource)
{
   const char *ext = strrchr((const char *)resource, '.');

   if ((ext != NULL) && (strcasecmp(ext, ".xml") == 0))
   {
      AddDocument((const char *)resource, (S_SOURCES *)user_data);
   }
}

/* A guide document, parsed again only when its content has changed */
static void AddDocument(const char *path, S_SOURCES *sources)
{
   U8BIT *data;
   U32BIT len, hash;
   char *filetype;
   S_GUIDE_DOC *entry, *free_entry;
   int i;

   if (sources->count >= MAX_DOCS)
   {
      return;
   }
   if (!CopyDecoded(path, &data, &len, &filetype))
   {
      sources->pending = TRUE;
      return;
   }
   free(filetype);
   hash = Hash(data, len);

   entry = NULL;
   free_entry = NULL;
   for (i = 0; (i < MAX_DOCS) && (entry == NULL); i++)
   {
      if ((cache[i].doc != NULL) && (strcmp(cache[i].path, path) == 0))
      {
         entry = &cache[i];
      }
      else if ((cache[i].doc == NULL) && (free_entry == NULL))
      {
         free_entry = &cache[i];
      }
   }
   if ((entry != NULL) && ((entry->hash != hash) || (entry->length != len)))
   {
      xmlFreeDoc(entry->doc);
      entry->doc = NULL;
      free_entry = entry;
      entry = NULL;
   }
   if ((entry == NULL) && (free_entry != NULL) && (strlen(path) < MAX_PATH_LEN))
   {
      free_entry->doc = xmlReadMemory((const char *)data, (int)len, NULL, NULL,
         XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR | XML_PARSE_HUGE);
      if (free_entry->doc != NULL)
      {
         strcpy(free_entry->path, path);
         free_entry->hash = hash;
         free_entry->length = len;
         entry = free_entry;
      }
   }
   free(data);

   if ((entry != NULL) && !entry->seen)
   {
      entry->seen = TRUE;
      sources->docs[sources->count++] = entry;
   }
}

static int CompareEvents(const void *a, const void *b)
{
   const S_EVENT *x = (const S_EVENT *)a;
   const S_EVENT *y = (const S_EVENT *)b;

   return (x->start < y->start) ? -1 : (x->start > y->start) ? 1 : 0;
}

/* The ScheduleEvents of a service in a document, with their times */
static int CollectEvents(S_GUIDE_DOC *doc, const char *sid, S_EVENT *events, int count, BOOLEAN *known)
{
   xmlNodePtr root, pd, plt, schedule, event, child;
   xmlChar *ref, *text;
   time_t start;
   long duration;

   root = xmlDocGetRootElement(doc->doc);
   for (pd = (root == NULL) ? NULL : root->children; pd != NULL; pd = pd->next)
   {
      if (!IsElement(pd, "ProgramDescription")) continue;
      for (plt = pd->children; plt != NULL; plt = plt->next)
      {
         if (!IsElement(plt, "ProgramLocationTable")) continue;
         for (schedule = plt->children; schedule != NULL; schedule = schedule->next)
         {
            if (!IsElement(schedule, "Schedule")) continue;
            ref = xmlGetProp(schedule, BAD_CAST "serviceIDRef");
            if ((ref == NULL) || (strcmp((const char *)ref, sid) != 0))
            {
               xmlFree(ref);
               continue;
            }
            xmlFree(ref);
            *known = TRUE;
            for (event = schedule->children; (event != NULL) && (count < MAX_EVENTS); event = event->next)
            {
               if (!IsElement(event, "ScheduleEvent")) continue;
               start = 0;
               duration = -1;
               events[count].crid = NULL;
               for (child = event->children; child != NULL; child = child->next)
               {
                  if (IsElement(child, "PublishedStartTime") && ((text = xmlNodeGetContent(child)) != NULL))
                  {
                     start = ParseDateTime((const char *)text);
                     xmlFree(text);
                  }
                  else if (IsElement(child, "PublishedDuration") && ((text = xmlNodeGetContent(child)) != NULL))
                  {
                     duration = ParseDuration((const char *)text);
                     xmlFree(text);
                  }
                  else if (IsElement(child, "Program") && (events[count].crid == NULL))
                  {
                     events[count].crid = xmlGetProp(child, BAD_CAST "crid");
                  }
               }
               if ((start == 0) || (duration < 0))
               {
                  xmlFree(events[count].crid);
                  continue;
               }
               events[count].start = start;
               events[count].end = start + duration;
               events[count].event = event;
               events[count].from = doc;
               count++;
            }
         }
      }
   }
   return count;
}

/* The ProgramInformation of a programme in a document, or NULL */
static xmlNodePtr FindProgram(S_GUIDE_DOC *doc, const xmlChar *crid)
{
   xmlNodePtr root, pd, pit, pi;
   xmlChar *id;

   root = xmlDocGetRootElement(doc->doc);
   for (pd = (root == NULL) ? NULL : root->children; pd != NULL; pd = pd->next)
   {
      if (!IsElement(pd, "ProgramDescription")) continue;
      for (pit = pd->children; pit != NULL; pit = pit->next)
      {
         if (!IsElement(pit, "ProgramInformationTable")) continue;
         for (pi = pit->children; pi != NULL; pi = pi->next)
         {
            if (!IsElement(pi, "ProgramInformation")) continue;
            id = xmlGetProp(pi, BAD_CAST "programId");
            if ((id != NULL) && (xmlStrcmp(id, crid) == 0))
            {
               xmlFree(id);
               return pi;
            }
            xmlFree(id);
         }
      }
   }
   return NULL;
}

/* The broadcast time: the NIP wall clock, else the capture's, else the local clock */
static time_t BroadcastTime(void)
{
   U32BIT seconds;
   U16BIT ms;
   double utc;

   if (DVBNIP_GetWallClock(&seconds, &ms))
   {
      return (time_t)seconds;
   }
   if (TUN_GetBroadcastTime(&utc))
   {
      return (time_t)utc;
   }
   return time(NULL);
}

static void FormatTime(time_t t, char *text, size_t size)
{
   struct tm tm;

   gmtime_r(&t, &tm);
   strftime(text, size, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* A TVAMain with the service's chosen events and their programmes */
static char* BuildAnswer(S_SOURCES *sources, const char *sid, int ask, time_t from, time_t to,
   BOOLEAN inclusive, U32BIT *len)
{
   S_EVENT *events;
   int count, kept, i, j, first, last, current;
   time_t now;
   BOOLEAN known = FALSE;
   xmlDocPtr out;
   xmlNodePtr root, pd, pit, plt, schedule, copy, program;
   xmlNsPtr ns;
   xmlChar *dump;
   int dump_len;
   char text[32];
   char *body;
   BOOLEAN duplicate;

   events = (S_EVENT *)calloc(MAX_EVENTS, sizeof(S_EVENT));
   if (events == NULL)
   {
      return NULL;
   }
   count = 0;
   for (i = 0; i < sources->count; i++)
   {
      count = CollectEvents(sources->docs[i], sid, events, count, &known);
   }
   qsort(events, count, sizeof(S_EVENT), CompareEvents);

   /* the same event may be in the content guide and the service guide */
   kept = 0;
   for (i = 0; i < count; i++)
   {
      duplicate = (kept > 0) && (events[kept - 1].start == events[i].start) &&
                  (events[kept - 1].crid != NULL) && (events[i].crid != NULL) &&
                  (xmlStrcmp(events[kept - 1].crid, events[i].crid) == 0);
      if (duplicate)
      {
         xmlFree(events[i].crid);
      }
      else
      {
         events[kept++] = events[i];
      }
   }
   count = kept;

   /* choose the events: now and next, now with up to ten before and after, or those in the window */
   if (ask != ASK_SCHEDULE)
   {
      now = BroadcastTime();
      /* the event on now, else (nothing on) the next one */
      for (current = 0; (current < count) && (events[current].end <= now); current++)
      {
      }
      first = (ask == ASK_WINDOW) ? current - WINDOW_EVENTS : current;
      if (first < 0)
      {
         first = 0;
      }
      last = current + ((ask == ASK_WINDOW) ? WINDOW_EVENTS : ((current < count) && (events[current].start <= now) ? 1 : 0));
      if (last >= count)
      {
         last = count - 1;
      }
      for (i = 0; i < count; i++)
      {
         if ((i < first) || (i > last))
         {
            xmlFree(events[i].crid);
            events[i].crid = NULL;
            events[i].event = NULL;
         }
      }
      from = (first <= last) ? events[first].start : now;
      to = (first <= last) ? events[last].end : now;
   }
   else
   {
      for (i = 0; i < count; i++)
      {
         if (!(((events[i].start >= from) && (events[i].start < to)) ||
               (inclusive && (events[i].start < from) && (events[i].end > from))))
         {
            xmlFree(events[i].crid);
            events[i].crid = NULL;
            events[i].event = NULL;
         }
      }
   }

   /* the answer, in the namespace of the documents (TV-Anytime) */
   out = xmlNewDoc(BAD_CAST "1.0");
   root = xmlNewNode(NULL, BAD_CAST "TVAMain");
   xmlDocSetRootElement(out, root);
   ns = xmlNewNs(root, (sources->count > 0) && (xmlDocGetRootElement(sources->docs[0]->doc)->ns != NULL) ?
      xmlDocGetRootElement(sources->docs[0]->doc)->ns->href : BAD_CAST "urn:tva:metadata:2024", NULL);
   xmlSetNs(root, ns);
   pd = xmlNewChild(root, ns, BAD_CAST "ProgramDescription", NULL);
   pit = xmlNewChild(pd, ns, BAD_CAST "ProgramInformationTable", NULL);
   plt = xmlNewChild(pd, ns, BAD_CAST "ProgramLocationTable", NULL);
   /* a service the guide doesn't know has empty tables (TS 103 770 clause 6.5.2.2); one it knows,
    * a Schedule even if it has no events then (clause 6.5.4.1) */
   schedule = NULL;
   if (known)
   {
      schedule = xmlNewChild(plt, ns, BAD_CAST "Schedule", NULL);
      xmlNewProp(schedule, BAD_CAST "serviceIDRef", BAD_CAST sid);
      FormatTime(from, text, sizeof(text));
      xmlNewProp(schedule, BAD_CAST "start", BAD_CAST text);
      FormatTime(to, text, sizeof(text));
      xmlNewProp(schedule, BAD_CAST "end", BAD_CAST text);
   }

   for (i = 0; i < count; i++)
   {
      if (events[i].event == NULL)
      {
         continue;
      }
      copy = xmlDocCopyNode(events[i].event, out, 1);
      if (copy != NULL)
      {
         xmlAddChild(schedule, copy);
      }
      /* its programme, once */
      duplicate = FALSE;
      for (j = 0; (j < i) && !duplicate; j++)
      {
         duplicate = (events[j].event != NULL) && (events[j].crid != NULL) && (events[i].crid != NULL) &&
                     (xmlStrcmp(events[j].crid, events[i].crid) == 0);
      }
      if (!duplicate && (events[i].crid != NULL) &&
          ((program = FindProgram(events[i].from, events[i].crid)) != NULL) &&
          ((copy = xmlDocCopyNode(program, out, 1)) != NULL))
      {
         xmlAddChild(pit, copy);
      }
   }
   for (i = 0; i < count; i++)
   {
      xmlFree(events[i].crid);
   }
   free(events);

   dump = NULL;
   xmlDocDumpMemoryEnc(out, &dump, &dump_len, "UTF-8");
   xmlFreeDoc(out);
   if (dump == NULL)
   {
      return NULL;
   }
   body = (char *)malloc(dump_len + 1);
   if (body != NULL)
   {
      memcpy(body, dump, dump_len);
      body[dump_len] = '\0';
      *len = (U32BIT)dump_len;
   }
   xmlFree(dump);
   return body;
}

/******************************************************************************
 * End of file
 *****************************************************************************/
