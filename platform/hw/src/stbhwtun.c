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
 * @brief   File tuner for NippApp: satellite (DVB-S2) tuner paths that "receive" captures named
 *          in a sources file.
 *
 *          A sources file line maps a carrier (RF frequency, polarisation, symbol rate) to a
 *          capture. DVBCore tunes a universal LNB, so the RF frequency is the IF plus the
 *          local oscillator the 22 kHz tone selects (9750 or 10600 MHz), and the polarisation
 *          is given by the LNB voltage (18 V horizontal, 13/14 V vertical). A tune that
 *          matches a TS source locks, and a feeder thread passes the file's packets to the
 *          demux in real time: paced by PCR, or by TDT and the average bitrate when the
 *          capture has no PCR. Anything else reports no lock, as an empty transponder would.
 *
 *          PCAP sources (fed as GSE) are milestone 3; for now they do not lock.
 * @file    stbhwtun.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwdef.h"
#include "stbhwos.h"
#include "stbhwtun.h"
#include "nipphw.h"

/*---constant definitions for this file---------------------------------------*/
#define MAX_SOURCES           32

#define LO_LOW_BAND_KHZ       9750000
#define LO_HIGH_BAND_KHZ      10600000
#define FREQ_TOLERANCE_KHZ    4000

#define READ_PACKETS          1024     /* packets read from the file at a time */
#define FEED_PACKETS          64       /* packets passed to the demux between pacing checks */

#define TDT_PID               0x14
#define TDT_TABLE_ID          0x70
#define TOT_TABLE_ID          0x73
#define MJD_UNIX_EPOCH        40587    /* MJD of 1970-01-01 */
#define PCR_HZ                27000000.0
#define MAX_BEHIND_SECS       10.0     /* further behind than this and pacing starts again */
#define MAX_PCR_JUMP_SECS     10.0     /* a PCR jump larger than this is a discontinuity */

#define NO_SOURCE             0xffff

/*---local typedef structs for this file--------------------------------------*/
typedef struct
{
   volatile BOOLEAN *stop;    /* the feeder's stop flag, so that pacing waits can be cut short */
   BOOLEAN anchored;
   double anchor_stream;      /* stream time at the anchor, seconds */
   double anchor_local;       /* local monotonic time at the anchor, seconds */

   /* PCR pacing */
   U16BIT pcr_pid;            /* first PID seen carrying PCR, 0x1fff if none yet */
   double last_pcr;

   /* TDT pacing, used while no PCR has been seen */
   U64BIT bytes;              /* bytes fed since the file was opened (or looped) */
   BOOLEAN have_tdt;
   double first_tdt;
   U64BIT first_tdt_bytes;
   double last_tdt;
   U64BIT last_tdt_bytes;
   double byte_rate;          /* bytes per second of broadcast time */
} S_PACING;

typedef struct
{
   pthread_mutex_t mutex;     /* tuning state; held while starting or stopping the feeder */

   E_STB_TUNE_LNB_VOLTAGE voltage;
   BOOLEAN tone_22k;
   U32BIT if_khz;
   U32BIT srate;
   BOOLEAN use_gse;
   GSE_PacketCallback gse_callback;
   void *gse_user_data;

   BOOLEAN locked;
   U16BIT source;             /* index of the source being fed, NO_SOURCE if none */

   pthread_t thread;
   BOOLEAN thread_running;
   volatile BOOLEAN stop;
   FILE *file;
   long sync_offset;
   S_PACING pacing;
   U8BIT path;
} S_TUNER;

/*---local (static) variable declarations for this file-----------------------*/
static S_SOURCE sources[MAX_SOURCES];
static U16BIT num_sources = 0;

static U8BIT num_paths = 0;
static S_TUNER tuners[NUM_TUNER_PATHS];

/*---local function prototypes for this file----------------------------------*/
static BOOLEAN ParseSourceLine(char *line, const char *base_dir, S_SOURCE *source);
static U16BIT FindSource(S_TUNER *tuner);
static BOOLEAN OpenTS(S_TUNER *tuner, const S_SOURCE *source);
static void StopFeeder(S_TUNER *tuner);
static void* FeederThread(void *param);
static double MonotonicNow(void);
static void SleepUntil(S_PACING *pacing, double target);
static void ResetPacing(S_PACING *pacing);
static void PaceToStreamTime(S_PACING *pacing, double stream_time);
static void InspectPacket(S_PACING *pacing, const U8BIT *packet);
static void PaceByTdt(S_PACING *pacing);

/*---global function definitions---------------------------------------------*/

BOOLEAN TUN_LoadSources(const char *filename)
{
   FILE *f;
   char line[1024];
   char base_dir[PATH_MAX];
   char *slash;
   U32BIT line_num;

   FUNCTION_START(TUN_LoadSources);

   num_sources = 0;

   if (realpath(filename, base_dir) == NULL)
   {
      fprintf(stderr, "NippApp: sources file '%s': %s\n", filename, strerror(errno));
      return FALSE;
   }
   slash = strrchr(base_dir, '/');
   if (slash != NULL)
   {
      *slash = '\0';
   }

   f = fopen(filename, "r");
   if (f == NULL)
   {
      fprintf(stderr, "NippApp: sources file '%s': %s\n", filename, strerror(errno));
      return FALSE;
   }

   line_num = 0;
   while (fgets(line, sizeof(line), f) != NULL)
   {
      line_num++;
      if (num_sources == MAX_SOURCES)
      {
         fprintf(stderr, "NippApp: %s: more than %u sources, the rest are ignored\n", filename,
            MAX_SOURCES);
         break;
      }
      if (!ParseSourceLine(line, base_dir, &sources[num_sources]))
      {
         continue;
      }
      if (sources[num_sources].freq_mhz == 0)
      {
         fprintf(stderr, "NippApp: %s line %u not understood, ignored\n", filename, line_num);
         continue;
      }
      num_sources++;
   }
   fclose(f);

   FUNCTION_FINISH(TUN_LoadSources);

   return TRUE;
}

U16BIT TUN_GetNumSources(void)
{
   return num_sources;
}

const S_SOURCE* TUN_GetSource(U16BIT index)
{
   return (index < num_sources) ? &sources[index] : NULL;
}

void STB_TuneInitialise(U8BIT paths)
{
   U8BIT i;

   FUNCTION_START(STB_TuneInitialise);

   num_paths = (paths <= NUM_TUNER_PATHS) ? paths : NUM_TUNER_PATHS;
   memset(tuners, 0, sizeof(tuners));
   for (i = 0; i < num_paths; i++)
   {
      pthread_mutex_init(&tuners[i].mutex, NULL);
      tuners[i].source = NO_SOURCE;
      tuners[i].path = i;
      tuners[i].pacing.stop = &tuners[i].stop;
   }

   FUNCTION_FINISH(STB_TuneInitialise);
}

void STB_TuneStartTuner(U8BIT path, U32BIT freq, U32BIT srate, E_STB_TUNE_FEC fec, S8BIT freq_off,
   E_STB_TUNE_TMODE tmode, E_STB_TUNE_TBWIDTH tbwidth, E_STB_TUNE_CMODE cmode,
   E_STB_TUNE_ANALOG_VIDEO_TYPE anlg_vtype)
{
   S_TUNER *tuner;
   U16BIT index;
   const S_SOURCE *source;
   BOOLEAN locked;

   FUNCTION_START(STB_TuneStartTuner);

   USE_UNWANTED_PARAM(fec);
   USE_UNWANTED_PARAM(freq_off);
   USE_UNWANTED_PARAM(tmode);
   USE_UNWANTED_PARAM(tbwidth);
   USE_UNWANTED_PARAM(cmode);
   USE_UNWANTED_PARAM(anlg_vtype);

   if (path >= num_paths)
   {
      return;
   }
   tuner = &tuners[path];

   pthread_mutex_lock(&tuner->mutex);

   tuner->if_khz = freq;
   tuner->srate = srate;
   index = FindSource(tuner);

   if ((index != NO_SOURCE) && (index == tuner->source) && tuner->thread_running)
   {
      /* Already receiving this carrier */
      locked = TRUE;
   }
   else
   {
      StopFeeder(tuner);
      locked = FALSE;

      if (index != NO_SOURCE)
      {
         source = &sources[index];
         if (source->kind != SOURCE_KIND_TS)
         {
            printf("NippApp: tuner %u: %u MHz %c is a PCAP source; PCAP input is not supported yet\n",
               path, source->freq_mhz, source->horizontal ? 'H' : 'V');
         }
         else if (OpenTS(tuner, source))
         {
            tuner->source = index;
            tuner->stop = FALSE;
            ResetPacing(&tuner->pacing);
            if (pthread_create(&tuner->thread, NULL, FeederThread, tuner) == 0)
            {
               tuner->thread_running = TRUE;
               locked = TRUE;
               printf("NippApp: tuner %u: %u MHz %c %u ksym/s -> %s\n", path, source->freq_mhz,
                  source->horizontal ? 'H' : 'V', source->symbol_rate, source->file);
            }
            else
            {
               fclose(tuner->file);
               tuner->file = NULL;
               tuner->source = NO_SOURCE;
            }
         }
      }
   }

   tuner->locked = locked;

   pthread_mutex_unlock(&tuner->mutex);

   STB_OSSendEvent(FALSE, HW_EV_CLASS_TUNER, locked ? HW_EV_TYPE_LOCKED : HW_EV_TYPE_NOTLOCKED,
      &path, sizeof(U8BIT));

   FUNCTION_FINISH(STB_TuneStartTuner);
}

void STB_TuneStopTuner(U8BIT path)
{
   FUNCTION_START(STB_TuneStopTuner);

   if (path < num_paths)
   {
      pthread_mutex_lock(&tuners[path].mutex);
      StopFeeder(&tuners[path]);
      tuners[path].locked = FALSE;
      pthread_mutex_unlock(&tuners[path].mutex);
   }

   FUNCTION_FINISH(STB_TuneStopTuner);
}

U16BIT STB_TuneGetSignalType(U8BIT path)
{
   return (path < num_paths) ? TUNE_SIGNAL_QPSK : TUNE_SIGNAL_NONE;
}

void STB_TuneSetSignalType(U8BIT path, E_STB_TUNE_SIGNAL_TYPE type)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(type);
}

E_STB_TUNE_SYSTEM_TYPE STB_TuneGetSystemType(U8BIT path)
{
   return (path < num_paths) ? TUNE_SYSTEM_TYPE_DVBS2 : TUNE_SYSTEM_TYPE_UNKNOWN;
}

void STB_TuneSetSystemType(U8BIT path, E_STB_TUNE_SYSTEM_TYPE type)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(type);
}

void STB_TuneSetLNBVoltage(U8BIT path, E_STB_TUNE_LNB_VOLTAGE voltage)
{
   if (path < num_paths)
   {
      tuners[path].voltage = voltage;
   }
}

void STB_TuneSet22kState(U8BIT path, BOOLEAN state)
{
   if (path < num_paths)
   {
      tuners[path].tone_22k = state;
   }
}

void STB_TuneSetGSEMode(U8BIT path, BOOLEAN use_gse)
{
   if (path < num_paths)
   {
      tuners[path].use_gse = use_gse;
   }
}

void STB_TuneSetGSEDataCallback(U8BIT path, void *user_data, GSE_PacketCallback callback)
{
   if (path < num_paths)
   {
      tuners[path].gse_callback = callback;
      tuners[path].gse_user_data = user_data;
   }
}

U32BIT STB_TuneGetActualSymbolRate(U8BIT path)
{
   return (path < num_paths) ? tuners[path].srate : 0;
}

U8BIT STB_TuneGetSignalStrength(U8BIT path)
{
   return ((path < num_paths) && tuners[path].locked) ? 100 : 0;
}

U8BIT STB_TuneGetDataIntegrity(U8BIT path)
{
   return ((path < num_paths) && tuners[path].locked) ? 100 : 0;
}

U8BIT STB_TuneSatGetCarrierStrength(U8BIT path, U32BIT freq)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(freq);
   return 0;
}

U32BIT STB_TuneGetMinTunerFreqKHz(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return 950000;
}

U32BIT STB_TuneGetMaxTunerFreqKHz(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return 2150000;
}

/*---Satellite settings: accepted and ignored; the file tuner does not need them----------------------*/

void STB_TuneSet12VSwitch(U8BIT path, BOOLEAN state)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(state);
}

void STB_TuneSendDISEQCMessage(U8BIT path, U8BIT *data, U8BIT size)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(size);
}

/* DVBCore passes the LO in kHz through a 16-bit parameter, so it cannot be used; the LO is
 * taken from the 22 kHz tone instead */
void STB_TuneSetLOFrequency(U8BIT tuner, U16BIT lo_freq)
{
   USE_UNWANTED_PARAM(tuner);
   USE_UNWANTED_PARAM(lo_freq);
}

void STB_TuneSetModulation(U8BIT path, E_STB_TUNE_MODULATION modulation)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(modulation);
}

void STB_TuneSetRollOff(U8BIT path, E_STB_TUNE_ROLL_OFF roll_off)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(roll_off);
}

void STB_TuneSetInputStreamIndicator(U8BIT path, U8BIT input_stream_identifier)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(input_stream_identifier);
}

void STB_TuneSetScramblingSequenceIndex(U8BIT path, U32BIT scrambling_sequence_index)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(scrambling_sequence_index);
}

void STB_TuneSetTimeSlice(U8BIT path, U8BIT timeslice_number)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(timeslice_number);
}

void STB_TuneActiveAerialPower(U8BIT path, BOOLEAN enabled)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(enabled);
}

void STB_TuneAutoRelock(U8BIT path, BOOLEAN state)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(state);
}

/*---Motorised dish: not supported--------------------------------------------------------------------*/

void STB_TuneAtPulsePosition(U8BIT path, U16BIT position)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(position);
}

void STB_TuneChangePulsePosition(U8BIT path, U16BIT count)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(count);
}

U16BIT STB_TuneGetPulsePosition(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return 0;
}

void STB_TuneSetPulseLimitEast(U8BIT path, U16BIT count)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(count);
}

void STB_TuneSetPulseLimitWest(U8BIT path, U16BIT count)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(count);
}

void STB_TuneChangeSkewPosition(U8BIT path, U16BIT count)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(count);
}

/*---Terrestrial and cable: not supported-------------------------------------------------------------*/

void STB_TuneSetPLP(U8BIT path, U8BIT plp)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(plp);
}

E_STB_TUNE_CMODE STB_TuneGetActualCableMode(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_MODE_QAM_UNDEFINED;
}

E_STB_TUNE_TBWIDTH STB_TuneGetActualTerrBwidth(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_TBWIDTH_8MHZ;
}

E_STB_TUNE_TCONST STB_TuneGetActualTerrConstellation(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_TCONST_UNDEFINED;
}

S8BIT STB_TuneGetActualTerrFreqOffset(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return 0;
}

E_STB_TUNE_TGUARDINT STB_TuneGetActualTerrGuardInt(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_TGUARDINT_UNDEFINED;
}

E_STB_TUNE_THIERARCHY STB_TuneGetActualTerrHierarchy(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_THIERARCHY_UNDEFINED;
}

E_STB_TUNE_TCODERATE STB_TuneGetActualTerrHpCodeRate(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_TCODERATE_UNDEFINED;
}

E_STB_TUNE_TCODERATE STB_TuneGetActualTerrLpCodeRate(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_TCODERATE_UNDEFINED;
}

E_STB_TUNE_TMODE STB_TuneGetActualTerrMode(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return TUNE_MODE_COFDM_UNDEFINED;
}

/*---local function definitions-----------------------------------------------*/

/* Parses "freq(MHz) pol symrate kind file [loop|stop]". Returns FALSE for blank and comment
 * lines; a line in error is returned with freq_mhz set to 0. */
static BOOLEAN ParseSourceLine(char *line, const char *base_dir, S_SOURCE *source)
{
   char *fields[6];
   char *p;
   U8BIT n;

   p = strchr(line, '#');
   if (p != NULL)
   {
      *p = '\0';
   }

   n = 0;
   p = strtok(line, " \t\r\n");
   while ((p != NULL) && (n < 6))
   {
      fields[n++] = p;
      p = strtok(NULL, " \t\r\n");
   }
   if (n == 0)
   {
      return FALSE;
   }

   memset(source, 0, sizeof(S_SOURCE));
   if ((n < 5) || (atoi(fields[0]) <= 0) || (atoi(fields[2]) <= 0))
   {
      return TRUE;
   }

   switch (toupper((unsigned char)fields[1][0]))
   {
      case 'H':
         source->horizontal = TRUE;
         break;
      case 'V':
         source->horizontal = FALSE;
         break;
      default:
         return TRUE;
   }

   if (strcmp(fields[3], "ts") == 0)
   {
      source->kind = SOURCE_KIND_TS;
   }
   else if (strcmp(fields[3], "pcap") == 0)
   {
      source->kind = SOURCE_KIND_PCAP;
   }
   else
   {
      return TRUE;
   }

   if (n == 6)
   {
      if (strcmp(fields[5], "loop") == 0)
      {
         source->loop = TRUE;
      }
      else if (strcmp(fields[5], "stop") != 0)
      {
         return TRUE;
      }
   }

   if (fields[4][0] == '/')
   {
      snprintf(source->file, sizeof(source->file), "%s", fields[4]);
   }
   else
   {
      snprintf(source->file, sizeof(source->file), "%s/%s", base_dir, fields[4]);
   }

   source->freq_mhz = (U32BIT)atoi(fields[0]);
   source->symbol_rate = (U32BIT)atoi(fields[2]);

   return TRUE;
}

/* The source for the carrier the tuner is set to, or NO_SOURCE */
static U16BIT FindSource(S_TUNER *tuner)
{
   U32BIT rf_khz;
   BOOLEAN horizontal;
   U32BIT source_khz;
   U32BIT source_srate;
   U32BIT diff;
   U16BIT i;

   rf_khz = tuner->if_khz + (tuner->tone_22k ? LO_HIGH_BAND_KHZ : LO_LOW_BAND_KHZ);
   horizontal = (tuner->voltage == LNB_VOLTAGE_18V);

   for (i = 0; i < num_sources; i++)
   {
      source_khz = sources[i].freq_mhz * 1000;
      diff = (source_khz > rf_khz) ? (source_khz - rf_khz) : (rf_khz - source_khz);
      if ((diff > FREQ_TOLERANCE_KHZ) || (sources[i].horizontal != horizontal))
      {
         continue;
      }
      /* Symbol rate within 2% */
      source_srate = sources[i].symbol_rate * 1000;
      diff = (source_srate > tuner->srate) ? (source_srate - tuner->srate) : (tuner->srate - source_srate);
      if (diff <= source_srate / 50)
      {
         return i;
      }
   }

   return NO_SOURCE;
}

/* Opens a TS file and finds the first packet boundary */
static BOOLEAN OpenTS(S_TUNER *tuner, const S_SOURCE *source)
{
   U8BIT buf[TS_PACKET_SIZE * 5];
   size_t num;
   long offset;

   tuner->file = fopen(source->file, "rb");
   if (tuner->file == NULL)
   {
      fprintf(stderr, "NippApp: tuner %u: cannot open %s: %s\n", tuner->path, source->file,
         strerror(errno));
      return FALSE;
   }

   num = fread(buf, 1, sizeof(buf), tuner->file);
   for (offset = 0; (offset + (4 * TS_PACKET_SIZE)) < (long)num; offset++)
   {
      if ((buf[offset] == 0x47) && (buf[offset + TS_PACKET_SIZE] == 0x47) &&
          (buf[offset + (2 * TS_PACKET_SIZE)] == 0x47) && (buf[offset + (3 * TS_PACKET_SIZE)] == 0x47))
      {
         tuner->sync_offset = offset;
         fseek(tuner->file, offset, SEEK_SET);
         return TRUE;
      }
   }

   fprintf(stderr, "NippApp: tuner %u: %s is not a 188-byte TS\n", tuner->path, source->file);
   fclose(tuner->file);
   tuner->file = NULL;
   return FALSE;
}

/* Called with the tuner's mutex held */
static void StopFeeder(S_TUNER *tuner)
{
   if (tuner->thread_running)
   {
      tuner->stop = TRUE;
      pthread_join(tuner->thread, NULL);
      tuner->thread_running = FALSE;
   }
   if (tuner->file != NULL)
   {
      fclose(tuner->file);
      tuner->file = NULL;
   }
   tuner->source = NO_SOURCE;
}

static void* FeederThread(void *param)
{
   S_TUNER *tuner = (S_TUNER *)param;
   const S_SOURCE *source = &sources[tuner->source];
   U8BIT *buffer;
   size_t num;
   U32BIT count;
   U32BIT i;
   U32BIT start;
   BOOLEAN at_end;

   buffer = (U8BIT *)malloc(READ_PACKETS * TS_PACKET_SIZE);
   if (buffer == NULL)
   {
      return NULL;
   }

   at_end = FALSE;
   while (!tuner->stop)
   {
      if (at_end)
      {
         /* Hold at the end of a "stop" source until the tuner is stopped or retuned */
         STB_OSTaskDelay(100);
         continue;
      }

      num = fread(buffer, 1, READ_PACKETS * TS_PACKET_SIZE, tuner->file);
      count = (U32BIT)(num / TS_PACKET_SIZE);
      if (count == 0)
      {
         if (source->loop)
         {
            fseek(tuner->file, tuner->sync_offset, SEEK_SET);
            ResetPacing(&tuner->pacing);
         }
         else
         {
            printf("NippApp: tuner %u: end of %s\n", tuner->path, source->file);
            at_end = TRUE;
         }
         continue;
      }

      if (buffer[0] != 0x47)
      {
         /* Lost sync (a truncated packet): find the next boundary */
         for (i = 1; (i < num - TS_PACKET_SIZE) && !((buffer[i] == 0x47) && (buffer[i + TS_PACKET_SIZE] == 0x47)); i++)
         {
         }
         fseek(tuner->file, (long)i - (long)num, SEEK_CUR);
         continue;
      }

      /* Pass the packets on in small batches, pacing between them */
      start = 0;
      for (i = 0; (i < count) && !tuner->stop; i++)
      {
         InspectPacket(&tuner->pacing, buffer + (i * TS_PACKET_SIZE));
         tuner->pacing.bytes += TS_PACKET_SIZE;

         if (((i - start + 1) == FEED_PACKETS) || (i == count - 1))
         {
            DMX_FeedPackets(tuner->path, buffer + (start * TS_PACKET_SIZE), i - start + 1);
            start = i + 1;
            if (tuner->pacing.pcr_pid == 0x1fff)
            {
               PaceByTdt(&tuner->pacing);
            }
         }
      }
   }

   free(buffer);
   return NULL;
}

static double MonotonicNow(void)
{
   struct timespec ts;

   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec + (ts.tv_nsec / 1e9);
}

/* Sleeps in steps of at most 100 ms, so that a stopped tuner does not wait for the pacing */
static void SleepUntil(S_PACING *pacing, double target)
{
   struct timespec ts;
   double wait;

   while (!*pacing->stop && ((wait = target - MonotonicNow()) > 0))
   {
      if (wait > 0.1)
      {
         wait = 0.1;
      }
      ts.tv_sec = 0;
      ts.tv_nsec = (long)(wait * 1e9);
      nanosleep(&ts, NULL);
   }
}

static void ResetPacing(S_PACING *pacing)
{
   volatile BOOLEAN *stop = pacing->stop;

   memset(pacing, 0, sizeof(S_PACING));
   pacing->stop = stop;
   pacing->pcr_pid = 0x1fff;
}

/* Waits until the local clock reaches the given stream time, starting again when the stream
 * jumps or the input falls too far behind */
static void PaceToStreamTime(S_PACING *pacing, double stream_time)
{
   double now = MonotonicNow();
   double target;

   if (!pacing->anchored)
   {
      pacing->anchor_stream = stream_time;
      pacing->anchor_local = now;
      pacing->anchored = TRUE;
      return;
   }

   target = pacing->anchor_local + (stream_time - pacing->anchor_stream);
   if (target - now > MAX_PCR_JUMP_SECS)
   {
      /* Discontinuity forwards */
      pacing->anchor_stream = stream_time;
      pacing->anchor_local = now;
   }
   else if (now - target > MAX_BEHIND_SECS)
   {
      /* Discontinuity backwards, or the input cannot keep up */
      pacing->anchor_stream = stream_time;
      pacing->anchor_local = now;
   }
   else
   {
      SleepUntil(pacing, target);
   }
}

/* Paces on PCR, and records TDT/TOT times for captures with no PCR */
static void InspectPacket(S_PACING *pacing, const U8BIT *packet)
{
   U16BIT pid;
   U64BIT base;
   U32BIT ext;
   double pcr;
   U16BIT offset;
   const U8BIT *section;
   U16BIT mjd;
   double utc;

   pid = ((packet[1] & 0x1f) << 8) | packet[2];

   /* PCR: adaptation field present, long enough, with the PCR flag */
   if (((packet[3] & 0x20) != 0) && (packet[4] >= 7) && ((packet[5] & 0x10) != 0))
   {
      if (pacing->pcr_pid == 0x1fff)
      {
         pacing->pcr_pid = pid;
      }
      if (pid == pacing->pcr_pid)
      {
         base = ((U64BIT)packet[6] << 25) | ((U64BIT)packet[7] << 17) | ((U64BIT)packet[8] << 9) |
            ((U64BIT)packet[9] << 1) | (packet[10] >> 7);
         ext = ((packet[10] & 0x01) << 8) | packet[11];
         pcr = ((base * 300) + ext) / PCR_HZ;
         if (pacing->anchored && (pcr < pacing->last_pcr - 1.0))
         {
            /* Wrapped or restarted */
            pacing->anchored = FALSE;
         }
         pacing->last_pcr = pcr;
         PaceToStreamTime(pacing, pcr);
      }
      return;
   }

   /* TDT/TOT: a section starting in this packet, with the UTC time at bytes 3-7 */
   if ((pid != TDT_PID) || (pacing->pcr_pid != 0x1fff) || ((packet[1] & 0x40) == 0) ||
       ((packet[3] & 0x10) == 0))
   {
      return;
   }
   offset = 4;
   if ((packet[3] & 0x20) != 0)
   {
      offset += packet[4] + 1;
   }
   if (offset >= TS_PACKET_SIZE)
   {
      return;
   }
   offset += packet[offset] + 1;
   if (offset + 8 > TS_PACKET_SIZE)
   {
      return;
   }
   section = packet + offset;
   if ((section[0] != TDT_TABLE_ID) && (section[0] != TOT_TABLE_ID))
   {
      return;
   }

   mjd = (section[3] << 8) | section[4];
   utc = ((double)(mjd - MJD_UNIX_EPOCH) * 86400) +
      ((((section[5] >> 4) * 10) + (section[5] & 0x0f)) * 3600) +
      ((((section[6] >> 4) * 10) + (section[6] & 0x0f)) * 60) +
      (((section[7] >> 4) * 10) + (section[7] & 0x0f));

   if (!pacing->have_tdt)
   {
      pacing->have_tdt = TRUE;
      pacing->first_tdt = utc;
      pacing->first_tdt_bytes = pacing->bytes;
   }
   else if (utc > pacing->first_tdt)
   {
      /* Average since the first TDT, as the TDT has only one second resolution */
      pacing->byte_rate = (pacing->bytes - pacing->first_tdt_bytes) / (utc - pacing->first_tdt);
   }
   else if (utc < pacing->first_tdt)
   {
      /* The time went backwards: start again */
      pacing->first_tdt = utc;
      pacing->first_tdt_bytes = pacing->bytes;
      pacing->byte_rate = 0;
      pacing->anchored = FALSE;
   }
   pacing->last_tdt = utc;
   pacing->last_tdt_bytes = pacing->bytes;
}

/* With no PCR, the stream time is the last TDT plus the bytes since it at the average rate.
 * Until the rate is known (the second TDT), the input is not paced. */
static void PaceByTdt(S_PACING *pacing)
{
   if (pacing->have_tdt && (pacing->byte_rate > 0))
   {
      PaceToStreamTime(pacing, pacing->last_tdt +
         ((double)(pacing->bytes - pacing->last_tdt_bytes) / pacing->byte_rate));
   }
}

/******************************************************************************
 * End of file
 *****************************************************************************/
