/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
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
#include <jni.h>

G_BEGIN_DECLS

typedef struct _GstAmcCodecInfo GstAmcCodecInfo;
typedef struct _GstAmcCodecType GstAmcCodecType;
typedef struct _GstAmcCodec GstAmcCodec;
typedef struct _GstAmcCodecBufferInfo GstAmcBufferInfo;
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
  guint8 *data;
  gsize len;
};

struct _GstAmcFormat {
  /* < private > */
  jobject format; /* global reference */
  jclass format_class; /* global reference */
};

struct _GstAmcCodec {
  /* < private > */
  jobject codec; /* global reference */
  jclass codec_class; /* global reference */
};

struct _GstAmcBufferInfo {
  gint flags;
  gint offset;
  gint64 presentationTimeUs;
  gint size;
};



GstAmcCodec * gst_amc_codec_new (const gchar *name);
void gst_amc_codec_free (GstAmcCodec * codec);

void gst_amc_codec_configure (GstAmcCodec * codec, gint flags);
GstAmcFormat * gst_amc_codec_get_output_format (GstAmcCodec * codec);

void gst_amc_codec_start (GstAmcCodec * codec);
void gst_amc_codec_stop (GstAmcCodec * codec);
void gst_amc_codec_flush (GstAmcCodec * codec);

GstAmcBuffer * gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers);
GstAmcBuffer * gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers);

gint gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs);
gint gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec, GstAmcBufferInfo *info, gint64 timeoutUs);

void gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index, const GstAmcBufferInfo *info);
void gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index);


GstAmcFormat * gst_amc_format_new_audio (const gchar *mime, gint sample_rate, gint channels);
GstAmcFormat * gst_amc_format_new_video (const gchar *mime, gint width, gint height);
void gst_amc_format_free (GstAmcFormat * format);

gboolean gst_amc_format_contains_key (GstAmcFormat *format, const gchar *key);

gboolean gst_amc_format_get_float (GstAmcFormat *format, const gchar *key, gfloat *value);
void gst_amc_format_set_float (GstAmcFormat *format, const gchar *key, gfloat *value);
gboolean gst_amc_format_get_int (GstAmcFormat *format, const gchar *key, gint *value);
void gst_amc_format_set_int (GstAmcFormat *format, const gchar *key, gint *value);
gboolean gst_amc_format_get_string (GstAmcFormat *format, const gchar *key, gchar **value);
void gst_amc_format_set_string (GstAmcFormat *format, const gchar *key, const gchar *value);

G_END_DECLS

#endif /* __GST_AMC_H__ */
