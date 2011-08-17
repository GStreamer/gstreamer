/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstbaseaudiovisualizer.h: base class for audio visualisation elements
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
 * SECTION:gstbaseaudiovisualizer
 *
 * A basclass for scopes (visualizers). It takes care of re-fitting the
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
#include <gst/controller/gstcontroller.h>

#include "gstbaseaudiovisualizer.h"

GST_DEBUG_CATEGORY_STATIC (base_audio_visualizer_debug);
#define GST_CAT_DEFAULT (base_audio_visualizer_debug)

#define DEFAULT_SHADER GST_BASE_AUDIO_VISUALIZER_SHADER_FADE
#define DEFAULT_SHADE_AMOUNT   0x000a0a0a

enum
{
  PROP_0,
  PROP_SHADER,
  PROP_SHADE_AMOUNT
};

static GstBaseTransformClass *parent_class = NULL;

static void gst_base_audio_visualizer_class_init (GstBaseAudioVisualizerClass *
    klass);
static void gst_base_audio_visualizer_init (GstBaseAudioVisualizer * scope,
    GstBaseAudioVisualizerClass * g_class);
static void gst_base_audio_visualizer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_audio_visualizer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_base_audio_visualizer_dispose (GObject * object);

static gboolean gst_base_audio_visualizer_src_negotiate (GstBaseAudioVisualizer
    * scope);
static gboolean gst_base_audio_visualizer_src_setcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_base_audio_visualizer_sink_setcaps (GstPad * pad,
    GstCaps * caps);

static GstFlowReturn gst_base_audio_visualizer_chain (GstPad * pad,
    GstBuffer * buffer);
static GstStateChangeReturn gst_base_audio_visualizer_change_state (GstElement *
    element, GstStateChange transition);

/* shading functions */

#define GST_TYPE_BASE_AUDIO_VISUALIZER_SHADER (gst_base_audio_visualizer_shader_get_type())
static GType
gst_base_audio_visualizer_shader_get_type (void)
{
  static GType shader_type = 0;
  static const GEnumValue shaders[] = {
    {GST_BASE_AUDIO_VISUALIZER_SHADER_NONE, "None", "none"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE, "Fade", "fade"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_UP, "Fade and move up",
        "fade-and-move-up"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_DOWN, "Fade and move down",
        "fade-and-move-down"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_LEFT, "Fade and move left",
        "fade-and-move-left"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_RIGHT,
          "Fade and move right",
        "fade-and-move-right"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_OUT,
        "Fade and move horizontally out", "fade-and-move-horiz-out"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_IN,
        "Fade and move horizontally in", "fade-and-move-horiz-in"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_OUT,
        "Fade and move vertically out", "fade-and-move-vert-out"},
    {GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_IN,
        "Fade and move vertically in", "fade-and-move-vert-in"},
    {0, NULL, NULL},
  };

  if (G_UNLIKELY (shader_type == 0)) {
    shader_type =
        g_enum_register_static ("GstBaseAudioVisualizerShader", shaders);
  }
  return shader_type;
}

/* we're only supporting GST_VIDEO_FORMAT_xRGB right now) */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN

#define SHADE1(_d, _s, _i, _r, _g, _b)          \
G_STMT_START {                                  \
    _d[_i] = (_s[_i] > _b) ? _s[_i] - _b : 0;   \
    _i++;                                       \
    _d[_i] = (_s[_i] > _g) ? _s[_i] - _g : 0;   \
    _i++;                                       \
    _d[_i] = (_s[_i] > _r) ? _s[_i] - _r : 0;   \
    _i++;                                       \
    _d[_i++] = 0;                               \
} G_STMT_END

#define SHADE2(_d, _s, _j, _i, _r, _g, _b)      \
G_STMT_START {                                  \
    _d[_j++] = (_s[_i] > _b) ? _s[_i] - _b : 0; \
    _i++;                                       \
    _d[_j++] = (_s[_i] > _g) ? _s[_i] - _g : 0; \
    _i++;                                       \
    _d[_j++] = (_s[_i] > _r) ? _s[_i] - _r : 0; \
    _i++;                                       \
    _d[_j++] = 0;                               \
    _i++;                                       \
} G_STMT_END

#else

#define SHADE1(_d, _s, _i, _r, _g, _b)          \
G_STMT_START {                                  \
    _d[_i++] = 0;                               \
    _d[_i] = (_s[_i] > _r) ? _s[_i] - _r : 0;   \
    _i++;                                       \
    _d[_i] = (_s[_i] > _g) ? _s[_i] - _g : 0;   \
    _i++;                                       \
    _d[_i] = (_s[_i] > _b) ? _s[_i] - _b : 0;   \
    _i++;                                       \
} G_STMT_END

#define SHADE2(_d, _s, _j, _i, _r, _g, _b)      \
G_STMT_START {                                  \
    _d[_j++] = 0;                               \
    _i++;                                       \
    _d[_j++] = (_s[_i] > _r) ? _s[_i] - _r : 0; \
    _i++;                                       \
    _d[_j++] = (_s[_i] > _g) ? _s[_i] - _g : 0; \
    _i++;                                       \
    _d[_j++] = (_s[_i] > _b) ? _s[_i] - _b : 0; \
    _i++;                                       \
} G_STMT_END

#endif

static void
shader_fade (GstBaseAudioVisualizer * scope, const guint8 * s, guint8 * d)
{
  guint i, bpf = scope->bpf;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  for (i = 0; i < bpf;) {
    SHADE1 (d, s, i, r, g, b);
  }
}

static void
shader_fade_and_move_up (GstBaseAudioVisualizer * scope, const guint8 * s,
    guint8 * d)
{
  guint i, j, bpf = scope->bpf;
  guint bpl = 4 * scope->width;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  for (j = 0, i = bpl; i < bpf;) {
    SHADE2 (d, s, j, i, r, g, b);
  }
}

static void
shader_fade_and_move_down (GstBaseAudioVisualizer * scope, const guint8 * s,
    guint8 * d)
{
  guint i, j, bpf = scope->bpf;
  guint bpl = 4 * scope->width;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  for (j = bpl, i = 0; j < bpf;) {
    SHADE2 (d, s, j, i, r, g, b);
  }
}

static void
shader_fade_and_move_left (GstBaseAudioVisualizer * scope,
    const guint8 * s, guint8 * d)
{
  guint i, j, k, bpf = scope->bpf;
  guint w = scope->width;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  /* move to the left */
  for (j = 0, i = 4; i < bpf;) {
    for (k = 0; k < w - 1; k++) {
      SHADE2 (d, s, j, i, r, g, b);
    }
    i += 4;
    j += 4;
  }
}

static void
shader_fade_and_move_right (GstBaseAudioVisualizer * scope,
    const guint8 * s, guint8 * d)
{
  guint i, j, k, bpf = scope->bpf;
  guint w = scope->width;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  /* move to the left */
  for (j = 4, i = 0; i < bpf;) {
    for (k = 0; k < w - 1; k++) {
      SHADE2 (d, s, j, i, r, g, b);
    }
    i += 4;
    j += 4;
  }
}

static void
shader_fade_and_move_horiz_out (GstBaseAudioVisualizer * scope,
    const guint8 * s, guint8 * d)
{
  guint i, j, bpf = scope->bpf / 2;
  guint bpl = 4 * scope->width;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  /* move upper half up */
  for (j = 0, i = bpl; i < bpf;) {
    SHADE2 (d, s, j, i, r, g, b);
  }
  /* move lower half down */
  for (j = bpf + bpl, i = bpf; j < bpf + bpf;) {
    SHADE2 (d, s, j, i, r, g, b);
  }
}

static void
shader_fade_and_move_horiz_in (GstBaseAudioVisualizer * scope,
    const guint8 * s, guint8 * d)
{
  guint i, j, bpf = scope->bpf / 2;
  guint bpl = 4 * scope->width;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  /* move upper half down */
  for (i = 0, j = bpl; i < bpf;) {
    SHADE2 (d, s, j, i, r, g, b);
  }
  /* move lower half up */
  for (i = bpf + bpl, j = bpf; i < bpf + bpf;) {
    SHADE2 (d, s, j, i, r, g, b);
  }
}

static void
shader_fade_and_move_vert_out (GstBaseAudioVisualizer * scope,
    const guint8 * s, guint8 * d)
{
  guint i, j, k, bpf = scope->bpf;
  guint m = scope->width / 2;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  /* move left half to the left */
  for (j = 0, i = 4; i < bpf;) {
    for (k = 0; k < m; k++) {
      SHADE2 (d, s, j, i, r, g, b);
    }
    j += 4 * m;
    i += 4 * m;
  }
  /* move right half to the right */
  for (j = 4 * (m + 1), i = 4 * m; j < bpf;) {
    for (k = 0; k < m; k++) {
      SHADE2 (d, s, j, i, r, g, b);
    }
    j += 4 * m;
    i += 4 * m;
  }
}

static void
shader_fade_and_move_vert_in (GstBaseAudioVisualizer * scope,
    const guint8 * s, guint8 * d)
{
  guint i, j, k, bpf = scope->bpf;
  guint m = scope->width / 2;
  guint r = (scope->shade_amount >> 16) & 0xff;
  guint g = (scope->shade_amount >> 8) & 0xff;
  guint b = (scope->shade_amount >> 0) & 0xff;

  /* move left half to the right */
  for (j = 4, i = 0; j < bpf;) {
    for (k = 0; k < m; k++) {
      SHADE2 (d, s, j, i, r, g, b);
    }
    j += 4 * m;
    i += 4 * m;
  }
  /* move right half to the left */
  for (j = 4 * m, i = 4 * (m + 1); i < bpf;) {
    for (k = 0; k < m; k++) {
      SHADE2 (d, s, j, i, r, g, b);
    }
    j += 4 * m;
    i += 4 * m;
  }
}

static void
gst_base_audio_visualizer_change_shader (GstBaseAudioVisualizer * scope)
{
  switch (scope->shader_type) {
    case GST_BASE_AUDIO_VISUALIZER_SHADER_NONE:
      scope->shader = NULL;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE:
      scope->shader = shader_fade;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_UP:
      scope->shader = shader_fade_and_move_up;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_DOWN:
      scope->shader = shader_fade_and_move_down;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_LEFT:
      scope->shader = shader_fade_and_move_left;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_RIGHT:
      scope->shader = shader_fade_and_move_right;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_OUT:
      scope->shader = shader_fade_and_move_horiz_out;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_HORIZ_IN:
      scope->shader = shader_fade_and_move_horiz_in;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_OUT:
      scope->shader = shader_fade_and_move_vert_out;
      break;
    case GST_BASE_AUDIO_VISUALIZER_SHADER_FADE_AND_MOVE_VERT_IN:
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
gst_base_audio_visualizer_get_type (void)
{
  static volatile gsize base_audio_visualizer_type = 0;

  if (g_once_init_enter (&base_audio_visualizer_type)) {
    static const GTypeInfo base_audio_visualizer_info = {
      sizeof (GstBaseAudioVisualizerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_base_audio_visualizer_class_init,
      NULL,
      NULL,
      sizeof (GstBaseAudioVisualizer),
      0,
      (GInstanceInitFunc) gst_base_audio_visualizer_init,
    };
    GType _type;

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseAudioVisualizer", &base_audio_visualizer_info,
        G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&base_audio_visualizer_type, _type);
  }
  return (GType) base_audio_visualizer_type;
}

static void
gst_base_audio_visualizer_class_init (GstBaseAudioVisualizerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (base_audio_visualizer_debug, "baseaudiovisualizer",
      0, "scope audio visualisation base class");

  gobject_class->set_property = gst_base_audio_visualizer_set_property;
  gobject_class->get_property = gst_base_audio_visualizer_get_property;
  gobject_class->dispose = gst_base_audio_visualizer_dispose;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_audio_visualizer_change_state);

  g_object_class_install_property (gobject_class, PROP_SHADER,
      g_param_spec_enum ("shader", "shader type",
          "Shader function to apply on each frame",
          GST_TYPE_BASE_AUDIO_VISUALIZER_SHADER, DEFAULT_SHADER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHADE_AMOUNT,
      g_param_spec_uint ("shade-amount", "shade amount",
          "Shading color to use (big-endian ARGB)", 0, G_MAXUINT32,
          DEFAULT_SHADE_AMOUNT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_base_audio_visualizer_init (GstBaseAudioVisualizer * scope,
    GstBaseAudioVisualizerClass * g_class)
{
  GstPadTemplate *pad_template;

  /* create the sink and src pads */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);
  scope->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_chain_function (scope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_visualizer_chain));
  gst_pad_set_setcaps_function (scope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_visualizer_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (scope), scope->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);
  scope->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_setcaps_function (scope->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_visualizer_src_setcaps));
  gst_element_add_pad (GST_ELEMENT (scope), scope->srcpad);

  scope->adapter = gst_adapter_new ();
  scope->inbuf = gst_buffer_new ();

  /* properties */
  scope->shader_type = DEFAULT_SHADER;
  gst_base_audio_visualizer_change_shader (scope);
  scope->shade_amount = DEFAULT_SHADE_AMOUNT;

  /* reset the initial video state */
  scope->width = 320;
  scope->height = 200;
  scope->fps_n = 25;            /* desired frame rate */
  scope->fps_d = 1;
  scope->frame_duration = GST_CLOCK_TIME_NONE;

  /* reset the initial audio state */
  scope->rate = GST_AUDIO_DEF_RATE;
  scope->channels = 2;

  scope->next_ts = GST_CLOCK_TIME_NONE;

  scope->config_lock = g_mutex_new ();
}

static void
gst_base_audio_visualizer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioVisualizer *scope = GST_BASE_AUDIO_VISUALIZER (object);

  switch (prop_id) {
    case PROP_SHADER:
      scope->shader_type = g_value_get_enum (value);
      gst_base_audio_visualizer_change_shader (scope);
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
gst_base_audio_visualizer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseAudioVisualizer *scope = GST_BASE_AUDIO_VISUALIZER (object);

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
gst_base_audio_visualizer_dispose (GObject * object)
{
  GstBaseAudioVisualizer *scope = GST_BASE_AUDIO_VISUALIZER (object);

  if (scope->adapter) {
    g_object_unref (scope->adapter);
    scope->adapter = NULL;
  }
  if (scope->inbuf) {
    gst_buffer_unref (scope->inbuf);
    scope->inbuf = NULL;
  }
  if (scope->pixelbuf) {
    g_free (scope->pixelbuf);
    scope->pixelbuf = NULL;
  }
  if (scope->config_lock) {
    g_mutex_free (scope->config_lock);
    scope->config_lock = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_base_audio_visualizer_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseAudioVisualizer *scope;
  GstStructure *structure;
  gint channels;
  gint rate;
  gboolean res = TRUE;

  scope = GST_BASE_AUDIO_VISUALIZER (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate))
    goto missing_caps_details;

  if (channels != 2)
    goto wrong_channels;

  if (rate <= 0)
    goto wrong_rate;

  scope->channels = channels;
  scope->rate = rate;

  GST_DEBUG_OBJECT (scope, "audio: channels %d, rate %d",
      scope->channels, scope->rate);

done:
  gst_object_unref (scope);
  return res;

  /* Errors */
missing_caps_details:
  {
    GST_WARNING_OBJECT (scope, "missing channels or rate in the caps");
    res = FALSE;
    goto done;
  }
wrong_channels:
  {
    GST_WARNING_OBJECT (scope, "number of channels must be 2, but is %d",
        channels);
    res = FALSE;
    goto done;
  }
wrong_rate:
  {
    GST_WARNING_OBJECT (scope, "sample rate must be >0, but is %d", rate);
    res = FALSE;
    goto done;
  }
}

static gboolean
gst_base_audio_visualizer_src_negotiate (GstBaseAudioVisualizer * scope)
{
  GstCaps *othercaps, *target, *intersect;
  GstStructure *structure;
  const GstCaps *templ;

  templ = gst_pad_get_pad_template_caps (scope->srcpad);

  GST_DEBUG_OBJECT (scope, "performing negotiation");

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (scope->srcpad);
  if (othercaps) {
    intersect = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);

    if (gst_caps_is_empty (intersect))
      goto no_format;

    target = gst_caps_copy_nth (intersect, 0);
    gst_caps_unref (intersect);
  } else {
    target = gst_caps_ref ((GstCaps *) templ);
  }

  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", scope->width);
  gst_structure_fixate_field_nearest_int (structure, "height", scope->height);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate",
      scope->fps_n, scope->fps_d);

  GST_DEBUG_OBJECT (scope, "final caps are %" GST_PTR_FORMAT, target);

  gst_pad_set_caps (scope->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    gst_caps_unref (intersect);
    return FALSE;
  }
}

static gboolean
gst_base_audio_visualizer_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseAudioVisualizer *scope;
  GstBaseAudioVisualizerClass *klass;
  gint w, h;
  gint num, denom;
  GstVideoFormat format;
  gboolean res = TRUE;

  scope = GST_BASE_AUDIO_VISUALIZER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_VISUALIZER_CLASS (G_OBJECT_GET_CLASS (scope));

  if (!gst_video_format_parse_caps (caps, &format, &w, &h)) {
    goto missing_caps_details;
  }
  if (!gst_video_parse_caps_framerate (caps, &num, &denom)) {
    goto missing_caps_details;
  }

  g_mutex_lock (scope->config_lock);

  scope->width = w;
  scope->height = h;
  scope->fps_n = num;
  scope->fps_d = denom;
  scope->video_format = format;

  scope->frame_duration = gst_util_uint64_scale_int (GST_SECOND,
      scope->fps_d, scope->fps_n);
  scope->spf = gst_util_uint64_scale_int (scope->rate,
      scope->fps_d, scope->fps_n);
  scope->req_spf = scope->spf;

  scope->bpf = w * h * 4;

  if (scope->pixelbuf)
    g_free (scope->pixelbuf);
  scope->pixelbuf = g_malloc0 (scope->bpf);

  if (klass->setup)
    res = klass->setup (scope);

  GST_DEBUG_OBJECT (scope, "video: dimension %dx%d, framerate %d/%d",
      scope->width, scope->height, scope->fps_n, scope->fps_d);
  GST_DEBUG_OBJECT (scope, "blocks: spf %u, req_spf %u",
      scope->spf, scope->req_spf);

  g_mutex_unlock (scope->config_lock);

done:
  gst_object_unref (scope);
  return res;

  /* Errors */
missing_caps_details:
  {
    GST_WARNING_OBJECT (scope,
        "missing width, height or framerate in the caps");
    res = FALSE;
    goto done;
  }
}

static GstFlowReturn
gst_base_audio_visualizer_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseAudioVisualizer *scope;
  GstBaseAudioVisualizerClass *klass;
  GstBuffer *inbuf;
  guint avail, sbpf;
  guint8 *adata;
  gboolean (*render) (GstBaseAudioVisualizer * scope, GstBuffer * audio,
      GstBuffer * video);

  scope = GST_BASE_AUDIO_VISUALIZER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_VISUALIZER_CLASS (G_OBJECT_GET_CLASS (scope));

  render = klass->render;

  GST_LOG_OBJECT (scope, "chainfunc called");

  /* resync on DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    scope->next_ts = GST_CLOCK_TIME_NONE;
    gst_adapter_clear (scope->adapter);
  }

  if (GST_PAD_CAPS (scope->srcpad) == NULL) {
    if (!gst_base_audio_visualizer_src_negotiate (scope))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  /* Match timestamps from the incoming audio */
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE)
    scope->next_ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (scope->adapter, buffer);

  g_mutex_lock (scope->config_lock);

  /* this is what we want */
  sbpf = scope->req_spf * scope->channels * sizeof (gint16);

  inbuf = scope->inbuf;
  /* FIXME: the timestamp in the adapter would be different */
  gst_buffer_copy_metadata (inbuf, buffer, GST_BUFFER_COPY_ALL);

  /* this is what we have */
  avail = gst_adapter_available (scope->adapter);
  GST_LOG_OBJECT (scope, "avail: %u, bpf: %u", avail, sbpf);
  while (avail >= sbpf) {
    GstBuffer *outbuf;

    g_mutex_unlock (scope->config_lock);
    ret = gst_pad_alloc_buffer_and_set_caps (scope->srcpad,
        GST_BUFFER_OFFSET_NONE,
        scope->bpf, GST_PAD_CAPS (scope->srcpad), &outbuf);
    g_mutex_lock (scope->config_lock);
    /* recheck as the value could have changed */
    sbpf = scope->req_spf * scope->channels * sizeof (gint16);

    /* no buffer allocated, we don't care why. */
    if (ret != GST_FLOW_OK)
      break;

    /* sync controlled properties */
    gst_object_sync_values (G_OBJECT (scope), scope->next_ts);

    GST_BUFFER_TIMESTAMP (outbuf) = scope->next_ts;
    GST_BUFFER_DURATION (outbuf) = scope->frame_duration;
    if (scope->shader) {
      memcpy (GST_BUFFER_DATA (outbuf), scope->pixelbuf, scope->bpf);
    } else {
      memset (GST_BUFFER_DATA (outbuf), 0, scope->bpf);
    }

    /* this can fail as the data size we need could have changed */
    if (!(adata = (guint8 *) gst_adapter_peek (scope->adapter, sbpf)))
      break;

    GST_BUFFER_DATA (inbuf) = adata;
    GST_BUFFER_SIZE (inbuf) = sbpf;

    /* call class->render() vmethod */
    if (render) {
      if (!render (scope, inbuf, outbuf)) {
        ret = GST_FLOW_ERROR;
      } else {
        /* run various post processing (shading and geometri transformation */
        if (scope->shader) {
          scope->shader (scope, GST_BUFFER_DATA (outbuf), scope->pixelbuf);
        }
      }
    }

    g_mutex_unlock (scope->config_lock);
    ret = gst_pad_push (scope->srcpad, outbuf);
    outbuf = NULL;
    g_mutex_lock (scope->config_lock);

    /* recheck as the value could have changed */
    sbpf = scope->req_spf * scope->channels * sizeof (gint16);
    GST_LOG_OBJECT (scope, "avail: %u, bpf: %u", avail, sbpf);
    /* we want to take less or more, depending on spf : req_spf */
    if (avail - sbpf >= sbpf) {
      gst_adapter_flush (scope->adapter, sbpf);
    } else if (avail - sbpf >= 0) {
      /* just flush a bit and stop */
      gst_adapter_flush (scope->adapter, (avail - sbpf));
      break;
    }
    avail = gst_adapter_available (scope->adapter);

    if (ret != GST_FLOW_OK)
      break;

    if (scope->next_ts != GST_CLOCK_TIME_NONE)
      scope->next_ts += scope->frame_duration;
  }

  g_mutex_unlock (scope->config_lock);

  gst_object_unref (scope);

  return ret;
}

static GstStateChangeReturn
gst_base_audio_visualizer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseAudioVisualizer *scope;

  scope = GST_BASE_AUDIO_VISUALIZER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      scope->next_ts = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (scope->adapter);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}
