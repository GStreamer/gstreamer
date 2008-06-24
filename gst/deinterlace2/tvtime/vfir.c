/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard.
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

/*
 * This file contains code from ffmpeg, see http://ffmpeg.org/ (LGPL)
 * and modifications by Billy Biggs.
 *
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#include <stdio.h>
#if defined (__SVR4) && defined (__sun)
# include <sys/int_types.h>
#else
# include <stdint.h>
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "speedy.h"
#include "gstdeinterlace2.h"

/*
 * The MPEG2 spec uses a slightly harsher filter, they specify
 * [-1 8 2 8 -1].  ffmpeg uses a similar filter but with more of
 * a tendancy to blur than to use the local information.  The
 * filter taps here are: [-1 4 2 4 -1].
 */

/**
  * C implementation.
  */
static inline void
deinterlace_line_c (uint8_t * dst, uint8_t * lum_m4,
    uint8_t * lum_m3, uint8_t * lum_m2,
    uint8_t * lum_m1, uint8_t * lum, int size)
{
  int sum;

  for (; size >= 0; size--) {
    sum = -lum_m4[0];
    sum += lum_m3[0] << 2;
    sum += lum_m2[0] << 1;
    sum += lum_m1[0] << 2;
    sum += -lum[0];
    dst[0] = (sum + 4) >> 3;    // This needs to be clipped at 0 and 255: cm[(sum + 4) >> 3];
    lum_m4++;
    lum_m3++;
    lum_m2++;
    lum_m1++;
    lum++;
    dst++;
  }
}

#ifdef HAVE_CPU_I386
#include "mmx.h"
static void
deinterlace_line_mmx (uint8_t * dst, uint8_t * lum_m4,
    uint8_t * lum_m3, uint8_t * lum_m2,
    uint8_t * lum_m1, uint8_t * lum, int size)
{
  mmx_t rounder;

  rounder.uw[0] = 4;
  rounder.uw[1] = 4;
  rounder.uw[2] = 4;
  rounder.uw[3] = 4;
  pxor_r2r (mm7, mm7);
  movq_m2r (rounder, mm6);

  for (; size > 3; size -= 4) {
    movd_m2r (*lum_m4, mm0);
    movd_m2r (*lum_m3, mm1);
    movd_m2r (*lum_m2, mm2);
    movd_m2r (*lum_m1, mm3);
    movd_m2r (*lum, mm4);
    punpcklbw_r2r (mm7, mm0);
    punpcklbw_r2r (mm7, mm1);
    punpcklbw_r2r (mm7, mm2);
    punpcklbw_r2r (mm7, mm3);
    punpcklbw_r2r (mm7, mm4);
    paddw_r2r (mm3, mm1);
    psllw_i2r (1, mm2);
    paddw_r2r (mm4, mm0);
    psllw_i2r (2, mm1);         // 2
    paddw_r2r (mm6, mm2);
    paddw_r2r (mm2, mm1);
    psubusw_r2r (mm0, mm1);
    psrlw_i2r (3, mm1);         // 3
    packuswb_r2r (mm7, mm1);
    movd_r2m (mm1, *dst);
    lum_m4 += 4;
    lum_m3 += 4;
    lum_m2 += 4;
    lum_m1 += 4;
    lum += 4;
    dst += 4;
  }
  emms ();

  /* Handle odd widths */
  if (size > 0)
    deinterlace_line_c (dst, lum_m4, lum_m3, lum_m2, lum_m1, lum, size);
}
#endif

/*
 * The commented-out method below that uses the bottom_field member is more
 * like the filter as specified in the MPEG2 spec, but it doesn't seem to
 * have the desired effect.
 */

static void
deinterlace_scanline_vfir (GstDeinterlace2 * object,
    deinterlace_scanline_data_t * data, uint8_t * output)
{
#ifdef HAVE_CPU_I386
  if (object->cpu_feature_flags & OIL_IMPL_FLAG_MMX) {
    deinterlace_line_mmx (output, data->tt1, data->t0, data->m1, data->b0,
        data->bb1, object->frame_width * 2);
  } else {
    deinterlace_line_c (output, data->tt1, data->t0, data->m1, data->b0,
        data->bb1, object->frame_width * 2);
  }
#else
  deinterlace_line_c (output, data->tt1, data->t0, data->m1, data->b0,
      data->bb1, object->frame_width * 2);
#endif
  // blit_packed422_scanline( output, data->m1, width );
}

static void
copy_scanline (GstDeinterlace2 * object,
    deinterlace_scanline_data_t * data, uint8_t * output)
{
  blit_packed422_scanline (output, data->m0, object->frame_width);
  /*
     if( data->bottom_field ) {
     deinterlace_line( output, data->tt2, data->t1, data->m2, data->b1, data->bb2, width*2 );
     } else {
     deinterlace_line( output, data->tt0, data->t1, data->m0, data->b1, data->bb0, width*2 );
     }
   */
}


static deinterlace_method_t vfirmethod = {
  0,                            //DEINTERLACE_PLUGIN_API_VERSION,
  "Blur: Vertical",
  "BlurVertical",
  2,
  0,
  0,
  0,
  0,
  1,
  deinterlace_scanline_vfir,
  copy_scanline,
  0,
  {"Avoids flicker by blurring consecutive frames",
        "of input.  Use this if you want to run your",
        "monitor at an arbitrary refresh rate and not",
        "use much CPU, and are willing to sacrifice",
        "detail.",
        "",
        "Vertical mode blurs favouring the most recent",
        "field for less visible trails.  From the",
        "deinterlacer filter in ffmpeg.",
      ""}
};

deinterlace_method_t *
dscaler_vfir_get_method (void)
{
  return &vfirmethod;
}
