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

static void
deinterlace_frame_vfir (GstDeinterlace2 * object)
{
  void (*func) (uint8_t * dst, uint8_t * lum_m4,
      uint8_t * lum_m3, uint8_t * lum_m2,
      uint8_t * lum_m1, uint8_t * lum, int size);
  gint line = 0;
  uint8_t *cur_field, *last_field;
  uint8_t *t0, *b0, *tt1, *m1, *bb1, *out_data;

#ifdef HAVE_CPU_I386
  if (object->cpu_feature_flags & OIL_IMPL_FLAG_MMX) {
    func = deinterlace_line_mmx;
  } else {
    func = deinterlace_line_c;
  }
#else
  func = deinterlace_line_c;
#endif

  cur_field =
      GST_BUFFER_DATA (object->field_history[object->history_count - 2].buf);
  last_field =
      GST_BUFFER_DATA (object->field_history[object->history_count - 1].buf);

  out_data = GST_BUFFER_DATA (object->out_buf);

  if (object->field_history[object->history_count - 2].flags ==
      PICTURE_INTERLACED_BOTTOM) {
    blit_packed422_scanline (out_data, cur_field, object->frame_width);
    out_data += object->output_stride;
  }

  blit_packed422_scanline (out_data, cur_field, object->frame_width);
  out_data += object->output_stride;
  line++;

  for (; line < object->field_height; line++) {
    t0 = cur_field;
    b0 = cur_field + object->field_stride;

    tt1 = last_field;
    m1 = last_field + object->field_stride;
    bb1 = last_field + (object->field_stride * 2);

    /* set valid data for corner cases */
    if (line == 1) {
      tt1 = bb1;
    } else if (line == object->field_height - 1) {
      bb1 = tt1;
    }

    func (out_data, tt1, t0, m1, b0, bb1, object->line_length);
    out_data += object->output_stride;
    cur_field += object->field_stride;
    last_field += object->field_stride;

    blit_packed422_scanline (out_data, cur_field, object->frame_width);
    out_data += object->output_stride;
  }

  if (object->field_history[object->history_count - 2].flags ==
      PICTURE_INTERLACED_TOP) {
    /* double the last scanline of the top field */
    blit_packed422_scanline (out_data, cur_field, object->frame_width);
  }
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
  0,
  0,
  deinterlace_frame_vfir,
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
