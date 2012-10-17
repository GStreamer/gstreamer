/*
 * Copyright (C) 2012, Collabora Ltd.
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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

#include "gst-dvm.h"
#include "gst-android-hardware-camera.h"

static struct
{
  jclass klass;
  jmethodID addCallbackBuffer;
  jmethodID getCameraInfo;
  jmethodID getNumberOfCameras;
  jmethodID getParameters;
  jmethodID lock;
  jmethodID open;
  jmethodID reconnect;
  jmethodID release;
  jmethodID setErrorCallback;
  jmethodID setParameters;
  jmethodID setPreviewCallbackWithBuffer;
  jmethodID setPreviewTexture;
  jmethodID startPreview;
  jmethodID startSmoothZoom;
  jmethodID stopPreview;
  jmethodID stopSmoothZoom;
  jmethodID unlock;
} android_hardware_camera = {0};

static struct
{
  jclass klass;
  jmethodID constructor;
  jfieldID facing;
  jfieldID orientation;
  jint CAMERA_FACING_BACK;
  jint CAMERA_FACING_FRONT;
} android_hardware_camera_camerainfo = {0};
gint CameraInfo_CAMERA_FACING_BACK;
gint CameraInfo_CAMERA_FACING_FRONT;

static struct
{
  jclass klass;
  jfieldID width;
  jfieldID height;
} android_hardware_camera_size = {0};

/* TODO: Add other parameters */
static struct
{
  jclass klass;
  jmethodID flatten;
  jmethodID getPreviewFormat;
  jmethodID getPreviewFpsRange;
  jmethodID getPreviewSize;
  jmethodID getSupportedPreviewFormats;
  jmethodID getSupportedPreviewFpsRange;
  jmethodID getSupportedPreviewSizes;
  jmethodID setPreviewFormat;
  jmethodID setPreviewFpsRange;
  jmethodID setPreviewSize;
  jmethodID unflatten;
} android_hardware_camera_parameters = {0};

static struct
{
  jclass klass;
  jmethodID iterator;
} java_util_list = {0};

static struct
{
  jclass klass;
  jmethodID hasNext;
  jmethodID next;
} java_util_iterator = {0};

static struct
{
  jclass klass;
  jmethodID intValue;
} java_lang_integer = {0};

static struct
{
  jclass klass;
  jmethodID constructor;
} com_gstreamer_gstahccallback = {0};

static void
gst_ah_camera_on_preview_frame (JNIEnv * env, jclass klass, jbyteArray data,
    jobject camera, jlong callback, jlong user_data)
{
  GstAHCPreviewCallback cb = (GstAHCPreviewCallback) (gsize) callback;

  cb (data, (gpointer) (gsize) user_data);
}

static void
gst_ah_camera_on_error (JNIEnv * env, jclass klass, jint error,
    jobject camera, jlong callback, jlong user_data)
{
  GstAHCErrorCallback cb = (GstAHCErrorCallback) (gsize) callback;

  cb (error, (gpointer) (gsize) user_data);
}

static JNINativeMethod native_methods[] = {
  {"gst_ah_camera_on_preview_frame", "([BLandroid/hardware/Camera;JJ)V",
      (void *) gst_ah_camera_on_preview_frame},
  {"gst_ah_camera_on_error", "(ILandroid/hardware/Camera;JJ)V",
      (void *) gst_ah_camera_on_error}
};

static gboolean
_init_classes ()
{
  JNIEnv *env = gst_dvm_get_env ();

  /* android.hardware.Camera */
  GST_DVM_GET_CLASS (android_hardware_camera, "android/hardware/Camera");
  GST_DVM_GET_METHOD (android_hardware_camera, addCallbackBuffer, "([B)V");
  GST_DVM_GET_STATIC_METHOD (android_hardware_camera, getCameraInfo,
      "(ILandroid/hardware/Camera$CameraInfo;)V");
  GST_DVM_GET_STATIC_METHOD (android_hardware_camera, getNumberOfCameras,
      "()I");
  GST_DVM_GET_METHOD (android_hardware_camera, getParameters,
      "()Landroid/hardware/Camera$Parameters;");
  GST_DVM_GET_METHOD (android_hardware_camera, lock, "()V");
  GST_DVM_GET_STATIC_METHOD (android_hardware_camera, open,
      "(I)Landroid/hardware/Camera;");
  GST_DVM_GET_METHOD (android_hardware_camera, reconnect, "()V");
  GST_DVM_GET_METHOD (android_hardware_camera, release, "()V");
  GST_DVM_GET_METHOD (android_hardware_camera, setErrorCallback,
      "(Landroid/hardware/Camera$ErrorCallback;)V");
  GST_DVM_GET_METHOD (android_hardware_camera, setParameters,
      "(Landroid/hardware/Camera$Parameters;)V");
  GST_DVM_GET_METHOD (android_hardware_camera, setPreviewCallbackWithBuffer,
      "(Landroid/hardware/Camera$PreviewCallback;)V");
  GST_DVM_GET_METHOD (android_hardware_camera, setPreviewTexture,
      "(Landroid/graphics/SurfaceTexture;)V");
  GST_DVM_GET_METHOD (android_hardware_camera, startPreview, "()V");
  GST_DVM_GET_METHOD (android_hardware_camera, startSmoothZoom, "(I)V");
  GST_DVM_GET_METHOD (android_hardware_camera, stopPreview, "()V");
  GST_DVM_GET_METHOD (android_hardware_camera, stopSmoothZoom, "()V");
  GST_DVM_GET_METHOD (android_hardware_camera, unlock, "()V");

  /* android.hardware.Camera.CameraInfo */
  GST_DVM_GET_CLASS (android_hardware_camera_camerainfo,
      "android/hardware/Camera$CameraInfo");
  GST_DVM_GET_CONSTRUCTOR (android_hardware_camera_camerainfo,
      constructor, "()V");
  GST_DVM_GET_FIELD (android_hardware_camera_camerainfo, facing, "I");
  GST_DVM_GET_FIELD (android_hardware_camera_camerainfo, orientation, "I");
  GST_DVM_GET_CONSTANT (android_hardware_camera_camerainfo, CAMERA_FACING_BACK,
      Int, "I");
  CameraInfo_CAMERA_FACING_BACK =
      android_hardware_camera_camerainfo.CAMERA_FACING_BACK;
  GST_DVM_GET_CONSTANT (android_hardware_camera_camerainfo, CAMERA_FACING_FRONT,
      Int, "I");
  CameraInfo_CAMERA_FACING_FRONT =
      android_hardware_camera_camerainfo.CAMERA_FACING_FRONT;

  /* android.hardware.Camera.Size */
  GST_DVM_GET_CLASS (android_hardware_camera_size,
      "android/hardware/Camera$Size");
  GST_DVM_GET_FIELD (android_hardware_camera_size, width, "I");
  GST_DVM_GET_FIELD (android_hardware_camera_size, height, "I");

  /* android.hardware.Camera.Parameters */
  GST_DVM_GET_CLASS (android_hardware_camera_parameters,
      "android/hardware/Camera$Parameters");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, flatten,
      "()Ljava/lang/String;");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, getPreviewFormat,
      "()I");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, getPreviewFpsRange,
      "([I)V");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, getPreviewSize,
      "()Landroid/hardware/Camera$Size;");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters,
      getSupportedPreviewFormats, "()Ljava/util/List;");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters,
      getSupportedPreviewFpsRange, "()Ljava/util/List;");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters,
      getSupportedPreviewSizes, "()Ljava/util/List;");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, setPreviewFormat,
      "(I)V");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, setPreviewFpsRange,
      "(II)V");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, setPreviewSize,
      "(II)V");
  GST_DVM_GET_METHOD (android_hardware_camera_parameters, unflatten,
      "(Ljava/lang/String;)V");

  /* java.util.List */
  GST_DVM_GET_CLASS (java_util_list, "java/util/List");
  GST_DVM_GET_METHOD (java_util_list, iterator, "()Ljava/util/Iterator;");

  /* java.util.Iterator */
  GST_DVM_GET_CLASS (java_util_iterator, "java/util/Iterator");
  GST_DVM_GET_METHOD (java_util_iterator, hasNext, "()Z");
  GST_DVM_GET_METHOD (java_util_iterator, next, "()Ljava/lang/Object;");

  /* java.lang.Integer */
  GST_DVM_GET_CLASS (java_lang_integer, "java/lang/Integer");
  GST_DVM_GET_METHOD (java_lang_integer, intValue, "()I");

  /* com.gstreamer.GstAhcCallback */
  GST_DVM_GET_CLASS (com_gstreamer_gstahccallback,
      "com/gstreamer/GstAhcCallback");
  GST_DVM_GET_CONSTRUCTOR (com_gstreamer_gstahccallback, constructor, "(JJ)V");

  if ((*env)->RegisterNatives (env, com_gstreamer_gstahccallback.klass,
          native_methods, G_N_ELEMENTS (native_methods))) {
    GST_ERROR ("Failed to register native methods for GstAhcCallback");
    return FALSE;
  }

  return TRUE;
}


gboolean
gst_android_hardware_camera_init ()
{
  if (!_init_classes ()) {
    gst_android_hardware_camera_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_hardware_camera_deinit ()
{
  JNIEnv *env = gst_dvm_get_env ();

  if (android_hardware_camera.klass)
    (*env)->DeleteGlobalRef (env, android_hardware_camera.klass);
  android_hardware_camera.klass = NULL;

  if (android_hardware_camera_camerainfo.klass)
    (*env)->DeleteGlobalRef (env, android_hardware_camera_camerainfo.klass);
  android_hardware_camera_camerainfo.klass = NULL;

  if (android_hardware_camera_size.klass)
    (*env)->DeleteGlobalRef (env, android_hardware_camera_size.klass);
  android_hardware_camera_size.klass = NULL;

  if (android_hardware_camera_parameters.klass)
    (*env)->DeleteGlobalRef (env, android_hardware_camera_parameters.klass);
  android_hardware_camera_parameters.klass = NULL;

  if (java_util_list.klass)
    (*env)->DeleteGlobalRef (env, java_util_list.klass);
  java_util_list.klass = NULL;

  if (java_util_iterator.klass)
    (*env)->DeleteGlobalRef (env, java_util_iterator.klass);
  java_util_iterator.klass = NULL;

  if (java_lang_integer.klass)
    (*env)->DeleteGlobalRef (env, java_lang_integer.klass);
  java_lang_integer.klass = NULL;

  if (com_gstreamer_gstahccallback.klass) {
    (*env)->UnregisterNatives (env, com_gstreamer_gstahccallback.klass);
    (*env)->DeleteGlobalRef (env, com_gstreamer_gstahccallback.klass);
  }
  com_gstreamer_gstahccallback.klass = NULL;
}

/* android.hardware.Camera */
void
gst_ah_camera_add_callback_buffer (GstAHCamera * self, jbyteArray buffer)
{
  JNIEnv *env = gst_dvm_get_env ();

  /* TODO: use a java.nio.ByteBuffer if possible */
  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.addCallbackBuffer, buffer);
}

gboolean
gst_ah_camera_get_camera_info (gint camera_id, GstAHCCameraInfo * camera_info)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject jcamera_info = NULL;
  gboolean ret = TRUE;

  jcamera_info = (*env)->NewObject (env,
      android_hardware_camera_camerainfo.klass,
      android_hardware_camera_camerainfo.constructor);
  if (!jcamera_info) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

  (*env)->CallStaticVoidMethod (env, android_hardware_camera.klass,
      android_hardware_camera.getCameraInfo, camera_id, jcamera_info);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

  camera_info->facing = (*env)->GetIntField (env, jcamera_info,
      android_hardware_camera_camerainfo.facing);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to get CameraInfo.facing field");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

  camera_info->orientation = (*env)->GetIntField (env, jcamera_info,
      android_hardware_camera_camerainfo.orientation);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to get CameraInfo.orientation field");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:
  if (jcamera_info)
    (*env)->DeleteLocalRef (env, jcamera_info);

  return ret;
}

gint
gst_ah_camera_get_number_of_cameras ()
{
  JNIEnv *env = gst_dvm_get_env ();
  gint num_cameras;

  num_cameras = (*env)->CallStaticIntMethod (env, android_hardware_camera.klass,
      android_hardware_camera.getNumberOfCameras);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return -1;
  }

  return num_cameras;
}

GstAHCParameters *
gst_ah_camera_get_parameters (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object = NULL;
  GstAHCParameters *params = NULL;

  object = (*env)->CallObjectMethod (env, self->object,
      android_hardware_camera.getParameters);
  if ((*env)->ExceptionCheck (env) || !object) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return NULL;
  }

  params = g_slice_new0 (GstAHCParameters);
  params->object = (*env)->NewGlobalRef (env, object);
  if (!params->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAHCParameters, params);
    params = NULL;
  }
  (*env)->DeleteLocalRef (env, object);

  return params;
}

gboolean
gst_ah_camera_lock (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object, android_hardware_camera.lock);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

GstAHCamera *
gst_ah_camera_open (gint camera_id)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object = NULL;
  GstAHCamera *camera = NULL;

  object = (*env)->CallStaticObjectMethod (env, android_hardware_camera.klass,
      android_hardware_camera.open, camera_id);
  if ((*env)->ExceptionCheck (env) || !object) {
    /* TODO: return a GError ? */
    //jthrowable e = (*env)->ExceptionOccurred(env);
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
    return NULL;
  }

  camera = g_slice_new0 (GstAHCamera);
  camera->object = (*env)->NewGlobalRef (env, object);
  if (!camera->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAHCamera, camera);
    camera = NULL;
  }
  (*env)->DeleteLocalRef (env, object);

  return camera;
}

gboolean
gst_ah_camera_reconnect (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object, android_hardware_camera.reconnect);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

void
gst_ah_camera_release (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object, android_hardware_camera.release);

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAHCamera, self);
}

gboolean
gst_ah_camera_set_parameters (GstAHCamera * self, GstAHCParameters * params)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.setParameters, params->object);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ah_camera_set_error_callback (GstAHCamera * self, GstAHCErrorCallback cb,
    gpointer user_data)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object;
  gboolean ret = TRUE;

  object = (*env)->NewObject (env,
      com_gstreamer_gstahccallback.klass,
      com_gstreamer_gstahccallback.constructor,
      *((jlong *) & cb), *((jlong *) & user_data));
  if (!object) {
    GST_ERROR ("Failed to create callback object");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }
  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.setErrorCallback, object);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);

  return ret;
}

gboolean
gst_ah_camera_set_preview_callback_with_buffer (GstAHCamera * self,
    GstAHCPreviewCallback cb, gpointer user_data)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object;
  gboolean ret = TRUE;

  object = (*env)->NewObject (env,
      com_gstreamer_gstahccallback.klass,
      com_gstreamer_gstahccallback.constructor,
      *((jlong *) & cb), *((jlong *) & user_data));
  if (!object) {
    GST_ERROR ("Failed to create callback object");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }
  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.setPreviewCallbackWithBuffer, object);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
    goto done;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);

  return ret;
}

void
gst_ah_camera_set_preview_texture (GstAHCamera * self,
    GstAGSurfaceTexture * surfaceTexture)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.setPreviewTexture, surfaceTexture->object);
}

gboolean
gst_ah_camera_start_preview (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.startPreview);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ah_camera_start_smooth_zoom (GstAHCamera * self, gint value)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.startSmoothZoom, value);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ah_camera_stop_preview (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.stopPreview);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ah_camera_stop_smooth_zoom (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera.stopSmoothZoom);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ah_camera_unlock (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object, android_hardware_camera.unlock);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

/* android.hardware.Camera.Size */
GstAHCSize *
gst_ahc_size_new (gint width, gint height)
{
  GstAHCSize *self = g_slice_new0 (GstAHCSize);

  self->width = width;
  self->height = height;

  return self;
}

void
gst_ahc_size_free (GstAHCSize * self)
{
  g_slice_free (GstAHCSize, self);
}


/* android.hardware.Camera.Parameters */
gchar *
gst_ahc_parameters_flatten (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring v_str = NULL;
  const gchar *v = NULL;
  gchar *ret = NULL;

  v_str = (*env)->CallObjectMethod (env, self->object,
      android_hardware_camera_parameters.flatten);
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

gint
gst_ahc_parameters_get_preview_format (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  gint format;

  format = (*env)->CallIntMethod (env, self->object,
      android_hardware_camera_parameters.getPreviewFormat);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return 0;
  }

  return format;
}

gboolean
gst_ahc_parameters_get_preview_fps_range (GstAHCParameters * self,
    gint * min, gint * max)
{
  JNIEnv *env = gst_dvm_get_env ();
  gboolean ret = TRUE;
  jintArray range = NULL;
  jint *fps = NULL;

  range = (*env)->NewIntArray (env, 2);
  if (!fps) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to create array");
    ret = FALSE;
    goto done;
  }

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera_parameters.getPreviewFpsRange, range);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return 0;
  }

  fps = (*env)->GetIntArrayElements (env, range, NULL);
  if ((*env)->ExceptionCheck (env) || !fps) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get array elements");
    ret = FALSE;
    goto done;
  }
  if (min)
    *min = fps[0];
  if (max)
    *max = fps[1];

done:
  if (fps)
    (*env)->ReleaseIntArrayElements (env, range, fps, JNI_ABORT);
  if (range)
    (*env)->DeleteLocalRef (env, range);

  return ret;
}

GstAHCSize *
gst_ahc_parameters_get_preview_size (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject jsize = NULL;
  GstAHCSize *size = NULL;

  jsize = (*env)->CallObjectMethod (env, self->object,
      android_hardware_camera_parameters.getPreviewSize);
  if ((*env)->ExceptionCheck (env) || !jsize) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  size = g_slice_new0 (GstAHCSize);

  size->width = (*env)->GetIntField (env, jsize,
      android_hardware_camera_size.width);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to get Camera.Size.width field");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAHCSize, size);
    size = NULL;
    goto done;
  }

  size->height = (*env)->GetIntField (env, jsize,
      android_hardware_camera_size.height);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to get Camera.Size.height field");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAHCSize, size);
    size = NULL;
    goto done;
  }

done:
  if (jsize)
    (*env)->DeleteLocalRef (env, jsize);

  return size;
}

static jobject
java_util_list_iterator (JNIEnv * env, jobject obj)
{
  return (*env)->CallObjectMethod (env, obj, java_util_list.iterator);
}

static jobject
java_util_iterator_next (JNIEnv * env, jobject obj)
{
  return (*env)->CallObjectMethod (env, obj, java_util_iterator.next);
}

static jboolean
java_util_iterator_has_next (JNIEnv * env, jobject obj)
{
  return (*env)->CallBooleanMethod (env, obj, java_util_iterator.hasNext);
}

static jint
java_lang_integer_int_value (JNIEnv * env, jobject obj)
{
  return (*env)->CallIntMethod (env, obj, java_lang_integer.intValue);
}

GList *
gst_ahc_parameters_get_supported_preview_formats (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject list = NULL;
  jobject iterator = NULL;
  GList *ret = NULL;

  list = (*env)->CallObjectMethod (env, self->object,
      android_hardware_camera_parameters.getSupportedPreviewFormats);
  if ((*env)->ExceptionCheck (env) || !list) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return NULL;
  }

  iterator = java_util_list_iterator (env, list);
  if (iterator) {
    while (java_util_iterator_has_next (env, iterator)) {
      jobject integer = java_util_iterator_next (env, iterator);

      if (integer) {
        jint value = java_lang_integer_int_value (env, integer);

        ret = g_list_append (ret, GINT_TO_POINTER (value));
        (*env)->DeleteLocalRef (env, integer);
      }
    }
    (*env)->DeleteLocalRef (env, iterator);
  }
  (*env)->DeleteLocalRef (env, list);

  return ret;
}

void
gst_ahc_parameters_supported_preview_formats_free (GList * list)
{
  g_list_free (list);
}

GList *
gst_ahc_parameters_get_supported_preview_fps_range (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject list = NULL;
  jobject iterator = NULL;
  GList *ret = NULL;

  list = (*env)->CallObjectMethod (env, self->object,
      android_hardware_camera_parameters.getSupportedPreviewFpsRange);
  if ((*env)->ExceptionCheck (env) || !list) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return NULL;
  }

  iterator = java_util_list_iterator (env, list);
  if (iterator) {
    while (java_util_iterator_has_next (env, iterator)) {
      jintArray range = java_util_iterator_next (env, iterator);

      if (range) {
        jint *fps = malloc (sizeof (jint) * 2);

        (*env)->GetIntArrayRegion (env, range, 0, 2, fps);
        ret = g_list_append (ret, fps);
        (*env)->DeleteLocalRef (env, range);
      }
    }
    (*env)->DeleteLocalRef (env, iterator);
  }
  (*env)->DeleteLocalRef (env, list);

  return ret;
}

void
gst_ahc_parameters_supported_preview_fps_range_free (GList * list)
{
  g_list_free_full (list, (GDestroyNotify) g_free);
}

GList *
gst_ahc_parameters_get_supported_preview_sizes (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject list = NULL;
  jobject iterator = NULL;
  GList *ret = NULL;

  list = (*env)->CallObjectMethod (env, self->object,
      android_hardware_camera_parameters.getSupportedPreviewSizes);
  if ((*env)->ExceptionCheck (env) || !list) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return NULL;
  }

  iterator = java_util_list_iterator (env, list);
  if (iterator) {
    while (java_util_iterator_has_next (env, iterator)) {
      jobject jsize = java_util_iterator_next (env, iterator);

      if (jsize) {
        jint width, height;

        width = (*env)->GetIntField (env, jsize,
            android_hardware_camera_size.width);
        height = (*env)->GetIntField (env, jsize,
            android_hardware_camera_size.height);

        ret = g_list_append (ret, gst_ahc_size_new (width, height));
        (*env)->DeleteLocalRef (env, jsize);
      }
    }
    (*env)->DeleteLocalRef (env, iterator);
  }
  (*env)->DeleteLocalRef (env, list);

  return ret;
}

void
gst_ahc_parameters_supported_preview_sizes_free (GList * list)
{
  g_list_free_full (list, (GDestroyNotify) gst_ahc_size_free);
}

gboolean
gst_ahc_parameters_set_preview_format (GstAHCParameters * self, gint format)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera_parameters.setPreviewFormat, format);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ahc_parameters_set_preview_fps_range (GstAHCParameters * self,
    gint min, gint max)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera_parameters.setPreviewFpsRange, min, max);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ahc_parameters_set_preview_size (GstAHCParameters * self,
    gint width, gint height)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera_parameters.setPreviewSize, width, height);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_ahc_parameters_unflatten (GstAHCParameters * self, gchar * flattened)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring v_str = NULL;
  gboolean ret = TRUE;

  v_str = (*env)->NewStringUTF (env, flattened);
  if (v_str == NULL)
    return FALSE;

  (*env)->CallVoidMethod (env, self->object,
      android_hardware_camera_parameters.unflatten, v_str);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    ret = FALSE;
  }

  (*env)->DeleteLocalRef (env, v_str);

  return ret;
}

void
gst_ahc_parameters_free (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAHCParameters, self);
}
