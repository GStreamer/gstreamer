/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <string.h>
#include <jni.h>
#include <gst/gst.h>
#include <pthread.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

typedef struct _CustomData {
  JNIEnv *env;
  GstElement *pipeline;
  GMainLoop *main_loop;
} CustomData;

static pthread_t gst_app_thread;
static pthread_key_t gst_app_thread_key;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;

/*
 * Private methods
 */
static void gst_detach_current_thread (void *env) {
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
    GST_DEBUG ("Message: %s", GST_MESSAGE_TYPE_NAME (msg));
}

static void *gst_app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  GstMessage *msg;
  CustomData *data = (CustomData *)userdata;

  pthread_key_create (&gst_app_thread_key, gst_detach_current_thread);
  pthread_setspecific (gst_app_thread_key, &data);

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &data->env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return;
  }

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  data->pipeline = gst_parse_launch ("videotestsrc num-buffers=10000 ! fakesink", NULL);

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
  // data = pthread_getspecific (gst_app_thread_key);
}

/*
 * Java Bindings
 */
void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = (CustomData *)g_malloc0 (sizeof (CustomData));
  (*env)->SetLongField (env, thiz, custom_data_field_id, (jlong)data);
  GST_DEBUG ("Created CustomData at %p", data);
  pthread_create (&gst_app_thread, NULL, &gst_app_function, data);
}

void gst_native_finalize (JNIEnv* env, jobject thiz) {
  CustomData *data = (CustomData *)(*env)->GetLongField (env, thiz, custom_data_field_id);
  GST_DEBUG ("Quitting main loop...");
  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");
  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  GST_DEBUG ("Done finalizing");
}

void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = (CustomData *)(*env)->GetLongField (env, thiz, custom_data_field_id);
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

void gst_native_pause (JNIEnv* env, jobject thiz) {
  CustomData *data = (CustomData *)(*env)->GetLongField (env, thiz, custom_data_field_id);
  GST_DEBUG ("Setting state to READY");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

void gst_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  GST_DEBUG ("The FieldID for the native_custom_data field is %p", custom_data_field_id);
}

static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
  { "nativePlay", "()V", (void *) gst_native_play},
  { "nativePause", "()V", (void *) gst_native_pause},
  { "classInit", "()V", (void *) gst_class_init}
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
  ret = (*env)->RegisterNatives (env, klass, native_methods, 5);

  return JNI_VERSION_1_4;
}
