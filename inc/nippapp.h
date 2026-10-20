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

#include <time.h>

#include "techtype.h"

#define NIPPAPP_VERSION    "0.1.0"

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
 * @brief   Asks the main thread to scan every source again (admin API)
 * @return  FALSE if a scan is already running or asked for
 */
BOOLEAN NIPPAPP_RequestScan(void);

/**
 * @brief   The scan state, for status
 * @param   running returns TRUE while a scan is running or asked for
 * @param   completed returns TRUE if the last scan completed
 * @param   finished returns when the last scan finished, or 0 if none has
 * @param   since returns when NippApp started
 */
void NIPPAPP_GetScanState(BOOLEAN *running, BOOLEAN *completed, time_t *finished, time_t *since);

/**
 * @brief   The pre-buffering setting (--prebuffer), for status
 * @return  the most channels pre-buffered on each carrier: 0 when off, DVBNIP_PREBUFFER_ALL for all
 */
U16BIT NIPPAPP_GetPrebuffer(void);

/**
 * @brief   The disk store setting (--store-dir), for status
 * @param   threshold returns the size from which files go to disk
 * @param   all_services returns TRUE if every service's files can go to disk (--disk-all)
 * @return  TRUE if there is a disk store
 */
BOOLEAN NIPPAPP_GetDiskStore(U32BIT *threshold, BOOLEAN *all_services);

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
 * @brief   Whether the admin API (/api/status, /api/scan) answers any address, or only this machine
 *          (the default). Set before HTTPD_Start.
 */
void HTTPD_AllowAdminFromAny(BOOLEAN any);

/**
 * @brief   Starts announcing the gateway with DNS-SD/mDNS (clause 11.1), through avahi-daemon: a
 *          _dvbservdsc._tcp and an _http._tcp service on each IPv4 interface, with dvbi_sep giving
 *          the entry points URL. Waits for avahi-daemon if it isn't running.
 * @param   name instance name, or NULL for "NippApp on <hostname>"
 * @param   port the HTTP port
 * @return  FALSE if the announcement can't be started
 */
BOOLEAN MDNS_Start(const char *name, int port);

/**
 * @brief   Withdraws the announcement
 */
void MDNS_Stop(void);

/**
 * @brief   The gateway's state as JSON, for /api/status: version, uptime, scan, sources, tuners,
 *          NIP streams and the services being received, file store, wall clock, networks
 * @return  the JSON, to be freed with free(), or NULL
 */
char* ADMIN_StatusJson(void);

/* REG_FilterEntryPoints results */
#define REG_NOT_FILTERED      0     /* no parameter that filters: serve the entry points as they are */
#define REG_FILTERED          1
#define REG_BAD_QUERY         (-1)  /* a parameter not defined, or a value not valid: 400 */

/**
 * @brief   Filters service list entry points by the DVB-I query parameters (TS 103 876 clause
 *          8.1.7.4, TS 103 770 clause 5.1.3.2): TargetCountry, regulatorListFlag, Delivery, Language,
 *          Genre, ProviderName, each as Name or Name[] and repeatable, and inlineImages. Every
 *          parameter given must match, any of its values; an offering that doesn't declare a
 *          property matches whatever is asked for it; a provider left with no offerings is removed.
 * @param   data the entry points
 * @param   len their length
 * @param   query the request's query string
 * @param   filtered returns the filtered entry points (REG_FILTERED), to be freed with free()
 * @param   filtered_len returns their length
 * @return  REG_FILTERED, REG_NOT_FILTERED or REG_BAD_QUERY
 */
int REG_FilterEntryPoints(const U8BIT *data, U32BIT len, const char *query, U8BIT **filtered,
   U32BIT *filtered_len);

/* GUIDE_Query: the request isn't a content guide query; serve the path as a file */
#define GUIDE_NOT_A_QUERY     (-1)

/**
 * @brief   Answers a DVB-I content guide query (TS 103 876 clause D.6.2) made to a NIP Content
 *          Guide Manifest's URL: ?sid=<ServiceIDRef>&start=<s>&end=<s>[&inclusive=true] or
 *          ?sid=<ServiceIDRef>&now_next=true
 * @param   path the request path (the manifest's broadcast path)
 * @param   query the query string
 * @param   body returns the answer, a TVAMain document, to be freed with free()
 * @param   len returns its length
 * @return  GUIDE_NOT_A_QUERY, or the HTTP status: 200, 400 (bad parameters), 404 (no guide), 503
 *          (the guide is still being received; try again after GUIDE_RetryAfter() seconds)
 */
int GUIDE_Query(const char *path, const char *query, char **body, U32BIT *len);

/**
 * @brief   Seconds to give in Retry-After when GUIDE_Query answers 503
 */
int GUIDE_RetryAfter(void);

/**
 * @brief   Calls back with the name of each network whose entry points the registry keeps
 */
void REG_ListNetworks(void *user_data, void (*callback)(void *user_data, const char *name));

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
