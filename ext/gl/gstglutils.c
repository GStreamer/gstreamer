/* 
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstglutils.h"

struct _compile_shader
{
  GstGLShader **shader;
  const gchar *vertex_src;
  const gchar *fragment_src;
};

static void
_compile_shader (GstGLContext * context, struct _compile_shader *data)
{
  GstGLShader *shader;
  GstGLSLStage *vert, *frag;
  GError *error = NULL;

  shader = gst_gl_shader_new (context);

  if (data->vertex_src) {
    vert = gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
        GST_GLSL_VERSION_NONE,
        GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY, data->vertex_src);
    if (!gst_glsl_stage_compile (vert, &error)) {
      GST_ERROR_OBJECT (vert, "%s", error->message);
      gst_object_unref (vert);
      gst_object_unref (shader);
      return;
    }
    if (!gst_gl_shader_attach (shader, vert)) {
      gst_object_unref (shader);
      return;
    }
  }

  if (data->fragment_src) {
    frag = gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
        GST_GLSL_VERSION_NONE,
        GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
        data->fragment_src);
    if (!gst_glsl_stage_compile (frag, &error)) {
      GST_ERROR_OBJECT (frag, "%s", error->message);
      gst_object_unref (frag);
      gst_object_unref (shader);
      return;
    }
    if (!gst_gl_shader_attach (shader, frag)) {
      gst_object_unref (shader);
      return;
    }
  }

  if (!gst_gl_shader_link (shader, &error)) {
    GST_ERROR_OBJECT (shader, "%s", error->message);
    g_error_free (error);
    error = NULL;
    gst_gl_context_clear_shader (context);
    gst_object_unref (shader);
    return;
  }

  *data->shader = shader;
}

/* Called by glfilter */
gboolean
gst_gl_context_gen_shader (GstGLContext * context, const gchar * vert_src,
    const gchar * frag_src, GstGLShader ** shader)
{
  struct _compile_shader data;

  g_return_val_if_fail (frag_src != NULL || vert_src != NULL, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);

  data.shader = shader;
  data.vertex_src = vert_src;
  data.fragment_src = frag_src;

  gst_gl_context_thread_add (context, (GstGLContextThreadFunc) _compile_shader,
      &data);

  return *shader != NULL;
}

static const gfloat identity_matrix[] = {
  1.0f, 0.0f, 0.0, 0.0f,
  0.0f, 1.0f, 0.0, 0.0f,
  0.0f, 0.0f, 1.0, 0.0f,
  0.0f, 0.0f, 0.0, 1.0f,
};

static const gfloat from_ndc_matrix[] = {
  0.5f, 0.0f, 0.0, 0.5f,
  0.0f, 0.5f, 0.0, 0.5f,
  0.0f, 0.0f, 0.5, 0.5f,
  0.0f, 0.0f, 0.0, 1.0f,
};

static const gfloat to_ndc_matrix[] = {
  2.0f, 0.0f, 0.0, -1.0f,
  0.0f, 2.0f, 0.0, -1.0f,
  0.0f, 0.0f, 2.0, -1.0f,
  0.0f, 0.0f, 0.0, 1.0f,
};

void
gst_gl_multiply_matrix4 (const gfloat * a, const gfloat * b, gfloat * result)
{
  int i, j, k;
  gfloat tmp[16] = { 0.0f };

  if (!a || !b || !result)
    return;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      for (k = 0; k < 4; k++) {
        tmp[i + (j * 4)] += a[i + (k * 4)] * b[k + (j * 4)];
      }
    }
  }

  for (i = 0; i < 16; i++)
    result[i] = tmp[i];
}

void gst_gl_get_affine_transformation_meta_as_ndc_ext
    (GstVideoAffineTransformationMeta * meta, gfloat * matrix)
{
  if (!meta) {
    int i;

    for (i = 0; i < 16; i++) {
      matrix[i] = identity_matrix[i];
    }
  } else {
    gfloat tmp[16] = { 0.0f };

    gst_gl_multiply_matrix4 (from_ndc_matrix, meta->matrix, tmp);
    gst_gl_multiply_matrix4 (tmp, to_ndc_matrix, matrix);
  }
}
