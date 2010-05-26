/* GStreamer
 * Copyright (C) <2010> Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

#include "gstgeometrictransform.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (geometric_transform_debug);
#define GST_CAT_DEFAULT geometric_transform_debug

static GstStaticPadTemplate gst_geometric_transform_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb; video/x-raw-gray")
    );

static GstStaticPadTemplate gst_geometric_transform_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb; video/x-raw-gray")
    );

static GstVideoFilterClass *parent_class = NULL;

static gboolean
gst_geometric_transform_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGeometricTransform *gt;
  gboolean ret;

  gt = GST_GEOMETRIC_TRANSFORM (btrans);

  ret = gst_video_format_parse_caps (incaps, &gt->format, &gt->width,
      &gt->height);
  if (ret) {
    gt->row_stride = gst_video_format_get_row_stride (gt->format, 0, gt->width);
    gt->pixel_stride = gst_video_format_get_pixel_stride (gt->format, 0);
  }
  return ret;
}

static void
gst_geometric_transform_do_map (GstGeometricTransform * gt, GstBuffer * inbuf,
    GstBuffer * outbuf, gint x, gint y, gdouble out_x, gdouble out_y)
{
  gint trunc_x = (gint) out_x;
  gint trunc_y = (gint) out_y;
  gint in_offset;
  gint out_offset;

  out_offset = y * gt->row_stride + x * gt->pixel_stride;
  in_offset = trunc_y * gt->row_stride + trunc_x * gt->pixel_stride;

  memcpy (GST_BUFFER_DATA (outbuf) + out_offset,
      GST_BUFFER_DATA (inbuf) + in_offset, gt->pixel_stride);
}

static GstFlowReturn
gst_geometric_transform_transform (GstBaseTransform * trans, GstBuffer * buf,
    GstBuffer * outbuf)
{
  GstGeometricTransform *gt;
  GstGeometricTransformClass *klass;
  gint x, y;
  gdouble out_x, out_y;
  guint8 *data;
  GstFlowReturn ret = GST_FLOW_OK;

  gt = GST_GEOMETRIC_TRANSFORM (trans);
  klass = GST_GEOMETRIC_TRANSFORM_GET_CLASS (gt);

  /* subclass must have defined the map_func */
  g_return_val_if_fail (klass->map_func, GST_FLOW_ERROR);

  data = GST_BUFFER_DATA (buf);
  for (x = 0; x < gt->width; x++) {
    for (y = 0; y < gt->height; y++) {
      if (!klass->map_func (gt, x, y, &out_x, &out_y)) {
        /* child should have warned */
        ret = GST_FLOW_ERROR;
        goto end;
      }

      /* do the mapping */
      gst_geometric_transform_do_map (gt, buf, outbuf, x, y, out_x, out_y);
    }
  }

end:
  return ret;
}

static void
gst_geometric_transform_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_geometric_transform_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_geometric_transform_src_template));
}

static void
gst_geometric_transform_class_init (gpointer klass, gpointer class_data)
{
  GstBaseTransformClass *trans_class;

  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_geometric_transform_set_caps);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_geometric_transform_transform);
}

static void
gst_geometric_transform_init (GTypeInstance * instance, gpointer g_class)
{
}

GType
gst_geometric_transform_get_type (void)
{
  static GType geometric_transform_type = 0;

  if (!geometric_transform_type) {
    static const GTypeInfo geometric_transform_info = {
      sizeof (GstGeometricTransformClass),
      gst_geometric_transform_base_init,
      NULL,
      gst_geometric_transform_class_init,
      NULL,
      NULL,
      sizeof (GstGeometricTransform),
      0,
      gst_geometric_transform_init,
    };

    geometric_transform_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstGeometricTransform", &geometric_transform_info,
        G_TYPE_FLAG_ABSTRACT);

    GST_DEBUG_CATEGORY_INIT (geometric_transform_debug, "geometrictransform", 0,
        "Base class for geometric transform elements");
  }
  return geometric_transform_type;
}
