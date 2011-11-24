/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstspacescope.c: simple stereo visualizer
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
 * SECTION:element-spacescope
 * @see_also: goom
 *
 * Spacescope is a simple audio visualisation element. It maps the left and
 * right channel to x and y coordinates.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc ! audioconvert ! spacescope ! ximagesink
 * ]|
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include "gstspacescope.h"

static GstStaticPadTemplate gst_space_scope_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate gst_space_scope_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );


GST_DEBUG_CATEGORY_STATIC (space_scope_debug);
#define GST_CAT_DEFAULT space_scope_debug

enum
{
  PROP_0,
  PROP_STYLE
};

enum
{
  STYLE_DOTS = 0,
  STYLE_LINES,
  NUM_STYLES
};

#define GST_TYPE_SPACE_SCOPE_STYLE (gst_space_scope_style_get_type ())
static GType
gst_space_scope_style_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {STYLE_DOTS, "draw dots (default)", "dots"},
      {STYLE_LINES, "draw lines", "lines"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstSpaceScopeStyle", values);
  }
  return gtype;
}

static void gst_space_scope_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_space_scope_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void render_dots (GstBaseAudioVisualizer * scope, guint32 * vdata,
    gint16 * adata, guint num_samples);
static void render_lines (GstBaseAudioVisualizer * scope, guint32 * vdata,
    gint16 * adata, guint num_samples);

static gboolean gst_space_scope_render (GstBaseAudioVisualizer * scope,
    GstBuffer * audio, GstBuffer * video);


GST_BOILERPLATE (GstSpaceScope, gst_space_scope, GstBaseAudioVisualizer,
    GST_TYPE_BASE_AUDIO_VISUALIZER);

static void
gst_space_scope_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Stereo visualizer",
      "Visualization",
      "Simple stereo visualizer", "Stefan Kost <ensonic@users.sf.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_space_scope_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_space_scope_sink_template));
}

static void
gst_space_scope_class_init (GstSpaceScopeClass * g_class)
{
  GObjectClass *gobject_class = (GObjectClass *) g_class;
  GstBaseAudioVisualizerClass *scope_class =
      (GstBaseAudioVisualizerClass *) g_class;

  gobject_class->set_property = gst_space_scope_set_property;
  gobject_class->get_property = gst_space_scope_get_property;

  scope_class->render = GST_DEBUG_FUNCPTR (gst_space_scope_render);

  g_object_class_install_property (gobject_class, PROP_STYLE,
      g_param_spec_enum ("style", "drawing style",
          "Drawing styles for the space scope display.",
          GST_TYPE_SPACE_SCOPE_STYLE, STYLE_DOTS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_space_scope_init (GstSpaceScope * scope, GstSpaceScopeClass * g_class)
{
  /* do nothing */
}

static void
gst_space_scope_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpaceScope *scope = GST_SPACE_SCOPE (object);

  switch (prop_id) {
    case PROP_STYLE:
      scope->style = g_value_get_enum (value);
      switch (scope->style) {
        case STYLE_DOTS:
          scope->process = render_dots;
          break;
        case STYLE_LINES:
          scope->process = render_lines;
          break;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_space_scope_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSpaceScope *scope = GST_SPACE_SCOPE (object);

  switch (prop_id) {
    case PROP_STYLE:
      g_value_set_enum (value, scope->style);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#include "gstdrawhelpers.h"

static void
render_dots (GstBaseAudioVisualizer * scope, guint32 * vdata, gint16 * adata,
    guint num_samples)
{
  guint i, s, x, y, ox, oy;
  gfloat dx, dy;
  guint w = scope->width;

  /* draw dots 1st channel x, 2nd channel y */
  dx = scope->width / 65536.0;
  ox = scope->width / 2;
  dy = scope->height / 65536.0;
  oy = scope->height / 2;
  s = 0;
  for (i = 0; i < num_samples; i++) {
    x = (guint) (ox + (gfloat) adata[s++] * dx);
    y = (guint) (oy + (gfloat) adata[s++] * dy);
    draw_dot (vdata, x, y, w, 0x00FFFFFF);
  }
}

static void
render_lines (GstBaseAudioVisualizer * scope, guint32 * vdata, gint16 * adata,
    guint num_samples)
{
  guint i, s, x, y, ox, oy;
  gfloat dx, dy;
  guint w = scope->width;
  guint h = scope->height;
  gint x2, y2;

  /* draw lines 1st channel x, 2nd channel y */
  dx = (w - 1) / 65536.0;
  ox = (w - 1) / 2;
  dy = (h - 1) / 65536.0;
  oy = (h - 1) / 2;
  s = 0;
  x2 = (guint) (ox + (gfloat) adata[s++] * dx);
  y2 = (guint) (oy + (gfloat) adata[s++] * dy);
  for (i = 1; i < num_samples; i++) {
    x = (guint) (ox + (gfloat) adata[s++] * dx);
    y = (guint) (oy + (gfloat) adata[s++] * dy);
    draw_line_aa (vdata, x2, x, y2, y, w, 0x00FFFFFF);
    x2 = x;
    y2 = y;
  }
}

static gboolean
gst_space_scope_render (GstBaseAudioVisualizer * base, GstBuffer * audio,
    GstBuffer * video)
{
  GstSpaceScope *scope = GST_SPACE_SCOPE (base);
  guint32 *vdata = (guint32 *) GST_BUFFER_DATA (video);
  gint16 *adata = (gint16 *) GST_BUFFER_DATA (audio);
  guint num_samples;

  num_samples = GST_BUFFER_SIZE (audio) / (base->channels * sizeof (gint16));
  scope->process (base, vdata, adata, num_samples);
  return TRUE;
}

gboolean
gst_space_scope_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (space_scope_debug, "spacescope", 0, "spacescope");

  return gst_element_register (plugin, "spacescope", GST_RANK_NONE,
      GST_TYPE_SPACE_SCOPE);
}
