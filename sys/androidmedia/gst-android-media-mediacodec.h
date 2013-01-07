/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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

#ifndef __GST_ANDROID_MEDIA_MEDIACODEC_H__
#define __GST_ANDROID_MEDIA_MEDIACODEC_H__

#include <gst/gst.h>
#include <jni.h>

#include "gst-android-media-mediaformat.h"

G_BEGIN_DECLS

typedef struct _GstAmMediaCodecBuffer GstAmMediaCodecBuffer;
typedef struct _GstAmMediaCodecBufferInfo GstAmMediaCodecBufferInfo;
typedef struct _GstAmMediaCodec GstAmMediaCodec;

struct _GstAmMediaCodecBuffer {
  guint8 *data;
  gsize size;
  /*< private >*/
  jobject object; /* global reference */
};

struct _GstAmMediaCodecBufferInfo {
  gint flags;
  gint offset;
  gint64 presentation_time_us;
  gint size;
};

struct _GstAmMediaCodec {
  /*< private >*/
  jobject object; /* global reference */
};

extern gint MediaCodec_BUFFER_FLAG_SYNC_FRAME;
extern gint MediaCodec_BUFFER_FLAG_CODEC_CONFIG;
extern gint MediaCodec_BUFFER_FLAG_END_OF_STREAM;

extern gint MediaCodec_CONFIGURE_FLAG_ENCODE;

extern gint MediaCodec_INFO_TRY_AGAIN_LATER;
extern gint MediaCodec_INFO_OUTPUT_FORMAT_CHANGED;
extern gint MediaCodec_INFO_OUTPUT_BUFFERS_CHANGED;

gboolean gst_android_media_mediacodec_init (void);
void gst_android_media_mediacodec_deinit (void);

gboolean gst_am_mediacodec_configure (GstAmMediaCodec * self,
    GstAmMediaFormat * format, gint flags);

GstAmMediaCodec * gst_am_mediacodec_create_by_codec_name (const gchar *name);
GstAmMediaCodec * gst_am_mediacodec_create_decoder_by_type (const gchar *type);
GstAmMediaCodec * gst_am_mediacodec_create_encoder_by_type (const gchar *type);

void gst_am_mediacodec_free (GstAmMediaCodec * self);

gint gst_am_mediacodec_dequeue_input_buffer (GstAmMediaCodec * self,
    gint64 timeoutUs);
gint gst_am_mediacodec_dequeue_output_buffer (GstAmMediaCodec * self,
    GstAmMediaCodecBufferInfo *info, gint64 timeoutUs);
gboolean gst_am_mediacodec_flush (GstAmMediaCodec * self);

GstAmMediaCodecBuffer * gst_am_mediacodec_get_input_buffers (GstAmMediaCodec * self,
    gsize * n_buffers);
GstAmMediaCodecBuffer * gst_am_mediacodec_get_output_buffers (GstAmMediaCodec * self,
    gsize * n_buffers);
void gst_am_mediacodec_free_buffers (GstAmMediaCodecBuffer * buffers, gsize n_buffers);
GstAmMediaFormat * gst_am_mediacodec_get_output_format (GstAmMediaCodec * self);

gboolean gst_am_mediacodec_queue_input_buffer (GstAmMediaCodec * self,
    gint index, const GstAmMediaCodecBufferInfo *info);
void gst_am_mediacodec_release (GstAmMediaCodec * self);
gboolean gst_am_mediacodec_release_output_buffer (GstAmMediaCodec * self,
    gint index);

gboolean gst_am_mediacodec_start (GstAmMediaCodec * self);
gboolean gst_am_mediacodec_stop (GstAmMediaCodec * self);

G_END_DECLS

#endif /* __GST_ANDROID_MEDIA_MEDIACODEC_H__ */
