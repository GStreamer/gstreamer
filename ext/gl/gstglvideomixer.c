/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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
 * SECTION:element-glvideomixer
 * @title: glvideomixer
 *
 * Composites a number of streams into a single output scene using OpenGL in
 * a similar fashion to compositor and videomixer. See the compositor plugin
 * for documentation about the #GstGLVideoMixerPad properties.
 *
 * ## Examples
 * |[
 * gst-launch-1.0  glvideomixer name=m ! glimagesink \
 *     videotestsrc ! video/x-raw, format=YUY2 ! glupload ! glcolorconvert ! m. \
 *     videotestsrc pattern=12 ! video/x-raw, format=I420, framerate=5/1, width=100, height=200 ! queue ! \
 *     glupload ! glcolorconvert ! m. \
 *     videotestsrc ! glupload ! gleffects effect=2 ! queue ! m.  \
 *     videotestsrc ! glupload ! glfiltercube ! queue ! m. \
 *     videotestsrc ! glupload ! gleffects effect=6 ! queue ! m.gst-launch-1.0  glvideomixer name=m ! glimagesink \
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/gstvideoaffinetransformationmeta.h>
#include <gst/controller/gstproxycontrolbinding.h>

#include "gstglvideomixer.h"
#include "gstglmixerbin.h"
#include "gstglutils.h"

#define GST_CAT_DEFAULT gst_gl_video_mixer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_GL_VIDEO_MIXER_BACKGROUND (gst_gl_video_mixer_background_get_type())
static GType
gst_gl_video_mixer_background_get_type (void)
{
  static GType mixer_background_type = 0;

  static const GEnumValue mixer_background[] = {
    {GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER, "Checker pattern", "checker"},
    {GST_GL_VIDEO_MIXER_BACKGROUND_BLACK, "Black", "black"},
    {GST_GL_VIDEO_MIXER_BACKGROUND_WHITE, "White", "white"},
    {GST_GL_VIDEO_MIXER_BACKGROUND_TRANSPARENT,
        "Transparent Background to enable further compositing", "transparent"},
    {0, NULL, NULL},
  };

  if (!mixer_background_type) {
    mixer_background_type =
        g_enum_register_static ("GstGLVideoMixerBackground", mixer_background);
  }
  return mixer_background_type;
}

#define GST_TYPE_GL_VIDEO_MIXER_BLEND_EQUATION (gst_gl_video_mixer_blend_equation_get_type())
static GType
gst_gl_video_mixer_blend_equation_get_type (void)
{
  static GType mixer_blend_equation_type = 0;

  static const GEnumValue mixer_blend_equations[] = {
    {GST_GL_VIDEO_MIXER_BLEND_EQUATION_ADD, "Add", "add"},
    {GST_GL_VIDEO_MIXER_BLEND_EQUATION_SUBTRACT, "Subtract", "subtract"},
    {GST_GL_VIDEO_MIXER_BLEND_EQUATION_REVERSE_SUBTRACT, "Reverse Subtract",
        "reverse-subtract"},
    {0, NULL, NULL},
  };

  if (!mixer_blend_equation_type) {
    mixer_blend_equation_type =
        g_enum_register_static ("GstGLVideoMixerBlendEquation",
        mixer_blend_equations);
  }
  return mixer_blend_equation_type;
}

#define GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION (gst_gl_video_mixer_blend_function_get_type())
static GType
gst_gl_video_mixer_blend_function_get_type (void)
{
  static GType mixer_blend_function_type = 0;

  static const GEnumValue mixer_blend_funcs[] = {
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ZERO, "Zero", "zero"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE, "One", "one"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_COLOR, "Source Color", "src-color"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_SRC_COLOR,
        "One Minus Source Color", "one-minus-src-color"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_DST_COLOR, "Destination Color",
        "dst-color"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_DST_COLOR,
        "One Minus Destination Color", "one-minus-dst-color"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA, "Source Alpha", "src-alpha"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_SRC_ALPHA,
        "One Minus Source Alpha", "one-minus-src-alpha"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_DST_ALPHA, "Destination Alpha",
        "dst-alpha"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_DST_ALPHA,
        "One Minus Destination Alpha", "one-minus-dst-alpha"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_CONSTANT_COLOR, "Constant Color",
        "constant-color"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_CONSTANT_COLOR,
        "One Minus Constant Color", "one-minus-contant-color"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_CONSTANT_ALPHA, "Constant Alpha",
        "constant-alpha"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_CONSTANT_COLOR,
        "One Minus Constant Alpha", "one-minus-contant-alpha"},
    {GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA_SATURATE,
        "Source Alpha Saturate", "src-alpha-saturate"},
    {0, NULL, NULL},
  };

  if (!mixer_blend_function_type) {
    mixer_blend_function_type =
        g_enum_register_static ("GstGLVideoMixerBlendFunction",
        mixer_blend_funcs);
  }
  return mixer_blend_function_type;
}

#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_WIDTH  0
#define DEFAULT_PAD_HEIGHT 0
#define DEFAULT_PAD_ALPHA  1.0
#define DEFAULT_PAD_ZORDER 0
#define DEFAULT_PAD_IGNORE_EOS FALSE
#define DEFAULT_PAD_BLEND_EQUATION_RGB GST_GL_VIDEO_MIXER_BLEND_EQUATION_ADD
#define DEFAULT_PAD_BLEND_EQUATION_ALPHA GST_GL_VIDEO_MIXER_BLEND_EQUATION_ADD
#define DEFAULT_PAD_BLEND_FUNCTION_SRC_RGB GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA
#define DEFAULT_PAD_BLEND_FUNCTION_SRC_ALPHA GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA
#define DEFAULT_PAD_BLEND_FUNCTION_DST_RGB GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_SRC_ALPHA
#define DEFAULT_PAD_BLEND_FUNCTION_DST_ALPHA GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_SRC_ALPHA

enum
{
  PROP_INPUT_0,
  PROP_INPUT_XPOS,
  PROP_INPUT_YPOS,
  PROP_INPUT_WIDTH,
  PROP_INPUT_HEIGHT,
  PROP_INPUT_ALPHA,
  PROP_INPUT_BLEND_EQUATION_RGB,
  PROP_INPUT_BLEND_EQUATION_ALPHA,
  PROP_INPUT_BLEND_FUNCTION_SRC_RGB,
  PROP_INPUT_BLEND_FUNCTION_SRC_ALPHA,
  PROP_INPUT_BLEND_FUNCTION_DST_RGB,
  PROP_INPUT_BLEND_FUNCTION_DST_ALPHA,
  PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_RED,
  PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_GREEN,
  PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_BLUE,
  PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_ALPHA,
  PROP_INPUT_ZORDER,
  PROP_INPUT_IGNORE_EOS,
};

static void gst_gl_video_mixer_input_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_input_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

typedef struct _GstGLVideoMixerInput GstGLVideoMixerInput;
typedef GstGhostPadClass GstGLVideoMixerInputClass;

struct _GstGLVideoMixerInput
{
  GstGhostPad parent;

  GstSegment segment;

  GstPad *mixer_pad;
};

GType gst_gl_video_mixer_input_get_type (void);
G_DEFINE_TYPE (GstGLVideoMixerInput, gst_gl_video_mixer_input,
    GST_TYPE_GHOST_PAD);

static void
gst_gl_video_mixer_input_init (GstGLVideoMixerInput * self)
{
}

static void
gst_gl_video_mixer_input_class_init (GstGLVideoMixerInputClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_gl_video_mixer_input_set_property;
  gobject_class->get_property = gst_gl_video_mixer_input_get_property;

  g_object_class_install_property (gobject_class, PROP_INPUT_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, 10000, DEFAULT_PAD_ZORDER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_IGNORE_EOS,
      g_param_spec_boolean ("ignore-eos", "Ignore EOS", "Aggregate the last "
          "frame on pads that are EOS till they are released",
          DEFAULT_PAD_IGNORE_EOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_EQUATION_RGB,
      g_param_spec_enum ("blend-equation-rgb", "Blend Equation RGB",
          "Blend Equation for RGB",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_EQUATION,
          DEFAULT_PAD_BLEND_EQUATION_RGB,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_EQUATION_ALPHA,
      g_param_spec_enum ("blend-equation-alpha", "Blend Equation Alpha",
          "Blend Equation for Alpha", GST_TYPE_GL_VIDEO_MIXER_BLEND_EQUATION,
          DEFAULT_PAD_BLEND_EQUATION_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_SRC_RGB,
      g_param_spec_enum ("blend-function-src-rgb", "Blend Function Source RGB",
          "Blend Function for Source RGB",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_SRC_RGB,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_SRC_ALPHA,
      g_param_spec_enum ("blend-function-src-alpha",
          "Blend Function Source Alpha", "Blend Function for Source Alpha",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_SRC_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_DST_RGB,
      g_param_spec_enum ("blend-function-dst-rgb",
          "Blend Function Destination RGB",
          "Blend Function for Destination RGB",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_DST_RGB,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_DST_ALPHA,
      g_param_spec_enum ("blend-function-dst-alpha",
          "Blend Function Destination Alpha",
          "Blend Function for Destiniation Alpha",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_DST_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_RED,
      g_param_spec_double ("blend-constant-color-red",
          "Blend Constant Color Red", "Blend Constant Color Red", 0.0, 1.0, 0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_GREEN,
      g_param_spec_double ("blend-constant-color-green",
          "Blend Constant Color Green", "Blend Constant Color Green", 0.0, 1.0,
          0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_BLUE,
      g_param_spec_double ("blend-constant-color-blue",
          "Blend Constant Color Green", "Blend Constant Color Green", 0.0, 1.0,
          0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_CONSTANT_COLOR_ALPHA,
      g_param_spec_double ("blend-constant-color-alpha",
          "Blend Constant Color Alpha", "Blend Constant Color Alpha", 0.0, 1.0,
          0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gl_video_mixer_input_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerInput *self = (GstGLVideoMixerInput *) object;

  if (self->mixer_pad)
    g_object_get_property (G_OBJECT (self->mixer_pad), pspec->name, value);
}

static void
gst_gl_video_mixer_input_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerInput *self = (GstGLVideoMixerInput *) object;

  if (self->mixer_pad)
    g_object_set_property (G_OBJECT (self->mixer_pad), pspec->name, value);
}

static GstGhostPad *
_create_video_mixer_input (GstGLMixerBin * self, GstPad * mixer_pad)
{
  GstGLVideoMixerInput *input =
      g_object_new (gst_gl_video_mixer_input_get_type (), "name",
      GST_OBJECT_NAME (mixer_pad), "direction", GST_PAD_DIRECTION (mixer_pad),
      NULL);

  if (!gst_ghost_pad_construct (GST_GHOST_PAD (input))) {
    gst_object_unref (input);
    return NULL;
  }
#define ADD_BINDING(obj,ref,prop) \
    gst_object_add_control_binding (GST_OBJECT (obj), \
        gst_proxy_control_binding_new (GST_OBJECT (obj), prop, \
            GST_OBJECT (ref), prop));
  ADD_BINDING (mixer_pad, input, "zorder");
  ADD_BINDING (mixer_pad, input, "xpos");
  ADD_BINDING (mixer_pad, input, "ypos");
  ADD_BINDING (mixer_pad, input, "width");
  ADD_BINDING (mixer_pad, input, "height");
  ADD_BINDING (mixer_pad, input, "alpha");
  ADD_BINDING (mixer_pad, input, "blend-equation-rgb");
  ADD_BINDING (mixer_pad, input, "blend-equation-alpha");
  ADD_BINDING (mixer_pad, input, "blend-function-src-rgb");
  ADD_BINDING (mixer_pad, input, "blend-function-src-alpha");
  ADD_BINDING (mixer_pad, input, "blend-function-dst-rgb");
  ADD_BINDING (mixer_pad, input, "blend-function-dst-alpha");
  ADD_BINDING (mixer_pad, input, "blend-constant-color-red");
  ADD_BINDING (mixer_pad, input, "blend-constant-color-green");
  ADD_BINDING (mixer_pad, input, "blend-constant-color-blue");
  ADD_BINDING (mixer_pad, input, "blend-constant-color-alpha");
#undef ADD_BINDING

  input->mixer_pad = mixer_pad;

  return GST_GHOST_PAD (input);
}

enum
{
  PROP_BIN_0,
  PROP_BIN_BACKGROUND,
};
#define DEFAULT_BACKGROUND GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER

static void gst_gl_video_mixer_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

typedef GstGLMixerBin GstGLVideoMixerBin;
typedef GstGLMixerBinClass GstGLVideoMixerBinClass;

G_DEFINE_TYPE (GstGLVideoMixerBin, gst_gl_video_mixer_bin,
    GST_TYPE_GL_MIXER_BIN);

static void
gst_gl_video_mixer_bin_init (GstGLVideoMixerBin * self)
{
  GstGLMixerBin *mix_bin = GST_GL_MIXER_BIN (self);

  gst_gl_mixer_bin_finish_init_with_element (mix_bin,
      g_object_new (GST_TYPE_GL_VIDEO_MIXER, NULL));
}

static void
gst_gl_video_mixer_bin_class_init (GstGLVideoMixerBinClass * klass)
{
  GstGLMixerBinClass *mixer_class = GST_GL_MIXER_BIN_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  mixer_class->create_input_pad = _create_video_mixer_input;

  gobject_class->set_property = gst_gl_video_mixer_bin_set_property;
  gobject_class->get_property = gst_gl_video_mixer_bin_get_property;

  g_object_class_install_property (gobject_class, PROP_BIN_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_GL_VIDEO_MIXER_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL video_mixer bin",
      "Bin/Filter/Effect/Video/Compositor", "OpenGL video_mixer bin",
      "Matthew Waters <matthew@centricular.com>");
}

static void
gst_gl_video_mixer_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLMixerBin *self = GST_GL_MIXER_BIN (object);

  if (self->mixer)
    g_object_get_property (G_OBJECT (self->mixer), pspec->name, value);
}

static void
gst_gl_video_mixer_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLMixerBin *self = GST_GL_MIXER_BIN (object);

  if (self->mixer)
    g_object_set_property (G_OBJECT (self->mixer), pspec->name, value);
}

enum
{
  PROP_0,
  PROP_BACKGROUND,
};

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_gl_video_mixer_debug, "glvideomixer", 0, "glvideomixer element");

#define gst_gl_video_mixer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLVideoMixer, gst_gl_video_mixer, GST_TYPE_GL_MIXER,
    DEBUG_INIT);

static void gst_gl_video_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *_update_caps (GstVideoAggregator * vagg, GstCaps * caps,
    GstCaps * filter);
static GstCaps *_fixate_caps (GstVideoAggregator * vagg, GstCaps * caps);
static gboolean gst_gl_video_mixer_propose_allocation (GstGLBaseMixer *
    base_mix, GstGLBaseMixerPad * base_pad, GstQuery * decide_query,
    GstQuery * query);
static void gst_gl_video_mixer_reset (GstGLMixer * mixer);
static gboolean gst_gl_video_mixer_init_shader (GstGLMixer * mixer,
    GstCaps * outcaps);

static gboolean gst_gl_video_mixer_process_textures (GstGLMixer * mixer,
    GstGLMemory * out_tex);
static gboolean gst_gl_video_mixer_callback (gpointer stuff);

/* *INDENT-OFF* */

/* fragment source */
static const gchar *video_mixer_f_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D texture;                     \n"
    "uniform float alpha;\n"
    "varying vec2 v_texcoord;                            \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  vec4 rgba = texture2D(texture, v_texcoord);\n"
    "  gl_FragColor = vec4(rgba.rgb, rgba.a * alpha);\n"
    "}                                                   \n";

/* checker vertex source */
static const gchar *checker_v_src =
    "attribute vec4 a_position;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = a_position;\n"
    "}\n";

/* checker fragment source */
static const gchar *checker_f_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "const float blocksize = 8.0;\n"
    "void main ()\n"
    "{\n"
    "  vec4 high = vec4(0.667, 0.667, 0.667, 1.0);\n"
    "  vec4 low = vec4(0.333, 0.333, 0.333, 1.0);\n"
    "  if (mod(gl_FragCoord.x, blocksize * 2.0) >= blocksize) {\n"
    "    if (mod(gl_FragCoord.y, blocksize * 2.0) >= blocksize)\n"
    "      gl_FragColor = low;\n"
    "    else\n"
    "      gl_FragColor = high;\n"
    "  } else {\n"
    "    if (mod(gl_FragCoord.y, blocksize * 2.0) < blocksize)\n"
    "      gl_FragColor = low;\n"
    "    else\n"
    "      gl_FragColor = high;\n"
    "  }\n"
    "}\n";
/* *INDENT-ON* */

#define GST_TYPE_GL_VIDEO_MIXER_PAD (gst_gl_video_mixer_pad_get_type())
#define GST_GL_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_VIDEO_MIXER_PAD, GstGLVideoMixerPad))
#define GST_GL_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_VIDEO_MIXER_PAD, GstGLVideoMixerPadClass))
#define GST_IS_GL_VIDEO_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_VIDEO_MIXER_PAD))
#define GST_IS_GL_VIDEO_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_VIDEO_MIXER_PAD))

typedef struct _GstGLVideoMixerPad GstGLVideoMixerPad;
typedef struct _GstGLVideoMixerPadClass GstGLVideoMixerPadClass;
typedef struct _GstGLVideoMixerCollect GstGLVideoMixerCollect;

/**
 * GstGLVideoMixerPad:
 *
 * The opaque #GstGLVideoMixerPad structure.
 */
struct _GstGLVideoMixerPad
{
  GstGLMixerPad parent;

  /* < private > */
  /* properties */
  gint xpos, ypos;
  gint width, height;
  gdouble alpha;

  GstGLVideoMixerBlendEquation blend_equation_rgb;
  GstGLVideoMixerBlendEquation blend_equation_alpha;
  GstGLVideoMixerBlendFunction blend_function_src_rgb;
  GstGLVideoMixerBlendFunction blend_function_src_alpha;
  GstGLVideoMixerBlendFunction blend_function_dst_rgb;
  GstGLVideoMixerBlendFunction blend_function_dst_alpha;
  gdouble blend_constant_color_red;
  gdouble blend_constant_color_green;
  gdouble blend_constant_color_blue;
  gdouble blend_constant_color_alpha;

  gboolean geometry_change;
  GLuint vertex_buffer;
};

struct _GstGLVideoMixerPadClass
{
  GstGLMixerPadClass parent_class;
};

GType gst_gl_video_mixer_pad_get_type (void);
G_DEFINE_TYPE (GstGLVideoMixerPad, gst_gl_video_mixer_pad,
    GST_TYPE_GL_MIXER_PAD);

static void gst_gl_video_mixer_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_video_mixer_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ALPHA,
  PROP_PAD_BLEND_EQUATION_RGB,
  PROP_PAD_BLEND_EQUATION_ALPHA,
  PROP_PAD_BLEND_FUNCTION_SRC_RGB,
  PROP_PAD_BLEND_FUNCTION_SRC_ALPHA,
  PROP_PAD_BLEND_FUNCTION_DST_RGB,
  PROP_PAD_BLEND_FUNCTION_DST_ALPHA,
  PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_RED,
  PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_GREEN,
  PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_BLUE,
  PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_ALPHA,
};

static void
gst_gl_video_mixer_pad_init (GstGLVideoMixerPad * pad)
{
  pad->alpha = DEFAULT_PAD_ALPHA;
  pad->blend_equation_rgb = DEFAULT_PAD_BLEND_EQUATION_RGB;
  pad->blend_equation_alpha = DEFAULT_PAD_BLEND_EQUATION_ALPHA;
  pad->blend_function_src_rgb = DEFAULT_PAD_BLEND_FUNCTION_SRC_RGB;
  pad->blend_function_src_alpha = DEFAULT_PAD_BLEND_FUNCTION_SRC_ALPHA;
  pad->blend_function_dst_rgb = DEFAULT_PAD_BLEND_FUNCTION_DST_RGB;
  pad->blend_function_dst_alpha = DEFAULT_PAD_BLEND_FUNCTION_DST_ALPHA;
}

static void
gst_gl_video_mixer_pad_class_init (GstGLVideoMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_gl_video_mixer_pad_set_property;
  gobject_class->get_property = gst_gl_video_mixer_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y Position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_EQUATION_RGB,
      g_param_spec_enum ("blend-equation-rgb", "Blend Equation RGB",
          "Blend Equation for RGB",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_EQUATION,
          DEFAULT_PAD_BLEND_EQUATION_RGB,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_EQUATION_ALPHA,
      g_param_spec_enum ("blend-equation-alpha", "Blend Equation Alpha",
          "Blend Equation for Alpha", GST_TYPE_GL_VIDEO_MIXER_BLEND_EQUATION,
          DEFAULT_PAD_BLEND_EQUATION_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_SRC_RGB,
      g_param_spec_enum ("blend-function-src-rgb", "Blend Function Source RGB",
          "Blend Function for Source RGB",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_SRC_RGB,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_SRC_ALPHA,
      g_param_spec_enum ("blend-function-src-alpha",
          "Blend Function Source Alpha", "Blend Function for Source Alpha",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_SRC_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_DST_RGB,
      g_param_spec_enum ("blend-function-dst-rgb",
          "Blend Function Destination RGB",
          "Blend Function for Destination RGB",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_DST_RGB,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_FUNCTION_DST_ALPHA,
      g_param_spec_enum ("blend-function-dst-alpha",
          "Blend Function Destination Alpha",
          "Blend Function for Destiniation Alpha",
          GST_TYPE_GL_VIDEO_MIXER_BLEND_FUNCTION,
          DEFAULT_PAD_BLEND_FUNCTION_DST_ALPHA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_RED,
      g_param_spec_double ("blend-constant-color-red",
          "Blend Constant Color Red", "Blend Constant Color Red", 0.0, 1.0, 0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_GREEN,
      g_param_spec_double ("blend-constant-color-green",
          "Blend Constant Color Green", "Blend Constant Color Green", 0.0, 1.0,
          0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_BLUE,
      g_param_spec_double ("blend-constant-color-blue",
          "Blend Constant Color Green", "Blend Constant Color Green", 0.0, 1.0,
          0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_ALPHA,
      g_param_spec_double ("blend-constant-color-alpha",
          "Blend Constant Color Alpha", "Blend Constant Color Alpha", 0.0, 1.0,
          0.0,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gl_video_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerPad *pad = GST_GL_VIDEO_MIXER_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, pad->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, pad->height);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    case PROP_PAD_BLEND_EQUATION_RGB:
      g_value_set_enum (value, pad->blend_equation_rgb);
      break;
    case PROP_PAD_BLEND_EQUATION_ALPHA:
      g_value_set_enum (value, pad->blend_equation_alpha);
      break;
    case PROP_PAD_BLEND_FUNCTION_SRC_RGB:
      g_value_set_enum (value, pad->blend_function_src_rgb);
      break;
    case PROP_PAD_BLEND_FUNCTION_SRC_ALPHA:
      g_value_set_enum (value, pad->blend_function_src_alpha);
      break;
    case PROP_PAD_BLEND_FUNCTION_DST_RGB:
      g_value_set_enum (value, pad->blend_function_dst_rgb);
      break;
    case PROP_PAD_BLEND_FUNCTION_DST_ALPHA:
      g_value_set_enum (value, pad->blend_function_dst_alpha);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_RED:
      g_value_set_double (value, pad->blend_constant_color_red);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_GREEN:
      g_value_set_double (value, pad->blend_constant_color_green);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_BLUE:
      g_value_set_double (value, pad->blend_constant_color_blue);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_ALPHA:
      g_value_set_double (value, pad->blend_constant_color_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_video_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixerPad *pad = GST_GL_VIDEO_MIXER_PAD (object);
  GstGLMixer *mix = GST_GL_MIXER (gst_pad_get_parent (GST_PAD (pad)));

  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_WIDTH:
      pad->width = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_HEIGHT:
      pad->height = g_value_get_int (value);
      pad->geometry_change = TRUE;
      break;
    case PROP_PAD_ALPHA:
      pad->alpha = g_value_get_double (value);
      break;
    case PROP_PAD_BLEND_EQUATION_RGB:
      pad->blend_equation_rgb = g_value_get_enum (value);
      break;
    case PROP_PAD_BLEND_EQUATION_ALPHA:
      pad->blend_equation_alpha = g_value_get_enum (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_SRC_RGB:
      pad->blend_function_src_rgb = g_value_get_enum (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_SRC_ALPHA:
      pad->blend_function_src_alpha = g_value_get_enum (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_DST_RGB:
      pad->blend_function_dst_rgb = g_value_get_enum (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_DST_ALPHA:
      pad->blend_function_dst_alpha = g_value_get_enum (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_RED:
      pad->blend_constant_color_red = g_value_get_double (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_GREEN:
      pad->blend_constant_color_green = g_value_get_double (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_BLUE:
      pad->blend_constant_color_blue = g_value_get_double (value);
      break;
    case PROP_PAD_BLEND_FUNCTION_CONSTANT_COLOR_ALPHA:
      pad->blend_constant_color_alpha = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_object_unref (mix);
}

static void
_del_buffer (GstGLContext * context, GLuint * pBuffer)
{
  context->gl_vtable->DeleteBuffers (1, pBuffer);
}

static void
gst_gl_video_mixer_release_pad (GstElement * element, GstPad * p)
{
  GstGLVideoMixerPad *pad = GST_GL_VIDEO_MIXER_PAD (p);
  if (pad->vertex_buffer) {
    GstGLBaseMixer *mix = GST_GL_BASE_MIXER (element);
    gst_gl_context_thread_add (mix->context, (GstGLContextThreadFunc)
        _del_buffer, &pad->vertex_buffer);
    pad->vertex_buffer = 0;
  }
  GST_ELEMENT_CLASS (g_type_class_peek_parent (G_OBJECT_GET_CLASS (element)))
      ->release_pad (element, p);
}

static void
gst_gl_video_mixer_class_init (GstGLVideoMixerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;
  GstVideoAggregatorClass *vagg_class = (GstVideoAggregatorClass *) klass;
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);
  element_class->release_pad = gst_gl_video_mixer_release_pad;

  gobject_class->set_property = gst_gl_video_mixer_set_property;
  gobject_class->get_property = gst_gl_video_mixer_get_property;

  gst_element_class_set_metadata (element_class, "OpenGL video_mixer",
      "Filter/Effect/Video/Compositor", "OpenGL video_mixer",
      "Matthew Waters <matthew@centricular.com>");

  g_object_class_install_property (gobject_class, PROP_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_GL_VIDEO_MIXER_BACKGROUND,
          DEFAULT_BACKGROUND, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_GL_MIXER_CLASS (klass)->set_caps = gst_gl_video_mixer_init_shader;
  GST_GL_MIXER_CLASS (klass)->reset = gst_gl_video_mixer_reset;
  GST_GL_MIXER_CLASS (klass)->process_textures =
      gst_gl_video_mixer_process_textures;

  vagg_class->update_caps = _update_caps;
  vagg_class->fixate_caps = _fixate_caps;

  agg_class->sinkpads_type = GST_TYPE_GL_VIDEO_MIXER_PAD;

  mix_class->propose_allocation = gst_gl_video_mixer_propose_allocation;

  GST_GL_BASE_MIXER_CLASS (klass)->supported_gl_api =
      GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2;
}

static void
gst_gl_video_mixer_init (GstGLVideoMixer * video_mixer)
{
  video_mixer->background = DEFAULT_BACKGROUND;
  video_mixer->shader = NULL;
}

static void
gst_gl_video_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixer *mixer = GST_GL_VIDEO_MIXER (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      mixer->background = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_video_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLVideoMixer *mixer = GST_GL_VIDEO_MIXER (object);

  switch (prop_id) {
    case PROP_BACKGROUND:
      g_value_set_enum (value, mixer->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_video_mixer_propose_allocation (GstGLBaseMixer * base_mix,
    GstGLBaseMixerPad * base_pad, GstQuery * decide_query, GstQuery * query)
{
  if (!GST_GL_BASE_MIXER_CLASS (parent_class)->propose_allocation (base_mix,
          base_pad, decide_query, query))
    return FALSE;

  gst_query_add_allocation_meta (query,
      GST_VIDEO_AFFINE_TRANSFORMATION_META_API_TYPE, 0);

  return TRUE;
}

static void
_mixer_pad_get_output_size (GstGLVideoMixer * mix,
    GstGLVideoMixerPad * mix_pad, gint out_par_n, gint out_par_d, gint * width,
    gint * height)
{
  GstVideoAggregatorPad *vagg_pad = GST_VIDEO_AGGREGATOR_PAD (mix_pad);
  gint pad_width, pad_height;
  guint dar_n, dar_d;

  /* FIXME: Anything better we can do here? */
  if (!vagg_pad->info.finfo
      || vagg_pad->info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_DEBUG_OBJECT (mix_pad, "Have no caps yet");
    *width = 0;
    *height = 0;
    return;
  }

  pad_width =
      mix_pad->width <=
      0 ? GST_VIDEO_INFO_WIDTH (&vagg_pad->info) : mix_pad->width;
  pad_height =
      mix_pad->height <=
      0 ? GST_VIDEO_INFO_HEIGHT (&vagg_pad->info) : mix_pad->height;

  if (!gst_video_calculate_display_ratio (&dar_n, &dar_d, pad_width, pad_height,
          GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
          GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d)) {
    GST_WARNING_OBJECT (mix_pad, "Cannot calculate display aspect ratio");
    *width = *height = 0;
    return;
  }
  GST_LOG_OBJECT (mix_pad, "scaling %ux%u by %u/%u (%u/%u / %u/%u)", pad_width,
      pad_height, dar_n, dar_d, GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d);

  if (pad_height % dar_n == 0) {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  } else if (pad_width % dar_d == 0) {
    pad_height = gst_util_uint64_scale_int (pad_width, dar_d, dar_n);
  } else {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  }

  *width = pad_width;
  *height = pad_height;
}

static GstCaps *
_update_caps (GstVideoAggregator * vagg, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GList *l;

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = l->data;

    if (!vaggpad->info.finfo)
      continue;

    if (GST_VIDEO_INFO_FORMAT (&vaggpad->info) == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    if (GST_VIDEO_INFO_MULTIVIEW_MODE (&vaggpad->info) !=
        GST_VIDEO_MULTIVIEW_MODE_NONE
        && GST_VIDEO_INFO_MULTIVIEW_MODE (&vaggpad->info) !=
        GST_VIDEO_MULTIVIEW_MODE_MONO) {
      GST_FIXME_OBJECT (vaggpad, "Multiview support is not implemented yet");
      GST_OBJECT_UNLOCK (vagg);
      return NULL;
    }

  }

  GST_OBJECT_UNLOCK (vagg);

  if (filter) {
    ret = gst_caps_intersect (caps, filter);
  } else {
    ret = gst_caps_ref (caps);
  }

  return ret;
}

static GstCaps *
_fixate_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstGLVideoMixer *mix = GST_GL_VIDEO_MIXER (vagg);
  gint best_width = 0, best_height = 0;
  gint best_fps_n = 0, best_fps_d = 0;
  gint par_n, par_d;
  gdouble best_fps = 0.;
  GstCaps *ret = NULL;
  GstStructure *s;
  GList *l;

  ret = gst_caps_make_writable (caps);

  /* we need this to calculate how large to make the output frame */
  s = gst_caps_get_structure (ret, 0);
  if (!gst_structure_has_field (s, "pixel-aspect-ratio")) {
    gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
  }
  gst_structure_fixate_field_nearest_fraction (s, "pixel-aspect-ratio", 1, 1);
  gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d);

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = l->data;
    GstGLVideoMixerPad *mixer_pad = GST_GL_VIDEO_MIXER_PAD (vaggpad);
    gint this_width, this_height;
    gint width, height;
    gint fps_n, fps_d;
    gdouble cur_fps;

    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);
    _mixer_pad_get_output_size (mix, mixer_pad, par_n, par_d, &width, &height);

    if (width == 0 || height == 0)
      continue;

    this_width = width + MAX (mixer_pad->xpos, 0);
    this_height = height + MAX (mixer_pad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);

    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  s = gst_caps_get_structure (ret, 0);
  gst_structure_fixate_field_nearest_int (s, "width", best_width);
  gst_structure_fixate_field_nearest_int (s, "height", best_height);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
      best_fps_d);
  ret = gst_caps_fixate (ret);

  return ret;
}

static gboolean
_reset_pad_gl (GstAggregator * agg, GstAggregatorPad * aggpad, gpointer udata)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (agg)->context->gl_vtable;
  GstGLVideoMixerPad *pad = GST_GL_VIDEO_MIXER_PAD (aggpad);

  if (pad->vertex_buffer) {
    gl->DeleteBuffers (1, &pad->vertex_buffer);
    pad->vertex_buffer = 0;
  }

  return TRUE;
}

static void
_reset_gl (GstGLContext * context, GstGLVideoMixer * video_mixer)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (video_mixer)->context->gl_vtable;

  if (video_mixer->vao) {
    gl->DeleteVertexArrays (1, &video_mixer->vao);
    video_mixer->vao = 0;
  }

  if (video_mixer->vbo_indices) {
    gl->DeleteBuffers (1, &video_mixer->vbo_indices);
    video_mixer->vbo_indices = 0;
  }

  if (video_mixer->checker_vbo) {
    gl->DeleteBuffers (1, &video_mixer->checker_vbo);
    video_mixer->checker_vbo = 0;
  }

  gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (video_mixer), _reset_pad_gl,
      NULL);
}

static void
gst_gl_video_mixer_reset (GstGLMixer * mixer)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mixer);
  GstGLContext *context = GST_GL_BASE_MIXER (mixer)->context;

  GST_DEBUG_OBJECT (mixer, "context:%p", context);

  if (video_mixer->shader)
    gst_object_unref (video_mixer->shader);
  video_mixer->shader = NULL;

  if (video_mixer->checker)
    gst_object_unref (video_mixer->checker);
  video_mixer->checker = NULL;

  if (GST_GL_BASE_MIXER (mixer)->context)
    gst_gl_context_thread_add (context, (GstGLContextThreadFunc) _reset_gl,
        mixer);
}

static gboolean
gst_gl_video_mixer_init_shader (GstGLMixer * mixer, GstCaps * outcaps)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mixer);

  if (video_mixer->shader)
    gst_object_unref (video_mixer->shader);

  return gst_gl_context_gen_shader (GST_GL_BASE_MIXER (mixer)->context,
      gst_gl_shader_string_vertex_mat4_vertex_transform,
      video_mixer_f_src, &video_mixer->shader);
}

static void
_video_mixer_process_gl (GstGLContext * context, GstGLVideoMixer * video_mixer)
{
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);

  gst_gl_framebuffer_draw_to_texture (mixer->fbo, video_mixer->out_tex,
      gst_gl_video_mixer_callback, video_mixer);
}

static gboolean
gst_gl_video_mixer_process_textures (GstGLMixer * mix, GstGLMemory * out_tex)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (mix);
  GstGLContext *context = GST_GL_BASE_MIXER (mix)->context;

  video_mixer->out_tex = out_tex;

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) _video_mixer_process_gl, video_mixer);

  return TRUE;
}

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

static void
_init_vbo_indices (GstGLVideoMixer * mixer)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;

  if (!mixer->vbo_indices) {
    gl->GenBuffers (1, &mixer->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, mixer->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);
  }
}

static gboolean
_draw_checker_background (GstGLVideoMixer * video_mixer)
{
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;
  gint attr_position_loc = 0;

  /* *INDENT-OFF* */
  gfloat v_vertices[] = {
    -1.0,-1.0,-1.0f,
     1.0,-1.0,-1.0f,
     1.0, 1.0,-1.0f,
    -1.0, 1.0,-1.0f,
  };
  /* *INDENT-ON* */

  if (!video_mixer->checker) {
    if (!gst_gl_context_gen_shader (GST_GL_BASE_MIXER (mixer)->context,
            checker_v_src, checker_f_src, &video_mixer->checker))
      return FALSE;
  }

  gst_gl_shader_use (video_mixer->checker);
  attr_position_loc =
      gst_gl_shader_get_attribute_location (video_mixer->checker, "a_position");

  _init_vbo_indices (video_mixer);

  if (!video_mixer->checker_vbo) {
    gl->GenBuffers (1, &video_mixer->checker_vbo);
    gl->BindBuffer (GL_ARRAY_BUFFER, video_mixer->checker_vbo);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 3 * sizeof (GLfloat), v_vertices,
        GL_STATIC_DRAW);
  } else {
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, video_mixer->vbo_indices);
    gl->BindBuffer (GL_ARRAY_BUFFER, video_mixer->checker_vbo);
  }

  gl->VertexAttribPointer (attr_position_loc, 3, GL_FLOAT,
      GL_FALSE, 3 * sizeof (GLfloat), (void *) 0);

  gl->EnableVertexAttribArray (attr_position_loc);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  return TRUE;
}

static gboolean
_draw_background (GstGLVideoMixer * video_mixer)
{
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;

  switch (video_mixer->background) {
    case GST_GL_VIDEO_MIXER_BACKGROUND_BLACK:
      gl->ClearColor (0.0, 0.0, 0.0, 1.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      break;
    case GST_GL_VIDEO_MIXER_BACKGROUND_WHITE:
      gl->ClearColor (1.0, 1.0, 1.0, 1.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      break;
    case GST_GL_VIDEO_MIXER_BACKGROUND_TRANSPARENT:
      gl->ClearColor (0.0, 0.0, 0.0, 0.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      break;
    case GST_GL_VIDEO_MIXER_BACKGROUND_CHECKER:
      return _draw_checker_background (video_mixer);
      break;
    default:
      break;
  }

  return TRUE;
}

static guint
_blend_equation_to_gl (GstGLVideoMixerBlendEquation equation)
{
  switch (equation) {
    case GST_GL_VIDEO_MIXER_BLEND_EQUATION_ADD:
      return GL_FUNC_ADD;
    case GST_GL_VIDEO_MIXER_BLEND_EQUATION_SUBTRACT:
      return GL_FUNC_SUBTRACT;
    case GST_GL_VIDEO_MIXER_BLEND_EQUATION_REVERSE_SUBTRACT:
      return GL_FUNC_REVERSE_SUBTRACT;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static guint
_blend_function_to_gl (GstGLVideoMixerBlendFunction equation)
{
  switch (equation) {
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ZERO:
      return GL_ZERO;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE:
      return GL_ONE;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_COLOR:
      return GL_SRC_COLOR;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_SRC_COLOR:
      return GL_ONE_MINUS_SRC_COLOR;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_DST_COLOR:
      return GL_DST_COLOR;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_DST_COLOR:
      return GL_ONE_MINUS_DST_COLOR;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA:
      return GL_SRC_ALPHA;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_SRC_ALPHA:
      return GL_ONE_MINUS_SRC_ALPHA;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_DST_ALPHA:
      return GL_DST_ALPHA;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_DST_ALPHA:
      return GL_ONE_MINUS_DST_ALPHA;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_CONSTANT_COLOR:
      return GL_CONSTANT_COLOR;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_CONSTANT_COLOR:
      return GL_ONE_MINUS_CONSTANT_COLOR;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_CONSTANT_ALPHA:
      return GL_CONSTANT_ALPHA;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_ONE_MINUS_CONSTANT_ALPHA:
      return GL_ONE_MINUS_CONSTANT_ALPHA;
    case GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA_SATURATE:
      return GL_SRC_ALPHA_SATURATE;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static gboolean
_set_blend_state (GstGLVideoMixer * video_mixer, GstGLVideoMixerPad * mix_pad)
{
  const GstGLFuncs *gl = GST_GL_BASE_MIXER (video_mixer)->context->gl_vtable;
  gboolean require_separate = FALSE;
  guint gl_func_src_rgb, gl_func_src_alpha, gl_func_dst_rgb, gl_func_dst_alpha;
  guint gl_equation_rgb, gl_equation_alpha;

  require_separate =
      mix_pad->blend_equation_rgb != mix_pad->blend_equation_alpha
      || mix_pad->blend_function_src_rgb != mix_pad->blend_function_src_alpha
      || mix_pad->blend_function_dst_rgb != mix_pad->blend_function_dst_alpha;

  if (require_separate && (!gl->BlendFuncSeparate
          || !gl->BlendEquationSeparate)) {
    GST_ERROR_OBJECT (mix_pad,
        "separated blend equations/functions requested however "
        "glBlendFuncSeparate or glBlendEquationSeparate not available");
    return FALSE;
  }

  if (mix_pad->blend_function_dst_rgb ==
      GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA_SATURATE) {
    GST_ERROR_OBJECT (mix_pad,
        "Destination RGB blend function cannot be \'SRC_ALPHA_SATURATE\'");
    return FALSE;
  }

  if (mix_pad->blend_function_dst_alpha ==
      GST_GL_VIDEO_MIXER_BLEND_FUNCTION_SRC_ALPHA_SATURATE) {
    GST_ERROR_OBJECT (mix_pad,
        "Destination alpha blend function cannot be \'SRC_ALPHA_SATURATE\'");
    return FALSE;
  }

  gl_equation_rgb = _blend_equation_to_gl (mix_pad->blend_equation_rgb);
  gl_equation_alpha = _blend_equation_to_gl (mix_pad->blend_equation_alpha);

  gl_func_src_rgb = _blend_function_to_gl (mix_pad->blend_function_src_rgb);
  gl_func_src_alpha = _blend_function_to_gl (mix_pad->blend_function_src_alpha);
  gl_func_dst_rgb = _blend_function_to_gl (mix_pad->blend_function_dst_rgb);
  gl_func_dst_alpha = _blend_function_to_gl (mix_pad->blend_function_dst_alpha);

  if (gl->BlendEquationSeparate)
    gl->BlendEquationSeparate (gl_equation_rgb, gl_equation_alpha);
  else
    gl->BlendEquation (gl_equation_rgb);

  if (gl->BlendFuncSeparate)
    gl->BlendFuncSeparate (gl_func_src_rgb, gl_func_dst_rgb, gl_func_src_alpha,
        gl_func_dst_alpha);
  else
    gl->BlendFunc (gl_func_src_rgb, gl_func_dst_rgb);

  gl->BlendColor (mix_pad->blend_constant_color_red,
      mix_pad->blend_constant_color_green, mix_pad->blend_constant_color_blue,
      mix_pad->blend_constant_color_alpha);

  return TRUE;
}

/* opengl scene, params: input texture (not the output mixer->texture) */
static gboolean
gst_gl_video_mixer_callback (gpointer stuff)
{
  GstGLVideoMixer *video_mixer = GST_GL_VIDEO_MIXER (stuff);
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (stuff);
  GstGLMixer *mixer = GST_GL_MIXER (video_mixer);
  GstGLFuncs *gl = GST_GL_BASE_MIXER (mixer)->context->gl_vtable;
  GLint attr_position_loc = 0;
  GLint attr_texture_loc = 0;
  guint out_width, out_height;
  GList *walk;

  out_width = GST_VIDEO_INFO_WIDTH (&vagg->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&vagg->info);

  gst_gl_context_clear_shader (GST_GL_BASE_MIXER (mixer)->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);

  gl->Disable (GL_DEPTH_TEST);
  gl->Disable (GL_CULL_FACE);

  if (gl->GenVertexArrays) {
    if (!video_mixer->vao)
      gl->GenVertexArrays (1, &video_mixer->vao);
    gl->BindVertexArray (video_mixer->vao);
  }

  if (!_draw_background (video_mixer))
    return FALSE;

  gst_gl_shader_use (video_mixer->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (video_mixer->shader, "a_position");
  attr_texture_loc =
      gst_gl_shader_get_attribute_location (video_mixer->shader, "a_texcoord");

  gl->Enable (GL_BLEND);

  GST_OBJECT_LOCK (video_mixer);
  walk = GST_ELEMENT (video_mixer)->sinkpads;
  while (walk) {
    GstGLMixerPad *mix_pad = walk->data;
    GstGLVideoMixerPad *pad = walk->data;
    GstVideoAggregatorPad *vagg_pad = walk->data;
    GstVideoInfo *v_info;
    guint in_tex;
    guint in_width, in_height;

    /* *INDENT-OFF* */
    gfloat v_vertices[] = {
      -1.0,-1.0,-1.0f, 0.0f, 0.0f,
       1.0,-1.0,-1.0f, 1.0f, 0.0f,
       1.0, 1.0,-1.0f, 1.0f, 1.0f,
      -1.0, 1.0,-1.0f, 0.0f, 1.0f,
    };
    /* *INDENT-ON* */

    v_info = &GST_VIDEO_AGGREGATOR_PAD (pad)->info;
    in_width = GST_VIDEO_INFO_WIDTH (v_info);
    in_height = GST_VIDEO_INFO_HEIGHT (v_info);

    if (!mix_pad->current_texture || in_width <= 0 || in_height <= 0
        || pad->alpha == 0.0f) {
      GST_DEBUG ("skipping texture:%u pad:%p width:%u height:%u alpha:%f",
          mix_pad->current_texture, pad, in_width, in_height, pad->alpha);
      walk = g_list_next (walk);
      continue;
    }

    if (!_set_blend_state (video_mixer, pad)) {
      GST_FIXME_OBJECT (pad, "skipping due to incorrect blend parameters");
      walk = g_list_next (walk);
      continue;
    }

    in_tex = mix_pad->current_texture;

    _init_vbo_indices (video_mixer);

    if (pad->geometry_change || !pad->vertex_buffer) {
      gint pad_width, pad_height;
      gfloat w, h;

      _mixer_pad_get_output_size (video_mixer, pad,
          GST_VIDEO_INFO_PAR_N (&vagg->info),
          GST_VIDEO_INFO_PAR_D (&vagg->info), &pad_width, &pad_height);

      w = ((gfloat) pad_width / (gfloat) out_width);
      h = ((gfloat) pad_height / (gfloat) out_height);

      /* top-left */
      v_vertices[0] = v_vertices[15] =
          2.0f * (gfloat) pad->xpos / (gfloat) out_width - 1.0f;
      /* bottom-left */
      v_vertices[1] = v_vertices[6] =
          2.0f * (gfloat) pad->ypos / (gfloat) out_height - 1.0f;
      /* top-right */
      v_vertices[5] = v_vertices[10] = v_vertices[0] + 2.0f * w;
      /* bottom-right */
      v_vertices[11] = v_vertices[16] = v_vertices[1] + 2.0f * h;
      GST_TRACE ("processing texture:%u dimensions:%ux%u, at %f,%f %fx%f with "
          "alpha:%f", in_tex, in_width, in_height, v_vertices[0], v_vertices[1],
          v_vertices[5], v_vertices[11], pad->alpha);

      if (!pad->vertex_buffer)
        gl->GenBuffers (1, &pad->vertex_buffer);

      gl->BindBuffer (GL_ARRAY_BUFFER, pad->vertex_buffer);

      gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), v_vertices,
          GL_STATIC_DRAW);

      pad->geometry_change = FALSE;
    } else {
      gl->BindBuffer (GL_ARRAY_BUFFER, pad->vertex_buffer);
    }
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, video_mixer->vbo_indices);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->BindTexture (GL_TEXTURE_2D, in_tex);
    gst_gl_shader_set_uniform_1i (video_mixer->shader, "texture", 0);
    gst_gl_shader_set_uniform_1f (video_mixer->shader, "alpha", pad->alpha);

    {
      GstVideoAffineTransformationMeta *af_meta;
      gfloat matrix[16];

      af_meta =
          gst_buffer_get_video_affine_transformation_meta (vagg_pad->buffer);
      gst_gl_get_affine_transformation_meta_as_ndc_ext (af_meta, matrix);
      gst_gl_shader_set_uniform_matrix_4fv (video_mixer->shader,
          "u_transformation", 1, FALSE, matrix);
    }

    gl->EnableVertexAttribArray (attr_position_loc);
    gl->EnableVertexAttribArray (attr_texture_loc);

    gl->VertexAttribPointer (attr_position_loc, 3, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), (void *) 0);

    gl->VertexAttribPointer (attr_texture_loc, 2, GL_FLOAT,
        GL_FALSE, 5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

    gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    walk = g_list_next (walk);
  }
  GST_OBJECT_UNLOCK (video_mixer);

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->DisableVertexAttribArray (attr_texture_loc);

  if (gl->GenVertexArrays)
    gl->BindVertexArray (0);

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);
  gl->BindTexture (GL_TEXTURE_2D, 0);

  gl->Disable (GL_BLEND);

  gst_gl_context_clear_shader (GST_GL_BASE_MIXER (mixer)->context);

  return TRUE;
}
