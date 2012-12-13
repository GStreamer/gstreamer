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

#ifndef __GST_ANDROID_MEDIA_MEDIACODECLIST_H__
#define __GST_ANDROID_MEDIA_MEDIACODECLIST_H__

#include <gst/gst.h>
#include <jni.h>

#include "gst-android-media-mediacodecinfo.h"

G_BEGIN_DECLS

gboolean gst_android_media_mediacodeclist_init (void);
void gst_android_media_mediacodeclist_deinit (void);

gint gst_am_mediacodeclist_get_codec_count (void);
GstAmMediaCodecInfo * gst_am_mediacodeclist_get_codec_info_at (int index);

G_END_DECLS

#endif /* __GST_ANDROID_MEDIA_MEDIACODECLIST_H__ */
