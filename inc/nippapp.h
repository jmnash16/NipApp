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
 * @return  TRUE if the server is running
 */
BOOLEAN HTTPD_Start(const char *port);

/**
 * @brief   Stops the HTTP server, waiting for requests in progress to finish
 */
void HTTPD_Stop(void);

#endif /* _NIPPAPP_H */
