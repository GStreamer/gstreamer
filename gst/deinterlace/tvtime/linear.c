/*
 * Copyright (C) 2002 Billy Biggs <vektor@dumbterm.net>.
 * Copyright (C) 2008,2010 Sebastian Dröge <slomo@collabora.co.uk>
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

#include "gstdeinterlacemethod.h"
#include <string.h>
#ifdef HAVE_ORC
#include <orc/orc.h>
#endif
#include "tvtime.h"

#define GST_TYPE_DEINTERLACE_METHOD_LINEAR	(gst_deinterlace_method_linear_get_type ())
#define GST_IS_DEINTERLACE_METHOD_LINEAR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_LINEAR))
#define GST_IS_DEINTERLACE_METHOD_LINEAR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_LINEAR))
#define GST_DEINTERLACE_METHOD_LINEAR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_LINEAR, GstDeinterlaceMethodLinearClass))
#define GST_DEINTERLACE_METHOD_LINEAR(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_LINEAR, GstDeinterlaceMethodLinear))
#define GST_DEINTERLACE_METHOD_LINEAR_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_LINEAR, GstDeinterlaceMethodLinearClass))
#define GST_DEINTERLACE_METHOD_LINEAR_CAST(obj)	((GstDeinterlaceMethodLinear*)(obj))

GType gst_deinterlace_method_linear_get_type (void);

typedef GstDeinterlaceSimpleMethod GstDeinterlaceMethodLinear;
typedef GstDeinterlaceSimpleMethodClass GstDeinterlaceMethodLinearClass;

static void
deinterlace_scanline_linear_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const guint8 * s1, const guint8 * s2, gint size)
{
  deinterlace_line_linear (out, s1, s2, size);
}

static void
deinterlace_scanline_linear_packed_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_c (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[0]);
}

static void
deinterlace_scanline_linear_planar_y_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_c (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[0]);
}

static void
deinterlace_scanline_linear_planar_u_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_c (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[1]);
}

static void
deinterlace_scanline_linear_planar_v_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_c (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[2]);
}

#ifdef BUILD_X86_ASM
#include "mmx.h"
static void
deinterlace_scanline_linear_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const guint8 * bot, const guint8 * top, gint size)
{
  const mmx_t shiftmask = { 0xfefffefffefffeffULL };    /* To avoid shifting chroma to luma. */
  int i;

  for (i = size / 32; i; --i) {
    movq_m2r (*bot, mm0);
    movq_m2r (*top, mm1);
    movq_m2r (*(bot + 8), mm2);
    movq_m2r (*(top + 8), mm3);
    movq_m2r (*(bot + 16), mm4);
    movq_m2r (*(top + 16), mm5);
    movq_m2r (*(bot + 24), mm6);
    movq_m2r (*(top + 24), mm7);
    pand_m2r (shiftmask, mm0);
    pand_m2r (shiftmask, mm1);
    pand_m2r (shiftmask, mm2);
    pand_m2r (shiftmask, mm3);
    pand_m2r (shiftmask, mm4);
    pand_m2r (shiftmask, mm5);
    pand_m2r (shiftmask, mm6);
    pand_m2r (shiftmask, mm7);
    psrlw_i2r (1, mm0);
    psrlw_i2r (1, mm1);
    psrlw_i2r (1, mm2);
    psrlw_i2r (1, mm3);
    psrlw_i2r (1, mm4);
    psrlw_i2r (1, mm5);
    psrlw_i2r (1, mm6);
    psrlw_i2r (1, mm7);
    paddb_r2r (mm1, mm0);
    paddb_r2r (mm3, mm2);
    paddb_r2r (mm5, mm4);
    paddb_r2r (mm7, mm6);
    movq_r2m (mm0, *out);
    movq_r2m (mm2, *(out + 8));
    movq_r2m (mm4, *(out + 16));
    movq_r2m (mm6, *(out + 24));
    out += 32;
    top += 32;
    bot += 32;
  }
  size = (size & 0x1f);

  for (i = size / 8; i; --i) {
    movq_m2r (*bot, mm0);
    movq_m2r (*top, mm1);
    pand_m2r (shiftmask, mm0);
    pand_m2r (shiftmask, mm1);
    psrlw_i2r (1, mm0);
    psrlw_i2r (1, mm1);
    paddb_r2r (mm1, mm0);
    movq_r2m (mm0, *out);
    out += 8;
    top += 8;
    bot += 8;
  }
  emms ();

  size = size & 0xf;

  /* Handle last few pixels. */
  for (i = size; i; --i) {
    *out++ = ((*top++) + (*bot++)) >> 1;
  }
}

static void
deinterlace_scanline_linear_packed_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmx (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[0]);
}

static void
deinterlace_scanline_linear_planar_y_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmx (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[0]);
}

static void
deinterlace_scanline_linear_planar_u_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmx (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[1]);
}

static void
deinterlace_scanline_linear_planar_v_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmx (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[2]);
}

#include "sse.h"
static void
deinterlace_scanline_linear_mmxext (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const guint8 * bot, const guint8 * top, gint size)
{
  gint i;

  for (i = size / 32; i; --i) {
    movq_m2r (*bot, mm0);
    movq_m2r (*top, mm1);
    movq_m2r (*(bot + 8), mm2);
    movq_m2r (*(top + 8), mm3);
    movq_m2r (*(bot + 16), mm4);
    movq_m2r (*(top + 16), mm5);
    movq_m2r (*(bot + 24), mm6);
    movq_m2r (*(top + 24), mm7);
    pavgb_r2r (mm1, mm0);
    pavgb_r2r (mm3, mm2);
    pavgb_r2r (mm5, mm4);
    pavgb_r2r (mm7, mm6);
    movntq_r2m (mm0, *out);
    movntq_r2m (mm2, *(out + 8));
    movntq_r2m (mm4, *(out + 16));
    movntq_r2m (mm6, *(out + 24));
    out += 32;
    top += 32;
    bot += 32;
  }
  size = (size & 0x1f);

  for (i = size / 8; i; --i) {
    movq_m2r (*bot, mm0);
    movq_m2r (*top, mm1);
    pavgb_r2r (mm1, mm0);
    movntq_r2m (mm0, *out);
    out += 8;
    top += 8;
    bot += 8;
  }
  emms ();

  size = size & 0xf;

  /* Handle last few pixels. */
  for (i = size; i; --i) {
    *out++ = ((*top++) + (*bot++)) >> 1;
  }
}

static void
deinterlace_scanline_linear_packed_mmxext (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmxext (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[0]);
}

static void
deinterlace_scanline_linear_planar_y_mmxext (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmxext (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[0]);
}

static void
deinterlace_scanline_linear_planar_u_mmxext (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmxext (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[1]);
}

static void
deinterlace_scanline_linear_planar_v_mmxext (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_mmxext (self, out, scanlines->t0, scanlines->b0,
      self->parent.row_stride[2]);
}

#endif

G_DEFINE_TYPE (GstDeinterlaceMethodLinear, gst_deinterlace_method_linear,
    GST_TYPE_DEINTERLACE_SIMPLE_METHOD);

static void
gst_deinterlace_method_linear_class_init (GstDeinterlaceMethodLinearClass *
    klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GstDeinterlaceSimpleMethodClass *dism_class =
      (GstDeinterlaceSimpleMethodClass *) klass;
#ifdef BUILD_X86_ASM
  guint cpu_flags =
      orc_target_get_default_flags (orc_target_get_by_name ("mmx"));
#endif

  dim_class->fields_required = 1;
  dim_class->name = "Television: Full resolution";
  dim_class->nick = "linear";
  dim_class->latency = 0;

  dism_class->interpolate_scanline_yuy2 = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_yvyu = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_uyvy = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_ayuv = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_argb = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_abgr = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_rgba = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_bgra = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_rgb = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_bgr = deinterlace_scanline_linear_packed_c;
  dism_class->interpolate_scanline_planar_y =
      deinterlace_scanline_linear_planar_y_c;
  dism_class->interpolate_scanline_planar_u =
      deinterlace_scanline_linear_planar_u_c;
  dism_class->interpolate_scanline_planar_v =
      deinterlace_scanline_linear_planar_v_c;

#ifdef BUILD_X86_ASM
  if (cpu_flags & ORC_TARGET_MMX_MMXEXT) {
    dism_class->interpolate_scanline_ayuv =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_yuy2 =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_yvyu =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_uyvy =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_argb =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_abgr =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_rgba =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_bgra =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_rgb =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_bgr =
        deinterlace_scanline_linear_packed_mmxext;
    dism_class->interpolate_scanline_planar_y =
        deinterlace_scanline_linear_planar_y_mmxext;
    dism_class->interpolate_scanline_planar_u =
        deinterlace_scanline_linear_planar_u_mmxext;
    dism_class->interpolate_scanline_planar_v =
        deinterlace_scanline_linear_planar_v_mmxext;
  } else if (cpu_flags & ORC_TARGET_MMX_MMX) {
    dism_class->interpolate_scanline_ayuv =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_yuy2 =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_yvyu =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_uyvy =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_argb =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_abgr =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_rgba =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_bgra =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_rgb =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_bgr =
        deinterlace_scanline_linear_packed_mmx;
    dism_class->interpolate_scanline_planar_y =
        deinterlace_scanline_linear_planar_y_mmx;
    dism_class->interpolate_scanline_planar_u =
        deinterlace_scanline_linear_planar_u_mmx;
    dism_class->interpolate_scanline_planar_v =
        deinterlace_scanline_linear_planar_v_mmx;
  }
#endif
}

static void
gst_deinterlace_method_linear_init (GstDeinterlaceMethodLinear * self)
{
}
