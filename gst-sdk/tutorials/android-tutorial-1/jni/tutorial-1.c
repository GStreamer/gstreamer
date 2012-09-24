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
  GMainLoop *main_loop;
  ANativeWindow *native_window;
  gboolean state;
  gint64 position;
  gint64 duration;
  GstElement *vsink;
  gint64 desired_position;
  gboolean initialized;
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

static void execute_seek (gint64 desired_position, CustomData *data) {
  gboolean res;
  GST_DEBUG ("Setting position to %lld milliseconds", desired_position / GST_MSECOND);
  res = gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, desired_position);
  GST_DEBUG ("Seek returned %d", res);
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
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  set_ui_message (GST_MESSAGE_TYPE_NAME (msg), data);
  refresh_ui (data);
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
  execute_seek (0, data);
}

static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    data->state = new_state;
    if (data->state >= GST_STATE_PAUSED && GST_CLOCK_TIME_IS_VALID (data->desired_position)) {
      execute_seek (data->desired_position, data);
      data->desired_position = GST_CLOCK_TIME_NONE;
    }
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
    GST_DEBUG ("Initialization complete, notifying application. native_window:%p main_loop:%d", data->native_window,data->main_loop);
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
  GMainContext *context;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* create our own GLib Main Context, so we do not interfere with other libraries using GLib */
  context = g_main_context_new ();

//  data->pipeline = gst_parse_launch ("filesrc location=/sdcard/Movies/sintel_trailer-480p.ogv ! oggdemux ! theoradec ! queue ! ffmpegcolorspace ! eglglessink name=vsink force_rendering_slow=1 can_create_window=0", NULL);
//  data->pipeline = gst_parse_launch ("videotestsrc ! ffmpegcolorspace ! video/x-raw-yuv,format=(fourcc)Y444 ! eglglessink name=vsink", NULL);
  //data->pipeline = gst_parse_launch ("souphttpsrc location=http://docs.gstreamer.com/media/sintel_trailer-480p.webm ! matroskademux ! amcviddec-omxgooglevpxdecoder ! queue ! ffmpegcolorspace ! eglglessink name=vsink", NULL);
  data->pipeline = gst_parse_launch ("playbin2 uri=http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);

  data->vsink = gst_bin_get_by_name (GST_BIN (data->pipeline), "vsink");
  if (!data->vsink)
    data->vsink = gst_object_ref (data->pipeline);
  if (data->native_window) {
    GST_DEBUG ("Native window already received, notifying the vsink about it.");
    gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->vsink), (guintptr)data->native_window);
  }

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
  g_source_attach (bus_source, context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  gst_object_unref (bus);

  /* Register a function that GLib will call 4 times per second */
  timeout_source = g_timeout_source_new (250);
  g_source_attach (timeout_source, context);
  g_source_set_callback (timeout_source, (GSourceFunc)refresh_ui, data, NULL);
  g_source_unref (timeout_source);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop... (CustomData:%p)", data);
  data->main_loop = g_main_loop_new (context, FALSE);
  check_initialization_complete (data);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (data->main_loop);
  data->main_loop = NULL;

  /* Free resources */
  g_main_context_unref (context);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->vsink);
  gst_object_unref (data->pipeline);

  return NULL;
}

/*
 * Java Bindings
 */
void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = (CustomData *)g_malloc0 (sizeof (CustomData));
  data->duration = GST_CLOCK_TIME_NONE;
  data->desired_position = GST_CLOCK_TIME_NONE;
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
  GST_DEBUG ("Deleting GlobalRef at %p", data->app);
  (*env)->DeleteGlobalRef (env, data->app);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

void gst_native_pause (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PAUSED");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
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

  if (data->vsink) {
    GST_DEBUG ("Pipeline already created, notifying the vsink about the native window.");
    gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->vsink), (guintptr)data->native_window);
  } else {
    GST_DEBUG ("Pipeline not created yet, vsink will later be notified about the native window.");
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
  ANativeWindow_release (data->native_window);
  data->native_window = NULL;

  gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->vsink), (guintptr)NULL);
}

static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
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
