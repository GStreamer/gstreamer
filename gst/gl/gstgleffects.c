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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gstglfilter.h>
//#include "effects/shadersources.h"
//#include "effects/textures.h"

#define GST_TYPE_GL_EFFECTS            (gst_gl_effects_get_type())
#define GST_GL_EFFECTS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GL_EFFECTS,GstGLEffects))
#define GST_IS_GL_EFFECTS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GL_EFFECTS))
#define GST_GL_EFFECTS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) , GST_TYPE_GL_EFFECTS,GstGLEffectsClass))
#define GST_IS_GL_EFFECTS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) , GST_TYPE_GL_EFFECTS))
#define GST_GL_EFFECTS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) , GST_TYPE_GL_EFFECTS,GstGLEffectsClass))

typedef struct _GstGLEffects GstGLEffects;
typedef struct _GstGLEffectsClass GstGLEffectsClass;

typedef enum {
  GST_GL_EFFECT_IDENTITY,
  GST_GL_EFFECT_SQUEEZE,
  GST_GL_EFFECT_STRETCH,
  GST_GL_EFFECT_TUNNEL,
  GST_GL_EFFECT_FISHEYE,
  GST_GL_EFFECT_TWIRL,
  GST_GL_EFFECT_BULGE,
  GST_GL_EFFECT_SQUARE,
  GST_GL_EFFECT_MIRROR,
  GST_GL_EFFECT_HEAT,
  GST_GL_EFFECT_SEPIA,
  GST_GL_EFFECT_CROSS,
  GST_GL_EFFECT_GLOW,
  GST_GL_EFFECT_EMBOSS,
  GST_GL_EFFECT_BACKGROUND,
  GST_GL_EFFECT_TEST,
  GST_GL_N_EFFECTS
} GstGLEffectsEffect;

#define NEEDED_TEXTURES 10

typedef void (* GstGLEffectProcessFunc) (GstGLEffects *effects);

struct _GstGLEffects
{
  GstGLFilter filter;
  
  GstGLEffectProcessFunc effect;

  GLuint intexture;
  GLuint midtexture[NEEDED_TEXTURES];
  GLuint outtexture;

  GHashTable *shaderstable;
};

struct _GstGLEffectsClass
{
  GstGLFilterClass filter_class;
};

GType gst_gl_effects_get_type (void);

#define GST_CAT_DEFAULT gst_gl_effects_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_effects_debug, "gleffects", 0, "gleffects element");

GST_BOILERPLATE_FULL (GstGLEffects, gst_gl_effects, GstGLFilter,
		      GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_effects_set_property (GObject * object, guint prop_id,
					 const GValue * value, GParamSpec * pspec);
static void gst_gl_effects_get_property (GObject * object, guint prop_id,
					 GValue * value, GParamSpec * pspec);
static void gst_gl_effects_draw_texture (GstGLEffects * effects, GLuint tex);

static void gst_gl_effects_init_resources (GstGLFilter* filter);
static void gst_gl_effects_reset_resources (GstGLFilter* filter);

static gboolean gst_gl_effects_filter (GstGLFilter * filter,
				       GstGLBuffer * inbuf, GstGLBuffer * outbuf);

#define GST_TYPE_GL_EFFECTS_EFFECT (gst_gl_effects_effect_get_type ())
static GType
gst_gl_effects_effect_get_type (void)
{
  static GType gl_effects_effect_type = 0;
  static const GEnumValue effect_types [] = {
    { GST_GL_EFFECT_IDENTITY, "Do nothing Effect", "identity" },
    { GST_GL_EFFECT_SQUEEZE, "Squeeze Effect", "squeeze" },
    { GST_GL_EFFECT_STRETCH, "Stretch Effect", "stretch" },
    { GST_GL_EFFECT_FISHEYE, "FishEye Effect", "fisheye" },
    { GST_GL_EFFECT_TWIRL, "Twirl Effect", "twirl" },
    { GST_GL_EFFECT_BULGE, "Bulge Effect", "bulge" },
    { GST_GL_EFFECT_TUNNEL, "Light Tunnel Effect", "tunnel" },
    { GST_GL_EFFECT_SQUARE, "Square Effect", "square" },
    { GST_GL_EFFECT_MIRROR, "Mirror Effect", "mirror" },
    { GST_GL_EFFECT_HEAT, "Heat Signature Effect", "heat" },
    { GST_GL_EFFECT_SEPIA, "Sepia Tone Effect", "sepia" },
    { GST_GL_EFFECT_CROSS, "Cross Processing Effect", "cross" },
    { GST_GL_EFFECT_GLOW, "Glow Lighting Effect", "glow" },
    { GST_GL_EFFECT_EMBOSS, "Emboss Convolution Effect", "emboss" },
    { GST_GL_EFFECT_BACKGROUND, "Difference Matte Effect", "background" },
    { GST_GL_EFFECT_TEST, "Test Effect", "test" },
    { 0, NULL, NULL }
  };
     
  if (!gl_effects_effect_type) {
    gl_effects_effect_type = 
      g_enum_register_static ("GstGLEffectsEffect", effect_types);
  }
  return gl_effects_effect_type;
}

static const GstElementDetails element_details = GST_ELEMENT_DETAILS (
  "Gstreamer OpenGL Effects",
  "Filter/Effect",
  "GL Shading Language effects",
  "Filippo Argiolas <filippo.argiolas@gmail.com>");

enum
{
  PROP_0,
  PROP_EFFECT
};

static void
gst_gl_effects_init_gl_resources (GstGLFilter *filter)
{
  GstGLEffects *effects = GST_GL_EFFECTS (filter);
  gint i;
  
  for (i=0; i<NEEDED_TEXTURES; i++) {
    glGenTextures (1, &effects->midtexture[i]);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, effects->midtexture[i]);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
		 filter->width, filter->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

static void
gst_gl_effects_reset_gl_resources (GstGLFilter *filter)
{
  GstGLEffects *effects = GST_GL_EFFECTS (filter);
  gint i;

  for (i=0; i<10; i++) {
    glDeleteTextures (1, &effects->midtexture[i]);
    effects->midtexture[i] = 0;
  }
}

static void
gst_gl_effects_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_effects_class_init (GstGLEffectsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_effects_set_property;
  gobject_class->get_property = gst_gl_effects_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_effects_filter;
  GST_GL_FILTER_CLASS (klass)->display_init_cb = gst_gl_effects_init_gl_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb = gst_gl_effects_reset_gl_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_effects_init_resources;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_effects_reset_resources;

  g_object_class_install_property (
    gobject_class,
    PROP_EFFECT,
    g_param_spec_enum ("effect",
		       "Effect",
		       "Select which effect apply to GL video texture",
		       GST_TYPE_GL_EFFECTS_EFFECT,
		       GST_GL_EFFECT_IDENTITY,
		       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gl_effects_draw_texture (GstGLEffects * effects, GLuint tex)
{
  GstGLFilter *filter = GST_GL_FILTER (effects);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);

  glBegin (GL_QUADS);

  glTexCoord2f (0.0, 0.0);
  glVertex2f (-1.0, -1.0);
  glTexCoord2f (filter->width, 0.0);
  glVertex2f (1.0, -1.0);
  glTexCoord2f (filter->width, filter->height);
  glVertex2f (1.0, 1.0);
  glTexCoord2f (0.0, filter->height);
  glVertex2f (-1.0, 1.0);

  glEnd ();
}

static void
gst_gl_effects_identity_callback (gint width, gint height, guint texture, gpointer data)
{
  GstGLEffects* effects = GST_GL_EFFECTS (data);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_effects_draw_texture (effects, texture);
}

static void
change_view (GstGLDisplay *display, gpointer data)
{
//  GstGLEffects *effects = GST_GL_EFFECTS (data);

  const double mirrormatrix[16] = {
    -1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
  };

  glMatrixMode (GL_MODELVIEW);
  glLoadMatrixd (mirrormatrix);
}

static void
gst_gl_effects_identity (GstGLEffects *effects) {
  GstGLFilter *filter = GST_GL_FILTER (effects);

  gst_gl_display_thread_add (filter->display, change_view, effects);
  gst_gl_filter_render_to_target (filter, effects->intexture, effects->outtexture,
				  gst_gl_effects_identity_callback, effects);
}

static void
gst_gl_effects_init (GstGLEffects * effects, GstGLEffectsClass * klass)
{
  effects->shaderstable = g_hash_table_new_full (g_str_hash, 
						 g_str_equal,
						 NULL,
						 g_object_unref);
  effects->effect = gst_gl_effects_identity;
}

static void
gst_gl_effects_reset_resources (GstGLFilter* filter)
{
  GstGLEffects* effects = GST_GL_EFFECTS(filter);

  g_hash_table_unref (effects->shaderstable);
  effects->shaderstable = NULL;
}

static void
gst_gl_effects_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstGLEffects *effects = GST_GL_EFFECTS (object); */

  switch (prop_id) {
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
    gst_gl_effects_set_effect (effects, g_value_get_enum (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_effects_init_resources (GstGLFilter* filter)
{
//  GstGLEffects* blur_filter = GST_GL_EFFECTS (filter);
}

static gboolean
gst_gl_effects_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
				GstGLBuffer* outbuf)
{
  GstGLEffects* effects = GST_GL_EFFECTS(filter);

  effects->intexture = inbuf->texture;
  effects->outtexture = outbuf->texture;
  
  effects->effect (effects);
  
  return TRUE;
}
