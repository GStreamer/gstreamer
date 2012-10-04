#include <string.h>
#include <jni.h>
#include <gst/gst.h>

/*
 * Java Bindings
 */
jstring gst_native_get_gstreamer_info (JNIEnv* env, jobject thiz) {
  return (*env)->NewStringUTF(env, gst_version_string());
}

static JNINativeMethod native_methods[] = {
  { "nativeGetGStreamerInfo", "()Ljava/lang/String;", (void *) gst_native_get_gstreamer_info}
};

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;

  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    GST_ERROR ("Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "com/gst_sdk_tutorials/tutorial_1/Tutorial1");
  (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS(native_methods));

  return JNI_VERSION_1_4;
}
