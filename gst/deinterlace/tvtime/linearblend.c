/**
 * Linear blend deinterlacing plugin.  The idea for this algorithm came
 * from the linear blend deinterlacer which originated in the mplayer
 * sources.
 *
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

#include "gstdeinterlace.h"
#include <string.h>

#define GST_TYPE_DEINTERLACE_METHOD_LINEAR_BLEND	(gst_deinterlace_method_linear_blend_get_type ())
#define GST_IS_DEINTERLACE_METHOD_LINEAR_BLEND(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_LINEAR_BLEND))
#define GST_IS_DEINTERLACE_METHOD_LINEAR_BLEND_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_LINEAR_BLEND))
#define GST_DEINTERLACE_METHOD_LINEAR_BLEND_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_LINEAR_BLEND, GstDeinterlaceMethodLinearBlendClass))
#define GST_DEINTERLACE_METHOD_LINEAR_BLEND(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_LINEAR_BLEND, GstDeinterlaceMethodLinearBlend))
#define GST_DEINTERLACE_METHOD_LINEAR_BLEND_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_LINEAR_BLEND, GstDeinterlaceMethodLinearBlendClass))
#define GST_DEINTERLACE_METHOD_LINEAR_BLEND_CAST(obj)	((GstDeinterlaceMethodLinearBlend*)(obj))

GType gst_deinterlace_method_linear_blend_get_type (void);

typedef GstDeinterlaceSimpleMethod GstDeinterlaceMethodLinearBlend;
typedef GstDeinterlaceSimpleMethodClass GstDeinterlaceMethodLinearBlendClass;

static inline void
deinterlace_scanline_linear_blend_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const guint8 * t0, const guint8 * b0, const guint8 * m1,
    gint size)
{
  while (size--) {
    *out++ = (*t0++ + *b0++ + (*m1++ << 1)) >> 2;
  }
}

static void
deinterlace_scanline_linear_blend_packed_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_blend_c (self, out, scanlines->t0, scanlines->b0,
      scanlines->m1, self->parent.row_stride[0]);
}

static inline void
deinterlace_scanline_linear_blend2_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const guint8 * m0, const guint8 * t1, const guint8 * b1,
    gint size)
{
  while (size--) {
    *out++ = (*t1++ + *b1++ + (*m0++ << 1)) >> 2;
  }
}

static void
deinterlace_scanline_linear_blend2_packed_c (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_blend2_c (self, out, scanlines->m0, scanlines->t1,
      scanlines->b1, self->parent.row_stride[0]);
}

#ifdef BUILD_X86_ASM
#include "mmx.h"
static inline void
deinterlace_scanline_linear_blend_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const guint8 * t0, const guint8 * b0, const guint8 * m1,
    gint size)
{
  gint i;

  i = size / 8;
  size -= i * 8;

  pxor_r2r (mm7, mm7);
  while (i--) {
    movd_m2r (*t0, mm0);
    movd_m2r (*b0, mm1);
    movd_m2r (*m1, mm2);

    movd_m2r (*(t0 + 4), mm3);
    movd_m2r (*(b0 + 4), mm4);
    movd_m2r (*(m1 + 4), mm5);

    punpcklbw_r2r (mm7, mm0);
    punpcklbw_r2r (mm7, mm1);
    punpcklbw_r2r (mm7, mm2);

    punpcklbw_r2r (mm7, mm3);
    punpcklbw_r2r (mm7, mm4);
    punpcklbw_r2r (mm7, mm5);

    psllw_i2r (1, mm2);
    psllw_i2r (1, mm5);
    paddw_r2r (mm0, mm2);
    paddw_r2r (mm3, mm5);
    paddw_r2r (mm1, mm2);
    paddw_r2r (mm4, mm5);
    psrlw_i2r (2, mm2);
    psrlw_i2r (2, mm5);
    packuswb_r2r (mm2, mm2);
    packuswb_r2r (mm5, mm5);

    movd_r2m (mm2, *out);
    movd_r2m (mm5, *(out + 4));
    out += 8;
    t0 += 8;
    b0 += 8;
    m1 += 8;
  }
  emms ();
  while (size--) {
    *out++ = (*t0++ + *b0++ + (*m1++ << 1)) >> 2;
  }
}

static void
deinterlace_scanline_linear_blend_packed_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_blend_mmx (self, out, scanlines->t0,
      scanlines->b0, scanlines->m1, self->parent.row_stride[0]);
}

static inline void
deinterlace_scanline_linear_blend2_mmx (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const guint8 * m0, const guint8 * t1, const guint8 * b1,
    gint size)
{
  gint i;

  i = size / 8;
  size -= i * 8;

  pxor_r2r (mm7, mm7);
  while (i--) {
    movd_m2r (*t1, mm0);
    movd_m2r (*b1, mm1);
    movd_m2r (*m0, mm2);

    movd_m2r (*(t1 + 4), mm3);
    movd_m2r (*(b1 + 4), mm4);
    movd_m2r (*(m0 + 4), mm5);

    punpcklbw_r2r (mm7, mm0);
    punpcklbw_r2r (mm7, mm1);
    punpcklbw_r2r (mm7, mm2);

    punpcklbw_r2r (mm7, mm3);
    punpcklbw_r2r (mm7, mm4);
    punpcklbw_r2r (mm7, mm5);

    psllw_i2r (1, mm2);
    psllw_i2r (1, mm5);
    paddw_r2r (mm0, mm2);
    paddw_r2r (mm3, mm5);
    paddw_r2r (mm1, mm2);
    paddw_r2r (mm4, mm5);
    psrlw_i2r (2, mm2);
    psrlw_i2r (2, mm5);
    packuswb_r2r (mm2, mm2);
    packuswb_r2r (mm5, mm5);

    movd_r2m (mm2, *out);
    movd_r2m (mm5, *(out + 4));
    out += 8;
    t1 += 8;
    b1 += 8;
    m0 += 8;
  }
  emms ();

  while (size--) {
    *out++ = (*t1++ + *b1++ + (*m0++ << 1)) >> 2;
  }
}

static void
deinterlace_scanline_linear_blend2_packed_mmx (GstDeinterlaceSimpleMethod *
    self, guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  deinterlace_scanline_linear_blend2_mmx (self, out, scanlines->m0,
      scanlines->t1, scanlines->b1, self->parent.row_stride[0]);
}

#endif

G_DEFINE_TYPE (GstDeinterlaceMethodLinearBlend,
    gst_deinterlace_method_linear_blend, GST_TYPE_DEINTERLACE_SIMPLE_METHOD);

static void
    gst_deinterlace_method_linear_blend_class_init
    (GstDeinterlaceMethodLinearBlendClass * klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GstDeinterlaceSimpleMethodClass *dism_class =
      (GstDeinterlaceSimpleMethodClass *) klass;
#ifdef BUILD_X86_ASM
  guint cpu_flags = oil_cpu_get_flags ();
#endif

  dim_class->fields_required = 2;
  dim_class->name = "Blur: Temporal";
  dim_class->nick = "linearblend";
  dim_class->latency = 0;

  dism_class->interpolate_scanline_yuy2 =
      deinterlace_scanline_linear_blend_packed_c;
  dism_class->interpolate_scanline_yvyu =
      deinterlace_scanline_linear_blend_packed_c;
  dism_class->copy_scanline_yuy2 = deinterlace_scanline_linear_blend2_packed_c;
  dism_class->copy_scanline_yvyu = deinterlace_scanline_linear_blend2_packed_c;

#ifdef BUILD_X86_ASM
  if (cpu_flags & OIL_IMPL_FLAG_MMX) {
    dism_class->interpolate_scanline_yuy2 =
        deinterlace_scanline_linear_blend_packed_mmx;
    dism_class->interpolate_scanline_yvyu =
        deinterlace_scanline_linear_blend_packed_mmx;
    dism_class->copy_scanline_yuy2 =
        deinterlace_scanline_linear_blend2_packed_mmx;
    dism_class->copy_scanline_yvyu =
        deinterlace_scanline_linear_blend2_packed_mmx;
  }
#endif
}

static void
gst_deinterlace_method_linear_blend_init (GstDeinterlaceMethodLinearBlend *
    self)
{
}
