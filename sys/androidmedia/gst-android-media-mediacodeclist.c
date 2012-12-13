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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/dvm/gst-dvm.h>

#include "gst-android-media-mediacodeclist.h"

static struct
{
  jclass klass;
  jmethodID getCodecCount;
  jmethodID getCodecInfoAt;
} android_media_mediacodeclist;


static gboolean
_init_classes (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  /* android.media.MediaCodecList */
  GST_DVM_GET_CLASS (android_media_mediacodeclist,
      "android/media/MediaCodecList");
  GST_DVM_GET_STATIC_METHOD (android_media_mediacodeclist, getCodecCount,
      "()I;");
  GST_DVM_GET_STATIC_METHOD (android_media_mediacodeclist, getCodecInfoAt,
      "(I)Landroid/media/MediaCodecInfo;");

  return TRUE;
}

gboolean
gst_android_media_mediacodeclist_init (void)
{
  if (!_init_classes ()) {
    gst_android_media_mediacodeclist_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_media_mediacodeclist_deinit (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  if (android_media_mediacodeclist.klass)
    (*env)->DeleteGlobalRef (env, android_media_mediacodeclist.klass);
  android_media_mediacodeclist.klass = NULL;
}

/* android.media.MediaFormat */
#define AMMCL_STATIC_CALL(error_statement, type, method, ...)            \
  GST_DVM_STATIC_CALL (error_statement, type,                           \
      android_media_mediacodeclist, method, ## __VA_ARGS__);

gint
gst_am_mediacodeclist_get_codec_count (void)
{
  JNIEnv *env = gst_dvm_get_env ();
  gint count = 0;

  count = AMMCL_STATIC_CALL (goto done, Int, getCodecCount);

done:

  return count;
}

GstAmMediaCodecInfo *
gst_am_mediacodeclist_get_codec_info_at (gint index)
{
  JNIEnv *env = gst_dvm_get_env ();
  GstAmMediaCodecInfo *info = NULL;
  jobject object = NULL;

  object = AMMCL_STATIC_CALL (goto done, Object, getCodecInfoAt, index);
  if (object) {
    info = g_slice_new0 (GstAmMediaCodecInfo);
    info->object = (*env)->NewGlobalRef (env, object);
    (*env)->DeleteLocalRef (env, object);
    if (!info->object) {
      GST_ERROR ("Failed to create global reference");
      (*env)->ExceptionClear (env);
      g_slice_free (GstAmMediaCodecInfo, info);
      info = NULL;
    }
  }

done:

  return info;
}
