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

#include "gst-android-media-mediacodec.h"


static struct
{
  jclass klass;
  jmethodID constructor;
  jfieldID flags;
  jfieldID offset;
  jfieldID presentationTimeUs;
  jfieldID size;
} android_media_mediacodec_bufferinfo;

static struct
{
  jclass klass;
  jmethodID configure;
  jmethodID createByCodecName;
  jmethodID createDecoderByType;
  jmethodID createEncoderByType;
  jmethodID dequeueInputBuffer;
  jmethodID dequeueOutputBuffer;
  jmethodID flush;
  jmethodID getInputBuffers;
  jmethodID getOutputBuffers;
  jmethodID getOutputFormat;
  jmethodID queueInputBuffer;
  jmethodID release;
  jmethodID releaseOutputBuffer;
  jmethodID start;
  jmethodID stop;
  jint BUFFER_FLAG_SYNC_FRAME;
  jint BUFFER_FLAG_CODEC_CONFIG;
  jint BUFFER_FLAG_END_OF_STREAM;
  jint CONFIGURE_FLAG_ENCODE;
  jint INFO_TRY_AGAIN_LATER;
  jint INFO_OUTPUT_FORMAT_CHANGED;
  jint INFO_OUTPUT_BUFFERS_CHANGED;
} android_media_mediacodec;

static gboolean
_init_classes (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  /* android.media.MediaCodec */
  GST_DVM_GET_CLASS (android_media_mediacodec, "android/media/MediaCodec");
  GST_DVM_GET_STATIC_METHOD (android_media_mediacodec, createByCodecName,
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  GST_DVM_GET_STATIC_METHOD (android_media_mediacodec, createDecoderByType,
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  GST_DVM_GET_STATIC_METHOD (android_media_mediacodec, createEncoderByType,
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  GST_DVM_GET_METHOD (android_media_mediacodec, configure,
      "(Landroid/media/MediaFormat;Landroid/view/Surface;"
      "Landroid/media/MediaCrypto;I)V");
  GST_DVM_GET_METHOD (android_media_mediacodec, dequeueInputBuffer, "(J)I");
  GST_DVM_GET_METHOD (android_media_mediacodec, dequeueOutputBuffer,
      "(Landroid/media/MediaCodec$BufferInfo;J)I");
  GST_DVM_GET_METHOD (android_media_mediacodec, flush, "()V");
  GST_DVM_GET_METHOD (android_media_mediacodec, getInputBuffers,
      "()[Ljava/nio/ByteBuffer;");
  GST_DVM_GET_METHOD (android_media_mediacodec, getOutputBuffers,
      "()[Ljava/nio/ByteBuffer;");
  GST_DVM_GET_METHOD (android_media_mediacodec, getOutputFormat,
      "()Landroid/media/MediaFormat;");
  GST_DVM_GET_METHOD (android_media_mediacodec, queueInputBuffer, "(IIIJI)V");
  GST_DVM_GET_METHOD (android_media_mediacodec, release, "()V");
  GST_DVM_GET_METHOD (android_media_mediacodec, releaseOutputBuffer, "(IZ)V");
  GST_DVM_GET_METHOD (android_media_mediacodec, start, "()V");
  GST_DVM_GET_METHOD (android_media_mediacodec, stop, "()V");


  GST_DVM_GET_CONSTANT (android_media_mediacodec, BUFFER_FLAG_SYNC_FRAME, Int,
      "I");
  MediaCodec_BUFFER_FLAG_SYNC_FRAME =
      android_media_mediacodec.BUFFER_FLAG_SYNC_FRAME;
  GST_DVM_GET_CONSTANT (android_media_mediacodec, BUFFER_FLAG_CODEC_CONFIG, Int,
      "I");
  MediaCodec_BUFFER_FLAG_CODEC_CONFIG =
      android_media_mediacodec.BUFFER_FLAG_CODEC_CONFIG;
  GST_DVM_GET_CONSTANT (android_media_mediacodec, BUFFER_FLAG_END_OF_STREAM,
      Int, "I");
  MediaCodec_BUFFER_FLAG_END_OF_STREAM =
      android_media_mediacodec.BUFFER_FLAG_END_OF_STREAM;

  GST_DVM_GET_CONSTANT (android_media_mediacodec, CONFIGURE_FLAG_ENCODE, Int,
      "I");
  MediaCodec_CONFIGURE_FLAG_ENCODE =
      android_media_mediacodec.CONFIGURE_FLAG_ENCODE;

  GST_DVM_GET_CONSTANT (android_media_mediacodec, INFO_TRY_AGAIN_LATER, Int,
      "I");
  MediaCodec_INFO_TRY_AGAIN_LATER =
      android_media_mediacodec.INFO_TRY_AGAIN_LATER;
  GST_DVM_GET_CONSTANT (android_media_mediacodec, INFO_OUTPUT_FORMAT_CHANGED,
      Int, "I");
  MediaCodec_INFO_OUTPUT_FORMAT_CHANGED =
      android_media_mediacodec.INFO_OUTPUT_FORMAT_CHANGED;
  GST_DVM_GET_CONSTANT (android_media_mediacodec, INFO_OUTPUT_BUFFERS_CHANGED,
      Int, "I");
  MediaCodec_INFO_OUTPUT_BUFFERS_CHANGED =
      android_media_mediacodec.INFO_OUTPUT_BUFFERS_CHANGED;

  /* android.media.MediaCodec.BufferInfo */
  GST_DVM_GET_CLASS (android_media_mediacodec_bufferinfo,
      "android/media/MediaCodec$BufferInfo");
  GST_DVM_GET_CONSTRUCTOR (android_media_mediacodec_bufferinfo, constructor,
      "()V");
  GST_DVM_GET_FIELD (android_media_mediacodec_bufferinfo, flags, "I");
  GST_DVM_GET_FIELD (android_media_mediacodec_bufferinfo, offset, "I");
  GST_DVM_GET_FIELD (android_media_mediacodec_bufferinfo, presentationTimeUs,
      "J");
  GST_DVM_GET_FIELD (android_media_mediacodec_bufferinfo, size, "I");

  return TRUE;
}

gboolean
gst_android_media_mediacodec_init (void)
{
  if (!_init_classes ()) {
    gst_android_media_mediacodec_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_media_mediacodec_deinit (void)
{
  JNIEnv *env = gst_dvm_get_env ();

  if (android_media_mediacodec.klass)
    (*env)->DeleteGlobalRef (env, android_media_mediacodec.klass);
  android_media_mediacodec.klass = NULL;

  if (android_media_mediacodec_bufferinfo.klass)
    (*env)->DeleteGlobalRef (env, android_media_mediacodec_bufferinfo.klass);
  android_media_mediacodec_bufferinfo.klass = NULL;
}

/* android.media.MediaCodec */

#define AMMC_CALL(error_statement, type, method, ...)                   \
  GST_DVM_CALL (error_statement, self->object, type,                    \
      android_media_mediacodec, method, ## __VA_ARGS__);
#define AMMC_STATIC_CALL(error_statement, type, method, ...)            \
  GST_DVM_STATIC_CALL (error_statement, type,                           \
      android_media_mediacodec, method, ## __VA_ARGS__);

gboolean
gst_am_mediacodec_configure (GstAmMediaCodec * self, GstAmMediaFormat * format,
    gint flags)
{
  JNIEnv *env = gst_dvm_get_env ();

  AMMC_CALL (return FALSE, Void, configure, format->object, NULL, NULL, flags);

  return TRUE;
}

GstAmMediaCodec *
gst_am_mediacodec_create_by_codec_name (const gchar * name)
{
  JNIEnv *env = gst_dvm_get_env ();
  GstAmMediaCodec *codec = NULL;
  jobject object = NULL;
  jstring name_str;

  name_str = (*env)->NewStringUTF (env, name);
  if (name_str == NULL)
    goto done;

  object = AMMC_STATIC_CALL (goto done, Object, createByCodecName, name_str);
  if (object) {
    codec = g_slice_new0 (GstAmMediaCodec);
    codec->object = (*env)->NewGlobalRef (env, object);
    (*env)->DeleteLocalRef (env, object);
    if (!codec->object) {
      GST_ERROR ("Failed to create global reference");
      (*env)->ExceptionClear (env);
      g_slice_free (GstAmMediaCodec, codec);
      codec = NULL;
    }
  }

done:
  if (name_str)
    (*env)->DeleteLocalRef (env, name_str);

  return codec;
}


GstAmMediaFormat *
gst_am_mediacodec_get_output_format (GstAmMediaCodec * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  GstAmMediaFormat *format = NULL;
  jobject object = NULL;

  object = AMMC_CALL (return NULL, Object, getOutputFormat);
  if (object) {
    format = g_slice_new0 (GstAmMediaFormat);
    format->object = (*env)->NewGlobalRef (env, object);
    (*env)->DeleteLocalRef (env, object);
    if (!format->object) {
      GST_ERROR ("Failed to create global reference");
      (*env)->ExceptionClear (env);
      g_slice_free (GstAmMediaFormat, format);
      return NULL;
    }
  }

  return format;
}

gboolean
gst_am_mediacodec_start (GstAmMediaCodec * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AMMC_CALL (return FALSE, Void, start);

  return TRUE;
}

gboolean
gst_am_mediacodec_stop (GstAmMediaCodec * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AMMC_CALL (return FALSE, Void, stop);

  return TRUE;
}

gboolean
gst_am_mediacodec_flush (GstAmMediaCodec * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AMMC_CALL (return FALSE, Void, flush);

  return TRUE;
}

void
gst_am_mediacodec_free (GstAmMediaCodec * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAmMediaCodec, self);
}

void
gst_am_mediacodec_release (GstAmMediaCodec * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AMMC_CALL (, Void, release);
}

void
gst_am_mediacodec_free_buffers (GstAmMediaCodecBuffer * buffers,
    gsize n_buffers)
{
  JNIEnv *env = gst_dvm_get_env ();
  jsize i;

  for (i = 0; i < n_buffers; i++) {
    if (buffers[i].object)
      (*env)->DeleteGlobalRef (env, buffers[i].object);
  }
  g_free (buffers);
}

GstAmMediaCodecBuffer *
gst_am_mediacodec_get_output_buffers (GstAmMediaCodec * self, gsize * n_buffers)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject output_buffers = NULL;
  jsize n_output_buffers;
  GstAmMediaCodecBuffer *ret = NULL;
  jsize i;

  *n_buffers = 0;
  output_buffers = AMMC_CALL (goto done, Object, getOutputBuffers);
  if (!output_buffers)
    goto done;

  n_output_buffers = (*env)->GetArrayLength (env, output_buffers);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get output buffers array length");
    goto done;
  }

  *n_buffers = n_output_buffers;
  ret = g_new0 (GstAmMediaCodecBuffer, n_output_buffers);

  for (i = 0; i < n_output_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, output_buffers, i);
    if ((*env)->ExceptionCheck (env) || !buffer) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get output buffer %d", i);
      goto error;
    }

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    (*env)->DeleteLocalRef (env, buffer);
    if (!ret[i].object) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to create global reference %d", i);
      goto error;
    }

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    if (!ret[i].data) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get buffer address %d", i);
      goto error;
    }
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

done:
  if (output_buffers)
    (*env)->DeleteLocalRef (env, output_buffers);
  output_buffers = NULL;

  return ret;
error:
  if (ret)
    gst_am_mediacodec_free_buffers (ret, n_output_buffers);
  ret = NULL;
  *n_buffers = 0;
  goto done;
}

GstAmMediaCodecBuffer *
gst_am_mediacodec_get_input_buffers (GstAmMediaCodec * self, gsize * n_buffers)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject input_buffers = NULL;
  jsize n_input_buffers;
  GstAmMediaCodecBuffer *ret = NULL;
  jsize i;

  *n_buffers = 0;

  input_buffers = AMMC_CALL (goto done, Object, getOutputBuffers);
  if (!input_buffers)
    goto done;

  n_input_buffers = (*env)->GetArrayLength (env, input_buffers);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get input buffers array length");
    goto done;
  }

  *n_buffers = n_input_buffers;
  ret = g_new0 (GstAmMediaCodecBuffer, n_input_buffers);

  for (i = 0; i < n_input_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, input_buffers, i);
    if ((*env)->ExceptionCheck (env) || !buffer) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get input buffer %d", i);
      goto error;
    }

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    (*env)->DeleteLocalRef (env, buffer);
    if (!ret[i].object) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to create global reference %d", i);
      goto error;
    }

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    if (!ret[i].data) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get buffer address %d", i);
      goto error;
    }
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

done:
  if (input_buffers)
    (*env)->DeleteLocalRef (env, input_buffers);
  input_buffers = NULL;

  return ret;
error:
  if (ret)
    gst_am_mediacodec_free_buffers (ret, n_input_buffers);
  ret = NULL;
  *n_buffers = 0;
  goto done;
}

gint
gst_am_mediacodec_dequeue_input_buffer (GstAmMediaCodec * self,
    gint64 timeoutUs)
{
  JNIEnv *env = gst_dvm_get_env ();
  gint ret = G_MININT;

  ret = AMMC_CALL (return G_MININT, Int, dequeueInputBuffer, timeoutUs);

  return ret;
}

#define AMMCBI_FIELD(error_statement, type, field)                       \
  GST_DVM_FIELD (error_statement, buffer_info, type,                     \
      android_media_mediacodec_bufferinfo, field);

static gboolean
_fill_buffer_info (JNIEnv * env, jobject buffer_info,
    GstAmMediaCodecBufferInfo * info)
{
  info->flags = AMMCBI_FIELD (return FALSE, Int, flags);
  info->offset = AMMCBI_FIELD (return FALSE, Int, offset);
  info->presentation_time_us =
      AMMCBI_FIELD (return FALSE, Long, presentationTimeUs);
  info->size = AMMCBI_FIELD (return FALSE, Int, size);

  return TRUE;
}

gint
gst_am_mediacodec_dequeue_output_buffer (GstAmMediaCodec * self,
    GstAmMediaCodecBufferInfo * info, gint64 timeoutUs)
{
  JNIEnv *env = gst_dvm_get_env ();
  gint ret = G_MININT;
  jobject info_o = NULL;

  info_o = (*env)->NewObject (env, android_media_mediacodec_bufferinfo.klass,
      android_media_mediacodec_bufferinfo.constructor);
  if (!info_o) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  ret = AMMC_CALL (goto error, Int, dequeueOutputBuffer, info_o, timeoutUs);

  if (!_fill_buffer_info (env, info_o, info))
    goto error;

done:
  if (info_o)
    (*env)->DeleteLocalRef (env, info_o);
  info_o = NULL;

  return ret;

error:
  ret = G_MININT;
  goto done;
}

gboolean
gst_am_mediacodec_queue_input_buffer (GstAmMediaCodec * self, gint index,
    const GstAmMediaCodecBufferInfo * info)
{
  JNIEnv *env = gst_dvm_get_env ();

  AMMC_CALL (return FALSE, Void, queueInputBuffer, index, info->offset,
      info->size, info->presentation_time_us, info->flags);

  return TRUE;
}

gboolean
gst_am_mediacodec_release_output_buffer (GstAmMediaCodec * self, gint index)
{
  JNIEnv *env = gst_dvm_get_env ();

  AMMC_CALL (return FALSE, Void, releaseOutputBuffer, index, JNI_FALSE);

  return TRUE;
}
