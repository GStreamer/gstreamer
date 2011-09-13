/*
 * Misc image conversion routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file imgconvert.c
 * Misc image conversion routines.
 */

/* TODO:
 * - write 'ffimg' program to test all the image related stuff
 * - move all api to slice based system
 * - integrate deinterlacing, postprocessing and scaling in the conversion process
 */

#include "avcodec.h"
#include "dsputil.h"
#include "gstffmpegcodecmap.h"

#include <string.h>
#include <stdlib.h>

GST_DEBUG_CATEGORY_EXTERN (ffmpegcolorspace_performance);

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)

/* this table gives more information about formats */
/* FIXME, this table is also in ffmpegcodecmap */
static PixFmtInfo pix_fmt_info[PIX_FMT_NB] = {
  /* YUV formats */
  /* [PIX_FMT_YUV420P] = */ {
        /* .format         = */ PIX_FMT_YUV420P,
        /* .name           = */ "yuv420p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 1,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YVU420P] = */ {
        /* .format         = */ PIX_FMT_YVU420P,
        /* .name           = */ "yvu420p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 1,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_NV12] = */ {
        /* .format         = */ PIX_FMT_NV12,
        /* .name           = */ "nv12",
        /* .nb_channels    = */ 2,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 1,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_NV21] = */ {
        /* .format         = */ PIX_FMT_NV21,
        /* .name           = */ "nv21",
        /* .nb_channels    = */ 2,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 1,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YUV422P] = */ {
        /* .format         = */ PIX_FMT_YUV422P,
        /* .name           = */ "yuv422p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YUV444P] = */ {
        /* .format         = */ PIX_FMT_YUV444P,
        /* .name           = */ "yuv444p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YUV422] = */ {
        /* .format         = */ PIX_FMT_YUV422,
        /* .name           = */ "yuv422",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_UYVY422] = */ {
        /* .format         = */ PIX_FMT_UYVY422,
        /* .name           = */ "uyvy422",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YVYU422] = */ {
        /* .format         = */ PIX_FMT_YVYU422,
        /* .name           = */ "yvyu422",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_V308] = */ {
        /* .format         = */ PIX_FMT_V308,
        /* .name           = */ "v308",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YUV410P] = */ {
        /* .format         = */ PIX_FMT_YUV410P,
        /* .name           = */ "yuv410p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 2,
        /* .y_chroma_shift = */ 2,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YVU410P] = */ {
        /* .format         = */ PIX_FMT_YVU410P,
        /* .name           = */ "yvu410p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 2,
        /* .y_chroma_shift = */ 2,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YUV411P] = */ {
        /* .format         = */ PIX_FMT_YUV411P,
        /* .name           = */ "yuv411p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 2,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_Y800] = */ {
        /* .format         = */ PIX_FMT_Y800,
        /* .name           = */ "y800",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_Y16] = */ {
        /* .format         = */ PIX_FMT_Y16,
        /* .name           = */ "y16",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 16,
      },

  /* JPEG YUV */
  /* [PIX_FMT_YUVJ420P] = */ {
        /* .format         = */ PIX_FMT_YUVJ420P,
        /* .name           = */ "yuvj420p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV_JPEG,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 1,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YUVJ422P] = */ {
        /* .format         = */ PIX_FMT_YUVJ422P,
        /* .name           = */ "yuvj422p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV_JPEG,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_YUVJ444P] = */ {
        /* .format         = */ PIX_FMT_YUVJ444P,
        /* .name           = */ "yuvj444p",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_YUV_JPEG,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },

  /* RGB formats */
  /* [PIX_FMT_RGB24] = */ {
        /* .format         = */ PIX_FMT_RGB24,
        /* .name           = */ "rgb24",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_BGR24] = */ {
        /* .format         = */ PIX_FMT_BGR24,
        /* .name           = */ "bgr24",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_RGB32] = */ {
        /* .format         = */ PIX_FMT_RGB32,
        /* .name           = */ "rgb32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_BGR32] = */ {
        /* .format         = */ PIX_FMT_BGR32,
        /* .name           = */ "bgr32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_RGB32] = */ {
        /* .format         = */ PIX_FMT_xRGB32,
        /* .name           = */ "xrgb32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_BGR32] = */ {
        /* .format         = */ PIX_FMT_BGRx32,
        /* .name           = */ "bgrx32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_RGBA32] = */ {
        /* .format         = */ PIX_FMT_RGBA32,
        /* .name           = */ "rgba32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_BGRA32] = */ {
        /* .format         = */ PIX_FMT_BGRA32,
        /* .name           = */ "bgra32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_ARGB32] = */ {
        /* .format         = */ PIX_FMT_ARGB32,
        /* .name           = */ "argb32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_ABGR32] = */ {
        /* .format         = */ PIX_FMT_ABGR32,
        /* .name           = */ "abgr32",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_RGB565] = */ {
        /* .format         = */ PIX_FMT_RGB565,
        /* .name           = */ "rgb565",
        /* .nb_channels    = */ 3,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 5,
      },
  /* [PIX_FMT_RGB555] = */ {
        /* .format         = */ PIX_FMT_RGB555,
        /* .name           = */ "rgb555",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 5,
      },

  /* gray / mono formats */
  /* [PIX_FMT_GRAY8] = */ {
        /* .format         = */ PIX_FMT_GRAY8,
        /* .name           = */ "gray",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_GRAY,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_GRAY16_L] = */ {
        /* .format         = */ PIX_FMT_GRAY16_L,
        /* .name           = */ "gray",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_GRAY,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 16,
      },
  /* [PIX_FMT_GRAY16_B] = */ {
        /* .format         = */ PIX_FMT_GRAY16_B,
        /* .name           = */ "gray",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_GRAY,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 16,
      },
  /* [PIX_FMT_MONOWHITE] = */ {
        /* .format         = */ PIX_FMT_MONOWHITE,
        /* .name           = */ "monow",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_GRAY,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 1,
      },
  /* [PIX_FMT_MONOBLACK] = */ {
        /* .format         = */ PIX_FMT_MONOBLACK,
        /* .name           = */ "monob",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_GRAY,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 1,
      },

  /* paletted formats */
  /* [PIX_FMT_PAL8] = */ {
        /* .format         = */ PIX_FMT_PAL8,
        /* .name           = */ "pal8",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_RGB,
        /* .pixel_type     = */ FF_PIXEL_PALETTE,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_XVMC_MPEG2_MC] = */ {
        /* .format         = */ PIX_FMT_XVMC_MPEG2_MC,
        /* .name           = */ "xvmcmc",
      },
  /* [PIX_FMT_XVMC_MPEG2_IDCT] = */ {
        /* .format         = */ PIX_FMT_XVMC_MPEG2_IDCT,
        /* .name           = */ "xvmcidct",
      },
  /* [PIX_FMT_UYVY411] = */ {
        /* .format         = */ PIX_FMT_UYVY411,
        /* .name           = */ "uyvy411",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 0,
        /* .x_chroma_shift = */ 2,
        /* .y_chroma_shift = */ 0,
        /* .depth          = */ 8,
      },
  /* [PIX_FMT_AYUV4444] = */ {
        /* .format         = */ PIX_FMT_AYUV4444,
        /* .name           = */ "ayuv4444",
        /* .nb_channels    = */ 1,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PACKED,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 0,
        /* .y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /* [PIX_FMT_YUVA420P] = */ {
        /* .format         = */ PIX_FMT_YUVA420P,
        /* .name           = */ "yuva420p",
        /* .nb_channels    = */ 4,
        /* .color_type     = */ FF_COLOR_YUV,
        /* .pixel_type     = */ FF_PIXEL_PLANAR,
        /* .is_alpha       = */ 1,
        /* .x_chroma_shift = */ 1,
        /* .y_chroma_shift = */ 1,
        /* .depth          = */ 8,
      }
};

/* returns NULL if not found */
/* undid static since this is also used in gstffmpegcodecmap.c */
PixFmtInfo *
get_pix_fmt_info (enum PixelFormat format)
{
  int i;

  for (i = 0; i < sizeof (pix_fmt_info) / sizeof (pix_fmt_info[0]); i++) {
    if (pix_fmt_info[i].format == format) {
      return pix_fmt_info + i;
    }
  }

  /* since this doesn't get checked *anywhere*, we might as well warn
     here if we return NULL so you have *some* idea what's going on */
  g_warning
      ("Could not find info for pixel format %d out of %d known pixel formats. One segfault coming up",
      format, PIX_FMT_NB);
  return NULL;
}

void
avcodec_get_chroma_sub_sample (int pix_fmt, int *h_shift, int *v_shift)
{
  *h_shift = get_pix_fmt_info (pix_fmt)->x_chroma_shift;
  *v_shift = get_pix_fmt_info (pix_fmt)->y_chroma_shift;
}

const char *
avcodec_get_pix_fmt_name (int pix_fmt)
{
  if (pix_fmt < 0 || pix_fmt >= PIX_FMT_NB)
    return "???";
  else
    return get_pix_fmt_info (pix_fmt)->name;
}

enum PixelFormat
avcodec_get_pix_fmt (const char *name)
{
  int i;

  for (i = 0; i < PIX_FMT_NB; i++)
    if (!strcmp (pix_fmt_info[i].name, name))
      break;
  return pix_fmt_info[i].format;
}

#if 0
static int
avpicture_layout (const AVPicture * src, int pix_fmt, int width, int height,
    unsigned char *dest, int dest_size)
{
  PixFmtInfo *pf = get_pix_fmt_info (pix_fmt);
  int i, j, w, h, data_planes;
  const unsigned char *s;
  int size = avpicture_get_size (pix_fmt, width, height);

  if (size > dest_size)
    return -1;

  if (pf->pixel_type == FF_PIXEL_PACKED || pf->pixel_type == FF_PIXEL_PALETTE) {
    if (pix_fmt == PIX_FMT_YUV422 ||
        pix_fmt == PIX_FMT_UYVY422 ||
        pix_fmt == PIX_FMT_RGB565 || pix_fmt == PIX_FMT_RGB555)
      w = width * 2;
    else if (pix_fmt == PIX_FMT_UYVY411)
      w = width + width / 2;
    else if (pix_fmt == PIX_FMT_PAL8)
      w = width;
    else
      w = width * (pf->depth * pf->nb_channels / 8);

    data_planes = 1;
    h = height;
  } else {
    data_planes = pf->nb_channels;
    w = (width * pf->depth + 7) / 8;
    h = height;
  }

  for (i = 0; i < data_planes; i++) {
    if (i == 1) {
      w = width >> pf->x_chroma_shift;
      h = height >> pf->y_chroma_shift;
    }
    s = src->data[i];
    for (j = 0; j < h; j++) {
      memcpy (dest, s, w);
      dest += w;
      s += src->linesize[i];
    }
  }

  if (pf->pixel_type == FF_PIXEL_PALETTE)
    memcpy ((unsigned char *) (((size_t) dest + 3) & ~3), src->data[1],
        256 * 4);

  return size;
}
#endif

int
avpicture_get_size (int pix_fmt, int width, int height)
{
  AVPicture dummy_pict;

  return gst_ffmpegcsp_avpicture_fill (&dummy_pict, NULL, pix_fmt, width,
      height, FALSE);
}

/**
 * compute the loss when converting from a pixel format to another 
 */
int
avcodec_get_pix_fmt_loss (int dst_pix_fmt, int src_pix_fmt, int has_alpha)
{
  const PixFmtInfo *pf, *ps;
  int loss;

  ps = get_pix_fmt_info (src_pix_fmt);
  pf = get_pix_fmt_info (dst_pix_fmt);

  /* compute loss */
  loss = 0;
  if (pf->depth < ps->depth ||
      (dst_pix_fmt == PIX_FMT_RGB555 && src_pix_fmt == PIX_FMT_RGB565))
    loss |= FF_LOSS_DEPTH;
  if (pf->x_chroma_shift > ps->x_chroma_shift ||
      pf->y_chroma_shift > ps->y_chroma_shift)
    loss |= FF_LOSS_RESOLUTION;
  switch (pf->color_type) {
    case FF_COLOR_RGB:
      if (ps->color_type != FF_COLOR_RGB && ps->color_type != FF_COLOR_GRAY)
        loss |= FF_LOSS_COLORSPACE;
      break;
    case FF_COLOR_GRAY:
      if (ps->color_type != FF_COLOR_GRAY)
        loss |= FF_LOSS_COLORSPACE;
      break;
    case FF_COLOR_YUV:
      if (ps->color_type != FF_COLOR_YUV)
        loss |= FF_LOSS_COLORSPACE;
      break;
    case FF_COLOR_YUV_JPEG:
      if (ps->color_type != FF_COLOR_YUV_JPEG &&
          ps->color_type != FF_COLOR_YUV && ps->color_type != FF_COLOR_GRAY)
        loss |= FF_LOSS_COLORSPACE;
      break;
    default:
      /* fail safe test */
      if (ps->color_type != pf->color_type)
        loss |= FF_LOSS_COLORSPACE;
      break;
  }
  if (pf->color_type == FF_COLOR_GRAY && ps->color_type != FF_COLOR_GRAY)
    loss |= FF_LOSS_CHROMA;
  if (!pf->is_alpha && (ps->is_alpha && has_alpha))
    loss |= FF_LOSS_ALPHA;
  if (pf->pixel_type == FF_PIXEL_PALETTE &&
      (ps->pixel_type != FF_PIXEL_PALETTE && ps->color_type != FF_COLOR_GRAY))
    loss |= FF_LOSS_COLORQUANT;
  return loss;
}

static int
avg_bits_per_pixel (int pix_fmt)
{
  int bits;
  const PixFmtInfo *pf;

  pf = get_pix_fmt_info (pix_fmt);
  switch (pf->pixel_type) {
    case FF_PIXEL_PACKED:
      switch (pix_fmt) {
        case PIX_FMT_YUV422:
        case PIX_FMT_UYVY422:
        case PIX_FMT_YVYU422:
        case PIX_FMT_RGB565:
        case PIX_FMT_RGB555:
          bits = 16;
          break;
        case PIX_FMT_UYVY411:
          bits = 12;
          break;
        default:
          bits = pf->depth * pf->nb_channels;
          break;
      }
      break;
    case FF_PIXEL_PLANAR:
      if (pf->x_chroma_shift == 0 && pf->y_chroma_shift == 0) {
        bits = pf->depth * pf->nb_channels;
      } else {
        bits = pf->depth + ((2 * pf->depth) >>
            (pf->x_chroma_shift + pf->y_chroma_shift));
      }
      break;
    case FF_PIXEL_PALETTE:
      bits = 8;
      break;
    default:
      bits = -1;
      break;
  }
  return bits;
}

static int
avcodec_find_best_pix_fmt1 (int pix_fmt_mask,
    int src_pix_fmt, int has_alpha, int loss_mask)
{
  int dist, i, loss, min_dist, dst_pix_fmt;

  /* find exact color match with smallest size */
  dst_pix_fmt = -1;
  min_dist = 0x7fffffff;
  for (i = 0; i < PIX_FMT_NB; i++) {
    if (pix_fmt_mask & (1 << i)) {
      loss = avcodec_get_pix_fmt_loss (i, src_pix_fmt, has_alpha) & loss_mask;
      if (loss == 0) {
        dist = avg_bits_per_pixel (i);
        if (dist < min_dist) {
          min_dist = dist;
          dst_pix_fmt = i;
        }
      }
    }
  }
  return dst_pix_fmt;
}

/** 
 * find best pixel format to convert to. Return -1 if none found 
 */
int
avcodec_find_best_pix_fmt (int pix_fmt_mask, int src_pix_fmt,
    int has_alpha, int *loss_ptr)
{
  int dst_pix_fmt, loss_mask, i;
  static const int loss_mask_order[] = {
    ~0,                         /* no loss first */
    ~FF_LOSS_ALPHA,
    ~FF_LOSS_RESOLUTION,
    ~(FF_LOSS_COLORSPACE | FF_LOSS_RESOLUTION),
    ~FF_LOSS_COLORQUANT,
    ~FF_LOSS_DEPTH,
    0,
  };

  /* try with successive loss */
  i = 0;
  for (;;) {
    loss_mask = loss_mask_order[i++];
    dst_pix_fmt = avcodec_find_best_pix_fmt1 (pix_fmt_mask, src_pix_fmt,
        has_alpha, loss_mask);
    if (dst_pix_fmt >= 0)
      goto found;
    if (loss_mask == 0)
      break;
  }
  return -1;
found:
  if (loss_ptr)
    *loss_ptr = avcodec_get_pix_fmt_loss (dst_pix_fmt, src_pix_fmt, has_alpha);
  return dst_pix_fmt;
}

static void
img_copy_plane (uint8_t * dst, int dst_wrap,
    const uint8_t * src, int src_wrap, int width, int height)
{
  for (; height > 0; height--) {
    memcpy (dst, src, width);
    dst += dst_wrap;
    src += src_wrap;
  }
}

/**
 * Copy image 'src' to 'dst'.
 */
static void
img_copy (AVPicture * dst, const AVPicture * src,
    int pix_fmt, int width, int height)
{
  int bwidth, bits, i;
  const PixFmtInfo *pf;

  pf = get_pix_fmt_info (pix_fmt);
  switch (pf->pixel_type) {
    case FF_PIXEL_PACKED:
      switch (pix_fmt) {
        case PIX_FMT_YUV422:
        case PIX_FMT_UYVY422:
        case PIX_FMT_YVYU422:
        case PIX_FMT_RGB565:
        case PIX_FMT_RGB555:
          bits = 16;
          break;
        case PIX_FMT_UYVY411:
          bits = 12;
          break;
        default:
          bits = pf->depth * pf->nb_channels;
          break;
      }
      bwidth = (width * bits + 7) >> 3;
      img_copy_plane (dst->data[0], dst->linesize[0],
          src->data[0], src->linesize[0], bwidth, height);
      break;
    case FF_PIXEL_PLANAR:
      for (i = 0; i < pf->nb_channels; i++) {
        int w, h;

        w = width;
        h = height;
        if (i == 1 || i == 2) {
          w >>= pf->x_chroma_shift;
          h >>= pf->y_chroma_shift;
        }
        bwidth = (w * pf->depth + 7) >> 3;
        img_copy_plane (dst->data[i], dst->linesize[i],
            src->data[i], src->linesize[i], bwidth, h);
      }
      break;
    case FF_PIXEL_PALETTE:
      img_copy_plane (dst->data[0], dst->linesize[0],
          src->data[0], src->linesize[0], width, height);
      /* copy the palette */
      img_copy_plane (dst->data[1], dst->linesize[1],
          src->data[1], src->linesize[1], 4, 256);
      break;
  }
}

/* XXX: totally non optimized */

static void
yuv422_to_yuv420p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = src->data[0];
  lum1 = dst->data[0];
  cb1 = dst->data[1];
  cr1 = dst->data[2];

  for (; height >= 1; height -= 2) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[0];
      cb[0] = p[1];
      lum[1] = p[2];
      cr[0] = p[3];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      lum[0] = p[0];
      cb[0] = p[1];
      cr[0] = p[3];
    }
    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
    if (height > 1) {
      p = p1;
      lum = lum1;
      for (w = width; w >= 2; w -= 2) {
        lum[0] = p[0];
        lum[1] = p[2];
        p += 4;
        lum += 2;
      }
      if (w) {
        lum[0] = p[0];
      }
      p1 += src->linesize[0];
      lum1 += dst->linesize[0];
    }
    cb1 += dst->linesize[1];
    cr1 += dst->linesize[2];
  }
}

static void
uyvy422_to_gray (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *lum1;
  int w;

  p1 = src->data[0];
  lum1 = dst->data[0];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;

    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[1];
      lum[1] = p[3];
      p += 4;
      lum += 2;
    }

    if (w)
      lum[0] = p[1];

    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
  }
}


static void
uyvy422_to_yuv420p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = src->data[0];

  lum1 = dst->data[0];
  cb1 = dst->data[1];
  cr1 = dst->data[2];

  for (; height >= 1; height -= 2) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[1];
      cb[0] = p[0];
      lum[1] = p[3];
      cr[0] = p[2];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      lum[0] = p[1];
      cb[0] = p[0];
      cr[0] = p[2];
    }
    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
    if (height > 1) {
      p = p1;
      lum = lum1;
      for (w = width; w >= 2; w -= 2) {
        lum[0] = p[1];
        lum[1] = p[3];
        p += 4;
        lum += 2;
      }
      if (w) {
        lum[0] = p[1];
      }
      p1 += src->linesize[0];
      lum1 += dst->linesize[0];
    }
    cb1 += dst->linesize[1];
    cr1 += dst->linesize[2];
  }
}


static void
uyvy422_to_yuv422p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = src->data[0];
  lum1 = dst->data[0];
  cb1 = dst->data[1];
  cr1 = dst->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[1];
      cb[0] = p[0];
      lum[1] = p[3];
      cr[0] = p[2];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      lum[0] = p[1];
      cb[0] = p[0];
      cr[0] = p[2];
    }
    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
    cb1 += dst->linesize[1];
    cr1 += dst->linesize[2];
  }
}

static void
yvyu422_to_gray (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *lum1;
  int w;

  p1 = src->data[0];
  lum1 = dst->data[0];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;

    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[0];
      lum[1] = p[2];
      p += 4;
      lum += 2;
    }

    if (w)
      lum[0] = p[0];

    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
  }
}

static void
yvyu422_to_yuv420p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = src->data[0];

  lum1 = dst->data[0];
  cb1 = dst->data[1];
  cr1 = dst->data[2];

  for (; height >= 1; height -= 2) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[0];
      cb[0] = p[3];
      lum[1] = p[2];
      cr[0] = p[1];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      lum[0] = p[0];
      cb[0] = p[3];
      cr[0] = p[1];
    }
    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
    if (height > 1) {
      p = p1;
      lum = lum1;
      for (w = width; w >= 2; w -= 2) {
        lum[0] = p[0];
        lum[1] = p[2];
        p += 4;
        lum += 2;
      }
      if (w) {
        lum[0] = p[0];
      }
      p1 += src->linesize[0];
      lum1 += dst->linesize[0];
    }
    cb1 += dst->linesize[1];
    cr1 += dst->linesize[2];
  }
}

static void
yvyu422_to_yuv422p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = src->data[0];
  lum1 = dst->data[0];
  cb1 = dst->data[1];
  cr1 = dst->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[0];
      cb[0] = p[3];
      lum[1] = p[2];
      cr[0] = p[1];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      lum[0] = p[0];
      cb[0] = p[3];
      cr[0] = p[1];
    }
    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
    cb1 += dst->linesize[1];
    cr1 += dst->linesize[2];
  }
}

static void
yuv422_to_yuv422p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = src->data[0];
  lum1 = dst->data[0];
  cb1 = dst->data[1];
  cr1 = dst->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      lum[0] = p[0];
      cb[0] = p[1];
      lum[1] = p[2];
      cr[0] = p[3];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      lum[0] = p[0];
      cb[0] = p[1];
      cr[0] = p[3];
    }
    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
    cb1 += dst->linesize[1];
    cr1 += dst->linesize[2];
  }
}

static void
yuv422p_to_yuv422 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *p, *p1;
  const uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = dst->data[0];
  lum1 = src->data[0];
  cb1 = src->data[1];
  cr1 = src->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      p[0] = lum[0];
      p[1] = cb[0];
      p[2] = lum[1];
      p[3] = cr[0];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      p[0] = lum[0];
      p[1] = cb[0];
      p[3] = cr[0];
    }
    p1 += dst->linesize[0];
    lum1 += src->linesize[0];
    cb1 += src->linesize[1];
    cr1 += src->linesize[2];
  }
}

static void
yuv422p_to_uyvy422 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *p, *p1;
  const uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = dst->data[0];
  lum1 = src->data[0];
  cb1 = src->data[1];
  cr1 = src->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      p[1] = lum[0];
      p[0] = cb[0];
      p[3] = lum[1];
      p[2] = cr[0];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      p[1] = lum[0];
      p[0] = cb[0];
      p[2] = cr[0];
    }
    p1 += dst->linesize[0];
    lum1 += src->linesize[0];
    cb1 += src->linesize[1];
    cr1 += src->linesize[2];
  }
}

static void
yuv422p_to_yvyu422 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *p, *p1;
  const uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = dst->data[0];
  lum1 = src->data[0];
  cb1 = src->data[1];
  cr1 = src->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 2; w -= 2) {
      p[0] = lum[0];
      p[3] = cb[0];
      p[2] = lum[1];
      p[1] = cr[0];
      p += 4;
      lum += 2;
      cb++;
      cr++;
    }
    if (w) {
      p[0] = lum[0];
      p[3] = cb[0];
      p[1] = cr[0];
    }
    p1 += dst->linesize[0];
    lum1 += src->linesize[0];
    cb1 += src->linesize[1];
    cr1 += src->linesize[2];
  }
}

static void
uyvy411_to_yuv411p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *p, *p1;
  uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = src->data[0];
  lum1 = dst->data[0];
  cb1 = dst->data[1];
  cr1 = dst->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 4; w -= 4) {
      cb[0] = p[0];
      lum[0] = p[1];
      lum[1] = p[2];
      cr[0] = p[3];
      lum[2] = p[4];
      lum[3] = p[5];
      p += 6;
      lum += 4;
      cb++;
      cr++;
    }
    p1 += src->linesize[0];
    lum1 += dst->linesize[0];
    cb1 += dst->linesize[1];
    cr1 += dst->linesize[2];
  }
}

static void
yuv411p_to_uyvy411 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *p, *p1;
  const uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
  int w;

  p1 = dst->data[0];
  lum1 = src->data[0];
  cb1 = src->data[1];
  cr1 = src->data[2];
  for (; height > 0; height--) {
    p = p1;
    lum = lum1;
    cb = cb1;
    cr = cr1;
    for (w = width; w >= 4; w -= 4) {
      p[0] = cb[0];
      p[1] = lum[0];
      p[2] = lum[1];
      p[3] = cr[0];
      p[4] = lum[2];
      p[5] = lum[3];
      p += 6;
      lum += 4;
      cb++;
      cr++;
    }
    p1 += dst->linesize[0];
    lum1 += src->linesize[0];
    cb1 += src->linesize[1];
    cr1 += src->linesize[2];
  }
}

static void
yuv420p_to_yuv422 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int w, h;
  uint8_t *line1, *line2, *linesrc = dst->data[0];
  uint8_t *lum1, *lum2, *lumsrc = src->data[0];
  uint8_t *cb1, *cb2 = src->data[1];
  uint8_t *cr1, *cr2 = src->data[2];

  for (h = height / 2; h--;) {
    line1 = linesrc;
    line2 = linesrc + dst->linesize[0];

    lum1 = lumsrc;
    lum2 = lumsrc + src->linesize[0];

    cb1 = cb2;
    cr1 = cr2;

    for (w = width / 2; w--;) {
      *line1++ = *lum1++;
      *line2++ = *lum2++;
      *line1++ = *line2++ = *cb1++;
      *line1++ = *lum1++;
      *line2++ = *lum2++;
      *line1++ = *line2++ = *cr1++;
    }
    /* odd width */
    if (width % 2 != 0) {
      *line1++ = *lum1++;
      *line2++ = *lum2++;
      *line1++ = *line2++ = *cb1++;
    }

    linesrc += dst->linesize[0] * 2;
    lumsrc += src->linesize[0] * 2;
    cb2 += src->linesize[1];
    cr2 += src->linesize[2];
  }
  /* odd height */
  if (height % 2 != 0) {
    line1 = linesrc;
    lum1 = lumsrc;
    cb1 = cb2;
    cr1 = cr2;

    for (w = width / 2; w--;) {
      *line1++ = *lum1++;
      *line1++ = *cb1++;
      *line1++ = *lum1++;
      *line1++ = *cr1++;
    }
    /* odd width */
    if (width % 2 != 0) {
      *line1++ = *lum1++;
      *line1++ = *cb1++;
    }
  }
}

static void
nv12_to_nv21 (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const uint8_t *s_c_ptr;
  uint8_t *d_c_ptr;
  int w, c_wrap;

  memcpy (dst->data[0], src->data[0], src->linesize[0] * height);

  s_c_ptr = src->data[1];
  d_c_ptr = dst->data[1];
  c_wrap = src->linesize[1] - ((width + 1) & ~0x01);

  for (; height >= 2; height -= 2) {
    for (w = width; w >= 2; w -= 2) {
      d_c_ptr[0] = s_c_ptr[1];
      d_c_ptr[1] = s_c_ptr[0];
      s_c_ptr += 2;
      d_c_ptr += 2;
    }

    /* handle odd width */
    if (w) {
      d_c_ptr[0] = s_c_ptr[1];
      d_c_ptr[1] = s_c_ptr[0];
      s_c_ptr += 2;
      d_c_ptr += 2;
    }
    s_c_ptr += c_wrap;
    d_c_ptr += c_wrap;
  }

  /* handle odd height */
  if (height) {
    for (w = width; w >= 2; w -= 2) {
      d_c_ptr[0] = s_c_ptr[1];
      d_c_ptr[1] = s_c_ptr[0];
      s_c_ptr += 2;
      d_c_ptr += 2;
    }

    /* handle odd width */
    if (w) {
      d_c_ptr[0] = s_c_ptr[1];
      d_c_ptr[1] = s_c_ptr[0];
      s_c_ptr += 2;
      d_c_ptr += 2;
    }
  }
}

static void
nv12_to_yuv444p (AVPicture * dst, const AVPicture * src, int width, int height)
{
  int w, h;
  uint8_t *dst_lum1, *dst_lum2, *dst_line = dst->data[0];
  uint8_t *dst_cb1, *dst_cb2, *dst_cb_line = dst->data[1];
  uint8_t *dst_cr1, *dst_cr2, *dst_cr_line = dst->data[2];
  uint8_t *lum1, *lum2, *src_lum_line = src->data[0];
  uint8_t *src_c1, *src_c_line = src->data[1];
  uint8_t cb, cr;

  for (h = height / 2; h--;) {
    dst_lum1 = dst_line;
    dst_lum2 = dst_line + dst->linesize[0];

    dst_cb1 = dst_cb_line;
    dst_cb2 = dst_cb_line + dst->linesize[1];
    dst_cr1 = dst_cr_line;
    dst_cr2 = dst_cr_line + dst->linesize[2];

    lum1 = src_lum_line;
    lum2 = src_lum_line + src->linesize[0];

    src_c1 = src_c_line;

    for (w = width / 2; w--;) {
      cb = *src_c1++;
      cr = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_lum2++ = *lum2++;
      *dst_cb1++ = *dst_cb2++ = cb;
      *dst_cr1++ = *dst_cr2++ = cr;
      *dst_lum1++ = *lum1++;
      *dst_lum2++ = *lum2++;
      *dst_cb1++ = *dst_cb2++ = cb;
      *dst_cr1++ = *dst_cr2++ = cr;
    }
    /* odd width */
    if (width % 2 != 0) {
      cb = *src_c1++;
      cr = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_lum2++ = *lum2++;
      *dst_cb1++ = *dst_cb2++ = *src_c1++;
      *dst_cr1++ = *dst_cr2++ = *src_c1++;
    }

    dst_line += dst->linesize[0] * 2;
    dst_cb_line += dst->linesize[1] * 2;
    dst_cr_line += dst->linesize[2] * 2;
    src_lum_line += src->linesize[0] * 2;
    src_c_line += src->linesize[1];
  }

  /* odd height */
  if (height % 2 != 0) {
    dst_lum1 = dst_line;
    lum1 = src_lum_line;
    src_c1 = src_c_line;
    dst_cb1 = dst_cb_line;
    dst_cr1 = dst_cr_line;

    for (w = width / 2; w--;) {
      cb = *src_c1++;
      cr = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_cb1++ = cb;
      *dst_cr1++ = cr;
      *dst_lum1++ = *lum1++;
      *dst_cb1++ = cb;
      *dst_cr1++ = cr;
    }
    /* odd width */
    if (width % 2 != 0) {
      cb = *src_c1++;
      cr = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_cb1++ = cb;
      *dst_cr1++ = cr;
    }
  }
}

#define nv21_to_nv12 nv12_to_nv21

static void
nv21_to_yuv444p (AVPicture * dst, const AVPicture * src, int width, int height)
{
  int w, h;
  uint8_t *dst_lum1, *dst_lum2, *dst_line = dst->data[0];
  uint8_t *dst_cb1, *dst_cb2, *dst_cb_line = dst->data[1];
  uint8_t *dst_cr1, *dst_cr2, *dst_cr_line = dst->data[2];
  uint8_t *lum1, *lum2, *src_lum_line = src->data[0];
  uint8_t *src_c1, *src_c_line = src->data[1];
  uint8_t cb, cr;

  for (h = height / 2; h--;) {
    dst_lum1 = dst_line;
    dst_lum2 = dst_line + dst->linesize[0];

    dst_cb1 = dst_cb_line;
    dst_cb2 = dst_cb_line + dst->linesize[1];
    dst_cr1 = dst_cr_line;
    dst_cr2 = dst_cr_line + dst->linesize[2];

    lum1 = src_lum_line;
    lum2 = src_lum_line + src->linesize[0];

    src_c1 = src_c_line;

    for (w = width / 2; w--;) {
      cr = *src_c1++;
      cb = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_lum2++ = *lum2++;
      *dst_cb1++ = *dst_cb2++ = cb;
      *dst_cr1++ = *dst_cr2++ = cr;
      *dst_lum1++ = *lum1++;
      *dst_lum2++ = *lum2++;
      *dst_cb1++ = *dst_cb2++ = cb;
      *dst_cr1++ = *dst_cr2++ = cr;
    }
    /* odd width */
    if (width % 2 != 0) {
      cr = *src_c1++;
      cb = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_lum2++ = *lum2++;
      *dst_cb1++ = *dst_cb2++ = *src_c1++;
      *dst_cr1++ = *dst_cr2++ = *src_c1++;
    }

    dst_line += dst->linesize[0] * 2;
    dst_cb_line += dst->linesize[1] * 2;
    dst_cr_line += dst->linesize[2] * 2;
    src_lum_line += src->linesize[0] * 2;
    src_c_line += src->linesize[1];
  }

  /* odd height */
  if (height % 2 != 0) {
    dst_lum1 = dst_line;
    lum1 = src_lum_line;
    src_c1 = src_c_line;

    dst_cb1 = dst_cb_line;
    dst_cr1 = dst_cr_line;

    for (w = width / 2; w--;) {
      cr = *src_c1++;
      cb = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_cb1++ = cb;
      *dst_cr1++ = cr;
      *dst_lum1++ = *lum1++;
      *dst_cb1++ = cb;
      *dst_cr1++ = cr;
    }
    /* odd width */
    if (width % 2 != 0) {
      cr = *src_c1++;
      cb = *src_c1++;
      *dst_lum1++ = *lum1++;
      *dst_cb1++ = cb;
      *dst_cr1++ = cr;
    }
  }
}

static void
yuva420p_to_yuv420p (AVPicture * dst, const AVPicture * src, int width,
    int height)
{
  memcpy (dst->data[0], src->data[0], dst->linesize[0] * height);
  memcpy (dst->data[1], src->data[1], dst->linesize[1] * ((height + 1) / 2));
  memcpy (dst->data[2], src->data[2], dst->linesize[2] * ((height + 1) / 2));
}

static void
yuva420p_to_yuv422 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int w, h;
  uint8_t *line1, *line2, *linesrc = dst->data[0];
  uint8_t *lum1, *lum2, *lumsrc = src->data[0];
  uint8_t *cb1, *cb2 = src->data[1];
  uint8_t *cr1, *cr2 = src->data[2];

  for (h = height / 2; h--;) {
    line1 = linesrc;
    line2 = linesrc + dst->linesize[0];

    lum1 = lumsrc;
    lum2 = lumsrc + src->linesize[0];

    cb1 = cb2;
    cr1 = cr2;

    for (w = width / 2; w--;) {
      *line1++ = *lum1++;
      *line2++ = *lum2++;
      *line1++ = *line2++ = *cb1++;
      *line1++ = *lum1++;
      *line2++ = *lum2++;
      *line1++ = *line2++ = *cr1++;
    }
    /* odd width */
    if (width % 2 != 0) {
      *line1++ = *lum1++;
      *line2++ = *lum2++;
      *line1++ = *line2++ = *cb1++;
    }

    linesrc += dst->linesize[0] * 2;
    lumsrc += src->linesize[0] * 2;
    cb2 += src->linesize[1];
    cr2 += src->linesize[2];
  }
  /* odd height */
  if (height % 2 != 0) {
    line1 = linesrc;
    lum1 = lumsrc;
    cb1 = cb2;
    cr1 = cr2;

    for (w = width / 2; w--;) {
      *line1++ = *lum1++;
      *line1++ = *cb1++;
      *line1++ = *lum1++;
      *line1++ = *cr1++;
    }
    /* odd width */
    if (width % 2 != 0) {
      *line1++ = *lum1++;
      *line1++ = *cb1++;
    }
  }
}

#define SCALEBITS 10
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1<<SCALEBITS) + 0.5))

#define YUV_TO_RGB1_CCIR(cb1, cr1)\
{\
    cb = (cb1) - 128;\
    cr = (cr1) - 128;\
    r_add = FIX(1.40200*255.0/224.0) * cr + ONE_HALF;\
    g_add = - FIX(0.34414*255.0/224.0) * cb - FIX(0.71414*255.0/224.0) * cr + \
            ONE_HALF;\
    b_add = FIX(1.77200*255.0/224.0) * cb + ONE_HALF;\
}

#define YUV_TO_RGB2_CCIR(r, g, b, y1)\
{\
    y = ((y1) - 16) * FIX(255.0/219.0);\
    r = cm[(y + r_add) >> SCALEBITS];\
    g = cm[(y + g_add) >> SCALEBITS];\
    b = cm[(y + b_add) >> SCALEBITS];\
}

#define YUV_TO_RGB1(cb1, cr1)\
{\
    cb = (cb1) - 128;\
    cr = (cr1) - 128;\
    r_add = FIX(1.40200) * cr + ONE_HALF;\
    g_add = - FIX(0.34414) * cb - FIX(0.71414) * cr + ONE_HALF;\
    b_add = FIX(1.77200) * cb + ONE_HALF;\
}

#define YUV_TO_RGB2(r, g, b, y1)\
{\
    y = (y1) << SCALEBITS;\
    r = cm[(y + r_add) >> SCALEBITS];\
    g = cm[(y + g_add) >> SCALEBITS];\
    b = cm[(y + b_add) >> SCALEBITS];\
}

#define Y_CCIR_TO_JPEG(y)\
 cm[((y) * FIX(255.0/219.0) + (ONE_HALF - 16 * FIX(255.0/219.0))) >> SCALEBITS]

#define Y_JPEG_TO_CCIR(y)\
 (((y) * FIX(219.0/255.0) + (ONE_HALF + (16 << SCALEBITS))) >> SCALEBITS)

#define C_CCIR_TO_JPEG(y)\
 cm[(((y) - 128) * FIX(127.0/112.0) + (ONE_HALF + (128 << SCALEBITS))) >> SCALEBITS]

/* NOTE: the clamp is really necessary! */
static inline int
C_JPEG_TO_CCIR (int y)
{
  y = (((y - 128) * FIX (112.0 / 127.0) + (ONE_HALF +
              (128 << SCALEBITS))) >> SCALEBITS);
  if (y < 16)
    y = 16;
  return y;
}


#define RGB_TO_Y(r, g, b) \
((FIX(0.29900) * (r) + FIX(0.58700) * (g) + \
  FIX(0.11400) * (b) + ONE_HALF) >> SCALEBITS)

#define RGB_TO_U(r1, g1, b1, shift)\
(((- FIX(0.16874) * r1 - FIX(0.33126) * g1 +         \
     FIX(0.50000) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_V(r1, g1, b1, shift)\
(((FIX(0.50000) * r1 - FIX(0.41869) * g1 -           \
   FIX(0.08131) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_Y_CCIR(r, g, b) \
((FIX(0.29900*219.0/255.0) * (r) + FIX(0.58700*219.0/255.0) * (g) + \
  FIX(0.11400*219.0/255.0) * (b) + (ONE_HALF + (16 << SCALEBITS))) >> SCALEBITS)

#define RGB_TO_U_CCIR(r1, g1, b1, shift)\
(((- FIX(0.16874*224.0/255.0) * r1 - FIX(0.33126*224.0/255.0) * g1 +         \
     FIX(0.50000*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

#define RGB_TO_V_CCIR(r1, g1, b1, shift)\
(((FIX(0.50000*224.0/255.0) * r1 - FIX(0.41869*224.0/255.0) * g1 -           \
   FIX(0.08131*224.0/255.0) * b1 + (ONE_HALF << shift) - 1) >> (SCALEBITS + shift)) + 128)

static uint8_t y_ccir_to_jpeg[256];
static uint8_t y_jpeg_to_ccir[256];
static uint8_t c_ccir_to_jpeg[256];
static uint8_t c_jpeg_to_ccir[256];

/* init various conversion tables */
static void
img_convert_init (void)
{
  int i;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;

  for (i = 0; i < 256; i++) {
    y_ccir_to_jpeg[i] = Y_CCIR_TO_JPEG (i);
    y_jpeg_to_ccir[i] = Y_JPEG_TO_CCIR (i);
    c_ccir_to_jpeg[i] = C_CCIR_TO_JPEG (i);
    c_jpeg_to_ccir[i] = C_JPEG_TO_CCIR (i);
  }
}

/* apply to each pixel the given table */
static void
img_apply_table (uint8_t * dst, int dst_wrap,
    const uint8_t * src, int src_wrap,
    int width, int height, const uint8_t * table1)
{
  int n;
  const uint8_t *s;
  uint8_t *d;
  const uint8_t *table;

  table = table1;
  for (; height > 0; height--) {
    s = src;
    d = dst;
    n = width;
    while (n >= 4) {
      d[0] = table[s[0]];
      d[1] = table[s[1]];
      d[2] = table[s[2]];
      d[3] = table[s[3]];
      d += 4;
      s += 4;
      n -= 4;
    }
    while (n > 0) {
      d[0] = table[s[0]];
      d++;
      s++;
      n--;
    }
    dst += dst_wrap;
    src += src_wrap;
  }
}

/* XXX: use generic filter ? */
/* XXX: in most cases, the sampling position is incorrect */

static void
img_copy_plane_resize (uint8_t * dst, int dst_wrap, int dst_width,
    int dst_height, const uint8_t * src, int src_wrap, int src_width,
    int src_height)
{
  img_copy_plane (dst, dst_wrap, src, src_wrap, dst_width, dst_height);
}

/* 4x1 -> 1x1 */
static void
shrink41 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  int w, s_w;
  const uint8_t *s;
  uint8_t *d;

  for (; dst_height > 0; dst_height--) {
    s = src;
    d = dst;
    for (s_w = src_width, w = dst_width; w > 0 && s_w > 3; w--, s_w -= 4) {
      d[0] = (s[0] + s[1] + s[2] + s[3] + 2) >> 2;
      s += 4;
      d++;
    }

    if (w) {
      if (s_w == 3)
        d[0] = (s[0] + s[1] + s[2]) / 3;
      else if (s_w == 2)
        d[0] = (s[0] + s[1]) / 2;
      else                      /* s_w == 1 */
        d[0] = s[0];
    }

    src += src_wrap;
    dst += dst_wrap;
  }
}

/* 2x1 -> 1x1 */
static void
shrink21 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  int w, s_w;
  const uint8_t *s;
  uint8_t *d;

  for (; dst_height > 0; dst_height--) {
    s = src;
    d = dst;
    for (s_w = src_width, w = dst_width; w > 0 && s_w > 1; w--, s_w -= 2) {
      d[0] = (s[0] + s[1]) >> 1;
      s += 2;
      d++;
    }

    if (w)                      /* s_w == 1 */
      d[0] = s[0];

    src += src_wrap;
    dst += dst_wrap;
  }
}

/* 1x2 -> 1x1 */
static void
shrink12 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  int w;
  uint8_t *d;
  const uint8_t *s1, *s2;

  for (; dst_height > 0; dst_height--, src_height -= 2) {
    s1 = src;
    s2 = s1 + (src_height > 1 ? src_wrap : 0);
    d = dst;
    for (w = dst_width; w >= 4; w -= 4) {
      d[0] = (s1[0] + s2[0]) >> 1;
      d[1] = (s1[1] + s2[1]) >> 1;
      d[2] = (s1[2] + s2[2]) >> 1;
      d[3] = (s1[3] + s2[3]) >> 1;
      s1 += 4;
      s2 += 4;
      d += 4;
    }
    for (; w > 0; w--) {
      d[0] = (s1[0] + s2[0]) >> 1;
      s1++;
      s2++;
      d++;
    }
    src += 2 * src_wrap;
    dst += dst_wrap;
  }
}

/* 2x2 -> 1x1 */
static void
shrink22 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  int w, s_w;
  const uint8_t *s1, *s2;
  uint8_t *d;

  for (; dst_height > 0; dst_height--, src_height -= 2) {
    s1 = src;
    s2 = s1 + (src_height > 1 ? src_wrap : 0);
    d = dst;
    for (s_w = src_width, w = dst_width; w >= 4; w -= 4, s_w -= 8) {
      d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
      d[1] = (s1[2] + s1[3] + s2[2] + s2[3] + 2) >> 2;
      d[2] = (s1[4] + s1[5] + s2[4] + s2[5] + 2) >> 2;
      d[3] = (s1[6] + s1[7] + s2[6] + s2[7] + 2) >> 2;
      s1 += 8;
      s2 += 8;
      d += 4;
    }
    for (; w > 0 && s_w > 1; w--, s_w -= 2) {
      d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
      s1 += 2;
      s2 += 2;
      d++;
    }

    if (w)
      d[0] = (s1[0] + s2[0] + 1) >> 1;

    src += 2 * src_wrap;
    dst += dst_wrap;
  }
}

/* 4x4 -> 1x1 */
static void
shrink44 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  int s_w, w;
  const uint8_t *s1, *s2, *s3, *s4;
  uint8_t *d;

  for (; dst_height > 0; dst_height--, src_height -= 4) {
    s1 = src;
    s2 = s1 + (src_height > 1 ? src_wrap : 0);
    s3 = s2 + (src_height > 2 ? src_wrap : 0);
    s4 = s3 + (src_height > 3 ? src_wrap : 0);
    d = dst;
    for (s_w = src_width, w = dst_width; s_w > 3 && w > 0; w--, s_w -= 4) {
      d[0] = (s1[0] + s1[1] + s1[2] + s1[3] +
          s2[0] + s2[1] + s2[2] + s2[3] +
          s3[0] + s3[1] + s3[2] + s3[3] +
          s4[0] + s4[1] + s4[2] + s4[3] + 8) >> 4;
      s1 += 4;
      s2 += 4;
      s3 += 4;
      s4 += 4;
      d++;
    }

    if (w) {
      if (s_w == 3)
        d[0] = (s1[0] + s1[1] + s1[2] +
            s2[0] + s2[1] + s2[2] +
            s3[0] + s3[1] + s3[2] + s4[0] + s4[1] + s4[2]) / 12;
      else if (s_w == 2)
        d[0] = (s1[0] + s1[1] +
            s2[0] + s2[1] + s3[0] + s3[1] + s4[0] + s4[1]) / 8;
      else                      /* s_w == 1 */
        d[0] = (s1[0] + s2[0] + s3[0] + s4[0]) / 4;
    }

    src += 4 * src_wrap;
    dst += dst_wrap;
  }
}

static void
grow21_line (uint8_t * dst, const uint8_t * src, int width)
{
  int w;
  const uint8_t *s1;
  uint8_t *d;

  s1 = src;
  d = dst;
  for (w = width; w >= 4; w -= 4) {
    d[1] = d[0] = s1[0];
    d[3] = d[2] = s1[1];
    s1 += 2;
    d += 4;
  }
  for (; w >= 2; w -= 2) {
    d[1] = d[0] = s1[0];
    s1++;
    d += 2;
  }
  /* only needed if width is not a multiple of two */
  if (w) {
    d[0] = s1[0];
  }
}

static void
grow41_line (uint8_t * dst, const uint8_t * src, int width)
{
  int w, v;
  const uint8_t *s1;
  uint8_t *d;

  s1 = src;
  d = dst;
  for (w = width; w >= 4; w -= 4) {
    v = s1[0];
    d[0] = v;
    d[1] = v;
    d[2] = v;
    d[3] = v;
    s1++;
    d += 4;
  }
  for (; w > 0; w--) {
    d[0] = s1[0];
    d++;
  }
}

/* 1x1 -> 2x1 */
static void
grow21 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  for (; dst_height > 0; dst_height--) {
    grow21_line (dst, src, dst_width);
    src += src_wrap;
    dst += dst_wrap;
  }
}

/* 1x1 -> 2x2 */
static void
grow22 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  for (; dst_height > 0; dst_height--) {
    grow21_line (dst, src, dst_width);
    if (dst_height % 2)
      src += src_wrap;
    dst += dst_wrap;
  }
}

/* 1x1 -> 4x1 */
static void
grow41 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  for (; dst_height > 0; dst_height--) {
    grow41_line (dst, src, dst_width);
    src += src_wrap;
    dst += dst_wrap;
  }
}

/* 1x1 -> 4x4 */
static void
grow44 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  for (; dst_height > 0; dst_height--) {
    grow41_line (dst, src, dst_width);
    if ((dst_height & 3) == 1)
      src += src_wrap;
    dst += dst_wrap;
  }
}

/* 1x2 -> 2x1 */
static void
conv411 (uint8_t * dst, int dst_wrap, int dst_width, int dst_height,
    const uint8_t * src, int src_wrap, int src_width, int src_height)
{
  int w, c;
  const uint8_t *s1, *s2;
  uint8_t *d;

  for (; dst_height > 0; dst_height--, src_height -= 2) {
    s1 = src;
    s2 = src + (src_height > 1 ? src_wrap : 0);
    d = dst;
    for (w = dst_width; w > 1; w -= 2) {
      c = (s1[0] + s2[0]) >> 1;
      d[0] = c;
      d[1] = c;
      s1++;
      s2++;
      d += 2;
    }

    if (w) {
      d[0] = (s1[0] + s2[0]) >> 1;
    }

    src += src_wrap * 2;
    dst += dst_wrap;
  }
}

/* XXX: add jpeg quantize code */

#define TRANSP_INDEX (6*6*6)

/* this is maybe slow, but allows for extensions */
static inline unsigned char
gif_clut_index (uint8_t r, uint8_t g, uint8_t b)
{
  return ((((r) / 47) % 6) * 6 * 6 + (((g) / 47) % 6) * 6 + (((b) / 47) % 6));
}

static void
build_rgb_palette (uint8_t * palette, int has_alpha)
{
  uint32_t *pal;
  static const uint8_t pal_value[6] = { 0x00, 0x33, 0x66, 0x99, 0xcc, 0xff };
  int i, r, g, b;

  pal = (uint32_t *) palette;
  i = 0;
  for (r = 0; r < 6; r++) {
    for (g = 0; g < 6; g++) {
      for (b = 0; b < 6; b++) {
        pal[i++] = (0xffU << 24) | (pal_value[r] << 16) |
            (pal_value[g] << 8) | pal_value[b];
      }
    }
  }
  if (has_alpha)
    pal[i++] = 0;
  while (i < 256)
    pal[i++] = 0xff000000;
}

/* copy bit n to bits 0 ... n - 1 */
static inline unsigned int
bitcopy_n (unsigned int a, int n)
{
  int mask;

  mask = (1 << n) - 1;
  return (a & (0xff & ~mask)) | ((-((a >> n) & 1)) & mask);
}

/* rgb555 handling */

#define RGB_NAME rgb555

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint16_t *)(s))[0];\
    r = bitcopy_n(v >> (10 - 3), 3);\
    g = bitcopy_n(v >> (5 - 3), 3);\
    b = bitcopy_n(v << 3, 3);\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint16_t *)(s))[0];\
    r = bitcopy_n(v >> (10 - 3), 3);\
    g = bitcopy_n(v >> (5 - 3), 3);\
    b = bitcopy_n(v << 3, 3);\
    a = (-(v >> 15)) & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint16_t *)(d))[0] = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) | \
                           ((a << 8) & 0x8000);\
}

#define BPP 2

#include "imgconvert_template.h"

/* rgb565 handling */

#define RGB_NAME rgb565

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint16_t *)(s))[0];\
    r = bitcopy_n(v >> (11 - 3), 3);\
    g = bitcopy_n(v >> (5 - 2), 2);\
    b = bitcopy_n(v << 3, 3);\
}

#define RGB_OUT(d, r, g, b)\
{\
    ((uint16_t *)(d))[0] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);\
}

#define BPP 2

#include "imgconvert_template.h"

/* bgr24 handling */

#define RGB_NAME bgr24

#define RGB_IN(r, g, b, s)\
{\
    b = (s)[0];\
    g = (s)[1];\
    r = (s)[2];\
}

#define RGB_OUT(d, r, g, b)\
{\
    (d)[0] = b;\
    (d)[1] = g;\
    (d)[2] = r;\
}

#define BPP 3

#include "imgconvert_template.h"

#undef RGB_IN
#undef RGB_OUT
#undef BPP

/* rgb24 handling */

#define RGB_NAME rgb24
#define FMT_RGB24

#define RGB_IN(r, g, b, s)\
{\
    r = (s)[0];\
    g = (s)[1];\
    b = (s)[2];\
}

#define RGB_OUT(d, r, g, b)\
{\
    (d)[0] = r;\
    (d)[1] = g;\
    (d)[2] = b;\
}

#define BPP 3

#include "imgconvert_template.h"

/* rgb32 handling */

#define RGB_NAME rgb32
#define FMT_RGBA32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = 0xff;\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = (a << 24) | (r << 16) | (g << 8) | b;\
}

#define BPP 4

#include "imgconvert_template.h"

/* bgr32 handling */

#define RGB_NAME bgr32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 8) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 24) & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = 0xff;\
    r = (v >> 8) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 24) & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = a | (r << 8) | (g << 16) | (b << 24);\
}

#define BPP 4

#include "imgconvert_template.h"

/* xrgb32 handling */

#define RGB_NAME xrgb32
#define FMT_RGBA32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 24) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 8) & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = 0xff;\
    r = (v >> 24) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 8) & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = (r << 24) | (g << 16) | (b << 8) | a;\
}

#define BPP 4

#include "imgconvert_template.h"

/* bgrx32 handling */

#define RGB_NAME bgrx32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = (v >> 16) & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = 0xff;\
    r = (v) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = (v >> 16) & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = r | (g << 8) | (b << 16) | (a << 24);\
}

#define BPP 4

#include "imgconvert_template.h"

/* rgba32 handling */

#define RGB_NAME rgba32
#define FMT_RGBA32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = (v >> 24) & 0xff;\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = (a << 24) | (r << 16) | (g << 8) | b;\
}

#define BPP 4

#include "imgconvert_template.h"

/* bgra32 handling */

#define RGB_NAME bgra32
#define FMT_BGRA32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 8) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 24) & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = v & 0xff;\
    r = (v >> 8) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 24) & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = a | (r << 8) | (g << 16) | (b << 24 );\
}

#define BPP 4

#include "imgconvert_template.h"

/* argb32 handling */

#define RGB_NAME argb32
#define FMT_ARGB32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 24) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 8) & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 24) & 0xff;\
    g = (v >> 16) & 0xff;\
    b = (v >> 8) & 0xff;\
    a = v & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = (r << 24) | (g << 16) | (b << 8) | a;\
}

#define BPP 4

#include "imgconvert_template.h"

/* abgr32 handling */

#define RGB_NAME abgr32
#define FMT_ABGR32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = v & 0xff;\
    g = (v >> 8) & 0xff;\
    b = (v >> 16) & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = v & 0xff;\
    g = (v >> 8) & 0xff;\
    b = (v >> 16) & 0xff;\
    a = (v >> 24) & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = r | (g << 8) | (b << 16) | (a << 24 );\
}

#define BPP 4

#include "imgconvert_template.h"

static void
gray_to_gray16_l (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - 2 * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      GST_WRITE_UINT16_LE (q, (*p << 8));
      q += 2;
      p++;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
gray_to_gray16_b (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - 2 * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      GST_WRITE_UINT16_BE (q, (*p << 8));
      q += 2;
      p++;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
gray16_l_to_gray (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - 2 * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      q[0] = GST_READ_UINT16_LE (p) >> 8;
      q++;
      p += 2;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
gray16_b_to_gray (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - 2 * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      q[0] = GST_READ_UINT16_BE (p) >> 8;
      q++;
      p += 2;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
gray16_b_to_gray16_l (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - 2 * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - 2 * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      q[0] = p[1];
      q[1] = p[0];
      q += 2;
      p += 2;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
mono_to_gray (AVPicture * dst, const AVPicture * src,
    int width, int height, int xor_mask)
{
  const unsigned char *p;
  unsigned char *q;
  int v, dst_wrap, src_wrap;
  int y, w;

  p = src->data[0];
  src_wrap = src->linesize[0] - ((width + 7) >> 3);

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - width;
  for (y = 0; y < height; y++) {
    w = width;
    while (w >= 8) {
      v = *p++ ^ xor_mask;
      q[0] = -(v >> 7);
      q[1] = -((v >> 6) & 1);
      q[2] = -((v >> 5) & 1);
      q[3] = -((v >> 4) & 1);
      q[4] = -((v >> 3) & 1);
      q[5] = -((v >> 2) & 1);
      q[6] = -((v >> 1) & 1);
      q[7] = -((v >> 0) & 1);
      w -= 8;
      q += 8;
    }
    if (w > 0) {
      v = *p++ ^ xor_mask;
      do {
        q[0] = -((v >> 7) & 1);
        q++;
        v <<= 1;
      } while (--w);
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
monowhite_to_gray (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  mono_to_gray (dst, src, width, height, 0xff);
}

static void
monoblack_to_gray (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  mono_to_gray (dst, src, width, height, 0x00);
}

static void
gray_to_mono (AVPicture * dst, const AVPicture * src,
    int width, int height, int xor_mask)
{
  int n;
  const uint8_t *s;
  uint8_t *d;
  int j, b, v, n1, src_wrap, dst_wrap, y;

  s = src->data[0];
  src_wrap = src->linesize[0] - width;

  d = dst->data[0];
  dst_wrap = dst->linesize[0] - ((width + 7) >> 3);

  for (y = 0; y < height; y++) {
    n = width;
    while (n >= 8) {
      v = 0;
      for (j = 0; j < 8; j++) {
        b = s[0];
        s++;
        v = (v << 1) | (b >> 7);
      }
      d[0] = v ^ xor_mask;
      d++;
      n -= 8;
    }
    if (n > 0) {
      n1 = n;
      v = 0;
      while (n > 0) {
        b = s[0];
        s++;
        v = (v << 1) | (b >> 7);
        n--;
      }
      d[0] = (v << (8 - (n1 & 7))) ^ xor_mask;
      d++;
    }
    s += src_wrap;
    d += dst_wrap;
  }
}

static void
gray_to_monowhite (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  gray_to_mono (dst, src, width, height, 0xff);
}

static void
gray_to_monoblack (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  gray_to_mono (dst, src, width, height, 0x00);
}

static void
y800_to_y16 (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - 2 * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      GST_WRITE_UINT16_LE (q, (*p << 8));
      q += 2;
      p++;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
y16_to_y800 (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - 2 * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      q[0] = GST_READ_UINT16_LE (p) >> 8;
      q++;
      p += 2;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void
yuva420p_to_ayuv4444 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr, *a1_ptr, *a2_ptr;
  uint8_t *d, *d1, *d2;
  int w, width2;

  d = dst->data[0];
  y1_ptr = src->data[0];
  cb_ptr = src->data[1];
  cr_ptr = src->data[2];
  a1_ptr = src->data[3];
  width2 = (width + 1) >> 1;
  for (; height >= 2; height -= 2) {
    d1 = d;
    d2 = d + dst->linesize[0];
    y2_ptr = y1_ptr + src->linesize[0];
    a2_ptr = a1_ptr + src->linesize[3];
    for (w = width; w >= 2; w -= 2) {
      d1[0] = a1_ptr[0];
      d1[1] = y1_ptr[0];
      d1[2] = cb_ptr[0];
      d1[3] = cr_ptr[0];

      d1[4 + 0] = a1_ptr[1];
      d1[4 + 1] = y1_ptr[1];
      d1[4 + 2] = cb_ptr[0];
      d1[4 + 3] = cr_ptr[0];

      d2[0] = a2_ptr[0];
      d2[1] = y2_ptr[0];
      d2[2] = cb_ptr[0];
      d2[3] = cr_ptr[0];

      d2[4 + 0] = a2_ptr[1];
      d2[4 + 1] = y2_ptr[1];
      d2[4 + 2] = cb_ptr[0];
      d2[4 + 3] = cr_ptr[0];

      d1 += 2 * 4;
      d2 += 2 * 4;

      y1_ptr += 2;
      y2_ptr += 2;
      cb_ptr++;
      cr_ptr++;
      a1_ptr += 2;
      a2_ptr += 2;
    }
    /* handle odd width */
    if (w) {
      d1[0] = a1_ptr[0];
      d1[1] = y1_ptr[0];
      d1[2] = cb_ptr[0];
      d1[3] = cr_ptr[0];

      d2[0] = a2_ptr[0];
      d2[1] = y2_ptr[0];
      d2[2] = cb_ptr[0];
      d2[3] = cr_ptr[0];

      d1 += 4;
      d2 += 4;
      y1_ptr++;
      y2_ptr++;
      cb_ptr++;
      cr_ptr++;
      a1_ptr++;
      a2_ptr++;
    }
    d += 2 * dst->linesize[0];
    y1_ptr += 2 * src->linesize[0] - width;
    cb_ptr += src->linesize[1] - width2;
    cr_ptr += src->linesize[2] - width2;
    a1_ptr += 2 * src->linesize[3] - width;
  }
  /* handle odd height */
  if (height) {
    d1 = d;
    for (w = width; w >= 2; w -= 2) {
      d1[0] = a1_ptr[0];
      d1[1] = y1_ptr[0];
      d1[2] = cb_ptr[0];
      d1[3] = cr_ptr[0];

      d1[4 + 0] = a1_ptr[1];
      d1[4 + 1] = y1_ptr[1];
      d1[4 + 2] = cb_ptr[0];
      d1[4 + 3] = cr_ptr[0];

      d1 += 2 * 4;

      y1_ptr += 2;
      cb_ptr++;
      cr_ptr++;
      a1_ptr += 2;
    }
    /* handle width */
    if (w) {
      d1[0] = a1_ptr[0];
      d1[1] = y1_ptr[0];
      d1[2] = cb_ptr[0];
      d1[3] = cr_ptr[0];
      d1 += 4;

      y1_ptr++;
      cb_ptr++;
      cr_ptr++;
      a1_ptr++;
    }
  }
}

static void
ayuv4444_to_yuva420p (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  int wrap, wrap3, width2;
  int u1, v1, w;
  uint8_t *lum, *cb, *cr, *a;
  const uint8_t *p;

  lum = dst->data[0];
  cb = dst->data[1];
  cr = dst->data[2];
  a = dst->data[3];

  width2 = (width + 1) >> 1;
  wrap = dst->linesize[0];
  wrap3 = src->linesize[0];
  p = src->data[0];
  for (; height >= 2; height -= 2) {
    for (w = width; w >= 2; w -= 2) {
      a[0] = p[0];
      lum[0] = p[1];
      u1 = p[2];
      v1 = p[3];

      a[1] = p[4 + 0];
      lum[1] = p[4 + 1];
      u1 += p[4 + 2];
      v1 += p[4 + 3];
      p += wrap3;
      lum += wrap;
      a += wrap;

      a[0] = p[0];
      lum[0] = p[1];
      u1 += p[2];
      v1 += p[3];

      a[1] = p[4 + 0];
      lum[1] = p[4 + 1];
      u1 += p[4 + 2];
      v1 += p[4 + 3];

      cb[0] = u1 >> 2;
      cr[0] = v1 >> 2;

      cb++;
      cr++;
      p += -wrap3 + 2 * 4;
      lum += -wrap + 2;
      a += -wrap + 2;
    }
    if (w) {
      a[0] = p[0];
      lum[0] = p[1];
      u1 = p[2];
      v1 = p[3];
      p += wrap3;
      lum += wrap;
      a += wrap;

      a[0] = p[0];
      lum[0] = p[1];
      u1 += p[2];
      v1 += p[3];

      cb[0] = u1 >> 1;
      cr[0] = v1 >> 1;
      cb++;
      cr++;
      p += -wrap3 + 4;
      lum += -wrap + 1;
      a += -wrap + 1;
    }
    p += wrap3 + (wrap3 - width * 4);
    lum += wrap + (wrap - width);
    a += wrap + (wrap - width);
    cb += dst->linesize[1] - width2;
    cr += dst->linesize[2] - width2;
  }
  /* handle odd height */
  if (height) {
    for (w = width; w >= 2; w -= 2) {
      a[0] = p[0];
      lum[0] = p[1];
      u1 = p[2];
      v1 = p[3];

      a[1] = p[4 + 0];
      lum[1] = p[4 + 1];
      u1 += p[4 + 2];
      v1 += p[4 + 3];
      cb[0] = u1 >> 1;
      cr[0] = v1 >> 1;
      cb++;
      cr++;
      p += 2 * 4;
      lum += 2;
      a += 2;
    }
    if (w) {
      a[0] = p[0];
      lum[0] = p[1];
      cb[0] = p[2];
      cr[0] = p[3];
    }
  }
}

typedef struct ConvertEntry
{
  enum PixelFormat src;
  enum PixelFormat dest;
  void (*convert) (AVPicture * dst,
      const AVPicture * src, int width, int height);
} ConvertEntry;

/* Add each new conversion function in this table. In order to be able
   to convert from any format to any format, the following constraints
   must be satisfied:

   - all FF_COLOR_RGB formats must convert to and from PIX_FMT_RGB24 

   - all FF_COLOR_GRAY formats must convert to and from PIX_FMT_GRAY8

   - all FF_COLOR_RGB formats with alpha must convert to and from PIX_FMT_RGBA32

   - PIX_FMT_YUV444P and PIX_FMT_YUVJ444P must convert to and from
     PIX_FMT_RGB24.

   - PIX_FMT_422 must convert to and from PIX_FMT_422P.

   The other conversion functions are just optimisations for common cases.
*/
static ConvertEntry convert_table[] = {
  {PIX_FMT_YUV420P, PIX_FMT_YUV422, yuv420p_to_yuv422},
  {PIX_FMT_YUV420P, PIX_FMT_RGB555, yuv420p_to_rgb555},
  {PIX_FMT_YUV420P, PIX_FMT_RGB565, yuv420p_to_rgb565},
  {PIX_FMT_YUV420P, PIX_FMT_BGR24, yuv420p_to_bgr24},
  {PIX_FMT_YUV420P, PIX_FMT_RGB24, yuv420p_to_rgb24},
  {PIX_FMT_YUV420P, PIX_FMT_RGB32, yuv420p_to_rgb32},
  {PIX_FMT_YUV420P, PIX_FMT_BGR32, yuv420p_to_bgr32},
  {PIX_FMT_YUV420P, PIX_FMT_xRGB32, yuv420p_to_xrgb32},
  {PIX_FMT_YUV420P, PIX_FMT_BGRx32, yuv420p_to_bgrx32},
  {PIX_FMT_YUV420P, PIX_FMT_RGBA32, yuv420p_to_rgba32},
  {PIX_FMT_YUV420P, PIX_FMT_BGRA32, yuv420p_to_bgra32},
  {PIX_FMT_YUV420P, PIX_FMT_ARGB32, yuv420p_to_argb32},
  {PIX_FMT_YUV420P, PIX_FMT_ABGR32, yuv420p_to_abgr32},

  {PIX_FMT_NV12, PIX_FMT_RGB555, nv12_to_rgb555},
  {PIX_FMT_NV12, PIX_FMT_RGB565, nv12_to_rgb565},
  {PIX_FMT_NV12, PIX_FMT_BGR24, nv12_to_bgr24},
  {PIX_FMT_NV12, PIX_FMT_RGB24, nv12_to_rgb24},
  {PIX_FMT_NV12, PIX_FMT_RGB32, nv12_to_rgb32},
  {PIX_FMT_NV12, PIX_FMT_BGR32, nv12_to_bgr32},
  {PIX_FMT_NV12, PIX_FMT_xRGB32, nv12_to_xrgb32},
  {PIX_FMT_NV12, PIX_FMT_BGRx32, nv12_to_bgrx32},
  {PIX_FMT_NV12, PIX_FMT_RGBA32, nv12_to_rgba32},
  {PIX_FMT_NV12, PIX_FMT_BGRA32, nv12_to_bgra32},
  {PIX_FMT_NV12, PIX_FMT_ARGB32, nv12_to_argb32},
  {PIX_FMT_NV12, PIX_FMT_ABGR32, nv12_to_abgr32},
  {PIX_FMT_NV12, PIX_FMT_NV21, nv12_to_nv21},
  {PIX_FMT_NV12, PIX_FMT_YUV444P, nv12_to_yuv444p},

  {PIX_FMT_NV21, PIX_FMT_RGB555, nv21_to_rgb555},
  {PIX_FMT_NV21, PIX_FMT_RGB565, nv21_to_rgb565},
  {PIX_FMT_NV21, PIX_FMT_BGR24, nv21_to_bgr24},
  {PIX_FMT_NV21, PIX_FMT_RGB24, nv21_to_rgb24},
  {PIX_FMT_NV21, PIX_FMT_RGB32, nv21_to_rgb32},
  {PIX_FMT_NV21, PIX_FMT_BGR32, nv21_to_bgr32},
  {PIX_FMT_NV21, PIX_FMT_xRGB32, nv21_to_xrgb32},
  {PIX_FMT_NV21, PIX_FMT_BGRx32, nv21_to_bgrx32},
  {PIX_FMT_NV21, PIX_FMT_RGBA32, nv21_to_rgba32},
  {PIX_FMT_NV21, PIX_FMT_BGRA32, nv21_to_bgra32},
  {PIX_FMT_NV21, PIX_FMT_ARGB32, nv21_to_argb32},
  {PIX_FMT_NV21, PIX_FMT_ABGR32, nv21_to_abgr32},
  {PIX_FMT_NV21, PIX_FMT_YUV444P, nv21_to_yuv444p},
  {PIX_FMT_NV21, PIX_FMT_NV12, nv21_to_nv12},

  {PIX_FMT_YUV422P, PIX_FMT_YUV422, yuv422p_to_yuv422},
  {PIX_FMT_YUV422P, PIX_FMT_UYVY422, yuv422p_to_uyvy422},
  {PIX_FMT_YUV422P, PIX_FMT_YVYU422, yuv422p_to_yvyu422},

  {PIX_FMT_YUV444P, PIX_FMT_RGB24, yuv444p_to_rgb24},

  {PIX_FMT_YUVJ420P, PIX_FMT_RGB555, yuvj420p_to_rgb555},
  {PIX_FMT_YUVJ420P, PIX_FMT_RGB565, yuvj420p_to_rgb565},
  {PIX_FMT_YUVJ420P, PIX_FMT_BGR24, yuvj420p_to_bgr24},
  {PIX_FMT_YUVJ420P, PIX_FMT_RGB24, yuvj420p_to_rgb24},
  {PIX_FMT_YUVJ420P, PIX_FMT_RGB32, yuvj420p_to_rgb32},
  {PIX_FMT_YUVJ420P, PIX_FMT_BGR32, yuvj420p_to_bgr32},
  {PIX_FMT_YUVJ420P, PIX_FMT_RGB32, yuvj420p_to_xrgb32},
  {PIX_FMT_YUVJ420P, PIX_FMT_BGR32, yuvj420p_to_bgrx32},
  {PIX_FMT_YUVJ420P, PIX_FMT_RGBA32, yuvj420p_to_rgba32},
  {PIX_FMT_YUVJ420P, PIX_FMT_BGRA32, yuvj420p_to_bgra32},
  {PIX_FMT_YUVJ420P, PIX_FMT_ARGB32, yuvj420p_to_argb32},
  {PIX_FMT_YUVJ420P, PIX_FMT_ABGR32, yuvj420p_to_abgr32},

  {PIX_FMT_YUVJ444P, PIX_FMT_RGB24, yuvj444p_to_rgb24},

  {PIX_FMT_YUV422, PIX_FMT_YUV420P, yuv422_to_yuv420p},
  {PIX_FMT_YUV422, PIX_FMT_YUV422P, yuv422_to_yuv422p},
  {PIX_FMT_YUV422, PIX_FMT_GRAY8, yvyu422_to_gray},
  {PIX_FMT_YUV422, PIX_FMT_RGB555, yuv422_to_rgb555},
  {PIX_FMT_YUV422, PIX_FMT_RGB565, yuv422_to_rgb565},
  {PIX_FMT_YUV422, PIX_FMT_BGR24, yuv422_to_bgr24},
  {PIX_FMT_YUV422, PIX_FMT_RGB24, yuv422_to_rgb24},
  {PIX_FMT_YUV422, PIX_FMT_BGR32, yuv422_to_bgr32},
  {PIX_FMT_YUV422, PIX_FMT_RGB32, yuv422_to_rgb32},
  {PIX_FMT_YUV422, PIX_FMT_xRGB32, yuv422_to_xrgb32},
  {PIX_FMT_YUV422, PIX_FMT_BGRx32, yuv422_to_bgrx32},
  {PIX_FMT_YUV422, PIX_FMT_BGRA32, yuv422_to_bgra32},
  {PIX_FMT_YUV422, PIX_FMT_RGBA32, yuv422_to_rgba32},
  {PIX_FMT_YUV422, PIX_FMT_ABGR32, yuv422_to_abgr32},
  {PIX_FMT_YUV422, PIX_FMT_ARGB32, yuv422_to_argb32},

  {PIX_FMT_UYVY422, PIX_FMT_YUV420P, uyvy422_to_yuv420p},
  {PIX_FMT_UYVY422, PIX_FMT_YUV422P, uyvy422_to_yuv422p},
  {PIX_FMT_UYVY422, PIX_FMT_GRAY8, uyvy422_to_gray},
  {PIX_FMT_UYVY422, PIX_FMT_RGB555, uyvy422_to_rgb555},
  {PIX_FMT_UYVY422, PIX_FMT_RGB565, uyvy422_to_rgb565},
  {PIX_FMT_UYVY422, PIX_FMT_BGR24, uyvy422_to_bgr24},
  {PIX_FMT_UYVY422, PIX_FMT_RGB24, uyvy422_to_rgb24},
  {PIX_FMT_UYVY422, PIX_FMT_RGB32, uyvy422_to_rgb32},
  {PIX_FMT_UYVY422, PIX_FMT_BGR32, uyvy422_to_bgr32},
  {PIX_FMT_UYVY422, PIX_FMT_xRGB32, uyvy422_to_xrgb32},
  {PIX_FMT_UYVY422, PIX_FMT_BGRx32, uyvy422_to_bgrx32},
  {PIX_FMT_UYVY422, PIX_FMT_RGBA32, uyvy422_to_rgba32},
  {PIX_FMT_UYVY422, PIX_FMT_BGRA32, uyvy422_to_bgra32},
  {PIX_FMT_UYVY422, PIX_FMT_ARGB32, uyvy422_to_argb32},
  {PIX_FMT_UYVY422, PIX_FMT_ABGR32, uyvy422_to_abgr32},

  {PIX_FMT_YVYU422, PIX_FMT_YUV420P, yvyu422_to_yuv420p},
  {PIX_FMT_YVYU422, PIX_FMT_YUV422P, yvyu422_to_yuv422p},
  {PIX_FMT_YVYU422, PIX_FMT_GRAY8, yvyu422_to_gray},
  {PIX_FMT_YVYU422, PIX_FMT_RGB555, yvyu422_to_rgb555},
  {PIX_FMT_YVYU422, PIX_FMT_RGB565, yvyu422_to_rgb565},
  {PIX_FMT_YVYU422, PIX_FMT_BGR24, yvyu422_to_bgr24},
  {PIX_FMT_YVYU422, PIX_FMT_RGB24, yvyu422_to_rgb24},
  {PIX_FMT_YVYU422, PIX_FMT_BGR32, yvyu422_to_bgr32},
  {PIX_FMT_YVYU422, PIX_FMT_RGB32, yvyu422_to_rgb32},
  {PIX_FMT_YVYU422, PIX_FMT_xRGB32, yvyu422_to_xrgb32},
  {PIX_FMT_YVYU422, PIX_FMT_BGRx32, yvyu422_to_bgrx32},
  {PIX_FMT_YVYU422, PIX_FMT_BGRA32, yvyu422_to_bgra32},
  {PIX_FMT_YVYU422, PIX_FMT_RGBA32, yvyu422_to_rgba32},
  {PIX_FMT_YVYU422, PIX_FMT_ABGR32, yvyu422_to_abgr32},
  {PIX_FMT_YVYU422, PIX_FMT_ARGB32, yvyu422_to_argb32},

  {PIX_FMT_RGB24, PIX_FMT_YUV420P, rgb24_to_yuv420p},
  {PIX_FMT_RGB24, PIX_FMT_YUVA420P, rgb24_to_yuva420p},
  {PIX_FMT_RGB24, PIX_FMT_NV12, rgb24_to_nv12},
  {PIX_FMT_RGB24, PIX_FMT_NV21, rgb24_to_nv21},
  {PIX_FMT_RGB24, PIX_FMT_RGB565, rgb24_to_rgb565},
  {PIX_FMT_RGB24, PIX_FMT_RGB555, rgb24_to_rgb555},
  {PIX_FMT_RGB24, PIX_FMT_RGB32, rgb24_to_rgb32},
  {PIX_FMT_RGB24, PIX_FMT_BGR32, rgb24_to_bgr32},
  {PIX_FMT_RGB24, PIX_FMT_xRGB32, rgb24_to_xrgb32},
  {PIX_FMT_RGB24, PIX_FMT_BGRx32, rgb24_to_bgrx32},
  {PIX_FMT_RGB24, PIX_FMT_RGBA32, rgb24_to_rgba32},
  {PIX_FMT_RGB24, PIX_FMT_BGR24, rgb24_to_bgr24},
  {PIX_FMT_RGB24, PIX_FMT_BGRA32, rgb24_to_bgra32},
  {PIX_FMT_RGB24, PIX_FMT_ARGB32, rgb24_to_argb32},
  {PIX_FMT_RGB24, PIX_FMT_ABGR32, rgb24_to_abgr32},
  {PIX_FMT_RGB24, PIX_FMT_Y800, rgb24_to_y800},
  {PIX_FMT_RGB24, PIX_FMT_Y16, rgb24_to_y16},
  {PIX_FMT_RGB24, PIX_FMT_GRAY8, rgb24_to_gray},
  {PIX_FMT_RGB24, PIX_FMT_GRAY16_L, rgb24_to_gray16_l},
  {PIX_FMT_RGB24, PIX_FMT_GRAY16_B, rgb24_to_gray16_b},
  {PIX_FMT_RGB24, PIX_FMT_PAL8, rgb24_to_pal8},
  {PIX_FMT_RGB24, PIX_FMT_YUV444P, rgb24_to_yuv444p},
  {PIX_FMT_RGB24, PIX_FMT_YUVJ420P, rgb24_to_yuvj420p},
  {PIX_FMT_RGB24, PIX_FMT_YUVJ444P, rgb24_to_yuvj444p},
  {PIX_FMT_RGB24, PIX_FMT_AYUV4444, rgb24_to_ayuv4444},
  {PIX_FMT_RGB24, PIX_FMT_V308, rgb24_to_v308},

  {PIX_FMT_RGB32, PIX_FMT_RGB24, rgb32_to_rgb24},
  {PIX_FMT_RGB32, PIX_FMT_RGB555, rgba32_to_rgb555},
  {PIX_FMT_RGB32, PIX_FMT_PAL8, rgb32_to_pal8},
  {PIX_FMT_RGB32, PIX_FMT_YUV420P, rgb32_to_yuv420p},
  {PIX_FMT_RGB32, PIX_FMT_YUVA420P, rgb32_to_yuva420p},
  {PIX_FMT_RGB32, PIX_FMT_NV12, rgb32_to_nv12},
  {PIX_FMT_RGB32, PIX_FMT_NV21, rgb32_to_nv21},
  {PIX_FMT_RGB32, PIX_FMT_Y800, rgb32_to_y800},
  {PIX_FMT_RGB32, PIX_FMT_Y16, rgb32_to_y16},
  {PIX_FMT_RGB32, PIX_FMT_GRAY8, rgb32_to_gray},
  {PIX_FMT_RGB32, PIX_FMT_GRAY16_L, rgb32_to_gray16_l},
  {PIX_FMT_RGB32, PIX_FMT_GRAY16_B, rgb32_to_gray16_b},

  {PIX_FMT_xRGB32, PIX_FMT_RGB24, xrgb32_to_rgb24},
  {PIX_FMT_xRGB32, PIX_FMT_PAL8, xrgb32_to_pal8},
  {PIX_FMT_xRGB32, PIX_FMT_YUV420P, xrgb32_to_yuv420p},
  {PIX_FMT_xRGB32, PIX_FMT_YUVA420P, xrgb32_to_yuva420p},
  {PIX_FMT_xRGB32, PIX_FMT_NV12, xrgb32_to_nv12},
  {PIX_FMT_xRGB32, PIX_FMT_NV21, xrgb32_to_nv21},
  {PIX_FMT_xRGB32, PIX_FMT_Y800, xrgb32_to_y800},
  {PIX_FMT_xRGB32, PIX_FMT_Y16, xrgb32_to_y16},
  {PIX_FMT_xRGB32, PIX_FMT_GRAY8, xrgb32_to_gray},
  {PIX_FMT_xRGB32, PIX_FMT_GRAY16_L, xrgb32_to_gray16_l},
  {PIX_FMT_xRGB32, PIX_FMT_GRAY16_B, xrgb32_to_gray16_b},

  {PIX_FMT_RGBA32, PIX_FMT_BGRA32, rgba32_to_bgra32},
  {PIX_FMT_RGBA32, PIX_FMT_ABGR32, rgba32_to_abgr32},
  {PIX_FMT_RGBA32, PIX_FMT_ARGB32, rgba32_to_argb32},
  {PIX_FMT_RGBA32, PIX_FMT_BGR32, rgba32_to_bgr32},
  {PIX_FMT_RGBA32, PIX_FMT_BGRx32, rgba32_to_bgrx32},
  {PIX_FMT_RGBA32, PIX_FMT_ABGR32, rgba32_to_abgr32},
  {PIX_FMT_RGBA32, PIX_FMT_RGB24, rgba32_to_rgb24},
  {PIX_FMT_RGBA32, PIX_FMT_RGB555, rgba32_to_rgb555},
  {PIX_FMT_RGBA32, PIX_FMT_PAL8, rgba32_to_pal8},
  {PIX_FMT_RGBA32, PIX_FMT_YUV420P, rgba32_to_yuv420p},
  {PIX_FMT_RGBA32, PIX_FMT_YUVA420P, rgba32_to_yuva420p},
  {PIX_FMT_RGBA32, PIX_FMT_NV12, rgba32_to_nv12},
  {PIX_FMT_RGBA32, PIX_FMT_NV21, rgba32_to_nv21},
  {PIX_FMT_RGBA32, PIX_FMT_Y800, rgba32_to_y800},
  {PIX_FMT_RGBA32, PIX_FMT_Y16, rgba32_to_y16},
  {PIX_FMT_RGBA32, PIX_FMT_GRAY8, rgba32_to_gray},
  {PIX_FMT_RGBA32, PIX_FMT_GRAY16_L, rgba32_to_gray16_l},
  {PIX_FMT_RGBA32, PIX_FMT_GRAY16_B, rgba32_to_gray16_b},
  {PIX_FMT_RGBA32, PIX_FMT_AYUV4444, rgba32_to_ayuv4444},

  {PIX_FMT_BGR24, PIX_FMT_RGB24, bgr24_to_rgb24},
  {PIX_FMT_BGR24, PIX_FMT_YUV420P, bgr24_to_yuv420p},
  {PIX_FMT_BGR24, PIX_FMT_YUVA420P, bgr24_to_yuva420p},
  {PIX_FMT_BGR24, PIX_FMT_NV12, bgr24_to_nv12},
  {PIX_FMT_BGR24, PIX_FMT_NV21, bgr24_to_nv21},
  {PIX_FMT_BGR24, PIX_FMT_Y800, bgr24_to_y800},
  {PIX_FMT_BGR24, PIX_FMT_Y16, bgr24_to_y16},
  {PIX_FMT_BGR24, PIX_FMT_GRAY8, bgr24_to_gray},
  {PIX_FMT_BGR24, PIX_FMT_GRAY16_L, bgr24_to_gray16_l},
  {PIX_FMT_BGR24, PIX_FMT_GRAY16_B, bgr24_to_gray16_b},

  {PIX_FMT_BGR32, PIX_FMT_RGB24, bgr32_to_rgb24},
  {PIX_FMT_BGR32, PIX_FMT_RGBA32, bgr32_to_rgba32},
  {PIX_FMT_BGR32, PIX_FMT_YUV420P, bgr32_to_yuv420p},
  {PIX_FMT_BGR32, PIX_FMT_YUVA420P, bgr32_to_yuva420p},
  {PIX_FMT_BGR32, PIX_FMT_NV12, bgr32_to_nv12},
  {PIX_FMT_BGR32, PIX_FMT_NV21, bgr32_to_nv21},
  {PIX_FMT_BGR32, PIX_FMT_Y800, bgr32_to_y800},
  {PIX_FMT_BGR32, PIX_FMT_Y16, bgr32_to_y16},
  {PIX_FMT_BGR32, PIX_FMT_GRAY8, bgr32_to_gray},
  {PIX_FMT_BGR32, PIX_FMT_GRAY16_L, bgr32_to_gray16_l},
  {PIX_FMT_BGR32, PIX_FMT_GRAY16_B, bgr32_to_gray16_b},

  {PIX_FMT_BGRx32, PIX_FMT_RGB24, bgrx32_to_rgb24},
  {PIX_FMT_BGRx32, PIX_FMT_RGBA32, bgrx32_to_rgba32},
  {PIX_FMT_BGRx32, PIX_FMT_YUV420P, bgrx32_to_yuv420p},
  {PIX_FMT_BGRx32, PIX_FMT_YUVA420P, bgrx32_to_yuva420p},
  {PIX_FMT_BGRx32, PIX_FMT_NV12, bgrx32_to_nv12},
  {PIX_FMT_BGRx32, PIX_FMT_NV21, bgrx32_to_nv21},
  {PIX_FMT_BGRx32, PIX_FMT_Y800, bgrx32_to_y800},
  {PIX_FMT_BGRx32, PIX_FMT_Y16, bgrx32_to_y16},
  {PIX_FMT_BGRx32, PIX_FMT_GRAY8, bgrx32_to_gray},
  {PIX_FMT_BGRx32, PIX_FMT_GRAY16_L, bgrx32_to_gray16_l},
  {PIX_FMT_BGRx32, PIX_FMT_GRAY16_B, bgrx32_to_gray16_b},

  {PIX_FMT_BGRA32, PIX_FMT_RGB24, bgra32_to_rgb24},
  {PIX_FMT_BGRA32, PIX_FMT_RGBA32, bgra32_to_rgba32},
  {PIX_FMT_BGRA32, PIX_FMT_YUV420P, bgra32_to_yuv420p},
  {PIX_FMT_BGRA32, PIX_FMT_YUVA420P, bgra32_to_yuva420p},
  {PIX_FMT_BGRA32, PIX_FMT_NV12, bgra32_to_nv12},
  {PIX_FMT_BGRA32, PIX_FMT_NV21, bgra32_to_nv21},
  {PIX_FMT_BGRA32, PIX_FMT_Y800, bgra32_to_y800},
  {PIX_FMT_BGRA32, PIX_FMT_Y16, bgra32_to_y16},
  {PIX_FMT_BGRA32, PIX_FMT_GRAY8, bgra32_to_gray},
  {PIX_FMT_BGRA32, PIX_FMT_GRAY16_L, bgra32_to_gray16_l},
  {PIX_FMT_BGRA32, PIX_FMT_GRAY16_B, bgra32_to_gray16_b},
  {PIX_FMT_BGRA32, PIX_FMT_AYUV4444, bgra32_to_ayuv4444},

  {PIX_FMT_ABGR32, PIX_FMT_RGB24, abgr32_to_rgb24},
  {PIX_FMT_ABGR32, PIX_FMT_RGBA32, abgr32_to_rgba32},
  {PIX_FMT_ABGR32, PIX_FMT_YUV420P, abgr32_to_yuv420p},
  {PIX_FMT_ABGR32, PIX_FMT_YUVA420P, abgr32_to_yuva420p},
  {PIX_FMT_ABGR32, PIX_FMT_NV12, abgr32_to_nv12},
  {PIX_FMT_ABGR32, PIX_FMT_NV21, abgr32_to_nv21},
  {PIX_FMT_ABGR32, PIX_FMT_Y800, abgr32_to_y800},
  {PIX_FMT_ABGR32, PIX_FMT_Y16, abgr32_to_y16},
  {PIX_FMT_ABGR32, PIX_FMT_GRAY8, abgr32_to_gray},
  {PIX_FMT_ABGR32, PIX_FMT_GRAY16_L, abgr32_to_gray16_l},
  {PIX_FMT_ABGR32, PIX_FMT_GRAY16_B, abgr32_to_gray16_b},
  {PIX_FMT_ABGR32, PIX_FMT_AYUV4444, abgr32_to_ayuv4444},

  {PIX_FMT_ARGB32, PIX_FMT_RGB24, argb32_to_rgb24},
  {PIX_FMT_ARGB32, PIX_FMT_RGBA32, argb32_to_rgba32},
  {PIX_FMT_ARGB32, PIX_FMT_YUV420P, argb32_to_yuv420p},
  {PIX_FMT_ARGB32, PIX_FMT_YUVA420P, argb32_to_yuva420p},
  {PIX_FMT_ARGB32, PIX_FMT_NV12, argb32_to_nv12},
  {PIX_FMT_ARGB32, PIX_FMT_NV21, argb32_to_nv21},
  {PIX_FMT_ARGB32, PIX_FMT_Y800, argb32_to_y800},
  {PIX_FMT_ARGB32, PIX_FMT_Y16, argb32_to_y16},
  {PIX_FMT_ARGB32, PIX_FMT_GRAY8, argb32_to_gray},
  {PIX_FMT_ARGB32, PIX_FMT_GRAY16_L, argb32_to_gray16_l},
  {PIX_FMT_ARGB32, PIX_FMT_GRAY16_B, argb32_to_gray16_b},
  {PIX_FMT_ARGB32, PIX_FMT_AYUV4444, argb32_to_ayuv4444},

  {PIX_FMT_RGB555, PIX_FMT_RGB24, rgb555_to_rgb24},
  {PIX_FMT_RGB555, PIX_FMT_RGB32, rgb555_to_rgba32},
  {PIX_FMT_RGB555, PIX_FMT_RGBA32, rgb555_to_rgba32},
  {PIX_FMT_RGB555, PIX_FMT_YUV420P, rgb555_to_yuv420p},
  {PIX_FMT_RGB555, PIX_FMT_YUVA420P, rgb555_to_yuva420p},
  {PIX_FMT_RGB555, PIX_FMT_NV12, rgb555_to_nv12},
  {PIX_FMT_RGB555, PIX_FMT_NV21, rgb555_to_nv21},
  {PIX_FMT_RGB555, PIX_FMT_Y800, rgb555_to_y800},
  {PIX_FMT_RGB555, PIX_FMT_Y16, rgb555_to_y16},
  {PIX_FMT_RGB555, PIX_FMT_GRAY8, rgb555_to_gray},
  {PIX_FMT_RGB555, PIX_FMT_GRAY16_L, rgb555_to_gray16_l},
  {PIX_FMT_RGB555, PIX_FMT_GRAY16_B, rgb555_to_gray16_b},

  {PIX_FMT_RGB565, PIX_FMT_RGB24, rgb565_to_rgb24},
  {PIX_FMT_RGB565, PIX_FMT_YUV420P, rgb565_to_yuv420p},
  {PIX_FMT_RGB565, PIX_FMT_YUVA420P, rgb565_to_yuva420p},
  {PIX_FMT_RGB565, PIX_FMT_NV12, rgb565_to_nv12},
  {PIX_FMT_RGB565, PIX_FMT_NV21, rgb565_to_nv21},
  {PIX_FMT_RGB565, PIX_FMT_Y800, rgb565_to_y800},
  {PIX_FMT_RGB565, PIX_FMT_Y16, rgb565_to_y16},
  {PIX_FMT_RGB565, PIX_FMT_GRAY8, rgb565_to_gray},
  {PIX_FMT_RGB565, PIX_FMT_GRAY16_L, rgb565_to_gray16_l},
  {PIX_FMT_RGB565, PIX_FMT_GRAY16_B, rgb565_to_gray16_b},

  {PIX_FMT_Y800, PIX_FMT_RGB555, y800_to_rgb555},
  {PIX_FMT_Y800, PIX_FMT_RGB565, y800_to_rgb565},
  {PIX_FMT_Y800, PIX_FMT_BGR24, y800_to_bgr24},
  {PIX_FMT_Y800, PIX_FMT_RGB24, y800_to_rgb24},
  {PIX_FMT_Y800, PIX_FMT_RGB32, y800_to_rgb32},
  {PIX_FMT_Y800, PIX_FMT_BGR32, y800_to_bgr32},
  {PIX_FMT_Y800, PIX_FMT_RGB32, y800_to_xrgb32},
  {PIX_FMT_Y800, PIX_FMT_BGR32, y800_to_bgrx32},
  {PIX_FMT_Y800, PIX_FMT_RGBA32, y800_to_rgba32},
  {PIX_FMT_Y800, PIX_FMT_BGRA32, y800_to_bgra32},
  {PIX_FMT_Y800, PIX_FMT_ARGB32, y800_to_argb32},
  {PIX_FMT_Y800, PIX_FMT_ABGR32, y800_to_abgr32},
  {PIX_FMT_Y800, PIX_FMT_Y16, y800_to_y16},

  {PIX_FMT_Y16, PIX_FMT_RGB555, y16_to_rgb555},
  {PIX_FMT_Y16, PIX_FMT_RGB565, y16_to_rgb565},
  {PIX_FMT_Y16, PIX_FMT_BGR24, y16_to_bgr24},
  {PIX_FMT_Y16, PIX_FMT_RGB24, y16_to_rgb24},
  {PIX_FMT_Y16, PIX_FMT_RGB32, y16_to_rgb32},
  {PIX_FMT_Y16, PIX_FMT_BGR32, y16_to_bgr32},
  {PIX_FMT_Y16, PIX_FMT_RGB32, y16_to_xrgb32},
  {PIX_FMT_Y16, PIX_FMT_BGR32, y16_to_bgrx32},
  {PIX_FMT_Y16, PIX_FMT_RGBA32, y16_to_rgba32},
  {PIX_FMT_Y16, PIX_FMT_BGRA32, y16_to_bgra32},
  {PIX_FMT_Y16, PIX_FMT_ARGB32, y16_to_argb32},
  {PIX_FMT_Y16, PIX_FMT_ABGR32, y16_to_abgr32},
  {PIX_FMT_Y16, PIX_FMT_Y800, y16_to_y800},

  {PIX_FMT_GRAY8, PIX_FMT_RGB555, gray_to_rgb555},
  {PIX_FMT_GRAY8, PIX_FMT_RGB565, gray_to_rgb565},
  {PIX_FMT_GRAY8, PIX_FMT_RGB24, gray_to_rgb24},
  {PIX_FMT_GRAY8, PIX_FMT_BGR24, gray_to_bgr24},
  {PIX_FMT_GRAY8, PIX_FMT_RGB32, gray_to_rgb32},
  {PIX_FMT_GRAY8, PIX_FMT_BGR32, gray_to_bgr32},
  {PIX_FMT_GRAY8, PIX_FMT_xRGB32, gray_to_xrgb32},
  {PIX_FMT_GRAY8, PIX_FMT_BGRx32, gray_to_bgrx32},
  {PIX_FMT_GRAY8, PIX_FMT_RGBA32, gray_to_rgba32},
  {PIX_FMT_GRAY8, PIX_FMT_BGRA32, gray_to_bgra32},
  {PIX_FMT_GRAY8, PIX_FMT_ARGB32, gray_to_argb32},
  {PIX_FMT_GRAY8, PIX_FMT_ABGR32, gray_to_abgr32},
  {PIX_FMT_GRAY8, PIX_FMT_MONOWHITE, gray_to_monowhite},
  {PIX_FMT_GRAY8, PIX_FMT_MONOBLACK, gray_to_monoblack},
  {PIX_FMT_GRAY8, PIX_FMT_GRAY16_L, gray_to_gray16_l},
  {PIX_FMT_GRAY8, PIX_FMT_GRAY16_B, gray_to_gray16_b},

  {PIX_FMT_MONOWHITE, PIX_FMT_GRAY8, monowhite_to_gray},

  {PIX_FMT_MONOBLACK, PIX_FMT_GRAY8, monoblack_to_gray},

  {PIX_FMT_GRAY16_L, PIX_FMT_GRAY8, gray16_l_to_gray},
  {PIX_FMT_GRAY16_L, PIX_FMT_RGB555, gray16_l_to_rgb555},
  {PIX_FMT_GRAY16_L, PIX_FMT_RGB565, gray16_l_to_rgb565},
  {PIX_FMT_GRAY16_L, PIX_FMT_BGR24, gray16_l_to_bgr24},
  {PIX_FMT_GRAY16_L, PIX_FMT_RGB24, gray16_l_to_rgb24},
  {PIX_FMT_GRAY16_L, PIX_FMT_BGR32, gray16_l_to_bgr32},
  {PIX_FMT_GRAY16_L, PIX_FMT_RGB32, gray16_l_to_rgb32},
  {PIX_FMT_GRAY16_L, PIX_FMT_xRGB32, gray16_l_to_xrgb32},
  {PIX_FMT_GRAY16_L, PIX_FMT_BGRx32, gray16_l_to_bgrx32},
  {PIX_FMT_GRAY16_L, PIX_FMT_ABGR32, gray16_l_to_abgr32},
  {PIX_FMT_GRAY16_L, PIX_FMT_ARGB32, gray16_l_to_argb32},
  {PIX_FMT_GRAY16_L, PIX_FMT_BGRA32, gray16_l_to_bgra32},
  {PIX_FMT_GRAY16_L, PIX_FMT_RGBA32, gray16_l_to_rgba32},
  {PIX_FMT_GRAY16_L, PIX_FMT_GRAY16_B, gray16_b_to_gray16_l},

  {PIX_FMT_GRAY16_B, PIX_FMT_GRAY8, gray16_b_to_gray},
  {PIX_FMT_GRAY16_B, PIX_FMT_RGB555, gray16_b_to_rgb555},
  {PIX_FMT_GRAY16_B, PIX_FMT_RGB565, gray16_b_to_rgb565},
  {PIX_FMT_GRAY16_B, PIX_FMT_BGR24, gray16_b_to_bgr24},
  {PIX_FMT_GRAY16_B, PIX_FMT_RGB24, gray16_b_to_rgb24},
  {PIX_FMT_GRAY16_B, PIX_FMT_BGR32, gray16_b_to_bgr32},
  {PIX_FMT_GRAY16_B, PIX_FMT_RGB32, gray16_b_to_rgb32},
  {PIX_FMT_GRAY16_B, PIX_FMT_xRGB32, gray16_b_to_xrgb32},
  {PIX_FMT_GRAY16_B, PIX_FMT_BGRx32, gray16_b_to_bgrx32},
  {PIX_FMT_GRAY16_B, PIX_FMT_ABGR32, gray16_b_to_abgr32},
  {PIX_FMT_GRAY16_B, PIX_FMT_ARGB32, gray16_b_to_argb32},
  {PIX_FMT_GRAY16_B, PIX_FMT_BGRA32, gray16_b_to_bgra32},
  {PIX_FMT_GRAY16_B, PIX_FMT_RGBA32, gray16_b_to_rgba32},
  {PIX_FMT_GRAY16_B, PIX_FMT_GRAY16_L, gray16_b_to_gray16_l},

  {PIX_FMT_PAL8, PIX_FMT_RGB555, pal8_to_rgb555},
  {PIX_FMT_PAL8, PIX_FMT_RGB565, pal8_to_rgb565},
  {PIX_FMT_PAL8, PIX_FMT_BGR24, pal8_to_bgr24},
  {PIX_FMT_PAL8, PIX_FMT_RGB24, pal8_to_rgb24},
  {PIX_FMT_PAL8, PIX_FMT_RGB32, pal8_to_rgb32},
  {PIX_FMT_PAL8, PIX_FMT_BGR32, pal8_to_bgr32},
  {PIX_FMT_PAL8, PIX_FMT_xRGB32, pal8_to_xrgb32},
  {PIX_FMT_PAL8, PIX_FMT_BGRx32, pal8_to_bgrx32},
  {PIX_FMT_PAL8, PIX_FMT_RGBA32, pal8_to_rgba32},
  {PIX_FMT_PAL8, PIX_FMT_BGRA32, pal8_to_bgra32},
  {PIX_FMT_PAL8, PIX_FMT_ARGB32, pal8_to_argb32},
  {PIX_FMT_PAL8, PIX_FMT_ABGR32, pal8_to_abgr32},

  {PIX_FMT_UYVY411, PIX_FMT_YUV411P, uyvy411_to_yuv411p},
  {PIX_FMT_YUV411P, PIX_FMT_UYVY411, yuv411p_to_uyvy411},

  {PIX_FMT_V308, PIX_FMT_RGB24, v308_to_rgb24},

  {PIX_FMT_AYUV4444, PIX_FMT_RGBA32, ayuv4444_to_rgba32},
  {PIX_FMT_AYUV4444, PIX_FMT_ARGB32, ayuv4444_to_argb32},
  {PIX_FMT_AYUV4444, PIX_FMT_BGRA32, ayuv4444_to_bgra32},
  {PIX_FMT_AYUV4444, PIX_FMT_ABGR32, ayuv4444_to_abgr32},
  {PIX_FMT_AYUV4444, PIX_FMT_RGB24, ayuv4444_to_rgb24},
  {PIX_FMT_AYUV4444, PIX_FMT_YUVA420P, ayuv4444_to_yuva420p},

  {PIX_FMT_YUVA420P, PIX_FMT_YUV420P, yuva420p_to_yuv420p},
  {PIX_FMT_YUVA420P, PIX_FMT_YUV422, yuva420p_to_yuv422},
  {PIX_FMT_YUVA420P, PIX_FMT_AYUV4444, yuva420p_to_ayuv4444},
  {PIX_FMT_YUVA420P, PIX_FMT_RGB555, yuva420p_to_rgb555},
  {PIX_FMT_YUVA420P, PIX_FMT_RGB565, yuva420p_to_rgb565},
  {PIX_FMT_YUVA420P, PIX_FMT_BGR24, yuva420p_to_bgr24},
  {PIX_FMT_YUVA420P, PIX_FMT_RGB24, yuva420p_to_rgb24},
  {PIX_FMT_YUVA420P, PIX_FMT_RGB32, yuva420p_to_rgb32},
  {PIX_FMT_YUVA420P, PIX_FMT_BGR32, yuva420p_to_bgr32},
  {PIX_FMT_YUVA420P, PIX_FMT_xRGB32, yuva420p_to_xrgb32},
  {PIX_FMT_YUVA420P, PIX_FMT_BGRx32, yuva420p_to_bgrx32},
  {PIX_FMT_YUVA420P, PIX_FMT_RGBA32, yuva420p_to_rgba32},
  {PIX_FMT_YUVA420P, PIX_FMT_BGRA32, yuva420p_to_bgra32},
  {PIX_FMT_YUVA420P, PIX_FMT_ARGB32, yuva420p_to_argb32},
  {PIX_FMT_YUVA420P, PIX_FMT_ABGR32, yuva420p_to_abgr32},
};

static ConvertEntry *
get_convert_table_entry (int src_pix_fmt, int dst_pix_fmt)
{
  int i;

  for (i = 0; i < sizeof (convert_table) / sizeof (convert_table[0]); i++) {
    if (convert_table[i].src == src_pix_fmt &&
        convert_table[i].dest == dst_pix_fmt) {
      return convert_table + i;
    }
  }

  return NULL;
}

static int
avpicture_alloc (AVPicture * picture, int pix_fmt, int width, int height,
    int interlaced)
{
  unsigned int size;
  void *ptr;

  size = avpicture_get_size (pix_fmt, width, height);
  ptr = av_malloc (size);
  if (!ptr)
    goto fail;
  gst_ffmpegcsp_avpicture_fill (picture, ptr, pix_fmt, width, height,
      interlaced);
  return 0;
fail:
  memset (picture, 0, sizeof (AVPicture));
  return -1;
}

static void
avpicture_free (AVPicture * picture)
{
  av_free (picture->data[0]);
}

/* return true if yuv planar */
static inline int
is_yuv_planar (PixFmtInfo * ps)
{
  return (ps->color_type == FF_COLOR_YUV ||
      ps->color_type == FF_COLOR_YUV_JPEG) && ps->pixel_type == FF_PIXEL_PLANAR;
}

/* XXX: always use linesize. Return -1 if not supported */
int
img_convert (AVPicture * dst, int dst_pix_fmt,
    const AVPicture * src, int src_pix_fmt, int src_width, int src_height)
{
  static int inited;
  int i, ret, dst_width, dst_height, int_pix_fmt;
  PixFmtInfo *src_pix, *dst_pix;
  ConvertEntry *ce;
  AVPicture tmp1, *tmp = &tmp1;

  if (G_UNLIKELY (src_width <= 0 || src_height <= 0))
    return 0;

  if (G_UNLIKELY (!inited)) {
    inited = 1;
    img_convert_init ();
  }

  dst_width = src_width;
  dst_height = src_height;

  dst_pix = get_pix_fmt_info (dst_pix_fmt);
  src_pix = get_pix_fmt_info (src_pix_fmt);
  if (G_UNLIKELY (src_pix_fmt == dst_pix_fmt)) {
    /* no conversion needed: just copy */
    img_copy (dst, src, dst_pix_fmt, dst_width, dst_height);
    return 0;
  }

  ce = get_convert_table_entry (src_pix_fmt, dst_pix_fmt);
  if (ce && ce->convert) {
    /* specific conversion routine */
    ce->convert (dst, src, dst_width, dst_height);
    return 0;
  }

  /* gray to YUV */
  if (is_yuv_planar (dst_pix) && dst_pix_fmt != PIX_FMT_Y16
      && src_pix_fmt == PIX_FMT_GRAY8) {
    int w, h, y;
    uint8_t *d;

    if (dst_pix->color_type == FF_COLOR_YUV_JPEG) {
      img_copy_plane (dst->data[0], dst->linesize[0],
          src->data[0], src->linesize[0], dst_width, dst_height);
    } else {
      img_apply_table (dst->data[0], dst->linesize[0],
          src->data[0], src->linesize[0],
          dst_width, dst_height, y_jpeg_to_ccir);
    }
    /* fill U and V with 128 */
    w = dst_width;
    h = dst_height;
    w >>= dst_pix->x_chroma_shift;
    h >>= dst_pix->y_chroma_shift;
    for (i = 1; i <= 2; i++) {
      d = dst->data[i];
      if (!d)
        continue;
      for (y = 0; y < h; y++) {
        memset (d, 128, w);
        d += dst->linesize[i];
      }
    }
    return 0;
  }

  /* YUV to gray */
  if (is_yuv_planar (src_pix) && src_pix_fmt != PIX_FMT_Y16
      && dst_pix_fmt == PIX_FMT_GRAY8) {
    if (src_pix->color_type == FF_COLOR_YUV_JPEG) {
      img_copy_plane (dst->data[0], dst->linesize[0],
          src->data[0], src->linesize[0], dst_width, dst_height);
    } else {
      img_apply_table (dst->data[0], dst->linesize[0],
          src->data[0], src->linesize[0],
          dst_width, dst_height, y_ccir_to_jpeg);
    }
    return 0;
  }

  /* YUV to YUV planar */
  if (is_yuv_planar (dst_pix) && is_yuv_planar (src_pix) &&
      dst_pix->depth == src_pix->depth) {
    int x_shift, y_shift, xy_shift;
    void (*resize_func) (uint8_t * dst, int dst_wrap, int dst_width,
        int dst_height, const uint8_t * src, int src_wrap, int src_width,
        int src_height);

    x_shift = (dst_pix->x_chroma_shift - src_pix->x_chroma_shift);
    y_shift = (dst_pix->y_chroma_shift - src_pix->y_chroma_shift);
    xy_shift = ((x_shift & 0xf) << 4) | (y_shift & 0xf);

    /* there must be filters for conversion at least from and to
       YUV444 format */
    switch (xy_shift) {
      case 0x00:
        resize_func = img_copy_plane_resize;
        break;
      case 0x10:
        resize_func = shrink21;
        break;
      case 0x20:
        resize_func = shrink41;
        break;
      case 0x01:
        resize_func = shrink12;
        break;
      case 0x11:
        resize_func = shrink22;
        break;
      case 0x22:
        resize_func = shrink44;
        break;
      case 0xf0:
        resize_func = grow21;
        break;
      case 0xe0:
        resize_func = grow41;
        break;
      case 0xff:
        resize_func = grow22;
        break;
      case 0xee:
        resize_func = grow44;
        break;
      case 0xf1:
        resize_func = conv411;
        break;
      default:
        /* currently not handled */
        goto no_chroma_filter;
    }

    img_copy_plane (dst->data[0], dst->linesize[0],
        src->data[0], src->linesize[0], dst_width, dst_height);

#define GEN_MASK(x) ((1<<(x))-1)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))

    for (i = 1; i <= 2; i++) {
      gint w, h;
      gint s_w, s_h;

      w = DIV_ROUND_UP_X (dst_width, dst_pix->x_chroma_shift);
      h = DIV_ROUND_UP_X (dst_height, dst_pix->y_chroma_shift);

      s_w = DIV_ROUND_UP_X (src_width, src_pix->x_chroma_shift);
      s_h = DIV_ROUND_UP_X (src_height, src_pix->y_chroma_shift);

      if (src->data[i] != NULL && dst->data[i] != NULL) {
        resize_func (dst->data[i], dst->linesize[i], w, h,
            src->data[i], src->linesize[i], s_w, s_h);
      } else if (dst->data[i] != NULL) {
        memset (dst->data[i], 128, dst->linesize[i] * h);
      }
    }
    /* if yuv color space conversion is needed, we do it here on
       the destination image */
    if (dst_pix->color_type != src_pix->color_type) {
      const uint8_t *y_table, *c_table;

      if (dst_pix->color_type == FF_COLOR_YUV) {
        y_table = y_jpeg_to_ccir;
        c_table = c_jpeg_to_ccir;
      } else {
        y_table = y_ccir_to_jpeg;
        c_table = c_ccir_to_jpeg;
      }
      img_apply_table (dst->data[0], dst->linesize[0],
          dst->data[0], dst->linesize[0], dst_width, dst_height, y_table);

      for (i = 1; i <= 2; i++)
        img_apply_table (dst->data[i], dst->linesize[i],
            dst->data[i], dst->linesize[i],
            dst_width >> dst_pix->x_chroma_shift,
            dst_height >> dst_pix->y_chroma_shift, c_table);
    }
    return 0;
  }
no_chroma_filter:
  GST_CAT_INFO (ffmpegcolorspace_performance,
      "no direct path to convert colorspace from %s -> %s", src_pix->name,
      dst_pix->name);

  /* try to use an intermediate format */
  if (src_pix_fmt == PIX_FMT_YUV422 || dst_pix_fmt == PIX_FMT_YUV422) {
    /* specific case: convert to YUV422P first */
    int_pix_fmt = PIX_FMT_YUV422P;
  } else if (src_pix_fmt == PIX_FMT_UYVY422 || dst_pix_fmt == PIX_FMT_UYVY422 ||
      src_pix_fmt == PIX_FMT_YVYU422 || dst_pix_fmt == PIX_FMT_YVYU422) {
    /* specific case: convert to YUV422P first */
    int_pix_fmt = PIX_FMT_YUV422P;
  } else if (src_pix_fmt == PIX_FMT_UYVY411 || dst_pix_fmt == PIX_FMT_UYVY411) {
    /* specific case: convert to YUV411P first */
    int_pix_fmt = PIX_FMT_YUV411P;
  } else if ((src_pix->color_type == FF_COLOR_GRAY &&
          src_pix_fmt != PIX_FMT_GRAY8) ||
      (dst_pix->color_type == FF_COLOR_GRAY && dst_pix_fmt != PIX_FMT_GRAY8)) {
    /* gray8 is the normalized format */
    int_pix_fmt = PIX_FMT_GRAY8;
  } else if (src_pix_fmt == PIX_FMT_Y16 || dst_pix_fmt == PIX_FMT_Y16) {
    /* y800 is the normalized format */
    int_pix_fmt = PIX_FMT_Y800;
  } else if ((is_yuv_planar (src_pix) &&
          src_pix_fmt != PIX_FMT_YUV444P && src_pix_fmt != PIX_FMT_YUVJ444P)) {
    /* yuv444 is the normalized format */
    if (src_pix->color_type == FF_COLOR_YUV_JPEG)
      int_pix_fmt = PIX_FMT_YUVJ444P;
    else
      int_pix_fmt = PIX_FMT_YUV444P;
  } else if ((is_yuv_planar (dst_pix) &&
          dst_pix_fmt != PIX_FMT_YUV444P && dst_pix_fmt != PIX_FMT_YUVJ444P)) {
    /* yuv444 is the normalized format */
    if (dst_pix->color_type == FF_COLOR_YUV_JPEG)
      int_pix_fmt = PIX_FMT_YUVJ444P;
    else
      int_pix_fmt = PIX_FMT_YUV444P;
  } else {
    /* the two formats are rgb or gray8 or yuv[j]444p */
    if (src_pix->is_alpha && dst_pix->is_alpha)
      int_pix_fmt = PIX_FMT_RGBA32;
    else
      int_pix_fmt = PIX_FMT_RGB24;
  }
  if (avpicture_alloc (tmp, int_pix_fmt, dst_width, dst_height,
          dst->interlaced) < 0)
    return -1;
  ret = -1;
  if (img_convert (tmp, int_pix_fmt,
          src, src_pix_fmt, src_width, src_height) < 0)
    goto fail1;

  if (img_convert (dst, dst_pix_fmt,
          tmp, int_pix_fmt, dst_width, dst_height) < 0)
    goto fail1;
  ret = 0;
fail1:
  avpicture_free (tmp);
  return ret;
}

/* NOTE: we scan all the pixels to have an exact information */
static int
get_alpha_info_pal8 (const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  int src_wrap, ret, x, y;
  unsigned int a;
  uint32_t *palette = (uint32_t *) src->data[1];

  p = src->data[0];
  src_wrap = src->linesize[0] - width;
  ret = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      a = palette[p[0]] >> 24;
      if (a == 0x00) {
        ret |= FF_ALPHA_TRANSP;
      } else if (a != 0xff) {
        ret |= FF_ALPHA_SEMI_TRANSP;
      }
      p++;
    }
    p += src_wrap;
  }
  return ret;
}

/**
 * Tell if an image really has transparent alpha values.
 * @return ored mask of FF_ALPHA_xxx constants
 */
int
img_get_alpha_info (const AVPicture * src, int pix_fmt, int width, int height)
{
  const PixFmtInfo *pf;
  int ret;

  pf = get_pix_fmt_info (pix_fmt);
  /* no alpha can be represented in format */
  if (!pf->is_alpha)
    return 0;
  switch (pix_fmt) {
    case PIX_FMT_RGB32:
      ret = get_alpha_info_rgb32 (src, width, height);
      break;
    case PIX_FMT_BGR32:
      ret = get_alpha_info_bgr32 (src, width, height);
      break;
    case PIX_FMT_xRGB32:
      ret = get_alpha_info_xrgb32 (src, width, height);
      break;
    case PIX_FMT_BGRx32:
      ret = get_alpha_info_bgrx32 (src, width, height);
      break;
    case PIX_FMT_RGBA32:
      ret = get_alpha_info_rgba32 (src, width, height);
      break;
    case PIX_FMT_BGRA32:
      ret = get_alpha_info_bgra32 (src, width, height);
      break;
    case PIX_FMT_ARGB32:
      ret = get_alpha_info_argb32 (src, width, height);
      break;
    case PIX_FMT_ABGR32:
      ret = get_alpha_info_abgr32 (src, width, height);
      break;
    case PIX_FMT_RGB555:
      ret = get_alpha_info_rgb555 (src, width, height);
      break;
    case PIX_FMT_PAL8:
      ret = get_alpha_info_pal8 (src, width, height);
      break;
    default:
      /* we do not know, so everything is indicated */
      ret = FF_ALPHA_TRANSP | FF_ALPHA_SEMI_TRANSP;
      break;
  }
  return ret;
}

#ifdef HAVE_MMX
#define DEINT_INPLACE_LINE_LUM \
                    movd_m2r(lum_m4[0],mm0);\
                    movd_m2r(lum_m3[0],mm1);\
                    movd_m2r(lum_m2[0],mm2);\
                    movd_m2r(lum_m1[0],mm3);\
                    movd_m2r(lum[0],mm4);\
                    punpcklbw_r2r(mm7,mm0);\
                    movd_r2m(mm2,lum_m4[0]);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    punpcklbw_r2r(mm7,mm3);\
                    punpcklbw_r2r(mm7,mm4);\
                    paddw_r2r(mm3,mm1);\
                    psllw_i2r(1,mm2);\
                    paddw_r2r(mm4,mm0);\
                    psllw_i2r(2,mm1);\
                    paddw_r2r(mm6,mm2);\
                    paddw_r2r(mm2,mm1);\
                    psubusw_r2r(mm0,mm1);\
                    psrlw_i2r(3,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,lum_m2[0]);

#define DEINT_LINE_LUM \
                    movd_m2r(lum_m4[0],mm0);\
                    movd_m2r(lum_m3[0],mm1);\
                    movd_m2r(lum_m2[0],mm2);\
                    movd_m2r(lum_m1[0],mm3);\
                    movd_m2r(lum[0],mm4);\
                    punpcklbw_r2r(mm7,mm0);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    punpcklbw_r2r(mm7,mm3);\
                    punpcklbw_r2r(mm7,mm4);\
                    paddw_r2r(mm3,mm1);\
                    psllw_i2r(1,mm2);\
                    paddw_r2r(mm4,mm0);\
                    psllw_i2r(2,mm1);\
                    paddw_r2r(mm6,mm2);\
                    paddw_r2r(mm2,mm1);\
                    psubusw_r2r(mm0,mm1);\
                    psrlw_i2r(3,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,dst[0]);
#endif

/* filter parameters: [-1 4 2 4 -1] // 8 */
#if 0
static void
deinterlace_line (uint8_t * dst,
    const uint8_t * lum_m4, const uint8_t * lum_m3,
    const uint8_t * lum_m2, const uint8_t * lum_m1,
    const uint8_t * lum, int size)
{
#ifndef HAVE_MMX
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  int sum;

  for (; size > 0; size--) {
    sum = -lum_m4[0];
    sum += lum_m3[0] << 2;
    sum += lum_m2[0] << 1;
    sum += lum_m1[0] << 2;
    sum += -lum[0];
    dst[0] = cm[(sum + 4) >> 3];
    lum_m4++;
    lum_m3++;
    lum_m2++;
    lum_m1++;
    lum++;
    dst++;
  }
#else

  {
    mmx_t rounder;

    rounder.uw[0] = 4;
    rounder.uw[1] = 4;
    rounder.uw[2] = 4;
    rounder.uw[3] = 4;
    pxor_r2r (mm7, mm7);
    movq_m2r (rounder, mm6);
  }
  for (; size > 3; size -= 4) {
    DEINT_LINE_LUM lum_m4 += 4;

    lum_m3 += 4;
    lum_m2 += 4;
    lum_m1 += 4;
    lum += 4;
    dst += 4;
  }
#endif
}

static void
deinterlace_line_inplace (uint8_t * lum_m4, uint8_t * lum_m3, uint8_t * lum_m2,
    uint8_t * lum_m1, uint8_t * lum, int size)
{
#ifndef HAVE_MMX
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  int sum;

  for (; size > 0; size--) {
    sum = -lum_m4[0];
    sum += lum_m3[0] << 2;
    sum += lum_m2[0] << 1;
    lum_m4[0] = lum_m2[0];
    sum += lum_m1[0] << 2;
    sum += -lum[0];
    lum_m2[0] = cm[(sum + 4) >> 3];
    lum_m4++;
    lum_m3++;
    lum_m2++;
    lum_m1++;
    lum++;
  }
#else

  {
    mmx_t rounder;

    rounder.uw[0] = 4;
    rounder.uw[1] = 4;
    rounder.uw[2] = 4;
    rounder.uw[3] = 4;
    pxor_r2r (mm7, mm7);
    movq_m2r (rounder, mm6);
  }
  for (; size > 3; size -= 4) {
    DEINT_INPLACE_LINE_LUM lum_m4 += 4;

    lum_m3 += 4;
    lum_m2 += 4;
    lum_m1 += 4;
    lum += 4;
  }
#endif
}
#endif

/* deinterlacing : 2 temporal taps, 3 spatial taps linear filter. The
   top field is copied as is, but the bottom field is deinterlaced
   against the top field. */
#if 0
static void
deinterlace_bottom_field (uint8_t * dst, int dst_wrap,
    const uint8_t * src1, int src_wrap, int width, int height)
{
  const uint8_t *src_m2, *src_m1, *src_0, *src_p1, *src_p2;
  int y;

  src_m2 = src1;
  src_m1 = src1;
  src_0 = &src_m1[src_wrap];
  src_p1 = &src_0[src_wrap];
  src_p2 = &src_p1[src_wrap];
  for (y = 0; y < (height - 2); y += 2) {
    memcpy (dst, src_m1, width);
    dst += dst_wrap;
    deinterlace_line (dst, src_m2, src_m1, src_0, src_p1, src_p2, width);
    src_m2 = src_0;
    src_m1 = src_p1;
    src_0 = src_p2;
    src_p1 += 2 * src_wrap;
    src_p2 += 2 * src_wrap;
    dst += dst_wrap;
  }
  memcpy (dst, src_m1, width);
  dst += dst_wrap;
  /* do last line */
  deinterlace_line (dst, src_m2, src_m1, src_0, src_0, src_0, width);
}

static void
deinterlace_bottom_field_inplace (uint8_t * src1, int src_wrap,
    int width, int height)
{
  uint8_t *src_m1, *src_0, *src_p1, *src_p2;
  int y;
  uint8_t *buf;

  buf = (uint8_t *) av_malloc (width);

  src_m1 = src1;
  memcpy (buf, src_m1, width);
  src_0 = &src_m1[src_wrap];
  src_p1 = &src_0[src_wrap];
  src_p2 = &src_p1[src_wrap];
  for (y = 0; y < (height - 2); y += 2) {
    deinterlace_line_inplace (buf, src_m1, src_0, src_p1, src_p2, width);
    src_m1 = src_p1;
    src_0 = src_p2;
    src_p1 += 2 * src_wrap;
    src_p2 += 2 * src_wrap;
  }
  /* do last line */
  deinterlace_line_inplace (buf, src_m1, src_0, src_0, src_0, width);
  av_free (buf);
}
#endif

/* deinterlace - if not supported return -1 */
#if 0
static int
avpicture_deinterlace (AVPicture * dst, const AVPicture * src,
    int pix_fmt, int width, int height)
{
  int i;

  if (pix_fmt != PIX_FMT_YUV420P &&
      pix_fmt != PIX_FMT_YUV422P &&
      pix_fmt != PIX_FMT_YUV444P && pix_fmt != PIX_FMT_YUV411P)
    return -1;
  if ((width & 3) != 0 || (height & 3) != 0)
    return -1;

  for (i = 0; i < 3; i++) {
    if (i == 1) {
      switch (pix_fmt) {
        case PIX_FMT_YUV420P:
          width >>= 1;
          height >>= 1;
          break;
        case PIX_FMT_YUV422P:
          width >>= 1;
          break;
        case PIX_FMT_YUV411P:
          width >>= 2;
          break;
        default:
          break;
      }
    }
    if (src == dst) {
      deinterlace_bottom_field_inplace (dst->data[i], dst->linesize[i],
          width, height);
    } else {
      deinterlace_bottom_field (dst->data[i], dst->linesize[i],
          src->data[i], src->linesize[i], width, height);
    }
  }
#ifdef HAVE_MMX
  emms ();
#endif
  return 0;
}
#endif

#undef FIX
