/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_AMC_H__
#define __GST_AMC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <jni.h>

G_BEGIN_DECLS

typedef struct _GstAmcCodecInfo GstAmcCodecInfo;
typedef struct _GstAmcCodecType GstAmcCodecType;
typedef struct _GstAmcCodec GstAmcCodec;
typedef struct _GstAmcBufferInfo GstAmcBufferInfo;
typedef struct _GstAmcFormat GstAmcFormat;
typedef struct _GstAmcBuffer GstAmcBuffer;

struct _GstAmcCodecType {
  gchar *mime;

  gint *color_formats;
  gint n_color_formats;

  struct {
    gint profile;
    gint level;
  } *profile_levels;
  gint n_profile_levels;
};

struct _GstAmcCodecInfo {
  gchar *name;
  gboolean is_encoder;
  GstAmcCodecType *supported_types;
  gint n_supported_types;
};

struct _GstAmcBuffer {
  jobject object; /* global reference */
  guint8 *data;
  gsize size;
};

struct _GstAmcFormat {
  /* < private > */
  jobject object; /* global reference */
};

struct _GstAmcCodec {
  /* < private > */
  jobject object; /* global reference */
};

struct _GstAmcBufferInfo {
  gint flags;
  gint offset;
  gint64 presentation_time_us;
  gint size;
};

extern GQuark gst_amc_codec_info_quark;

GstAmcCodec * gst_amc_codec_new (const gchar *name);
void gst_amc_codec_free (GstAmcCodec * codec);

gboolean gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format, gint flags);
GstAmcFormat * gst_amc_codec_get_output_format (GstAmcCodec * codec);

gboolean gst_amc_codec_start (GstAmcCodec * codec);
gboolean gst_amc_codec_stop (GstAmcCodec * codec);
gboolean gst_amc_codec_flush (GstAmcCodec * codec);
gboolean gst_amc_codec_release (GstAmcCodec * codec);

GstAmcBuffer * gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers);
GstAmcBuffer * gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers);
void gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers);

gint gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs);
gint gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec, GstAmcBufferInfo *info, gint64 timeoutUs);

gboolean gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index, const GstAmcBufferInfo *info);
gboolean gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index);


GstAmcFormat * gst_amc_format_new_audio (const gchar *mime, gint sample_rate, gint channels);
GstAmcFormat * gst_amc_format_new_video (const gchar *mime, gint width, gint height);
void gst_amc_format_free (GstAmcFormat * format);

gchar * gst_amc_format_to_string (GstAmcFormat * format);

gboolean gst_amc_format_contains_key (GstAmcFormat *format, const gchar *key);

gboolean gst_amc_format_get_float (GstAmcFormat *format, const gchar *key, gfloat *value);
void gst_amc_format_set_float (GstAmcFormat *format, const gchar *key, gfloat value);
gboolean gst_amc_format_get_int (GstAmcFormat *format, const gchar *key, gint *value);
void gst_amc_format_set_int (GstAmcFormat *format, const gchar *key, gint value);
gboolean gst_amc_format_get_string (GstAmcFormat *format, const gchar *key, gchar **value);
void gst_amc_format_set_string (GstAmcFormat *format, const gchar *key, const gchar *value);
gboolean gst_amc_format_get_buffer (GstAmcFormat *format, const gchar *key, guint8 **data, gsize *size);
void gst_amc_format_set_buffer (GstAmcFormat *format, const gchar *key, guint8 *data, gsize size);

GstVideoFormat gst_amc_color_format_to_video_format (gint color_format);
gint gst_amc_video_format_to_color_format (GstVideoFormat video_format);

const gchar * gst_amc_avc_profile_to_string (gint profile, const gchar **alternative);
gint gst_amc_avc_profile_from_string (const gchar *profile);
const gchar * gst_amc_avc_level_to_string (gint level);
gint gst_amc_avc_level_from_string (const gchar *level);
gint gst_amc_h263_profile_to_gst_id (gint profile);
gint gst_amc_h263_profile_from_gst_id (gint profile);
gint gst_amc_h263_level_to_gst_id (gint level);
gint gst_amc_h263_level_from_gst_id (gint level);
const gchar * gst_amc_mpeg4_profile_to_string (gint profile);
gint gst_amc_avc_mpeg4_profile_from_string (const gchar *profile);
const gchar * gst_amc_mpeg4_level_to_string (gint level);
gint gst_amc_mpeg4_level_from_string (const gchar *level);
const gchar * gst_amc_aac_profile_to_string (gint profile);
gint gst_amc_aac_profile_from_string (const gchar *profile);

gboolean gst_amc_audio_channel_mask_to_positions (guint32 channel_mask, gint channels, GstAudioChannelPosition *pos);
guint32 gst_amc_audio_channel_mask_from_positions (GstAudioChannelPosition *positions, gint channels);

G_END_DECLS

#endif /* __GST_AMC_H__ */
