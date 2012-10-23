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
#include "gstahccallback.h"
#include "gst-android-hardware-camera.h"
#include "stdio.h"

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
} android_hardware_camera = {
0};

static struct
{
  jclass klass;
  jmethodID constructor;
  jfieldID facing;
  jfieldID orientation;
  jint CAMERA_FACING_BACK;
  jint CAMERA_FACING_FRONT;
} android_hardware_camera_camerainfo = {
0};

gint CameraInfo_CAMERA_FACING_BACK;
gint CameraInfo_CAMERA_FACING_FRONT;

static struct
{
  jclass klass;
  jfieldID width;
  jfieldID height;
} android_hardware_camera_size = {
0};

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
} android_hardware_camera_parameters = {
0};

static struct
{
  jclass klass;
  jmethodID iterator;
} java_util_list = {
0};

static struct
{
  jclass klass;
  jmethodID hasNext;
  jmethodID next;
} java_util_iterator = {
0};

static struct
{
  jclass klass;
  jmethodID intValue;
} java_lang_integer = {
0};

static struct
{
  jclass klass;
  jmethodID constructor;
} com_gstreamer_gstahccallback = {
0};

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
_init_classes (void)
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
  if (gst_ahc_callback_jar) {
    jclass dex_loader = NULL;
    gchar *path = g_strdup_printf ("%s/GstAhcCallback.jar", g_getenv ("TMP"));
    FILE *fd = fopen (path, "wb");

    GST_WARNING ("Found embedded GstAhcCallback.jar, trying to load dynamically"
        "from %s", path);
    if (fd) {
      if (fwrite (gst_ahc_callback_jar, gst_ahc_callback_jar_size, 1, fd) == 1) {
        dex_loader = (*env)->FindClass (env, "dalvik/system/DexClassLoader");
        (*env)->ExceptionClear (env);
      }
      fclose (fd);
    }

    if (dex_loader) {
      jmethodID constructor;
      jmethodID load_class;

      constructor = (*env)->GetMethodID (env, dex_loader, "<init>",
          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
          "Ljava/lang/ClassLoader;)V");
      load_class = (*env)->GetMethodID (env, dex_loader, "loadClass",
          "(Ljava/lang/String;)Ljava/lang/Class;");
      (*env)->ExceptionClear (env);
      if (constructor && load_class) {
        jstring dex_path = NULL;
        jstring optimized_directory = NULL;

        dex_path = (*env)->NewStringUTF (env, path);
        optimized_directory = (*env)->NewStringUTF (env, g_getenv ("TMP"));
        (*env)->ExceptionClear (env);
        if (dex_path && optimized_directory) {
          jobject loader;
          jobject parent = NULL;
          jclass klass;

          klass = (*env)->FindClass (env, "java/lang/Class");
          (*env)->ExceptionClear (env);
          if (klass) {
            jmethodID get_class_loader;

            get_class_loader = (*env)->GetMethodID (env, klass,
                "getClassLoader", "()Ljava/lang/ClassLoader;");
            (*env)->ExceptionClear (env);
            if (get_class_loader) {
              parent = (*env)->CallObjectMethod (env, klass, get_class_loader);
              (*env)->ExceptionClear (env);
            }
            (*env)->DeleteLocalRef (env, klass);
          }
          loader = (*env)->NewObject (env, dex_loader, constructor, dex_path,
              optimized_directory, NULL, parent);
          (*env)->ExceptionClear (env);
          if (loader) {
            jstring class_name = NULL;

            class_name = (*env)->NewStringUTF (env,
                "com/gstreamer/GstAhcCallback");
            (*env)->ExceptionClear (env);
            if (class_name) {
              jclass temp;

              temp = (*env)->CallObjectMethod (env, loader, load_class,
                  class_name);
              (*env)->ExceptionClear (env);
              if (temp) {

                GST_WARNING ("Successfully loaded embedded GstAhcCallback");
                com_gstreamer_gstahccallback.klass = (*env)->NewGlobalRef (env,
                    temp);
                (*env)->DeleteLocalRef (env, temp);
              }
              (*env)->DeleteLocalRef (env, class_name);
            }
            (*env)->DeleteLocalRef (env, loader);
          }
          if (parent)
            (*env)->DeleteLocalRef (env, parent);
        }
        if (dex_path)
          (*env)->DeleteLocalRef (env, dex_path);
        if (optimized_directory)
          (*env)->DeleteLocalRef (env, optimized_directory);
      }
      (*env)->DeleteLocalRef (env, dex_loader);
      g_free (path);
    }
  } else {
    GST_WARNING ("Did not find embedded GstAhcCallback.jar, fallback to"
        " FindClass");
  }
  if (!com_gstreamer_gstahccallback.klass) {
    GST_DVM_GET_CLASS (com_gstreamer_gstahccallback,
        "com/gstreamer/GstAhcCallback");
  }
  GST_DVM_GET_CONSTRUCTOR (com_gstreamer_gstahccallback, constructor, "(JJ)V");

  if ((*env)->RegisterNatives (env, com_gstreamer_gstahccallback.klass,
          native_methods, G_N_ELEMENTS (native_methods))) {
    GST_ERROR ("Failed to register native methods for GstAhcCallback");
    return FALSE;
  }

  return TRUE;
}


gboolean
gst_android_hardware_camera_init (void)
{
  if (!_init_classes ()) {
    gst_android_hardware_camera_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_hardware_camera_deinit (void)
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
#define AHC_CALL(error_statement, type, method, ...)                    \
  GST_DVM_CALL (error_statement, self->object, type, android_hardware_camera, \
      method, ## __VA_ARGS__);
#define AHC_STATIC_CALL(error_statement, type, method, ...)             \
  GST_DVM_STATIC_CALL (error_statement, type, android_hardware_camera,  \
      method, ## __VA_ARGS__);

void
gst_ah_camera_add_callback_buffer (GstAHCamera * self, jbyteArray buffer)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (, Void, addCallbackBuffer, buffer);
}

gboolean
gst_ah_camera_get_camera_info (gint camera_id, GstAHCCameraInfo * camera_info)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject jcamera_info = NULL;
  gboolean ret = FALSE;

  jcamera_info = (*env)->NewObject (env,
      android_hardware_camera_camerainfo.klass,
      android_hardware_camera_camerainfo.constructor);
  if (!jcamera_info) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    goto done;
  }

  AHC_STATIC_CALL (goto done, Void, getCameraInfo, camera_id, jcamera_info);

  camera_info->facing = (*env)->GetIntField (env, jcamera_info,
      android_hardware_camera_camerainfo.facing);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to get CameraInfo.facing field");
    (*env)->ExceptionClear (env);
    goto done;
  }

  camera_info->orientation = (*env)->GetIntField (env, jcamera_info,
      android_hardware_camera_camerainfo.orientation);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to get CameraInfo.orientation field");
    (*env)->ExceptionClear (env);
    goto done;
  }

  ret = TRUE;
done:
  if (jcamera_info)
    (*env)->DeleteLocalRef (env, jcamera_info);

  return ret;
}

gint
gst_ah_camera_get_number_of_cameras (void)
{
  JNIEnv *env = gst_dvm_get_env ();
  gint num_cameras;

  num_cameras = AHC_STATIC_CALL (return -1, Int, getNumberOfCameras);

  return num_cameras;
}

GstAHCParameters *
gst_ah_camera_get_parameters (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object = NULL;
  GstAHCParameters *params = NULL;

  object = AHC_CALL (return NULL, Object, getParameters);

  params = g_slice_new0 (GstAHCParameters);
  params->object = (*env)->NewGlobalRef (env, object);
  (*env)->DeleteLocalRef (env, object);
  if (!params->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAHCParameters, params);
    return NULL;
  }

  return params;
}

gboolean
gst_ah_camera_lock (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, lock);

  return TRUE;
}

GstAHCamera *
gst_ah_camera_open (gint camera_id)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object = NULL;
  GstAHCamera *camera = NULL;

  object = AHC_STATIC_CALL (goto done, Object, open, camera_id);

  camera = g_slice_new0 (GstAHCamera);
  camera->object = (*env)->NewGlobalRef (env, object);
  (*env)->DeleteLocalRef (env, object);
  if (!camera->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAHCamera, camera);
    goto done;
  }

  return camera;
done:
  return NULL;
}

gboolean
gst_ah_camera_reconnect (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, reconnect);

  return TRUE;
}

void
gst_ah_camera_release (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (, Void, release);

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAHCamera, self);
}

gboolean
gst_ah_camera_set_parameters (GstAHCamera * self, GstAHCParameters * params)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, setParameters, params->object);

  return TRUE;
}

gboolean
gst_ah_camera_set_error_callback (GstAHCamera * self, GstAHCErrorCallback cb,
    gpointer user_data)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject object;
  gboolean ret = FALSE;

  object = (*env)->NewObject (env,
      com_gstreamer_gstahccallback.klass,
      com_gstreamer_gstahccallback.constructor,
      *((jlong *) & cb), *((jlong *) & user_data));
  if (!object) {
    GST_ERROR ("Failed to create callback object");
    (*env)->ExceptionClear (env);
    goto done;
  }

  AHC_CALL (goto done, Void, setErrorCallback, object);

  ret = TRUE;
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
  gboolean ret = FALSE;

  object = (*env)->NewObject (env,
      com_gstreamer_gstahccallback.klass,
      com_gstreamer_gstahccallback.constructor,
      *((jlong *) & cb), *((jlong *) & user_data));
  if (!object) {
    GST_ERROR ("Failed to create callback object");
    (*env)->ExceptionClear (env);
    goto done;
  }

  AHC_CALL (goto done, Void, setPreviewCallbackWithBuffer, object);

  ret = TRUE;
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

  AHC_CALL (, Void, setPreviewTexture, surfaceTexture->object);
}

gboolean
gst_ah_camera_start_preview (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, startPreview);

  return TRUE;
}

gboolean
gst_ah_camera_start_smooth_zoom (GstAHCamera * self, gint value)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, startSmoothZoom, value);

  return TRUE;
}

gboolean
gst_ah_camera_stop_preview (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, stopPreview);

  return TRUE;
}

gboolean
gst_ah_camera_stop_smooth_zoom (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, stopSmoothZoom);

  return TRUE;
}

gboolean
gst_ah_camera_unlock (GstAHCamera * self)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHC_CALL (return FALSE, Void, unlock);

  return TRUE;
}

#undef AHC_CALL
#undef AHC_STATIC_CALL

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

/* java.util.List */
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

/* android.hardware.Camera.Parameters */
#define AHCP_CALL(error_statement, type, method, ...)                   \
  GST_DVM_CALL (error_statement, self->object, type,                    \
      android_hardware_camera_parameters, method, ## __VA_ARGS__);
#define AHCP_STATIC_CALL(error_statement, type, method, ...)            \
  GST_DVM_STATIC_CALL (error_statement, type,                           \
      android_hardware_camera_parameters, method, ## __VA_ARGS__);

gchar *
gst_ahc_parameters_flatten (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jstring v_str = NULL;
  const gchar *v = NULL;
  gchar *ret = NULL;

  v_str = AHCP_CALL (goto done, Object, flatten);
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

  format = AHCP_CALL (return 0, Int, getPreviewFormat);

  return format;
}

gboolean
gst_ahc_parameters_get_preview_fps_range (GstAHCParameters * self,
    gint * min, gint * max)
{
  JNIEnv *env = gst_dvm_get_env ();
  gboolean ret = FALSE;
  jintArray range = NULL;
  jint *fps = NULL;

  range = (*env)->NewIntArray (env, 2);
  if (!fps) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to create array");
    goto done;
  }

  AHCP_CALL (goto done, Void, getPreviewFpsRange, range);

  fps = (*env)->GetIntArrayElements (env, range, NULL);
  if ((*env)->ExceptionCheck (env) || !fps) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get array elements");
    goto done;
  }
  if (min)
    *min = fps[0];
  if (max)
    *max = fps[1];

  ret = TRUE;
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

  jsize = AHCP_CALL (goto done, Object, getPreviewSize);

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

GList *
gst_ahc_parameters_get_supported_preview_formats (GstAHCParameters * self)
{
  JNIEnv *env = gst_dvm_get_env ();
  jobject list = NULL;
  jobject iterator = NULL;
  GList *ret = NULL;

  list = AHCP_CALL (return NULL, Object, getSupportedPreviewFormats);

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

  list = AHCP_CALL (return NULL, Object, getSupportedPreviewFpsRange);

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

  list = AHCP_CALL (return NULL, Object, getSupportedPreviewSizes);

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

  AHCP_CALL (return FALSE, Void, setPreviewFormat, format);

  return TRUE;
}

gboolean
gst_ahc_parameters_set_preview_fps_range (GstAHCParameters * self,
    gint min, gint max)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHCP_CALL (return FALSE, Void, setPreviewFpsRange, min, max);

  return TRUE;
}

gboolean
gst_ahc_parameters_set_preview_size (GstAHCParameters * self,
    gint width, gint height)
{
  JNIEnv *env = gst_dvm_get_env ();

  AHCP_CALL (return FALSE, Void, setPreviewSize, width, height);

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

  AHCP_CALL (ret = FALSE, Void, unflatten, v_str);

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
