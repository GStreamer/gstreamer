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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamc.h"
#include "gstamc-constants.h"

#include "gstamcvideodec.h"
#include "gstamcaudiodec.h"

#include <gmodule.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <string.h>
#include <jni.h>

#include <pthread.h>

GST_DEBUG_CATEGORY (gst_amc_debug);
#define GST_CAT_DEFAULT gst_amc_debug

GQuark gst_amc_codec_info_quark = 0;

static GList *codec_infos = NULL;
#ifdef GST_AMC_IGNORE_UNKNOWN_COLOR_FORMATS
static gboolean ignore_unknown_color_formats = TRUE;
#else
static gboolean ignore_unknown_color_formats = FALSE;
#endif

static GModule *java_module;
static jint (*get_created_java_vms) (JavaVM ** vmBuf, jsize bufLen,
    jsize * nVMs);
static jint (*create_java_vm) (JavaVM ** p_vm, JNIEnv ** p_env, void *vm_args);
static JavaVM *java_vm;
static gboolean started_java_vm = FALSE;

static gboolean accepted_color_formats (GstAmcCodecType * type,
    gboolean is_encoder);

/* Global cached references */
static struct
{
  jclass klass;
  jmethodID constructor;
} java_string;
static struct
{
  jclass klass;
  jmethodID configure;
  jmethodID create_by_codec_name;
  jmethodID dequeue_input_buffer;
  jmethodID dequeue_output_buffer;
  jmethodID flush;
  jmethodID get_input_buffers;
  jmethodID get_output_buffers;
  jmethodID get_output_format;
  jmethodID queue_input_buffer;
  jmethodID release;
  jmethodID release_output_buffer;
  jmethodID start;
  jmethodID stop;
} media_codec;
static struct
{
  jclass klass;
  jmethodID constructor;
  jfieldID flags;
  jfieldID offset;
  jfieldID presentation_time_us;
  jfieldID size;
} media_codec_buffer_info;
static struct
{
  jclass klass;
  jmethodID create_audio_format;
  jmethodID create_video_format;
  jmethodID to_string;
  jmethodID contains_key;
  jmethodID get_float;
  jmethodID set_float;
  jmethodID get_integer;
  jmethodID set_integer;
  jmethodID get_string;
  jmethodID set_string;
  jmethodID get_byte_buffer;
  jmethodID set_byte_buffer;
} media_format;

static pthread_key_t current_jni_env;

static JNIEnv *
gst_amc_attach_current_thread (void)
{
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_6;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

static void
gst_amc_detach_current_thread (void *env)
{
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

static JNIEnv *
gst_amc_get_jni_env (void)
{
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = gst_amc_attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

static gboolean
initialize_java_vm (void)
{
  jsize n_vms;

  java_module = g_module_open ("libdvm", G_MODULE_BIND_LOCAL);
  if (!java_module)
    goto load_failed;

  if (!g_module_symbol (java_module, "JNI_CreateJavaVM",
          (gpointer *) & create_java_vm))
    goto symbol_error;

  if (!g_module_symbol (java_module, "JNI_GetCreatedJavaVMs",
          (gpointer *) & get_created_java_vms))
    goto symbol_error;

  n_vms = 0;
  if (get_created_java_vms (&java_vm, 1, &n_vms) < 0)
    goto get_created_failed;

  if (n_vms > 0) {
    GST_DEBUG ("Successfully got existing Java VM %p", java_vm);
  } else {
    JNIEnv *env;
    JavaVMInitArgs vm_args;
    JavaVMOption options[4];

    options[0].optionString = "-verbose:jni";
    options[1].optionString = "-verbose:gc";
    options[2].optionString = "-Xcheck:jni";
    options[3].optionString = "-Xdebug";

    vm_args.version = JNI_VERSION_1_4;
    vm_args.options = options;
    vm_args.nOptions = 4;
    vm_args.ignoreUnrecognized = JNI_TRUE;
    if (create_java_vm (&java_vm, &env, &vm_args) < 0)
      goto create_failed;
    GST_DEBUG ("Successfully created Java VM %p", java_vm);

    started_java_vm = TRUE;
  }

  return java_vm != NULL;

load_failed:
  {
    GST_ERROR ("Failed to load libdvm: %s", g_module_error ());
    return FALSE;
  }
symbol_error:
  {
    GST_ERROR ("Failed to locate required JNI symbols in libdvm: %s",
        g_module_error ());
    g_module_close (java_module);
    java_module = NULL;
    return FALSE;
  }
get_created_failed:
  {
    GST_ERROR ("Failed to get already created VMs");
    g_module_close (java_module);
    java_module = NULL;
    return FALSE;
  }
create_failed:
  {
    GST_ERROR ("Failed to create a Java VM");
    g_module_close (java_module);
    java_module = NULL;
    return FALSE;
  }
}

GstAmcCodec *
gst_amc_codec_new (const gchar * name)
{
  JNIEnv *env;
  GstAmcCodec *codec = NULL;
  jstring name_str;
  jobject object = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  env = gst_amc_get_jni_env ();

  name_str = (*env)->NewStringUTF (env, name);
  if (name_str == NULL)
    goto error;

  codec = g_slice_new0 (GstAmcCodec);

  object =
      (*env)->CallStaticObjectMethod (env, media_codec.klass,
      media_codec.create_by_codec_name, name_str);
  if ((*env)->ExceptionCheck (env) || !object) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to create codec '%s'", name);
    goto error;
  }

  codec->object = (*env)->NewGlobalRef (env, object);
  if (!codec->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    goto error;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);
  if (name_str)
    (*env)->DeleteLocalRef (env, name_str);
  name_str = NULL;

  return codec;

error:
  if (codec)
    g_slice_free (GstAmcCodec, codec);
  codec = NULL;
  goto done;
}

void
gst_amc_codec_free (GstAmcCodec * codec)
{
  JNIEnv *env;

  g_return_if_fail (codec != NULL);

  env = gst_amc_get_jni_env ();
  (*env)->DeleteGlobalRef (env, codec->object);
  g_slice_free (GstAmcCodec, codec);
}

gboolean
gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format, gint flags)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.configure,
      format->object, NULL, NULL, flags);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

GstAmcFormat *
gst_amc_codec_get_output_format (GstAmcCodec * codec)
{
  JNIEnv *env;
  GstAmcFormat *ret = NULL;
  jobject object = NULL;

  g_return_val_if_fail (codec != NULL, NULL);

  env = gst_amc_get_jni_env ();

  object =
      (*env)->CallObjectMethod (env, codec->object,
      media_codec.get_output_format);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  ret = g_slice_new0 (GstAmcFormat);

  ret->object = (*env)->NewGlobalRef (env, object);
  if (!ret->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAmcFormat, ret);
    ret = NULL;
  }

  (*env)->DeleteLocalRef (env, object);

done:

  return ret;
}

gboolean
gst_amc_codec_start (GstAmcCodec * codec)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.start);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_stop (GstAmcCodec * codec)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.stop);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_flush (GstAmcCodec * codec)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.flush);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_release (GstAmcCodec * codec)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.release);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

void
gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers)
{
  JNIEnv *env;
  jsize i;

  g_return_if_fail (buffers != NULL);

  env = gst_amc_get_jni_env ();

  for (i = 0; i < n_buffers; i++) {
    if (buffers[i].object)
      (*env)->DeleteGlobalRef (env, buffers[i].object);
  }
  g_free (buffers);
}

GstAmcBuffer *
gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  JNIEnv *env;
  jobject output_buffers = NULL;
  jsize n_output_buffers;
  GstAmcBuffer *ret = NULL;
  jsize i;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (n_buffers != NULL, NULL);

  *n_buffers = 0;
  env = gst_amc_get_jni_env ();

  output_buffers =
      (*env)->CallObjectMethod (env, codec->object,
      media_codec.get_output_buffers);
  if ((*env)->ExceptionCheck (env) || !output_buffers) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  n_output_buffers = (*env)->GetArrayLength (env, output_buffers);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get output buffers array length");
    goto done;
  }

  *n_buffers = n_output_buffers;
  ret = g_new0 (GstAmcBuffer, n_output_buffers);

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
    gst_amc_codec_free_buffers (ret, n_output_buffers);
  ret = NULL;
  *n_buffers = 0;
  goto done;
}

GstAmcBuffer *
gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  JNIEnv *env;
  jobject input_buffers = NULL;
  jsize n_input_buffers;
  GstAmcBuffer *ret = NULL;
  jsize i;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (n_buffers != NULL, NULL);

  *n_buffers = 0;
  env = gst_amc_get_jni_env ();

  input_buffers =
      (*env)->CallObjectMethod (env, codec->object,
      media_codec.get_input_buffers);
  if ((*env)->ExceptionCheck (env) || !input_buffers) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  n_input_buffers = (*env)->GetArrayLength (env, input_buffers);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get input buffers array length");
    goto done;
  }

  *n_buffers = n_input_buffers;
  ret = g_new0 (GstAmcBuffer, n_input_buffers);

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
    gst_amc_codec_free_buffers (ret, n_input_buffers);
  ret = NULL;
  *n_buffers = 0;
  goto done;
}

gint
gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs)
{
  JNIEnv *env;
  gint ret = G_MININT;

  g_return_val_if_fail (codec != NULL, G_MININT);

  env = gst_amc_get_jni_env ();

  ret =
      (*env)->CallIntMethod (env, codec->object,
      media_codec.dequeue_input_buffer, timeoutUs);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = G_MININT;
    goto done;
  }

done:

  return ret;
}

static gboolean
gst_amc_codec_fill_buffer_info (JNIEnv * env, jobject buffer_info,
    GstAmcBufferInfo * info)
{
  g_return_val_if_fail (buffer_info != NULL, FALSE);

  info->flags =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.flags);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer info field");
    return FALSE;
  }

  info->offset =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.offset);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer info field");
    return FALSE;
  }

  info->presentation_time_us =
      (*env)->GetLongField (env, buffer_info,
      media_codec_buffer_info.presentation_time_us);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer info field");
    return FALSE;
  }

  info->size =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.size);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer info field");
    return FALSE;
  }

  return TRUE;
}

gint
gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec,
    GstAmcBufferInfo * info, gint64 timeoutUs)
{
  JNIEnv *env;
  gint ret = G_MININT;
  jobject info_o = NULL;

  g_return_val_if_fail (codec != NULL, G_MININT);

  env = gst_amc_get_jni_env ();

  info_o =
      (*env)->NewObject (env, media_codec_buffer_info.klass,
      media_codec_buffer_info.constructor);
  if (!info_o) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  ret =
      (*env)->CallIntMethod (env, codec->object,
      media_codec.dequeue_output_buffer, info_o, timeoutUs);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = G_MININT;
    goto done;
  }

  if (!gst_amc_codec_fill_buffer_info (env, info_o, info)) {
    ret = G_MININT;
    goto done;
  }

done:
  if (info_o)
    (*env)->DeleteLocalRef (env, info_o);
  info_o = NULL;

  return ret;
}

gboolean
gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.queue_input_buffer,
      index, info->offset, info->size, info->presentation_time_us, info->flags);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.release_output_buffer,
      index, JNI_FALSE);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

GstAmcFormat *
gst_amc_format_new_audio (const gchar * mime, gint sample_rate, gint channels)
{
  JNIEnv *env;
  GstAmcFormat *format = NULL;
  jstring mime_str;
  jobject object = NULL;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_amc_get_jni_env ();

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_slice_new0 (GstAmcFormat);

  object =
      (*env)->CallStaticObjectMethod (env, media_format.klass,
      media_format.create_audio_format, mime_str, sample_rate, channels);
  if ((*env)->ExceptionCheck (env) || !object) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to create format '%s'", mime);
    goto error;
  }

  format->object = (*env)->NewGlobalRef (env, object);
  if (!format->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    goto error;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);
  if (mime_str)
    (*env)->DeleteLocalRef (env, mime_str);
  mime_str = NULL;

  return format;

error:
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

GstAmcFormat *
gst_amc_format_new_video (const gchar * mime, gint width, gint height)
{
  JNIEnv *env;
  GstAmcFormat *format = NULL;
  jstring mime_str;
  jobject object = NULL;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_amc_get_jni_env ();

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_slice_new0 (GstAmcFormat);

  object =
      (*env)->CallStaticObjectMethod (env, media_format.klass,
      media_format.create_video_format, mime_str, width, height);
  if ((*env)->ExceptionCheck (env) || !object) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to create format '%s'", mime);
    goto error;
  }

  format->object = (*env)->NewGlobalRef (env, object);
  if (!format->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    goto error;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);
  if (mime_str)
    (*env)->DeleteLocalRef (env, mime_str);
  mime_str = NULL;

  return format;

error:
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

void
gst_amc_format_free (GstAmcFormat * format)
{
  JNIEnv *env;

  g_return_if_fail (format != NULL);

  env = gst_amc_get_jni_env ();
  (*env)->DeleteGlobalRef (env, format->object);
  g_slice_free (GstAmcFormat, format);
}

gchar *
gst_amc_format_to_string (GstAmcFormat * format)
{
  JNIEnv *env;
  jstring v_str = NULL;
  const gchar *v = NULL;
  gchar *ret = NULL;

  g_return_val_if_fail (format != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  v_str =
      (*env)->CallObjectMethod (env, format->object, media_format.to_string);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  v = (*env)->GetStringUTFChars (env, v_str, NULL);
  if (!v) {
    GST_ERROR ("Failed to convert string to UTF8");
    (*env)->ExceptionClear (env);
    goto done;
  }

  ret = g_strdup (v);

done:
  if (v)
    (*env)->ReleaseStringUTFChars (env, v_str, v);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);

  return ret;
}

gboolean
gst_amc_format_contains_key (GstAmcFormat * format, const gchar * key)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  ret =
      (*env)->CallBooleanMethod (env, format->object, media_format.contains_key,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

gboolean
gst_amc_format_get_float (GstAmcFormat * format, const gchar * key,
    gfloat * value)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  *value = 0;
  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  *value =
      (*env)->CallFloatMethod (env, format->object, media_format.get_float,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

void
gst_amc_format_set_float (GstAmcFormat * format, const gchar * key,
    gfloat value)
{
  JNIEnv *env;
  jstring key_str = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);

  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  (*env)->CallVoidMethod (env, format->object, media_format.set_float, key_str,
      value);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
}

gboolean
gst_amc_format_get_int (GstAmcFormat * format, const gchar * key, gint * value)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  *value = 0;
  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  *value =
      (*env)->CallIntMethod (env, format->object, media_format.get_integer,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;

}

void
gst_amc_format_set_int (GstAmcFormat * format, const gchar * key, gint value)
{
  JNIEnv *env;
  jstring key_str = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);

  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  (*env)->CallVoidMethod (env, format->object, media_format.set_integer,
      key_str, value);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
}

gboolean
gst_amc_format_get_string (GstAmcFormat * format, const gchar * key,
    gchar ** value)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jstring v_str = NULL;
  const gchar *v = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  *value = 0;
  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  v_str =
      (*env)->CallObjectMethod (env, format->object, media_format.get_string,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

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

void
gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value)
{
  JNIEnv *env;
  jstring key_str = NULL;
  jstring v_str = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (value != NULL);

  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  v_str = (*env)->NewStringUTF (env, value);
  if (!v_str)
    goto done;

  (*env)->CallVoidMethod (env, format->object, media_format.set_string, key_str,
      v_str);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);
}

gboolean
gst_amc_format_get_buffer (GstAmcFormat * format, const gchar * key,
    guint8 ** data, gsize * size)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jobject v = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size != NULL, FALSE);

  *data = NULL;
  *size = 0;
  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  v = (*env)->CallObjectMethod (env, format->object,
      media_format.get_byte_buffer, key_str);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  *data = (*env)->GetDirectBufferAddress (env, v);
  if (!data) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer address");
    goto done;
  }
  *size = (*env)->GetDirectBufferCapacity (env, v);
  *data = g_memdup (*data, *size);

  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->DeleteLocalRef (env, v);

  return ret;
}

void
gst_amc_format_set_buffer (GstAmcFormat * format, const gchar * key,
    guint8 * data, gsize size)
{
  JNIEnv *env;
  jstring key_str = NULL;
  jobject v = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (data != NULL);

  env = gst_amc_get_jni_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str)
    goto done;

  /* FIXME: The memory must remain valid until the codec is stopped */
  v = (*env)->NewDirectByteBuffer (env, data, size);
  if (!v)
    goto done;

  (*env)->CallVoidMethod (env, format->object, media_format.set_byte_buffer,
      key_str, v);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->DeleteLocalRef (env, v);
}

static gboolean
get_java_classes (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;
  jclass tmp;

  GST_DEBUG ("Retrieving Java classes");

  env = gst_amc_get_jni_env ();

  tmp = (*env)->FindClass (env, "java/lang/String");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get string class");
    goto done;
  }
  java_string.klass = (*env)->NewGlobalRef (env, tmp);
  if (!java_string.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get string class global reference");
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  java_string.constructor =
      (*env)->GetMethodID (env, java_string.klass, "<init>", "([C)V");
  if (!java_string.constructor) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get string methods");
    goto done;
  }

  tmp = (*env)->FindClass (env, "android/media/MediaCodec");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec class");
    goto done;
  }
  media_codec.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_codec.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec class global reference");
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_codec.create_by_codec_name =
      (*env)->GetStaticMethodID (env, media_codec.klass, "createByCodecName",
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  media_codec.configure =
      (*env)->GetMethodID (env, media_codec.klass, "configure",
      "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");
  media_codec.dequeue_input_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "dequeueInputBuffer",
      "(J)I");
  media_codec.dequeue_output_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "dequeueOutputBuffer",
      "(Landroid/media/MediaCodec$BufferInfo;J)I");
  media_codec.flush =
      (*env)->GetMethodID (env, media_codec.klass, "flush", "()V");
  media_codec.get_input_buffers =
      (*env)->GetMethodID (env, media_codec.klass, "getInputBuffers",
      "()[Ljava/nio/ByteBuffer;");
  media_codec.get_output_buffers =
      (*env)->GetMethodID (env, media_codec.klass, "getOutputBuffers",
      "()[Ljava/nio/ByteBuffer;");
  media_codec.get_output_format =
      (*env)->GetMethodID (env, media_codec.klass, "getOutputFormat",
      "()Landroid/media/MediaFormat;");
  media_codec.queue_input_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "queueInputBuffer",
      "(IIIJI)V");
  media_codec.release =
      (*env)->GetMethodID (env, media_codec.klass, "release", "()V");
  media_codec.release_output_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "releaseOutputBuffer",
      "(IZ)V");
  media_codec.start =
      (*env)->GetMethodID (env, media_codec.klass, "start", "()V");
  media_codec.stop =
      (*env)->GetMethodID (env, media_codec.klass, "stop", "()V");

  if (!media_codec.configure ||
      !media_codec.create_by_codec_name ||
      !media_codec.dequeue_input_buffer ||
      !media_codec.dequeue_output_buffer ||
      !media_codec.flush ||
      !media_codec.get_input_buffers ||
      !media_codec.get_output_buffers ||
      !media_codec.get_output_format ||
      !media_codec.queue_input_buffer ||
      !media_codec.release ||
      !media_codec.release_output_buffer ||
      !media_codec.start || !media_codec.stop) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec methods");
    goto done;
  }

  tmp = (*env)->FindClass (env, "android/media/MediaCodec$BufferInfo");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec buffer info class");
    goto done;
  }
  media_codec_buffer_info.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_codec_buffer_info.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec buffer info class global reference");
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_codec_buffer_info.constructor =
      (*env)->GetMethodID (env, media_codec_buffer_info.klass, "<init>", "()V");
  media_codec_buffer_info.flags =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "flags", "I");
  media_codec_buffer_info.offset =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "offset", "I");
  media_codec_buffer_info.presentation_time_us =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass,
      "presentationTimeUs", "J");
  media_codec_buffer_info.size =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "size", "I");
  if (!media_codec_buffer_info.constructor || !media_codec_buffer_info.flags
      || !media_codec_buffer_info.offset
      || !media_codec_buffer_info.presentation_time_us
      || !media_codec_buffer_info.size) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer info methods and fields");
    goto done;
  }

  tmp = (*env)->FindClass (env, "android/media/MediaFormat");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format class");
    goto done;
  }
  media_format.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_format.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format class global reference");
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_format.create_audio_format =
      (*env)->GetStaticMethodID (env, media_format.klass, "createAudioFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  media_format.create_video_format =
      (*env)->GetStaticMethodID (env, media_format.klass, "createVideoFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  media_format.to_string =
      (*env)->GetMethodID (env, media_format.klass, "toString",
      "()Ljava/lang/String;");
  media_format.contains_key =
      (*env)->GetMethodID (env, media_format.klass, "containsKey",
      "(Ljava/lang/String;)Z");
  media_format.get_float =
      (*env)->GetMethodID (env, media_format.klass, "getFloat",
      "(Ljava/lang/String;)F");
  media_format.set_float =
      (*env)->GetMethodID (env, media_format.klass, "setFloat",
      "(Ljava/lang/String;F)V");
  media_format.get_integer =
      (*env)->GetMethodID (env, media_format.klass, "getInteger",
      "(Ljava/lang/String;)I");
  media_format.set_integer =
      (*env)->GetMethodID (env, media_format.klass, "setInteger",
      "(Ljava/lang/String;I)V");
  media_format.get_string =
      (*env)->GetMethodID (env, media_format.klass, "getString",
      "(Ljava/lang/String;)Ljava/lang/String;");
  media_format.set_string =
      (*env)->GetMethodID (env, media_format.klass, "setString",
      "(Ljava/lang/String;Ljava/lang/String;)V");
  media_format.get_byte_buffer =
      (*env)->GetMethodID (env, media_format.klass, "getByteBuffer",
      "(Ljava/lang/String;)Ljava/nio/ByteBuffer;");
  media_format.set_byte_buffer =
      (*env)->GetMethodID (env, media_format.klass, "setByteBuffer",
      "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");
  if (!media_format.create_audio_format || !media_format.create_video_format
      || !media_format.contains_key || !media_format.get_float
      || !media_format.set_float || !media_format.get_integer
      || !media_format.set_integer || !media_format.get_string
      || !media_format.set_string || !media_format.get_byte_buffer
      || !media_format.set_byte_buffer) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format methods");
    goto done;
  }

done:
  if (tmp)
    (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  return ret;
}

static gboolean
scan_codecs (GstPlugin * plugin)
{
  gboolean ret = TRUE;
  JNIEnv *env;
  jclass codec_list_class = NULL;
  jmethodID get_codec_count_id, get_codec_info_at_id;
  jint codec_count, i;
  const GstStructure *cache_data;

  GST_DEBUG ("Scanning codecs");

  if ((cache_data = gst_plugin_get_cache_data (plugin))) {
    const GValue *arr = gst_structure_get_value (cache_data, "codecs");
    guint i, n;

    GST_DEBUG ("Getting codecs from cache");
    n = gst_value_array_get_size (arr);
    for (i = 0; i < n; i++) {
      const GValue *cv = gst_value_array_get_value (arr, i);
      const GstStructure *cs = gst_value_get_structure (cv);
      const gchar *name;
      gboolean is_encoder;
      const GValue *starr;
      guint j, n2;
      GstAmcCodecInfo *gst_codec_info;

      gst_codec_info = g_new0 (GstAmcCodecInfo, 1);

      name = gst_structure_get_string (cs, "name");
      gst_structure_get_boolean (cs, "is-encoder", &is_encoder);
      gst_codec_info->name = g_strdup (name);
      gst_codec_info->is_encoder = is_encoder;

      starr = gst_structure_get_value (cs, "supported-types");
      n2 = gst_value_array_get_size (starr);

      gst_codec_info->n_supported_types = n2;
      gst_codec_info->supported_types = g_new0 (GstAmcCodecType, n2);

      for (j = 0; j < n2; j++) {
        const GValue *stv = gst_value_array_get_value (starr, j);
        const GstStructure *sts = gst_value_get_structure (stv);
        const gchar *mime;
        const GValue *cfarr;
        const GValue *plarr;
        guint k, n3;
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[j];

        mime = gst_structure_get_string (sts, "mime");
        gst_codec_type->mime = g_strdup (mime);

        cfarr = gst_structure_get_value (sts, "color-formats");
        n3 = gst_value_array_get_size (cfarr);

        gst_codec_type->n_color_formats = n3;
        gst_codec_type->color_formats = g_new0 (gint, n3);

        for (k = 0; k < n3; k++) {
          const GValue *cfv = gst_value_array_get_value (cfarr, k);
          gint cf = g_value_get_int (cfv);

          gst_codec_type->color_formats[k] = cf;
        }

        plarr = gst_structure_get_value (sts, "profile-levels");
        n3 = gst_value_array_get_size (plarr);

        gst_codec_type->n_profile_levels = n3;
        gst_codec_type->profile_levels =
            g_malloc0 (sizeof (gst_codec_type->profile_levels[0]) * n3);

        for (k = 0; k < n3; k++) {
          const GValue *plv = gst_value_array_get_value (plarr, k);
          const GValue *p, *l;

          p = gst_value_array_get_value (plv, 0);
          l = gst_value_array_get_value (plv, 1);
          gst_codec_type->profile_levels[k].profile = g_value_get_int (p);
          gst_codec_type->profile_levels[k].level = g_value_get_int (l);
        }
      }

      codec_infos = g_list_append (codec_infos, gst_codec_info);
    }

    return TRUE;
  }

  env = gst_amc_get_jni_env ();

  codec_list_class = (*env)->FindClass (env, "android/media/MediaCodecList");
  if (!codec_list_class) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec list class");
    goto done;
  }

  get_codec_count_id =
      (*env)->GetStaticMethodID (env, codec_list_class, "getCodecCount", "()I");
  get_codec_info_at_id =
      (*env)->GetStaticMethodID (env, codec_list_class, "getCodecInfoAt",
      "(I)Landroid/media/MediaCodecInfo;");
  if (!get_codec_count_id || !get_codec_info_at_id) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec list method IDs");
    goto done;
  }

  codec_count =
      (*env)->CallStaticIntMethod (env, codec_list_class, get_codec_count_id);
  if ((*env)->ExceptionCheck (env)) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get number of available codecs");
    goto done;
  }

  GST_LOG ("Found %d available codecs", codec_count);

  for (i = 0; i < codec_count; i++) {
    GstAmcCodecInfo *gst_codec_info;
    jobject codec_info = NULL;
    jclass codec_info_class = NULL;
    jmethodID get_capabilities_for_type_id, get_name_id;
    jmethodID get_supported_types_id, is_encoder_id;
    jobject name = NULL;
    const gchar *name_str = NULL;
    jboolean is_encoder;
    jarray supported_types = NULL;
    jsize n_supported_types;
    jsize j;
    gboolean valid_codec = TRUE;

    gst_codec_info = g_new0 (GstAmcCodecInfo, 1);

    codec_info =
        (*env)->CallStaticObjectMethod (env, codec_list_class,
        get_codec_info_at_id, i);
    if ((*env)->ExceptionCheck (env) || !codec_info) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info %d", i);
      valid_codec = FALSE;
      goto next_codec;
    }

    codec_info_class = (*env)->GetObjectClass (env, codec_info);
    if (!codec_list_class) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info class");
      valid_codec = FALSE;
      goto next_codec;
    }

    get_capabilities_for_type_id =
        (*env)->GetMethodID (env, codec_info_class, "getCapabilitiesForType",
        "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
    get_name_id =
        (*env)->GetMethodID (env, codec_info_class, "getName",
        "()Ljava/lang/String;");
    get_supported_types_id =
        (*env)->GetMethodID (env, codec_info_class, "getSupportedTypes",
        "()[Ljava/lang/String;");
    is_encoder_id =
        (*env)->GetMethodID (env, codec_info_class, "isEncoder", "()Z");
    if (!get_capabilities_for_type_id || !get_name_id
        || !get_supported_types_id || !is_encoder_id) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info method IDs");
      valid_codec = FALSE;
      goto next_codec;
    }

    name = (*env)->CallObjectMethod (env, codec_info, get_name_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec name");
      valid_codec = FALSE;
      goto next_codec;
    }
    name_str = (*env)->GetStringUTFChars (env, name, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to convert codec name to UTF8");
      valid_codec = FALSE;
      goto next_codec;
    }

    GST_INFO ("Checking codec '%s'", name_str);

    /* Compatibility codec names */
    if (strcmp (name_str, "AACEncoder") == 0 ||
        strcmp (name_str, "OMX.google.raw.decoder") == 0) {
      GST_INFO ("Skipping compatibility codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    if (g_str_has_suffix (name_str, ".secure")) {
      GST_INFO ("Skipping DRM codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    /* FIXME: Non-Google codecs usually just don't work and hang forever
     * or crash when not used from a process that started the Java
     * VM via the non-public AndroidRuntime class. Can we somehow
     * initialize all this?
     */
    if (started_java_vm && !g_str_has_prefix (name_str, "OMX.google.")) {
      GST_INFO ("Skipping non-Google codec '%s' in standalone mode", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    if (g_str_has_prefix (name_str, "OMX.ARICENT.")) {
      GST_INFO ("Skipping possible broken codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    /* FIXME:
     *   - Vorbis: Generates clicks for multi-channel streams
     *   - *Law: Generates output with too low frequencies
     */
    if (strcmp (name_str, "OMX.google.vorbis.decoder") == 0 ||
        strcmp (name_str, "OMX.google.g711.alaw.decoder") == 0 ||
        strcmp (name_str, "OMX.google.g711.mlaw.decoder") == 0) {
      GST_INFO ("Skipping known broken codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }
    gst_codec_info->name = g_strdup (name_str);

    is_encoder = (*env)->CallBooleanMethod (env, codec_info, is_encoder_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to detect if codec is an encoder");
      valid_codec = FALSE;
      goto next_codec;
    }
    gst_codec_info->is_encoder = is_encoder;

    supported_types =
        (*env)->CallObjectMethod (env, codec_info, get_supported_types_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types");
      valid_codec = FALSE;
      goto next_codec;
    }

    n_supported_types = (*env)->GetArrayLength (env, supported_types);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types array length");
      valid_codec = FALSE;
      goto next_codec;
    }

    GST_INFO ("Codec '%s' has %d supported types", name_str, n_supported_types);

    gst_codec_info->supported_types =
        g_new0 (GstAmcCodecType, n_supported_types);
    gst_codec_info->n_supported_types = n_supported_types;

    if (n_supported_types == 0) {
      valid_codec = FALSE;
      GST_ERROR ("Codec has no supported types");
      goto next_codec;
    }

    for (j = 0; j < n_supported_types; j++) {
      GstAmcCodecType *gst_codec_type;
      jobject supported_type = NULL;
      const gchar *supported_type_str = NULL;
      jobject capabilities = NULL;
      jclass capabilities_class = NULL;
      jfieldID color_formats_id, profile_levels_id;
      jobject color_formats = NULL;
      jobject profile_levels = NULL;
      jint *color_formats_elems = NULL;
      jsize n_elems, k;

      gst_codec_type = &gst_codec_info->supported_types[j];

      supported_type = (*env)->GetObjectArrayElement (env, supported_types, j);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get %d-th supported type", j);
        valid_codec = FALSE;
        goto next_supported_type;
      }

      supported_type_str =
          (*env)->GetStringUTFChars (env, supported_type, NULL);
      if ((*env)->ExceptionCheck (env) || !supported_type_str) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to convert supported type to UTF8");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      GST_INFO ("Supported type '%s'", supported_type_str);
      gst_codec_type->mime = g_strdup (supported_type_str);

      capabilities =
          (*env)->CallObjectMethod (env, codec_info,
          get_capabilities_for_type_id, supported_type);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities for supported type");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      capabilities_class = (*env)->GetObjectClass (env, capabilities);
      if (!capabilities_class) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities class");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      color_formats_id =
          (*env)->GetFieldID (env, capabilities_class, "colorFormats", "[I");
      profile_levels_id =
          (*env)->GetFieldID (env, capabilities_class, "profileLevels",
          "[Landroid/media/MediaCodecInfo$CodecProfileLevel;");
      if (!color_formats_id || !profile_levels_id) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities field IDs");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      color_formats =
          (*env)->GetObjectField (env, capabilities, color_formats_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color formats");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, color_formats);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color formats array length");
        valid_codec = FALSE;
        goto next_supported_type;
      }
      gst_codec_type->n_color_formats = n_elems;
      gst_codec_type->color_formats = g_new0 (gint, n_elems);
      color_formats_elems =
          (*env)->GetIntArrayElements (env, color_formats, NULL);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color format elements");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      for (k = 0; k < n_elems; k++) {
        if (strcmp (name_str, "OMX.k3.video.decoder.avc") == 0)
          if (n_elems == 1 && color_formats_elems[k] == COLOR_FormatYCbYCr) {
            GST_INFO ("On HuaweiMediaPad it reports a wrong COLOR_FormatYCbYCr,"
                "should be COLOR_TI_FormatYUV420PackedSemiPlanar, fix it.");
            color_formats_elems[k] = COLOR_TI_FormatYUV420PackedSemiPlanar;
          }

        GST_INFO ("Color format %d: %d", k, color_formats_elems[k]);
        gst_codec_type->color_formats[k] = color_formats_elems[k];
      }

      if (g_str_has_prefix (gst_codec_type->mime, "video/")) {
        if (!n_elems) {
          GST_ERROR ("No supported color formats for video codec");
          valid_codec = FALSE;
          goto next_supported_type;
        }

        if (!ignore_unknown_color_formats
            && !accepted_color_formats (gst_codec_type, is_encoder)) {
          GST_ERROR ("Codec has unknown color formats, ignoring");
          valid_codec = FALSE;
          g_assert_not_reached ();
          goto next_supported_type;
        }
      }

      profile_levels =
          (*env)->GetObjectField (env, capabilities, profile_levels_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get profile/levels");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, profile_levels);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get profile/levels array length");
        valid_codec = FALSE;
        goto next_supported_type;
      }
      gst_codec_type->n_profile_levels = n_elems;
      gst_codec_type->profile_levels =
          g_malloc0 (sizeof (gst_codec_type->profile_levels[0]) * n_elems);
      for (k = 0; k < n_elems; k++) {
        jobject profile_level = NULL;
        jclass profile_level_class = NULL;
        jfieldID level_id, profile_id;
        jint level, profile;

        profile_level = (*env)->GetObjectArrayElement (env, profile_levels, k);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get %d-th profile/level", k);
          valid_codec = FALSE;
          goto next_profile_level;
        }

        profile_level_class = (*env)->GetObjectClass (env, profile_level);
        if (!profile_level_class) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level class");
          valid_codec = FALSE;
          goto next_profile_level;
        }

        level_id = (*env)->GetFieldID (env, profile_level_class, "level", "I");
        profile_id =
            (*env)->GetFieldID (env, profile_level_class, "profile", "I");
        if (!level_id || !profile_id) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level field IDs");
          valid_codec = FALSE;
          goto next_profile_level;
        }

        level = (*env)->GetIntField (env, profile_level, level_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get level");
          valid_codec = FALSE;
          goto next_profile_level;
        }
        GST_INFO ("Level %d: 0x%08x", k, level);
        gst_codec_type->profile_levels[k].level = level;

        profile = (*env)->GetIntField (env, profile_level, profile_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile");
          valid_codec = FALSE;
          goto next_profile_level;
        }
        GST_INFO ("Profile %d: 0x%08x", k, profile);
        gst_codec_type->profile_levels[k].profile = profile;

      next_profile_level:
        if (profile_level)
          (*env)->DeleteLocalRef (env, profile_level);
        profile_level = NULL;
        if (profile_level_class)
          (*env)->DeleteLocalRef (env, profile_level_class);
        profile_level_class = NULL;
        if (!valid_codec)
          break;
      }

    next_supported_type:
      if (color_formats_elems)
        (*env)->ReleaseIntArrayElements (env, color_formats,
            color_formats_elems, JNI_ABORT);
      color_formats_elems = NULL;
      if (color_formats)
        (*env)->DeleteLocalRef (env, color_formats);
      color_formats = NULL;
      if (profile_levels)
        (*env)->DeleteLocalRef (env, profile_levels);
      color_formats = NULL;
      if (capabilities)
        (*env)->DeleteLocalRef (env, capabilities);
      capabilities = NULL;
      if (capabilities_class)
        (*env)->DeleteLocalRef (env, capabilities_class);
      capabilities_class = NULL;
      if (supported_type_str)
        (*env)->ReleaseStringUTFChars (env, supported_type, supported_type_str);
      supported_type_str = NULL;
      if (supported_type)
        (*env)->DeleteLocalRef (env, supported_type);
      supported_type = NULL;
      if (!valid_codec)
        break;
    }

    /* We need at least a valid supported type */
    if (valid_codec) {
      GST_LOG ("Successfully scanned codec '%s'", name_str);
      codec_infos = g_list_append (codec_infos, gst_codec_info);
      gst_codec_info = NULL;
    }

    /* Clean up of all local references we got */
  next_codec:
    if (name_str)
      (*env)->ReleaseStringUTFChars (env, name, name_str);
    name_str = NULL;
    if (name)
      (*env)->DeleteLocalRef (env, name);
    name = NULL;
    if (supported_types)
      (*env)->DeleteLocalRef (env, supported_types);
    supported_types = NULL;
    if (codec_info)
      (*env)->DeleteLocalRef (env, codec_info);
    codec_info = NULL;
    if (codec_info_class)
      (*env)->DeleteLocalRef (env, codec_info_class);
    codec_info_class = NULL;
    if (gst_codec_info) {
      gint j;

      for (j = 0; j < gst_codec_info->n_supported_types; j++) {
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[j];

        g_free (gst_codec_type->mime);
        g_free (gst_codec_type->color_formats);
        g_free (gst_codec_type->profile_levels);
      }
      g_free (gst_codec_info->supported_types);
      g_free (gst_codec_info->name);
      g_free (gst_codec_info);
    }
    gst_codec_info = NULL;
    valid_codec = TRUE;
  }

  ret = codec_infos != NULL;

  /* If successful we store a cache of the codec information in
   * the registry. Otherwise we would always load all codecs during
   * plugin initialization which can take quite some time (because
   * of hardware) and also loads lots of shared libraries (which
   * number is limited by 64 in Android).
   */
  if (ret) {
    GstStructure *new_cache_data = gst_structure_new_empty ("gst-amc-cache");
    GList *l;
    GValue arr = { 0, };

    g_value_init (&arr, GST_TYPE_ARRAY);

    for (l = codec_infos; l; l = l->next) {
      GstAmcCodecInfo *gst_codec_info = l->data;
      GValue cv = { 0, };
      GstStructure *cs = gst_structure_new_empty ("gst-amc-codec");
      GValue starr = { 0, };
      gint i;

      gst_structure_set (cs, "name", G_TYPE_STRING, gst_codec_info->name,
          "is-encoder", G_TYPE_BOOLEAN, gst_codec_info->is_encoder, NULL);

      g_value_init (&starr, GST_TYPE_ARRAY);

      for (i = 0; i < gst_codec_info->n_supported_types; i++) {
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[i];
        GstStructure *sts = gst_structure_new_empty ("gst-amc-supported-type");
        GValue stv = { 0, };
        GValue tmparr = { 0, };
        gint j;

        gst_structure_set (sts, "mime", G_TYPE_STRING, gst_codec_type->mime,
            NULL);

        g_value_init (&tmparr, GST_TYPE_ARRAY);
        for (j = 0; j < gst_codec_type->n_color_formats; j++) {
          GValue tmp = { 0, };

          g_value_init (&tmp, G_TYPE_INT);
          g_value_set_int (&tmp, gst_codec_type->color_formats[j]);
          gst_value_array_append_value (&tmparr, &tmp);
          g_value_unset (&tmp);
        }
        gst_structure_set_value (sts, "color-formats", &tmparr);
        g_value_unset (&tmparr);

        g_value_init (&tmparr, GST_TYPE_ARRAY);
        for (j = 0; j < gst_codec_type->n_profile_levels; j++) {
          GValue tmparr2 = { 0, };
          GValue tmp = { 0, };

          g_value_init (&tmparr2, GST_TYPE_ARRAY);
          g_value_init (&tmp, G_TYPE_INT);
          g_value_set_int (&tmp, gst_codec_type->profile_levels[j].profile);
          gst_value_array_append_value (&tmparr2, &tmp);
          g_value_set_int (&tmp, gst_codec_type->profile_levels[j].level);
          gst_value_array_append_value (&tmparr2, &tmp);
          gst_value_array_append_value (&tmparr, &tmparr2);
          g_value_unset (&tmp);
          g_value_unset (&tmparr2);
        }
        gst_structure_set_value (sts, "profile-levels", &tmparr);

        g_value_init (&stv, GST_TYPE_STRUCTURE);
        gst_value_set_structure (&stv, sts);
        gst_value_array_append_value (&starr, &stv);
        g_value_unset (&tmparr);
        gst_structure_free (sts);
      }

      gst_structure_set_value (cs, "supported-types", &starr);
      g_value_unset (&starr);

      g_value_init (&cv, GST_TYPE_STRUCTURE);
      gst_value_set_structure (&cv, cs);
      gst_value_array_append_value (&arr, &cv);
      g_value_unset (&cv);
      gst_structure_free (cs);
    }

    gst_structure_set_value (new_cache_data, "codecs", &arr);
    g_value_unset (&arr);

    gst_plugin_set_cache_data (plugin, new_cache_data);
  }

done:
  if (codec_list_class)
    (*env)->DeleteLocalRef (env, codec_list_class);

  return ret;
}

static const struct
{
  gint color_format;
  GstVideoFormat video_format;
} color_format_mapping_table[] = {
  {
  COLOR_FormatYUV420Planar, GST_VIDEO_FORMAT_I420}, {
  COLOR_FormatYUV420SemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_TI_FormatYUV420PackedSemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYUV420SemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka, GST_VIDEO_FORMAT_NV12}
};

static gboolean
accepted_color_formats (GstAmcCodecType * type, gboolean is_encoder)
{
  gint i, j;
  gint accepted = 0, all = type->n_color_formats;

  for (i = 0; i < type->n_color_formats; i++) {
    gboolean found = FALSE;
    /* We ignore this one */
    if (type->color_formats[i] == COLOR_FormatAndroidOpaque)
      all--;

    for (j = 0; j < G_N_ELEMENTS (color_format_mapping_table); j++) {
      if (color_format_mapping_table[j].color_format == type->color_formats[i]) {
        found = TRUE;
        break;
      }
    }

    if (found)
      accepted++;
  }

  if (is_encoder)
    return accepted > 0;
  else
    return accepted == all && all > 0;
}

GstVideoFormat
gst_amc_color_format_to_video_format (gint color_format)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (color_format_mapping_table); i++) {
    if (color_format_mapping_table[i].color_format == color_format)
      return color_format_mapping_table[i].video_format;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

gint
gst_amc_video_format_to_color_format (GstVideoFormat video_format)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (color_format_mapping_table); i++) {
    if (color_format_mapping_table[i].video_format == video_format)
      return color_format_mapping_table[i].color_format;
  }

  return -1;
}

static const struct
{
  gint id;
  const gchar *str;
  const gchar *alt_str;
} avc_profile_mapping_table[] = {
  {
  AVCProfileBaseline, "baseline", "constrained-baseline"}, {
  AVCProfileMain, "main", NULL}, {
  AVCProfileExtended, "extended", NULL}, {
  AVCProfileHigh, "high"}, {
  AVCProfileHigh10, "high-10", "high-10-intra"}, {
  AVCProfileHigh422, "high-4:2:2", "high-4:2:2-intra"}, {
  AVCProfileHigh444, "high-4:4:4", "high-4:4:4-intra"}
};

const gchar *
gst_amc_avc_profile_to_string (gint profile, const gchar ** alternative)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (avc_profile_mapping_table); i++) {
    if (avc_profile_mapping_table[i].id == profile) {
      *alternative = avc_profile_mapping_table[i].alt_str;
      return avc_profile_mapping_table[i].str;
    }
  }

  return NULL;
}

gint
gst_amc_avc_profile_from_string (const gchar * profile)
{
  gint i;

  g_return_val_if_fail (profile != NULL, -1);

  for (i = 0; i < G_N_ELEMENTS (avc_profile_mapping_table); i++) {
    if (strcmp (avc_profile_mapping_table[i].str, profile) == 0)
      return avc_profile_mapping_table[i].id;
  }

  return -1;
}

static const struct
{
  gint id;
  const gchar *str;
} avc_level_mapping_table[] = {
  {
  AVCLevel1, "1"}, {
  AVCLevel1b, "1b"}, {
  AVCLevel11, "1.1"}, {
  AVCLevel12, "1.2"}, {
  AVCLevel13, "1.3"}, {
  AVCLevel2, "2"}, {
  AVCLevel21, "2.1"}, {
  AVCLevel22, "2.2"}, {
  AVCLevel3, "3"}, {
  AVCLevel31, "3.1"}, {
  AVCLevel32, "3.2"}, {
  AVCLevel4, "4"}, {
  AVCLevel41, "4.1"}, {
  AVCLevel42, "4.2"}, {
  AVCLevel5, "5"}, {
  AVCLevel51, "5.1"}
};

const gchar *
gst_amc_avc_level_to_string (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (avc_level_mapping_table); i++) {
    if (avc_level_mapping_table[i].id == level)
      return avc_level_mapping_table[i].str;
  }

  return NULL;
}

gint
gst_amc_avc_level_from_string (const gchar * level)
{
  gint i;

  g_return_val_if_fail (level != NULL, -1);

  for (i = 0; i < G_N_ELEMENTS (avc_level_mapping_table); i++) {
    if (strcmp (avc_level_mapping_table[i].str, level) == 0)
      return avc_level_mapping_table[i].id;
  }

  return -1;
}

static const struct
{
  gint id;
  gint gst_id;
} h263_profile_mapping_table[] = {
  {
  H263ProfileBaseline, 0}, {
  H263ProfileH320Coding, 1}, {
  H263ProfileBackwardCompatible, 2}, {
  H263ProfileISWV2, 3}, {
  H263ProfileISWV3, 4}, {
  H263ProfileHighCompression, 5}, {
  H263ProfileInternet, 6}, {
  H263ProfileInterlace, 7}, {
  H263ProfileHighLatency, 8}
};

gint
gst_amc_h263_profile_to_gst_id (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_profile_mapping_table); i++) {
    if (h263_profile_mapping_table[i].id == profile)
      return h263_profile_mapping_table[i].gst_id;
  }

  return -1;
}

gint
gst_amc_h263_profile_from_gst_id (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_profile_mapping_table); i++) {
    if (h263_profile_mapping_table[i].gst_id == profile)
      return h263_profile_mapping_table[i].id;
  }

  return -1;
}

static const struct
{
  gint id;
  gint gst_id;
} h263_level_mapping_table[] = {
  {
  H263Level10, 10}, {
  H263Level20, 20}, {
  H263Level30, 30}, {
  H263Level40, 40}, {
  H263Level50, 50}, {
  H263Level60, 60}, {
  H263Level70, 70}
};

gint
gst_amc_h263_level_to_gst_id (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_level_mapping_table); i++) {
    if (h263_level_mapping_table[i].id == level)
      return h263_level_mapping_table[i].gst_id;
  }

  return -1;
}

gint
gst_amc_h263_level_from_gst_id (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_level_mapping_table); i++) {
    if (h263_level_mapping_table[i].gst_id == level)
      return h263_level_mapping_table[i].id;
  }

  return -1;
}

static const struct
{
  gint id;
  const gchar *str;
} mpeg4_profile_mapping_table[] = {
  {
  MPEG4ProfileSimple, "simple"}, {
  MPEG4ProfileSimpleScalable, "simple-scalable"}, {
  MPEG4ProfileCore, "core"}, {
  MPEG4ProfileMain, "main"}, {
  MPEG4ProfileNbit, "n-bit"}, {
  MPEG4ProfileScalableTexture, "scalable"}, {
  MPEG4ProfileSimpleFace, "simple-face"}, {
  MPEG4ProfileSimpleFBA, "simple-fba"}, {
  MPEG4ProfileBasicAnimated, "basic-animated-texture"}, {
  MPEG4ProfileHybrid, "hybrid"}, {
  MPEG4ProfileAdvancedRealTime, "advanced-real-time"}, {
  MPEG4ProfileCoreScalable, "core-scalable"}, {
  MPEG4ProfileAdvancedCoding, "advanced-coding-efficiency"}, {
  MPEG4ProfileAdvancedCore, "advanced-core"}, {
  MPEG4ProfileAdvancedScalable, "advanced-scalable-texture"}, {
  MPEG4ProfileAdvancedSimple, "advanced-simple"}
};

const gchar *
gst_amc_mpeg4_profile_to_string (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (mpeg4_profile_mapping_table); i++) {
    if (mpeg4_profile_mapping_table[i].id == profile)
      return mpeg4_profile_mapping_table[i].str;
  }

  return NULL;
}

gint
gst_amc_avc_mpeg4_profile_from_string (const gchar * profile)
{
  gint i;

  g_return_val_if_fail (profile != NULL, -1);

  for (i = 0; i < G_N_ELEMENTS (mpeg4_profile_mapping_table); i++) {
    if (strcmp (mpeg4_profile_mapping_table[i].str, profile) == 0)
      return mpeg4_profile_mapping_table[i].id;
  }

  return -1;
}

static const struct
{
  gint id;
  const gchar *str;
} mpeg4_level_mapping_table[] = {
  {
  MPEG4Level0, "0"}, {
  MPEG4Level0b, "0b"}, {
  MPEG4Level1, "1"}, {
  MPEG4Level2, "2"}, {
  MPEG4Level3, "3"}, {
  MPEG4Level4, "4"}, {
  MPEG4Level4a, "4a"}, {
MPEG4Level5, "5"},};

const gchar *
gst_amc_mpeg4_level_to_string (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (mpeg4_level_mapping_table); i++) {
    if (mpeg4_level_mapping_table[i].id == level)
      return mpeg4_level_mapping_table[i].str;
  }

  return NULL;
}

gint
gst_amc_mpeg4_level_from_string (const gchar * level)
{
  gint i;

  g_return_val_if_fail (level != NULL, -1);

  for (i = 0; i < G_N_ELEMENTS (mpeg4_level_mapping_table); i++) {
    if (strcmp (mpeg4_level_mapping_table[i].str, level) == 0)
      return mpeg4_level_mapping_table[i].id;
  }

  return -1;
}

static const struct
{
  gint id;
  const gchar *str;
} aac_profile_mapping_table[] = {
  {
  AACObjectMain, "main"}, {
  AACObjectLC, "lc"}, {
  AACObjectSSR, "ssr"}, {
  AACObjectLTP, "ltp"}
};

const gchar *
gst_amc_aac_profile_to_string (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (aac_profile_mapping_table); i++) {
    if (aac_profile_mapping_table[i].id == profile)
      return aac_profile_mapping_table[i].str;
  }

  return NULL;
}

gint
gst_amc_aac_profile_from_string (const gchar * profile)
{
  gint i;

  g_return_val_if_fail (profile != NULL, -1);

  for (i = 0; i < G_N_ELEMENTS (aac_profile_mapping_table); i++) {
    if (strcmp (aac_profile_mapping_table[i].str, profile) == 0)
      return aac_profile_mapping_table[i].id;
  }

  return -1;
}

static const struct
{
  guint32 mask;
  GstAudioChannelPosition pos;
} channel_mapping_table[] = {
  {
  CHANNEL_OUT_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  CHANNEL_OUT_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
  CHANNEL_OUT_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
  CHANNEL_OUT_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE1}, {
  CHANNEL_OUT_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
  CHANNEL_OUT_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
  CHANNEL_OUT_FRONT_LEFT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
  CHANNEL_OUT_FRONT_RIGHT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
  CHANNEL_OUT_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
  CHANNEL_OUT_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
  CHANNEL_OUT_SIDE_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
  CHANNEL_OUT_TOP_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_INVALID}
};

gboolean
gst_amc_audio_channel_mask_to_positions (guint32 channel_mask, gint channels,
    GstAudioChannelPosition * pos)
{
  gint i, j;

  if (channel_mask == 0) {
    if (channels == 1) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
      return TRUE;
    }
    if (channels == 2) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      return TRUE;
    }

    /* Now let the guesswork begin, these are the
     * AAC default channel assignments for these numbers
     * of channels */
    if (channels == 3) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER;
    } else if (channels == 4) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_CENTER;
    } else if (channels == 5) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT;
    } else if (channels == 6) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT | CHANNEL_OUT_LOW_FREQUENCY;
    } else if (channels == 8) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT | CHANNEL_OUT_LOW_FREQUENCY |
          CHANNEL_OUT_FRONT_LEFT_OF_CENTER | CHANNEL_OUT_FRONT_RIGHT_OF_CENTER;
    }
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (channel_mapping_table); i++) {
    if ((channel_mask & channel_mapping_table[i].mask)) {
      pos[j++] = channel_mapping_table[i].pos;
      if (channel_mapping_table[i].pos == GST_AUDIO_CHANNEL_POSITION_INVALID) {
        memset (pos, 0, sizeof (GstAudioChannelPosition) * channels);
        GST_ERROR ("Unable to map channel mask 0x%08x",
            channel_mapping_table[i].mask);
        return FALSE;
      }
      if (j == channels)
        break;
    }
  }

  if (j != channels) {
    memset (pos, 0, sizeof (GstAudioChannelPosition) * channels);
    GST_ERROR ("Unable to map all channel positions in mask 0x%08x",
        channel_mask);
    return FALSE;
  }

  return TRUE;
}

guint32
gst_amc_audio_channel_mask_from_positions (GstAudioChannelPosition * positions,
    gint channels)
{
  gint i, j;
  guint32 channel_mask = 0;

  if (channels == 1 && !positions)
    return CHANNEL_OUT_FRONT_CENTER;
  if (channels == 2 && !positions)
    return CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT;

  for (i = 0; i < channels; i++) {
    if (positions[i] == GST_AUDIO_CHANNEL_POSITION_INVALID)
      return 0;

    for (j = 0; j < G_N_ELEMENTS (channel_mapping_table); j++) {
      if (channel_mapping_table[j].pos == positions[i]) {
        channel_mask |= channel_mapping_table[j].mask;
        break;
      }
    }

    if (j == G_N_ELEMENTS (channel_mapping_table)) {
      GST_ERROR ("Unable to map channel position %d", positions[i]);
      return 0;
    }
  }

  return channel_mask;
}

static gchar *
create_type_name (const gchar * parent_name, const gchar * codec_name)
{
  gchar *typified_name;
  gint i, k;
  gint parent_name_len = strlen (parent_name);
  gint codec_name_len = strlen (codec_name);
  gboolean upper = TRUE;

  typified_name = g_new0 (gchar, parent_name_len + 1 + strlen (codec_name) + 1);
  memcpy (typified_name, parent_name, parent_name_len);
  typified_name[parent_name_len] = '-';

  for (i = 0, k = 0; i < codec_name_len; i++) {
    if (g_ascii_isalnum (codec_name[i])) {
      if (upper)
        typified_name[parent_name_len + 1 + k++] =
            g_ascii_toupper (codec_name[i]);
      else
        typified_name[parent_name_len + 1 + k++] =
            g_ascii_tolower (codec_name[i]);

      upper = FALSE;
    } else {
      /* Skip all non-alnum chars and start a new upper case word */
      upper = TRUE;
    }
  }

  return typified_name;
}

static gchar *
create_element_name (gboolean video, gboolean encoder, const gchar * codec_name)
{
#define PREFIX_LEN 10
  static const gchar *prefixes[] = {
    "amcviddec-",
    "amcauddec-",
    "amcvidenc-",
    "amcaudenc-"
  };
  gchar *element_name;
  gint i, k;
  gint codec_name_len = strlen (codec_name);
  const gchar *prefix;

  if (video && !encoder)
    prefix = prefixes[0];
  else if (!video && !encoder)
    prefix = prefixes[1];
  else if (video && encoder)
    prefix = prefixes[2];
  else
    prefix = prefixes[3];

  element_name = g_new0 (gchar, PREFIX_LEN + strlen (codec_name) + 1);
  memcpy (element_name, prefix, PREFIX_LEN);

  for (i = 0, k = 0; i < codec_name_len; i++) {
    if (g_ascii_isalnum (codec_name[i])) {
      element_name[PREFIX_LEN + k++] = g_ascii_tolower (codec_name[i]);
    }
    /* Skip all non-alnum chars */
  }

  return element_name;
}

#undef PREFIX_LEN

static gboolean
register_codecs (GstPlugin * plugin)
{
  gboolean ret = TRUE;
  GList *l;

  GST_DEBUG ("Registering plugins");

  for (l = codec_infos; l; l = l->next) {
    GstAmcCodecInfo *codec_info = l->data;
    gboolean is_audio = FALSE;
    gboolean is_video = FALSE;
    gint i;
    gint n_types;

    GST_DEBUG ("Registering codec '%s'", codec_info->name);
    for (i = 0; i < codec_info->n_supported_types; i++) {
      GstAmcCodecType *codec_type = &codec_info->supported_types[i];

      if (g_str_has_prefix (codec_type->mime, "audio/"))
        is_audio = TRUE;
      else if (g_str_has_prefix (codec_type->mime, "video/"))
        is_video = TRUE;
    }

    n_types = 0;
    if (is_audio)
      n_types++;
    if (is_video)
      n_types++;

    for (i = 0; i < n_types; i++) {
      GTypeQuery type_query;
      GTypeInfo type_info = { 0, };
      GType type, subtype;
      gchar *type_name, *element_name;
      guint rank;

      if (is_video && !codec_info->is_encoder) {
        type = gst_amc_video_dec_get_type ();
      } else if (is_audio && !codec_info->is_encoder) {
        type = gst_amc_audio_dec_get_type ();
      } else {
        GST_DEBUG ("Skipping unsupported codec type");
        continue;
      }

      g_type_query (type, &type_query);
      memset (&type_info, 0, sizeof (type_info));
      type_info.class_size = type_query.class_size;
      type_info.instance_size = type_query.instance_size;
      type_name = create_type_name (type_query.type_name, codec_info->name);

      if (g_type_from_name (type_name) != G_TYPE_INVALID) {
        GST_ERROR ("Type '%s' already exists for codec '%s'", type_name,
            codec_info->name);
        g_free (type_name);
        continue;
      }

      subtype = g_type_register_static (type, type_name, &type_info, 0);
      g_free (type_name);

      g_type_set_qdata (subtype, gst_amc_codec_info_quark, codec_info);

      element_name =
          create_element_name (is_video, codec_info->is_encoder,
          codec_info->name);

      /* Give the Google software codec a secondary rank,
       * everything else is likely a hardware codec */
      if (g_str_has_prefix (codec_info->name, "OMX.google"))
        rank = GST_RANK_SECONDARY;
      else
        rank = GST_RANK_PRIMARY;

      ret |= gst_element_register (plugin, element_name, rank, subtype);
      g_free (element_name);

      is_video = FALSE;
    }
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  const gchar *ignore;

  GST_DEBUG_CATEGORY_INIT (gst_amc_debug, "amc", 0, "android-media-codec");

  pthread_key_create (&current_jni_env, gst_amc_detach_current_thread);

  if (!initialize_java_vm ())
    return FALSE;

  gst_plugin_add_dependency_simple (plugin, NULL, "/etc", "media_codecs.xml",
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  if (!get_java_classes ())
    return FALSE;

  /* Set this to TRUE to allow registering decoders that have
   * any unknown color formats, or encoders that only have
   * unknown color formats
   */
  ignore = g_getenv ("GST_AMC_IGNORE_UNKNOWN_COLOR_FORMATS");
  if (ignore && strcmp (ignore, "yes") == 0)
    ignore_unknown_color_formats = TRUE;

  if (!scan_codecs (plugin))
    return FALSE;

  gst_amc_codec_info_quark = g_quark_from_static_string ("gst-amc-codec-info");

  if (!register_codecs (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    androidmedia,
    "Android Media plugin",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
