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

#define GST_TYPE_DECKLINK2_OUTPUT (gst_decklink2_output_get_type())
G_DECLARE_FINAL_TYPE (GstDeckLink2Output, gst_decklink2_output,
    GST, DECKLINK2_OUTPUT, GstObject);

typedef struct
{
  guint buffered_video;
  guint buffered_audio;
  GstClockTime video_running_time;
  GstClockTime audio_running_time;
  GstClockTime hw_time;
  GstClockTime buffered_video_time;
  GstClockTime buffered_audio_time;
  guint64 scheduled_video_frames;
  guint64 scheduled_audio_samples;
  guint64 late_count;
  guint64 drop_count;
  guint64 overrun_count;
  guint64 underrun_count;
  guint64 duplicate_count;
  guint64 dropped_sample_count;
  guint64 silent_sample_count;
} GstDecklink2OutputStats;

struct GstDeckLink2OutputVideoConfig
{
  GstDeckLink2DisplayMode display_mode;
  BMDVideoOutputFlags output_flags;
  GstDeckLink2KeyerMode keyer_mode;
  guint8 keyer_level;
  GstDeckLink2MappingFormat mapping_format;
};

struct GstDeckLink2OutputAudioConfig
{
  BMDAudioSampleType sample_type;
  guint channels;
};

struct GstDeckLink2OutputConfig
{
  guint n_preroll_frames;
  guint min_buffered;
  guint max_buffered;
  GstDeckLink2OutputVideoConfig video;
  GstDeckLink2OutputAudioConfig audio;
};

GstDeckLink2Output * gst_decklink2_output_new (IDeckLink * device,
                                               GstDeckLink2APILevel api_level);

GstCaps *            gst_decklink2_output_get_caps (GstDeckLink2Output * output,
                                                    BMDDisplayMode mode,
                                                    BMDPixelFormat format);

gboolean             gst_decklink2_output_get_display_mode (GstDeckLink2Output * output,
                                                            const GstVideoInfo * info,
                                                            GstDeckLink2DisplayMode * display_mode);

guint                gst_decklink2_output_get_max_audio_channels (GstDeckLink2Output * output);

HRESULT              gst_decklink2_output_configure        (GstDeckLink2Output * output,
                                                            BMDProfileID profile_id,
                                                            const GstDeckLink2OutputConfig * config);

IDeckLinkVideoFrame * gst_decklink2_output_upload          (GstDeckLink2Output * output,
                                                            const GstVideoInfo * info,
                                                            const GstDeckLink2HDRMetadata * hdr_metadata,
                                                            GstBuffer * buffer,
                                                            BMDTimecodeFormat timecode_format,
                                                            gint caption_line,
                                                            gint afd_bar_line,
                                                            gboolean output_vanc);


GstFlowReturn         gst_decklink2_output_schedule_stream  (GstDeckLink2Output * output,
                                                             IDeckLinkVideoFrame * frame,
                                                             guint8 *audio_buf,
                                                             gsize audio_buf_size,
                                                             GstDecklink2OutputStats *stats);

HRESULT               gst_decklink2_output_stop            (GstDeckLink2Output * output);

G_END_DECLS
