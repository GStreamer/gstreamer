/*
 * GStreamer
 * Copyright (C) 2010 Filippo Argiolas <filippo.argiolas@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
#  include <config.h>
#endif

#include <gst/gst.h>
#include <math.h>

#include "gststretch.h"
#include "geometricmath.h"

GST_DEBUG_CATEGORY_STATIC (gst_stretch_debug);
#define GST_CAT_DEFAULT gst_stretch_debug

GST_BOILERPLATE (GstStretch, gst_stretch, GstCircleGeometricTransform,
    GST_TYPE_CIRCLE_GEOMETRIC_TRANSFORM);

/* Clean up */
static void
gst_stretch_finalize (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* GObject vmethod implementations */

static void
gst_stretch_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "stretch",
      "Transform/Effect/Video",
      "Stretch the image in a circle around the center point",
      "Filippo Argiolas <filippo.argiolas@gmail.com>");
}

static gboolean
stretch_map (GstGeometricTransform * gt, gint x, gint y, gdouble * in_x,
    gdouble * in_y)
{
  GstCircleGeometricTransform *cgt = GST_CIRCLE_GEOMETRIC_TRANSFORM_CAST (gt);
  GstStretch *stretch = GST_STRETCH_CAST (gt);

  gdouble norm_x, norm_y;
  gdouble r;

  gdouble width = gt->width;
  gdouble height = gt->height;

  /* normalize in ((-1.0, -1.0), (1.0, 1.0) and traslate the center */
  norm_x = 2.0 * (x / width - cgt->x_center);
  norm_y = 2.0 * (y / height - cgt->y_center);

  r = sqrt (norm_x * norm_x + norm_y * norm_y);

  /* TODO: make it customizable, 0.7 is the radius of the stretch
   * ellipse, with current way of setting radius (absolute value) it
   * would be sqrt(hw*hw + hh*hh)/radius*radius but currently I prefer
   * leaving it hardcoded because I don't think absolute radius makes
   * any sense: the user either knows the video size and can easily
   * calculate the fractional radius and set that or doesn't know the
   * size a priori and wants to obtain the same effect no matter of
   * the video dimensions */

  /* actually "stretch" name is a bit misleading, what the transform
   * really does is shrink the center and gradually return to normal
   * size while r increases. The shrink thing drags pixels giving
   * stretching the image around the center */
  /* 2.0 is the shrink amount. Make it customizable? */
  norm_x *= 2.0 - smoothstep (0.0, 0.7, r);
  norm_y *= 2.0 - smoothstep (0.0, 0.7, r);

  /* unnormalize */
  *in_x = (0.5 * norm_x + cgt->x_center) * width;
  *in_y = (0.5 * norm_y + cgt->y_center) * height;

  GST_DEBUG_OBJECT (stretch, "Inversely mapped %d %d into %lf %lf",
      x, y, *in_x, *in_y);

  return TRUE;
}

static void
gst_stretch_class_init (GstStretchClass * klass)
{
  GObjectClass *gobject_class;
  GstGeometricTransformClass *gstgt_class;

  gobject_class = (GObjectClass *) klass;
  gstgt_class = (GstGeometricTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_stretch_finalize);

  gstgt_class->map_func = stretch_map;
}

static void
gst_stretch_init (GstStretch * filter, GstStretchClass * gclass)
{
  GstGeometricTransform *gt = GST_GEOMETRIC_TRANSFORM (filter);

  gt->off_edge_pixels = GST_GT_OFF_EDGES_PIXELS_CLAMP;
}

gboolean
gst_stretch_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_stretch_debug, "stretch", 0, "stretch");

  return gst_element_register (plugin, "stretch", GST_RANK_NONE,
      GST_TYPE_STRETCH);
}
