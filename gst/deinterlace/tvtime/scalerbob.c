/**
 * Double lines
 * Copyright (C) 2008,2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "gstdeinterlacemethod.h"
#include <string.h>

#define GST_TYPE_DEINTERLACE_METHOD_SCALER_BOB	(gst_deinterlace_method_scaler_bob_get_type ())
#define GST_IS_DEINTERLACE_METHOD_SCALER_BOB(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_SCALER_BOB))
#define GST_IS_DEINTERLACE_METHOD_SCALER_BOB_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_SCALER_BOB))
#define GST_DEINTERLACE_METHOD_SCALER_BOB_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_SCALER_BOB, GstDeinterlaceMethodScalerBobClass))
#define GST_DEINTERLACE_METHOD_SCALER_BOB(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_SCALER_BOB, GstDeinterlaceMethodScalerBob))
#define GST_DEINTERLACE_METHOD_SCALER_BOB_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_SCALER_BOB, GstDeinterlaceMethodScalerBobClass))
#define GST_DEINTERLACE_METHOD_SCALER_BOB_CAST(obj)	((GstDeinterlaceMethodScalerBob*)(obj))

GType gst_deinterlace_method_scaler_bob_get_type (void);

typedef GstDeinterlaceSimpleMethod GstDeinterlaceMethodScalerBob;
typedef GstDeinterlaceSimpleMethodClass GstDeinterlaceMethodScalerBobClass;

static void
deinterlace_scanline_scaler_bob_packed (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  oil_memcpy (out, scanlines->t0, self->parent.row_stride[0]);
}

G_DEFINE_TYPE (GstDeinterlaceMethodScalerBob, gst_deinterlace_method_scaler_bob,
    GST_TYPE_DEINTERLACE_SIMPLE_METHOD);

static void
gst_deinterlace_method_scaler_bob_class_init (GstDeinterlaceMethodScalerBobClass
    * klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GstDeinterlaceSimpleMethodClass *dism_class =
      (GstDeinterlaceSimpleMethodClass *) klass;

  dim_class->fields_required = 1;
  dim_class->name = "Double lines";
  dim_class->nick = "scalerbob";
  dim_class->latency = 0;

  dism_class->interpolate_scanline_yuy2 =
      deinterlace_scanline_scaler_bob_packed;
  dism_class->interpolate_scanline_yvyu =
      deinterlace_scanline_scaler_bob_packed;
}

static void
gst_deinterlace_method_scaler_bob_init (GstDeinterlaceMethodScalerBob * self)
{
}
