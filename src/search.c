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
 * @brief   NippApp: satellite and LNB setup, and the search of each carrier in the sources file.
 *          Runs on the main thread, polling DVBCore for the end of each search as App does.
 * @file    search.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <stdio.h>
#include <string.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbdpc.h"
#include "stbuni.h"
#include "ap_cfg.h"
#include "ap_cntrl.h"
#include "ap_dbacc.h"
#include "ap_si.h"
#include "nipphw.h"

#include "nippapp.h"

/*---constant definitions for this file---------------------------------------*/
#define POLL_MS               200
#define SEARCH_TIMEOUT_MS     (180 * 1000)

/*---local function prototypes for this file----------------------------------*/
static void* GetSatellite(void);
static BOOLEAN SearchSource(void *satellite, const S_SOURCE *source);

/*---global function definitions---------------------------------------------*/

BOOLEAN NIPPAPP_SetupCountry(const char *country)
{
   U32BIT code;

   if ((country == NULL) || (strlen(country) != 3))
   {
      fprintf(stderr, "NippApp: the country must be a 3-letter ISO 3166 code, e.g. gbr\n");
      return FALSE;
   }
   code = ((U32BIT)(country[0] | 0x20) << 16) | ((U32BIT)(country[1] | 0x20) << 8) |
      (U32BIT)(country[2] | 0x20);

   /* The country decides how LCNs are allocated; App asks for it in its first-time setup */
   if (ACFG_GetCountry() != code)
   {
      if (!ACFG_SetCountry(code))
      {
         fprintf(stderr, "NippApp: country '%s' is not known to DVBCore\n", country);
         return FALSE;
      }
      printf("NippApp: country set to %s\n", country);
   }

   return TRUE;
}

BOOLEAN NIPPAPP_Scan(void)
{
   void *satellite;
   U16BIT i;

   satellite = GetSatellite();
   if (satellite == NULL)
   {
      fprintf(stderr, "NippApp: cannot create the satellite and LNB records\n");
      return FALSE;
   }

   if (TUN_GetNumSources() == 0)
   {
      printf("NippApp: no sources to search\n");
      return TRUE;
   }

   for (i = 0; (i < TUN_GetNumSources()) && !NIPPAPP_StopRequested(0); i++)
   {
      SearchSource(satellite, TUN_GetSource(i));
   }

   return !NIPPAPP_StopRequested(0);
}

void NIPPAPP_PrintServices(void)
{
   void **slist;
   U16BIT num;
   U16BIT i;
   U16BIT onid;
   U16BIT tid;
   U16BIT sid;
   U8BIT *name;
   const char *text;

   ADB_GetServiceList(ADB_SERVICE_LIST_ALL, &slist, &num);
   printf("NippApp: %u services in the database\n", num);

   for (i = 0; i < num; i++)
   {
      ADB_GetServiceIds(slist[i], &onid, &tid, &sid);
      name = ADB_GetServiceFullName(slist[i], FALSE);
      text = (name != NULL) ? (const char *)name : "";
      /* DVBCore's UTF-8 strings start with the 0x15 encoding byte */
      if (text[0] == 0x15)
      {
         text++;
      }
      printf("  LCN %4u  %-24s  onid 0x%04x  tsid 0x%04x  sid 0x%04x  type 0x%03x\n",
         ADB_GetServiceLcn(slist[i]), text, onid, tid, sid, (unsigned)ADB_GetServiceType(slist[i]));
      if (name != NULL)
      {
         STB_ReleaseUnicodeString(name);
      }
   }

   if (num != 0)
   {
      ADB_ReleaseServiceList(slist, num);
   }
}

/*---local function definitions-----------------------------------------------*/

/* The satellite all sources are searched on, created on first run with a universal LNB */
static void* GetSatellite(void)
{
   ADB_LNB_SETTINGS settings;
   void *lnb;
   void *satellite;

   satellite = ADB_GetNextSatellite(NULL);
   if (satellite != NULL)
   {
      return satellite;
   }

   lnb = ADB_GetNextLNB(NULL);
   if (lnb == NULL)
   {
      memset(&settings, 0, sizeof(settings));
      settings.type = LNB_TYPE_UNIVERSAL;
      settings.power = LNB_POWER_AUTO;
      settings.diseqc_tone = DISEQC_TONE_OFF;
      settings.c_switch = DISEQC_CSWITCH_OFF;
      settings.name = (U8BIT *)"NippApp";
      lnb = ADB_AddLNB(&settings);
   }
   if (lnb == NULL)
   {
      return NULL;
   }

   printf("NippApp: first run, creating the satellite and LNB records\n");
   return ADB_AddSatellite((U8BIT *)"NippApp", 0, 0, TRUE, lnb);
}

static BOOLEAN SearchSource(void *satellite, const S_SOURCE *source)
{
   S_MANUAL_TUNING_PARAMS params;
   E_SEARCH_SERVICE_TYPE service_type;
   BOOLEAN is_gse;
   BOOLEAN completed;
   U32BIT waited;

   is_gse = (source->kind == SOURCE_KIND_PCAP);

   printf("NippApp: searching %u MHz %c %u ksym/s (%s)\n", source->freq_mhz,
      source->horizontal ? 'H' : 'V', source->symbol_rate, is_gse ? "GSE" : "MPE");

   memset(&params, 0, sizeof(params));
   params.freq = source->freq_mhz;
   params.u.sat.satellite = satellite;
   params.u.sat.polarity = source->horizontal ? POLARITY_HORIZONTAL : POLARITY_VERTICAL;
   params.u.sat.symbol_rate = (U16BIT)source->symbol_rate;
   params.u.sat.fec = FEC_AUTOMATIC;
   params.u.sat.dvb_s2 = TRUE;
   params.u.sat.modulation = MOD_AUTO;
   params.u.sat.is_gse = is_gse;

   service_type = (E_SEARCH_SERVICE_TYPE)(SEARCH_SERVICE_TYPE_ALL | SEARCH_SERVICE_TYPE_DVBNIP |
      (is_gse ? SEARCH_SERVICE_TYPE_DVBNIP_GSE : 0));

   ADB_PrepareDatabaseForSearch(SIGNAL_QPSK, satellite, FALSE, TRUE);
   ASI_SetSearchServiceType(service_type);

   if (!ACTL_StartManualSearch(SIGNAL_QPSK, &params, is_gse ? ACTL_DVBNIP_GSE_SEARCH : ACTL_FREQ_SEARCH))
   {
      fprintf(stderr, "NippApp: the search could not be started\n");
      ADB_FinaliseDatabaseAfterSearch(FALSE, SIGNAL_QPSK, NULL, FALSE, FALSE, TRUE);
      return FALSE;
   }

   completed = FALSE;
   for (waited = 0; waited < SEARCH_TIMEOUT_MS; waited += POLL_MS)
   {
      if (ACTL_IsSearchComplete())
      {
         completed = TRUE;
         break;
      }
      if (NIPPAPP_StopRequested(POLL_MS))
      {
         break;
      }
   }

   if (!completed)
   {
      printf("NippApp: search %s\n", NIPPAPP_StopRequested(0) ? "stopped" : "timed out");
   }

   ACTL_FinishManualSearch();
   /* As App does for a manual search: the satellite is not passed when finalising */
   ADB_FinaliseDatabaseAfterSearch(TRUE, SIGNAL_QPSK, NULL, completed, FALSE, TRUE);

   return completed;
}

/******************************************************************************
 * End of file
 *****************************************************************************/
