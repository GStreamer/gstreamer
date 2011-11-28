/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * EffecTV:
 * Copyright (C) 2001-2002 FUKUCHI Kentarou
 *
 * EdgeTV - detects edge and display it in good old computer way
 *
 * EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

/**
 * SECTION:element-edgetv
 *
 * EdgeTV detects edges and display it in good old low resolution
 * computer way.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! edgetv ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of edgetv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstedge.h"

#include <gst/video/video.h>

GST_BOILERPLATE (GstEdgeTV, gst_edgetv, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS_STR GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_RGBx
#else
#define CAPS_STR GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_xRGB
#endif

static GstStaticPadTemplate gst_edgetv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static GstStaticPadTemplate gst_edgetv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static gboolean
gst_edgetv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstEdgeTV *edgetv = GST_EDGETV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (edgetv);
  if (gst_structure_get_int (structure, "width", &edgetv->width) &&
      gst_structure_get_int (structure, "height", &edgetv->height)) {
    guint map_size;

    edgetv->map_width = edgetv->width / 4;
    edgetv->map_height = edgetv->height / 4;
    edgetv->video_width_margin = edgetv->width % 4;

    map_size = edgetv->map_width * edgetv->map_height * sizeof (guint32) * 2;

    g_free (edgetv->map);
    edgetv->map = (guint32 *) g_malloc0 (map_size);
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (edgetv);

  return ret;
}

static GstFlowReturn
gst_edgetv_transform (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  GstEdgeTV *filter = GST_EDGETV (trans);
  gint x, y, r, g, b;
  guint32 *src, *dest;
  guint32 p, q;
  guint32 v0, v1, v2, v3;
  gint width, map_height, map_width;
  gint video_width_margin;
  guint32 *map;
  GstFlowReturn ret = GST_FLOW_OK;

  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);

  GST_OBJECT_LOCK (filter);
  map = filter->map;
  width = filter->width;
  map_height = filter->map_height;
  map_width = filter->map_width;
  video_width_margin = filter->video_width_margin;
  src += width * 4 + 4;
  dest += width * 4 + 4;

  for (y = 1; y < map_height - 1; y++) {
    for (x = 1; x < map_width - 1; x++) {
      p = *src;
      q = *(src - 4);

      /* difference between the current pixel and left neighbor. */
      r = ((p & 0xff0000) - (q & 0xff0000)) >> 16;
      g = ((p & 0xff00) - (q & 0xff00)) >> 8;
      b = (p & 0xff) - (q & 0xff);
      r *= r;
      g *= g;
      b *= b;
      r = r >> 5;               /* To lack the lower bit for saturated addition,  */
      g = g >> 5;               /* devide the value with 32, instead of 16. It is */
      b = b >> 4;               /* same as `v2 &= 0xfefeff' */
      if (r > 127)
        r = 127;
      if (g > 127)
        g = 127;
      if (b > 255)
        b = 255;
      v2 = (r << 17) | (g << 9) | b;

      /* difference between the current pixel and upper neighbor. */
      q = *(src - width * 4);
      r = ((p & 0xff0000) - (q & 0xff0000)) >> 16;
      g = ((p & 0xff00) - (q & 0xff00)) >> 8;
      b = (p & 0xff) - (q & 0xff);
      r *= r;
      g *= g;
      b *= b;
      r = r >> 5;
      g = g >> 5;
      b = b >> 4;
      if (r > 127)
        r = 127;
      if (g > 127)
        g = 127;
      if (b > 255)
        b = 255;
      v3 = (r << 17) | (g << 9) | b;

      v0 = map[(y - 1) * map_width * 2 + x * 2];
      v1 = map[y * map_width * 2 + (x - 1) * 2 + 1];
      map[y * map_width * 2 + x * 2] = v2;
      map[y * map_width * 2 + x * 2 + 1] = v3;
      r = v0 + v1;
      g = r & 0x01010100;
      dest[0] = r | (g - (g >> 8));
      r = v0 + v3;
      g = r & 0x01010100;
      dest[1] = r | (g - (g >> 8));
      dest[2] = v3;
      dest[3] = v3;
      r = v2 + v1;
      g = r & 0x01010100;
      dest[width] = r | (g - (g >> 8));
      r = v2 + v3;
      g = r & 0x01010100;
      dest[width + 1] = r | (g - (g >> 8));
      dest[width + 2] = v3;
      dest[width + 3] = v3;
      dest[width * 2] = v2;
      dest[width * 2 + 1] = v2;
      dest[width * 2 + 2] = 0;
      dest[width * 2 + 3] = 0;
      dest[width * 3] = v2;
      dest[width * 3 + 1] = v2;
      dest[width * 3 + 2] = 0;
      dest[width * 3 + 3] = 0;

      src += 4;
      dest += 4;
    }
    src += width * 3 + 8 + video_width_margin;
    dest += width * 3 + 8 + video_width_margin;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static gboolean
gst_edgetv_start (GstBaseTransform * trans)
{
  GstEdgeTV *edgetv = GST_EDGETV (trans);

  if (edgetv->map)
    memset (edgetv->map, 0,
        edgetv->map_width * edgetv->map_height * sizeof (guint32) * 2);
  return TRUE;
}

static void
gst_edgetv_finalize (GObject * object)
{
  GstEdgeTV *edgetv = GST_EDGETV (object);

  g_free (edgetv->map);
  edgetv->map = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_edgetv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "EdgeTV effect",
      "Filter/Effect/Video",
      "Apply edge detect on video", "Wim Taymans <wim.taymans@chello.be>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_edgetv_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_edgetv_src_template);
}

static void
gst_edgetv_class_init (GstEdgeTVClass * klass)
{
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_edgetv_finalize;

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_edgetv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_edgetv_transform);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_edgetv_start);
}

static void
gst_edgetv_init (GstEdgeTV * edgetv, GstEdgeTVClass * klass)
{
}
