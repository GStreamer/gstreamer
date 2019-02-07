#include <string.h>
#include <jni.h>
#include <android/log.h>
#include <gst/gst.h>

/*
 * Java Bindings
 */
static jstring
gst_native_get_gstreamer_info (JNIEnv * env, jobject thiz)
{
  char *version_utf8 = gst_version_string ();
  jstring *version_jstring = (*env)->NewStringUTF (env, version_utf8);
  g_free (version_utf8);
  return version_jstring;
}

static JNINativeMethod native_methods[] = {
  {"nativeGetGStreamerInfo", "()Ljava/lang/String;",
      (void *) gst_native_get_gstreamer_info}
};

jint
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "tutorial-1",
        "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env,
      "org/freedesktop/gstreamer/tutorials/tutorial_1/Tutorial1");
  (*env)->RegisterNatives (env, klass, native_methods,
      G_N_ELEMENTS (native_methods));

  return JNI_VERSION_1_4;
}
