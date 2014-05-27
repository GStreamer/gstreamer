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

#include <gst/gl/gstglapi.h>
#include "gstgltransformation.h"

#define GST_CAT_DEFAULT gst_gl_transformation_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_FOVY,
  PROP_ORTHO,
  PROP_TRANSLATION_X,
  PROP_TRANSLATION_Y,
  PROP_TRANSLATION_Z,
  PROP_ROTATION_X,
  PROP_ROTATION_Y,
  PROP_ROTATION_Z,
  PROP_SCALE_X,
  PROP_SCALE_Y
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

static void gst_gl_transformation_reset (GstGLFilter * filter);
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

  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_transformation_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_transformation_reset;
  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_transformation_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_transformation_filter_texture;

  g_object_class_install_property (gobject_class, PROP_FOVY,
      g_param_spec_float ("fovy", "Fovy", "Field of view angle in degrees",
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
          "Translates the video at the X-Axis.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRANSLATION_Y,
      g_param_spec_float ("translation-y", "Y Translation",
          "Translates the video at the Y-Axis.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRANSLATION_Z,
      g_param_spec_float ("translation-z", "Z Translation",
          "Translates the video at the Z-Axis.",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Scale */
  g_object_class_install_property (gobject_class, PROP_SCALE_X,
      g_param_spec_float ("scale-x", "X Scale",
          "Scale multiplierer for the X-Axis.",
          0.0, G_MAXFLOAT, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SCALE_Y,
      g_param_spec_float ("scale-y", "Y Scale",
          "Scale multiplierer for the Y-Axis.",
          0.0, G_MAXFLOAT, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL transformation filter",
      "Filter/Effect/Video", "Transform video on the GPU",
      "Lubosz Sarnecki <lubosz@gmail.com>");
}

static void
gst_gl_transformation_init (GstGLTransformation * filter)
{
  filter->shader = NULL;
  filter->fovy = 90;
  filter->aspect = 0;
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
      GRAPHENE_POINT3D_INIT (transformation->xtranslation,
      transformation->ytranslation,
      transformation->ztranslation);

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

  graphene_matrix_init_rotate (&model_matrix,
      transformation->xrotation, graphene_vec3_x_axis ());
  graphene_matrix_rotate (&model_matrix,
      transformation->yrotation, graphene_vec3_y_axis ());
  graphene_matrix_rotate (&model_matrix,
      transformation->zrotation, graphene_vec3_z_axis ());
  graphene_matrix_scale (&model_matrix,
      transformation->xscale, transformation->yscale, 1.0f);
  graphene_matrix_translate (&model_matrix, &translation_vector);

  if (transformation->ortho) {
    graphene_matrix_init_ortho (&projection_matrix,
        -transformation->aspect, transformation->aspect,
        -1, 1, transformation->znear, transformation->zfar);
  } else {
    graphene_matrix_init_perspective (&projection_matrix,
        transformation->fovy,
        transformation->aspect, transformation->znear, transformation->zfar);
  }

  graphene_matrix_init_look_at (&view_matrix, &eye, &center, &up);

  graphene_matrix_multiply (&projection_matrix, &view_matrix, &vp_matrix);
  graphene_matrix_multiply (&vp_matrix, &model_matrix,
      &transformation->mvp_matrix);
}

static void
gst_gl_transformation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLTransformation *filter = GST_GL_TRANSFORMATION (object);

  switch (prop_id) {
    case PROP_FOVY:
      filter->fovy = g_value_get_float (value);
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
    case PROP_FOVY:
      g_value_set_float (value, filter->fovy);
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

  gst_gl_transformation_build_mvp (transformation);

  return TRUE;
}

static void
gst_gl_transformation_reset (GstGLFilter * filter)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  /* blocking call, wait until the opengl thread has destroyed the shader */
  if (transformation->shader)
    gst_gl_context_del_shader (filter->context, transformation->shader);
  transformation->shader = NULL;
}

static gboolean
gst_gl_transformation_init_shader (GstGLFilter * filter)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  if (gst_gl_context_get_gl_api (filter->context)) {
    /* blocking call, wait until the opengl thread has compiled the shader */
    return gst_gl_context_gen_shader (filter->context, cube_v_src, cube_f_src,
        &transformation->shader);
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
  gst_gl_context_use_fbo_v2 (filter->context,
      GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info),
      filter->fbo, filter->depthbuffer,
      out_tex, gst_gl_transformation_callback, (gpointer) transformation);

  return TRUE;
}

static void
gst_gl_transformation_callback (gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

/* *INDENT-OFF* */

  const GLfloat positions[] = {
     -transformation->aspect,  1.0,  0.0, 1.0,
      transformation->aspect,  1.0,  0.0, 1.0,
      transformation->aspect, -1.0,  0.0, 1.0,
     -transformation->aspect, -1.0,  0.0, 1.0,
  };

  const GLfloat texture_coordinates[] = {
     0.0,  1.0,
     1.0,  1.0,
     1.0,  0.0,
     0.0,  0.0,
  };

/* *INDENT-ON* */

  GLushort indices[] = { 0, 1, 2, 3, 0 };

  GLfloat temp_matrix[16];

  GLint attr_position_loc = 0;
  GLint attr_texture_loc = 0;

  gst_gl_context_clear_shader (filter->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);

  gl->ClearColor (0.f, 0.f, 0.f, 0.f);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (transformation->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (transformation->shader, "position");

  attr_texture_loc =
      gst_gl_shader_get_attribute_location (transformation->shader, "uv");

  /* Load the vertex position */
  gl->VertexAttribPointer (attr_position_loc, 4, GL_FLOAT,
      GL_FALSE, 0, positions);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (attr_texture_loc, 2, GL_FLOAT,
      GL_FALSE, 0, texture_coordinates);

  gl->EnableVertexAttribArray (attr_position_loc);
  gl->EnableVertexAttribArray (attr_texture_loc);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, transformation->in_tex);
  gst_gl_shader_set_uniform_1i (transformation->shader, "texture", 0);

  graphene_matrix_to_float (&transformation->mvp_matrix, temp_matrix);
  gst_gl_shader_set_uniform_matrix_4fv (transformation->shader, "mvp",
      1, GL_FALSE, temp_matrix);

  gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, indices);

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->DisableVertexAttribArray (attr_texture_loc);

  gst_gl_context_clear_shader (filter->context);
}
