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

   dir = opendir(SLEP_DIR);
   if (dir == NULL)
   {
      return FALSE;
   }
   count = 0;
   while (((entry = readdir(dir)) != NULL) && (count < MAX_SLEPS))
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
   closedir(dir);
   if (count == 0)
   {
      return FALSE;
   }
   qsort(names, count, sizeof(names[0]), CompareNames);

   base = NULL;
   root = NULL;
   *data = NULL;
   for (i = 0; i < count; i++)
   {
      snprintf(filename, sizeof(filename), "%s/%s", SLEP_DIR, names[i]);
      content = ReadWholeFile(filename, &content_len);
      if (content == NULL)
      {
         continue;
      }
      if (count == 1)
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
