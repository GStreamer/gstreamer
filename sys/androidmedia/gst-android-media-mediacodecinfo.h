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

#ifndef __GST_ANDROID_MEDIA_MEDIACODECINFO_H__
#define __GST_ANDROID_MEDIA_MEDIACODECINFO_H__

#include <gst/gst.h>
#include <jni.h>

G_BEGIN_DECLS

typedef struct _GstAmMediaCodecInfo GstAmMediaCodecInfo;
typedef struct _GstAmMediaCodecCapabilities GstAmMediaCodecCapabilities;
typedef struct _GstAmMediaCodecProfileLevel GstAmMediaCodecProfileLevel;

struct _GstAmMediaCodecInfo {
  /*< private >*/
  jobject object; /* global reference */
};

struct _GstAmMediaCodecCapabilities {
  /*< private >*/
  jobject object; /* global reference */
};

struct _GstAmMediaCodecProfileLevel {
  /*< private >*/
  jobject object; /* global reference */
};

gboolean gst_android_media_mediacodecinfo_init (void);
void gst_android_media_mediacodecinfo_deinit (void);

void gst_am_mediacodecinfo_free (GstAmMediaCodecInfo * self);
void gst_am_mediacodeccapabilities_free (GstAmMediaCodecCapabilities * self);
void gst_am_mediacodecprofilelevel_free (GstAmMediaCodecProfileLevel * self);

GstAmMediaCodecCapabilities * gst_am_mediacodecinfo_get_capabilities_for_type (GstAmMediaCodecInfo * self, const gchar *type);
gchar * gst_am_mediacodecinfo_get_name (GstAmMediaCodecInfo * self);
/* GList <gchar *> */
GList * gst_am_mediacodecinfo_get_supported_types (GstAmMediaCodecInfo * self);
gboolean gst_am_mediacodecinfo_is_encoder (GstAmMediaCodecInfo * self);

/* GList <int> */
GList * gst_am_mediacodeccapabilities_get_color_formats (GstAmMediaCodecCapabilities *self);
/* GList <GstAmMediaCodecProfileLevel *> */
GList * gst_am_mediacodeccapabilities_get_profile_levels (GstAmMediaCodecCapabilities *self);

gint gst_am_mediacodecprofilelevel_get_level (GstAmMediaCodecProfileLevel *self);
gint gst_am_mediacodecprofilelevel_get_profile (GstAmMediaCodecProfileLevel *self);

extern gint AudioFormat_CHANNEL_OUT_FRONT_LEFT;
extern gint AudioFormat_CHANNEL_OUT_FRONT_RIGHT;
extern gint AudioFormat_CHANNEL_OUT_FRONT_CENTER;
extern gint AudioFormat_CHANNEL_OUT_LOW_FREQUENCY;
extern gint AudioFormat_CHANNEL_OUT_BACK_LEFT;
extern gint AudioFormat_CHANNEL_OUT_BACK_RIGHT;
extern gint AudioFormat_CHANNEL_OUT_FRONT_LEFT_OF_CENTER;
extern gint AudioFormat_CHANNEL_OUT_FRONT_RIGHT_OF_CENTER;
extern gint AudioFormat_CHANNEL_OUT_BACK_CENTER;
extern gint AudioFormat_CHANNEL_OUT_SIDE_LEFT;
extern gint AudioFormat_CHANNEL_OUT_SIDE_RIGHT;
extern gint AudioFormat_CHANNEL_OUT_TOP_CENTER;
extern gint AudioFormat_CHANNEL_OUT_TOP_FRONT_LEFT;
extern gint AudioFormat_CHANNEL_OUT_TOP_FRONT_CENTER;
extern gint AudioFormat_CHANNEL_OUT_TOP_FRONT_RIGHT;
extern gint AudioFormat_CHANNEL_OUT_TOP_BACK_LEFT;
extern gint AudioFormat_CHANNEL_OUT_TOP_BACK_CENTER;
extern gint AudioFormat_CHANNEL_OUT_TOP_BACK_RIGHT;

G_END_DECLS

#endif /* __GST_ANDROID_MEDIA_MEDIACODECINFO_H__ */
