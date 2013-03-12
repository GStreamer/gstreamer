/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstaudiovisualizer.h: base class for audio visualisation elements
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:gstaudiovisualizer
 *
 * A baseclass for scopes (visualizers). It takes care of re-fitting the
 * audio-rate to video-rate and handles renegotiation (downstream video size
 * changes).
 * 
 * It also provides several background shading effects. These effects are
 * applied to a previous picture before the render() implementation can draw a
 * new frame.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstaudiovisualizer.h"

GST_DEBUG_CATEGORY_STATIC (audio_visualizer_debug);
#define GST_CAT_DEFAULT (audio_visualizer_debug)

#define DEFAULT_SHADER GST_AUDIO_VISUALIZER_SHADER_FADE
#define DEFAULT_SHADE_AMOUNT   0x000a0a0a

enum
{
  PROP_0,
  PROP_SHADER,
  PROP_SHADE_AMOUNT
};

static GstBaseTransformClass *parent_class = NULL;

static void gst_audio_visualizer_class_init (GstAudioVisualizerClass * klass);
static void gst_audio_visualizer_init (GstAudioVisualizer * scope,
    GstAudioVisualizerClass * g_class);
static void gst_audio_visualizer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_visualizer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_audio_visualizer_dispose (GObject * object);

static gboolean gst_audio_visualizer_src_negotiate (GstAudioVisualizer * scope);
static gboolean gst_audio_visualizer_src_setcaps (GstAudioVisualizer *
    scope, GstCaps * caps);
static gboolean gst_audio_visualizer_sink_setcaps (GstAudioVisualizer *
    scope, GstCaps * caps);

static GstFlowReturn gst_audio_visualizer_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_audio_visualizer_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_audio_visualizer_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

static gboolean gst_audio_visualizer_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static gboolean gst_audio_visualizer_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static GstStateChangeReturn gst_audio_visualizer_change_state (GstElement *
    element, GstStateChange transition);

/* shading functions */

#define GST_TYPE_AUDIO_VISUALIZER_SHADER (gst_audio_visualizer_shader_get_type())
static GType
gst_audio_visualizer_shader_get_type (void)
{
  static GType shader_type = 0;
  static const GEnumValue shaders[] = {
    {GST_AUDIO_VISUALIZER_SHADER_NONE, "None", "none"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE, "Fade", "fade"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_UP, "Fade and move up",
        "fade-and-move-up"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_DOWN, "Fade and move down",
        "fade-and-move-down"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_LEFT, "Fade and move left",
        "fade-and-move-left"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_RIGHT,
          "Fade and move right",
        "fade-and-move-right"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_OUT,
        "Fade and move horizontally out", "fade-and-move-horiz-out"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_IN,
        "Fade and move horizontally in", "fade-and-move-horiz-in"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_OUT,
        "Fade and move vertically out", "fade-and-move-vert-out"},
    {GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_IN,
        "Fade and move vertically in", "fade-and-move-vert-in"},
    {0, NULL, NULL},
  };

  if (G_UNLIKELY (shader_type == 0)) {
    /* TODO: rename when exporting it as a library */
    shader_type =
        g_enum_register_static ("GstAudioVisualizerShader-BaseExtVisual",
        shaders);
  }
  return shader_type;
}

/* we're only supporting GST_VIDEO_FORMAT_xRGB right now) */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN

#define SHADE(_d, _s, _i, _r, _g, _b)                     \
G_STMT_START {                                            \
    _d[_i * 4 + 0] = (_s[_i * 4 + 0] > _b) ? _s[_i * 4 + 0] - _b : 0; \
    _d[_i * 4 + 1] = (_s[_i * 4 + 1] > _g) ? _s[_i * 4 + 1] - _g : 0; \
    _d[_i * 4 + 2] = (_s[_i * 4 + 2] > _r) ? _s[_i * 4 + 2] - _r : 0; \
    _d[_i * 4 + 3] = 0;                                       \
} G_STMT_END

#else /* G_BYTE_ORDER == G_LITTLE_ENDIAN */

#define SHADE(_d, _s, _i, _r, _g, _b)                     \
G_STMT_START {                                            \
    _d[_i * 4 + 0] = 0;                                       \
    _d[_i * 4 + 1] = (_s[_i * 4 + 1] > _r) ? _s[_i * 4 + 1] - _r : 0; \
    _d[_i * 4 + 2] = (_s[_i * 4 + 2] > _g) ? _s[_i * 4 + 2] - _g : 0; \
    _d[_i * 4 + 3] = (_s[_i * 4 + 3] > _b) ? _s[_i * 4 + 3] - _b : 0; \
} G_STMT_END

#endif

static void
shader_fade (GstAudioVisualizer * scope, const GstVideoFrame * sframe,
    GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *d;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  for (j = 0; j < height; j++) {
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    s += ss;
    d += ds;
  }
}

static void
shader_fade_and_move_up (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *d;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  for (j = 1; j < height; j++) {
    s += ss;
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    d += ds;
  }
}

static void
shader_fade_and_move_down (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *d;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  for (j = 1; j < height; j++) {
    d += ds;
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    s += ss;
  }
}

static void
shader_fade_and_move_left (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *d;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  width -= 1;
  s += 4;

  /* move to the left */
  for (j = 0; j < height; j++) {
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    d += ds;
    s += ss;
  }
}

static void
shader_fade_and_move_right (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *d;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  width -= 1;
  d += 4;

  /* move to the right */
  for (j = 0; j < height; j++) {
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    d += ds;
    s += ss;
  }
}

static void
shader_fade_and_move_horiz_out (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *d;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  /* move upper half up */
  for (j = 0; j < height / 2; j++) {
    s += ss;
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    d += ds;
  }
  /* move lower half down */
  for (j = 0; j < height / 2; j++) {
    d += ds;
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    s += ss;
  }
}

static void
shader_fade_and_move_horiz_in (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *d;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  /* move upper half down */
  for (j = 0; j < height / 2; j++) {
    d += ds;
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    s += ss;
  }
  /* move lower half up */
  for (j = 0; j < height / 2; j++) {
    s += ss;
    for (i = 0; i < width; i++) {
      SHADE (d, s, i, r, g, b);
    }
    d += ds;
  }
}

static void
shader_fade_and_move_vert_out (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *s1, *d, *d1;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  for (j = 0; j < height; j++) {
    /* move left half to the left */
    s1 = s + 1;
    for (i = 0; i < width / 2; i++) {
      SHADE (d, s1, i, r, g, b);
    }
    /* move right half to the right */
    d1 = d + 1;
    for (; i < width - 1; i++) {
      SHADE (d1, s, i, r, g, b);
    }
    s += ss;
    d += ds;
  }
}

static void
shader_fade_and_move_vert_in (GstAudioVisualizer * scope,
    const GstVideoFrame * sframe, GstVideoFrame * dframe)
{
  guint i, j;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;
  guint8 *s, *s1, *d, *d1;
  gint ss, ds, width, height;

  s = GST_VIDEO_FRAME_PLANE_DATA (sframe, 0);
  ss = GST_VIDEO_FRAME_PLANE_STRIDE (sframe, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dframe, 0);
  ds = GST_VIDEO_FRAME_PLANE_STRIDE (dframe, 0);

  width = GST_VIDEO_FRAME_WIDTH (sframe);
  height = GST_VIDEO_FRAME_HEIGHT (sframe);

  for (j = 0; j < height; j++) {
    /* move left half to the right */
    d1 = d + 1;
    for (i = 0; i < width / 2; i++) {
      SHADE (d1, s, i, r, g, b);
    }
    /* move right half to the left */
    s1 = s + 1;
    for (; i < width - 1; i++) {
      SHADE (d, s1, i, r, g, b);
    }
    s += ss;
    d += ds;
  }
}

static void
gst_audio_visualizer_change_shader (GstAudioVisualizer * scope)
{
  switch (scope->shader_type) {
    case GST_AUDIO_VISUALIZER_SHADER_NONE:
      scope->shader = NULL;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE:
      scope->shader = shader_fade;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_UP:
      scope->shader = shader_fade_and_move_up;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_DOWN:
      scope->shader = shader_fade_and_move_down;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_LEFT:
      scope->shader = shader_fade_and_move_left;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_RIGHT:
      scope->shader = shader_fade_and_move_right;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_OUT:
      scope->shader = shader_fade_and_move_horiz_out;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_IN:
      scope->shader = shader_fade_and_move_horiz_in;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_OUT:
      scope->shader = shader_fade_and_move_vert_out;
      break;
    case GST_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_IN:
      scope->shader = shader_fade_and_move_vert_in;
      break;
    default:
      GST_ERROR ("invalid shader function");
      scope->shader = NULL;
      break;
  }
}

/* base class */

GType
gst_audio_visualizer_get_type (void)
{
  static volatile gsize audio_visualizer_type = 0;

  if (g_once_init_enter (&audio_visualizer_type)) {
    static const GTypeInfo audio_visualizer_info = {
      sizeof (GstAudioVisualizerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_audio_visualizer_class_init,
      NULL,
      NULL,
      sizeof (GstAudioVisualizer),
      0,
      (GInstanceInitFunc) gst_audio_visualizer_init,
    };
    GType _type;

    /* TODO: rename when exporting it as a library */
    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAudioVisualizer-BaseExtVisual", &audio_visualizer_info,
        G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&audio_visualizer_type, _type);
  }
  return (GType) audio_visualizer_type;
}

static void
gst_audio_visualizer_class_init (GstAudioVisualizerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (audio_visualizer_debug, "audiobasevisualizer",
      0, "scope audio visualisation base class");

  gobject_class->set_property = gst_audio_visualizer_set_property;
  gobject_class->get_property = gst_audio_visualizer_get_property;
  gobject_class->dispose = gst_audio_visualizer_dispose;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_audio_visualizer_change_state);

  g_object_class_install_property (gobject_class, PROP_SHADER,
      g_param_spec_enum ("shader", "shader type",
          "Shader function to apply on each frame",
          GST_TYPE_AUDIO_VISUALIZER_SHADER, DEFAULT_SHADER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHADE_AMOUNT,
      g_param_spec_uint ("shade-amount", "shade amount",
          "Shading color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_SHADE_AMOUNT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_audio_visualizer_init (GstAudioVisualizer * scope,
    GstAudioVisualizerClass * g_class)
{
  GstPadTemplate *pad_template;

  /* create the sink and src pads */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);
  scope->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_chain_function (scope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_visualizer_chain));
  gst_pad_set_event_function (scope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_visualizer_sink_event));
  gst_pad_set_query_function (scope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_visualizer_sink_query));
  gst_element_add_pad (GST_ELEMENT (scope), scope->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);
  scope->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_event_function (scope->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_visualizer_src_event));
  gst_pad_set_query_function (scope->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_visualizer_src_query));
  gst_element_add_pad (GST_ELEMENT (scope), scope->srcpad);

  scope->adapter = gst_adapter_new ();
  scope->inbuf = gst_buffer_new ();

  /* properties */
  scope->shader_type = DEFAULT_SHADER;
  gst_audio_visualizer_change_shader (scope);
  scope->shade_amount = DEFAULT_SHADE_AMOUNT;

  /* reset the initial video state */
  gst_video_info_init (&scope->vinfo);
  scope->frame_duration = GST_CLOCK_TIME_NONE;

  /* reset the initial state */
  gst_audio_info_init (&scope->ainfo);
  gst_video_info_init (&scope->vinfo);

  g_mutex_init (&scope->config_lock);
}

static void
gst_audio_visualizer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioVisualizer *scope = GST_AUDIO_VISUALIZER (object);

  switch (prop_id) {
    case PROP_SHADER:
      scope->shader_type = g_value_get_enum (value);
      gst_audio_visualizer_change_shader (scope);
      break;
    case PROP_SHADE_AMOUNT:
      scope->shade_amount = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_visualizer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioVisualizer *scope = GST_AUDIO_VISUALIZER (object);

  switch (prop_id) {
    case PROP_SHADER:
      g_value_set_enum (value, scope->shader_type);
      break;
    case PROP_SHADE_AMOUNT:
      g_value_set_uint (value, scope->shade_amount);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_visualizer_dispose (GObject * object)
{
  GstAudioVisualizer *scope = GST_AUDIO_VISUALIZER (object);

  if (scope->adapter) {
    g_object_unref (scope->adapter);
    scope->adapter = NULL;
  }
  if (scope->inbuf) {
    gst_buffer_unref (scope->inbuf);
    scope->inbuf = NULL;
  }
  if (scope->tempbuf) {
    gst_video_frame_unmap (&scope->tempframe);
    gst_buffer_unref (scope->tempbuf);
    scope->tempbuf = NULL;
  }
  if (scope->config_lock.p) {
    g_mutex_clear (&scope->config_lock);
    scope->config_lock.p = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audio_visualizer_reset (GstAudioVisualizer * scope)
{
  gst_adapter_clear (scope->adapter);
  gst_segment_init (&scope->segment, GST_FORMAT_UNDEFINED);

  GST_OBJECT_LOCK (scope);
  scope->proportion = 1.0;
  scope->earliest_time = -1;
  GST_OBJECT_UNLOCK (scope);
}

static gboolean
gst_audio_visualizer_sink_setcaps (GstAudioVisualizer * scope, GstCaps * caps)
{
  GstAudioInfo info;
  gboolean res = TRUE;

  if (!gst_audio_info_from_caps (&info, caps))
    goto wrong_caps;

  scope->ainfo = info;

  GST_DEBUG_OBJECT (scope, "audio: channels %d, rate %d",
      GST_AUDIO_INFO_CHANNELS (&info), GST_AUDIO_INFO_RATE (&info));

done:
  return res;

  /* Errors */
wrong_caps:
  {
    GST_WARNING_OBJECT (scope, "could not parse caps");
    res = FALSE;
    goto done;
  }
}

static gboolean
gst_audio_visualizer_src_setcaps (GstAudioVisualizer * scope, GstCaps * caps)
{
  GstVideoInfo info;
  GstAudioVisualizerClass *klass;
  gboolean res;

  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  klass = GST_AUDIO_VISUALIZER_CLASS (G_OBJECT_GET_CLASS (scope));

  scope->vinfo = info;

  scope->frame_duration = gst_util_uint64_scale_int (GST_SECOND,
      GST_VIDEO_INFO_FPS_D (&info), GST_VIDEO_INFO_FPS_N (&info));
  scope->spf = gst_util_uint64_scale_int (GST_AUDIO_INFO_RATE (&scope->ainfo),
      GST_VIDEO_INFO_FPS_D (&info), GST_VIDEO_INFO_FPS_N (&info));
  scope->req_spf = scope->spf;

  if (scope->tempbuf) {
    gst_video_frame_unmap (&scope->tempframe);
    gst_buffer_unref (scope->tempbuf);
  }
  scope->tempbuf = gst_buffer_new_wrapped (g_malloc0 (scope->vinfo.size),
      scope->vinfo.size);
  gst_video_frame_map (&scope->tempframe, &scope->vinfo, scope->tempbuf,
      GST_MAP_READWRITE);

  if (klass->setup)
    res = klass->setup (scope);

  GST_DEBUG_OBJECT (scope, "video: dimension %dx%d, framerate %d/%d",
      GST_VIDEO_INFO_WIDTH (&info), GST_VIDEO_INFO_HEIGHT (&info),
      GST_VIDEO_INFO_FPS_N (&info), GST_VIDEO_INFO_FPS_D (&info));
  GST_DEBUG_OBJECT (scope, "blocks: spf %u, req_spf %u",
      scope->spf, scope->req_spf);

  res = gst_pad_set_caps (scope->srcpad, caps);

  return res;

  /* ERRORS */
wrong_caps:
  {
    GST_DEBUG_OBJECT (scope, "error parsing caps");
    return FALSE;
  }
}

static gboolean
gst_audio_visualizer_src_negotiate (GstAudioVisualizer * scope)
{
  GstCaps *othercaps, *target;
  GstStructure *structure;
  GstCaps *templ;
  GstQuery *query;
  GstBufferPool *pool;
  GstStructure *config;
  guint size, min, max;

  templ = gst_pad_get_pad_template_caps (scope->srcpad);

  GST_DEBUG_OBJECT (scope, "performing negotiation");

  /* see what the peer can do */
  othercaps = gst_pad_peer_query_caps (scope->srcpad, NULL);
  if (othercaps) {
    target = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);
    gst_caps_unref (templ);

    if (gst_caps_is_empty (target))
      goto no_format;

    target = gst_caps_truncate (target);
  } else {
    target = templ;
  }

  target = gst_caps_make_writable (target);
  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 200);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 25, 1);

  target = gst_caps_fixate (target);

  GST_DEBUG_OBJECT (scope, "final caps are %" GST_PTR_FORMAT, target);

  gst_audio_visualizer_src_setcaps (scope, target);

  /* try to get a bufferpool now */
  /* find a pool for the negotiated caps now */
  query = gst_query_new_allocation (target, TRUE);

  if (!gst_pad_peer_query (scope->srcpad, query)) {
    /* not a problem, we use the query defaults */
    GST_DEBUG_OBJECT (scope, "allocation query failed");
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* we got configuration from our peer, parse them */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
  } else {
    pool = NULL;
    size = 0;
    min = max = 0;
  }

  if (pool == NULL) {
    /* we did not get a pool, make one ourselves then */
    pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, target, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  if (scope->pool) {
    gst_buffer_pool_set_active (scope->pool, FALSE);
    gst_object_unref (scope->pool);
  }
  scope->pool = pool;

  /* and activate */
  gst_buffer_pool_set_active (pool, TRUE);

  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    GST_ERROR_OBJECT (scope, "no format");
    gst_caps_unref (target);
    return FALSE;
  }
}

/* make sure we are negotiated */
static GstFlowReturn
gst_audio_visualizer_ensure_negotiated (GstAudioVisualizer * scope)
{
  gboolean reconfigure;

  reconfigure = gst_pad_check_reconfigure (scope->srcpad);

  /* we don't know an output format yet, pick one */
  if (reconfigure || !gst_pad_has_current_caps (scope->srcpad)) {
    if (!gst_audio_visualizer_src_negotiate (scope))
      return GST_FLOW_NOT_NEGOTIATED;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_visualizer_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAudioVisualizer *scope;
  GstAudioVisualizerClass *klass;
  GstBuffer *inbuf;
  guint64 dist, ts;
  guint avail, sbpf;
  gpointer adata;
  gint bps, channels, rate;

  scope = GST_AUDIO_VISUALIZER (parent);
  klass = GST_AUDIO_VISUALIZER_CLASS (G_OBJECT_GET_CLASS (scope));

  GST_LOG_OBJECT (scope, "chainfunc called");

  /* resync on DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (scope->adapter);
  }

  /* Make sure have an output format */
  ret = gst_audio_visualizer_ensure_negotiated (scope);
  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (buffer);
    goto beach;
  }
  channels = GST_AUDIO_INFO_CHANNELS (&scope->ainfo);
  rate = GST_AUDIO_INFO_RATE (&scope->ainfo);
  bps = GST_AUDIO_INFO_BPS (&scope->ainfo);

  if (bps == 0) {
    GST_ERROR_OBJECT (scope, "no BPS set");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  gst_adapter_push (scope->adapter, buffer);

  g_mutex_lock (&scope->config_lock);

  /* this is what we want */
  sbpf = scope->req_spf * channels * sizeof (gint16);

  inbuf = scope->inbuf;
  /* FIXME: the timestamp in the adapter would be different */
  gst_buffer_copy_into (inbuf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);

  /* this is what we have */
  avail = gst_adapter_available (scope->adapter);
  GST_LOG_OBJECT (scope, "avail: %u, bpf: %u", avail, sbpf);
  while (avail >= sbpf) {
    GstBuffer *outbuf;
    GstVideoFrame outframe;

    /* get timestamp of the current adapter content */
    ts = gst_adapter_prev_timestamp (scope->adapter, &dist);
    if (GST_CLOCK_TIME_IS_VALID (ts)) {
      /* convert bytes to time */
      dist /= bps;
      ts += gst_util_uint64_scale_int (dist, GST_SECOND, rate);
    }

    if (GST_CLOCK_TIME_IS_VALID (ts)) {
      gint64 qostime;
      gboolean need_skip;

      qostime =
          gst_segment_to_running_time (&scope->segment, GST_FORMAT_TIME, ts) +
          scope->frame_duration;

      GST_OBJECT_LOCK (scope);
      /* check for QoS, don't compute buffers that are known to be late */
      need_skip = scope->earliest_time != -1 && qostime <= scope->earliest_time;
      GST_OBJECT_UNLOCK (scope);

      if (need_skip) {
        GST_WARNING_OBJECT (scope,
            "QoS: skip ts: %" GST_TIME_FORMAT ", earliest: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (qostime), GST_TIME_ARGS (scope->earliest_time));
        goto skip;
      }
    }

    g_mutex_unlock (&scope->config_lock);
    ret = gst_buffer_pool_acquire_buffer (scope->pool, &outbuf, NULL);
    g_mutex_lock (&scope->config_lock);
    /* recheck as the value could have changed */
    sbpf = scope->req_spf * channels * sizeof (gint16);

    /* no buffer allocated, we don't care why. */
    if (ret != GST_FLOW_OK)
      break;

    /* sync controlled properties */
    gst_object_sync_values (GST_OBJECT (scope), ts);

    GST_BUFFER_TIMESTAMP (outbuf) = ts;
    GST_BUFFER_DURATION (outbuf) = scope->frame_duration;

    /* this can fail as the data size we need could have changed */
    if (!(adata = (gpointer) gst_adapter_map (scope->adapter, sbpf)))
      break;

    gst_video_frame_map (&outframe, &scope->vinfo, outbuf, GST_MAP_READWRITE);

    if (scope->shader) {
      gst_video_frame_copy (&outframe, &scope->tempframe);
    } else {
      /* gst_video_frame_clear() or is output frame already cleared */
      gint i;

      for (i = 0; i < scope->vinfo.finfo->n_planes; i++) {
        memset (outframe.data[i], 0, outframe.map[i].size);
      }
    }

    gst_buffer_replace_all_memory (inbuf,
        gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY, adata, sbpf, 0,
            sbpf, NULL, NULL));

    /* call class->render() vmethod */
    if (klass->render) {
      if (!klass->render (scope, inbuf, &outframe)) {
        GST_ERROR_OBJECT (scope, "render failed");
        ret = GST_FLOW_ERROR;
      } else {
        /* run various post processing (shading and geometric transformation) */
        /* FIXME: SHADER assumes 32bpp */
        if (scope->shader &&
            GST_VIDEO_INFO_COMP_PSTRIDE (&scope->vinfo, 0) == 4) {
          scope->shader (scope, &outframe, &scope->tempframe);
        }
      }
    }
    gst_video_frame_unmap (&outframe);

    g_mutex_unlock (&scope->config_lock);
    ret = gst_pad_push (scope->srcpad, outbuf);
    outbuf = NULL;
    g_mutex_lock (&scope->config_lock);

  skip:
    /* recheck as the value could have changed */
    sbpf = scope->req_spf * channels * sizeof (gint16);
    GST_LOG_OBJECT (scope, "avail: %u, bpf: %u", avail, sbpf);
    /* we want to take less or more, depending on spf : req_spf */
    if (avail - sbpf >= sbpf) {
      gst_adapter_flush (scope->adapter, sbpf);
      gst_adapter_unmap (scope->adapter);
    } else if (avail >= sbpf) {
      /* just flush a bit and stop */
      gst_adapter_flush (scope->adapter, (avail - sbpf));
      gst_adapter_unmap (scope->adapter);
      break;
    }
    avail = gst_adapter_available (scope->adapter);

    if (ret != GST_FLOW_OK)
      break;
  }

  g_mutex_unlock (&scope->config_lock);

beach:
  return ret;
}

static gboolean
gst_audio_visualizer_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res;
  GstAudioVisualizer *scope;

  scope = GST_AUDIO_VISUALIZER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, NULL, &proportion, &diff, &timestamp);

      /* save stuff for the _chain() function */
      GST_OBJECT_LOCK (scope);
      scope->proportion = proportion;
      if (diff >= 0)
        /* we're late, this is a good estimate for next displayable
         * frame (see part-qos.txt) */
        scope->earliest_time = timestamp + 2 * diff + scope->frame_duration;
      else
        scope->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (scope);

      res = gst_pad_push_event (scope->sinkpad, event);
      break;
    }
    case GST_EVENT_RECONFIGURE:
      /* dont't forward */
      gst_event_unref (event);
      res = TRUE;
      break;
    default:
      res = gst_pad_push_event (scope->sinkpad, event);
      break;
  }

  return res;
}

static gboolean
gst_audio_visualizer_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res;
  GstAudioVisualizer *scope;

  scope = GST_AUDIO_VISUALIZER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_audio_visualizer_sink_setcaps (scope, caps);
      break;
    }
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (scope->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_audio_visualizer_reset (scope);
      res = gst_pad_push_event (scope->srcpad, event);
      break;
    case GST_EVENT_SEGMENT:
    {
      /* the newsegment values are used to clip the input samples
       * and to convert the incomming timestamps to running time so
       * we can do QoS */
      gst_event_copy_segment (event, &scope->segment);

      res = gst_pad_push_event (scope->srcpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (scope->srcpad, event);
      break;
  }

  return res;
}

static gboolean
gst_audio_visualizer_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean res = FALSE;
  GstAudioVisualizer *scope;

  scope = GST_AUDIO_VISUALIZER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;
      gboolean us_live;
      GstClockTime our_latency;
      guint max_samples;
      gint rate = GST_AUDIO_INFO_RATE (&scope->ainfo);

      if (rate == 0)
        break;

      if ((res = gst_pad_peer_query (scope->sinkpad, query))) {
        gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (scope, "Peer latency: min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        /* the max samples we must buffer buffer */
        max_samples = MAX (scope->req_spf, scope->spf);
        our_latency = gst_util_uint64_scale_int (max_samples, GST_SECOND, rate);

        GST_DEBUG_OBJECT (scope, "Our latency: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (our_latency));

        /* we add some latency but only if we need to buffer more than what
         * upstream gives us */
        min_latency += our_latency;
        if (max_latency != -1)
          max_latency += our_latency;

        GST_DEBUG_OBJECT (scope, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        gst_query_set_latency (query, TRUE, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
gst_audio_visualizer_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

static GstStateChangeReturn
gst_audio_visualizer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstAudioVisualizer *scope;

  scope = GST_AUDIO_VISUALIZER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_audio_visualizer_reset (scope);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (scope->pool) {
        gst_buffer_pool_set_active (scope->pool, FALSE);
        gst_object_replace ((GstObject **) & scope->pool, NULL);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
