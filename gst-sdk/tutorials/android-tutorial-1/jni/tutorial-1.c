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
} CustomData;

static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;

/*
 * Private methods
 */
static JNIEnv *gst_attach_current_thread (void) {
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

static void gst_detach_current_thread (void *env) {
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

static JNIEnv *gst_get_jni_env (void) {
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = gst_attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GST_DEBUG ("Message: %s", GST_MESSAGE_TYPE_NAME (msg));
  JNIEnv *env = gst_get_jni_env ();
  jstring jmessage = (*env)->NewStringUTF(env, GST_MESSAGE_TYPE_NAME (msg));
  (*env)->CallVoidMethod (env, data->app, set_message_method_id, jmessage);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
  (*env)->DeleteLocalRef (env, jmessage);
}

static void *gst_app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  GstMessage *msg;
  CustomData *data = (CustomData *)userdata;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  data->pipeline = gst_parse_launch ("videotestsrc ! eglglessink force_rendering_slow=1 can_create_window=0", NULL);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)error_cb, data);
  gst_object_unref (bus);
  
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
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG ("Created CustomData at %p", data);
  data->app = (*env)->NewGlobalRef (env, thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);
  pthread_create (&gst_app_thread, NULL, &gst_app_function, data);
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
  GST_DEBUG ("Done finalizing");
}

void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

void gst_native_pause (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  GST_DEBUG ("Setting state to READY");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

void gst_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  GST_DEBUG ("The FieldID for the native_custom_data field is %p", custom_data_field_id);
  set_message_method_id = (*env)->GetMethodID (env, klass, "setMessage", "(Ljava/lang/String;)V");
  GST_DEBUG ("The MethodID for the setMessage method is %p", set_message_method_id);
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

  pthread_key_create (&current_jni_env, gst_detach_current_thread);

  return JNI_VERSION_1_4;
}
