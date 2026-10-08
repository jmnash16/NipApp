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
 * @brief   Platform functions NippApp does not need: audio/video decoding, on-screen display,
 *          PVR, front panel, VBI and EBU teletext. NippApp is headless and serves the files it
 *          receives rather than decoding them, so these do nothing.
 * @file    stubs.c
 */

/*---includes for this file---------------------------------------------------*/
/* compiler library header files */
#include <string.h>

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwdef.h"
#include "stbhwc.h"
#include "stbhwav.h"
#include "stbhwosd.h"
#include "stbhwfp.h"
#include "stbpvrpr.h"

/*---global function definitions---------------------------------------------*/

/*---EBU teletext-------------------------------------------------------------------------------------*/

void STB_EBUInitialise(void)
{
}

/*---Audio/video decoding: NippApp never decodes; it serves the received files ---------------------*/

void STB_AVApplyVideoTransformation(U8BIT path, S_RECTANGLE *input, S_RECTANGLE *output)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(input);
   USE_UNWANTED_PARAM(output);
}

void STB_AVBlankVideo(U8BIT path, BOOLEAN blank)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(blank);
}

void STB_AVChangeADMode(U8BIT path, E_STB_AV_AUDIO_MODE mode)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(mode);
}

void STB_AVChangeAudioMode(U8BIT path, E_STB_AV_AUDIO_MODE mode)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(mode);
}

void STB_AVDisableHDMIDecoding(void)
{
}

void STB_AVEnableHDMIDecoding(void)
{
}

U8BIT STB_AVGetAudioVolume(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
   return 0;
}

void STB_AVGetHDMINativeResolution(U16BIT *width, U16BIT *height)
{
   *width = SCREEN_WIDTH;
   *height = SCREEN_HEIGHT;
}

U8BIT STB_AVGetHDMISupportedModes(E_STB_AV_VIDEO_FORMAT **modes)
{
   *modes = NULL;
   return 0;
}

S16BIT STB_AVGetMaxPlaySpeed(U8BIT video_decoder)
{
   USE_UNWANTED_PARAM(video_decoder);
   return 0;
}

S16BIT STB_AVGetMinPlaySpeed(U8BIT video_decoder)
{
   USE_UNWANTED_PARAM(video_decoder);
   return 0;
}

S16BIT STB_AVGetNextPlaySpeed(U8BIT video_decoder, S16BIT speed, S16BIT inc, BOOLEAN include_slow_speeds)
{
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(speed);
   USE_UNWANTED_PARAM(inc);
   USE_UNWANTED_PARAM(include_slow_speeds);
   return 0;
}

void STB_AVGetSTC(U8BIT path, U8BIT stc[5])
{
   USE_UNWANTED_PARAM(path);
   memset(stc, 0, 5);
}

void STB_AVInitialise(U8BIT audio_paths, U8BIT video_paths)
{
   USE_UNWANTED_PARAM(audio_paths);
   USE_UNWANTED_PARAM(video_paths);
}

BOOLEAN STB_AVIsHDCPAuthenticated(void)
{
   return FALSE;
}

BOOLEAN STB_AVSetADCodec(U8BIT path, E_STB_AV_AUDIO_CODEC codec)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(codec);
   return FALSE;
}

void STB_AVSetADSource(U8BIT path, E_STB_AV_DECODE_SOURCE source, U32BIT param)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(source);
   USE_UNWANTED_PARAM(param);
}

void STB_AVSetADVolume(U8BIT path, U8BIT vol)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(vol);
}

void STB_AVSetAVOutput(BOOLEAN av_on)
{
   USE_UNWANTED_PARAM(av_on);
}

void STB_AVSetAVOutputSource(E_STB_AV_OUTPUTS output, E_STB_AV_SOURCES source, U32BIT param)
{
   USE_UNWANTED_PARAM(output);
   USE_UNWANTED_PARAM(source);
   USE_UNWANTED_PARAM(param);
}

BOOLEAN STB_AVSetAudioCodec(U8BIT path, E_STB_AV_AUDIO_CODEC codec)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(codec);
   return FALSE;
}

void STB_AVSetAudioDelay(U8BIT path, U16BIT millisecond)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(millisecond);
}

void STB_AVSetAudioMute(U8BIT path, BOOLEAN mute)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(mute);
}

void STB_AVSetAudioSource(U8BIT path, E_STB_AV_DECODE_SOURCE source, U32BIT param)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(source);
   USE_UNWANTED_PARAM(param);
}

void STB_AVSetAudioVolume(U8BIT path, U8BIT vol)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(vol);
}

void STB_AVSetHDMIAudioMode(U8BIT path, E_STB_DIGITAL_AUDIO_TYPE audio_type)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(audio_type);
}

void STB_AVSetHDMIStandby(BOOLEAN standby)
{
   USE_UNWANTED_PARAM(standby);
}

void STB_AVSetSpdifMode(U8BIT path, E_STB_DIGITAL_AUDIO_TYPE audio_type)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(audio_type);
}

void STB_AVSetTVType(U8BIT path, E_STB_AV_ASPECT_RATIO ratio, E_STB_AV_VIDEO_FORMAT format)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(ratio);
   USE_UNWANTED_PARAM(format);
}

void STB_AVSetVideoCallback(U8BIT path, void (*callback)(S_STB_AV_VIDEO_INFO *, void *), void *user_data)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(callback);
   USE_UNWANTED_PARAM(user_data);
}

BOOLEAN STB_AVSetVideoCodec(U8BIT path, E_STB_AV_VIDEO_CODEC codec)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(codec);
   return FALSE;
}

void STB_AVSetVideoSource(U8BIT path, E_STB_AV_DECODE_SOURCE source, U32BIT param)
{
   USE_UNWANTED_PARAM(path);
   USE_UNWANTED_PARAM(source);
   USE_UNWANTED_PARAM(param);
}

void STB_AVStartADDecoding(U8BIT decoder)
{
   USE_UNWANTED_PARAM(decoder);
}

void STB_AVStartAudioDecoding(U8BIT decoder)
{
   USE_UNWANTED_PARAM(decoder);
}

void STB_AVStartVideoDecoding(U8BIT path)
{
   USE_UNWANTED_PARAM(path);
}

void STB_AVStopADDecoding(U8BIT decoder)
{
   USE_UNWANTED_PARAM(decoder);
}

void STB_AVStopAudioDecoding(U8BIT decoder)
{
   USE_UNWANTED_PARAM(decoder);
}

void STB_AVStopVideoDecoding(U8BIT decoder)
{
   USE_UNWANTED_PARAM(decoder);
}

/*---On-screen display: NippApp is headless --------------------------------------------------------*/

void * STB_OSDCreateRegion(U16BIT width, U16BIT height, U8BIT depth)
{
   USE_UNWANTED_PARAM(width);
   USE_UNWANTED_PARAM(height);
   USE_UNWANTED_PARAM(depth);
   return NULL;
}

void STB_OSDDestroyRegion(void *handle)
{
   USE_UNWANTED_PARAM(handle);
}

BOOLEAN STB_OSDDisableUIRegion(void)
{
   return FALSE;
}

void STB_OSDDrawBitmapInRegion(void *handle, U16BIT x, U16BIT y, U16BIT w, U16BIT h, U8BIT *bitmap, BOOLEAN non_modifying_colour)
{
   USE_UNWANTED_PARAM(handle);
   USE_UNWANTED_PARAM(x);
   USE_UNWANTED_PARAM(y);
   USE_UNWANTED_PARAM(w);
   USE_UNWANTED_PARAM(h);
   USE_UNWANTED_PARAM(bitmap);
   USE_UNWANTED_PARAM(non_modifying_colour);
}

BOOLEAN STB_OSDEnable(BOOLEAN enable)
{
   USE_UNWANTED_PARAM(enable);
   return FALSE;
}

BOOLEAN STB_OSDEnableUIRegion(void)
{
   return FALSE;
}

void STB_OSDFillRegion(void *handle, U8BIT colour)
{
   USE_UNWANTED_PARAM(handle);
   USE_UNWANTED_PARAM(colour);
}

void STB_OSDGetSize(U16BIT *width, U16BIT *height)
{
   *width = SCREEN_WIDTH;
   *height = SCREEN_HEIGHT;
}

void STB_OSDHideRegion(void *handle)
{
   USE_UNWANTED_PARAM(handle);
}

void STB_OSDInitialise(U8BIT num_max_regions)
{
   USE_UNWANTED_PARAM(num_max_regions);
}

void STB_OSDMoveRegion(void *handle, U16BIT x, U16BIT y)
{
   USE_UNWANTED_PARAM(handle);
   USE_UNWANTED_PARAM(x);
   USE_UNWANTED_PARAM(y);
}

void STB_OSDRegionFillRect(void *handle, U16BIT left, U16BIT top, U16BIT width, U16BIT height, U8BIT colour)
{
   USE_UNWANTED_PARAM(handle);
   USE_UNWANTED_PARAM(left);
   USE_UNWANTED_PARAM(top);
   USE_UNWANTED_PARAM(width);
   USE_UNWANTED_PARAM(height);
   USE_UNWANTED_PARAM(colour);
}

void STB_OSDRegionToRegionCopy(void *handle_new, void *handle_orig)
{
   USE_UNWANTED_PARAM(handle_new);
   USE_UNWANTED_PARAM(handle_orig);
}

void STB_OSDResize(BOOLEAN scaling, U16BIT width, U16BIT height, U16BIT x_offset, U16BIT y_offset)
{
   USE_UNWANTED_PARAM(scaling);
   USE_UNWANTED_PARAM(width);
   USE_UNWANTED_PARAM(height);
   USE_UNWANTED_PARAM(x_offset);
   USE_UNWANTED_PARAM(y_offset);
}

void STB_OSDSetRGBPalette(void *handle, U32BIT *trgb)
{
   USE_UNWANTED_PARAM(handle);
   USE_UNWANTED_PARAM(trgb);
}

void STB_OSDSetRegionDisplaySize(U16BIT width, U16BIT height)
{
   USE_UNWANTED_PARAM(width);
   USE_UNWANTED_PARAM(height);
}

void STB_OSDSetYCrCbPalette(void *region_handle, U32BIT *tycrcb)
{
   USE_UNWANTED_PARAM(region_handle);
   USE_UNWANTED_PARAM(tycrcb);
}

void STB_OSDShowRegion(void *handle)
{
   USE_UNWANTED_PARAM(handle);
}

void STB_OSDUpdateRegions(void)
{
}

/*---PVR: not supported ----------------------------------------------------------------------------*/

U8BIT STB_PVRAcquireRecorderIndex(U8BIT tuner, U8BIT demux)
{
   USE_UNWANTED_PARAM(tuner);
   USE_UNWANTED_PARAM(demux);
   return 0xff;
}

BOOLEAN STB_PVRCanBeUsedForRecording(U16BIT disk_id, U8BIT *basename)
{
   USE_UNWANTED_PARAM(disk_id);
   USE_UNWANTED_PARAM(basename);
   return FALSE;
}

BOOLEAN STB_PVRDeleteRecording(U16BIT disk_id, U8BIT *basename)
{
   USE_UNWANTED_PARAM(disk_id);
   USE_UNWANTED_PARAM(basename);
   return FALSE;
}

BOOLEAN STB_PVRGetElapsedTime(U8BIT audio_decoder, U8BIT video_decoder, U8BIT *elapsed_hours, U8BIT *elapsed_mins, U8BIT *elapsed_secs)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(elapsed_hours);
   USE_UNWANTED_PARAM(elapsed_mins);
   USE_UNWANTED_PARAM(elapsed_secs);
   return FALSE;
}

S16BIT STB_PVRGetPlaySpeed(U8BIT audio_decoder, U8BIT video_decoder)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   return 0;
}

BOOLEAN STB_PVRGetRecordingSize(U16BIT disk_id, U8BIT *basename, U32BIT *rec_size_kb)
{
   USE_UNWANTED_PARAM(disk_id);
   USE_UNWANTED_PARAM(basename);
   USE_UNWANTED_PARAM(rec_size_kb);
   return FALSE;
}

U8BIT STB_PVRInitPlayback(U8BIT num_audio_decoders, U8BIT num_video_decoders)
{
   USE_UNWANTED_PARAM(num_audio_decoders);
   USE_UNWANTED_PARAM(num_video_decoders);
   return 0;
}

U8BIT STB_PVRInitRecording(U8BIT num_tuners)
{
   USE_UNWANTED_PARAM(num_tuners);
   return 0;
}

BOOLEAN STB_PVRIsPlayStarted(U8BIT audio_decoder, U8BIT video_decoder)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   return FALSE;
}

BOOLEAN STB_PVRIsRecordStarted(U8BIT rec_index)
{
   USE_UNWANTED_PARAM(rec_index);
   return FALSE;
}

BOOLEAN STB_PVRIsValidRecording(U16BIT disk_id, U8BIT *basename)
{
   USE_UNWANTED_PARAM(disk_id);
   USE_UNWANTED_PARAM(basename);
   return FALSE;
}

void STB_PVRPlayEnabled(U8BIT audio_decoder, U8BIT video_decoder, BOOLEAN *video, BOOLEAN *audio)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   *video = FALSE;
   *audio = FALSE;
}

void STB_PVRPlayHasVideo(U8BIT audio_decoder, U8BIT video_decoder, BOOLEAN has_video)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(has_video);
}

BOOLEAN STB_PVRPlaySetPosition(U8BIT audio_decoder, U8BIT video_decoder, U32BIT position_in_seconds)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(position_in_seconds);
   return FALSE;
}

BOOLEAN STB_PVRPlayStart(U16BIT disk_id, U8BIT audio_decoder, U8BIT video_decoder, U8BIT demux, U8BIT *basename)
{
   USE_UNWANTED_PARAM(disk_id);
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(demux);
   USE_UNWANTED_PARAM(basename);
   return FALSE;
}

void STB_PVRPlayStop(U8BIT audio_decoder, U8BIT video_decoder)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
}

void STB_PVRPlayTrickMode(U8BIT audio_decoder, U8BIT video_decoder, E_STB_PVR_PLAY_MODE mode, S16BIT speed)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(mode);
   USE_UNWANTED_PARAM(speed);
}

BOOLEAN STB_PVRRecordChangePids(U8BIT rec_index, U16BIT num_pids, S_PVR_PID_INFO *pids_array)
{
   USE_UNWANTED_PARAM(rec_index);
   USE_UNWANTED_PARAM(num_pids);
   USE_UNWANTED_PARAM(pids_array);
   return FALSE;
}

void STB_PVRRecordEnabled(U8BIT rec_index, BOOLEAN *video, BOOLEAN *audio)
{
   USE_UNWANTED_PARAM(rec_index);
   *video = FALSE;
   *audio = FALSE;
}

BOOLEAN STB_PVRRecordPause(U8BIT rec_index)
{
   USE_UNWANTED_PARAM(rec_index);
   return FALSE;
}

BOOLEAN STB_PVRRecordResume(U8BIT rec_index)
{
   USE_UNWANTED_PARAM(rec_index);
   return FALSE;
}

BOOLEAN STB_PVRRecordStart(U16BIT disk_id, U8BIT rec_index, U8BIT *basename, U16BIT num_pids, S_PVR_PID_INFO *pid_array)
{
   USE_UNWANTED_PARAM(disk_id);
   USE_UNWANTED_PARAM(rec_index);
   USE_UNWANTED_PARAM(basename);
   USE_UNWANTED_PARAM(num_pids);
   USE_UNWANTED_PARAM(pid_array);
   return FALSE;
}

void STB_PVRRecordStop(U8BIT rec_index)
{
   USE_UNWANTED_PARAM(rec_index);
}

void STB_PVRReleaseRecorderIndex(U8BIT rec_index)
{
   USE_UNWANTED_PARAM(rec_index);
}

BOOLEAN STB_PVRSetPlaySpeed(U8BIT audio_decoder, U8BIT video_decoder, S16BIT speed)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(speed);
   return FALSE;
}

void STB_PVRSetPlayStartMode(U8BIT audio_decoder, U8BIT video_decoder, E_STB_PVR_START_MODE mode)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(mode);
}

void STB_PVRSetPlaybackDecryptionKey(U8BIT audio_decoder, U8BIT video_decoder, BOOLEAN state, U8BIT *key, U8BIT *iv, U32BIT key_len)
{
   USE_UNWANTED_PARAM(audio_decoder);
   USE_UNWANTED_PARAM(video_decoder);
   USE_UNWANTED_PARAM(state);
   USE_UNWANTED_PARAM(key);
   USE_UNWANTED_PARAM(iv);
   USE_UNWANTED_PARAM(key_len);
}

void STB_PVRSetRecordEncryptionKey(U8BIT rec_index, BOOLEAN state, U8BIT *key, U8BIT *iv, U32BIT key_len)
{
   USE_UNWANTED_PARAM(rec_index);
   USE_UNWANTED_PARAM(state);
   USE_UNWANTED_PARAM(key);
   USE_UNWANTED_PARAM(iv);
   USE_UNWANTED_PARAM(key_len);
}

void STB_PVRSetRecordStartMode(U8BIT rec_index, E_STB_PVR_START_MODE mode, U32BIT param)
{
   USE_UNWANTED_PARAM(rec_index);
   USE_UNWANTED_PARAM(mode);
   USE_UNWANTED_PARAM(param);
}

/*---Front panel -----------------------------------------------------------------------------------*/

void STB_FPSetClock(U16BIT mjd, U16BIT year, U8BIT month, U8BIT day, U8BIT hours, U8BIT minutes, U8BIT seconds)
{
   USE_UNWANTED_PARAM(mjd);
   USE_UNWANTED_PARAM(year);
   USE_UNWANTED_PARAM(month);
   USE_UNWANTED_PARAM(day);
   USE_UNWANTED_PARAM(hours);
   USE_UNWANTED_PARAM(minutes);
   USE_UNWANTED_PARAM(seconds);
}

/*---VBI insertion ---------------------------------------------------------------------------------*/

void STB_HWInitialiseVBI(void)
{
}

void STB_HWVBIInsert(U8BIT *pes_data_field, U32BIT num_bytes)
{
   USE_UNWANTED_PARAM(pes_data_field);
   USE_UNWANTED_PARAM(num_bytes);
}

/******************************************************************************
 * End of file
 *****************************************************************************/
