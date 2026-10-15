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
 * @brief   NippApp service list registry store: a copy on disk of every DVB-I document received
 *          (service list entry points, service lists, playlists), so they can be served when the
 *          carrier that broadcasts them isn't tuned, including after a restart (TS 103 876
 *          clauses 8.2.8 and C.4.3).
 *
 *          Documents are kept under registry/ in the working directory, at their broadcast path
 *          (http://dvb.gw/ses.com/dvbi/list.xml is registry/ses.com/dvbi/list.xml); a document
 *          identified by URN is kept under registry/urn/, and a bare name (dvb_i_sl_ep.xml, which
 *          some headends send instead of a dvb.gw URL) is taken as a path from the root. Its media
 *          type is kept beside it, in a file with TYPE_SUFFIX added to the name.
 *
 *          Each network's entry points are also kept under registry/slep/, named after their
 *          registry entity, whatever location they were sent with. The gateway's entry points
 *          (SLEP_LOCATION, clause 8.1.7.3) are made from them: one network's as they are; with
 *          several, the first (by name) with every other one's provider offerings added, so a
 *          client is offered every network's service lists.
 *
 *          Channels that no broadcast DVB-I list covers (a broadcast without one, such as TP37) are
 *          offered in a list NippApp makes, GENERATED_LIST_PATH, which the entry points then offer
 *          too, from a provider named after the gateway.
 * @file    registry.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbllist.h"
#include "flute.h"
#include "nip_mgr.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
#define REGISTRY_DIR          "registry"
#define URN_DIR               "urn"
#define SLEP_DIR              REGISTRY_DIR "/slep"
#define MAX_ENTITY_NAME       100
#define MAX_SLEPS             32

/* The list of channels no broadcast list covers, and how it is offered */
#define GENERATED_LIST_PATH   "/dvbi/nippapp_list.xml"
#define GENERATED_LIST_TYPE   "application/vnd.dvb.dvbisl+xml"
#define GENERATED_LIST_ID     "tag:nippapp.dtvkit.org,2026:unlisted"
#define GENERATED_PROVIDER    "NippApp gateway"
#define GENERATED_LIST_NAME   "Services without a broadcast list"

/* the entry points offering it, merged with the broadcast ones like another network's */
static const char generated_slep[] =
   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
   "<ServiceListEntryPoints xmlns=\"urn:dvb:metadata:servicelistdiscovery:2024\""
   " xmlns:dvbi-types=\"urn:dvb:metadata:servicediscovery-types:2023\" xml:lang=\"en\">"
   "<ServiceListRegistryEntity regulatorFlag=\"false\"><Name>" GENERATED_PROVIDER "</Name></ServiceListRegistryEntity>"
   "<ProviderOffering><Provider regulatorFlag=\"false\"><Name>" GENERATED_PROVIDER "</Name></Provider>"
   "<ServiceListOffering regulatorListFlag=\"false\">"
   "<dvbi-types:ServiceListName>" GENERATED_LIST_NAME "</dvbi-types:ServiceListName>"
   "<dvbi-types:ServiceListURI contentType=\"application/xml\">"
   "<dvbi-types:URI>http://dvb.gw" GENERATED_LIST_PATH "</dvbi-types:URI></dvbi-types:ServiceListURI>"
   "<dvbi-types:ServiceListId>" GENERATED_LIST_ID "</dvbi-types:ServiceListId>"
   "</ServiceListOffering></ProviderOffering></ServiceListEntryPoints>";
#define TYPE_SUFFIX           ".nippapp-type"
#define MAX_PATH_LEN          1024
#define MAX_TYPE_LEN          256
#define MAX_DOCUMENT_SIZE     (4 * 1024 * 1024)

/* The location the entry points are kept under: the one the spec gives them (clause 8.2.5.1) */
#define SLEP_LOCATION         "urn:dvb:metadata:nativeip:dvb-i-slep"
#define SLEP_TYPE             "application/xml+dvb-i-slep"

/*---local (static) variable declarations for this file-----------------------*/

/* Media types of the documents kept, compared without any parameters */
static const char *dvbi_types[] = {
   "application/vnd.dvb.dvbisl+xml",
   "application/xml+dvb-i-sl",
   "application/xml+dvb-i-slep",
   "application/xml+dvb-i-pl",
   NULL
};

/*---local function prototypes for this file----------------------------------*/
static void FileStored(void *user_data, S_FLUTE_FILE *file);
static void KeepDocument(const char *location, const char *filetype, U8BIT *data, U32BIT len,
   U32BIT version);
static BOOLEAN RegistryFile(const char *location, char *filename, size_t size);
static BOOLEAN MakeParentDirs(char *filename);
static BOOLEAN SameContent(const char *filename, const U8BIT *data, U32BIT len);
static BOOLEAN WriteFileAtomically(const char *filename, const void *data, U32BIT len);
static U8BIT* ReadWholeFile(const char *filename, U32BIT *len);
static void KeepEntryPoints(const U8BIT *data, U32BIT len);
static BOOLEAN EntityFileName(const U8BIT *data, U32BIT len, char *filename, size_t size);
static BOOLEAN LoadEntryPoints(U8BIT **data, U32BIT *len, char **filetype);
static int CompareNames(const void *a, const void *b);
static BOOLEAN BuildGeneratedList(U8BIT **data, U32BIT *len);

/*---local typedef structs for this file--------------------------------------*/

/* A document being written */
typedef struct
{
   char *text;
   size_t len;
   size_t size;
   BOOLEAN failed;
} S_TEXT;

/* The generated list as it is built: its services, and its LCN table */
typedef struct
{
   S_TEXT services;
   S_TEXT lcns;
   unsigned count;
} S_GENERATED;

/*---global function definitions---------------------------------------------*/

BOOLEAN REG_Init(void)
{
   if (((mkdir(REGISTRY_DIR, 0755) != 0) && (errno != EEXIST)) ||
       ((mkdir(SLEP_DIR, 0755) != 0) && (errno != EEXIST)))
   {
      fprintf(stderr, "NippApp: cannot create %s: %s\n", SLEP_DIR, strerror(errno));
      return FALSE;
   }

   DVBNIP_SetFileStoreCallback(NULL, FileStored);
   return TRUE;
}

void REG_Stop(void)
{
   DVBNIP_SetFileStoreCallback(NULL, NULL);
}

BOOLEAN REG_IsDvbiType(const char *filetype)
{
   const char **type;
   size_t len;

   if (filetype == NULL)
   {
      return FALSE;
   }
   len = strcspn(filetype, "; ");
   for (type = dvbi_types; *type != NULL; type++)
   {
      if ((strlen(*type) == len) && (strncasecmp(filetype, *type, len) == 0))
      {
         return TRUE;
      }
   }
   return FALSE;
}

BOOLEAN REG_Load(const char *location, U8BIT **data, U32BIT *len, char **filetype)
{
   char filename[MAX_PATH_LEN];
   char type_name[MAX_PATH_LEN + sizeof(TYPE_SUFFIX)];
   U8BIT *type;
   U32BIT type_len;

   *data = NULL;
   *filetype = NULL;

   /* the gateway's entry points, made from every network's */
   if ((strcmp(location, SLEP_LOCATION) == 0) && LoadEntryPoints(data, len, filetype))
   {
      return TRUE;
   }

   /* the list of channels no broadcast list covers */
   if (strcmp(location, GENERATED_LIST_PATH) == 0)
   {
      if (!BuildGeneratedList(data, len))
      {
         return FALSE;
      }
      *filetype = strdup(GENERATED_LIST_TYPE);
      if (*filetype == NULL)
      {
         free(*data);
         *data = NULL;
         return FALSE;
      }
      return TRUE;
   }

   if (!RegistryFile(location, filename, sizeof(filename)))
   {
      return FALSE;
   }
   snprintf(type_name, sizeof(type_name), "%s%s", filename, TYPE_SUFFIX);

   type = ReadWholeFile(type_name, &type_len);
   if (type == NULL)
   {
      return FALSE;
   }
   *data = ReadWholeFile(filename, len);
   if (*data == NULL)
   {
      free(type);
      return FALSE;
   }
   *filetype = (char *)type;
   return TRUE;
}

/* Entry point query parameters (TS 103 876 clause 8.1.7.4, TS 103 770): each may be given as
 * Name or Name[], more than once */
enum { Q_COUNTRY, Q_LANGUAGE, Q_GENRE, Q_PROVIDER, Q_DELIVERY, Q_NUM };
static const char *query_names[Q_NUM] = { "TargetCountry", "Language", "Genre", "ProviderName", "Delivery" };
#define MAX_QUERY_VALUES      16
#define MAX_QUERY_VALUE       256

typedef struct
{
   char values[Q_NUM][MAX_QUERY_VALUES][MAX_QUERY_VALUE];
   int counts[Q_NUM];
   BOOLEAN regulator;            /* regulatorListFlag=true */
   BOOLEAN any;                  /* a parameter that filters was given */
} S_QUERY;

/* %XX and '+' decoded, into a buffer of MAX_QUERY_VALUE */
static void DecodeQueryValue(const char *from, size_t len, char *to)
{
   size_t n = 0;
   unsigned hex;

   while ((len > 0) && (n + 1 < MAX_QUERY_VALUE))
   {
      if ((*from == '%') && (len >= 3) && (sscanf(from + 1, "%2x", &hex) == 1))
      {
         to[n++] = (char)hex;
         from += 3;
         len -= 3;
      }
      else
      {
         to[n++] = (*from == '+') ? ' ' : *from;
         from++;
         len--;
      }
   }
   to[n] = '\0';
}

static void ParseQuery(const char *query, S_QUERY *q)
{
   const char *p, *eq, *end;
   char name[64];
   size_t name_len;
   int i;

   memset(q, 0, sizeof(*q));
   for (p = query; (p != NULL) && (*p != '\0'); p = (*end == '&') ? end + 1 : end)
   {
      end = p + strcspn(p, "&");
      eq = memchr(p, '=', (size_t)(end - p));
      if (eq == NULL)
      {
         continue;
      }
      DecodeQueryValue(p, (size_t)(eq - p), name);
      name_len = strlen(name);
      if ((name_len > 2) && (strcmp(name + name_len - 2, "[]") == 0))
      {
         name[name_len - 2] = '\0';
      }
      if (strcasecmp(name, "regulatorListFlag") == 0)
      {
         DecodeQueryValue(eq + 1, (size_t)(end - eq - 1), name);
         if (strcasecmp(name, "true") == 0)
         {
            q->regulator = TRUE;
            q->any = TRUE;
         }
         continue;
      }
      for (i = 0; i < Q_NUM; i++)
      {
         if ((strcasecmp(name, query_names[i]) == 0) && (q->counts[i] < MAX_QUERY_VALUES))
         {
            DecodeQueryValue(eq + 1, (size_t)(end - eq - 1), q->values[i][q->counts[i]]);
            if (q->values[i][q->counts[i]][0] != '\0')
            {
               q->counts[i]++;
               q->any = TRUE;
            }
         }
      }
   }
}

/* Text without the white space around it (Eutelsat lays its names out over several lines) */
static void TrimmedText(xmlNodePtr node, char *text, size_t size)
{
   xmlChar *content = xmlNodeGetContent(node);
   const char *start = (const char *)content;
   size_t len;

   text[0] = '\0';
   if (content == NULL)
   {
      return;
   }
   while (isspace((unsigned char)*start))
   {
      start++;
   }
   len = strlen(start);
   while ((len > 0) && isspace((unsigned char)start[len - 1]))
   {
      len--;
   }
   if (len >= size)
   {
      len = size - 1;
   }
   memcpy(text, start, len);
   text[len] = '\0';
   xmlFree(content);
}

static BOOLEAN IsElement(xmlNodePtr node, const char *name)
{
   return (node->type == XML_ELEMENT_NODE) && (xmlStrcmp(node->name, BAD_CAST name) == 0);
}

/* The delivery type a Delivery child names, as dvbi-client-js and DVB-I queries name them */
static const char* DeliveryName(const xmlChar *element, char *buf, size_t size)
{
   size_t len;

   if (xmlStrcmp(element, BAD_CAST "DASHDelivery") == 0) return "dvb-dash";
   if (xmlStrcmp(element, BAD_CAST "DVBTDelivery") == 0) return "dvb-t";
   if (xmlStrcmp(element, BAD_CAST "DVBSDelivery") == 0) return "dvb-s";
   if (xmlStrcmp(element, BAD_CAST "DVBCDelivery") == 0) return "dvb-c";
   len = strlen((const char *)element);
   if ((len > 8) && (strcmp((const char *)element + len - 8, "Delivery") == 0))
   {
      len -= 8;
   }
   if (len >= size)
   {
      len = size - 1;
   }
   memcpy(buf, element, len);
   buf[len] = '\0';
   for (char *c = buf; *c != '\0'; c++)
   {
      *c = (char)tolower((unsigned char)*c);
   }
   return buf;
}

static BOOLEAN DeliveryMatches(const char *have, const char *want)
{
   /* the client's short names too */
   if (strcasecmp(want, "dash") == 0) want = "dvb-dash";
   else if (strcasecmp(want, "dvbt") == 0) want = "dvb-t";
   else if (strcasecmp(want, "dvbs") == 0) want = "dvb-s";
   else if (strcasecmp(want, "dvbc") == 0) want = "dvb-c";
   return strcasecmp(have, want) == 0;
}

/* One language tag is the other or a more specific form of it: en matches en-GB, and en-GB en */
static BOOLEAN LanguageMatches(const char *have, const char *want)
{
   size_t have_len = strlen(have);
   size_t want_len = strlen(want);
   size_t len = (have_len < want_len) ? have_len : want_len;

   return (strncasecmp(have, want, len) == 0) &&
          ((have[len] == '\0') || (have[len] == '-')) && ((want[len] == '\0') || (want[len] == '-'));
}

static BOOLEAN AnyValue(const S_QUERY *q, int param, const char *have)
{
   int i;

   for (i = 0; i < q->counts[param]; i++)
   {
      if (((param == Q_LANGUAGE) && LanguageMatches(have, q->values[param][i])) ||
          ((param == Q_DELIVERY) && DeliveryMatches(have, q->values[param][i])) ||
          ((param != Q_LANGUAGE) && (param != Q_DELIVERY) && (strcasecmp(have, q->values[param][i]) == 0)))
      {
         return TRUE;
      }
   }
   return FALSE;
}

/* Whether a ServiceListOffering matches every parameter given; one that doesn't declare a property
 * that is asked for doesn't match */
static BOOLEAN OfferingMatches(xmlNodePtr offering, const S_QUERY *q)
{
   xmlNodePtr node, child;
   xmlChar *attr;
   char text[MAX_QUERY_VALUE];
   char buf[64];
   char *token, *save;
   BOOLEAN found[Q_NUM] = { FALSE, FALSE, FALSE, FALSE, FALSE };

   if (q->regulator)
   {
      attr = xmlGetProp(offering, BAD_CAST "regulatorListFlag");
      found[0] = (attr != NULL) && (xmlStrcmp(attr, BAD_CAST "true") == 0);
      xmlFree(attr);
      if (!found[0])
      {
         return FALSE;
      }
      found[0] = FALSE;
   }
   for (node = offering->children; node != NULL; node = node->next)
   {
      if (IsElement(node, "TargetCountry") && (q->counts[Q_COUNTRY] > 0))
      {
         TrimmedText(node, text, sizeof(text));
         for (token = strtok_r(text, ", \t\n", &save); token != NULL; token = strtok_r(NULL, ", \t\n", &save))
         {
            found[Q_COUNTRY] |= AnyValue(q, Q_COUNTRY, token);
         }
      }
      else if (IsElement(node, "Language") && (q->counts[Q_LANGUAGE] > 0))
      {
         TrimmedText(node, text, sizeof(text));
         found[Q_LANGUAGE] |= AnyValue(q, Q_LANGUAGE, text);
      }
      else if (IsElement(node, "Genre") && (q->counts[Q_GENRE] > 0))
      {
         attr = xmlGetProp(node, BAD_CAST "href");
         if (attr != NULL)
         {
            found[Q_GENRE] |= AnyValue(q, Q_GENRE, (const char *)attr);
            xmlFree(attr);
         }
      }
      else if (IsElement(node, "Delivery") && (q->counts[Q_DELIVERY] > 0))
      {
         for (child = node->children; child != NULL; child = child->next)
         {
            if (child->type == XML_ELEMENT_NODE)
            {
               found[Q_DELIVERY] |= AnyValue(q, Q_DELIVERY, DeliveryName(child->name, buf, sizeof(buf)));
            }
         }
      }
   }
   return ((q->counts[Q_COUNTRY] == 0) || found[Q_COUNTRY]) &&
          ((q->counts[Q_LANGUAGE] == 0) || found[Q_LANGUAGE]) &&
          ((q->counts[Q_GENRE] == 0) || found[Q_GENRE]) &&
          ((q->counts[Q_DELIVERY] == 0) || found[Q_DELIVERY]);
}

/* Whether a ProviderOffering's provider is one asked for */
static BOOLEAN ProviderMatches(xmlNodePtr provider_offering, const S_QUERY *q)
{
   xmlNodePtr node, child;
   char text[MAX_QUERY_VALUE];

   if (q->counts[Q_PROVIDER] == 0)
   {
      return TRUE;
   }
   for (node = provider_offering->children; node != NULL; node = node->next)
   {
      if (IsElement(node, "Provider"))
      {
         for (child = node->children; child != NULL; child = child->next)
         {
            if (IsElement(child, "Name"))
            {
               TrimmedText(child, text, sizeof(text));
               if (AnyValue(q, Q_PROVIDER, text))
               {
                  return TRUE;
               }
            }
         }
      }
   }
   return FALSE;
}

BOOLEAN REG_FilterEntryPoints(const U8BIT *data, U32BIT len, const char *query, U8BIT **filtered,
   U32BIT *filtered_len)
{
   S_QUERY *q;
   xmlDocPtr doc;
   xmlNodePtr root, po, next_po, slo, next_slo;
   xmlChar *out;
   int out_len;
   BOOLEAN keep;

   *filtered = NULL;
   *filtered_len = 0;
   q = (S_QUERY *)malloc(sizeof(S_QUERY));
   if (q == NULL)
   {
      return FALSE;
   }
   ParseQuery(query, q);
   if (!q->any)
   {
      free(q);
      return FALSE;
   }
   doc = xmlReadMemory((const char *)data, (int)len, NULL, NULL, XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
   root = (doc == NULL) ? NULL : xmlDocGetRootElement(doc);
   if (root == NULL)
   {
      xmlFreeDoc(doc);
      free(q);
      return FALSE;
   }

   for (po = root->children; po != NULL; po = next_po)
   {
      next_po = po->next;
      if (!IsElement(po, "ProviderOffering"))
      {
         continue;
      }
      keep = FALSE;
      if (ProviderMatches(po, q))
      {
         for (slo = po->children; slo != NULL; slo = next_slo)
         {
            next_slo = slo->next;
            if (IsElement(slo, "ServiceListOffering"))
            {
               if (OfferingMatches(slo, q))
               {
                  keep = TRUE;
               }
               else
               {
                  xmlUnlinkNode(slo);
                  xmlFreeNode(slo);
               }
            }
         }
      }
      if (!keep)
      {
         xmlUnlinkNode(po);
         xmlFreeNode(po);
      }
   }
   free(q);

   out = NULL;
   xmlDocDumpMemoryEnc(doc, &out, &out_len, "UTF-8");
   xmlFreeDoc(doc);
   if (out == NULL)
   {
      return FALSE;
   }
   *filtered = (U8BIT *)malloc(out_len + 1);
   if (*filtered != NULL)
   {
      memcpy(*filtered, out, out_len);
      (*filtered)[out_len] = '\0';
      *filtered_len = (U32BIT)out_len;
   }
   xmlFree(out);
   return *filtered != NULL;
}

void REG_ListNetworks(void *user_data, void (*callback)(void *user_data, const char *name))
{
   DIR *dir;
   struct dirent *entry;
   char name[MAX_ENTITY_NAME + 8];
   size_t len;

   dir = opendir(SLEP_DIR);
   if (dir == NULL)
   {
      return;
   }
   while ((entry = readdir(dir)) != NULL)
   {
      len = strlen(entry->d_name);
      if ((entry->d_name[0] != '.') && (len > 4) && (len < sizeof(name)) && (strcmp(entry->d_name + len - 4, ".xml") == 0))
      {
         memcpy(name, entry->d_name, len - 4);
         name[len - 4] = '\0';
         callback(user_data, name);
      }
   }
   closedir(dir);
}

/*---local function definitions-----------------------------------------------*/

/* Called by the NIP library, with its file store locked, for each file it stores. A DVB-I
 * document is copied to the registry when its content has changed. */
static void FileStored(void *user_data, S_FLUTE_FILE *file)
{
   S_FLUTE_FILE_READ reader;
   U8BIT *data;
   U8BIT *decoded;
   U32BIT len;
   U32BIT decoded_len;
   size_t slep_len = strlen(SLEP_TYPE);

   USE_UNWANTED_PARAM(user_data);

   if ((file->filename == NULL) || !REG_IsDvbiType((const char *)file->filetype) ||
       (file->length > MAX_DOCUMENT_SIZE))
   {
      return;
   }

   data = (U8BIT *)malloc(file->length + 1);
   if (data == NULL)
   {
      return;
   }
   FLUTE_FileOpen(&reader, file);
   len = FLUTE_FileRead(&reader, data, file->length);

   /* a document sent compressed is kept decoded */
   if ((file->encoding != NULL) && (file->encoding[0] != '\0') &&
       (strcasecmp((const char *)file->encoding, "null") != 0))
   {
      if (!ENC_Decode((const char *)file->encoding, data, len, &decoded, &decoded_len))
      {
         free(data);
         return;
      }
      free(data);
      data = decoded;
      len = decoded_len;
   }

   KeepDocument((const char *)file->filename, (const char *)file->filetype, data, len, file->version);

   /* The entry points, whatever location they came with, are kept as their network's */
   if ((strncasecmp((const char *)file->filetype, SLEP_TYPE, slep_len) == 0) &&
       ((file->filetype[slep_len] == '\0') || (file->filetype[slep_len] == ';')))
   {
      KeepEntryPoints(data, len);
   }

   free(data);
}

/* Keeps a network's entry points under SLEP_DIR, named after their registry entity, so that a new
 * version replaces the old one and other networks' are kept */
static void KeepEntryPoints(const U8BIT *data, U32BIT len)
{
   char filename[MAX_PATH_LEN];

   if (EntityFileName(data, len, filename, sizeof(filename)) && !SameContent(filename, data, len) &&
       WriteFileAtomically(filename, data, len))
   {
      printf("NippApp: registry: kept the entry points %s\n", filename + strlen(SLEP_DIR) + 1);
   }
}

/* The file for entry points: the name of their ServiceListRegistryEntity, or else of their first
 * Provider, with anything but letters, digits, '-' and '.' made '_' */
static BOOLEAN EntityFileName(const U8BIT *data, U32BIT len, char *filename, size_t size)
{
   xmlDocPtr doc;
   xmlNodePtr root, node, child;
   xmlChar *name = NULL;
   char safe[MAX_ENTITY_NAME + 1];
   const xmlChar *p;
   size_t n;

   doc = xmlReadMemory((const char *)data, (int)len, NULL, NULL, XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
   if (doc == NULL)
   {
      return FALSE;
   }
   root = xmlDocGetRootElement(doc);
   for (node = (root == NULL) ? NULL : root->children; (node != NULL) && (name == NULL); node = node->next)
   {
      if ((node->type == XML_ELEMENT_NODE) &&
          ((xmlStrcmp(node->name, BAD_CAST "ServiceListRegistryEntity") == 0) ||
           (xmlStrcmp(node->name, BAD_CAST "ProviderOffering") == 0)))
      {
         /* ProviderOffering holds the name in its Provider */
         child = node->children;
         if (xmlStrcmp(node->name, BAD_CAST "ProviderOffering") == 0)
         {
            while ((child != NULL) && ((child->type != XML_ELEMENT_NODE) || (xmlStrcmp(child->name, BAD_CAST "Provider") != 0)))
            {
               child = child->next;
            }
            child = (child == NULL) ? NULL : child->children;
         }
         for (; (child != NULL) && (name == NULL); child = child->next)
         {
            if ((child->type == XML_ELEMENT_NODE) && (xmlStrcmp(child->name, BAD_CAST "Name") == 0))
            {
               name = xmlNodeGetContent(child);
            }
         }
      }
   }
   xmlFreeDoc(doc);

   /* letters and digits kept, every run of anything else one '_' */
   n = 0;
   for (p = name; (p != NULL) && (*p != '\0') && (n < MAX_ENTITY_NAME); p++)
   {
      if (isalnum(*p) || (*p == '-') || (*p == '.'))
      {
         safe[n++] = (char)*p;
      }
      else if ((n > 0) && (safe[n - 1] != '_'))
      {
         safe[n++] = '_';
      }
   }
   while ((n > 0) && (safe[n - 1] == '_'))
   {
      n--;
   }
   safe[n] = '\0';
   xmlFree(name);

   if ((n == 0) || (safe[0] == '.'))
   {
      return FALSE;
   }
   return (snprintf(filename, size, "%s/%s.xml", SLEP_DIR, safe) < (int)size);
}

static int CompareNames(const void *a, const void *b)
{
   return strcmp(*(char * const *)a, *(char * const *)b);
}

/* The gateway's entry points, made from every network's kept under SLEP_DIR: one network's as they
 * are; with several, the first by name with the provider offerings of the others added. The
 * offerings keep their own namespace, which may be an older or newer schema version than the
 * first network's. */
static BOOLEAN LoadEntryPoints(U8BIT **data, U32BIT *len, char **filetype)
{
   DIR *dir;
   struct dirent *entry;
   char *names[MAX_SLEPS];
   char filename[MAX_PATH_LEN];
   int count, i;
   size_t name_len;
   xmlDocPtr base, doc;
   xmlNodePtr root, node, copy;
   xmlChar *merged;
   int merged_len;
   U8BIT *content;
   U32BIT content_len;
   BOOLEAN generated;
   int total;

   count = 0;
   dir = opendir(SLEP_DIR);
   while ((dir != NULL) && ((entry = readdir(dir)) != NULL) && (count < MAX_SLEPS))
   {
      name_len = strlen(entry->d_name);
      if ((entry->d_name[0] != '.') && (name_len > 4) && (strcmp(entry->d_name + name_len - 4, ".xml") == 0))
      {
         names[count] = strdup(entry->d_name);
         if (names[count] != NULL)
         {
            count++;
         }
      }
   }
   if (dir != NULL)
   {
      closedir(dir);
   }
   qsort(names, count, sizeof(names[0]), CompareNames);

   /* the gateway's own offering comes last, when it has channels to offer */
   generated = BuildGeneratedList(&content, &content_len);
   free(content);
   total = count + (generated ? 1 : 0);
   if (total == 0)
   {
      return FALSE;
   }

   base = NULL;
   root = NULL;
   *data = NULL;
   for (i = 0; i < total; i++)
   {
      if (i < count)
      {
         snprintf(filename, sizeof(filename), "%s/%s", SLEP_DIR, names[i]);
         content = ReadWholeFile(filename, &content_len);
      }
      else
      {
         content = (U8BIT *)strdup(generated_slep);
         content_len = (content == NULL) ? 0 : (U32BIT)strlen(generated_slep);
      }
      if (content == NULL)
      {
         continue;
      }
      if (total == 1)
      {
         /* one network: as it was broadcast */
         *data = content;
         *len = content_len;
         break;
      }
      doc = xmlReadMemory((const char *)content, (int)content_len, NULL, NULL,
         XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
      free(content);
      if (doc == NULL)
      {
         continue;
      }
      if (base == NULL)
      {
         base = doc;
         root = xmlDocGetRootElement(base);
         continue;
      }
      for (node = xmlDocGetRootElement(doc)->children; (node != NULL) && (root != NULL); node = node->next)
      {
         if ((node->type == XML_ELEMENT_NODE) && (xmlStrcmp(node->name, BAD_CAST "ProviderOffering") == 0))
         {
            copy = xmlDocCopyNode(node, base, 1);
            if (copy != NULL)
            {
               xmlAddChild(root, copy);
            }
         }
      }
      xmlFreeDoc(doc);
   }

   if (base != NULL)
   {
      merged = NULL;
      xmlDocDumpMemoryEnc(base, &merged, &merged_len, "UTF-8");
      if (merged != NULL)
      {
         *data = (U8BIT *)malloc(merged_len + 1);
         if (*data != NULL)
         {
            memcpy(*data, merged, merged_len);
            (*data)[merged_len] = '\0';
            *len = (U32BIT)merged_len;
         }
         xmlFree(merged);
      }
      xmlFreeDoc(base);
   }

   for (i = 0; i < count; i++)
   {
      free(names[i]);
   }

   if (*data == NULL)
   {
      return FALSE;
   }
   *filetype = strdup(SLEP_TYPE);
   if (*filetype == NULL)
   {
      free(*data);
      *data = NULL;
      return FALSE;
   }
   return TRUE;
}

/* Appends to a document being written */
static void Append(S_TEXT *t, const char *format, ...)
{
   va_list args;
   char *bigger;
   int n;

   if (t->failed)
   {
      return;
   }
   for (;;)
   {
      va_start(args, format);
      n = vsnprintf((t->text == NULL) ? NULL : t->text + t->len, t->size - t->len, format, args);
      va_end(args);
      if (n < 0)
      {
         t->failed = TRUE;
         return;
      }
      if ((t->text != NULL) && ((size_t)n < t->size - t->len))
      {
         t->len += (size_t)n;
         return;
      }
      bigger = (char *)realloc(t->text, t->size + (size_t)n + 4096);
      if (bigger == NULL)
      {
         t->failed = TRUE;
         return;
      }
      t->text = bigger;
      t->size += (size_t)n + 4096;
   }
}

/* Appends text, escaped for XML */
static void AppendEscaped(S_TEXT *t, const char *text)
{
   for (; *text != '\0'; text++)
   {
      switch (*text)
      {
         case '&': Append(t, "&amp;"); break;
         case '<': Append(t, "&lt;"); break;
         case '>': Append(t, "&gt;"); break;
         case '"': Append(t, "&quot;"); break;
         default: Append(t, "%c", *text); break;
      }
   }
}

/* A channel no broadcast list covers: a Service with a DASH and/or an HLS instance, and its LCN */
static void AddGeneratedChannel(void *user_data, const S_DVBNIP_CHANNEL *channel)
{
   S_GENERATED *g = (S_GENERATED *)user_data;
   char id[64];

   snprintf(id, sizeof(id), "tag:nippapp.dtvkit.org,2026:%u.%u.%u",
      (unsigned)channel->onid, (unsigned)channel->tsid, (unsigned)channel->sid);

   Append(&g->services, "<Service version=\"1\"><UniqueIdentifier>%s</UniqueIdentifier>", id);
   if (channel->dash_path != NULL)
   {
      Append(&g->services, "<ServiceInstance priority=\"1\"><SourceType>urn:dvb:metadata:source:dvb-dash</SourceType>"
         "<DASHDeliveryParameters><UriBasedLocation contentType=\"application/dash+xml\"><dvbi-types:URI>http://dvb.gw");
      AppendEscaped(&g->services, (const char *)channel->dash_path);
      Append(&g->services, "</dvbi-types:URI></UriBasedLocation></DASHDeliveryParameters></ServiceInstance>");
   }
   if (channel->hls_path != NULL)
   {
      Append(&g->services, "<ServiceInstance priority=\"%d\">"
         "<IdentifierBasedDeliveryParameters contentType=\"application/vnd.apple.mpegurl\">http://dvb.gw",
         (channel->dash_path != NULL) ? 2 : 1);
      AppendEscaped(&g->services, (const char *)channel->hls_path);
      Append(&g->services, "</IdentifierBasedDeliveryParameters></ServiceInstance>");
   }
   Append(&g->services, "<ServiceName>");
   AppendEscaped(&g->services, (const char *)channel->name);
   Append(&g->services, "</ServiceName><ProviderName>" GENERATED_PROVIDER "</ProviderName>"
      "<ServiceType href=\"urn:dvb:metadata:cs:ServiceTypeCS:2019:linear\"/></Service>");

   if (channel->lcn != 0)
   {
      Append(&g->lcns, "<LCN channelNumber=\"%u\" serviceRef=\"%s\" selectable=\"true\" visible=\"true\"/>",
         (unsigned)channel->lcn, id);
   }
   g->count++;
}

/* The list of channels no broadcast DVB-I list covers; FALSE if there are none. The URIs use
 * dvb.gw, as broadcast lists do, and are changed to the gateway's address when it is served. */
static BOOLEAN BuildGeneratedList(U8BIT **data, U32BIT *len)
{
   S_GENERATED g;
   S_TEXT doc;

   *data = NULL;
   *len = 0;
   memset(&g, 0, sizeof(g));
   DVBNIP_ListUnlistedChannels(&g, AddGeneratedChannel);
   if ((g.count == 0) || g.services.failed || g.lcns.failed)
   {
      free(g.services.text);
      free(g.lcns.text);
      return FALSE;
   }

   memset(&doc, 0, sizeof(doc));
   Append(&doc, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<ServiceList xmlns=\"urn:dvb:metadata:servicediscovery:2024\""
      " xmlns:dvbi-types=\"urn:dvb:metadata:servicediscovery-types:2023\""
      " xml:lang=\"en\" id=\"" GENERATED_LIST_ID "\" version=\"1\">"
      "<Name>" GENERATED_LIST_NAME "</Name><ProviderName>" GENERATED_PROVIDER "</ProviderName>");
   if (g.lcns.len > 0)
   {
      Append(&doc, "<LCNTableList><LCNTable>%s</LCNTable></LCNTableList>", g.lcns.text);
   }
   Append(&doc, "%s</ServiceList>", g.services.text);
   free(g.services.text);
   free(g.lcns.text);
   if (doc.failed)
   {
      free(doc.text);
      return FALSE;
   }
   *data = (U8BIT *)doc.text;
   *len = (U32BIT)doc.len;
   return TRUE;
}

/* Writes a document to the registry if its content has changed */
static void KeepDocument(const char *location, const char *filetype, U8BIT *data, U32BIT len,
   U32BIT version)
{
   char filename[MAX_PATH_LEN];
   char type_name[MAX_PATH_LEN + sizeof(TYPE_SUFFIX)];

   if (!RegistryFile(location, filename, sizeof(filename)) || SameContent(filename, data, len) ||
       !MakeParentDirs(filename))
   {
      return;
   }

   snprintf(type_name, sizeof(type_name), "%s%s", filename, TYPE_SUFFIX);
   if (WriteFileAtomically(type_name, filetype, strlen(filetype)) &&
       WriteFileAtomically(filename, data, len))
   {
      printf("NippApp: registry: kept %s (%s, version %u)\n", location, filetype, (unsigned)version);
   }
}

/* The registry file for a document's location: a URL (scheme and host removed), a broadcast path
 * starting with '/', a URN, or a bare name taken as a path from the root. Returns FALSE for a
 * location that can't be stored safely. */
static BOOLEAN RegistryFile(const char *location, char *filename, size_t size)
{
   const char *path;
   const char *p;
   char *q;
   int len;

   p = strstr(location, "://");
   if (p != NULL)
   {
      path = strchr(p + 3, '/');
      if (path == NULL)
      {
         return FALSE;
      }
      len = snprintf(filename, size, "%s%s", REGISTRY_DIR, path);
   }
   else if (location[0] == '/')
   {
      len = snprintf(filename, size, "%s%s", REGISTRY_DIR, location);
   }
   else if (strncasecmp(location, "urn:", 4) != 0)
   {
      len = snprintf(filename, size, "%s/%s", REGISTRY_DIR, location);
   }
   else
   {
      /* A URN: one file, with the characters that aren't safe in a name replaced */
      len = snprintf(filename, size, "%s/%s/%s", REGISTRY_DIR, URN_DIR, location);
      for (q = filename + strlen(REGISTRY_DIR) + strlen(URN_DIR) + 2; *q != '\0'; q++)
      {
         if (*q == '/' || *q == ':' || *q == '\\')
         {
            *q = '_';
         }
      }
   }

   if ((len <= 0) || ((size_t)len >= size) || (filename[len - 1] == '/'))
   {
      return FALSE;
   }

   /* Keep inside the registry: no "..", no query or fragment */
   if ((strstr(filename, "/../") != NULL) || (strcmp(filename + len - 3, "/..") == 0) ||
       (strpbrk(filename, "?#") != NULL) || (strstr(filename, "//") != NULL))
   {
      return FALSE;
   }
   return TRUE;
}

static BOOLEAN MakeParentDirs(char *filename)
{
   char *slash;

   for (slash = strchr(filename, '/'); slash != NULL; slash = strchr(slash + 1, '/'))
   {
      *slash = '\0';
      if ((mkdir(filename, 0755) != 0) && (errno != EEXIST))
      {
         *slash = '/';
         return FALSE;
      }
      *slash = '/';
   }
   return TRUE;
}

static BOOLEAN SameContent(const char *filename, const U8BIT *data, U32BIT len)
{
   U8BIT *old;
   U32BIT old_len;
   BOOLEAN same;

   old = ReadWholeFile(filename, &old_len);
   same = (old != NULL) && (old_len == len) && (memcmp(old, data, len) == 0);
   free(old);
   return same;
}

/* Writes a temporary file and renames it, so that a reader never sees a partial document */
static BOOLEAN WriteFileAtomically(const char *filename, const void *data, U32BIT len)
{
   char temp_name[MAX_PATH_LEN + 16];
   FILE *f;
   BOOLEAN ok;

   snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);
   f = fopen(temp_name, "wb");
   if (f == NULL)
   {
      return FALSE;
   }
   ok = (fwrite(data, 1, len, f) == len);
   ok = (fclose(f) == 0) && ok;
   if (ok)
   {
      ok = (rename(temp_name, filename) == 0);
   }
   if (!ok)
   {
      unlink(temp_name);
   }
   return ok;
}

/* Reads a file into memory, adding a terminating zero; NULL if it can't be read */
static U8BIT* ReadWholeFile(const char *filename, U32BIT *len)
{
   FILE *f;
   long size;
   U8BIT *data;

   f = fopen(filename, "rb");
   if (f == NULL)
   {
      return NULL;
   }
   data = NULL;
   if ((fseek(f, 0, SEEK_END) == 0) && ((size = ftell(f)) >= 0) && (size <= MAX_DOCUMENT_SIZE) &&
       (fseek(f, 0, SEEK_SET) == 0))
   {
      data = (U8BIT *)malloc(size + 1);
      if ((data != NULL) && (fread(data, 1, size, f) == (size_t)size))
      {
         data[size] = '\0';
         *len = (U32BIT)size;
      }
      else
      {
         free(data);
         data = NULL;
      }
   }
   fclose(f);
   return data;
}

/******************************************************************************
 * End of file
 *****************************************************************************/
