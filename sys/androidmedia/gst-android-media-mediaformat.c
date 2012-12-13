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
#include <string.h>

#include "gst-android-media-mediaformat.h"

static struct
{
  jclass klass;
  jmethodID constructor;
  jmethodID containsKey;
  jmethodID createAudioFormat;
  jmethodID createVideoFormat;
  jmethodID getByteBuffer;
  jmethodID getFloat;
  jmethodID getInteger;
  jmethodID getLong;
  jmethodID getString;
  jmethodID setByteBuffer;
  jmethodID setFloat;
  jmethodID setInteger;
  jmethodID setLong;
  jmethodID setString;
  jmethodID toString;
} android_media_mediaformat;


static gboolean
_init_classes (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  /* android.media.MediaFormat */
  GST_DVM_GET_CLASS (android_media_mediaformat, "android/media/MediaFormat");
  GST_DVM_GET_CONSTRUCTOR (android_media_mediaformat, constructor, "()V");
  GST_DVM_GET_STATIC_METHOD (android_media_mediaformat, createAudioFormat,
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  GST_DVM_GET_STATIC_METHOD (android_media_mediaformat, createVideoFormat,
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  GST_DVM_GET_METHOD (android_media_mediaformat, toString,
      "()Ljava/lang/String;");
  GST_DVM_GET_METHOD (android_media_mediaformat, containsKey,
      "(Ljava/lang/String;)Z");
  GST_DVM_GET_METHOD (android_media_mediaformat, getFloat,
      "(Ljava/lang/String;)F");
  GST_DVM_GET_METHOD (android_media_mediaformat, setFloat,
      "(Ljava/lang/String;F)V");
  GST_DVM_GET_METHOD (android_media_mediaformat, getInteger,
      "(Ljava/lang/String;)I");
  GST_DVM_GET_METHOD (android_media_mediaformat, setInteger,
      "(Ljava/lang/String;I)V");
  GST_DVM_GET_METHOD (android_media_mediaformat, getLong,
      "(Ljava/lang/String;)J");
  GST_DVM_GET_METHOD (android_media_mediaformat, setLong,
      "(Ljava/lang/String;J)V");
  GST_DVM_GET_METHOD (android_media_mediaformat, getString,
      "(Ljava/lang/String;)Ljava/lang/String;");
  GST_DVM_GET_METHOD (android_media_mediaformat, setString,
      "(Ljava/lang/String;Ljava/lang/String;)V");
  GST_DVM_GET_METHOD (android_media_mediaformat, getByteBuffer,
      "(Ljava/lang/String;)Ljava/nio/ByteBuffer;");
  GST_DVM_GET_METHOD (android_media_mediaformat, setByteBuffer,
      "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");

  return TRUE;
}

gboolean
gst_android_media_mediaformat_init (void)
{
  if (!_init_classes ()) {
    gst_android_media_mediaformat_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_media_mediaformat_deinit (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  if (android_media_mediaformat.klass)
    (*env)->DeleteGlobalRef (env, android_media_mediaformat.klass);
  android_media_mediaformat.klass = NULL;
}

/* android.media.MediaFormat */
#define AMMF_CALL(error_statement, type, method, ...)                   \
  GST_DVM_CALL (error_statement, self->object, type,                    \
      android_media_mediaformat, method, ## __VA_ARGS__);
#define AMMF_STATIC_CALL(error_statement, type, method, ...)            \
  GST_DVM_STATIC_CALL (error_statement, type,                           \
      android_media_mediaformat, method, ## __VA_ARGS__);

GstAmMediaFormat *
gst_am_mediaformat_new (void)
{
  JNIEnv *env = gst_dvm_get_env ();
  GstAmMediaFormat *format = NULL;
  jobject object = NULL;

  object = (*env)->NewObject (env, android_media_mediaformat.klass,
      android_media_mediaformat.constructor);
  if ((*env)->ExceptionCheck (env) || !object) {
    GST_ERROR ("Failed to create callback object");
    (*env)->ExceptionClear (env);
    return NULL;
  }

  format = g_slice_new0 (GstAmMediaFormat);
  format->object = (*env)->NewGlobalRef (env, object);
  (*env)->DeleteLocalRef (env, object);
  if (!format->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAmMediaFormat, format);
    return NULL;
  }

  return format;
}

GstAmMediaFormat *
gst_am_mediaformat_create_audio_format (const gchar * mime,
    gint sample_rate, gint channels)
{
  JNIEnv *env = gst_dvm_get_env ();
  GstAmMediaFormat *format = NULL;
  jstring mime_str;
  jobject object = NULL;

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto done;

  object = AMMF_STATIC_CALL (goto done, Object, createAudioFormat, mime_str,
      sample_rate, channels);
  if (object) {
    format = g_slice_new0 (GstAmMediaFormat);
    format->object = (*env)->NewGlobalRef (env, object);
    (*env)->DeleteLocalRef (env, object);
    if (!format->object) {
      GST_ERROR ("Failed to create global reference");
      (*env)->ExceptionClear (env);
      g_slice_free (GstAmMediaFormat, format);
      format = NULL;
    }
  }

done:
  if (mime_str)
    (*env)->DeleteLocalRef (env, mime_str);

  return format;
}

GstAmMediaFormat *
gst_am_mediaformat_create_video_format (const gchar * mime,
    gint width, gint height)
{
  JNIEnv *env = gst_dvm_get_env ();
  GstAmMediaFormat *format = NULL;
  jstring mime_str;
  jobject object = NULL;

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto done;

  object = AMMF_STATIC_CALL (goto done, Object, createVideoFormat, mime_str,
      width, height);
  if (object) {
    format = g_slice_new0 (GstAmMediaFormat);
    format->object = (*env)->NewGlobalRef (env, object);
    (*env)->DeleteLocalRef (env, object);
    if (!format->object) {
      GST_ERROR ("Failed to create global reference");
      (*env)->ExceptionClear (env);
      g_slice_free (GstAmMediaFormat, format);
      format = NULL;
    }
  }

done:
  if (mime_str)
    (*env)->DeleteLocalRef (env, mime_str);

  return format;
}

void
gst_am_mediaformat_free (GstAmMediaFormat * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAmMediaFormat, self);
}

gchar *
gst_am_mediaformat_to_string (GstAmMediaFormat * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring v_str = NULL;
  const gchar *v = NULL;
  gchar *ret = NULL;

  v_str = AMMF_CALL (return NULL, Object, toString);
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

gboolean
gst_am_mediaformat_contains_key (GstAmMediaFormat * self, const gchar * key)
{
  JNIEnv *env = gst_dvm_get_env ();
  gboolean ret = FALSE;
  jstring key_str = NULL;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  ret = AMMF_CALL (ret = FALSE; goto done, Boolean, containsKey, key_str);

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

gboolean
gst_am_mediaformat_get_float (GstAmMediaFormat * self, const gchar * key,
    gfloat * value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  gboolean ret = FALSE;

  *value = 0;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  *value = AMMF_CALL (goto done, Float, getFloat, key_str);
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

gboolean
gst_am_mediaformat_set_float (GstAmMediaFormat * self, const gchar * key,
    gfloat value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  gboolean ret = FALSE;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  AMMF_CALL (goto done, Void, setFloat, key_str, value);
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

gboolean
gst_am_mediaformat_get_int (GstAmMediaFormat * self, const gchar * key,
    gint * value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  gboolean ret = FALSE;

  *value = 0;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  *value = AMMF_CALL (goto done, Int, getInteger, key_str);
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;

}

gboolean
gst_am_mediaformat_set_int (GstAmMediaFormat * self, const gchar * key,
    gint value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  gboolean ret = FALSE;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  AMMF_CALL (goto done, Void, setInteger, key_str, value);
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

gboolean
gst_am_mediaformat_get_long (GstAmMediaFormat * self, const gchar * key,
    glong * value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  gboolean ret = FALSE;
  jlong long_value;

  *value = 0;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  long_value = AMMF_CALL (goto done, Long, getLong, key_str);
  *value = long_value;
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;

}

gboolean
gst_am_mediaformat_set_long (GstAmMediaFormat * self, const gchar * key,
    glong value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  gboolean ret = FALSE;
  jlong long_value = value;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  AMMF_CALL (goto done, Void, setLong, key_str, long_value);
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

gboolean
gst_am_mediaformat_get_string (GstAmMediaFormat * self, const gchar * key,
    gchar ** value)
{
  JNIEnv *env = gst_dvm_get_env ();
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jstring v_str = NULL;
  const gchar *v = NULL;

  *value = 0;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  v_str = AMMF_CALL (goto done, Object, getString, key_str);

  v = (*env)->GetStringUTFChars (env, v_str, NULL);
  if (!v) {
    GST_ERROR ("Failed to convert string to UTF8");
    (*env)->ExceptionClear (env);
    goto done;
  }

  *value = g_strdup (v);

  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->ReleaseStringUTFChars (env, v_str, v);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);

  return ret;
}

gboolean
gst_am_mediaformat_set_string (GstAmMediaFormat * self, const gchar * key,
    const gchar * value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  jstring v_str = NULL;
  gboolean ret = FALSE;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  v_str = (*env)->NewStringUTF (env, value);
  if (!v_str)
    goto done;

  AMMF_CALL (goto done, Void, setString, key_str, v_str);
  ret = TRUE;
done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);

  return ret;
}

gboolean
gst_am_mediaformat_get_buffer (GstAmMediaFormat * self, const gchar * key,
    GstBuffer ** value)
{
  JNIEnv *env = gst_dvm_get_env ();
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jobject v = NULL;
  guint8 *data;
  gsize size;

  *value = 0;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  v = AMMF_CALL (goto done, Object, getByteBuffer, key_str);

  data = (*env)->GetDirectBufferAddress (env, v);
  if (!data) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer address");
    goto done;
  }
  size = (*env)->GetDirectBufferCapacity (env, v);
  *value = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (*value), data, size);

  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->DeleteLocalRef (env, v);

  return ret;
}

gboolean
gst_am_mediaformat_set_buffer (GstAmMediaFormat * self, const gchar * key,
    GstBuffer * value)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring key_str = NULL;
  jobject v = NULL;
  gboolean ret = FALSE;

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  /* FIXME: The buffer must remain valid until the codec is stopped */
  v = (*env)->NewDirectByteBuffer (env, GST_BUFFER_DATA (value),
      GST_BUFFER_SIZE (value));
  if (!v)
    goto done;

  AMMF_CALL (goto done, Void, setByteBuffer, key_str, v);
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->DeleteLocalRef (env, v);

  return ret;
}
