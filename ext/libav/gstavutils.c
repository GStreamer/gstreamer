/* GStreamer
 * Copyright (c) 2009 Edward Hervey <bilboed@bilboed.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstavutils.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#ifdef __MINGW32__
#include <stdlib.h>
#endif

#include <libavutil/mem.h>

const gchar *
gst_ffmpeg_get_codecid_longname (enum AVCodecID codec_id)
{
  AVCodec *codec;
  /* Let's use what ffmpeg can provide us */

  if ((codec = avcodec_find_decoder (codec_id)) ||
      (codec = avcodec_find_encoder (codec_id)))
    return codec->long_name;
  return NULL;
}

gint
av_smp_format_depth (enum AVSampleFormat smp_fmt)
{
  gint depth = -1;
  switch (smp_fmt) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
      depth = 1;
      break;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
      depth = 2;
      break;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
      depth = 4;
      break;
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
      depth = 8;
      break;
    default:
      GST_ERROR ("UNHANDLED SAMPLE FORMAT !");
      break;
  }
  return depth;
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
static PixFmtInfo pix_fmt_info[AV_PIX_FMT_NB];
void
gst_ffmpeg_init_pix_fmt_info (void)
{
  /* YUV formats */
  pix_fmt_info[AV_PIX_FMT_YUV420P].name = g_strdup ("yuv420p");
  pix_fmt_info[AV_PIX_FMT_YUV420P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUV420P].color_type = FF_COLOR_YUV;
  pix_fmt_info[AV_PIX_FMT_YUV420P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUV420P].depth = 8,
      pix_fmt_info[AV_PIX_FMT_YUV420P].x_chroma_shift = 1,
      pix_fmt_info[AV_PIX_FMT_YUV420P].y_chroma_shift = 1;

  pix_fmt_info[AV_PIX_FMT_YUV422P].name = g_strdup ("yuv422p");
  pix_fmt_info[AV_PIX_FMT_YUV422P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUV422P].color_type = FF_COLOR_YUV;
  pix_fmt_info[AV_PIX_FMT_YUV422P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUV422P].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUV422P].x_chroma_shift = 1;
  pix_fmt_info[AV_PIX_FMT_YUV422P].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_YUV444P].name = g_strdup ("yuv444p");
  pix_fmt_info[AV_PIX_FMT_YUV444P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUV444P].color_type = FF_COLOR_YUV;
  pix_fmt_info[AV_PIX_FMT_YUV444P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUV444P].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUV444P].x_chroma_shift = 0;
  pix_fmt_info[AV_PIX_FMT_YUV444P].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_YUYV422].name = g_strdup ("yuv422");
  pix_fmt_info[AV_PIX_FMT_YUYV422].nb_channels = 1;
  pix_fmt_info[AV_PIX_FMT_YUYV422].color_type = FF_COLOR_YUV;
  pix_fmt_info[AV_PIX_FMT_YUYV422].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[AV_PIX_FMT_YUYV422].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUYV422].x_chroma_shift = 1;
  pix_fmt_info[AV_PIX_FMT_YUYV422].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_YUV410P].name = g_strdup ("yuv410p");
  pix_fmt_info[AV_PIX_FMT_YUV410P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUV410P].color_type = FF_COLOR_YUV;
  pix_fmt_info[AV_PIX_FMT_YUV410P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUV410P].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUV410P].x_chroma_shift = 2;
  pix_fmt_info[AV_PIX_FMT_YUV410P].y_chroma_shift = 2;

  pix_fmt_info[AV_PIX_FMT_YUV411P].name = g_strdup ("yuv411p");
  pix_fmt_info[AV_PIX_FMT_YUV411P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUV411P].color_type = FF_COLOR_YUV;
  pix_fmt_info[AV_PIX_FMT_YUV411P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUV411P].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUV411P].x_chroma_shift = 2;
  pix_fmt_info[AV_PIX_FMT_YUV411P].y_chroma_shift = 0;

  /* JPEG YUV */
  pix_fmt_info[AV_PIX_FMT_YUVJ420P].name = g_strdup ("yuvj420p");
  pix_fmt_info[AV_PIX_FMT_YUVJ420P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUVJ420P].color_type = FF_COLOR_YUV_JPEG;
  pix_fmt_info[AV_PIX_FMT_YUVJ420P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUVJ420P].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUVJ420P].x_chroma_shift = 1;
  pix_fmt_info[AV_PIX_FMT_YUVJ420P].y_chroma_shift = 1;

  pix_fmt_info[AV_PIX_FMT_YUVJ422P].name = g_strdup ("yuvj422p");
  pix_fmt_info[AV_PIX_FMT_YUVJ422P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUVJ422P].color_type = FF_COLOR_YUV_JPEG;
  pix_fmt_info[AV_PIX_FMT_YUVJ422P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUVJ422P].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUVJ422P].x_chroma_shift = 1;
  pix_fmt_info[AV_PIX_FMT_YUVJ422P].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_YUVJ444P].name = g_strdup ("yuvj444p");
  pix_fmt_info[AV_PIX_FMT_YUVJ444P].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_YUVJ444P].color_type = FF_COLOR_YUV_JPEG;
  pix_fmt_info[AV_PIX_FMT_YUVJ444P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUVJ444P].depth = 8;
  pix_fmt_info[AV_PIX_FMT_YUVJ444P].x_chroma_shift = 0;
  pix_fmt_info[AV_PIX_FMT_YUVJ444P].y_chroma_shift = 0;

  /* RGB formats */
  pix_fmt_info[AV_PIX_FMT_RGB24].name = g_strdup ("rgb24");
  pix_fmt_info[AV_PIX_FMT_RGB24].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_RGB24].color_type = FF_COLOR_RGB;
  pix_fmt_info[AV_PIX_FMT_RGB24].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[AV_PIX_FMT_RGB24].depth = 8;
  pix_fmt_info[AV_PIX_FMT_RGB24].x_chroma_shift = 0;
  pix_fmt_info[AV_PIX_FMT_RGB24].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_BGR24].name = g_strdup ("bgr24");
  pix_fmt_info[AV_PIX_FMT_BGR24].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_BGR24].color_type = FF_COLOR_RGB;
  pix_fmt_info[AV_PIX_FMT_BGR24].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[AV_PIX_FMT_BGR24].depth = 8;
  pix_fmt_info[AV_PIX_FMT_BGR24].x_chroma_shift = 0;
  pix_fmt_info[AV_PIX_FMT_BGR24].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_RGB32].name = g_strdup ("rgba32");
  pix_fmt_info[AV_PIX_FMT_RGB32].nb_channels = 4;
  pix_fmt_info[AV_PIX_FMT_RGB32].is_alpha = 1;
  pix_fmt_info[AV_PIX_FMT_RGB32].color_type = FF_COLOR_RGB;
  pix_fmt_info[AV_PIX_FMT_RGB32].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[AV_PIX_FMT_RGB32].depth = 8;
  pix_fmt_info[AV_PIX_FMT_RGB32].x_chroma_shift = 0;
  pix_fmt_info[AV_PIX_FMT_RGB32].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_RGB565].name = g_strdup ("rgb565");
  pix_fmt_info[AV_PIX_FMT_RGB565].nb_channels = 3;
  pix_fmt_info[AV_PIX_FMT_RGB565].color_type = FF_COLOR_RGB;
  pix_fmt_info[AV_PIX_FMT_RGB565].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[AV_PIX_FMT_RGB565].depth = 5;
  pix_fmt_info[AV_PIX_FMT_RGB565].x_chroma_shift = 0;
  pix_fmt_info[AV_PIX_FMT_RGB565].y_chroma_shift = 0;

  pix_fmt_info[AV_PIX_FMT_RGB555].name = g_strdup ("rgb555");
  pix_fmt_info[AV_PIX_FMT_RGB555].nb_channels = 4;
  pix_fmt_info[AV_PIX_FMT_RGB555].is_alpha = 1;
  pix_fmt_info[AV_PIX_FMT_RGB555].color_type = FF_COLOR_RGB;
  pix_fmt_info[AV_PIX_FMT_RGB555].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[AV_PIX_FMT_RGB555].depth = 5;
  pix_fmt_info[AV_PIX_FMT_RGB555].x_chroma_shift = 0;
  pix_fmt_info[AV_PIX_FMT_RGB555].y_chroma_shift = 0;

  /* gray / mono formats */
  pix_fmt_info[AV_PIX_FMT_GRAY8].name = g_strdup ("gray");
  pix_fmt_info[AV_PIX_FMT_GRAY8].nb_channels = 1;
  pix_fmt_info[AV_PIX_FMT_GRAY8].color_type = FF_COLOR_GRAY;
  pix_fmt_info[AV_PIX_FMT_GRAY8].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_GRAY8].depth = 8;

  pix_fmt_info[AV_PIX_FMT_MONOWHITE].name = g_strdup ("monow");
  pix_fmt_info[AV_PIX_FMT_MONOWHITE].nb_channels = 1;
  pix_fmt_info[AV_PIX_FMT_MONOWHITE].color_type = FF_COLOR_GRAY;
  pix_fmt_info[AV_PIX_FMT_MONOWHITE].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_MONOWHITE].depth = 1;

  pix_fmt_info[AV_PIX_FMT_MONOBLACK].name = g_strdup ("monob");
  pix_fmt_info[AV_PIX_FMT_MONOBLACK].nb_channels = 1;
  pix_fmt_info[AV_PIX_FMT_MONOBLACK].color_type = FF_COLOR_GRAY;
  pix_fmt_info[AV_PIX_FMT_MONOBLACK].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_MONOBLACK].depth = 1;

  /* paletted formats */
  pix_fmt_info[AV_PIX_FMT_PAL8].name = g_strdup ("pal8");
  pix_fmt_info[AV_PIX_FMT_PAL8].nb_channels = 4;
  pix_fmt_info[AV_PIX_FMT_PAL8].is_alpha = 1;
  pix_fmt_info[AV_PIX_FMT_PAL8].color_type = FF_COLOR_RGB;
  pix_fmt_info[AV_PIX_FMT_PAL8].pixel_type = FF_PIXEL_PALETTE;
  pix_fmt_info[AV_PIX_FMT_PAL8].depth = 8;

  pix_fmt_info[AV_PIX_FMT_YUVA420P].name = g_strdup ("yuva420p");
  pix_fmt_info[AV_PIX_FMT_YUVA420P].nb_channels = 4;
  pix_fmt_info[AV_PIX_FMT_YUVA420P].is_alpha = 1;
  pix_fmt_info[AV_PIX_FMT_YUVA420P].color_type = FF_COLOR_YUV;
  pix_fmt_info[AV_PIX_FMT_YUVA420P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[AV_PIX_FMT_YUVA420P].depth = 8,
      pix_fmt_info[AV_PIX_FMT_YUVA420P].x_chroma_shift = 1,
      pix_fmt_info[AV_PIX_FMT_YUVA420P].y_chroma_shift = 1;
};

int
gst_ffmpeg_avpicture_get_size (int pix_fmt, int width, int height)
{
  AVPicture dummy_pict;

  return gst_ffmpeg_avpicture_fill (&dummy_pict, NULL, pix_fmt, width, height);
}

#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X (x, 1)
#define ROUND_UP_4(x) ROUND_UP_X (x, 2)
#define ROUND_UP_8(x) ROUND_UP_X (x, 3)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))

int
gst_ffmpeg_avpicture_fill (AVPicture * picture,
    uint8_t * ptr, enum AVPixelFormat pix_fmt, int width, int height)
{
  int size, w2, h2, size2;
  int stride, stride2;
  PixFmtInfo *pinfo;

  pinfo = &pix_fmt_info[pix_fmt];

  switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
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
      picture->data[3] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = stride2;
      picture->linesize[2] = stride2;
      picture->linesize[3] = 0;
      GST_DEBUG ("planes %d %d %d", 0, size, size + size2);
      GST_DEBUG ("strides %d %d %d", stride, stride2, stride2);
      return size + 2 * size2;
    case AV_PIX_FMT_YUVA420P:
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
      picture->data[3] = picture->data[2] + size2;
      picture->linesize[0] = stride;
      picture->linesize[1] = stride2;
      picture->linesize[2] = stride2;
      picture->linesize[3] = stride;
      GST_DEBUG ("planes %d %d %d %d", 0, size, size + size2, size + 2 * size2);
      GST_DEBUG ("strides %d %d %d %d", stride, stride2, stride2, stride);
      return 2 * size + 2 * size2;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
      stride = ROUND_UP_4 (width * 3);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 0;
      picture->linesize[2] = 0;
      picture->linesize[3] = 0;
      return size;
      /*case AV_PIX_FMT_AYUV4444:
         case AV_PIX_FMT_BGR32:
         case AV_PIX_FMT_BGRA32:
         case AV_PIX_FMT_RGB32: */
    case AV_PIX_FMT_RGB32:
      stride = width * 4;
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 0;
      picture->linesize[2] = 0;
      picture->linesize[3] = 0;
      return size;
    case AV_PIX_FMT_RGB555:
    case AV_PIX_FMT_RGB565:
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_UYVY422:
      stride = ROUND_UP_4 (width * 2);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 0;
      picture->linesize[2] = 0;
      picture->linesize[3] = 0;
      return size;
    case AV_PIX_FMT_UYYVYY411:
      /* FIXME, probably not the right stride */
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      picture->linesize[0] = width + width / 2;
      picture->linesize[1] = 0;
      picture->linesize[2] = 0;
      picture->linesize[3] = 0;
      return size + size / 2;
    case AV_PIX_FMT_GRAY8:
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 0;
      picture->linesize[2] = 0;
      picture->linesize[3] = 0;
      return size;
    case AV_PIX_FMT_MONOWHITE:
    case AV_PIX_FMT_MONOBLACK:
      stride = ROUND_UP_4 ((width + 7) >> 3);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 0;
      picture->linesize[2] = 0;
      picture->linesize[3] = 0;
      return size;
    case AV_PIX_FMT_PAL8:
      /* already forced to be with stride, so same result as other function */
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = ptr + size;    /* palette is stored here as 256 32 bit words */
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 4;
      picture->linesize[2] = 0;
      picture->linesize[3] = 0;
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

/* Create a GstBuffer of the requested size and caps.
 * The memory will be allocated by ffmpeg, making sure it's properly aligned
 * for any processing. */

GstBuffer *
new_aligned_buffer (gint size)
{
  GstBuffer *buf;
  guint8 *data;

  data = av_malloc (size);

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, size, 0, size, data, av_free));

  return buf;
}

int
gst_ffmpeg_auto_max_threads (void)
{
  static gsize n_threads = 0;
  if (g_once_init_enter (&n_threads)) {
    int n = 1;
#if defined(_WIN32)
    {
      const char *s = getenv ("NUMBER_OF_PROCESSORS");
      if (s) {
        n = atoi (s);
      }
    }
#elif defined(__APPLE__)
    {
      int mib[] = { CTL_HW, HW_NCPU };
      size_t dataSize = sizeof (int);

      if (sysctl (mib, 2, &n, &dataSize, NULL, 0)) {
        n = 1;
      }
    }
#else
    n = sysconf (_SC_NPROCESSORS_CONF);
#endif
    if (n < 1)
      n = 1;

    g_once_init_leave (&n_threads, n);
  }

  return (int) (n_threads);
}
