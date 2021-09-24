/* GStreamer
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <stdint.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <gst/player/gstplayer.h>
#include <gst/player/gstplayer-video-overlay-video-renderer.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

#define GET_CUSTOM_DATA(env, thiz, fieldID) (Player *)(gintptr)(*env)->GetLongField (env, thiz, fieldID)
#define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(gintptr)data)

typedef struct _Player
{
  jobject java_player;
  GstPlayer *player;
  GstPlayerVideoRenderer *renderer;
  ANativeWindow *native_window;
} Player;

static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID native_player_field_id;
static jmethodID on_position_updated_method_id;
static jmethodID on_duration_changed_method_id;
static jmethodID on_state_changed_method_id;
static jmethodID on_buffering_method_id;
static jmethodID on_end_of_stream_method_id;
static jmethodID on_error_method_id;
static jmethodID on_video_dimensions_changed_method_id;

/* Register this thread with the VM */
static JNIEnv *
attach_current_thread (void)
{
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

/* Unregister this thread from the VM */
static void
detach_current_thread (void *env)
{
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

/* Retrieve the JNI environment for this thread */
static JNIEnv *
get_jni_env (void)
{
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

/*
 * Java Bindings
 */
static void
on_position_updated (GstPlayer * unused, GstClockTime position, Player * player)
{
  JNIEnv *env = get_jni_env ();

  (*env)->CallVoidMethod (env, player->java_player,
      on_position_updated_method_id, position);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }
}

static void
on_duration_changed (GstPlayer * unused, GstClockTime duration, Player * player)
{
  JNIEnv *env = get_jni_env ();

  (*env)->CallVoidMethod (env, player->java_player,
      on_duration_changed_method_id, duration);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }
}

static void
on_state_changed (GstPlayer * unused, GstPlayerState state, Player * player)
{
  JNIEnv *env = get_jni_env ();

  (*env)->CallVoidMethod (env, player->java_player,
      on_state_changed_method_id, state);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }
}

static void
on_buffering (GstPlayer * unused, gint percent, Player * player)
{
  JNIEnv *env = get_jni_env ();

  (*env)->CallVoidMethod (env, player->java_player,
      on_buffering_method_id, percent);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }
}

static void
on_end_of_stream (GstPlayer * unused, Player * player)
{
  JNIEnv *env = get_jni_env ();

  (*env)->CallVoidMethod (env, player->java_player, on_end_of_stream_method_id);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }
}

static void
on_error (GstPlayer * unused, GError * err, Player * player)
{
  JNIEnv *env = get_jni_env ();
  jstring error_msg;

  error_msg = (*env)->NewStringUTF (env, err->message);

  (*env)->CallVoidMethod (env, player->java_player, on_error_method_id,
      err->code, error_msg);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }

  (*env)->DeleteLocalRef (env, error_msg);
}

static void
on_video_dimensions_changed (GstPlayer * unused, gint width, gint height,
    Player * player)
{
  JNIEnv *env = get_jni_env ();

  (*env)->CallVoidMethod (env, player->java_player,
      on_video_dimensions_changed_method_id, width, height);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
  }
}

static void
native_new (JNIEnv * env, jobject thiz)
{
  Player *player = g_new0 (Player, 1);

  player->renderer = gst_player_video_overlay_video_renderer_new (NULL);
  player->player = gst_player_new (player->renderer, NULL);
  SET_CUSTOM_DATA (env, thiz, native_player_field_id, player);
  player->java_player = (*env)->NewGlobalRef (env, thiz);

  g_signal_connect (player->player, "position-updated",
      G_CALLBACK (on_position_updated), player);
  g_signal_connect (player->player, "duration-changed",
      G_CALLBACK (on_duration_changed), player);
  g_signal_connect (player->player, "state-changed",
      G_CALLBACK (on_state_changed), player);
  g_signal_connect (player->player, "buffering",
      G_CALLBACK (on_buffering), player);
  g_signal_connect (player->player, "end-of-stream",
      G_CALLBACK (on_end_of_stream), player);
  g_signal_connect (player->player, "error", G_CALLBACK (on_error), player);
  g_signal_connect (player->player, "video-dimensions-changed",
      G_CALLBACK (on_video_dimensions_changed), player);
}

static void
native_free (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);

  if (!player)
    return;

  g_object_unref (player->player);
  (*env)->DeleteGlobalRef (env, player->java_player);
  g_free (player);
  SET_CUSTOM_DATA (env, thiz, native_player_field_id, NULL);
}

static void
native_play (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);

  if (!player)
    return;

  gst_player_play (player->player);
}

static void
native_pause (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);

  if (!player)
    return;

  gst_player_pause (player->player);
}

static void
native_stop (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);

  if (!player)
    return;

  gst_player_stop (player->player);
}

static void
native_seek (JNIEnv * env, jobject thiz, jlong position)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);

  if (!player)
    return;

  gst_player_seek (player->player, position);
}

static void
native_set_uri (JNIEnv * env, jobject thiz, jobject uri)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);
  const gchar *uri_str;

  if (!player)
    return;

  uri_str = (*env)->GetStringUTFChars (env, uri, NULL);
  g_object_set (player->player, "uri", uri_str, NULL);
  (*env)->ReleaseStringUTFChars (env, uri, uri_str);
}

static jobject
native_get_uri (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);
  jobject uri;
  gchar *uri_str;

  if (!player)
    return NULL;

  g_object_get (player->player, "uri", &uri_str, NULL);

  uri = (*env)->NewStringUTF (env, uri_str);
  g_free (uri_str);

  return uri;
}

static jlong
native_get_position (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);
  jdouble position;

  if (!player)
    return -1;

  g_object_get (player->player, "position", &position, NULL);

  return position;
}

static jlong
native_get_duration (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);
  jlong duration;

  if (!player)
    return -1;

  g_object_get (player->player, "duration", &duration, NULL);

  return duration;
}

static jdouble
native_get_volume (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);
  jdouble volume;

  if (!player)
    return 1.0;

  g_object_get (player->player, "volume", &volume, NULL);

  return volume;
}

static void
native_set_volume (JNIEnv * env, jobject thiz, jdouble volume)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);

  if (!player)
    return;

  g_object_set (player->player, "volume", volume, NULL);
}

static jboolean
native_get_mute (JNIEnv * env, jobject thiz)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);
  jboolean mute;

  if (!player)
    return FALSE;

  g_object_get (player->player, "mute", &mute, NULL);

  return mute;
}

static void
native_set_mute (JNIEnv * env, jobject thiz, jboolean mute)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);

  if (!player)
    return;

  g_object_set (player->player, "mute", mute, NULL);
}

static void
native_set_surface (JNIEnv * env, jobject thiz, jobject surface)
{
  Player *player = GET_CUSTOM_DATA (env, thiz, native_player_field_id);
  ANativeWindow *new_native_window;

  if (!player)
    return;

  new_native_window = surface ? ANativeWindow_fromSurface (env, surface) : NULL;
  GST_DEBUG ("Received surface %p (native window %p)", surface,
      new_native_window);

  if (player->native_window) {
    ANativeWindow_release (player->native_window);
  }

  player->native_window = new_native_window;
  gst_player_video_overlay_video_renderer_set_window_handle
      (GST_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (player->renderer),
      (gpointer) new_native_window);
  gst_player_video_overlay_video_renderer_expose
      (GST_PLAYER_VIDEO_OVERLAY_VIDEO_RENDERER (player->renderer));
}

static void
native_class_init (JNIEnv * env, jclass klass)
{
  native_player_field_id =
      (*env)->GetFieldID (env, klass, "native_player", "J");
  on_position_updated_method_id =
      (*env)->GetMethodID (env, klass, "onPositionUpdated", "(J)V");
  on_duration_changed_method_id =
      (*env)->GetMethodID (env, klass, "onDurationChanged", "(J)V");
  on_state_changed_method_id =
      (*env)->GetMethodID (env, klass, "onStateChanged", "(I)V");
  on_buffering_method_id =
      (*env)->GetMethodID (env, klass, "onBuffering", "(I)V");
  on_end_of_stream_method_id =
      (*env)->GetMethodID (env, klass, "onEndOfStream", "()V");
  on_error_method_id =
      (*env)->GetMethodID (env, klass, "onError", "(ILjava/lang/String;)V");
  on_video_dimensions_changed_method_id =
      (*env)->GetMethodID (env, klass, "onVideoDimensionsChanged", "(II)V");

  if (!native_player_field_id ||
      !on_position_updated_method_id || !on_duration_changed_method_id ||
      !on_state_changed_method_id || !on_buffering_method_id ||
      !on_end_of_stream_method_id ||
      !on_error_method_id || !on_video_dimensions_changed_method_id) {
    static const gchar *message =
        "The calling class does not implement all necessary interface methods";
    jclass exception_class = (*env)->FindClass (env, "java/lang/Exception");
    __android_log_print (ANDROID_LOG_ERROR, "GstPlayer", "%s", message);
    (*env)->ThrowNew (env, exception_class, message);
  }

  gst_debug_set_threshold_for_name ("gst-player", GST_LEVEL_TRACE);
}

/* List of implemented native methods */
static JNINativeMethod native_methods[] = {
  {"nativeClassInit", "()V", (void *) native_class_init},
  {"nativeNew", "()V", (void *) native_new},
  {"nativePlay", "()V", (void *) native_play},
  {"nativePause", "()V", (void *) native_pause},
  {"nativeStop", "()V", (void *) native_stop},
  {"nativeSeek", "(J)V", (void *) native_seek},
  {"nativeFree", "()V", (void *) native_free},
  {"nativeGetUri", "()Ljava/lang/String;", (void *) native_get_uri},
  {"nativeSetUri", "(Ljava/lang/String;)V", (void *) native_set_uri},
  {"nativeGetPosition", "()J", (void *) native_get_position},
  {"nativeGetDuration", "()J", (void *) native_get_duration},
  {"nativeGetVolume", "()D", (void *) native_get_volume},
  {"nativeSetVolume", "(D)V", (void *) native_set_volume},
  {"nativeGetMute", "()Z", (void *) native_get_mute},
  {"nativeSetMute", "(Z)V", (void *) native_set_mute},
  {"nativeSetSurface", "(Landroid/view/Surface;)V",
      (void *) native_set_surface}
};

/* Library initializer */
jint
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;

  java_vm = vm;

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "GstPlayer",
        "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "org/freedesktop/gstreamer/Player");
  if (!klass) {
    __android_log_print (ANDROID_LOG_ERROR, "GstPlayer",
        "Could not retrieve class org.freedesktop.gstreamer.Player");
    return 0;
  }
  if ((*env)->RegisterNatives (env, klass, native_methods,
          G_N_ELEMENTS (native_methods))) {
    __android_log_print (ANDROID_LOG_ERROR, "GstPlayer",
        "Could not register native methods for org.freedesktop.gstreamer.Player");
    return 0;
  }

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
