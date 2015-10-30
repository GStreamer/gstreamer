/* GStreamer
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstchannelmix.h: setup of channel conversion matrices
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

#ifndef __GST_CHANNEL_MIX_H__
#define __GST_CHANNEL_MIX_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

typedef struct _GstChannelMix GstChannelMix;

/**
 * GstChannelMixFlags:
 * @GST_CHANNEL_MIX_FLAGS_NONE: no flag
 * @GST_CHANNEL_MIX_FLAGS_UNPOSITIONED_IN: input channels are explicitly unpositioned
 * @GST_CHANNEL_MIX_FLAGS_UNPOSITIONED_OUT: output channels are explicitly unpositioned
 *
 * Flags passed to gst_channel_mix_new()
 */
typedef enum {
  GST_CHANNEL_MIX_FLAGS_NONE             = 0,
  GST_CHANNEL_MIX_FLAGS_UNPOSITIONED_IN  = (1 << 0),
  GST_CHANNEL_MIX_FLAGS_UNPOSITIONED_OUT = (1 << 1)
} GstChannelMixFlags;

GstChannelMix * gst_channel_mix_new             (GstChannelMixFlags flags,
                                                 gint in_channels,
                                                 GstAudioChannelPosition in_position[64],
                                                 gint out_channels,
                                                 GstAudioChannelPosition out_position[64]);
void            gst_channel_mix_free            (GstChannelMix *mix);

/*
 * Checks for passthrough (= identity matrix).
 */
gboolean        gst_channel_mix_is_passthrough     (GstChannelMix *mix);

/*
 * Do actual mixing.
 */
void            gst_channel_mix_mix             (GstChannelMix   * mix,
                                                 GstAudioFormat    format,
                                                 GstAudioLayout    layout,
                                                 const gpointer    in_data,
                                                 gpointer          out_data,
                                                 gint              samples);

#endif /* __GST_CHANNEL_MIX_H__ */
