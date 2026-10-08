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

#endif /* _NIPPAPP_H */
