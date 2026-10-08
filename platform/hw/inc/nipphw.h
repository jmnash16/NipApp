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
 * @brief   NippApp platform internals: the file tuner's sources and the link from the tuner
 *          to the demux. Not part of the DVBCore platform API.
 * @file    nipphw.h
 */
#ifndef _NIPPHW_H
#define _NIPPHW_H

#include "techtype.h"

#define TS_PACKET_SIZE     188
#define MAX_SOURCE_PATH    512

typedef enum
{
   SOURCE_KIND_TS,
   SOURCE_KIND_PCAP
} E_SOURCE_KIND;

/* One line of the sources file: the carrier a capture stands in for */
typedef struct
{
   U32BIT freq_mhz;           /* RF frequency */
   BOOLEAN horizontal;        /* polarisation */
   U32BIT symbol_rate;        /* ksym/s */
   E_SOURCE_KIND kind;
   BOOLEAN loop;              /* at the end of the file, start again (TRUE) or stop (FALSE) */
   char file[MAX_SOURCE_PATH];
} S_SOURCE;

/**
 * @brief   Reads the sources file. Must be called before DVBCore initialises the tuner.
 *          Relative file names are taken relative to the sources file's directory.
 * @param   filename path of the sources file
 * @return  TRUE if the file was read; lines in error are reported and skipped
 */
BOOLEAN TUN_LoadSources(const char *filename);

/**
 * @brief   Returns the number of sources read by TUN_LoadSources
 */
U16BIT TUN_GetNumSources(void);

/**
 * @brief   Returns a source
 * @param   index source index, 0 to TUN_GetNumSources() - 1
 * @return  the source, or NULL if index is out of range
 */
const S_SOURCE* TUN_GetSource(U16BIT index);

/**
 * @brief   Passes TS packets from a tuner to every demux whose source is that tuner.
 *          Called on the tuner's feeder thread.
 * @param   tuner_path tuner the packets were received on
 * @param   packets whole 188-byte TS packets, each starting with the sync byte
 * @param   num_packets number of packets
 */
void DMX_FeedPackets(U8BIT tuner_path, U8BIT *packets, U32BIT num_packets);

#endif /* _NIPPHW_H */
