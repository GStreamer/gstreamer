/* GStreamer
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GST_BASE_AUDIO_UTILS_H_
#define _GST_BASE_AUDIO_UTILS_H_

#ifndef GST_USE_UNSTABLE_API
#warning "Base audio utils provide unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/audio/multichannel.h>

G_BEGIN_DECLS

/**
 * GstAudioFormatInfo:
 * @is_int: whether sample data is int or float
 * @rate: rate of sample data
 * @channels: number of channels in sample data
 * @width: width (in bits) of sample data
 * @depth: used bits in sample data (if integer)
 * @sign: sign of sample data (if integer)
 * @endian: endianness of sample data
 * @bpf: bytes per audio frame
 */
typedef struct _GstAudioFormatInfo {
  gboolean is_int;
  gint  rate;
  gint  channels;
  gint  width;
  gint  depth;
  gboolean sign;
  gint  endian;
  GstAudioChannelPosition *channel_pos;

  gint  bpf;
} GstAudioFormatInfo;

gboolean gst_base_audio_parse_caps (GstCaps * caps,
    GstAudioFormatInfo * state, gboolean * changed);

GstCaps *gst_base_audio_add_streamheader (GstCaps * caps, GstBuffer * buf, ...);

gboolean gst_base_audio_encoded_audio_convert (GstAudioFormatInfo * fmt,
    gint64 bytes, gint64 samples, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

gboolean gst_base_audio_raw_audio_convert (GstAudioFormatInfo * fmt, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

G_END_DECLS

#endif

