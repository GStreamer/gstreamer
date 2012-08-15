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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamc.h"

#include <gst/gst.h>
#include <string.h>
#include <jni.h>

GST_DEBUG_CATEGORY (gst_amc_debug);
#define GST_CAT_DEFAULT gst_amc_debug

static GModule *java_module;
static jint (*get_created_java_vms) (JavaVM ** vmBuf, jsize bufLen,
    jsize * nVMs);
static jint (*create_java_vm) (JavaVM ** p_vm, JNIEnv ** p_env, void *vm_args);
static JavaVM *java_vm;

static JNIEnv *
gst_amc_attach_current_thread (void)
{
  JNIEnv *env;
  JavaVMAttachArgs args;

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
gst_amc_detach_current_thread (void)
{
  (*java_vm)->DetachCurrentThread (java_vm);
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
    JavaVMOption options[1];

    /* FIXME: Do we need any options here? Like exit()
     * handler, or classpaths? */
    vm_args.version = JNI_VERSION_1_6;
    vm_args.options = options;
    vm_args.nOptions = 0;
    vm_args.ignoreUnrecognized = JNI_TRUE;
    if (create_java_vm (&java_vm, &env, &vm_args) < 0)
      goto create_failed;
    GST_DEBUG ("Successfully created Java VM %p", java_vm);
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
  return NULL;
}

void
gst_amc_codec_free (GstAmcCodec * codec)
{

}

void
gst_amc_codec_configure (GstAmcCodec * codec, gint flags)
{

}

GstAmcFormat *
gst_amc_codec_get_output_format (GstAmcCodec * codec)
{
  return NULL;
}

void
gst_amc_codec_start (GstAmcCodec * codec)
{

}

void
gst_amc_codec_stop (GstAmcCodec * codec)
{

}

void
gst_amc_codec_flush (GstAmcCodec * codec)
{

}

GstAmcBuffer *
gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  return NULL;
}

GstAmcBuffer *
gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  return NULL;
}

gint
gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs)
{
  return -1;
}

gint
gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec,
    GstAmcBufferInfo * info, gint64 timeoutUs)
{
  return -1;
}

void
gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info)
{

}

void
gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index)
{

}

GstAmcFormat *
gst_amc_format_new_audio (const gchar * mime, gint sample_rate, gint channels)
{
  return NULL;
}

GstAmcFormat *
gst_amc_format_new_video (const gchar * mime, gint width, gint height)
{
  return NULL;
}

void
gst_amc_format_free (GstAmcFormat * format)
{

}

gboolean
gst_amc_format_contains_key (GstAmcFormat * format, const gchar * key)
{
  return FALSE;
}

gboolean
gst_amc_format_get_float (GstAmcFormat * format, const gchar * key,
    gfloat * value)
{
  return FALSE;
}

void
gst_amc_format_set_float (GstAmcFormat * format, const gchar * key,
    gfloat * value)
{

}

gboolean
gst_amc_format_get_int (GstAmcFormat * format, const gchar * key, gint * value)
{
  return FALSE;
}

void
gst_amc_format_set_int (GstAmcFormat * format, const gchar * key, gint * value)
{

}

gboolean
gst_amc_format_get_string (GstAmcFormat * format, const gchar * key,
    gchar ** value)
{
  return FALSE;
}

void
gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value)
{

}

static GList *codec_infos = NULL;

static gboolean
register_codecs (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;
  jclass codec_list_class = NULL;
  jmethodID get_codec_count_id, get_codec_info_at_id;
  jint codec_count, i;

  env = gst_amc_attach_current_thread ();

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
    gboolean valid_type = FALSE;

    gst_codec_info = g_new0 (GstAmcCodecInfo, 1);

    codec_info =
        (*env)->CallStaticObjectMethod (env, codec_list_class,
        get_codec_info_at_id, i);
    if ((*env)->ExceptionCheck (env) || !codec_info) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info %d", i);
      goto next_codec;
    }

    codec_info_class = (*env)->GetObjectClass (env, codec_info);
    if (!codec_list_class) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info class");
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
      goto next_codec;
    }

    name = (*env)->CallObjectMethod (env, codec_info, get_name_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec name");
      goto next_codec;
    }
    name_str = (*env)->GetStringUTFChars (env, name, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to convert codec name to UTF8");
      goto next_codec;
    }

    GST_INFO ("Checking codec '%s'", name_str);

    /* Compatibility codec names */
    if (strcmp (name_str, "AACEncoder") == 0 ||
        strcmp (name_str, "OMX.google.raw.decoder") == 0) {
      GST_INFO ("Skipping compatibility codec '%s'", name_str);
      goto next_codec;
    }
    gst_codec_info->name = g_strdup (name_str);

    is_encoder = (*env)->CallBooleanMethod (env, codec_info, is_encoder_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to detect if codec is an encoder");
      goto next_codec;
    }
    gst_codec_info->is_encoder = is_encoder;

    supported_types =
        (*env)->CallObjectMethod (env, codec_info, get_supported_types_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types");
      goto next_codec;
    }

    n_supported_types = (*env)->GetArrayLength (env, supported_types);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types array length");
      goto next_codec;
    }

    GST_INFO ("Codec '%s' has %d supported types", name_str, n_supported_types);

    gst_codec_info->supported_types = g_new0 (GstAmcCodecType, 1);
    gst_codec_info->n_supported_types = n_supported_types;

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
        goto next_supported_type;
      }

      supported_type_str =
          (*env)->GetStringUTFChars (env, supported_type, NULL);
      if ((*env)->ExceptionCheck (env) || !supported_type_str) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to convert supported type to UTF8");
        goto next_supported_type;
      }

      GST_INFO ("Supported type '%s'", supported_type_str);
      gst_codec_type->mime = g_strdup (supported_type_str);
      valid_type = TRUE;

      capabilities =
          (*env)->CallObjectMethod (env, codec_info,
          get_capabilities_for_type_id, supported_type);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities for supported type");
        goto next_supported_type;
      }

      capabilities_class = (*env)->GetObjectClass (env, capabilities);
      if (!capabilities_class) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities class");
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
        goto next_supported_type;
      }

      color_formats =
          (*env)->GetObjectField (env, capabilities, color_formats_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color formats");
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, color_formats);
      gst_codec_type->n_color_formats = n_elems;
      gst_codec_type->color_formats = g_new0 (gint, n_elems);
      color_formats_elems =
          (*env)->GetIntArrayElements (env, color_formats, NULL);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color format elements");
        goto next_supported_type;
      }

      for (k = 0; k < n_elems; k++) {
        GST_INFO ("Color format %d: %d", k, color_formats_elems[k]);
        gst_codec_type->color_formats[k] = color_formats_elems[k];
      }

      profile_levels =
          (*env)->GetObjectField (env, capabilities, profile_levels_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get profile/levels");
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, profile_levels);
      gst_codec_type->n_profile_levels = n_elems;
      gst_codec_type->profile_levels =
          g_malloc0 (sizeof (gst_codec_type->profile_levels) * n_elems);
      for (k = 0; k < n_elems; k++) {
        jobject profile_level = NULL;
        jclass profile_level_class = NULL;
        jfieldID level_id, profile_id;
        jint level, profile;

        profile_level = (*env)->GetObjectArrayElement (env, profile_levels, k);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get %d-th profile/level", k);
          goto next_profile_level;
        }

        profile_level_class = (*env)->GetObjectClass (env, profile_level);
        if (!profile_level_class) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level class");
          goto next_profile_level;
        }

        level_id = (*env)->GetFieldID (env, profile_level_class, "level", "I");
        profile_id =
            (*env)->GetFieldID (env, profile_level_class, "profile", "I");
        if (!level_id || !profile_id) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level field IDs");
          goto next_profile_level;
        }

        level = (*env)->GetIntField (env, profile_level, level_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get level");
          goto next_profile_level;
        }
        GST_INFO ("Level %d: 0x%08x", k, level);
        gst_codec_type->profile_levels[k].level = level;

        profile = (*env)->GetIntField (env, profile_level, profile_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile");
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
    }

    /* We need at least a valid supported type */
    if (valid_type) {
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
    valid_type = FALSE;
  }

  ret = codec_infos != NULL;

done:
  if (codec_list_class)
    (*env)->DeleteLocalRef (env, codec_list_class);

  gst_amc_detach_current_thread ();

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_amc_debug, "amc", 0, "android-media-codec");

  if (!initialize_java_vm ())
    return FALSE;

  if (!register_codecs ())
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "androidmediacodec",
    "GStreamer Android MediaCodec Plug-ins",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
