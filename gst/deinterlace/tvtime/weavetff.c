/**
 * Weave frames, top-field-first.
 * Copyright (C) 2003 Billy Biggs <vektor@dumbterm.net>.
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "_stdint.h"
#include "gstdeinterlace.h"
#include <string.h>

#define GST_TYPE_DEINTERLACE_METHOD_WEAVE_TFF	(gst_deinterlace_method_weave_tff_get_type ())
#define GST_IS_DEINTERLACE_METHOD_WEAVE_TFF(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_WEAVE_TFF))
#define GST_IS_DEINTERLACE_METHOD_WEAVE_TFF_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_WEAVE_TFF))
#define GST_DEINTERLACE_METHOD_WEAVE_TFF_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_WEAVE_TFF, GstDeinterlaceMethodWeaveTFFClass))
#define GST_DEINTERLACE_METHOD_WEAVE_TFF(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_WEAVE_TFF, GstDeinterlaceMethodWeaveTFF))
#define GST_DEINTERLACE_METHOD_WEAVE_TFF_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_WEAVE_TFF, GstDeinterlaceMethodWeaveTFFClass))
#define GST_DEINTERLACE_METHOD_WEAVE_TFF_CAST(obj)	((GstDeinterlaceMethodWeaveTFF*)(obj))

GType gst_deinterlace_method_weave_tff_get_type (void);

typedef GstDeinterlaceSimpleMethod GstDeinterlaceMethodWeaveTFF;

typedef GstDeinterlaceSimpleMethodClass GstDeinterlaceMethodWeaveTFFClass;


static void
deinterlace_scanline_weave (GstDeinterlaceMethod * self,
    GstDeinterlace * parent, guint8 * out,
    GstDeinterlaceScanlineData * scanlines, gint width)
{
  oil_memcpy (out, scanlines->m1, parent->row_stride);
}

static void
copy_scanline (GstDeinterlaceMethod * self, GstDeinterlace * parent,
    guint8 * out, GstDeinterlaceScanlineData * scanlines, gint width)
{
  /* FIXME: original code used m2 and m0 but this looks really bad */
  if (scanlines->bottom_field) {
    oil_memcpy (out, scanlines->bb0, parent->row_stride);
  } else {
    oil_memcpy (out, scanlines->bb2, parent->row_stride);
  }
}

G_DEFINE_TYPE (GstDeinterlaceMethodWeaveTFF, gst_deinterlace_method_weave_tff,
    GST_TYPE_DEINTERLACE_SIMPLE_METHOD);

static void
gst_deinterlace_method_weave_tff_class_init (GstDeinterlaceMethodWeaveTFFClass *
    klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GstDeinterlaceSimpleMethodClass *dism_class =
      (GstDeinterlaceSimpleMethodClass *) klass;

  dim_class->fields_required = 3;
  dim_class->name = "Progressive: Top Field First";
  dim_class->nick = "weavetff";
  dim_class->latency = 0;

  dism_class->interpolate_scanline = deinterlace_scanline_weave;
  dism_class->copy_scanline = copy_scanline;
}

static void
gst_deinterlace_method_weave_tff_init (GstDeinterlaceMethodWeaveTFF * self)
{
}
