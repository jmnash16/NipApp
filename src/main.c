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
 * @brief   NippApp: headless DVB-NIP gateway built on DVBCore. Startup, main loop and shutdown.
 * @file    main.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "app.h"
#include "ap_dbacc.h"
#include "nipphw.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
#define NIPPAPP_VERSION    "0.1.0"
#define DEFAULT_SOURCES    "sources.conf"
#define DEFAULT_COUNTRY    "gbr"
#define DEFAULT_PORT       "8080"

/* DVBCore's disks are the directories under ./disks; the NIP library keeps the SIF, NIF and
 * DVB-I list there, so that the services can be tuned after a restart */
#define DISKS_DIR          "disks"
#define NIP_DISK_DIR       "disks/nip"

/*---local (static) variable declarations for this file-----------------------*/
/* Posted by the signal handler; sem_post is async-signal-safe */
static sem_t stop_sem;
static volatile sig_atomic_t stop_signal = 0;

/*---local function prototypes for this file----------------------------------*/
static void Usage(const char *name);
static void SignalHandler(int sig);
static BOOLEAN InstallSignalHandlers(void);
static BOOLEAN FindPlayerDir(const char *given, const char *argv0, char *dir, size_t size);

/*---global function definitions---------------------------------------------*/

BOOLEAN NIPPAPP_StopRequested(U32BIT wait_ms)
{
   struct timespec until;

   if ((stop_signal == 0) && (wait_ms != 0))
   {
      clock_gettime(CLOCK_REALTIME, &until);
      until.tv_sec += wait_ms / 1000;
      until.tv_nsec += (long)(wait_ms % 1000) * 1000000;
      if (until.tv_nsec >= 1000000000)
      {
         until.tv_sec++;
         until.tv_nsec -= 1000000000;
      }
      while ((sem_timedwait(&stop_sem, &until) != 0) && (errno == EINTR) && (stop_signal == 0))
      {
      }
   }

   return (stop_signal != 0);
}

int main(int argc, char *argv[])
{
   static const struct option long_opts[] = {
      {"dir", required_argument, NULL, 'd'},
      {"sources", required_argument, NULL, 's'},
      {"scan", no_argument, NULL, 'S'},
      {"country", required_argument, NULL, 'c'},
      {"port", required_argument, NULL, 'p'},
      {"player-dir", required_argument, NULL, 'P'},
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'V'},
      {NULL, 0, NULL, 0}
   };
   const char *work_dir = NULL;
   const char *sources_file = DEFAULT_SOURCES;
   char sources_path[PATH_MAX];
   const char *country = DEFAULT_COUNTRY;
   const char *port = DEFAULT_PORT;
   const char *player_arg = NULL;
   char player_dir[PATH_MAX];
   BOOLEAN force_scan = FALSE;
   int opt;

   /* Line-buffered output, so that progress shows when the output goes to a file */
   setvbuf(stdout, NULL, _IOLBF, 0);

   while ((opt = getopt_long(argc, argv, "d:s:Sc:p:P:hV", long_opts, NULL)) != -1)
   {
      switch (opt)
      {
         case 'd':
            work_dir = optarg;
            break;
         case 's':
            sources_file = optarg;
            break;
         case 'S':
            force_scan = TRUE;
            break;
         case 'c':
            country = optarg;
            break;
         case 'p':
            port = optarg;
            break;
         case 'P':
            player_arg = optarg;
            break;
         case 'V':
            printf("NippApp %s\n", NIPPAPP_VERSION);
            return 0;
         case 'h':
            Usage(argv[0]);
            return 0;
         default:
            Usage(argv[0]);
            return 1;
      }
   }

   /* The sources file is named relative to where NippApp was started, before -d */
   if (realpath(sources_file, sources_path) == NULL)
   {
      fprintf(stderr, "NippApp: sources file '%s': %s\n", sources_file, strerror(errno));
      return 1;
   }

   /* The player's files are next to the executable (build/nippapp -> player/), unless given; found
    * before -d changes the working directory */
   if (!FindPlayerDir(player_arg, argv[0], player_dir, sizeof(player_dir)))
   {
      player_dir[0] = '\0';
   }

   /* DVBCore keeps its database (./dtvkit.sqlite3) and NVM file in the working directory */
   if ((work_dir != NULL) && (chdir(work_dir) != 0))
   {
      fprintf(stderr, "NippApp: cannot use directory '%s': %s\n", work_dir, strerror(errno));
      return 1;
   }

   if (((mkdir(DISKS_DIR, 0755) != 0) && (errno != EEXIST)) ||
       ((mkdir(NIP_DISK_DIR, 0755) != 0) && (errno != EEXIST)))
   {
      fprintf(stderr, "NippApp: cannot create %s: %s\n", NIP_DISK_DIR, strerror(errno));
      return 1;
   }

   if ((sem_init(&stop_sem, 0, 0) != 0) || !InstallSignalHandlers())
   {
      fprintf(stderr, "NippApp: cannot set up signal handling: %s\n", strerror(errno));
      return 1;
   }

   printf("NippApp %s starting\n", NIPPAPP_VERSION);

   /* The tuner reads the sources when DVBCore initialises it */
   if (!TUN_LoadSources(sources_path))
   {
      return 1;
   }
   printf("NippApp: %u sources in %s\n", TUN_GetNumSources(), sources_path);

   if (!APP_InitialiseDVB(NIPPAPP_EventHandler, DVB_INIT_NO_TELETEXT_OR_SUBTITLES))
   {
      fprintf(stderr, "NippApp: DVBCore initialisation failed\n");
      return 1;
   }

   printf("NippApp: DVBCore initialised, %u services in the database\n",
      (unsigned)ADB_GetNumServicesInList(ADB_SERVICE_LIST_ALL, FALSE));

   if (!NIPPAPP_SetupCountry(country) || !REG_Init())
   {
      return 1;
   }

   if (force_scan || (ADB_GetNumServicesInList(ADB_SERVICE_LIST_ALL, FALSE) == 0))
   {
      if (NIPPAPP_Scan())
      {
         NIPPAPP_PrintServices();
      }
   }

   if (!NIPPAPP_StopRequested(0) && !HTTPD_Start(port, (player_dir[0] != '\0') ? player_dir : NULL))
   {
      return 1;
   }

   /* Everything else runs on DVBCore's tasks; the main thread waits for a stop request */
   while (!NIPPAPP_StopRequested(1000))
   {
   }

   HTTPD_Stop();
   REG_Stop();

   printf("NippApp: signal %d, shutting down\n", (int)stop_signal);

   ADB_SaveDatabase();

   printf("NippApp: stopped\n");

   return 0;
}

/*---local function definitions-----------------------------------------------*/

static void Usage(const char *name)
{
   printf("Usage: %s [options]\n"
      "  -d, --dir <path>       working directory for the database and NVM files\n"
      "  -s, --sources <file>   sources file mapping carriers to captures (default %s)\n"
      "  -S, --scan             search every source, even if the database has services\n"
      "  -c, --country <code>   ISO 3166 country, which decides LCN allocation (default %s)\n"
      "  -p, --port <port>      HTTP port (default %s)\n"
      "  -P, --player-dir <dir> web player files (default: player/ next to build/)\n"
      "  -h, --help             show this help\n"
      "  -V, --version          show the version\n", name, DEFAULT_SOURCES, DEFAULT_COUNTRY, DEFAULT_PORT);
}

static void SignalHandler(int sig)
{
   stop_signal = sig;
   sem_post(&stop_sem);
}

static BOOLEAN InstallSignalHandlers(void)
{
   struct sigaction action;

   memset(&action, 0, sizeof(action));
   action.sa_handler = SignalHandler;
   sigemptyset(&action.sa_mask);

   return (sigaction(SIGINT, &action, NULL) == 0) && (sigaction(SIGTERM, &action, NULL) == 0) &&
      (sigaction(SIGQUIT, &action, NULL) == 0);
}

static BOOLEAN FindPlayerDir(const char *given, const char *argv0, char *dir, size_t size)
{
   char exe[PATH_MAX];
   char candidate[PATH_MAX + 16];
   char *slash;
   ssize_t len;

   if (given != NULL)
   {
      if (realpath(given, dir) == NULL)
      {
         fprintf(stderr, "NippApp: player directory '%s': %s\n", given, strerror(errno));
         return FALSE;
      }
      return TRUE;
   }

   len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
   if (len <= 0)
   {
      USE_UNWANTED_PARAM(argv0);
      return FALSE;
   }
   exe[len] = '\0';

   /* <dir>/build/nippapp -> <dir>/player */
   slash = strrchr(exe, '/');
   if (slash != NULL)
   {
      *slash = '\0';
      slash = strrchr(exe, '/');
   }
   if (slash == NULL)
   {
      return FALSE;
   }
   *slash = '\0';
   snprintf(candidate, sizeof(candidate), "%s/player", exe);
   return ((realpath(candidate, dir) != NULL) && (strlen(dir) < size));
}

/******************************************************************************
 * End of file
 *****************************************************************************/
