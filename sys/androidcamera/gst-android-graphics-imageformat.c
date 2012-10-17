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
#include "gst-android-graphics-imageformat.h"


static struct
{
  jclass klass;
  jmethodID getBitsPerPixel;
  jint JPEG;
  jint NV16;
  jint NV21;
  jint RGB_565;
  jint UNKNOWN;
  jint YUY2;
  jint YV12;
} android_graphics_imageformat = {0};

gint ImageFormat_JPEG;
gint ImageFormat_NV16;
gint ImageFormat_NV21;
gint ImageFormat_RGB_565;
gint ImageFormat_UNKNOWN;
gint ImageFormat_YUY2;
gint ImageFormat_YV12;

static gboolean
_init_classes ()
{
  JNIEnv *env = gst_dvm_get_env ();

  /* android.graphics.ImageFormat */
  GST_DVM_GET_CLASS (android_graphics_imageformat,
      "android/graphics/ImageFormat");
  GST_DVM_GET_STATIC_METHOD (android_graphics_imageformat, getBitsPerPixel,
      "(I)I");

  GST_DVM_GET_CONSTANT (android_graphics_imageformat, JPEG, Int, "I");
  ImageFormat_JPEG = android_graphics_imageformat.JPEG;
  GST_DVM_GET_CONSTANT (android_graphics_imageformat, NV16, Int, "I");
  ImageFormat_NV16 = android_graphics_imageformat.NV16;
  GST_DVM_GET_CONSTANT (android_graphics_imageformat, NV21, Int, "I");
  ImageFormat_NV21 = android_graphics_imageformat.NV21;
  GST_DVM_GET_CONSTANT (android_graphics_imageformat, RGB_565, Int, "I");
  ImageFormat_RGB_565 = android_graphics_imageformat.RGB_565;
  GST_DVM_GET_CONSTANT (android_graphics_imageformat, UNKNOWN, Int, "I");
  ImageFormat_UNKNOWN = android_graphics_imageformat.UNKNOWN;
  GST_DVM_GET_CONSTANT (android_graphics_imageformat, YUY2, Int, "I");
  ImageFormat_YUY2 = android_graphics_imageformat.YUY2;
  GST_DVM_GET_CONSTANT (android_graphics_imageformat, YV12, Int, "I");
  ImageFormat_YV12 = android_graphics_imageformat.YV12;

  return TRUE;
}

gboolean
gst_android_graphics_imageformat_init ()
{
  if (!_init_classes ()) {
    gst_android_graphics_imageformat_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_graphics_imageformat_deinit ()
{
  JNIEnv *env = gst_dvm_get_env ();

  if (android_graphics_imageformat.klass)
    (*env)->DeleteGlobalRef (env, android_graphics_imageformat.klass);
  android_graphics_imageformat.klass = NULL;
}

/* android.graphics.ImageFormat */
gint
gst_ag_imageformat_get_bits_per_pixel (gint format)
{
  JNIEnv *env = gst_dvm_get_env ();
  jint bpp = 0;

  bpp = (*env)->CallStaticIntMethod (env,
      android_graphics_imageformat.klass,
      android_graphics_imageformat.getBitsPerPixel, format);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return -1;
  }

  return bpp;
}
