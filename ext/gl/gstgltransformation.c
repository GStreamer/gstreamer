/*
 * GStreamer
 * Copyright (C) 2014 Lubosz Sarnecki <lubosz@gmail.com>
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

/**
 * SECTION:element-gltransformation
 *
 * Transforms video on the GPU.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch gltestsrc ! gltransformation rotation-z=45 ! glimagesink
 * ]| A pipeline to rotate by 45 degrees
 * |[
 * gst-launch gltestsrc ! gltransformation translation-x=0.5 ! glimagesink
 * ]| Translate the video by 0.5
 * |[
 * gst-launch gltestsrc ! gltransformation scale-y=0.5 scale-x=0.5 ! glimagesink
 * ]| Resize the video by 0.5
 * |[
 * gst-launch gltestsrc ! gltransformation rotation-x=-45 ortho=True ! glimagesink
 * ]| Rotate the video around the X-Axis by -45° with an orthographic projection
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgltransformation.h"

#include <gst/gl/gstglapi.h>
#include <graphene-gobject.h>

#define GST_CAT_DEFAULT gst_gl_transformation_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_gl_transformation_parent_class parent_class

enum
{
  PROP_0,
  PROP_FOV,
  PROP_ORTHO,
  PROP_TRANSLATION_X,
  PROP_TRANSLATION_Y,
  PROP_TRANSLATION_Z,
  PROP_ROTATION_X,
  PROP_ROTATION_Y,
  PROP_ROTATION_Z,
  PROP_SCALE_X,
  PROP_SCALE_Y,
  PROP_MVP
};

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_gl_transformation_debug, "gltransformation", 0, "gltransformation element");

G_DEFINE_TYPE_WITH_CODE (GstGLTransformation, gst_gl_transformation,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_transformation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_transformation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_transformation_set_caps (GstGLFilter * filter,
    GstCaps * incaps, GstCaps * outcaps);

static void gst_gl_transformation_reset_gl (GstGLFilter * filter);
static gboolean gst_gl_transformation_stop (GstBaseTransform * trans);
static gboolean gst_gl_transformation_init_shader (GstGLFilter * filter);
static void gst_gl_transformation_callback (gpointer stuff);
static void gst_gl_transformation_build_mvp (GstGLTransformation *
    transformation);

static gboolean gst_gl_transformation_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);

/* vertex source */
static const gchar *cube_v_src =
    "attribute vec4 position;                     \n"
    "attribute vec2 uv;                           \n"
    "uniform mat4 mvp;                            \n"
    "varying vec2 out_uv;                         \n"
    "void main()                                  \n"
    "{                                            \n"
    "   gl_Position = mvp * position;             \n"
    "   out_uv = uv;                              \n"
    "}                                            \n";

/* fragment source */
static const gchar *cube_f_src =
    "#ifdef GL_ES                                 \n"
    "  precision mediump float;                   \n"
    "#endif                                       \n"
    "varying vec2 out_uv;                         \n"
    "uniform sampler2D texture;                   \n"
    "void main()                                  \n"
    "{                                            \n"
    "  gl_FragColor = texture2D (texture, out_uv);\n"
    "}                                            \n";

static void
gst_gl_transformation_class_init (GstGLTransformationClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_transformation_set_property;
  gobject_class->get_property = gst_gl_transformation_get_property;

  GST_GL_FILTER_CLASS (klass)->init_fbo = gst_gl_transformation_init_shader;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb =
      gst_gl_transformation_reset_gl;
  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_transformation_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_transformation_filter_texture;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_transformation_stop;

  g_object_class_install_property (gobject_class, PROP_FOV,
      g_param_spec_float ("fov", "Fov", "Field of view angle in degrees",
          0.0, G_MAXFLOAT, 90.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ORTHO,
      g_param_spec_boolean ("ortho", "Orthographic",
          "Use orthographic projection", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Rotation */
  g_object_class_install_property (gobject_class, PROP_ROTATION_X,
      g_param_spec_float ("rotation-x", "X Rotation",
          "Rotates the video around the X-Axis in degrees.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ROTATION_Y,
      g_param_spec_float ("rotation-y", "Y Rotation",
          "Rotates the video around the Y-Axis in degrees.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ROTATION_Z,
      g_param_spec_float ("rotation-z", "Z Rotation",
          "Rotates the video around the Z-Axis in degrees.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Translation */
  g_object_class_install_property (gobject_class, PROP_TRANSLATION_X,
      g_param_spec_float ("translation-x", "X Translation",
          "Translates the video at the X-Axis, in universal [0-1] coordinate.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRANSLATION_Y,
      g_param_spec_float ("translation-y", "Y Translation",
          "Translates the video at the Y-Axis, in universal [0-1] coordinate.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRANSLATION_Z,
      g_param_spec_float ("translation-z", "Z Translation",
          "Translates the video at the Z-Axis, in universal [0-1] coordinate.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Scale */
  g_object_class_install_property (gobject_class, PROP_SCALE_X,
      g_param_spec_float ("scale-x", "X Scale",
          "Scale multiplier for the X-Axis.",
          0.0, G_MAXFLOAT, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SCALE_Y,
      g_param_spec_float ("scale-y", "Y Scale",
          "Scale multiplier for the Y-Axis.",
          0.0, G_MAXFLOAT, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* MVP */
  g_object_class_install_property (gobject_class, PROP_MVP,
      g_param_spec_boxed ("mvp-matrix",
          "Modelview Projection Matrix",
          "The final Graphene 4x4 Matrix for transformation",
          GRAPHENE_TYPE_MATRIX, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL transformation filter",
      "Filter/Effect/Video", "Transform video on the GPU",
      "Lubosz Sarnecki <lubosz@gmail.com>");

  GST_GL_BASE_FILTER_CLASS (klass)->supported_gl_api =
      GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2;
}

static void
gst_gl_transformation_init (GstGLTransformation * filter)
{
  filter->shader = NULL;
  filter->fov = 90;
  filter->aspect = 1.0;
  filter->znear = 0.1;
  filter->zfar = 100;

  filter->xscale = 1.0;
  filter->yscale = 1.0;

  filter->in_tex = 0;

  gst_gl_transformation_build_mvp (filter);
}

static void
gst_gl_transformation_build_mvp (GstGLTransformation * transformation)
{
  graphene_point3d_t translation_vector =
      GRAPHENE_POINT3D_INIT (transformation->xtranslation * 2.0 *
      transformation->aspect,
      transformation->ytranslation * 2.0,
      transformation->ztranslation * 2.0);

  graphene_matrix_t model_matrix;
  graphene_matrix_t projection_matrix;
  graphene_matrix_t view_matrix;
  graphene_matrix_t vp_matrix;

  graphene_vec3_t eye;
  graphene_vec3_t center;
  graphene_vec3_t up;

  graphene_vec3_init (&eye, 0.f, 0.f, 1.f);
  graphene_vec3_init (&center, 0.f, 0.f, 0.f);
  graphene_vec3_init (&up, 0.f, 1.f, 0.f);

  graphene_matrix_init_scale (&model_matrix,
      transformation->xscale, transformation->yscale, 1.0f);

  graphene_matrix_rotate (&model_matrix,
      transformation->xrotation, graphene_vec3_x_axis ());
  graphene_matrix_rotate (&model_matrix,
      transformation->yrotation, graphene_vec3_y_axis ());
  graphene_matrix_rotate (&model_matrix,
      transformation->zrotation, graphene_vec3_z_axis ());

  graphene_matrix_translate (&model_matrix, &translation_vector);

  if (transformation->ortho) {
    graphene_matrix_init_ortho (&projection_matrix,
        -transformation->aspect, transformation->aspect,
        -1, 1, transformation->znear, transformation->zfar);
  } else {
    graphene_matrix_init_perspective (&projection_matrix,
        transformation->fov,
        transformation->aspect, transformation->znear, transformation->zfar);
  }

  graphene_matrix_init_look_at (&view_matrix, &eye, &center, &up);

  graphene_matrix_multiply (&view_matrix, &projection_matrix, &vp_matrix);
  graphene_matrix_multiply (&model_matrix, &vp_matrix,
      &transformation->mvp_matrix);
}

static void
gst_gl_transformation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLTransformation *filter = GST_GL_TRANSFORMATION (object);

  switch (prop_id) {
    case PROP_FOV:
      filter->fov = g_value_get_float (value);
      break;
    case PROP_ORTHO:
      filter->ortho = g_value_get_boolean (value);
      break;
    case PROP_TRANSLATION_X:
      filter->xtranslation = g_value_get_float (value);
      break;
    case PROP_TRANSLATION_Y:
      filter->ytranslation = g_value_get_float (value);
      break;
    case PROP_TRANSLATION_Z:
      filter->ztranslation = g_value_get_float (value);
      break;
    case PROP_ROTATION_X:
      filter->xrotation = g_value_get_float (value);
      break;
    case PROP_ROTATION_Y:
      filter->yrotation = g_value_get_float (value);
      break;
    case PROP_ROTATION_Z:
      filter->zrotation = g_value_get_float (value);
      break;
    case PROP_SCALE_X:
      filter->xscale = g_value_get_float (value);
      break;
    case PROP_SCALE_Y:
      filter->yscale = g_value_get_float (value);
      break;
    case PROP_MVP:
      if (g_value_get_boxed (value) != NULL)
        filter->mvp_matrix = *((graphene_matrix_t *) g_value_get_boxed (value));
      return;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  gst_gl_transformation_build_mvp (filter);
}

static void
gst_gl_transformation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLTransformation *filter = GST_GL_TRANSFORMATION (object);

  switch (prop_id) {
    case PROP_FOV:
      g_value_set_float (value, filter->fov);
      break;
    case PROP_ORTHO:
      g_value_set_boolean (value, filter->ortho);
      break;
    case PROP_TRANSLATION_X:
      g_value_set_float (value, filter->xtranslation);
      break;
    case PROP_TRANSLATION_Y:
      g_value_set_float (value, filter->ytranslation);
      break;
    case PROP_TRANSLATION_Z:
      g_value_set_float (value, filter->ztranslation);
      break;
    case PROP_ROTATION_X:
      g_value_set_float (value, filter->xrotation);
      break;
    case PROP_ROTATION_Y:
      g_value_set_float (value, filter->yrotation);
      break;
    case PROP_ROTATION_Z:
      g_value_set_float (value, filter->zrotation);
      break;
    case PROP_SCALE_X:
      g_value_set_float (value, filter->xscale);
      break;
    case PROP_SCALE_Y:
      g_value_set_float (value, filter->yscale);
      break;
    case PROP_MVP:
      g_value_set_boxed (value, (gconstpointer) & filter->mvp_matrix);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_transformation_set_caps (GstGLFilter * filter, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  transformation->aspect =
      (gdouble) GST_VIDEO_INFO_WIDTH (&filter->out_info) /
      (gdouble) GST_VIDEO_INFO_HEIGHT (&filter->out_info);

  transformation->caps_change = TRUE;

  gst_gl_transformation_build_mvp (transformation);

  return TRUE;
}

static void
gst_gl_transformation_reset_gl (GstGLFilter * filter)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);
  const GstGLFuncs *gl = GST_GL_BASE_FILTER (filter)->context->gl_vtable;

  if (transformation->vao) {
    gl->DeleteVertexArrays (1, &transformation->vao);
    transformation->vao = 0;
  }

  if (transformation->vertex_buffer) {
    gl->DeleteBuffers (1, &transformation->vertex_buffer);
    transformation->vertex_buffer = 0;
  }

  if (transformation->vbo_indices) {
    gl->DeleteBuffers (1, &transformation->vbo_indices);
    transformation->vbo_indices = 0;
  }

  if (transformation->shader) {
    gst_object_unref (transformation->shader);
    transformation->shader = NULL;
  }
}

static gboolean
gst_gl_transformation_stop (GstBaseTransform * trans)
{
  GstGLBaseFilter *basefilter = GST_GL_BASE_FILTER (trans);
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (trans);

  /* blocking call, wait until the opengl thread has destroyed the shader */
  if (basefilter->context && transformation->shader) {
    gst_gl_context_del_shader (basefilter->context, transformation->shader);
    transformation->shader = NULL;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_gl_transformation_init_shader (GstGLFilter * filter)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  if (transformation->shader) {
    gst_object_unref (transformation->shader);
    transformation->shader = NULL;
  }

  if (gst_gl_context_get_gl_api (GST_GL_BASE_FILTER (filter)->context)) {
    /* blocking call, wait until the opengl thread has compiled the shader */
    return gst_gl_context_gen_shader (GST_GL_BASE_FILTER (filter)->context,
        cube_v_src, cube_f_src, &transformation->shader);
  }
  return TRUE;
}

static gboolean
gst_gl_transformation_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  transformation->in_tex = in_tex;

  /* blocking call, use a FBO */
  gst_gl_context_use_fbo_v2 (GST_GL_BASE_FILTER (filter)->context,
      GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info),
      filter->fbo, filter->depthbuffer,
      out_tex, gst_gl_transformation_callback, (gpointer) transformation);

  return TRUE;
}

static const GLushort indices[] = { 0, 1, 2, 3, 0 };

static void
_upload_vertices (GstGLTransformation * transformation)
{
  const GstGLFuncs *gl =
      GST_GL_BASE_FILTER (transformation)->context->gl_vtable;

/* *INDENT-OFF* */
  GLfloat vertices[] = {
     -transformation->aspect,  1.0,  0.0, 1.0, 0.0, 1.0,
      transformation->aspect,  1.0,  0.0, 1.0, 1.0, 1.0,
      transformation->aspect, -1.0,  0.0, 1.0, 1.0, 0.0,
     -transformation->aspect, -1.0,  0.0, 1.0, 0.0, 0.0
  };
  /* *INDENT-ON* */

  gl->BindBuffer (GL_ARRAY_BUFFER, transformation->vertex_buffer);

  gl->BufferData (GL_ARRAY_BUFFER, 4 * 6 * sizeof (GLfloat), vertices,
      GL_STATIC_DRAW);
}

static void
_bind_buffer (GstGLTransformation * transformation)
{
  const GstGLFuncs *gl =
      GST_GL_BASE_FILTER (transformation)->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, transformation->vbo_indices);
  gl->BindBuffer (GL_ARRAY_BUFFER, transformation->vertex_buffer);

  /* Load the vertex position */
  gl->VertexAttribPointer (transformation->attr_position, 4, GL_FLOAT,
      GL_FALSE, 6 * sizeof (GLfloat), (void *) 0);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (transformation->attr_texture, 2, GL_FLOAT, GL_FALSE,
      6 * sizeof (GLfloat), (void *) (4 * sizeof (GLfloat)));

  gl->EnableVertexAttribArray (transformation->attr_position);
  gl->EnableVertexAttribArray (transformation->attr_texture);
}

static void
_unbind_buffer (GstGLTransformation * transformation)
{
  const GstGLFuncs *gl =
      GST_GL_BASE_FILTER (transformation)->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (transformation->attr_position);
  gl->DisableVertexAttribArray (transformation->attr_texture);
}

static void
gst_gl_transformation_callback (gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);
  GstGLFuncs *gl = GST_GL_BASE_FILTER (filter)->context->gl_vtable;

  GLfloat temp_matrix[16];

  gst_gl_context_clear_shader (GST_GL_BASE_FILTER (filter)->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);

  gl->ClearColor (0.f, 0.f, 0.f, 1.f);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (transformation->shader);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, transformation->in_tex);
  gst_gl_shader_set_uniform_1i (transformation->shader, "texture", 0);

  graphene_matrix_to_float (&transformation->mvp_matrix, temp_matrix);
  gst_gl_shader_set_uniform_matrix_4fv (transformation->shader, "mvp",
      1, GL_FALSE, temp_matrix);

  if (!transformation->vertex_buffer) {
    transformation->attr_position =
        gst_gl_shader_get_attribute_location (transformation->shader,
        "position");

    transformation->attr_texture =
        gst_gl_shader_get_attribute_location (transformation->shader, "uv");

    if (gl->GenVertexArrays) {
      gl->GenVertexArrays (1, &transformation->vao);
      gl->BindVertexArray (transformation->vao);
    }

    gl->GenBuffers (1, &transformation->vertex_buffer);

    gl->GenBuffers (1, &transformation->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, transformation->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);

    transformation->caps_change = TRUE;
  }

  if (gl->GenVertexArrays)
    gl->BindVertexArray (transformation->vao);

  if (transformation->caps_change) {
    _upload_vertices (transformation);
    _bind_buffer (transformation);

    if (gl->GenVertexArrays) {
      gl->BindVertexArray (0);
      gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
      gl->BindBuffer (GL_ARRAY_BUFFER, 0);
    }
  } else if (!gl->GenVertexArrays) {
    _bind_buffer (transformation);
  }

  gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, indices);

  if (gl->GenVertexArrays)
    gl->BindVertexArray (0);
  else
    _unbind_buffer (transformation);

  gst_gl_context_clear_shader (GST_GL_BASE_FILTER (filter)->context);
  transformation->caps_change = FALSE;
}
