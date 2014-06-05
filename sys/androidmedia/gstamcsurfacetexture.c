/*
 * Copyright (C) 2013, Fluendo S.A.
 *   Author: Andoni Morales <amorales@fluendo.com>
 *
 * Copyright (C) 2014, Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@collabora.com>
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
#include "gstamcsurfacetexture.h"

G_DEFINE_TYPE (GstAmcSurfaceTexture, gst_amc_surface_texture, G_TYPE_OBJECT);

static gpointer parent_class = NULL;
static void gst_amc_surface_texture_dispose (GObject * object);

static gboolean
_cache_java_class (GstAmcSurfaceTextureClass * klass, GError ** err)
{
  JNIEnv *env;

  gst_amc_jni_initialize ();
  env = gst_amc_jni_get_env ();

  klass->jklass =
      gst_amc_jni_get_class (env, err, "android/graphics/SurfaceTexture");
  if (!klass->jklass) {
    return FALSE;
  }

  klass->constructor =
      gst_amc_jni_get_method_id (env, err, klass->jklass, "<init>", "(I)V");
  if (!klass->constructor) {
    goto error;
  }

  klass->set_on_frame_available_listener =
      gst_amc_jni_get_method_id (env, err, klass->jklass,
      "setOnFrameAvailableListener",
      "(Landroid/graphics/SurfaceTexture$OnFrameAvailableListener;)V");

  klass->set_default_buffer_size =
      gst_amc_jni_get_method_id (env, err, klass->jklass,
      "setDefaultBufferSize", "(II)V");
  if (!klass->set_default_buffer_size) {
    goto error;
  }

  klass->update_tex_image =
      gst_amc_jni_get_method_id (env, err, klass->jklass, "updateTexImage",
      "()V");
  if (!klass->update_tex_image) {
    goto error;
  }

  klass->detach_from_gl_context =
      gst_amc_jni_get_method_id (env, err, klass->jklass, "detachFromGLContext",
      "()V");
  if (!klass->detach_from_gl_context) {
    goto error;
  }

  klass->attach_to_gl_context =
      gst_amc_jni_get_method_id (env, err, klass->jklass, "attachToGLContext",
      "(I)V");
  if (!klass->attach_to_gl_context) {
    goto error;
  }

  klass->get_transform_matrix =
      gst_amc_jni_get_method_id (env, err, klass->jklass, "getTransformMatrix",
      "([F)V");
  if (!klass->get_transform_matrix) {
    goto error;
  }

  klass->get_timestamp =
      gst_amc_jni_get_method_id (env, err, klass->jklass, "getTimestamp",
      "()J");
  if (!klass->get_timestamp) {
    goto error;
  }

  klass->release =
      gst_amc_jni_get_method_id (env, err, klass->jklass, "release", "()V");
  if (!klass->release) {
    goto error;
  }

  return TRUE;

error:
  gst_amc_jni_object_unref (env, klass->constructor);
  return FALSE;
}

static void
gst_amc_surface_texture_init (GstAmcSurfaceTexture * self)
{
}

static void
gst_amc_surface_texture_class_init (GstAmcSurfaceTextureClass * klass)
{
  GError *err = NULL;
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->dispose = gst_amc_surface_texture_dispose;

  if (!_cache_java_class (klass, &err)) {
    g_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
        "Could not cache java class android/graphics/SurfaceTexture: %s",
        err->message);
    g_clear_error (&err);
  }
}

static void
gst_amc_surface_texture_dispose (GObject * object)
{
  GstAmcSurfaceTexture *self;
  JNIEnv *env;
  GError *err = NULL;

  self = GST_AMC_SURFACE_TEXTURE (object);
  env = gst_amc_jni_get_env ();

  if (!gst_amc_surface_texture_release (self, &err)) {
    GST_ERROR ("Could not release surface texture: %s", err->message);
    g_clear_error (&err);
  }

  if (self->jobject) {
    gst_amc_jni_object_unref (env, self->jobject);
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

GstAmcSurfaceTexture *
gst_amc_surface_texture_new (GError ** err)
{
  GstAmcSurfaceTexture *texture = NULL;
  GstAmcSurfaceTextureClass *klass;
  JNIEnv *env;

  texture = g_object_new (GST_TYPE_AMC_SURFACE_TEXTURE, NULL);
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (texture);
  env = gst_amc_jni_get_env ();

  texture->texture_id = 0;

  texture->jobject = gst_amc_jni_new_object (env, err, TRUE, klass->jklass,
      klass->constructor, texture->texture_id);
  if (texture->jobject == NULL) {
    goto error;
  }

  if (!gst_amc_surface_texture_detach_from_gl_context (texture, err)) {
    goto error;
  }

  return texture;

error:
  if (texture)
    g_object_unref (texture);
  return NULL;
}

gboolean
gst_amc_surface_texture_set_default_buffer_size (GstAmcSurfaceTexture * self,
    gint width, gint height, GError ** err)
{
  JNIEnv *env;
  GstAmcSurfaceTextureClass *klass;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  return gst_amc_jni_call_void_method (env, err, self->jobject,
      klass->set_default_buffer_size, width, height);
}

gboolean
gst_amc_surface_texture_update_tex_image (GstAmcSurfaceTexture * self,
    GError ** err)
{
  JNIEnv *env;
  GstAmcSurfaceTextureClass *klass;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  return gst_amc_jni_call_void_method (env, err, self->jobject,
      klass->update_tex_image);
}

gboolean
gst_amc_surface_texture_detach_from_gl_context (GstAmcSurfaceTexture * self,
    GError ** err)
{
  JNIEnv *env;
  gboolean ret;
  GstAmcSurfaceTextureClass *klass;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  ret =
      gst_amc_jni_call_void_method (env, err, self->jobject,
      klass->detach_from_gl_context);
  self->texture_id = 0;
  return ret;
}

gboolean
gst_amc_surface_texture_attach_to_gl_context (GstAmcSurfaceTexture * self,
    gint texture_id, GError ** err)
{
  JNIEnv *env;
  gboolean ret;
  GstAmcSurfaceTextureClass *klass;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  ret =
      gst_amc_jni_call_void_method (env, err, self->jobject,
      klass->attach_to_gl_context, texture_id);
  self->texture_id = texture_id;
  return ret;
}

gboolean
gst_amc_surface_texture_get_transform_matrix (GstAmcSurfaceTexture * self,
    const gfloat * matrix, GError ** err)
{
  JNIEnv *env;
  gboolean ret;
  GstAmcSurfaceTextureClass *klass;
  /* 4x4 Matrix */
  jsize size = 16;
  jfloatArray floatarray;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  floatarray = (*env)->NewFloatArray (env, size);
  ret =
      gst_amc_jni_call_void_method (env, err, self->jobject,
      klass->get_transform_matrix, floatarray);
  if (ret) {
    (*env)->GetFloatArrayRegion (env, floatarray, 0, size, (jfloat *) matrix);
    (*env)->DeleteLocalRef (env, floatarray);
  }

  return ret;
}

gboolean
gst_amc_surface_texture_get_timestamp (GstAmcSurfaceTexture * self,
    gint64 * result, GError ** err)
{
  JNIEnv *env;
  GstAmcSurfaceTextureClass *klass;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  return gst_amc_jni_call_long_method (env, err, self->jobject,
      klass->get_timestamp, result);
}

gboolean
gst_amc_surface_texture_release (GstAmcSurfaceTexture * self, GError ** err)
{
  JNIEnv *env;
  GstAmcSurfaceTextureClass *klass;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  return gst_amc_jni_call_void_method (env, err, self->jobject, klass->release);
}

gboolean
gst_amc_surface_texture_set_on_frame_available_listener (GstAmcSurfaceTexture *
    self, jobject listener, GError ** err)
{
  JNIEnv *env;
  GstAmcSurfaceTextureClass *klass;

  env = gst_amc_jni_get_env ();
  klass = GST_AMC_SURFACE_TEXTURE_GET_CLASS (self);

  return gst_amc_jni_call_void_method (env, err, self->jobject,
      klass->set_on_frame_available_listener, listener);
}
