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
 * @brief   NippApp internal interfaces
 * @file    nippapp.h
 */
#ifndef _NIPPAPP_H
#define _NIPPAPP_H

#include "techtype.h"

/**
 * @brief   DVBCore event handler, registered with APP_InitialiseDVB
 * @param   event event code, as defined in stberc.h
 * @param   event_data optional event-specific data
 * @param   data_size size of event_data
 */
void NIPPAPP_EventHandler(U32BIT event, void *event_data, U32BIT data_size);

/**
 * @brief   Whether NippApp has been asked to stop (SIGINT, SIGTERM or SIGQUIT)
 * @param   wait_ms time to wait for a stop request before returning, 0 to return at once
 * @return  TRUE if a stop has been requested
 */
BOOLEAN NIPPAPP_StopRequested(U32BIT wait_ms);

/**
 * @brief   Sets DVBCore's country, which decides how LCNs are allocated
 * @param   country 3-letter ISO 3166 code, e.g. "gbr"
 * @return  TRUE if set, FALSE if the code is not valid or not known to DVBCore
 */
BOOLEAN NIPPAPP_SetupCountry(const char *country);

/**
 * @brief   Searches every carrier in the sources file, creating the satellite and LNB records on
 *          first run. Runs on the main thread and returns when the searches are done.
 * @return  TRUE if every search ran, FALSE if stopped or the records could not be created
 */
BOOLEAN NIPPAPP_Scan(void);

/**
 * @brief   Prints the services in the database
 */
void NIPPAPP_PrintServices(void);

/**
 * @brief   Starts the HTTP server, which serves the files the NIP library receives at their
 *          broadcast paths and tunes services on demand
 * @param   port TCP port to listen on
 * @param   player_dir directory of the web player's files, served at /player/, or NULL
 * @return  TRUE if the server is running
 */
BOOLEAN HTTPD_Start(const char *port, const char *player_dir);

/**
 * @brief   Stops the HTTP server, waiting for requests in progress to finish
 */
void HTTPD_Stop(void);

/**
 * @brief   Starts the registry: from now on, every DVB-I document the NIP library receives is
 *          kept on disk, under registry/ in the working directory
 * @return  TRUE if started
 */
BOOLEAN REG_Init(void);

/**
 * @brief   Stops keeping DVB-I documents
 */
void REG_Stop(void);

/**
 * @brief   Whether a media type is one the registry keeps (DVB-I service list entry points,
 *          service lists, playlists), ignoring case and parameters
 * @param   filetype media type, may be NULL
 */
BOOLEAN REG_IsDvbiType(const char *filetype);

/**
 * @brief   Reads the registry's copy of a DVB-I document
 * @param   location broadcast path (/ses.com/dvbi/list.xml), URL or URN of the document
 * @param   data returns the document, zero-terminated, to be freed with free()
 * @param   len returns the length of the document
 * @param   filetype returns its media type, to be freed with free()
 * @return  TRUE if the registry has the document
 */
BOOLEAN REG_Load(const char *location, U8BIT **data, U32BIT *len, char **filetype);

/**
 * @brief   Decodes content sent compressed (the FDT's Content-Encoding: gzip, zlib or deflate)
 * @param   encoding the Content-Encoding
 * @param   data the content as broadcast
 * @param   len its length
 * @param   decoded returns the decoded content, zero-terminated, to be freed with free()
 * @param   decoded_len returns its length
 * @return  TRUE if decoded; FALSE for an encoding not known, or content that doesn't decode
 */
BOOLEAN ENC_Decode(const char *encoding, const U8BIT *data, U32BIT len, U8BIT **decoded, U32BIT *decoded_len);

#endif /* _NIPPAPP_H */
