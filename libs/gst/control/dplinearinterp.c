/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdplinearinterp.c: linear interpolation dynamic parameter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "dplinearinterp.h"

static void gst_dp_linint_class_init (GstDParamClass * klass);
static void gst_dp_linint_base_class_init (GstDParamClass * klass);
static void gst_dp_linint_init (GstDParam * dp_linint);

GType
gst_dp_linint_get_type (void)
{
  static GType dp_linint_type = 0;

  if (!dp_linint_type) {
    static const GTypeInfo dp_linint_info = {
      sizeof (GstDParamClass),
      (GBaseInitFunc) gst_dp_linint_base_class_init,
      NULL,
      (GClassInitFunc) gst_dp_linint_class_init,
      NULL,
      NULL,
      sizeof (GstDParam),
      0,
      (GInstanceInitFunc) gst_dp_linint_init,
    };
    dp_linint_type =
	g_type_register_static (GST_TYPE_DPARAM, "GstDParamLinInterp",
	&dp_linint_info, 0);
  }
  return dp_linint_type;
}

static void
gst_dp_linint_base_class_init (GstDParamClass * klass)
{

}

static void
gst_dp_linint_class_init (GstDParamClass * klass)
{

}

static void
gst_dp_linint_init (GstDParam * dp_linint)
{
  g_return_if_fail (dp_linint != NULL);
}

/**
 * gst_dp_linint_new:
 * @type: the type that this dp_linint will store
 *
 * Returns: a new instance of GstDParam
 */
GstDParam *
gst_dp_linint_new (GType type)
{
  GstDParam *dparam;
  GstDParamLinInterp *dp_linint;

  dp_linint = g_object_new (gst_dp_linint_get_type (), NULL);
  dparam = GST_DPARAM (dp_linint);

  return dparam;
}
