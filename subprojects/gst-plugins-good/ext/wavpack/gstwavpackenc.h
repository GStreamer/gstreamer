/* GStreamer Wavpack encoder plugin
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackenc.h: Wavpack audio encoder
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

#ifndef __GST_WAVPACK_ENC_H__
#define __GST_WAVPACK_ENC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>

#include <wavpack/wavpack.h>

G_BEGIN_DECLS

#define GST_TYPE_WAVPACK_ENC (gst_wavpack_enc_get_type())
G_DECLARE_FINAL_TYPE (GstWavpackEnc, gst_wavpack_enc, GST, WAVPACK_ENC,
    GstAudioEncoder)

typedef struct
{
  gboolean correction;
  GstWavpackEnc *wavpack_enc;
  gboolean passthrough;
} GstWavpackEncWriteID;

struct _GstWavpackEnc
{
  GstAudioEncoder element;

  /*< private > */
  GstPad *wvcsrcpad;

  GstFlowReturn srcpad_last_return;
  GstFlowReturn wvcsrcpad_last_return;

  WavpackConfig *wp_config;
  WavpackContext *wp_context;

  gint samplerate;
  gint channels;
  gint channel_mask;
  gint8 channel_mapping[8];
  gboolean need_channel_remap;
  gint depth;

  GstWavpackEncWriteID wv_id;
  GstWavpackEncWriteID wvc_id;

  guint mode;
  guint bitrate;
  gdouble bps;
  guint correction_mode;
  gboolean md5;
  GChecksum *md5_context;
  guint extra_processing;
  guint joint_stereo_mode;

  void *first_block;
  int32_t first_block_size;

  GstBuffer *pending_buffer;
  gint32 pending_offset;
  GstEvent *pending_segment;

  GstClockTime timestamp_offset;
  GstClockTime next_ts;
};

gboolean gst_wavpack_enc_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_WAVPACK_ENC_H__ */
