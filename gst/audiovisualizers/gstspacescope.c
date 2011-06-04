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
  GstBaseAudioVisualizerClass *scope_class =
      (GstBaseAudioVisualizerClass *) g_class;

  scope_class->render = GST_DEBUG_FUNCPTR (gst_space_scope_render);
}

static void
gst_space_scope_init (GstSpaceScope * scope, GstSpaceScopeClass * g_class)
{
  /* do nothing */
}

static gboolean
gst_space_scope_render (GstBaseAudioVisualizer * scope, GstBuffer * audio,
    GstBuffer * video)
{
  guint32 *vdata = (guint32 *) GST_BUFFER_DATA (video);
  gint16 *adata = (gint16 *) GST_BUFFER_DATA (audio);
  guint i, s, x, y, off, ox, oy;
  guint num_samples;
  gfloat dx, dy;
  guint w = scope->width;

  /* draw dots 1st channel x, 2nd channel y */
  num_samples = GST_BUFFER_SIZE (audio) / (scope->channels * sizeof (gint16));
  dx = scope->width / 65536.0;
  ox = scope->width / 2;
  dy = scope->height / 65536.0;
  oy = scope->height / 2;
  s = 0;
  for (i = 0; i < num_samples; i++) {
    x = (guint) (ox + (gfloat) adata[s++] * dx);
    y = (guint) (oy + (gfloat) adata[s++] * dy);
    off = (y * w) + x;
    vdata[off] = 0x00FFFFFF;
  }
  return TRUE;
}

gboolean
gst_space_scope_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (space_scope_debug, "spacescope", 0, "spacescope");

  return gst_element_register (plugin, "spacescope", GST_RANK_NONE,
      GST_TYPE_SPACE_SCOPE);
}
