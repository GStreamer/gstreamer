/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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
 * SECTION:element-glalphacombine
 * @title: glalphacombine
 *
 * Combines two RGBA OpenGL textures by taking RGB from the color input and
 * alpha from a configurable component of the alpha input.
 *
 * This is useful for GL decoder or import paths where color and alpha streams
 * are both exposed as sampled RGBA textures. For example, Android decoders and
 * EGLImage-based embedded Linux paths may expose frames as external-oes
 * textures, so the alpha stream has already been sampled as RGBA instead of
 * being exposed as a separate luma plane.
 *
 * The sink pads currently accept RGBA GLMemory 2D and external-oes textures
 * and the src pad produces RGBA GLMemory 2D textures. By default, the red
 * component of the alpha input is written to the output alpha channel.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglelements.h"
#include "gstglalphacombine.h"

#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>

#define GST_CAT_DEFAULT gst_gl_alpha_combine_debug
GST_DEBUG_CATEGORY_STATIC (gst_gl_alpha_combine_debug);

#define DEFAULT_ALPHA_COMPONENT GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_RED

enum
{
  PROP_0,
  PROP_ALPHA_COMPONENT
};

/**
 * GstGLAlphaCombine:src:
 *
 * The output pad.
 *
 * Since: 1.30
 */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "
        "format = (string) RGBA, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE ", " "texture-target = (string) 2D")
    );

/**
 * GstGLAlphaCombine:sink:
 *
 * The color input pad.
 *
 * Since: 1.30
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "
        "format = (string) RGBA, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "texture-target = (string) { 2D, external-oes }")
    );

/**
 * GstGLAlphaCombine:alpha:
 *
 * The alpha input pad.
 *
 * Since: 1.30
 */
static GstStaticPadTemplate alpha_factory = GST_STATIC_PAD_TEMPLATE ("alpha",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "
        "format = (string) RGBA, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "texture-target = (string) { 2D, external-oes }")
    );

/* *INDENT-OFF* */
static const gchar glsl_external_image_extension[] =
    "#extension GL_OES_EGL_image_external : require\n";

static const gchar gl_alpha_combine_frag[] =
    "varying vec2 v_texcoord;\n"
    "uniform %s color_tex;\n"
    "uniform %s alpha_tex;\n"
    "uniform vec4 alpha_selector;\n"
    "void main () {\n"
    "  vec4 color = texture2D (color_tex, v_texcoord);\n"
    "  vec4 alpha = texture2D (alpha_tex, v_texcoord);\n"
    "  float a = dot (alpha, alpha_selector);\n"
    "  gl_FragColor = vec4 (color.rgb, a);\n"
    "}\n";
/* *INDENT-ON* */

static GType gst_gl_alpha_combine_alpha_component_get_type (void);
static void gst_gl_alpha_combine_gl_stop (GstGLBaseMixer * base_mix);
static gboolean gst_gl_alpha_combine_process_textures (GstGLMixer * mixer,
    GstGLMemory * out_tex);
static void gst_gl_alpha_combine_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_alpha_combine_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

#define gst_gl_alpha_combine_parent_class parent_class
G_DEFINE_TYPE (GstGLAlphaCombine, gst_gl_alpha_combine, GST_TYPE_GL_MIXER);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (glalphacombine, "glalphacombine",
    GST_RANK_NONE, GST_TYPE_GL_ALPHA_COMBINE, gl_element_init (plugin));

static GType
gst_gl_alpha_combine_alpha_component_get_type (void)
{
  static GType alpha_component_type = 0;

  if (g_once_init_enter (&alpha_component_type)) {
    static const GEnumValue values[] = {
      {GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_RED, "Red component", "red"},
      {GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_GREEN, "Green component", "green"},
      {GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_BLUE, "Blue component", "blue"},
      {GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_ALPHA, "Alpha component", "alpha"},
      {0, NULL, NULL}
    };
    GType type = g_enum_register_static ("GstGLAlphaCombineAlphaComponent",
        values);

    g_once_init_leave (&alpha_component_type, type);
  }

  return alpha_component_type;
}

static void
gst_gl_alpha_combine_class_init (GstGLAlphaCombineClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstGLBaseMixerClass *base_mix_class = GST_GL_BASE_MIXER_CLASS (klass);
  GstGLMixerClass *mixer_class = GST_GL_MIXER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "glalphacombine", 0,
      "OpenGL alpha combiner");

  gobject_class->set_property = gst_gl_alpha_combine_set_property;
  gobject_class->get_property = gst_gl_alpha_combine_get_property;

  /**
   * GstGLAlphaCombine:alpha-component:
   *
   * The RGBA component sampled from the alpha input and written to output alpha.
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_ALPHA_COMPONENT,
      g_param_spec_enum ("alpha-component", "Alpha component",
          "RGBA component sampled from the alpha input and written to output alpha",
          gst_gl_alpha_combine_alpha_component_get_type (),
          DEFAULT_ALPHA_COMPONENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "OpenGL alpha combiner", "Filter/Editor/Video/Compositor",
      "Combines RGBA GL textures by replacing color alpha from an alpha input",
      "GStreamer contributors");

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &sink_factory, GST_TYPE_GL_MIXER_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &alpha_factory, GST_TYPE_GL_MIXER_PAD);

  base_mix_class->gl_stop = gst_gl_alpha_combine_gl_stop;
  base_mix_class->supported_gl_api =
      GST_GL_API_GLES2 | GST_GL_API_OPENGL | GST_GL_API_OPENGL3;

  mixer_class->process_textures = gst_gl_alpha_combine_process_textures;

  gst_type_mark_as_plugin_api (gst_gl_alpha_combine_alpha_component_get_type (),
      0);
}

static void
gst_gl_alpha_combine_init (GstGLAlphaCombine * self)
{
  GstPadTemplate *templ;

  self->alpha_component = DEFAULT_ALPHA_COMPONENT;
  self->color_texture_target = GST_GL_TEXTURE_TARGET_EXTERNAL_OES;
  self->alpha_texture_target = GST_GL_TEXTURE_TARGET_EXTERNAL_OES;
  self->shader_color_texture_target = GST_GL_TEXTURE_TARGET_NONE;
  self->shader_alpha_texture_target = GST_GL_TEXTURE_TARGET_NONE;

  templ = gst_static_pad_template_get (&sink_factory);
  self->color_pad = g_object_new (GST_TYPE_GL_MIXER_PAD,
      "name", "sink", "direction", GST_PAD_SINK, "template", templ, NULL);
  gst_object_unref (templ);
  gst_element_add_pad (GST_ELEMENT_CAST (self), GST_PAD_CAST (self->color_pad));

  templ = gst_static_pad_template_get (&alpha_factory);
  self->alpha_pad = g_object_new (GST_TYPE_GL_MIXER_PAD,
      "name", "alpha", "direction", GST_PAD_SINK, "template", templ, NULL);
  gst_object_unref (templ);
  gst_element_add_pad (GST_ELEMENT_CAST (self), GST_PAD_CAST (self->alpha_pad));
}

static void
gst_gl_alpha_combine_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLAlphaCombine *self = GST_GL_ALPHA_COMBINE (object);

  switch (prop_id) {
    case PROP_ALPHA_COMPONENT:
      GST_OBJECT_LOCK (self);
      self->alpha_component = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_alpha_combine_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLAlphaCombine *self = GST_GL_ALPHA_COMBINE (object);

  switch (prop_id) {
    case PROP_ALPHA_COMPONENT:
      GST_OBJECT_LOCK (self);
      g_value_set_enum (value, self->alpha_component);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_alpha_combine_pad_get_texture_target (GstGLAlphaCombine * self,
    GstGLMixerPad * pad, GstGLTextureTarget * target)
{
  GstCaps *caps = gst_pad_get_current_caps (GST_PAD_CAST (pad));
  GstStructure *s;
  const gchar *target_str;

  if (!caps)
    goto no_caps;

  s = gst_caps_get_structure (caps, 0);
  target_str = gst_structure_get_string (s, "texture-target");
  if (!target_str) {
    *target = GST_GL_TEXTURE_TARGET_2D;
    gst_caps_unref (caps);
    return TRUE;
  }

  *target = gst_gl_texture_target_from_string (target_str);
  if (*target == GST_GL_TEXTURE_TARGET_NONE)
    goto unsupported_texture_target;

  gst_caps_unref (caps);

  return TRUE;

no_caps:
  GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
      ("Missing current caps on pad %s", GST_PAD_NAME (pad)), (NULL));
  return FALSE;
unsupported_texture_target:
  GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
      ("Unsupported texture target on pad %s", GST_PAD_NAME (pad)),
      ("texture-target %s", target_str));
  gst_caps_unref (caps);
  return FALSE;
}

static const gchar *
gst_gl_alpha_combine_sampler_for_target (GstGLTextureTarget target)
{
  switch (target) {
    case GST_GL_TEXTURE_TARGET_2D:
      return "sampler2D";
    case GST_GL_TEXTURE_TARGET_EXTERNAL_OES:
      return "samplerExternalOES";
    default:
      g_assert_not_reached ();
      return NULL;
  }
}

static gboolean
gst_gl_alpha_combine_create_shader (GstGLAlphaCombine * self,
    GstGLTextureTarget color_target, GstGLTextureTarget alpha_target)
{
  GstGLBaseMixer *base_mix = GST_GL_BASE_MIXER (self);
  GError *error = NULL;
  const gchar *frags[3];
  gchar *frag;
  guint frag_i = 0;

  if (color_target == GST_GL_TEXTURE_TARGET_EXTERNAL_OES ||
      alpha_target == GST_GL_TEXTURE_TARGET_EXTERNAL_OES)
    frags[frag_i++] = glsl_external_image_extension;
  frags[frag_i++] =
      gst_gl_shader_string_get_highest_precision (base_mix->context,
      GST_GLSL_VERSION_NONE,
      GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY);

  frag = g_strdup_printf (gl_alpha_combine_frag,
      gst_gl_alpha_combine_sampler_for_target (color_target),
      gst_gl_alpha_combine_sampler_for_target (alpha_target));
  frags[frag_i++] = frag;

  if (!(self->shader =
          gst_gl_shader_new_link_with_stages (base_mix->context, &error,
              gst_glsl_stage_new_default_vertex (base_mix->context),
              gst_glsl_stage_new_with_strings (base_mix->context,
                  GL_FRAGMENT_SHADER, GST_GLSL_VERSION_NONE,
                  GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
                  frag_i, frags), NULL))) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("Failed to initialize alpha combine shader"),
        ("%s", error ? error->message : "Unknown error"));
    g_clear_error (&error);
    g_free (frag);
    return FALSE;
  }

  self->shader_color_texture_target = color_target;
  self->shader_alpha_texture_target = alpha_target;
  g_free (frag);

  return TRUE;
}

static gboolean
gst_gl_alpha_combine_ensure_shader (GstGLAlphaCombine * self,
    GstGLTextureTarget color_target, GstGLTextureTarget alpha_target)
{
  if (self->shader &&
      self->shader_color_texture_target == color_target &&
      self->shader_alpha_texture_target == alpha_target)
    return TRUE;

  gst_clear_object (&self->shader);
  return gst_gl_alpha_combine_create_shader (self, color_target, alpha_target);
}

static void
gst_gl_alpha_combine_gl_stop (GstGLBaseMixer * base_mix)
{
  GstGLAlphaCombine *self = GST_GL_ALPHA_COMBINE (base_mix);
  const GstGLFuncs *gl =
      base_mix->context ? base_mix->context->gl_vtable : NULL;

  if (gl) {
    if (self->vao)
      gl->DeleteVertexArrays (1, &self->vao);
    if (self->vertex_buffer)
      gl->DeleteBuffers (1, &self->vertex_buffer);
    if (self->vbo_indices)
      gl->DeleteBuffers (1, &self->vbo_indices);
  }

  self->vao = 0;
  self->vertex_buffer = 0;
  self->vbo_indices = 0;
  self->shader_color_texture_target = GST_GL_TEXTURE_TARGET_NONE;
  self->shader_alpha_texture_target = GST_GL_TEXTURE_TARGET_NONE;
  gst_clear_object (&self->shader);

  GST_GL_BASE_MIXER_CLASS (parent_class)->gl_stop (base_mix);
}

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

static void
gst_gl_alpha_combine_init_vbo (GstGLAlphaCombine * self)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (self)->context->gl_vtable;

  if (!self->vbo_indices) {
    gl->GenBuffers (1, &self->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, self->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);
  }

  if (!self->vertex_buffer) {
    const gfloat vertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
      1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
      1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
      -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };

    gl->GenBuffers (1, &self->vertex_buffer);
    gl->BindBuffer (GL_ARRAY_BUFFER, self->vertex_buffer);
    gl->BufferData (GL_ARRAY_BUFFER, sizeof (vertices), vertices,
        GL_STATIC_DRAW);
  } else {
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, self->vbo_indices);
    gl->BindBuffer (GL_ARRAY_BUFFER, self->vertex_buffer);
  }
}

static gboolean
gst_gl_alpha_combine_callback (gpointer data)
{
  GstGLAlphaCombine *self = GST_GL_ALPHA_COMBINE (data);
  GstGLBaseMixer *base_mix = GST_GL_BASE_MIXER (self);
  const GstGLFuncs *gl = base_mix->context->gl_vtable;
  guint color_tex, alpha_tex;
  gint attr_position_loc, attr_texture_loc;
  GstGLAlphaCombineAlphaComponent alpha_component;
  GstGLTextureTarget color_target, alpha_target;

  GST_OBJECT_LOCK (self);
  color_tex = self->color_pad->current_texture;
  alpha_tex = self->alpha_pad->current_texture;
  alpha_component = self->alpha_component;
  color_target = self->color_texture_target;
  alpha_target = self->alpha_texture_target;
  GST_OBJECT_UNLOCK (self);

  if (!color_tex || !alpha_tex) {
    GST_DEBUG_OBJECT (self, "Waiting for both color and alpha textures");
    return TRUE;
  }

  if (!gst_gl_alpha_combine_ensure_shader (self, color_target, alpha_target))
    return FALSE;

  gst_gl_context_clear_shader (base_mix->context);

  gl->Disable (GL_DEPTH_TEST);
  gl->Disable (GL_CULL_FACE);
  gl->Disable (GL_BLEND);

  gst_gl_shader_use (self->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (self->shader, "a_position");
  attr_texture_loc =
      gst_gl_shader_get_attribute_location (self->shader, "a_texcoord");

  if (gl->GenVertexArrays) {
    if (!self->vao)
      gl->GenVertexArrays (1, &self->vao);
    gl->BindVertexArray (self->vao);
  }

  gst_gl_alpha_combine_init_vbo (self);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (gst_gl_texture_target_to_gl (color_target), color_tex);
  gst_gl_shader_set_uniform_1i (self->shader, "color_tex", 0);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (gst_gl_texture_target_to_gl (alpha_target), alpha_tex);
  gst_gl_shader_set_uniform_1i (self->shader, "alpha_tex", 1);
  switch (alpha_component) {
    case GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_GREEN:
      gst_gl_shader_set_uniform_4f (self->shader, "alpha_selector",
          0.0, 1.0, 0.0, 0.0);
      break;
    case GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_BLUE:
      gst_gl_shader_set_uniform_4f (self->shader, "alpha_selector",
          0.0, 0.0, 1.0, 0.0);
      break;
    case GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_ALPHA:
      gst_gl_shader_set_uniform_4f (self->shader, "alpha_selector",
          0.0, 0.0, 0.0, 1.0);
      break;
    case GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_RED:
    default:
      gst_gl_shader_set_uniform_4f (self->shader, "alpha_selector",
          1.0, 0.0, 0.0, 0.0);
      break;
  }

  gl->EnableVertexAttribArray (attr_position_loc);
  gl->EnableVertexAttribArray (attr_texture_loc);

  gl->VertexAttribPointer (attr_position_loc, 3, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), (void *) 0);
  gl->VertexAttribPointer (attr_texture_loc, 2, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->DisableVertexAttribArray (attr_texture_loc);

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);
  gl->ActiveTexture (GL_TEXTURE1);
  gl->BindTexture (gst_gl_texture_target_to_gl (alpha_target), 0);
  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (gst_gl_texture_target_to_gl (color_target), 0);

  gst_gl_context_clear_shader (base_mix->context);

  if (gl->GenVertexArrays)
    gl->BindVertexArray (0);

  return TRUE;
}

static void
gst_gl_alpha_combine_process_gl (GstGLContext * context,
    GstGLAlphaCombine * self)
{
  GstGLFramebuffer *fbo = gst_gl_mixer_get_framebuffer (GST_GL_MIXER (self));

  gst_gl_framebuffer_draw_to_texture (fbo, self->out_tex,
      gst_gl_alpha_combine_callback, self);

  gst_clear_object (&fbo);
}

static gboolean
gst_gl_alpha_combine_process_textures (GstGLMixer * mixer,
    GstGLMemory * out_tex)
{
  GstGLAlphaCombine *self = GST_GL_ALPHA_COMBINE (mixer);
  GstGLContext *context = GST_GL_BASE_MIXER (mixer)->context;
  GstGLTextureTarget color_target, alpha_target;

  if (!gst_gl_alpha_combine_pad_get_texture_target (self, self->color_pad,
          &color_target) ||
      !gst_gl_alpha_combine_pad_get_texture_target (self, self->alpha_pad,
          &alpha_target))
    return FALSE;

  self->out_tex = out_tex;
  GST_OBJECT_LOCK (self);
  self->color_texture_target = color_target;
  self->alpha_texture_target = alpha_target;
  GST_OBJECT_UNLOCK (self);

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) gst_gl_alpha_combine_process_gl, self);

  return TRUE;
}
