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

#include <gst/dvm/gstdvm.h>

#include "gst-android-media-mediacodecinfo.h"

static struct
{
  jclass klass;
  jmethodID getCapabilitiesForType;
  jmethodID getName;
  jmethodID getSupportedTypes;
  jmethodID isEncoder;
} android_media_mediacodecinfo;

static struct
{
  jclass klass;
  jfieldID colorFormats;
  jfieldID profileLevels;
} android_media_mediacodeccapabilities;

static struct
{
  jclass klass;
  jfieldID level;
  jfieldID profile;
} android_media_mediacodecprofilelevel;

static struct
{
  jclass klass;

  jint CHANNEL_OUT_FRONT_LEFT;
  jint CHANNEL_OUT_FRONT_RIGHT;
  jint CHANNEL_OUT_FRONT_CENTER;
  jint CHANNEL_OUT_LOW_FREQUENCY;
  jint CHANNEL_OUT_BACK_LEFT;
  jint CHANNEL_OUT_BACK_RIGHT;
  jint CHANNEL_OUT_FRONT_LEFT_OF_CENTER;
  jint CHANNEL_OUT_FRONT_RIGHT_OF_CENTER;
  jint CHANNEL_OUT_BACK_CENTER;
  jint CHANNEL_OUT_SIDE_LEFT;
  jint CHANNEL_OUT_SIDE_RIGHT;
  jint CHANNEL_OUT_TOP_CENTER;
  jint CHANNEL_OUT_TOP_FRONT_LEFT;
  jint CHANNEL_OUT_TOP_FRONT_CENTER;
  jint CHANNEL_OUT_TOP_FRONT_RIGHT;
  jint CHANNEL_OUT_TOP_BACK_LEFT;
  jint CHANNEL_OUT_TOP_BACK_CENTER;
  jint CHANNEL_OUT_TOP_BACK_RIGHT;
} android_media_audioformat;

gint AudioFormat_CHANNEL_OUT_FRONT_LEFT;
gint AudioFormat_CHANNEL_OUT_FRONT_RIGHT;
gint AudioFormat_CHANNEL_OUT_FRONT_CENTER;
gint AudioFormat_CHANNEL_OUT_LOW_FREQUENCY;
gint AudioFormat_CHANNEL_OUT_BACK_LEFT;
gint AudioFormat_CHANNEL_OUT_BACK_RIGHT;
gint AudioFormat_CHANNEL_OUT_FRONT_LEFT_OF_CENTER;
gint AudioFormat_CHANNEL_OUT_FRONT_RIGHT_OF_CENTER;
gint AudioFormat_CHANNEL_OUT_BACK_CENTER;
gint AudioFormat_CHANNEL_OUT_SIDE_LEFT;
gint AudioFormat_CHANNEL_OUT_SIDE_RIGHT;
gint AudioFormat_CHANNEL_OUT_TOP_CENTER;
gint AudioFormat_CHANNEL_OUT_TOP_FRONT_LEFT;
gint AudioFormat_CHANNEL_OUT_TOP_FRONT_CENTER;
gint AudioFormat_CHANNEL_OUT_TOP_FRONT_RIGHT;
gint AudioFormat_CHANNEL_OUT_TOP_BACK_LEFT;
gint AudioFormat_CHANNEL_OUT_TOP_BACK_CENTER;
gint AudioFormat_CHANNEL_OUT_TOP_BACK_RIGHT;


static gboolean
_init_classes (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  /* android.media.MediaCodecInfo */
  GST_DVM_GET_CLASS (android_media_mediacodecinfo,
      "android/media/MediaCodecInfo");
  GST_DVM_GET_METHOD (android_media_mediacodecinfo, getCapabilitiesForType,
      "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
  GST_DVM_GET_METHOD (android_media_mediacodecinfo, getName,
      "()Ljava/lang/String;");
  GST_DVM_GET_METHOD (android_media_mediacodecinfo, getSupportedTypes,
      "()[java/lang/String;");
  GST_DVM_GET_METHOD (android_media_mediacodecinfo, isEncoder, "()Z");

  GST_DVM_GET_CLASS (android_media_mediacodeccapabilities,
      "android/media/MediaCodecInfo$CodecCapabilities");
  GST_DVM_GET_FIELD (android_media_mediacodeccapabilities, colorFormats, "[I");
  GST_DVM_GET_FIELD (android_media_mediacodeccapabilities, profileLevels,
      "[Landroid/media/MediaCodecInfo$CodecProfileLevel;");

  GST_DVM_GET_CLASS (android_media_mediacodecprofilelevel,
      "android/media/MediaCodecInfo$ProfileLevel");
  GST_DVM_GET_FIELD (android_media_mediacodecprofilelevel, level, "I");
  GST_DVM_GET_FIELD (android_media_mediacodecprofilelevel, profile, "I");

  GST_DVM_GET_CLASS (android_media_audioformat, "android/media/AudioFormat");

  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_FRONT_LEFT, Int,
      "I");
  AudioFormat_CHANNEL_OUT_FRONT_LEFT =
      android_media_audioformat.CHANNEL_OUT_FRONT_LEFT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_FRONT_RIGHT, Int,
      "I");
  AudioFormat_CHANNEL_OUT_FRONT_RIGHT =
      android_media_audioformat.CHANNEL_OUT_FRONT_RIGHT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_FRONT_CENTER,
      Int, "I");
  AudioFormat_CHANNEL_OUT_FRONT_CENTER =
      android_media_audioformat.CHANNEL_OUT_FRONT_CENTER;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_LOW_FREQUENCY,
      Int, "I");
  AudioFormat_CHANNEL_OUT_LOW_FREQUENCY =
      android_media_audioformat.CHANNEL_OUT_LOW_FREQUENCY;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_BACK_LEFT, Int,
      "I");
  AudioFormat_CHANNEL_OUT_BACK_LEFT =
      android_media_audioformat.CHANNEL_OUT_BACK_LEFT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_BACK_RIGHT, Int,
      "I");
  AudioFormat_CHANNEL_OUT_BACK_RIGHT =
      android_media_audioformat.CHANNEL_OUT_BACK_RIGHT;
  GST_DVM_GET_CONSTANT (android_media_audioformat,
      CHANNEL_OUT_FRONT_LEFT_OF_CENTER, Int, "I");
  AudioFormat_CHANNEL_OUT_FRONT_LEFT_OF_CENTER =
      android_media_audioformat.CHANNEL_OUT_FRONT_LEFT_OF_CENTER;
  GST_DVM_GET_CONSTANT (android_media_audioformat,
      CHANNEL_OUT_FRONT_RIGHT_OF_CENTER, Int, "I");
  AudioFormat_CHANNEL_OUT_FRONT_RIGHT_OF_CENTER =
      android_media_audioformat.CHANNEL_OUT_FRONT_RIGHT_OF_CENTER;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_BACK_CENTER, Int,
      "I");
  AudioFormat_CHANNEL_OUT_BACK_CENTER =
      android_media_audioformat.CHANNEL_OUT_BACK_CENTER;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_SIDE_LEFT, Int,
      "I");
  AudioFormat_CHANNEL_OUT_SIDE_LEFT =
      android_media_audioformat.CHANNEL_OUT_SIDE_LEFT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_SIDE_RIGHT, Int,
      "I");
  AudioFormat_CHANNEL_OUT_SIDE_RIGHT =
      android_media_audioformat.CHANNEL_OUT_SIDE_RIGHT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_TOP_CENTER, Int,
      "I");
  AudioFormat_CHANNEL_OUT_TOP_CENTER =
      android_media_audioformat.CHANNEL_OUT_TOP_CENTER;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_TOP_FRONT_LEFT,
      Int, "I");
  AudioFormat_CHANNEL_OUT_TOP_FRONT_LEFT =
      android_media_audioformat.CHANNEL_OUT_TOP_FRONT_LEFT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_TOP_FRONT_CENTER,
      Int, "I");
  AudioFormat_CHANNEL_OUT_TOP_FRONT_CENTER =
      android_media_audioformat.CHANNEL_OUT_TOP_FRONT_CENTER;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_TOP_FRONT_RIGHT,
      Int, "I");
  AudioFormat_CHANNEL_OUT_TOP_FRONT_RIGHT =
      android_media_audioformat.CHANNEL_OUT_TOP_FRONT_RIGHT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_TOP_BACK_LEFT,
      Int, "I");
  AudioFormat_CHANNEL_OUT_TOP_BACK_LEFT =
      android_media_audioformat.CHANNEL_OUT_TOP_BACK_LEFT;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_TOP_BACK_CENTER,
      Int, "I");
  AudioFormat_CHANNEL_OUT_TOP_BACK_CENTER =
      android_media_audioformat.CHANNEL_OUT_TOP_BACK_CENTER;
  GST_DVM_GET_CONSTANT (android_media_audioformat, CHANNEL_OUT_TOP_BACK_RIGHT,
      Int, "I");
  AudioFormat_CHANNEL_OUT_TOP_BACK_RIGHT =
      android_media_audioformat.CHANNEL_OUT_TOP_BACK_RIGHT;

  return TRUE;
}

gboolean
gst_android_media_mediacodecinfo_init (void)
{
  if (!_init_classes ()) {
    gst_android_media_mediacodecinfo_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_media_mediacodecinfo_deinit (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  if (android_media_mediacodecinfo.klass)
    (*env)->DeleteGlobalRef (env, android_media_mediacodecinfo.klass);
  android_media_mediacodecinfo.klass = NULL;

  if (android_media_mediacodeccapabilities.klass)
    (*env)->DeleteGlobalRef (env, android_media_mediacodeccapabilities.klass);
  android_media_mediacodeccapabilities.klass = NULL;

  if (android_media_mediacodecprofilelevel.klass)
    (*env)->DeleteGlobalRef (env, android_media_mediacodecprofilelevel.klass);
  android_media_mediacodecprofilelevel.klass = NULL;

  if (android_media_audioformat.klass)
    (*env)->DeleteGlobalRef (env, android_media_audioformat.klass);
  android_media_audioformat.klass = NULL;
}

/* android.media.MediaCodecInfo */
#define AMMCI_CALL(error_statement, type, method, ...)                  \
  GST_DVM_CALL (error_statement, self->object, type,                    \
      android_media_mediacodecinfo, method, ## __VA_ARGS__);

void
gst_am_mediacodecinfo_free (GstAmMediaCodecInfo * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAmMediaCodecInfo, self);
}

void
gst_am_mediacodeccapabilities_free (GstAmMediaCodecCapabilities * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAmMediaCodecCapabilities, self);
}

void
gst_am_mediacodecprofilelevel_free (GstAmMediaCodecProfileLevel * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAmMediaCodecProfileLevel, self);
}


GstAmMediaCodecCapabilities *
gst_am_mediacodecinfo_get_capabilities_for_type (GstAmMediaCodecInfo * self,
    const gchar * type)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object = NULL;
  jstring type_str = NULL;
  GstAmMediaCodecCapabilities *caps = NULL;

  type_str = (*env)->NewStringUTF (env, type);
  if (!type_str)
    goto done;

  object = AMMCI_CALL (goto done, Object, getCapabilitiesForType, type_str);

  if (object) {
    caps = g_slice_new0 (GstAmMediaCodecCapabilities);
    caps->object = (*env)->NewGlobalRef (env, object);
    (*env)->DeleteLocalRef (env, object);
    if (!caps->object) {
      GST_ERROR ("Failed to create global reference");
      (*env)->ExceptionClear (env);
      g_slice_free (GstAmMediaCodecCapabilities, caps);
      caps = NULL;
    }
  }

done:
  if (type_str)
    (*env)->DeleteLocalRef (env, type_str);

  return caps;
}

gchar *
gst_am_mediacodecinfo_get_name (GstAmMediaCodecInfo * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring v_str = NULL;
  const gchar *v = NULL;
  gchar *ret = NULL;

  v_str = AMMCI_CALL (return NULL, Object, getName);
  if (v_str) {
    v = (*env)->GetStringUTFChars (env, v_str, NULL);
    if (!v) {
      GST_ERROR ("Failed to convert string to UTF8");
      (*env)->ExceptionClear (env);
      goto done;
    }
    ret = g_strdup (v);
  }

done:
  if (v)
    (*env)->ReleaseStringUTFChars (env, v_str, v);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);

  return ret;
}

GList *
gst_am_mediacodecinfo_get_supported_types (GstAmMediaCodecInfo * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jarray arr = NULL;
  jint arr_len = 0;
  GList *ret = NULL;
  gint i;

  arr = AMMCI_CALL (goto done, Object, getSupportedTypes);
  if (!arr)
    goto done;
  arr_len = (*env)->GetArrayLength (env, arr);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get array length");
    goto done;
  }

  for (i = 0; i < arr_len; i++) {
    jstring str = NULL;
    const gchar *str_v = NULL;

    str = (*env)->GetObjectArrayElement (env, arr, i);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get array element %d", i);
      continue;
    }
    if (!str)
      continue;

    str_v = (*env)->GetStringUTFChars (env, str, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get string characters");
      (*env)->DeleteLocalRef (env, str);
      str = NULL;
      continue;
    }
    ret = g_list_append (ret, g_strdup (str_v));
    (*env)->ReleaseStringUTFChars (env, str, str_v);
    str_v = NULL;
    (*env)->DeleteLocalRef (env, str);
    str = NULL;
  }

done:
  if (arr)
    (*env)->DeleteLocalRef (env, arr);

  return ret;
}

gboolean
gst_am_mediacodecinfo_is_encoder (GstAmMediaCodecInfo * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  gboolean ret = FALSE;

  ret = AMMCI_CALL (return FALSE, Boolean, isEncoder);

  return ret;
}

#define AMMCC_FIELD(error_statement, type, field)                        \
  GST_DVM_FIELD (error_statement, self->object, type,                    \
      android_media_mediacodeccapabilities, field);

GList *
gst_am_mediacodeccapabilities_get_color_formats (GstAmMediaCodecCapabilities *
    self)
{
  JNIEnv *env = gst_dvm_get_env ();
  GList *ret = NULL;
  jarray arr = NULL;
  jint arr_len = 0;
  jint *arr_n = NULL;
  gint i;

  arr = AMMCC_FIELD (goto done, Object, colorFormats);
  arr_len = (*env)->GetArrayLength (env, arr);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get array length");
    goto done;
  }

  arr_n = (*env)->GetIntArrayElements (env, arr, NULL);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get array elements");
    goto done;
  }

  for (i = 0; i < arr_len; i++)
    ret = g_list_append (ret, GINT_TO_POINTER (arr_n[i]));

done:
  if (arr_n)
    (*env)->ReleaseIntArrayElements (env, arr, arr_n, JNI_ABORT);
  if (arr)
    (*env)->DeleteLocalRef (env, arr);

  return ret;
}

GList *
gst_am_mediacodeccapabilities_get_profile_levels (GstAmMediaCodecCapabilities *
    self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jarray arr = NULL;
  jint arr_len = 0;
  GList *ret = NULL;
  gint i;

  arr = AMMCC_FIELD (goto done, Object, profileLevels);
  if (!arr)
    goto done;
  arr_len = (*env)->GetArrayLength (env, arr);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get array length");
    goto done;
  }

  for (i = 0; i < arr_len; i++) {
    jobject object = NULL;

    object = (*env)->GetObjectArrayElement (env, arr, i);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get array element %d", i);
      continue;
    }
    if (!object)
      continue;

    if (object) {
      GstAmMediaCodecProfileLevel *profile_level =
          g_slice_new0 (GstAmMediaCodecProfileLevel);

      profile_level->object = (*env)->NewGlobalRef (env, object);
      (*env)->DeleteLocalRef (env, object);
      object = NULL;
      if (!profile_level->object) {
        GST_ERROR ("Failed to create global reference");
        (*env)->ExceptionClear (env);
        g_slice_free (GstAmMediaCodecProfileLevel, profile_level);
      } else {
        ret = g_list_append (ret, profile_level);
      }
    }
  }

done:
  if (arr)
    (*env)->DeleteLocalRef (env, arr);

  return ret;
}

#define AMMCPL_FIELD(error_statement, type, field)                       \
  GST_DVM_FIELD (error_statement, self->object, type,                    \
      android_media_mediacodecprofilelevel, field);

gint
gst_am_mediacodecprofilelevel_get_level (GstAmMediaCodecProfileLevel * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  gint ret;

  ret = AMMCPL_FIELD (return -1, Int, level);

  return ret;
}

gint
gst_am_mediacodecprofilelevel_get_profile (GstAmMediaCodecProfileLevel * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  gint ret;

  ret = AMMCPL_FIELD (return -1, Int, profile);

  return ret;
}
