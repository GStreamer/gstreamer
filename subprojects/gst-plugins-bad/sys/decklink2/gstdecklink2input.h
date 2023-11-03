/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <gst/gst.h>
#include "gstdecklink2utils.h"

G_BEGIN_DECLS

#define GST_TYPE_DECKLINK2_INPUT (gst_decklink2_input_get_type())
G_DECLARE_FINAL_TYPE (GstDeckLink2Input, gst_decklink2_input,
    GST, DECKLINK2_INPUT, GstObject);

typedef struct _GstDeckLink2InputVideoConfig
{
  BMDVideoConnection connection;
  BMDDisplayMode display_mode;
  BMDPixelFormat pixel_format;
  BMDTimecodeFormat timecode_format;
  gboolean auto_detect;
  gboolean output_cc;
  gboolean output_afd_bar;
  gboolean output_vanc;
} GstDeckLink2InputVideoConfig;

typedef struct _GstDeckLink2InputAudioConfig
{
  BMDAudioConnection connection;
  BMDAudioSampleType sample_type;
  GstDeckLink2AudioChannels channels;
} GstDeckLink2InputAudioConfig;

typedef struct
{
  gboolean have_mapping;
  gboolean window_filled;
  guint64 generation;
  gdouble slope;
  guint64 xbase;
  guint64 b;
  guint64 num;
  guint64 den;
  gdouble r_squared;
  GstClockTimeDiff diff;
} GstDeckLink2InputCalibration;

typedef struct
{
  GstClockTime stream_time;
  GstClockTime hardware_time;
  GstClockTime hardware_reference_time;
  GstClockTime capture_time;
  GstClockTime calibrated_time;
  guint64 audio_packet_time;
  GstClockTime audio_stream_time;
  GstClockTime calibrated_audio_time;
  GstClockTimeDiff av_sync;
  GstDeckLink2InputCalibration calibration;
} GstDeckLink2InputStats;

void               gst_decklink2_input_stats_init (GstDeckLink2InputStats * stats);

GstDeckLink2Input * gst_decklink2_input_new (IDeckLink * device,
                                             GstDeckLink2APILevel api_level);

GstCaps *           gst_decklink2_input_get_caps (GstDeckLink2Input * input,
                                                  BMDDisplayMode mode,
                                                  BMDPixelFormat format);

gboolean            gst_decklink2_input_get_display_mode (GstDeckLink2Input * input,
                                                          const GstVideoInfo * info,
                                                          GstDeckLink2DisplayMode * display_mode);

HRESULT             gst_decklink2_input_start (GstDeckLink2Input * input,
                                               GstElement * client,
                                               BMDProfileID profile_id,
                                               guint buffer_size,
                                               GstClockTime skip_first_time,
                                               const GstDeckLink2InputVideoConfig * video_config,
                                               const GstDeckLink2InputAudioConfig * audio_config);

void                gst_decklink2_input_stop (GstDeckLink2Input * input);

void                gst_decklink2_input_set_flushing (GstDeckLink2Input * input,
                                                      gboolean flush);

GstFlowReturn       gst_decklink2_input_get_data (GstDeckLink2Input * input,
                                                  GstBuffer ** buffer,
                                                  GstCaps ** caps,
                                                  GstDeckLink2InputStats * stats);

gboolean            gst_decklink2_input_has_signal (GstDeckLink2Input * input);

G_END_DECLS
