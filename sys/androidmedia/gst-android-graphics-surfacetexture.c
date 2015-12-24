/*
 * Copyright (C) 2012, Collabora Ltd.
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * Copyright (C) 2015, Collabora Ltd.
 *   Author: Justin Kim <justin.kim@collabora.com>
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

#include "gstjniutils.h"

#include "gst-android-graphics-surfacetexture.h"

static struct
{
  jclass klass;
  jmethodID constructor;
  jmethodID release;
} android_graphics_surfacetexture = {
0};

static gboolean
_init_classes (void)
{
  JNIEnv *env;
  GError *err = NULL;

  env = gst_amc_jni_get_env ();

  /* android.graphics.SurfaceTexture */
  android_graphics_surfacetexture.klass =
      gst_amc_jni_get_class (env, &err, "android/graphics/SurfaceTexture");

  if (!android_graphics_surfacetexture.klass) {
    GST_ERROR ("Failed to get android.graphics.SurfaceTexture class: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  android_graphics_surfacetexture.constructor =
      gst_amc_jni_get_method_id (env, &err,
      android_graphics_surfacetexture.klass, "<init>", "(I)V");
  android_graphics_surfacetexture.release =
      gst_amc_jni_get_method_id (env, &err,
      android_graphics_surfacetexture.klass, "release", "()V");

  return TRUE;
}

gboolean
gst_android_graphics_surfacetexture_init (void)
{
  if (!_init_classes ()) {
    gst_android_graphics_surfacetexture_deinit ();
    return FALSE;
  }

  return TRUE;
}

void
gst_android_graphics_surfacetexture_deinit (void)
{
  JNIEnv *env = gst_amc_jni_get_env ();

  if (android_graphics_surfacetexture.klass)
    (*env)->DeleteGlobalRef (env, android_graphics_surfacetexture.klass);
  android_graphics_surfacetexture.klass = NULL;
}

/* android.graphics.SurfaceTexture */
GstAGSurfaceTexture *
gst_ag_surfacetexture_new (gint texture_id)
{
  JNIEnv *env = gst_amc_jni_get_env ();
  jobject object = NULL;
  GstAGSurfaceTexture *tex = NULL;

  object = (*env)->NewObject (env,
      android_graphics_surfacetexture.klass,
      android_graphics_surfacetexture.constructor, texture_id);
  if ((*env)->ExceptionCheck (env) || !object) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
    return NULL;
  }

  tex = g_slice_new0 (GstAGSurfaceTexture);
  tex->object = (*env)->NewGlobalRef (env, object);
  if (!tex->object) {
    GST_ERROR ("Failed to create global reference");
    (*env)->ExceptionClear (env);
    g_slice_free (GstAGSurfaceTexture, tex);
    tex = NULL;
  }
  (*env)->DeleteLocalRef (env, object);

  return tex;
}

void
gst_ag_surfacetexture_release (GstAGSurfaceTexture * self)
{
  JNIEnv *env;
  GError *err = NULL;

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_void_method (env, &err, self->object,
          android_graphics_surfacetexture.release)) {
    GST_ERROR ("Failed to call release: %s", err->message);
    g_clear_error (&err);
  }

}

void
gst_ag_surfacetexture_free (GstAGSurfaceTexture * self)
{
  JNIEnv *env = gst_amc_jni_get_env ();

  (*env)->DeleteGlobalRef (env, self->object);
  g_slice_free (GstAGSurfaceTexture, self);
}
