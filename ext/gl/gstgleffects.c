/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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
 * SECTION:element-gleffects.
 *
 * GL Shading Language effects.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch videotestsrc ! glupload ! gleffects effect=5 ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) and GLSL (OpenGL Shading Language) are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gstglconfig.h>
#include "gstgleffects.h"

#define GST_TYPE_GL_EFFECTS            (gst_gl_effects_get_type())
#define GST_GL_EFFECTS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GL_EFFECTS,GstGLEffects))
#define GST_IS_GL_EFFECTS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GL_EFFECTS))
#define GST_GL_EFFECTS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) , GST_TYPE_GL_EFFECTS,GstGLEffectsClass))
#define GST_IS_GL_EFFECTS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) , GST_TYPE_GL_EFFECTS))
#define GST_GL_EFFECTS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) , GST_TYPE_GL_EFFECTS,GstGLEffectsClass))

#define GST_CAT_DEFAULT gst_gl_effects_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_effects_debug, "gleffects", 0, "gleffects element");

G_DEFINE_TYPE_WITH_CODE (GstGLEffects, gst_gl_effects, GST_TYPE_GL_FILTER,
    DEBUG_INIT);

static void gst_gl_effects_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_effects_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_effects_init_resources (GstGLFilter * filter);
static void gst_gl_effects_reset_resources (GstGLFilter * filter);

static gboolean gst_gl_effects_on_init_gl_context (GstGLFilter * filter);

static void gst_gl_effects_ghash_func_clean (gpointer key, gpointer value,
    gpointer data);

static gboolean gst_gl_effects_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);

/* dont' forget to edit the following when a new effect is added */
typedef enum
{
  GST_GL_EFFECT_IDENTITY,
  GST_GL_EFFECT_MIRROR,
  GST_GL_EFFECT_SQUEEZE,
  GST_GL_EFFECT_STRETCH,
  GST_GL_EFFECT_TUNNEL,
  GST_GL_EFFECT_FISHEYE,
  GST_GL_EFFECT_TWIRL,
  GST_GL_EFFECT_BULGE,
  GST_GL_EFFECT_SQUARE,
  GST_GL_EFFECT_HEAT,
  GST_GL_EFFECT_SEPIA,
  GST_GL_EFFECT_XPRO,
  GST_GL_EFFECT_LUMA_XPRO,
  GST_GL_EFFECT_XRAY,
  GST_GL_EFFECT_SIN,
  GST_GL_EFFECT_GLOW,
  GST_GL_N_EFFECTS
} GstGLEffectsEffect;

#define GST_TYPE_GL_EFFECTS_EFFECT (gst_gl_effects_effect_get_type ())
static GType
gst_gl_effects_effect_get_type (void)
{
  static GType gl_effects_effect_type = 0;
  static const GEnumValue effect_types[] = {
    {GST_GL_EFFECT_IDENTITY, "Do nothing Effect", "identity"},
    {GST_GL_EFFECT_MIRROR, "Mirror Effect", "mirror"},
    {GST_GL_EFFECT_SQUEEZE, "Squeeze Effect", "squeeze"},
#if GST_GL_HAVE_OPENGL
    {GST_GL_EFFECT_STRETCH, "Stretch Effect", "stretch"},
    {GST_GL_EFFECT_FISHEYE, "FishEye Effect", "fisheye"},
    {GST_GL_EFFECT_TWIRL, "Twirl Effect", "twirl"},
    {GST_GL_EFFECT_BULGE, "Bulge Effect", "bulge"},
    {GST_GL_EFFECT_TUNNEL, "Light Tunnel Effect", "tunnel"},
    {GST_GL_EFFECT_SQUARE, "Square Effect", "square"},
    {GST_GL_EFFECT_HEAT, "Heat Signature Effect", "heat"},
    {GST_GL_EFFECT_SEPIA, "Sepia Toning Effect", "sepia"},
    {GST_GL_EFFECT_XPRO, "Cross Processing Effect", "xpro"},
    {GST_GL_EFFECT_LUMA_XPRO, "Luma Cross Processing Effect", "lumaxpro"},
    {GST_GL_EFFECT_XRAY, "Glowing negative effect", "xray"},
    {GST_GL_EFFECT_SIN, "All Grey but Red Effect", "sin"},
    {GST_GL_EFFECT_GLOW, "Glow Lighting Effect", "glow"},
#endif
    {0, NULL, NULL}
  };

  if (!gl_effects_effect_type) {
    gl_effects_effect_type =
        g_enum_register_static ("GstGLEffectsEffect", effect_types);
  }
  return gl_effects_effect_type;
}

static void
gst_gl_effects_set_effect (GstGLEffects * effects, gint effect_type)
{

  switch (effect_type) {
    case GST_GL_EFFECT_IDENTITY:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_identity;
      break;
    case GST_GL_EFFECT_MIRROR:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_mirror;
      break;
    case GST_GL_EFFECT_SQUEEZE:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_squeeze;
      break;
#if GST_GL_HAVE_OPENGL
    case GST_GL_EFFECT_STRETCH:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_stretch;
      break;
    case GST_GL_EFFECT_TUNNEL:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_tunnel;
      break;
    case GST_GL_EFFECT_FISHEYE:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_fisheye;
      break;
    case GST_GL_EFFECT_TWIRL:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_twirl;
      break;
    case GST_GL_EFFECT_BULGE:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_bulge;
      break;
    case GST_GL_EFFECT_SQUARE:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_square;
      break;
    case GST_GL_EFFECT_HEAT:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_heat;
      break;
    case GST_GL_EFFECT_SEPIA:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_sepia;
      break;
    case GST_GL_EFFECT_XPRO:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_xpro;
      break;
    case GST_GL_EFFECT_LUMA_XPRO:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_luma_xpro;
      break;
    case GST_GL_EFFECT_XRAY:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_xray;
      break;
    case GST_GL_EFFECT_SIN:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_sin;
      break;
    case GST_GL_EFFECT_GLOW:
      effects->effect = (GstGLEffectProcessFunc) gst_gl_effects_glow;
      break;
#endif
    default:
      g_assert_not_reached ();
  }
  effects->current_effect = effect_type;
}

/* init resources that need a gl context */
static void
gst_gl_effects_init_gl_resources (GstGLFilter * filter)
{
  GstGLEffects *effects = GST_GL_EFFECTS (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;
  gint i = 0;

  for (i = 0; i < NEEDED_TEXTURES; i++) {

    if (effects->midtexture[i]) {
      gl->DeleteTextures (1, &effects->midtexture[i]);
      effects->midtexture[i] = 0;
    }

    gl->GenTextures (1, &effects->midtexture[i]);
    gl->BindTexture (GL_TEXTURE_2D, effects->midtexture[i]);
    gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
        GST_VIDEO_INFO_WIDTH (&filter->out_info),
        GST_VIDEO_INFO_HEIGHT (&filter->out_info),
        0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

/* free resources that need a gl context */
static void
gst_gl_effects_reset_gl_resources (GstGLFilter * filter)
{
  GstGLEffects *effects = GST_GL_EFFECTS (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;
  gint i = 0;

  for (i = 0; i < NEEDED_TEXTURES; i++) {
    gl->DeleteTextures (1, &effects->midtexture[i]);
    effects->midtexture[i] = 0;
  }
  for (i = 0; i < GST_GL_EFFECTS_N_CURVES; i++) {
    gl->DeleteTextures (1, &effects->curve[i]);
    effects->curve[i] = 0;
  }
}

static void
gst_gl_effects_class_init (GstGLEffectsClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_effects_set_property;
  gobject_class->get_property = gst_gl_effects_get_property;

  GST_GL_FILTER_CLASS (klass)->filter_texture = gst_gl_effects_filter_texture;
  GST_GL_FILTER_CLASS (klass)->display_init_cb =
      gst_gl_effects_init_gl_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb =
      gst_gl_effects_reset_gl_resources;
  GST_GL_FILTER_CLASS (klass)->onStart = gst_gl_effects_init_resources;
  GST_GL_FILTER_CLASS (klass)->onStop = gst_gl_effects_reset_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_effects_on_init_gl_context;

  g_object_class_install_property (gobject_class,
      PROP_EFFECT,
      g_param_spec_enum ("effect",
          "Effect",
          "Select which effect apply to GL video texture",
          GST_TYPE_GL_EFFECTS_EFFECT,
          GST_GL_EFFECT_IDENTITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_HSWAP,
      g_param_spec_boolean ("hswap",
          "Horizontal Swap",
          "Switch video texture left to right, useful with webcams",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class,
      "Gstreamer OpenGL Effects", "Filter/Effect/Video",
      "GL Shading Language effects",
      "Filippo Argiolas <filippo.argiolas@gmail.com>");
}

static void
set_horizontal_swap (GstGLContext * context, gpointer data)
{
#if GST_GL_HAVE_OPENGL
  GstGLFuncs *gl = context->gl_vtable;

  if (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL) {
    const gfloat mirrormatrix[16] = {
      -1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0
    };

    gl->MatrixMode (GL_MODELVIEW);
    gl->LoadMatrixf (mirrormatrix);
  }
#endif
}

static void
gst_gl_effects_init (GstGLEffects * effects)
{
  effects->effect = gst_gl_effects_identity;
  effects->horizontal_swap = FALSE;
}

static void
gst_gl_effects_ghash_func_clean (gpointer key, gpointer value, gpointer data)
{
  GstGLShader *shader = (GstGLShader *) value;
  GstGLFilter *filter = (GstGLFilter *) data;

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_context_del_shader (filter->context, shader);

  value = NULL;
}

static void
gst_gl_effects_reset_resources (GstGLFilter * filter)
{
  GstGLEffects *effects = GST_GL_EFFECTS (filter);

  /* release shaders in the gl thread */
  g_hash_table_foreach (effects->shaderstable, gst_gl_effects_ghash_func_clean,
      filter);

  /* clean the htable without calling values destructors
   * because shaders have been released in the glthread
   * through the foreach func */
  g_hash_table_unref (effects->shaderstable);
  effects->shaderstable = NULL;
}

static void
gst_gl_effects_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLEffects *effects = GST_GL_EFFECTS (object);

  switch (prop_id) {
    case PROP_EFFECT:
      gst_gl_effects_set_effect (effects, g_value_get_enum (value));
      break;
    case PROP_HSWAP:
      effects->horizontal_swap = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_effects_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLEffects *effects = GST_GL_EFFECTS (object);

  switch (prop_id) {
    case PROP_EFFECT:
      g_value_set_enum (value, effects->current_effect);
      break;
    case PROP_HSWAP:
      g_value_set_boolean (value, effects->horizontal_swap);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_effects_init_resources (GstGLFilter * filter)
{
  GstGLEffects *effects = GST_GL_EFFECTS (filter);
  gint i;

  effects->shaderstable = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = 0; i < NEEDED_TEXTURES; i++) {
    effects->midtexture[i] = 0;
  }
  for (i = 0; i < GST_GL_EFFECTS_N_CURVES; i++) {
    effects->curve[i] = 0;
  }
}

static gboolean
gst_gl_effects_on_init_gl_context (GstGLFilter * filter)
{
  return TRUE;
}

static gboolean
gst_gl_effects_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLEffects *effects = GST_GL_EFFECTS (filter);

  effects->intexture = in_tex;
  effects->outtexture = out_tex;

  if (effects->horizontal_swap == TRUE)
    gst_gl_context_thread_add (filter->context, set_horizontal_swap, effects);

  effects->effect (effects);

  return TRUE;
}
