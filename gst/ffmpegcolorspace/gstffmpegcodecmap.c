/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (c) 2002-2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <gst/gst.h>
#include <avcodec.h>
#include <string.h>

#include "gstffmpegcodecmap.h"

/* this macro makes a caps width fixed or unfixed width/height
 * properties depending on whether we've got a context.
 *
 * See below for why we use this.
 */

#define GST_FF_VID_CAPS_NEW(mimetype, props...)			\
    gst_caps_new_simple (mimetype,			      	\
	"width",     GST_TYPE_INT_RANGE, 16, 4096,      	\
	"height",    GST_TYPE_INT_RANGE, 16, 4096,	      	\
	"framerate", GST_TYPE_DOUBLE_RANGE, 0., G_MAXDOUBLE,	\
	##props, NULL)

/* Convert a FFMPEG Pixel Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_pixfmt_to_caps (enum PixelFormat pix_fmt)
{
  GstCaps *caps = NULL;

  int bpp = 0, depth = 0, endianness = 0;
  gulong g_mask = 0, r_mask = 0, b_mask = 0;
  guint32 fmt = 0;

  switch (pix_fmt) {
    case PIX_FMT_YUV420P:
      fmt = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    case PIX_FMT_YUV422:
      fmt = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case PIX_FMT_RGB24:
      bpp = depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0xff0000;
      g_mask = 0x00ff00;
      b_mask = 0x0000ff;
      break;
    case PIX_FMT_BGR24:
      bpp = depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x0000ff;
      g_mask = 0x00ff00;
      b_mask = 0xff0000;
      break;
    case PIX_FMT_YUV422P:
      fmt = GST_MAKE_FOURCC ('Y', '4', '2', 'B');
      break;
    case PIX_FMT_YUV444P:
      /* .. */
      break;
    case PIX_FMT_RGBA32:
      bpp = 32;
      depth = 24;
      endianness = G_BIG_ENDIAN;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      r_mask = 0x00ff0000;
      g_mask = 0x0000ff00;
      b_mask = 0x000000ff;
#else
      r_mask = 0x0000ff00;
      g_mask = 0x00ff0000;
      b_mask = 0xff000000;
#endif
      break;
    case PIX_FMT_YUV410P:
      fmt = GST_MAKE_FOURCC ('Y', 'U', 'V', '9');
      break;
    case PIX_FMT_YUV411P:
      fmt = GST_MAKE_FOURCC ('Y', '4', '1', 'B');
      break;
    case PIX_FMT_RGB565:
      bpp = depth = 16;
      endianness = G_BYTE_ORDER;
      r_mask = 0xf800;
      g_mask = 0x07e0;
      b_mask = 0x001f;
      break;
    case PIX_FMT_RGB555:
      bpp = 16;
      depth = 15;
      endianness = G_BYTE_ORDER;
      r_mask = 0x7c00;
      g_mask = 0x03e0;
      b_mask = 0x001f;
      break;
    default:
      /* give up ... */
      break;
  }

  if (bpp != 0) {
    caps = GST_FF_VID_CAPS_NEW ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, bpp,
        "depth", G_TYPE_INT, depth,
        "red_mask", G_TYPE_INT, r_mask,
        "green_mask", G_TYPE_INT, g_mask,
        "blue_mask", G_TYPE_INT, b_mask,
        "endianness", G_TYPE_INT, endianness, NULL);
  } else if (fmt) {
    caps = GST_FF_VID_CAPS_NEW ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fmt, NULL);
  }

  if (caps != NULL) {
    char *str = gst_caps_to_string (caps);

    GST_DEBUG ("caps for pix_fmt=%d: %s", pix_fmt, str);
    g_free (str);
  } else {
    GST_WARNING ("No caps found for pix_fmt=%d", pix_fmt);
  }

  return caps;
}

/* Convert a FFMPEG codec Type and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * CodecType is primarily meant for uncompressed data GstCaps!
 */

GstCaps *
gst_ffmpeg_pix_fmt_to_caps (void)
{
  GstCaps *caps, *temp;
  enum PixelFormat i;

  caps = gst_caps_new_empty ();
  for (i = 0; i < PIX_FMT_NB; i++) {
    temp = gst_ffmpeg_pixfmt_to_caps (i);
    if (temp != NULL) {
      gst_caps_append (caps, temp);
    }
  }

  return caps;
}

/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 * and other video properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

enum PixelFormat
gst_ffmpeg_caps_to_pix_fmt (const GstCaps * caps,
    int *width, int *height, double *framerate)
{
  GstStructure *structure;
  enum PixelFormat pix_fmt = PIX_FMT_NB;

  g_return_val_if_fail (gst_caps_get_size (caps) == 1, PIX_FMT_NB);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", width);
  gst_structure_get_int (structure, "height", height);
  gst_structure_get_double (structure, "framerate", framerate);

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
      switch (fourcc) {
        case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
          pix_fmt = PIX_FMT_YUV422;
          break;
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          pix_fmt = PIX_FMT_YUV420P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
          pix_fmt = PIX_FMT_YUV411P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
          pix_fmt = PIX_FMT_YUV422P;
          break;
        case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
          pix_fmt = PIX_FMT_YUV410P;
          break;
#if 0
        case FIXME:
          pix_fmt = PIX_FMT_YUV444P;
          break;
#endif
      }
    }
  } else if (strcmp (gst_structure_get_name (structure),
          "video/x-raw-rgb") == 0) {
    gint bpp = 0, rmask = 0, endianness = 0;

    if (gst_structure_get_int (structure, "bpp", &bpp) &&
        gst_structure_get_int (structure, "endianness", &endianness) &&
        gst_structure_get_int (structure, "red_mask", &rmask)) {
      switch (bpp) {
        case 32:
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
          if (rmask == 0x00ff0000)
#else
          if (rmask == 0x0000ff00)
#endif
            pix_fmt = PIX_FMT_RGBA32;
          break;
        case 24:
          if (rmask == 0x0000FF)
            pix_fmt = PIX_FMT_BGR24;
          else
            pix_fmt = PIX_FMT_RGB24;
          break;
        case 16:
          if (endianness == G_BYTE_ORDER)
            pix_fmt = PIX_FMT_RGB565;
          break;
        case 15:
          if (endianness == G_BYTE_ORDER)
            pix_fmt = PIX_FMT_RGB555;
          break;
        default:
          /* nothing */
          break;
      }
    }
  }

  return pix_fmt;
}
