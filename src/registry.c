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
 *          identified by URN, such as the entry points, is kept under registry/urn/. Its media
 *          type is kept beside it, in a file with TYPE_SUFFIX added to the name.
 * @file    registry.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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
#define TYPE_SUFFIX           ".nippapp-type"
#define MAX_PATH_LEN          1024
#define MAX_TYPE_LEN          256
#define MAX_DOCUMENT_SIZE     (4 * 1024 * 1024)

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
static BOOLEAN RegistryFile(const char *location, char *filename, size_t size);
static BOOLEAN MakeParentDirs(char *filename);
static BOOLEAN SameContent(const char *filename, const U8BIT *data, U32BIT len);
static BOOLEAN WriteFileAtomically(const char *filename, const void *data, U32BIT len);
static U8BIT* ReadWholeFile(const char *filename, U32BIT *len);

/*---global function definitions---------------------------------------------*/

BOOLEAN REG_Init(void)
{
   if ((mkdir(REGISTRY_DIR, 0755) != 0) && (errno != EEXIST))
   {
      fprintf(stderr, "NippApp: cannot create %s: %s\n", REGISTRY_DIR, strerror(errno));
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
   char filename[MAX_PATH_LEN];
   char type_name[MAX_PATH_LEN + sizeof(TYPE_SUFFIX)];
   S_FLUTE_FILE_READ reader;
   U8BIT *data;
   U32BIT len;

   USE_UNWANTED_PARAM(user_data);

   if ((file->filename == NULL) || !REG_IsDvbiType((const char *)file->filetype) ||
       (file->length > MAX_DOCUMENT_SIZE) ||
       !RegistryFile((const char *)file->filename, filename, sizeof(filename)))
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

   if (!SameContent(filename, data, len) && MakeParentDirs(filename))
   {
      snprintf(type_name, sizeof(type_name), "%s%s", filename, TYPE_SUFFIX);
      if (WriteFileAtomically(type_name, file->filetype, strlen((const char *)file->filetype)) &&
          WriteFileAtomically(filename, data, len))
      {
         printf("NippApp: registry: kept %s (%s, version %u)\n", (const char *)file->filename,
            (const char *)file->filetype, (unsigned)file->version);
      }
   }

   free(data);
}

/* The registry file for a document's location: a URL (scheme and host removed), a broadcast path
 * starting with '/', or a URN. Returns FALSE for a location that can't be stored safely. */
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
