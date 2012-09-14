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
  gboolean playing;
  gint64 position;
  gint64 duration;
} CustomData;

static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;
static jmethodID set_current_position_method_id;

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

static void set_message (const gchar *message, CustomData *data) {
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

static void set_current_position (gint64 position, gint64 duration, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting current position/duration to: %lld / %lld (ms)", position, duration);
  (*env)->CallVoidMethod (env, data->app, set_current_position_method_id, position, duration);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
}

static gboolean refresh_ui (CustomData *data) {
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 current = -1;

  /* We do not want to update anything unless we are in the PLAYING state */
  if (!data->playing)
    return TRUE;

  /* If we didn't know it yet, query the stream duration */
  if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    if (!gst_element_query_duration (data->pipeline, &fmt, &data->duration)) {
      GST_WARNING ("Could not query current duration");
    }
  }

  if (gst_element_query_position (data->pipeline, &fmt, &data->position)) {
    /* Java expects these values in milliseconds, and Gst provides nanoseconds */
    set_current_position (data->position/1000000, data->duration/1000000, data);
  }
  return TRUE;
}

static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  message_string = g_strdup_printf ("Error received from element %s: %s", GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);
  set_message (message_string, data);
  g_free (message_string);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  set_message (GST_MESSAGE_TYPE_NAME (msg), data);
  refresh_ui (data);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    set_message (gst_element_state_get_name (new_state), data);
    data->playing = (new_state == GST_STATE_PLAYING);
  }
}

static void new_buffer (GstElement *sink, CustomData *data) {
  GstBuffer *buffer;
   
  /* Retrieve the buffer */
  g_signal_emit_by_name (sink, "pull-buffer", &buffer);
  if (buffer) {
    if (data->native_window) {
      int i;
      ANativeWindow_Buffer nbuff;
      GstStructure *str;
      gint width, height;

      str = gst_caps_get_structure (GST_BUFFER_CAPS (buffer), 0);
      gst_structure_get_int (str, "width", &width);
      gst_structure_get_int (str, "height", &height);

      ANativeWindow_setBuffersGeometry(data->native_window, width, height, WINDOW_FORMAT_RGBX_8888);

      ANativeWindow_lock(data->native_window, &nbuff, NULL);
      for (i=0; i<nbuff.height; i++) {
        memcpy (nbuff.bits + nbuff.stride * 4 * i, GST_BUFFER_DATA (buffer) + width * 4 * i, width * 4);
      }
      ANativeWindow_unlockAndPost (data->native_window);
    }
    gst_buffer_unref (buffer);
  }
}

static void *app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  GstMessage *msg;
  CustomData *data = (CustomData *)userdata;
  GstElement *vsink;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

//  data->pipeline = gst_parse_launch ("videotestsrc ! eglglessink force_rendering_slow=1 can_create_window=0", NULL);
//  data->pipeline = gst_parse_launch ("filesrc location=/sdcard/Movies/sintel_trailer-480p.ogv ! oggdemux ! theoradec ! fakesink", NULL);
//  data->pipeline = gst_parse_launch ("souphttpsrc location=http://docs.gstreamer.com/media/sintel_trailer-480p.ogv ! oggdemux ! theoradec ! fakesink", NULL);
//  data->pipeline = gst_parse_launch ("videotestsrc ! ffmpegcolorspace ! appsink name=vsink emit-signals=1 caps=video/x-raw-rgb,bpp=(int)32,endianness=(int)4321,depth=(int)24,red_mask=(int)-16777216,green_mask=(int)16711680,blue_mask=(int)65280,width=(int)320,height=(int)240,framerate=(fraction)30/1", NULL);
  data->pipeline = gst_parse_launch ("souphttpsrc location=http://docs.gstreamer.com/media/sintel_trailer-480p.ogv ! oggdemux ! theoradec ! ffmpegcolorspace ! appsink name=vsink emit-signals=1 caps=video/x-raw-rgb,bpp=(int)32,endianness=(int)4321,depth=(int)24,red_mask=(int)-16777216,green_mask=(int)16711680,blue_mask=(int)65280", NULL);

  vsink = gst_bin_get_by_name (GST_BIN (data->pipeline), "vsink");
  g_signal_connect (vsink, "new-buffer", G_CALLBACK (new_buffer), data);
  gst_object_unref (vsink);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  gst_object_unref (bus);

  /* Register a function that GLib will call every second */
  g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, data);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop...");
  data->main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exitted main loop");

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->pipeline);
}

/*
 * Java Bindings
 */
void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = (CustomData *)g_malloc0 (sizeof (CustomData));
  data->duration = GST_CLOCK_TIME_NONE;
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG ("Created CustomData at %p", data);
  data->app = (*env)->NewGlobalRef (env, thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);
  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

void gst_native_finalize (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
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
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

void gst_native_pause (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  GST_DEBUG ("Setting state to PAUSED");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

void gst_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  GST_DEBUG ("The FieldID for the native_custom_data field is %p", custom_data_field_id);
  set_message_method_id = (*env)->GetMethodID (env, klass, "setMessage", "(Ljava/lang/String;)V");
  GST_DEBUG ("The MethodID for the setMessage method is %p", set_message_method_id);
  set_current_position_method_id = (*env)->GetMethodID (env, klass, "setCurrentPosition", "(JJ)V");
  GST_DEBUG ("The MethodID for the setCurrentPosition method is %p", set_current_position_method_id);
}

void gst_native_surface_init (JNIEnv *env, jobject thiz, jobject surface) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  GST_DEBUG ("Received surface %p", surface);
  if (data->native_window) {
    GST_DEBUG ("Releasing previous native window %p", data->native_window);
    ANativeWindow_release (data->native_window);
  }
  data->native_window = ANativeWindow_fromSurface(env, surface);
  GST_DEBUG ("Got Native Window %p", data->native_window);

  gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->pipeline), (guintptr)data->native_window);
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

  gst_x_overlay_set_window_handle (GST_X_OVERLAY (data->pipeline), (guintptr)NULL);
}

static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
  { "nativePlay", "()V", (void *) gst_native_play},
  { "nativePause", "()V", (void *) gst_native_pause},
  { "classInit", "()V", (void *) gst_class_init},
  { "nativeSurfaceInit", "(Ljava/lang/Object;)V", (void *) gst_native_surface_init},
  { "nativeSurfaceFinalize", "()V", (void *) gst_native_surface_finalize}
};

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;
  int ret;

  GST_DEBUG_CATEGORY_INIT (debug_category, "tutorial-1", 0, "Android tutorial 1");

  java_vm = vm;

  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    GST_ERROR ("Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "com/gst_sdk_tutorials/tutorial_1/Tutorial1");
  ret = (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS(native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
