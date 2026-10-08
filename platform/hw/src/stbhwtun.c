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
 * @brief   Tuner for NippApp: satellite (DVB-S2) tuner paths with no input yet.
 *          Every tune reports no lock. Milestone 2 replaces this with the file tuner, which
 *          feeds TS files and PCAP files (as GSE) named in a sources file.
 * @file    stbhwtun.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <string.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwdef.h"
#include "stbhwos.h"
#include "stbhwtun.h"

/*---local typedef structs for this file--------------------------------------*/
typedef struct
{
   U32BIT freq;
   U32BIT srate;
   BOOLEAN use_gse;
   GSE_PacketCallback gse_callback;
   void *gse_user_data;
} S_TUNER;

/*---local (static) variable declarations for this file-----------------------*/
static U8BIT num_paths = 0;
static S_TUNER tuners[NUM_TUNER_PATHS];

/*---global function definitions---------------------------------------------*/

void STB_TuneInitialise(U8BIT paths)
{
   FUNCTION_START(STB_TuneInitialise);

   num_paths = (paths <= NUM_TUNER_PATHS) ? paths : NUM_TUNER_PATHS;
   memset(tuners, 0, sizeof(tuners));

   FUNCTION_FINISH(STB_TuneInitialise);
}

void STB_TuneStartTuner(U8BIT path, U32BIT freq, U32BIT srate, E_STB_TUNE_FEC fec, S8BIT freq_off,
   E_STB_TUNE_TMODE tmode, E_STB_TUNE_TBWIDTH tbwidth, E_STB_TUNE_CMODE cmode,
   E_STB_TUNE_ANALOG_VIDEO_TYPE anlg_vtype)
{
   FUNCTION_START(STB_TuneStartTuner);

   USE_UNWANTED_PARAM(fec);
   USE_UNWANTED_PARAM(freq_off);
   USE_UNWANTED_PARAM(tmode);
   USE_UNWANTED_PARAM(tbwidth);
   USE_UNWANTED_PARAM(cmode);
   USE_UNWANTED_PARAM(anlg_vtype);

   if (path < num_paths)
   {
      tuners[path].freq = freq;
      tuners[path].srate = srate;

      /* No inputs yet: report what an empty transponder would */
      STB_OSSendEvent(FALSE, HW_EV_CLASS_TUNER, HW_EV_TYPE_NOTLOCKED, &path, sizeof(U8BIT));
   }

   FUNCTION_FINISH(STB_TuneStartTuner);
}

void STB_TuneStopTuner(U8BIT path)
{
   FUNCTION_START(STB_TuneStopTuner);
   if (path < num_paths)
   {
      tuners[path].freq = 0;
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
   USE_UNWANTED_PARAM(path);
   return 0;
}

U8BIT STB_TuneGetDataIntegrity(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return 0;
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

void STB_TuneSetLNBVoltage(U8BIT path, E_STB_TUNE_LNB_VOLTAGE voltage)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(voltage);
}

void STB_TuneSet22kState(U8BIT path, BOOLEAN state)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(state);
}

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

/******************************************************************************
 * End of file
 *****************************************************************************/
