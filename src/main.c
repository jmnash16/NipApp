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
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "app.h"
#include "ap_dbacc.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
#define NIPPAPP_VERSION    "0.1.0"

/*---local (static) variable declarations for this file-----------------------*/
/* Posted by the signal handler; sem_post is async-signal-safe */
static sem_t stop_sem;
static volatile sig_atomic_t stop_signal = 0;

/*---local function prototypes for this file----------------------------------*/
static void Usage(const char *name);
static void SignalHandler(int sig);
static BOOLEAN InstallSignalHandlers(void);

/*---global function definitions---------------------------------------------*/

int main(int argc, char *argv[])
{
   static const struct option long_opts[] = {
      {"dir", required_argument, NULL, 'd'},
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'V'},
      {NULL, 0, NULL, 0}
   };
   const char *work_dir = NULL;
   int opt;

   while ((opt = getopt_long(argc, argv, "d:hV", long_opts, NULL)) != -1)
   {
      switch (opt)
      {
         case 'd':
            work_dir = optarg;
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

   /* DVBCore keeps its database (./dtvkit.sqlite3) and NVM file in the working directory */
   if ((work_dir != NULL) && (chdir(work_dir) != 0))
   {
      fprintf(stderr, "NippApp: cannot use directory '%s': %s\n", work_dir, strerror(errno));
      return 1;
   }

   if ((sem_init(&stop_sem, 0, 0) != 0) || !InstallSignalHandlers())
   {
      fprintf(stderr, "NippApp: cannot set up signal handling: %s\n", strerror(errno));
      return 1;
   }

   printf("NippApp %s starting\n", NIPPAPP_VERSION);

   if (!APP_InitialiseDVB(NIPPAPP_EventHandler, DVB_INIT_NO_TELETEXT_OR_SUBTITLES))
   {
      fprintf(stderr, "NippApp: DVBCore initialisation failed\n");
      return 1;
   }

   printf("NippApp: DVBCore initialised, %u services in the database\n",
      (unsigned)ADB_GetNumServicesInList(ADB_SERVICE_LIST_ALL, FALSE));

   /* Everything runs on DVBCore's tasks; the main thread waits for a stop request */
   while ((sem_wait(&stop_sem) != 0) && (errno == EINTR))
   {
   }

   printf("NippApp: signal %d, shutting down\n", (int)stop_signal);

   ADB_SaveDatabase();

   printf("NippApp: stopped\n");

   return 0;
}

/*---local function definitions-----------------------------------------------*/

static void Usage(const char *name)
{
   printf("Usage: %s [options]\n"
      "  -d, --dir <path>   working directory for the database and NVM files\n"
      "  -h, --help         show this help\n"
      "  -V, --version      show the version\n", name);
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

/******************************************************************************
 * End of file
 *****************************************************************************/
