/* GStreamer
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *           (C) 2015 Wim Taymans <wim.taymans@gmail.com>
 *
 * audio-channel-mix.h: setup of channel conversion matrices
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

#ifndef __GST_AUDIO_CHANNEL_MIX_H__
#define __GST_AUDIO_CHANNEL_MIX_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

typedef struct _GstAudioChannelMix GstAudioChannelMix;

/**
 * GstAudioChannelMixFlags:
 * @GST_AUDIO_CHANNEL_MIX_FLAGS_NONE: no flag
 * @GST_AUDIO_CHANNEL_MIX_FLAGS_NON_INTERLEAVED: channels are not interleaved
 * @GST_AUDIO_CHANNEL_MIX_FLAGS_UNPOSITIONED_IN: input channels are explicitly unpositioned
 * @GST_AUDIO_CHANNEL_MIX_FLAGS_UNPOSITIONED_OUT: output channels are explicitly unpositioned
 *
 * Flags passed to gst_audio_channel_mix_new()
 */
typedef enum {
  GST_AUDIO_CHANNEL_MIX_FLAGS_NONE             = 0,
  GST_AUDIO_CHANNEL_MIX_FLAGS_NON_INTERLEAVED  = (1 << 0),
  GST_AUDIO_CHANNEL_MIX_FLAGS_UNPOSITIONED_IN  = (1 << 1),
  GST_AUDIO_CHANNEL_MIX_FLAGS_UNPOSITIONED_OUT = (1 << 2)
} GstAudioChannelMixFlags;

GstAudioChannelMix * gst_audio_channel_mix_new       (GstAudioChannelMixFlags flags,
                                                      GstAudioFormat format,
                                                      gint in_channels,
                                                      GstAudioChannelPosition *in_position,
                                                      gint out_channels,
                                                      GstAudioChannelPosition *out_position);
void                 gst_audio_channel_mix_free      (GstAudioChannelMix *mix);

/*
 * Checks for passthrough (= identity matrix).
 */
gboolean        gst_audio_channel_mix_is_passthrough  (GstAudioChannelMix *mix);

/*
 * Do actual mixing.
 */
void            gst_audio_channel_mix_samples   (GstAudioChannelMix * mix,
                                                 const gpointer       in[],
                                                 gpointer             out[],
                                                 gint                 samples);

#endif /* __GST_AUDIO_CHANNEL_MIX_H__ */
