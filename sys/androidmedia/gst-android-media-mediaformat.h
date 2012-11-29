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

#ifndef __GST_ANDROID_MEDIA_MEDIAFORMAT_H__
#define __GST_ANDROID_MEDIA_MEDIAFORMAT_H__

#include <gst/gst.h>
#include <jni.h>

typedef struct _GstAmMediaFormat GstAmMediaFormat;

struct _GstAmMediaFormat {
  /*< private >*/
  jobject object; /* global reference */
};

G_BEGIN_DECLS

gboolean gst_android_media_mediaformat_init (void);
void gst_android_media_mediaformat_deinit (void);

GstAmMediaFormat * gst_am_mediaformat_new (void);
GstAmMediaFormat * gst_am_mediaformat_create_audio_format (const gchar *mime,
    gint sample_rate, gint channels);
GstAmMediaFormat * gst_am_mediaformat_create_video_format (const gchar *mime,
    gint width, gint height);
void gst_am_mediaformat_free (GstAmMediaFormat * self);

gboolean gst_am_mediaformat_get_float (GstAmMediaFormat * self,
    const gchar *key, gfloat *value);
gboolean gst_am_mediaformat_set_float (GstAmMediaFormat * self,
    const gchar *key, gfloat value);
gboolean gst_am_mediaformat_get_int (GstAmMediaFormat * self,
    const gchar *key, gint *value);
gboolean gst_am_mediaformat_set_int (GstAmMediaFormat * self,
    const gchar *key, gint value);
gboolean gst_am_mediaformat_get_long (GstAmMediaFormat * self,
    const gchar *key, glong *value);
gboolean gst_am_mediaformat_set_long (GstAmMediaFormat * self,
    const gchar *key, glong value);
gboolean gst_am_mediaformat_get_string (GstAmMediaFormat * self,
    const gchar *key, gchar **value);
gboolean gst_am_mediaformat_set_string (GstAmMediaFormat * self,
    const gchar *key, const gchar *value);
gboolean gst_am_mediaformat_get_buffer (GstAmMediaFormat * self,
    const gchar *key, GstBuffer **value);
gboolean gst_am_mediaformat_set_buffer (GstAmMediaFormat * self,
    const gchar *key, GstBuffer *value);


gboolean gst_am_mediaformat_contains_key (GstAmMediaFormat * self,
    const gchar *key);
gchar * gst_am_mediaformat_to_string (GstAmMediaFormat * self);

G_END_DECLS

#endif /* __GST_ANDROID_MEDIA_MEDIAFORMAT_H__ */
