/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (c) 2002-2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

/*
 * Read a palette from a caps.
 */

static void
gst_ffmpeg_get_palette (const GstCaps * caps, AVCodecContext * context)
{
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *palette_v;
  const GstBuffer *palette;

  /* do we have a palette? */
  if ((palette_v = gst_structure_get_value (str, "palette_data")) && context) {
    palette = g_value_get_boxed (palette_v);
    if (GST_BUFFER_SIZE (palette) >= 256 * 4) {
      if (context->palctrl)
        av_free (context->palctrl);
      context->palctrl = av_malloc (sizeof (AVPaletteControl));
      context->palctrl->palette_changed = 1;
      memcpy (context->palctrl->palette, GST_BUFFER_DATA (palette),
          AVPALETTE_SIZE);
    }
  }
}

static void
gst_ffmpeg_set_palette (GstCaps * caps, AVCodecContext * context)
{
  if (context->palctrl) {
    GstBuffer *palette = gst_buffer_new_and_alloc (256 * 4);

    memcpy (GST_BUFFER_DATA (palette), context->palctrl->palette,
        AVPALETTE_SIZE);
    gst_caps_set_simple (caps, "palette_data", GST_TYPE_BUFFER, palette, NULL);
  }
}

/* this macro makes a caps width fixed or unfixed width/height
 * properties depending on whether we've got a context.
 *
 * See below for why we use this.
 *
 * We should actually do this stuff at the end, like in riff-media.c,
 * but I'm too lazy today. Maybe later.
 */

#define GST_FF_VID_CAPS_NEW(mimetype, ...)			\
    (context != NULL) ?						\
    gst_caps_new_simple (mimetype,			      	\
	"width",     G_TYPE_INT,   context->width,	      	\
	"height",    G_TYPE_INT,   context->height,	  	\
	"framerate", G_TYPE_DOUBLE, 1. * context->frame_rate /  \
				   context->frame_rate_base,    \
	__VA_ARGS__, NULL)  					\
    :	  							\
    gst_caps_new_simple (mimetype,			      	\
	"width",     GST_TYPE_INT_RANGE, 16, 4096,      	\
	"height",    GST_TYPE_INT_RANGE, 16, 4096,	      	\
	"framerate", GST_TYPE_DOUBLE_RANGE, 0., G_MAXDOUBLE,	\
	__VA_ARGS__, NULL)

/* same for audio - now with channels/sample rate
 */

#define GST_FF_AUD_CAPS_NEW(mimetype, ...)			\
    (context != NULL) ?					      	\
    gst_caps_new_simple (mimetype,	      			\
	"rate", G_TYPE_INT, context->sample_rate,		\
	"channels", G_TYPE_INT, context->channels,		\
	__VA_ARGS__, NULL)					\
    :								\
    gst_caps_new_simple (mimetype,	      			\
	__VA_ARGS__, NULL)

/* Convert a FFMPEG Pixel Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_pixfmt_to_caps (enum PixelFormat pix_fmt, AVCodecContext * context)
{
  GstCaps *caps = NULL;

  int bpp = 0, depth = 0, endianness = 0;
  gulong g_mask = 0, r_mask = 0, b_mask = 0, a_mask = 0;
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
    case PIX_FMT_RGB32:
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
    case PIX_FMT_RGBA32:
      bpp = 32;
      depth = 32;
      endianness = G_BIG_ENDIAN;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      r_mask = 0x000000ff;
      g_mask = 0x0000ff00;
      b_mask = 0x00ff0000;
      a_mask = 0xff000000;
#else
      r_mask = 0xff000000;
      g_mask = 0x00ff0000;
      b_mask = 0x0000ff00;
      a_mask = 0x000000ff;
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
    case PIX_FMT_PAL8:
      bpp = depth = 8;
      endianness = G_BYTE_ORDER;
      break;
    case PIX_FMT_AYUV4444:
      fmt = GST_MAKE_FOURCC ('A', 'Y', 'U', 'V');
      break;
    default:
      /* give up ... */
      break;
  }

  if (bpp != 0) {
    if (a_mask != 0) {
      caps = GST_FF_VID_CAPS_NEW ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, bpp,
          "depth", G_TYPE_INT, depth,
          "red_mask", G_TYPE_INT, r_mask,
          "green_mask", G_TYPE_INT, g_mask,
          "blue_mask", G_TYPE_INT, b_mask,
          "alpha_mask", G_TYPE_INT, a_mask,
          "endianness", G_TYPE_INT, endianness, NULL);
    } else if (r_mask != 0) {
      caps = GST_FF_VID_CAPS_NEW ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, bpp,
          "depth", G_TYPE_INT, depth,
          "red_mask", G_TYPE_INT, r_mask,
          "green_mask", G_TYPE_INT, g_mask,
          "blue_mask", G_TYPE_INT, b_mask,
          "endianness", G_TYPE_INT, endianness, NULL);
    } else {
      caps = GST_FF_VID_CAPS_NEW ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, bpp,
          "depth", G_TYPE_INT, depth,
          "endianness", G_TYPE_INT, endianness, NULL);
      if (context) {
        gst_ffmpeg_set_palette (caps, context);
      }
    }
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

/* Convert a FFMPEG Sample Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_smpfmt_to_caps (enum SampleFormat sample_fmt,
    AVCodecContext * context)
{
  GstCaps *caps = NULL;

  int bpp = 0;
  gboolean signedness = FALSE;

  switch (sample_fmt) {
    case SAMPLE_FMT_S16:
      signedness = TRUE;
      bpp = 16;
      break;

    default:
      /* .. */
      break;
  }

  if (bpp) {
    caps = GST_FF_AUD_CAPS_NEW ("audio/x-raw-int",
        "signed", G_TYPE_BOOLEAN, signedness,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "width", G_TYPE_INT, bpp, "depth", G_TYPE_INT, bpp, NULL);
  }

  if (caps != NULL) {
    char *str = gst_caps_to_string (caps);

    GST_DEBUG ("caps for sample_fmt=%d: %s", sample_fmt, str);
    g_free (str);
  } else {
    GST_WARNING ("No caps found for sample_fmt=%d", sample_fmt);
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
gst_ffmpegcsp_codectype_to_caps (enum CodecType codec_type,
    AVCodecContext * context)
{
  GstCaps *caps;

  switch (codec_type) {
    case CODEC_TYPE_VIDEO:
      if (context) {
        caps = gst_ffmpeg_pixfmt_to_caps (context->pix_fmt,
            context->width == -1 ? NULL : context);
      } else {
        GstCaps *temp;
        enum PixelFormat i;

        caps = gst_caps_new_empty ();
        for (i = 0; i < PIX_FMT_NB; i++) {
          temp = gst_ffmpeg_pixfmt_to_caps (i, NULL);
          if (temp != NULL) {
            gst_caps_append (caps, temp);
          }
        }
      }
      break;

    case CODEC_TYPE_AUDIO:
      if (context) {
        caps = gst_ffmpeg_smpfmt_to_caps (context->sample_fmt, context);
      } else {
        GstCaps *temp;
        enum SampleFormat i;

        caps = gst_caps_new_empty ();
        for (i = 0; i <= SAMPLE_FMT_S16; i++) {
          temp = gst_ffmpeg_smpfmt_to_caps (i, NULL);
          if (temp != NULL) {
            gst_caps_append (caps, temp);
          }
        }
      }
      break;

    default:
      /* .. */
      caps = NULL;
      break;
  }

  return caps;
}

/* Convert a GstCaps (audio/raw) to a FFMPEG SampleFmt
 * and other audio properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_smpfmt (const GstCaps * caps,
    AVCodecContext * context, gboolean raw)
{
  GstStructure *structure;
  gint depth = 0, width = 0, endianness = 0;
  gboolean signedness = FALSE;

  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &context->channels);
  gst_structure_get_int (structure, "rate", &context->sample_rate);

  if (!raw)
    return;

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "depth", &depth) &&
      gst_structure_get_int (structure, "signed", &signedness) &&
      gst_structure_get_int (structure, "endianness", &endianness)) {
    if (width == 16 && depth == 16 &&
        endianness == G_BYTE_ORDER && signedness == TRUE) {
      context->sample_fmt = SAMPLE_FMT_S16;
    }
  }
}


/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 * and other video properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_pixfmt (const GstCaps * caps,
    AVCodecContext * context, gboolean raw)
{
  GstStructure *structure;
  gdouble fps;

  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &context->width);
  gst_structure_get_int (structure, "height", &context->height);

  if (gst_structure_get_double (structure, "framerate", &fps)) {
    context->frame_rate = fps * DEFAULT_FRAME_RATE_BASE;
    context->frame_rate_base = DEFAULT_FRAME_RATE_BASE;
  }

  if (!raw)
    return;

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
      switch (fourcc) {
        case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
          context->pix_fmt = PIX_FMT_YUV422;
          break;
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          context->pix_fmt = PIX_FMT_YUV420P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
          context->pix_fmt = PIX_FMT_YUV411P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
          context->pix_fmt = PIX_FMT_YUV422P;
          break;
        case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
          context->pix_fmt = PIX_FMT_YUV410P;
          break;
        case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
          context->pix_fmt = PIX_FMT_AYUV4444;
          break;
#if 0
        case FIXME:
          context->pix_fmt = PIX_FMT_YUV444P;
          break;
#endif
      }
    }
  } else if (strcmp (gst_structure_get_name (structure),
          "video/x-raw-rgb") == 0) {
    gint bpp = 0, rmask = 0, endianness = 0, amask = 0;

    if (gst_structure_get_int (structure, "bpp", &bpp) &&
        gst_structure_get_int (structure, "endianness", &endianness)) {
      if (gst_structure_get_int (structure, "red_mask", &rmask)) {
        switch (bpp) {
          case 32:
            if (gst_structure_get_int (structure, "alpha_mask", &amask)) {
              context->pix_fmt = PIX_FMT_RGBA32;
            } else {
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
              if (rmask == 0x00ff0000)
#else
              if (rmask == 0x0000ff00)
#endif
                context->pix_fmt = PIX_FMT_RGB32;
            }
            break;
          case 24:
            if (rmask == 0x0000FF)
              context->pix_fmt = PIX_FMT_BGR24;
            else
              context->pix_fmt = PIX_FMT_RGB24;
            break;
          case 16:
            if (endianness == G_BYTE_ORDER)
              context->pix_fmt = PIX_FMT_RGB565;
            break;
          case 15:
            if (endianness == G_BYTE_ORDER)
              context->pix_fmt = PIX_FMT_RGB555;
            break;
          default:
            /* nothing */
            break;
        }
      } else {
        if (bpp == 8) {
          context->pix_fmt = PIX_FMT_PAL8;
          gst_ffmpeg_get_palette (caps, context);
        }
      }
    }
  }
}

/* Convert a GstCaps and a FFMPEG codec Type to a
 * AVCodecContext. If the context is ommitted, no fixed values
 * for video/audio size will be included in the context
 *
 * CodecType is primarily meant for uncompressed data GstCaps!
 */

void
gst_ffmpegcsp_caps_with_codectype (enum CodecType type,
    const GstCaps * caps, AVCodecContext * context)
{
  if (context == NULL)
    return;

  switch (type) {
    case CODEC_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context, TRUE);
      break;

    case CODEC_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context, TRUE);
      break;

    default:
      /* unknown */
      break;
  }
}

/*
 * Fill in pointers to memory in a AVPicture, where
 * everything is aligned by 4 (as required by X).
 * This is mostly a copy from imgconvert.c with some
 * small changes.
 */

#define FF_COLOR_RGB      0     /* RGB color space */
#define FF_COLOR_GRAY     1     /* gray color space */
#define FF_COLOR_YUV      2     /* YUV color space. 16 <= Y <= 235, 16 <= U, V <= 240 */
#define FF_COLOR_YUV_JPEG 3     /* YUV color space. 0 <= Y <= 255, 0 <= U, V <= 255 */

#define FF_PIXEL_PLANAR   0     /* each channel has one component in AVPicture */
#define FF_PIXEL_PACKED   1     /* only one components containing all the channels */
#define FF_PIXEL_PALETTE  2     /* one components containing indexes for a palette */

typedef struct PixFmtInfo
{
  const char *name;
  uint8_t nb_channels;          /* number of channels (including alpha) */
  uint8_t color_type;           /* color type (see FF_COLOR_xxx constants) */
  uint8_t pixel_type;           /* pixel storage type (see FF_PIXEL_xxx constants) */
  uint8_t is_alpha:1;           /* true if alpha can be specified */
  uint8_t x_chroma_shift;       /* X chroma subsampling factor is 2 ^ shift */
  uint8_t y_chroma_shift;       /* Y chroma subsampling factor is 2 ^ shift */
  uint8_t depth;                /* bit depth of the color components */
} PixFmtInfo;

/* this table gives more information about formats */
static PixFmtInfo pix_fmt_info[PIX_FMT_NB] = {
  /*[PIX_FMT_YUV420P] = */ {
        /*.name = */ "yuv420p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 1,
        /*.y_chroma_shift = */ 1,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUV422] = */ {
        /*.name = */ "yuv422",
        /*.nb_channels = */ 1,
        /*.color_type = */ FF_COLOR_YUV,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 1,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_RGB24] = */ {
        /*.name = */ "rgb24",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_RGB,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_BGR24] = */ {
        /*.name = */ "bgr24",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_RGB,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUV422P] = */ {
        /*.name = */ "yuv422p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.depth = */ 8,
        /*.x_chroma_shift = */ 1,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUV444P] = */ {
        /*.name = */ "yuv444p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_RGBA32] = */ {
        /*.name = */ "rgba32",
        /*.nb_channels = */ 4,
        /*.color_type = */ FF_COLOR_RGB,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 1,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_RGB32] = */ {
        /*.name = */ "rgb32",
        /*.nb_channels = */ 4,
        /*.color_type = */ FF_COLOR_RGB,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUV410P] = */ {
        /*.name = */ "yuv410p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 2,
        /*.y_chroma_shift = */ 2,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUV411P] = */ {
        /*.name = */ "yuv411p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 2,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_RGB565] = */ {
        /*.name = */ "rgb565",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_RGB,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 5
      },
  /*[PIX_FMT_RGB555] = */ {
        /*.name = */ "rgb555",
        /*.nb_channels = */ 4,
        /*.color_type = */ FF_COLOR_RGB,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 1,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 5
      },
  /*[PIX_FMT_GRAY8] = */ {
        /*.name = */ "gray",
        /*.nb_channels = */ 1,
        /*.color_type = */ FF_COLOR_GRAY,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_MONOWHITE] = */ {
        /*.name = */ "monow",
        /*.nb_channels = */ 1,
        /*.color_type = */ FF_COLOR_GRAY,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 1
      },
  /*[PIX_FMT_MONOBLACK] = */ {
        /*.name = */ "monob",
        /*.nb_channels = */ 1,
        /*.color_type = */ FF_COLOR_GRAY,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 1
      },
  /*[PIX_FMT_PAL8] = */ {
        /*.name = */ "pal8",
        /*.nb_channels = */ 4,
        /*.color_type = */ FF_COLOR_RGB,
        /*.pixel_type = */ FF_PIXEL_PALETTE,
        /*.is_alpha = */ 1,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUVJ420P] = */ {
        /*.name = */ "yuvj420p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV_JPEG,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 1,
        /*.y_chroma_shift = */ 1,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUVJ422P] = */ {
        /*.name = */ "yuvj422p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV_JPEG,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 1,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_YUVJ444P] = */ {
        /*.name = */ "yuvj444p",
        /*.nb_channels = */ 3,
        /*.color_type = */ FF_COLOR_YUV_JPEG,
        /*.pixel_type = */ FF_PIXEL_PLANAR,
        /*.is_alpha = */ 0,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      },
  /*[PIX_FMT_XVMC_MPEG2_MC] = */ {
        /*.name = */ NULL
      },
  /*[PIX_FMT_UYVY422] = */ {
        /*.name = */ NULL
      },
  /*[PIX_FMT_UYVY411] = */ {
        /*.name = */ NULL
      },
  /*[PIX_FMT_AYUV] = */ {
        /*.name = */ "ayuv4444",
        /*.nb_channels = */ 1,
        /*.color_type = */ FF_COLOR_YUV,
        /*.pixel_type = */ FF_PIXEL_PACKED,
        /*.is_alpha = */ 1,
        /*.x_chroma_shift = */ 0,
        /*.y_chroma_shift = */ 0,
        /*.depth = */ 8
      }
};

#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X (x, 1)
#define ROUND_UP_4(x) ROUND_UP_X (x, 2)
#define ROUND_UP_8(x) ROUND_UP_X (x, 3)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))

int
gst_ffmpegcsp_avpicture_fill (AVPicture * picture,
    uint8_t * ptr, enum PixelFormat pix_fmt, int width, int height)
{
  int size, w2, h2, size2;
  int stride, stride2;
  PixFmtInfo *pinfo;

  pinfo = &pix_fmt_info[pix_fmt];

  switch (pix_fmt) {
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ444P:
      stride = ROUND_UP_4 (width);
      h2 = ROUND_UP_X (height, pinfo->y_chroma_shift);
      size = stride * h2;
      w2 = DIV_ROUND_UP_X (width, pinfo->x_chroma_shift);
      stride2 = ROUND_UP_4 (w2);
      h2 = DIV_ROUND_UP_X (height, pinfo->y_chroma_shift);
      size2 = stride2 * h2;
      picture->data[0] = ptr;
      picture->data[1] = picture->data[0] + size;
      picture->data[2] = picture->data[1] + size2;
      picture->linesize[0] = stride;
      picture->linesize[1] = stride2;
      picture->linesize[2] = stride2;
      return size + 2 * size2;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
      stride = ROUND_UP_4 (width * 3);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_AYUV4444:
    case PIX_FMT_RGB32:
    case PIX_FMT_RGBA32:
      stride = width * 4;
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_RGB555:
    case PIX_FMT_RGB565:
    case PIX_FMT_YUV422:
    case PIX_FMT_UYVY422:
      stride = ROUND_UP_4 (width * 2);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_UYVY411:
      /* FIXME, probably not the right stride */
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = width + width / 2;
      return size + size / 2;
    case PIX_FMT_GRAY8:
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
      stride = ROUND_UP_4 ((width + 7) >> 3);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_PAL8:
      /* already forced to be with stride, so same result as other function */
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = ptr + size;    /* palette is stored here as 256 32 bit words */
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 4;
      return size + 256 * 4;
    default:
      picture->data[0] = NULL;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      return -1;
  }

  return 0;
}
