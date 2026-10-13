/*******************************************************************************
 * Copyright © 2026 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2024 Ocean Blue Software Ltd
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
 * @brief   NippApp HTTP server: serves what the NIP library receives at its broadcast path
 *          (http://dvb.gw/<path> is served at /<path>, TS 103 876 clauses 8.2.7, 8.2.8), so DVB-I
 *          clients can use the broadcast service lists, manifests and segments unchanged.
 *
 *          - Requests under a service's directory start the service, which tunes its carrier on
 *            demand; it stops when its files are no longer requested (the NIP library's logic).
 *          - A file not received yet is waited for, then 404 (clause 8.5.3).
 *          - DVB-I service lists and entry points have http://dvb.gw/ replaced by this server's
 *            address (Annex F.2.1). When the NIP library doesn't have one (its carrier isn't
 *            tuned), the copy kept by the registry is served.
 *          - MPDs get a UTCTiming pointing at /time and a short update period (clause 9.1.2);
 *            AAC init segments get the plain AudioSpecificConfig some browsers need.
 *          - /time gives the NIP wall clock (clauses 8.6.1, 8.6.3), or the TDT/capture time.
 *          - /dvbi/slep.xml gives the DVB-I service list entry points, which the broadcast
 *            identifies by URN only (clause 8.2.5.1).
 *          - /player/ serves the web player's files.
 *
 *          The MPD rewrite and the AAC fix are taken from the test tool in DVBCore/dvbnip/test.
 * @file    httpd.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* third party header files */
#include "mongoose.h"

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbheap.h"
#include "stbllist.h"
#include "flute.h"
#include "nip_mgr.h"
#include "nipphw.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
#define MAX_HOST_LEN          128

/* How often players are told to reload a live MPD. The NIP library stops a service when its files
 * aren't requested for a while, so this keeps it running while the player is. */
#define MPD_UPDATE_PERIOD     "PT2S"

/* Room for the rewritten MPD attributes and UTCTiming element */
#define MPD_REWRITE_SPACE     1024

/* A file that hasn't arrived is waited for, in steps, before answering 404 */
#define HOLD_MS               8000

#define PLAYER_PREFIX         "/player/"
#define MAX_PLAYER_FILE       (8 * 1024 * 1024)
#define MAX_PATH_LEN          1024
#define HOLD_STEP_MS          200

/* The broadcast gives the DVB-I service list entry points a URN, not a URL (clause 8.2.5.1), so
 * the gateway chooses where to serve them (clause 8.1.7.3) */
#define SLEP_PATH             "/dvbi/slep.xml"
#define SLEP_LOCATION         "urn:dvb:metadata:nativeip:dvb-i-slep"

static const char application_dash_xml[] = "application/dash+xml";
static const char dvb_gw_prefix[] = "http://dvb.gw/";

/* Extensions of broadcast files worth waiting for */
static const char *hold_extensions[] = {
   ".mpd", ".m3u8", ".m4s", ".mp4", ".m4v", ".m4a", ".ts", ".cmfv", ".cmfa", ".vtt", ".ttml", ".xml",
   NULL
};

/*---local typedef structs for this file--------------------------------------*/
typedef struct
{
   char *buf;
   U32BIT len;
   U32BIT size;
} S_STRING_BUFFER;

/*---local (static) variable declarations for this file-----------------------*/
static struct mg_context *ctx = NULL;
static char listen_port[16];
static char default_host[MAX_HOST_LEN];
static char player_root[MAX_PATH_LEN];

/* Media types of the player's files, by extension */
static const char *player_types[][2] = {
   {".html", "text/html; charset=utf-8"},
   {".js", "text/javascript"},
   {".mjs", "text/javascript"},
   {".css", "text/css"},
   {".map", "application/json"},
   {".json", "application/json"},
   {".svg", "image/svg+xml"},
   {".png", "image/png"},
   {".md", "text/plain; charset=utf-8"},
   {".txt", "text/plain; charset=utf-8"},
   {NULL, NULL}
};

/*---local function prototypes for this file----------------------------------*/
static int BeginRequestHandler(struct mg_connection *conn);

/*---global function definitions---------------------------------------------*/

BOOLEAN HTTPD_Start(const char *port, const char *player_dir)
{
   struct mg_callbacks callbacks;
   const char *options[] = {"listening_ports", NULL, "num_threads", "32", NULL};

   snprintf(listen_port, sizeof(listen_port), "%s", port);
   snprintf(player_root, sizeof(player_root), "%s", (player_dir != NULL) ? player_dir : "");
   snprintf(default_host, sizeof(default_host), "localhost:%s", port);
   options[1] = listen_port;

   memset(&callbacks, 0, sizeof(callbacks));
   callbacks.begin_request = BeginRequestHandler;

   ctx = mg_start(&callbacks, NULL, options);
   if (ctx == NULL)
   {
      fprintf(stderr, "NippApp: cannot start the HTTP server on port %s; is it in use?\n", port);
      return FALSE;
   }

   printf("NippApp: HTTP server on port %s, player at http://localhost:%s/player/\n", port, port);
   return TRUE;
}

void HTTPD_Stop(void)
{
   if (ctx != NULL)
   {
      mg_stop(ctx);
      ctx = NULL;
   }
}

/*---local function definitions-----------------------------------------------*/

/************************************************************************
 * Responses
 */

static void WriteAll(struct mg_connection *conn, const void *data, U32BIT len)
{
   const char *ptr = (const char *)data;
   int this_write;

   while (len > 0)
   {
      this_write = mg_write(conn, ptr, len);
      if (this_write <= 0)
      {
         break;
      }
      ptr += this_write;
      len -= this_write;
   }
}

/* Every response allows any origin: DVB-I clients are web pages served from elsewhere */
static void SendResponse(struct mg_connection *conn, int status, const char *status_text,
   const char *content_type, const char *extra_headers, const void *body, U32BIT len)
{
   mg_printf(conn,
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %u\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "%s"
      "\r\n",
      status, status_text, content_type, len, extra_headers == NULL ? "" : extra_headers);

   if ((body != NULL) && (len > 0) && (strcmp(mg_get_request_info(conn)->request_method, "HEAD") != 0))
   {
      WriteAll(conn, body, len);
   }
}

static void SendNotFound(struct mg_connection *conn)
{
   SendResponse(conn, 404, "Not Found", "text/plain", "Cache-Control: no-store\r\n", NULL, 0);
}

static void GetHost(struct mg_connection *conn, char *host)
{
   const char *header = mg_get_header(conn, "Host");

   /* Only use the Host header if it's safe to put into a document */
   if ((header == NULL) || (strlen(header) >= MAX_HOST_LEN) || (strpbrk(header, "\"<>&' ") != NULL))
   {
      header = default_host;
   }
   strcpy(host, header);
}

/************************************************************************
 * Index page: what has been received from the announcement channels
 */

static void AppendString(S_STRING_BUFFER *buf, const char *str)
{
   U32BIT len = strlen(str);
   char *grown;

   if (buf->len + len + 1 > buf->size)
   {
      grown = (char *)realloc(buf->buf, buf->size + len + 4096);
      if (grown == NULL)
      {
         return;
      }
      buf->buf = grown;
      buf->size += len + 4096;
   }
   memcpy(buf->buf + buf->len, str, len + 1);
   buf->len += len;
}

/* The path a file is served at: its location without the scheme and host */
static const char* ServedPath(const char *location)
{
   const char *p = strstr(location, "://");

   if (p != NULL)
   {
      p = strchr(p + 3, '/');
      return (p != NULL) ? p : "/";
   }
   return location;
}

static void IndexFileCallback(void *user_data, S_FLUTE_FILE *file)
{
   S_STRING_BUFFER *buf = (S_STRING_BUFFER *)user_data;
   const char *path;
   char line[1024];

   if ((file->filename == NULL) || (file->filetype == NULL) ||
       ((strstr((char *)file->filetype, "dvb-i") == NULL) && (strstr((char *)file->filetype, "dvbisl") == NULL) &&
        (strstr((char *)file->filetype, "dvb-nip") == NULL)))
   {
      return;
   }
   path = ServedPath((char *)file->filename);
   if (strpbrk(path, "\"<>&") != NULL)
   {
      return;
   }
   if (path[0] == '/')
   {
      snprintf(line, sizeof(line), "<li><a href=\"%s\">%s</a> (%s, version %u)</li>\n", path, path,
         (char *)file->filetype, (unsigned)file->version);
   }
   else
   {
      snprintf(line, sizeof(line), "<li>%s (%s, version %u)</li>\n", path, (char *)file->filetype,
         (unsigned)file->version);
   }
   AppendString(buf, line);
}

static void SendIndex(struct mg_connection *conn)
{
   S_STRING_BUFFER buf = {NULL, 0, 0};

   AppendString(&buf, "<!doctype html><html><head><meta charset=\"utf-8\"><title>NippApp</title></head>"
      "<body><h1>NippApp</h1><p><a href=\"" PLAYER_PREFIX "\">Open the player</a></p>"
      "<p>DVB-I service list entry points: <a href=\"" SLEP_PATH "\">" SLEP_PATH
      "</a></p><p>Signalling received from the broadcast "
      "(DVB-I entry points and service lists, NIP signalling):</p><ul>\n");
   DVBNIP_EnumFiles(&buf, IndexFileCallback);
   AppendString(&buf, "</ul></body></html>\n");

   if (buf.buf != NULL)
   {
      SendResponse(conn, 200, "OK", "text/html; charset=utf-8", "Cache-Control: no-store\r\n",
         buf.buf, buf.len);
      free(buf.buf);
   }
}

/************************************************************************
 * Broadcast time (clause 8.6.3): xs:dateTime, with milliseconds for the ms query
 */

static void SendTime(struct mg_connection *conn)
{
   const struct mg_request_info *request_info = mg_get_request_info(conn);
   double utc;
   time_t secs;
   struct tm tm;
   char text[64];
   int len;
   U32BIT wall_seconds;
   U16BIT wall_ms;

   /* The NIP wall clock (EXT_TIME, clause 8.6.1) when the broadcast gives it; otherwise the TDT
    * or capture time the tuner keeps */
   if (DVBNIP_GetWallClock(&wall_seconds, &wall_ms))
   {
      utc = wall_seconds + (wall_ms / 1000.0);
   }
   else if (!TUN_GetBroadcastTime(&utc))
   {
      /* No broadcast time yet: let the player retry rather than use the wrong clock */
      SendResponse(conn, 503, "Service Unavailable", "text/plain", "Cache-Control: no-store\r\n", NULL, 0);
      return;
   }

   secs = (time_t)utc;
   gmtime_r(&secs, &tm);
   len = strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &tm);
   if ((request_info->query_string == NULL) || (strstr(request_info->query_string, "ms") != NULL))
   {
      /* Players need better than a second, so milliseconds are given unless another form is asked */
      len += snprintf(text + len, sizeof(text) - len, ".%03d", (int)((utc - secs) * 1000));
   }
   snprintf(text + len, sizeof(text) - len, "Z");
   SendResponse(conn, 200, "OK", "text/plain", "Cache-Control: no-store\r\n", text, strlen(text));
}

/************************************************************************
 * DVB-I documents: http://dvb.gw/ is replaced by this server's address
 */

static char* RewriteDvbGw(struct mg_connection *conn, const char *doc, U32BIT *len)
{
   char host[MAX_HOST_LEN];
   char replacement[MAX_HOST_LEN + 16];
   const char *p;
   const char *match;
   U32BIT count;
   U32BIT prefix_len;
   U32BIT replacement_len;
   char *out;
   char *q;

   GetHost(conn, host);
   snprintf(replacement, sizeof(replacement), "http://%s/", host);
   prefix_len = strlen(dvb_gw_prefix);
   replacement_len = strlen(replacement);

   count = 0;
   for (p = doc; (p = strstr(p, dvb_gw_prefix)) != NULL; p += prefix_len)
   {
      count++;
   }

   out = (char *)malloc(*len + (count * replacement_len) + 1);
   if (out == NULL)
   {
      return NULL;
   }

   q = out;
   for (p = doc; (match = strstr(p, dvb_gw_prefix)) != NULL; p = match + prefix_len)
   {
      memcpy(q, p, match - p);
      q += match - p;
      memcpy(q, replacement, replacement_len);
      q += replacement_len;
   }
   strcpy(q, p);
   *len = strlen(out);
   return out;
}

/************************************************************************
 * MPD rewriting
 *
 * The UTCTiming is pointed at /time on this server, the broadcast time (clause 9.1.2), which
 * also lets live MPDs from a recording play.  The update period is set, so the player keeps
 * requesting the MPD, which keeps the service running.
 */

static void RemoveText(char *start, U32BIT len)
{
   memmove(start, start + len, strlen(start + len) + 1);
}

static void InsertText(char *at, const char *text)
{
   U32BIT len = strlen(text);
   memmove(at + len, at, strlen(at) + 1);
   memcpy(at, text, len);
}

/* remove all <name .../> and <name ...>...</name> elements */
static void RemoveElements(char *doc, const char *name)
{
   char open_tag[64], close_tag[64];
   char *start, *end;
   U32BIT open_len;

   snprintf(open_tag, sizeof open_tag, "<%s", name);
   snprintf(close_tag, sizeof close_tag, "</%s>", name);
   open_len = strlen(open_tag);

   start = doc;
   while ((start = strstr(start, open_tag)) != NULL)
   {
      /* check it's not an element whose name just starts with this one */
      if (start[open_len] != ' ' && start[open_len] != '/' && start[open_len] != '>' &&
          start[open_len] != '\t' && start[open_len] != '\r' && start[open_len] != '\n')
      {
         start += open_len;
         continue;
      }

      end = strchr(start, '>');
      if (end == NULL)
      {
         break;
      }
      if (end[-1] != '/')
      {
         end = strstr(end, close_tag);
         if (end == NULL)
         {
            break;
         }
         end += strlen(close_tag) - 1;
      }
      RemoveText(start, end + 1 - start);
   }
}

/* remove an attribute from the element starting at tag */
static void RemoveAttribute(char *tag, const char *name)
{
   char pattern[64];
   char *tag_end, *attr, *value_end;

   snprintf(pattern, sizeof pattern, " %s=\"", name);
   tag_end = strchr(tag, '>');
   attr = strstr(tag, pattern);
   if (tag_end != NULL && attr != NULL && attr < tag_end)
   {
      value_end = strchr(attr + strlen(pattern), '"');
      if (value_end != NULL)
      {
         RemoveText(attr, value_end + 1 - attr);
      }
   }
}

static void RewriteMPD(struct mg_connection *conn, char *mpd)
{
   char host[MAX_HOST_LEN];
   char text[MAX_HOST_LEN + 128];
   char *mpd_tag, *mpd_tag_end, *mpd_close;

   mpd_tag = strstr(mpd, "<MPD");
   mpd_close = strstr(mpd, "</MPD>");
   if (mpd_tag == NULL || mpd_close == NULL)
   {
      return;
   }

   RemoveElements(mpd, "UTCTiming");

   /* replace the update period of live MPDs */
   RemoveAttribute(mpd_tag, "minimumUpdatePeriod");
   mpd_tag_end = strchr(mpd_tag, '>');
   if (mpd_tag_end != NULL && strstr(mpd_tag, "type=\"dynamic\"") != NULL &&
       strstr(mpd_tag, "type=\"dynamic\"") < mpd_tag_end)
   {
      InsertText(mpd_tag + strlen("<MPD"), " minimumUpdatePeriod=\"" MPD_UPDATE_PERIOD "\"");
   }

   /* UTCTiming is the last element of the MPD */
   GetHost(conn, host);
   snprintf(text, sizeof text,
      "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://%s/time\"/>\n",
      host);
   InsertText(strstr(mpd, "</MPD>"), text);
}

/************************************************************************
 * AAC configuration fix for init segments
 *
 * An AAC-LC AudioSpecificConfig may end with explicit signalling that SBR is not present
 * (sync extension 0x2B7, extension type 5, sbrPresentFlag 0).  It's valid, and means the same
 * as the plain 2 byte config, but some browser decoders (e.g. Media Foundation on Windows)
 * reject it - so it's reduced to the plain form, and the enclosing sizes adjusted.
 */

#define WORD32(p)             (((U32BIT)(p)[0] << 24) | ((p)[1] << 16) | ((p)[2] << 8) | (p)[3])
#define AAC_LC                2
#define AAC_SBR               5
#define AAC_SYNC_EXTENSION    0x2b7
#define AAC_PLAIN_CONFIG_LEN  2
#define MAX_INIT_SEGMENT_LEN  65536

static void SetWord32(U8BIT *p, U32BIT value)
{
   p[0] = value >> 24;
   p[1] = value >> 16;
   p[2] = value >> 8;
   p[3] = value;
}

static U8BIT* FindBox(U8BIT *start, U8BIT *end, const char *type)
{
   U32BIT size;

   while (start + 8 <= end)
   {
      size = WORD32(start);
      if (size < 8 || size > (U32BIT)(end - start))
      {
         return NULL;
      }
      if (memcmp(start + 4, type, 4) == 0)
      {
         return start;
      }
      start += size;
   }
   return NULL;
}

/* MPEG-4 descriptor length, 7 bits per byte with a continuation flag */
static U8BIT* ReadDescriptor(U8BIT *p, U8BIT *end, U8BIT tag, U32BIT *len, U8BIT **len_ptr, int *len_bytes)
{
   if (p >= end || *p != tag)
   {
      return NULL;
   }
   p++;
   *len_ptr = p;
   *len = 0;
   *len_bytes = 0;
   do
   {
      if (p >= end || *len_bytes == 4)
      {
         return NULL;
      }
      *len = (*len << 7) | (*p & 0x7f);
      (*len_bytes)++;
   }
   while (*p++ & 0x80);

   return p;
}

static void WriteDescriptorLength(U8BIT *p, int len_bytes, U32BIT len)
{
   int i;

   for (i = len_bytes - 1; i >= 0; i--)
   {
      p[i] = (len & 0x7f) | ((i < len_bytes - 1) ? 0x80 : 0);
      len >>= 7;
   }
}

/* returns the length after any change */
static U32BIT FixAACConfig(U8BIT *data, U32BIT len)
{
   U8BIT *end = data + len;
   U8BIT *boxes[8];   /* moov, trak, mdia, minf, stbl, stsd, mp4a, esds */
   U8BIT *p, *es_len_ptr, *dc_len_ptr, *asc_len_ptr, *asc;
   U32BIT es_len, dc_len, asc_len, remove;
   int es_len_bytes, dc_len_bytes, asc_len_bytes, i;
   U8BIT flags;

   if ((boxes[0] = FindBox(data, end, "moov")) == NULL)
   {
      return len;
   }

   /* init segments for DASH have a single track */
   if ((boxes[1] = FindBox(boxes[0] + 8, boxes[0] + WORD32(boxes[0]), "trak")) == NULL ||
       (boxes[2] = FindBox(boxes[1] + 8, boxes[1] + WORD32(boxes[1]), "mdia")) == NULL ||
       (boxes[3] = FindBox(boxes[2] + 8, boxes[2] + WORD32(boxes[2]), "minf")) == NULL ||
       (boxes[4] = FindBox(boxes[3] + 8, boxes[3] + WORD32(boxes[3]), "stbl")) == NULL ||
       (boxes[5] = FindBox(boxes[4] + 8, boxes[4] + WORD32(boxes[4]), "stsd")) == NULL ||
       (boxes[6] = FindBox(boxes[5] + 16, boxes[5] + WORD32(boxes[5]), "mp4a")) == NULL ||
       (boxes[7] = FindBox(boxes[6] + 36, boxes[6] + WORD32(boxes[6]), "esds")) == NULL)
   {
      return len;
   }

   /* ES_Descriptor -> DecoderConfigDescriptor -> DecoderSpecificInfo (the AudioSpecificConfig) */
   end = boxes[7] + WORD32(boxes[7]);
   if ((p = ReadDescriptor(boxes[7] + 12, end, 0x03, &es_len, &es_len_ptr, &es_len_bytes)) == NULL ||
       p + 3 > end)
   {
      return len;
   }
   flags = p[2];
   p += 3;
   if (flags & 0x80) p += 2;                 /* dependsOn_ES_ID */
   if ((flags & 0x40) && p < end) p += 1 + *p; /* URL */
   if (flags & 0x20) p += 2;                 /* OCR_ES_Id */

   if ((p = ReadDescriptor(p, end, 0x04, &dc_len, &dc_len_ptr, &dc_len_bytes)) == NULL ||
       (asc = ReadDescriptor(p + 13, end, 0x05, &asc_len, &asc_len_ptr, &asc_len_bytes)) == NULL ||
       asc_len < 5 || asc + asc_len > end)
   {
      return len;
   }

   /* AAC-LC, frequency given by index (not escaped), GASpecificConfig, then the extension */
   if ((asc[0] >> 3) != AAC_LC ||
       (((asc[0] & 0x7) << 1) | (asc[1] >> 7)) == 0xf ||
       (((asc[2] << 3) | (asc[3] >> 5)) != AAC_SYNC_EXTENSION) ||
       (asc[3] & 0x1f) != AAC_SBR ||
       (asc[4] & 0x80) != 0)
   {
      return len;
   }

   remove = asc_len - AAC_PLAIN_CONFIG_LEN;
   WriteDescriptorLength(asc_len_ptr, asc_len_bytes, AAC_PLAIN_CONFIG_LEN);
   WriteDescriptorLength(dc_len_ptr, dc_len_bytes, dc_len - remove);
   WriteDescriptorLength(es_len_ptr, es_len_bytes, es_len - remove);
   for (i = 0; i < 8; i++)
   {
      SetWord32(boxes[i], WORD32(boxes[i]) - remove);
   }
   p = asc + AAC_PLAIN_CONFIG_LEN;
   memmove(p, p + remove, (data + len) - (p + remove));

   return len - remove;
}

/************************************************************************
 * Files from the NIP file store
 */

/* Whether a path looks like broadcast content, which may still be on its way */
static BOOLEAN WorthWaitingFor(const char *path)
{
   const char *ext = strrchr(path, '.');
   const char **e;

   if ((ext == NULL) || (strchr(ext, '/') != NULL))
   {
      return FALSE;
   }
   for (e = hold_extensions; *e != NULL; e++)
   {
      if (strcasecmp(ext, *e) == 0)
      {
         return TRUE;
      }
   }
   return FALSE;
}

/* prefer_registry: serve the registry's copy before the NIP library's. For the entry points,
 * which the registry keeps whatever location they came with, the NIP library may hold an older
 * network's under the URN. */
static void SendFile(struct mg_connection *conn, const char *path, BOOLEAN prefer_registry)
{
   U8BIT *data;
   U8BIT *filetype;
   U32BIT len;
   U32BIT waited;
   char *body;
   BOOLEAN from_registry;

   /* The NIP library's copy is the latest. Without one, a DVB-I document is served from the
    * registry; anything else under a service's directory starts the service, so it may arrive
    * shortly and is waited for before answering 404. */
   from_registry = prefer_registry && REG_Load(path, &data, &len, (char **)&filetype);
   waited = 0;
   while (!from_registry && !DVBNIP_CopyFile((const U8BIT *)path, &data, &len, &filetype))
   {
      if (REG_Load(path, &data, &len, (char **)&filetype))
      {
         from_registry = TRUE;
         break;
      }
      if (!WorthWaitingFor(path) || (waited >= HOLD_MS))
      {
         SendNotFound(conn);
         return;
      }
      usleep(HOLD_STEP_MS * 1000);
      waited += HOLD_STEP_MS;
   }

   if ((filetype != NULL) && (strncmp((const char *)filetype, application_dash_xml, strlen(application_dash_xml)) == 0))
   {
      body = (char *)malloc(len + MPD_REWRITE_SPACE);
      if (body != NULL)
      {
         memcpy(body, data, len + 1);
         RewriteMPD(conn, body);
         SendResponse(conn, 200, "OK", (const char *)filetype, "Cache-Control: no-store\r\n",
            body, strlen(body));
         free(body);
      }
   }
   else if (REG_IsDvbiType((const char *)filetype))
   {
      body = RewriteDvbGw(conn, (const char *)data, &len);
      if (body != NULL)
      {
         SendResponse(conn, 200, "OK", (const char *)filetype, "Cache-Control: no-store\r\n", body, len);
         free(body);
      }
   }
   else
   {
      if ((filetype != NULL) && (strstr((const char *)filetype, "mp4") != NULL) && (len < MAX_INIT_SEGMENT_LEN))
      {
         len = FixAACConfig(data, len);
      }
      SendResponse(conn, 200, "OK", (filetype == NULL) ? "application/octet-stream" : (const char *)filetype,
         NULL, data, len);
   }

   if (from_registry)
   {
      free(data);
      free(filetype);
   }
   else
   {
      STB_FreeMemory(data);
      if (filetype != NULL)
      {
         STB_FreeMemory(filetype);
      }
   }
}

/************************************************************************
 * The web player's files, from the player directory
 */

static void SendPlayerFile(struct mg_connection *conn, const char *uri)
{
   const char *name = uri + strlen(PLAYER_PREFIX);
   const char *ext;
   const char *type;
   char filename[MAX_PATH_LEN];
   FILE *f;
   long size;
   char *data;
   int i;

   if (*name == '\0')
   {
      name = "player.html";
   }

   /* Only plain names below the player directory */
   if ((player_root[0] == '\0') || (strstr(name, "..") != NULL) || (strstr(name, "//") != NULL) ||
       (name[0] == '/') || ((size_t)snprintf(filename, sizeof(filename), "%s/%s", player_root, name) >= sizeof(filename)))
   {
      SendNotFound(conn);
      return;
   }

   type = NULL;
   ext = strrchr(name, '.');
   for (i = 0; (ext != NULL) && (player_types[i][0] != NULL); i++)
   {
      if (strcasecmp(ext, player_types[i][0]) == 0)
      {
         type = player_types[i][1];
      }
   }
   if ((type == NULL) && (strchr(name, '.') == NULL))
   {
      /* LICENSE, VERSION */
      type = "text/plain; charset=utf-8";
   }

   f = (type != NULL) ? fopen(filename, "rb") : NULL;
   if (f == NULL)
   {
      SendNotFound(conn);
      return;
   }
   data = NULL;
   if ((fseek(f, 0, SEEK_END) == 0) && ((size = ftell(f)) >= 0) && (size <= MAX_PLAYER_FILE) &&
       (fseek(f, 0, SEEK_SET) == 0) && ((data = (char *)malloc(size + 1)) != NULL) &&
       (fread(data, 1, size, f) == (size_t)size))
   {
      SendResponse(conn, 200, "OK", type, "Cache-Control: no-cache\r\n", data, (U32BIT)size);
   }
   else
   {
      SendNotFound(conn);
   }
   free(data);
   fclose(f);
}

static int BeginRequestHandler(struct mg_connection *conn)
{
   const struct mg_request_info *request_info = mg_get_request_info(conn);
   const char *uri = request_info->uri;

   if ((strcmp(request_info->request_method, "GET") != 0) && (strcmp(request_info->request_method, "HEAD") != 0))
   {
      SendResponse(conn, 405, "Method Not Allowed", "text/plain", "Allow: GET, HEAD\r\n", NULL, 0);
   }
   else if (strcmp(uri, "/") == 0)
   {
      SendIndex(conn);
   }
   else if (strcmp(uri, "/time") == 0)
   {
      SendTime(conn);
   }
   else if (strcmp(uri, SLEP_PATH) == 0)
   {
      SendFile(conn, SLEP_LOCATION, TRUE);
   }
   else if (strcmp(uri, "/player") == 0)
   {
      SendResponse(conn, 302, "Found", "text/plain", "Location: " PLAYER_PREFIX "\r\n", NULL, 0);
   }
   else if (strncmp(uri, PLAYER_PREFIX, strlen(PLAYER_PREFIX)) == 0)
   {
      SendPlayerFile(conn, uri);
   }
   else
   {
      SendFile(conn, uri, FALSE);
   }

   /* Non-zero: the request has been answered */
   return 1;
}

/******************************************************************************
 * End of file
 *****************************************************************************/
