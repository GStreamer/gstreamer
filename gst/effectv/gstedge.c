/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. * This library is free software;
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/gstvideofilter.h>

#include <string.h>

#include <gst/video/video.h>

#define GST_TYPE_EDGETV \
  (gst_edgetv_get_type())
#define GST_EDGETV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EDGETV,GstEdgeTV))
#define GST_EDGETV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EDGETV,GstEdgeTVClass))
#define GST_IS_EDGETV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EDGETV))
#define GST_IS_EDGETV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EDGETV))

typedef struct _GstEdgeTV GstEdgeTV;
typedef struct _GstEdgeTVClass GstEdgeTVClass;

struct _GstEdgeTV
{
  GstVideoFilter videofilter;

  gint width, height;
  gint map_width, map_height;
  guint32 *map;
  gint video_width_margin;
};

struct _GstEdgeTVClass
{
  GstVideoFilterClass parent_class;
};

GType gst_edgetv_get_type (void);

static const GstElementDetails gst_edgetv_details =
GST_ELEMENT_DETAILS ("EdgeTV effect",
    "Filter/Effect/Video",
    "Apply edge detect on video",
    "Wim Taymans <wim.taymans@chello.be>");

static GstStaticPadTemplate gst_edgetv_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx)
    );

static GstStaticPadTemplate gst_edgetv_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx)
    );

static GstVideoFilterClass *parent_class = NULL;

static gboolean
gst_edgetv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstEdgeTV *edgetv = GST_EDGETV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_get_int (structure, "width", &edgetv->width) &&
      gst_structure_get_int (structure, "height", &edgetv->height)) {
    edgetv->map_width = edgetv->width / 4;
    edgetv->map_height = edgetv->height / 4;
    edgetv->video_width_margin = edgetv->width % 4;

    g_free (edgetv->map);
    edgetv->map =
        (guint32 *) g_malloc (edgetv->map_width * edgetv->map_height *
        sizeof (guint32) * 2);
    memset (edgetv->map, 0,
        edgetv->map_width * edgetv->map_height * sizeof (guint32) * 2);
    ret = TRUE;
  }

  return ret;
}

static gboolean
gst_edgetv_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstEdgeTV *filter;
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width, height;

  filter = GST_EDGETV (btrans);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    *size = width * height * 32 / 8;
    ret = TRUE;
    GST_DEBUG_OBJECT (filter, "our frame size is %d bytes (%dx%d)", *size,
        width, height);
  }

  return ret;
}

static GstFlowReturn
gst_edgetv_transform (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  GstEdgeTV *filter;
  gint x, y, r, g, b;
  guint32 *src, *dest;
  guint32 p, q;
  guint32 v0, v1, v2, v3;
  GstFlowReturn ret = GST_FLOW_OK;

  filter = GST_EDGETV (trans);

  gst_buffer_copy_metadata (out, in, GST_BUFFER_COPY_TIMESTAMPS);

  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);

  src += filter->width * 4 + 4;
  dest += filter->width * 4 + 4;

  for (y = 1; y < filter->map_height - 1; y++) {
    for (x = 1; x < filter->map_width - 1; x++) {

      p = *src;
      q = *(src - 4);

      /* difference between the current pixel and right neighbor. */
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
      q = *(src - filter->width * 4);
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

      v0 = filter->map[(y - 1) * filter->map_width * 2 + x * 2];
      v1 = filter->map[y * filter->map_width * 2 + (x - 1) * 2 + 1];
      filter->map[y * filter->map_width * 2 + x * 2] = v2;
      filter->map[y * filter->map_width * 2 + x * 2 + 1] = v3;
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
      dest[filter->width] = r | (g - (g >> 8));
      r = v2 + v3;
      g = r & 0x01010100;
      dest[filter->width + 1] = r | (g - (g >> 8));
      dest[filter->width + 2] = v3;
      dest[filter->width + 3] = v3;
      dest[filter->width * 2] = v2;
      dest[filter->width * 2 + 1] = v2;
      dest[filter->width * 3] = v2;
      dest[filter->width * 3 + 1] = v2;

      src += 4;
      dest += 4;
    }
    src += filter->width * 3 + 8 + filter->video_width_margin;
    dest += filter->width * 3 + 8 + filter->video_width_margin;
  }

  return ret;
}

static void
gst_edgetv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_edgetv_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_edgetv_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_edgetv_src_template));
}

static void
gst_edgetv_class_init (gpointer klass, gpointer class_data)
{
  GstBaseTransformClass *trans_class;

  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_edgetv_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_edgetv_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_edgetv_transform);
}

static void
gst_edgetv_init (GTypeInstance * instance, gpointer g_class)
{
  GstEdgeTV *edgetv = GST_EDGETV (instance);

  edgetv->map = NULL;
}

GType
gst_edgetv_get_type (void)
{
  static GType edgetv_type = 0;

  if (!edgetv_type) {
    static const GTypeInfo edgetv_info = {
      sizeof (GstEdgeTVClass),
      gst_edgetv_base_init,
      NULL,
      (GClassInitFunc) gst_edgetv_class_init,
      NULL,
      NULL,
      sizeof (GstEdgeTV),
      0,
      (GInstanceInitFunc) gst_edgetv_init,
    };

    edgetv_type =
        g_type_register_static (GST_TYPE_VIDEO_FILTER, "GstEdgeTV",
        &edgetv_info, 0);
  }
  return edgetv_type;
}
