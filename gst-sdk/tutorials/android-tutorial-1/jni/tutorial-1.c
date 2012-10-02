#include <string.h>
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <gst/gst.h>
#include <pthread.h>
#include <gst/interfaces/xoverlay.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

/*
 * These macros provide a way to store the native pointer to CustomData, which might be 32 or 64 bits, into
 * a jlong, which is always 64 bits, without warnings.
 */
#if GLIB_SIZEOF_VOID_P == 8
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)data)
#else
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(jint)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(jint)data)
#endif

typedef struct _CustomData {
  jobject app;
  GstElement *pipeline;
  GMainContext *context;
  GMainLoop *main_loop;
  ANativeWindow *native_window;
  GstState state, target_state;
  gint64 position;
  gint64 duration;
  gint64 desired_position;
  GstClockTime last_seek_time;
  gboolean initialized;
  gboolean is_live;
} CustomData;

static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;
static jmethodID set_current_position_method_id;
static jmethodID set_current_state_method_id;
static jmethodID on_gstreamer_initialized_method_id;

/*
 * Private methods
 */
static JNIEnv *attach_current_thread (void) {
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

static void detach_current_thread (void *env) {
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

static JNIEnv *get_jni_env (void) {
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

static void set_ui_message (const gchar *message, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting message to: %s", message);
  jstring jmessage = (*env)->NewStringUTF(env, message);
  (*env)->CallVoidMethod (env, data->app, set_message_method_id, jmessage);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
  (*env)->DeleteLocalRef (env, jmessage);
}

static void set_current_ui_position (gint position, gint duration, CustomData *data) {
  JNIEnv *env = get_jni_env ();
//  GST_DEBUG ("Setting current position/duration to: %d / %d (ms)", position, duration);
  (*env)->CallVoidMethod (env, data->app, set_current_position_method_id, position, duration);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
}

static gboolean refresh_ui (CustomData *data) {
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 current = -1;

  /* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
  if (!data || !data->pipeline || data->state < GST_STATE_PAUSED)
    return TRUE;

  /* If we didn't know it yet, query the stream duration */
  if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    if (!gst_element_query_duration (data->pipeline, &fmt, &data->duration)) {
      GST_WARNING ("Could not query current duration");
    }
  }

  if (gst_element_query_position (data->pipeline, &fmt, &data->position)) {
    /* Java expects these values in milliseconds, and Gst provides nanoseconds */
    set_current_ui_position (data->position / GST_MSECOND, data->duration / GST_MSECOND, data);
  }
  return TRUE;
}

static void execute_seek (gint64 desired_position, CustomData *data);
static gboolean
delayed_seek_cb (CustomData *data)
{
  GST_DEBUG ("Doing delayed seek %" GST_TIME_FORMAT, GST_TIME_ARGS (data->desired_position));
  data->last_seek_time = GST_CLOCK_TIME_NONE;
  execute_seek (data->desired_position, data);
  return FALSE;
}

static void execute_seek (gint64 desired_position, CustomData *data) {
  gboolean res;
  gint64 diff;

  if (desired_position == GST_CLOCK_TIME_NONE)
    return;

  diff = gst_util_get_timestamp () - data->last_seek_time;

  if (GST_CLOCK_TIME_IS_VALID (data->last_seek_time) && diff < 500 * GST_MSECOND) {
    GSource *timeout_source;
    
    if (!GST_CLOCK_TIME_IS_VALID (data->desired_position)) {
      timeout_source = g_timeout_source_new (diff / GST_MSECOND);
      g_source_attach (timeout_source, data->context);
      g_source_set_callback (timeout_source, (GSourceFunc)delayed_seek_cb, data, NULL);
      g_source_unref (timeout_source);
    }
    data->desired_position = desired_position;
    GST_DEBUG ("Throttling seek to %" GST_TIME_FORMAT ", will be in %" GST_TIME_FORMAT,
        GST_TIME_ARGS (desired_position), GST_TIME_ARGS (500 * GST_MSECOND - diff));
  } else {
    GST_DEBUG ("Setting position to %lld milliseconds", desired_position / GST_MSECOND);
    res = gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, desired_position);
    data->last_seek_time = gst_util_get_timestamp ();
    data->desired_position = GST_CLOCK_TIME_NONE;
    GST_DEBUG ("Seek returned %d", res);
  }
}

static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  message_string = g_strdup_printf ("Error received from element %s: %s", GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);
  set_ui_message (message_string, data);
  g_free (message_string);
  data->target_state = GST_STATE_NULL;
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  set_ui_message (GST_MESSAGE_TYPE_NAME (msg), data);
  refresh_ui (data);
  data->target_state = GST_STATE_PAUSED;
  data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
  execute_seek (0, data);
}

static void duration_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  data->duration = GST_CLOCK_TIME_NONE;
}

static void buffering_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  gint percent;

  if (data->is_live)
    return;

  gst_message_parse_buffering (msg, &percent);
  if (percent < 100 && data->target_state >= GST_STATE_PAUSED) {
    gchar * message_string = g_strdup_printf ("Buffering %d %%", percent);
    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
    set_ui_message (message_string, data);
    g_free (message_string);
  } else if (data->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
    set_ui_message ("PLAYING", data);
  } else if (data->target_state >= GST_STATE_PAUSED) {
    set_ui_message ("PAUSED", data);
  }
}

static void clock_lost_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  if (data->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
    gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  }
}

static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    data->state = new_state;
    if (data->state >= GST_STATE_PAUSED && GST_CLOCK_TIME_IS_VALID (data->desired_position))
      execute_seek (data->desired_position, data);
    GST_DEBUG ("State changed to %s, notifying application", gst_element_state_get_name(new_state));
    (*env)->CallVoidMethod (env, data->app, set_current_state_method_id, new_state);
    if ((*env)->ExceptionCheck (env)) {
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
  }
}

static void check_initialization_complete (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  /* Check if all conditions are met to report GStreamer as initialized.
   * These conditions will change depending on the application */
  if (!data->initialized && data->native_window && data->main_loop) {
    GST_DEBUG ("Initialization complete, notifying application. native_window:%p main_loop:%p", data->native_window,data->main_loop);
    (*env)->CallVoidMethod (env, data->app, on_gstreamer_initialized_method_id);
    if ((*env)->ExceptionCheck (env)) {
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
    data->initialized = TRUE;
  }
}

static void *app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  GstMessage *msg;
  CustomData *data = (CustomData *)userdata;
  GSource *timeout_source;
  GSource *bus_source;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* create our own GLib Main Context, so we do not interfere with other libraries using GLib */
  data->context = g_main_context_new ();

  data->pipeline = gst_element_factory_make ("playbin2", NULL);

  if (0) {
	  GstElement *fakesink;
	  fakesink = gst_element_factory_make ("fakesink", NULL);
	  g_object_set (fakesink, "sync", TRUE, NULL);
	  g_object_set (data->pipeline, "video-sink", fakesink, NULL);
  }

  if (data->native_window) {
    GST_DEBUG ("Native window already received, notifying the pipeline about it.");
    gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->pipeline), (guintptr)data->native_window);
  }

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
  g_source_attach (bus_source, data->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::duration", (GCallback)duration_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::buffering", (GCallback)buffering_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::clock-lost", (GCallback)clock_lost_cb, data);
  gst_object_unref (bus);

  /* Register a function that GLib will call 4 times per second */
  timeout_source = g_timeout_source_new (250);
  g_source_attach (timeout_source, data->context);
  g_source_set_callback (timeout_source, (GSourceFunc)refresh_ui, data, NULL);
  g_source_unref (timeout_source);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop... (CustomData:%p)", data);
  data->main_loop = g_main_loop_new (data->context, FALSE);
  check_initialization_complete (data);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (data->main_loop);
  data->main_loop = NULL;

  /* Free resources */
  g_main_context_unref (data->context);
  data->target_state = GST_STATE_NULL;
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->pipeline);

  return NULL;
}

/*
 * Java Bindings
 */
void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = g_new0 (CustomData, 1);
  data->duration = GST_CLOCK_TIME_NONE;
  data->desired_position = GST_CLOCK_TIME_NONE;
  data->last_seek_time = GST_CLOCK_TIME_NONE;
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG ("Created CustomData at %p", data);
  data->app = (*env)->NewGlobalRef (env, thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);
  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

void gst_native_finalize (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Quitting main loop...");
  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");
  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Deleting GlobalRef for app object at %p", data->app);
  (*env)->DeleteGlobalRef (env, data->app);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

void gst_native_set_uri (JNIEnv* env, jobject thiz, jstring uri) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data || !data->pipeline) return;
  const jbyte *char_uri = (*env)->GetStringUTFChars (env, uri, NULL);
  GST_DEBUG ("Setting URI to %s", char_uri);
  if (data->target_state >= GST_STATE_READY)
	gst_element_set_state (data->pipeline, GST_STATE_READY);
  g_object_set(data->pipeline, "uri", char_uri, NULL);
  (*env)->ReleaseStringUTFChars (env, uri, char_uri);
  data->is_live = (gst_element_set_state (data->pipeline, data->target_state) == GST_STATE_CHANGE_NO_PREROLL);
}

void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PLAYING");
  data->target_state = GST_STATE_PLAYING;
  data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
}

void gst_native_pause (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PAUSED");
  data->target_state = GST_STATE_PAUSED;
  data->is_live = (gst_element_set_state (data->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
}

void gst_native_set_position (JNIEnv* env, jobject thiz, int milliseconds) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  gint64 desired_position = (gint64)(milliseconds * GST_MSECOND);
  if (data->state == GST_STATE_PLAYING || data->state == GST_STATE_PAUSED) {
    execute_seek(desired_position, data);
  } else {
    GST_DEBUG ("Scheduling seek to %d milliseconds for later", milliseconds);
    data->desired_position = desired_position;
  }
}

jboolean gst_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  GST_DEBUG ("The FieldID for the native_custom_data field is %p", custom_data_field_id);
  set_message_method_id = (*env)->GetMethodID (env, klass, "setMessage", "(Ljava/lang/String;)V");
  GST_DEBUG ("The MethodID for the setMessage method is %p", set_message_method_id);
  set_current_position_method_id = (*env)->GetMethodID (env, klass, "setCurrentPosition", "(II)V");
  GST_DEBUG ("The MethodID for the setCurrentPosition method is %p", set_current_position_method_id);
  on_gstreamer_initialized_method_id = (*env)->GetMethodID (env, klass, "onGStreamerInitialized", "()V");
  GST_DEBUG ("The MethodID for the onGStreamerInitialized method is %p", on_gstreamer_initialized_method_id);
  set_current_state_method_id = (*env)->GetMethodID (env, klass, "setCurrentState", "(I)V");
  GST_DEBUG ("The MethodID for the setCurrentState method is %p", set_current_state_method_id);

  if (!custom_data_field_id || !set_message_method_id || !set_current_position_method_id ||
      !on_gstreamer_initialized_method_id || !set_current_state_method_id) {
    GST_ERROR ("The calling class does not implement all necessary interface methods");
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

void gst_native_surface_init (JNIEnv *env, jobject thiz, jobject surface) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Received surface %p", surface);
  if (data->native_window) {
    GST_DEBUG ("Releasing previous native window %p", data->native_window);
    ANativeWindow_release (data->native_window);
  }
  data->native_window = ANativeWindow_fromSurface(env, surface);
  GST_DEBUG ("Got Native Window %p", data->native_window);

  if (data->pipeline) {
    GST_DEBUG ("Pipeline already created, notifying the it about the native window.");
    gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->pipeline), (guintptr)data->native_window);
  } else {
    GST_DEBUG ("Pipeline not created yet, it will later be notified about the native window.");
  }

  check_initialization_complete (data);
}

void gst_native_surface_finalize (JNIEnv *env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) {
    GST_WARNING ("Received surface finalize but there is no CustomData. Ignoring.");
    return;
  }
  GST_DEBUG ("Releasing Native Window %p", data->native_window);

  if (data->pipeline) {
	gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->pipeline), (guintptr)NULL);
	gst_element_set_state (data->pipeline, GST_STATE_NULL);
  }

  ANativeWindow_release (data->native_window);
  data->native_window = NULL;
}

static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
  { "nativeSetUri", "(Ljava/lang/String;)V", (void *) gst_native_set_uri},
  { "nativePlay", "()V", (void *) gst_native_play},
  { "nativePause", "()V", (void *) gst_native_pause},
  { "nativeSetPosition", "(I)V", (void*) gst_native_set_position},
  { "classInit", "()Z", (void *) gst_class_init},
  { "nativeSurfaceInit", "(Ljava/lang/Object;)V", (void *) gst_native_surface_init},
  { "nativeSurfaceFinalize", "()V", (void *) gst_native_surface_finalize}
};

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;

  GST_DEBUG_CATEGORY_INIT (debug_category, "tutorial-1", 0, "Android tutorial 1");

  java_vm = vm;

  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    GST_ERROR ("Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "com/gst_sdk_tutorials/tutorial_1/Tutorial1");
  (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS(native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
