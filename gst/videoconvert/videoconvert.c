/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "videoconvert.h"

#include <glib.h>
#include <string.h>
#include <math.h>

#include "gstvideoconvertorc.h"


static void videoconvert_convert_generic (VideoConvert * convert,
    GstVideoFrame * dest, const GstVideoFrame * src);
static void videoconvert_convert_matrix (VideoConvert * convert);
static void videoconvert_convert_matrix16 (VideoConvert * convert);
static gboolean videoconvert_convert_lookup_fastpath (VideoConvert * convert);
static void videoconvert_convert_compute_matrix (VideoConvert * convert);
static void videoconvert_dither_none (VideoConvert * convert, int j);
static void videoconvert_dither_verterr (VideoConvert * convert, int j);
static void videoconvert_dither_halftone (VideoConvert * convert, int j);


VideoConvert *
videoconvert_convert_new (GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  VideoConvert *convert;
  int i, width;

  convert = g_malloc0 (sizeof (VideoConvert));

  convert->in_info = *in_info;
  convert->out_info = *out_info;
  convert->dither16 = videoconvert_dither_none;

  if (!videoconvert_convert_lookup_fastpath (convert)) {
    convert->convert = videoconvert_convert_generic;
    videoconvert_convert_compute_matrix (convert);
  }

  convert->width = GST_VIDEO_INFO_WIDTH (in_info);
  convert->height = GST_VIDEO_INFO_HEIGHT (in_info);

  width = convert->width;

  convert->tmpline = g_malloc (sizeof (guint8) * (width + 8) * 4);
  convert->tmpline16 = g_malloc (sizeof (guint16) * (width + 8) * 4);
  convert->errline = g_malloc0 (sizeof (guint16) * width * 4);

  if (GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_RGB8P) {
    /* build poor man's palette, taken from ffmpegcolorspace */
    static const guint8 pal_value[6] = { 0x00, 0x33, 0x66, 0x99, 0xcc, 0xff };
    guint32 *palette;
    gint r, g, b;

    convert->palette = palette = g_new (guint32, 256);
    i = 0;
    for (r = 0; r < 6; r++) {
      for (g = 0; g < 6; g++) {
        for (b = 0; b < 6; b++) {
          palette[i++] =
              (0xffU << 24) | (pal_value[r] << 16) | (pal_value[g] << 8) |
              pal_value[b];
        }
      }
    }
    palette[i++] = 0;           /* 100% transparent, i == 6*6*6 */
    while (i < 256)
      palette[i++] = 0xff000000;
  }
  return convert;
}

void
videoconvert_convert_free (VideoConvert * convert)
{
  g_free (convert->palette);
  g_free (convert->tmpline);
  g_free (convert->tmpline16);
  g_free (convert->errline);

  g_free (convert);
}

void
videoconvert_convert_set_dither (VideoConvert * convert, int type)
{
  switch (type) {
    case 0:
    default:
      convert->dither16 = videoconvert_dither_none;
      break;
    case 1:
      convert->dither16 = videoconvert_dither_verterr;
      break;
    case 2:
      convert->dither16 = videoconvert_dither_halftone;
      break;
  }
}

void
videoconvert_convert_convert (VideoConvert * convert,
    GstVideoFrame * dest, const GstVideoFrame * src)
{
  convert->convert (convert, dest, src);
}

void
videoconvert_convert_matrix (VideoConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint8 *tmpline = convert->tmpline;

  for (i = 0; i < convert->width; i++) {
    r = tmpline[i * 4 + 1];
    g = tmpline[i * 4 + 2];
    b = tmpline[i * 4 + 3];

    y = (convert->cmatrix[0][0] * r + convert->cmatrix[0][1] * g +
        convert->cmatrix[0][2] * b + convert->cmatrix[0][3]) >> 8;
    u = (convert->cmatrix[1][0] * r + convert->cmatrix[1][1] * g +
        convert->cmatrix[1][2] * b + convert->cmatrix[1][3]) >> 8;
    v = (convert->cmatrix[2][0] * r + convert->cmatrix[2][1] * g +
        convert->cmatrix[2][2] * b + convert->cmatrix[2][3]) >> 8;

    tmpline[i * 4 + 1] = CLAMP (y, 0, 255);
    tmpline[i * 4 + 2] = CLAMP (u, 0, 255);
    tmpline[i * 4 + 3] = CLAMP (v, 0, 255);
  }
}

void
videoconvert_convert_matrix16 (VideoConvert * convert)
{
  int i;
  int r, g, b;
  int y, u, v;
  guint16 *tmpline = convert->tmpline16;

  for (i = 0; i < convert->width; i++) {
    r = tmpline[i * 4 + 1];
    g = tmpline[i * 4 + 2];
    b = tmpline[i * 4 + 3];

    y = (convert->cmatrix[0][0] * r + convert->cmatrix[0][1] * g +
        convert->cmatrix[0][2] * b + convert->cmatrix[0][3]) >> 8;
    u = (convert->cmatrix[1][0] * r + convert->cmatrix[1][1] * g +
        convert->cmatrix[1][2] * b + convert->cmatrix[1][3]) >> 8;
    v = (convert->cmatrix[2][0] * r + convert->cmatrix[2][1] * g +
        convert->cmatrix[2][2] * b + convert->cmatrix[2][3]) >> 8;

    tmpline[i * 4 + 1] = CLAMP (y, 0, 65535);
    tmpline[i * 4 + 2] = CLAMP (u, 0, 65535);
    tmpline[i * 4 + 3] = CLAMP (v, 0, 65535);
  }
}

static void
matrix_identity (VideoConvert * convert)
{
  /* do nothing */
}

static void
videoconvert_convert_compute_matrix (VideoConvert * convert)
{
  GstVideoInfo *in_info, *out_info;
  ColorMatrix dst;
  gint i, j;
  const GstVideoFormatInfo *sfinfo, *dfinfo;
  gboolean use_16;
  gint in_bits, out_bits;

  in_info = &convert->in_info;
  out_info = &convert->out_info;

  if (in_info->colorimetry.range == out_info->colorimetry.range &&
      in_info->colorimetry.matrix == out_info->colorimetry.matrix) {
    GST_DEBUG ("using identity color transform");
    convert->matrix = matrix_identity;
    convert->matrix16 = matrix_identity;
    return;
  }

  sfinfo = in_info->finfo;
  dfinfo = out_info->finfo;

  in_bits =
      GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info
      (sfinfo->unpack_format), 0);
  out_bits =
      GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info
      (dfinfo->unpack_format), 0);

  if (in_bits == 16 || out_bits == 16)
    use_16 = TRUE;
  else
    use_16 = FALSE;

  color_matrix_set_identity (&dst);

  /* 1, bring color components to [0..1.0] range */
  switch (in_info->colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      switch (in_info->finfo->unpack_format) {
        case GST_VIDEO_FORMAT_AYUV:
        case GST_VIDEO_FORMAT_AYUV64:
          GST_DEBUG ("using 0-255 input range YUV");
          if (use_16) {
            color_matrix_offset_components (&dst, 0, -32768, -32768);
            color_matrix_scale_components (&dst, (1 / 65535.0), (1 / 65535.0),
                (1 / 65535.0));
          } else {
            color_matrix_offset_components (&dst, 0, -128, -128);
            color_matrix_scale_components (&dst, (1 / 255.0), (1 / 255.0),
                (1 / 255.0));
          }
          break;
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ARGB64:
          GST_DEBUG ("using 0-255 input range RGB");
          if (use_16)
            color_matrix_scale_components (&dst, (1 / 65535.0), (1 / 65535.0),
                (1 / 65535.0));
          else
            color_matrix_scale_components (&dst, (1 / 255.0), (1 / 255.0),
                (1 / 255.0));
          break;
        default:
          break;
      }
      break;
    default:
    case GST_VIDEO_COLOR_RANGE_16_235:
      /* offset ans scale required to get input video black to (0.,0.,0.) */
      switch (in_info->finfo->unpack_format) {
        case GST_VIDEO_FORMAT_AYUV:
        case GST_VIDEO_FORMAT_AYUV64:
          GST_DEBUG ("using 16-235 input range YUV");
          if (use_16) {
            color_matrix_offset_components (&dst, -4096, -32768, -32768);
            color_matrix_scale_components (&dst, (1 / 56064.0), (1 / 57344.0),
                (1 / 57344.0));
          } else {
            color_matrix_offset_components (&dst, -16, -128, -128);
            color_matrix_scale_components (&dst, (1 / 219.0), (1 / 224.0),
                (1 / 224.0));
          }
          break;
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ARGB64:
          GST_DEBUG ("using 16-235 input range RGB");
          if (use_16) {
            color_matrix_offset_components (&dst, -4096, -4096, -4096);
            color_matrix_scale_components (&dst, (1 / 56064.0), (1 / 56064.0),
                (1 / 56064.0));
          } else {
            color_matrix_offset_components (&dst, -16, -16, -16);
            color_matrix_scale_components (&dst, (1 / 219.0), (1 / 219.0),
                (1 / 219.0));
          }
          break;
        default:
          break;
      }
      break;
  }

  /* 2. bring components to R'G'B' space */
  switch (in_info->colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_RGB:
      break;
    case GST_VIDEO_COLOR_MATRIX_FCC:
      color_matrix_YCbCr_to_RGB (&dst, 0.30, 0.11);
      break;
    case GST_VIDEO_COLOR_MATRIX_BT709:
      color_matrix_YCbCr_to_RGB (&dst, 0.2126, 0.0722);
      break;
    case GST_VIDEO_COLOR_MATRIX_BT601:
      color_matrix_YCbCr_to_RGB (&dst, 0.2990, 0.1140);
      break;
    case GST_VIDEO_COLOR_MATRIX_SMPTE240M:
      color_matrix_YCbCr_to_RGB (&dst, 0.212, 0.087);
    default:
      break;
  }
  /* 3. inverse transfer function. R'G'B' to linear RGB */

  /* 4. from RGB to XYZ using the primaries */

  /* 5. from XYZ to RGB using the primaries */

  /* 6. transfer function. linear RGB to R'G'B' */

  /* 7. bring components to YCbCr space */
  switch (out_info->colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_RGB:
      break;
    case GST_VIDEO_COLOR_MATRIX_FCC:
      color_matrix_RGB_to_YCbCr (&dst, 0.30, 0.11);
      break;
    case GST_VIDEO_COLOR_MATRIX_BT709:
      color_matrix_RGB_to_YCbCr (&dst, 0.2126, 0.0722);
      break;
    case GST_VIDEO_COLOR_MATRIX_BT601:
      color_matrix_RGB_to_YCbCr (&dst, 0.2990, 0.1140);
      break;
    case GST_VIDEO_COLOR_MATRIX_SMPTE240M:
      color_matrix_RGB_to_YCbCr (&dst, 0.212, 0.087);
    default:
      break;
  }

  /* 8, bring color components to nominal range */
  switch (out_info->colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      switch (out_info->finfo->unpack_format) {
        case GST_VIDEO_FORMAT_AYUV:
        case GST_VIDEO_FORMAT_AYUV64:
          GST_DEBUG ("using 0-255 output range YUV");
          if (use_16) {
            color_matrix_scale_components (&dst, 65535.0, 65535.0, 65535.0);
            color_matrix_offset_components (&dst, 0, 32768, 32768);
          } else {
            color_matrix_scale_components (&dst, 255.0, 255.0, 255.0);
            color_matrix_offset_components (&dst, 0, 128, 128);
          }
          break;
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ARGB64:
          GST_DEBUG ("using 0-255 output range RGB");
          if (use_16)
            color_matrix_scale_components (&dst, 65535.0, 65535.0, 65535.0);
          else
            color_matrix_scale_components (&dst, 255.0, 255.0, 255.0);
          break;
        default:
          break;
      }
      break;
      GST_DEBUG ("using 0-255 output range");
      if (use_16)
        color_matrix_scale_components (&dst, 65535.0, 65535.0, 65535.0);
      else
        color_matrix_scale_components (&dst, 255.0, 255.0, 255.0);
      break;
    case GST_VIDEO_COLOR_RANGE_16_235:
    default:
      switch (out_info->finfo->unpack_format) {
        case GST_VIDEO_FORMAT_AYUV:
        case GST_VIDEO_FORMAT_AYUV64:
          GST_DEBUG ("using 16-235 output range YUV");
          if (use_16) {
            color_matrix_scale_components (&dst, 56064.0, 57344.0, 57344.0);
            color_matrix_offset_components (&dst, 4096, 32768, 32768);
          } else {
            color_matrix_scale_components (&dst, 219.0, 224.0, 224.0);
            color_matrix_offset_components (&dst, 16, 128, 128);
          }
          break;
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ARGB64:
          GST_DEBUG ("using 16-235 output range RGB");
          if (use_16) {
            color_matrix_scale_components (&dst, 56064.0, 56064.0, 56064.0);
            color_matrix_offset_components (&dst, 4096, 4096, 4096);
          } else {
            color_matrix_scale_components (&dst, 219.0, 219.0, 219.0);
            color_matrix_offset_components (&dst, 16, 16, 16);
          }
          break;
        default:
          break;
      }
      break;
  }
  /* because we're doing 8-bit matrix coefficients */
  color_matrix_scale_components (&dst, 256.0, 256.0, 256.0);

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      convert->cmatrix[i][j] = rint (dst.m[i][j]);

  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[0][0],
      convert->cmatrix[0][1], convert->cmatrix[0][2], convert->cmatrix[0][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[1][0],
      convert->cmatrix[1][1], convert->cmatrix[1][2], convert->cmatrix[1][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[2][0],
      convert->cmatrix[2][1], convert->cmatrix[2][2], convert->cmatrix[2][3]);
  GST_DEBUG ("[%6d %6d %6d %6d]", convert->cmatrix[3][0],
      convert->cmatrix[3][1], convert->cmatrix[3][2], convert->cmatrix[3][3]);

  convert->matrix = videoconvert_convert_matrix;
  convert->matrix16 = videoconvert_convert_matrix16;
}

static void
videoconvert_dither_none (VideoConvert * convert, int j)
{
}

static void
videoconvert_dither_verterr (VideoConvert * convert, int j)
{
  int i;
  guint16 *tmpline = convert->tmpline16;
  guint16 *errline = convert->errline;
  unsigned int mask = 0xff;

  for (i = 0; i < 4 * convert->width; i++) {
    int x = tmpline[i] + errline[i];
    if (x > 65535)
      x = 65535;
    tmpline[i] = x;
    errline[i] = x & mask;
  }
}

static void
videoconvert_dither_halftone (VideoConvert * convert, int j)
{
  int i;
  guint16 *tmpline = convert->tmpline16;
  static guint16 halftone[8][8] = {
    {0, 128, 32, 160, 8, 136, 40, 168},
    {192, 64, 224, 96, 200, 72, 232, 104},
    {48, 176, 16, 144, 56, 184, 24, 152},
    {240, 112, 208, 80, 248, 120, 216, 88},
    {12, 240, 44, 172, 4, 132, 36, 164},
    {204, 76, 236, 108, 196, 68, 228, 100},
    {60, 188, 28, 156, 52, 180, 20, 148},
    {252, 142, 220, 92, 244, 116, 212, 84}
  };

  for (i = 0; i < convert->width * 4; i++) {
    int x;
    x = tmpline[i] + halftone[(i >> 2) & 7][j & 7];
    if (x > 65535)
      x = 65535;
    tmpline[i] = x;
  }
}

#define TO_16(x) (((x)<<8) | (x))

#define UNPACK_FRAME(frame,dest,line,width) \
  frame->info.finfo->unpack_func (frame->info.finfo, GST_VIDEO_PACK_FLAG_NONE, \
      dest, frame->data, frame->info.stride, 0, line, width)
#define PACK_FRAME(frame,dest,line,width) \
  frame->info.finfo->pack_func (frame->info.finfo, GST_VIDEO_PACK_FLAG_NONE, \
      dest, 0, frame->data, frame->info.stride, frame->info.chroma_site, line, width);

static void
videoconvert_convert_generic (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i, j;
  const GstVideoFormatInfo *sfinfo, *dfinfo;
  gint width, height;
  guint src_bits, dest_bits;

  sfinfo = src->info.finfo;
  dfinfo = dest->info.finfo;

  src_bits =
      GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info
      (sfinfo->unpack_format), 0);
  dest_bits =
      GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info
      (dfinfo->unpack_format), 0);

  height = convert->height;
  width = convert->width;

  if (sfinfo->unpack_func == NULL) {
    GST_ERROR ("no unpack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (src)));
    return;
  }

  if (dfinfo->pack_func == NULL) {
    GST_ERROR ("no pack_func for format %s",
        gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (dest)));
    return;
  }

  for (j = 0; j < height; j++) {
    if (src_bits == 16) {
      UNPACK_FRAME (src, convert->tmpline16, j, width);
    } else {
      UNPACK_FRAME (src, convert->tmpline, j, width);

      if (dest_bits == 16)
        for (i = 0; i < width * 4; i++)
          convert->tmpline16[i] = TO_16 (convert->tmpline[i]);
    }

    if (dest_bits == 16 || src_bits == 16) {
      convert->matrix16 (convert);
      convert->dither16 (convert, j);
    } else {
      convert->matrix (convert);
    }

    if (dest_bits == 16) {
      PACK_FRAME (dest, convert->tmpline16, j, width);
    } else {
      if (src_bits == 16)
        for (i = 0; i < width * 4; i++)
          convert->tmpline[i] = convert->tmpline16[i] >> 8;

      PACK_FRAME (dest, convert->tmpline, j, width);
    }
  }
}

#define FRAME_GET_PLANE_STRIDE(frame, plane) \
  GST_VIDEO_FRAME_PLANE_STRIDE (frame, plane)
#define FRAME_GET_PLANE_LINE(frame, plane, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_PLANE_DATA (frame, plane))) + \
      FRAME_GET_PLANE_STRIDE (frame, plane) * (line))

#define FRAME_GET_COMP_STRIDE(frame, comp) \
  GST_VIDEO_FRAME_COMP_STRIDE (frame, comp)
#define FRAME_GET_COMP_LINE(frame, comp, line) \
  (gpointer)(((guint8*)(GST_VIDEO_FRAME_COMP_DATA (frame, comp))) + \
      FRAME_GET_COMP_STRIDE (frame, comp) * (line))

#define FRAME_GET_STRIDE(frame)      FRAME_GET_PLANE_STRIDE (frame, 0)
#define FRAME_GET_LINE(frame,line)   FRAME_GET_PLANE_LINE (frame, 0, line)

#define FRAME_GET_Y_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_Y, line)
#define FRAME_GET_U_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_U, line)
#define FRAME_GET_V_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_V, line)
#define FRAME_GET_A_LINE(frame,line) FRAME_GET_COMP_LINE(frame, GST_VIDEO_COMP_A, line)

#define FRAME_GET_Y_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_Y)
#define FRAME_GET_U_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_U)
#define FRAME_GET_V_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_V)
#define FRAME_GET_A_STRIDE(frame)    FRAME_GET_COMP_STRIDE(frame, GST_VIDEO_COMP_A)

/* Fast paths */

static void
convert_I420_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_I420_YUY2 (FRAME_GET_LINE (dest, i),
        FRAME_GET_LINE (dest, i + 1),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_Y_LINE (src, i + 1),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_I420_UYVY (FRAME_GET_LINE (dest, i),
        FRAME_GET_LINE (dest, i + 1),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_Y_LINE (src, i + 1),
        FRAME_GET_U_LINE (src, i >> 1),
        FRAME_GET_V_LINE (src, i >> 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_I420_AYUV (FRAME_GET_LINE (dest, i),
        FRAME_GET_LINE (dest, i + 1),
        FRAME_GET_Y_LINE (src, i),
        FRAME_GET_Y_LINE (src, i + 1),
        FRAME_GET_U_LINE (src, i >> 1), FRAME_GET_V_LINE (src, i >> 1), width);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_I420_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_420_422 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_420_422 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);
}

static void
convert_I420_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_420_444 (FRAME_GET_U_LINE (dest, 0),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (dest, 1),
      2 * FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_420_444 (FRAME_GET_V_LINE (dest, 0),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (dest, 1),
      2 * FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i, h;
  gint width = convert->width;
  gint height = convert->height;

  h = height;
  if (width & 1)
    h--;

  for (i = 0; i < h; i += 2) {
    cogorc_convert_YUY2_I420 (FRAME_GET_Y_LINE (dest, i),
        FRAME_GET_Y_LINE (dest, i + 1),
        FRAME_GET_U_LINE (dest, i >> 1),
        FRAME_GET_V_LINE (dest, i >> 1),
        FRAME_GET_LINE (src, i), FRAME_GET_LINE (src, i + 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_YUY2_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2,
      height & 1 ? height - 1 : height);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_YUY2_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_YUY2_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_YUY2_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_YUY2_Y444 (FRAME_GET_COMP_LINE (dest, 0, 0),
      FRAME_GET_COMP_STRIDE (dest, 0), FRAME_GET_COMP_LINE (dest, 1, 0),
      FRAME_GET_COMP_STRIDE (dest, 1), FRAME_GET_COMP_LINE (dest, 2, 0),
      FRAME_GET_COMP_STRIDE (dest, 2), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}


static void
convert_UYVY_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  gint width = convert->width;
  gint height = convert->height;

  for (i = 0; i < GST_ROUND_DOWN_2 (height); i += 2) {
    cogorc_convert_UYVY_I420 (FRAME_GET_COMP_LINE (dest, 0, i),
        FRAME_GET_COMP_LINE (dest, 0, i + 1),
        FRAME_GET_COMP_LINE (dest, 1, i >> 1),
        FRAME_GET_COMP_LINE (dest, 2, i >> 1),
        FRAME_GET_LINE (src, i), FRAME_GET_LINE (src, i + 1), (width + 1) / 2);
  }

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_UYVY_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2,
      height & 1 ? height - 1 : height);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_UYVY_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_UYVY_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_UYVY_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_AYUV_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_I420 (FRAME_GET_Y_LINE (dest, 0),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_Y_LINE (dest, 1),
      2 * FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      2 * FRAME_GET_STRIDE (src), FRAME_GET_LINE (src, 1),
      2 * FRAME_GET_STRIDE (src), width / 2, height / 2);
}

static void
convert_AYUV_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width / 2, height);
}

static void
convert_AYUV_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_Y42B (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), (width + 1) / 2,
      height & 1 ? height - 1 : height);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_AYUV_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_Y444 (FRAME_GET_Y_LINE (dest, 0),
      FRAME_GET_Y_STRIDE (dest), FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_Y42B_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_422_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_422_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_Y42B_Y444 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_422_444 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height);

  cogorc_planar_chroma_422_444 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y42B_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y42B_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y42B_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y42B_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width) / 2, height);
}

static void
convert_Y444_I420 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_444_420 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      2 * FRAME_GET_U_STRIDE (src), FRAME_GET_U_LINE (src, 1),
      2 * FRAME_GET_U_STRIDE (src), (width + 1) / 2, height / 2);

  cogorc_planar_chroma_444_420 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      2 * FRAME_GET_V_STRIDE (src), FRAME_GET_V_LINE (src, 1),
      2 * FRAME_GET_V_STRIDE (src), (width + 1) / 2, height / 2);

  /* now handle last line */
  if (height & 1) {
    UNPACK_FRAME (src, convert->tmpline, height - 1, width);
    PACK_FRAME (dest, convert->tmpline, height - 1, width);
  }
}

static void
convert_Y444_Y42B (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_memcpy_2d (FRAME_GET_Y_LINE (dest, 0), FRAME_GET_Y_STRIDE (dest),
      FRAME_GET_Y_LINE (src, 0), FRAME_GET_Y_STRIDE (src), width, height);

  cogorc_planar_chroma_444_422 (FRAME_GET_U_LINE (dest, 0),
      FRAME_GET_U_STRIDE (dest), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), (width + 1) / 2, height);

  cogorc_planar_chroma_444_422 (FRAME_GET_V_LINE (dest, 0),
      FRAME_GET_V_STRIDE (dest), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y444_YUY2 (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y444_YUY2 (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y444_UYVY (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y444_UYVY (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), (width + 1) / 2, height);
}

static void
convert_Y444_AYUV (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_Y444_AYUV (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_Y_LINE (src, 0),
      FRAME_GET_Y_STRIDE (src), FRAME_GET_U_LINE (src, 0),
      FRAME_GET_U_STRIDE (src), FRAME_GET_V_LINE (src, 0),
      FRAME_GET_V_STRIDE (src), width, height);
}

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
static void
convert_AYUV_ARGB (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_ARGB (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_AYUV_BGRA (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_BGRA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_AYUV_ABGR (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_ABGR (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_AYUV_RGBA (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint width = convert->width;
  gint height = convert->height;

  cogorc_convert_AYUV_RGBA (FRAME_GET_LINE (dest, 0),
      FRAME_GET_STRIDE (dest), FRAME_GET_LINE (src, 0),
      FRAME_GET_STRIDE (src), width, height);
}

static void
convert_I420_BGRA (VideoConvert * convert, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  int i;
  int quality = 0;
  gint width = convert->width;
  gint height = convert->height;

  if (quality > 3) {
    for (i = 0; i < height; i++) {
      if (i & 1) {
        cogorc_convert_I420_BGRA_avg (FRAME_GET_LINE (dest, i),
            FRAME_GET_Y_LINE (src, i),
            FRAME_GET_U_LINE (src, i >> 1),
            FRAME_GET_U_LINE (src, (i >> 1) + 1),
            FRAME_GET_V_LINE (src, i >> 1),
            FRAME_GET_V_LINE (src, (i >> 1) + 1), width);
      } else {
        cogorc_convert_I420_BGRA (FRAME_GET_LINE (dest, i),
            FRAME_GET_Y_LINE (src, i),
            FRAME_GET_U_LINE (src, i >> 1),
            FRAME_GET_V_LINE (src, i >> 1), width);
      }
    }
  } else {
    for (i = 0; i < height; i++) {
      cogorc_convert_I420_BGRA (FRAME_GET_LINE (dest, i),
          FRAME_GET_Y_LINE (src, i),
          FRAME_GET_U_LINE (src, i >> 1),
          FRAME_GET_V_LINE (src, i >> 1), width);
    }
  }
}
#endif



/* Fast paths */

typedef struct
{
  GstVideoFormat in_format;
  GstVideoColorMatrix in_matrix;
  GstVideoFormat out_format;
  GstVideoColorMatrix out_matrix;
  gboolean keeps_color_matrix;
  void (*convert) (VideoConvert * convert, GstVideoFrame * dest,
      const GstVideoFrame * src);
} VideoTransform;
static const VideoTransform transforms[] = {
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_I420_YUY2},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_I420_UYVY},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_I420_AYUV},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_I420_Y42B},
  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_I420_Y444},

  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_YUY2_I420},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_UYVY_YUY2},      /* alias */
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_YUY2_AYUV},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_YUY2_Y42B},
  {GST_VIDEO_FORMAT_YUY2, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_YUY2_Y444},

  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_UYVY_I420},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_UYVY_YUY2},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_UYVY_AYUV},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_UYVY_Y42B},
  {GST_VIDEO_FORMAT_UYVY, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_UYVY_Y444},

  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_AYUV_I420},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_AYUV_YUY2},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_AYUV_UYVY},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_AYUV_Y42B},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_AYUV_Y444},

  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y42B_I420},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y42B_YUY2},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y42B_UYVY},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y42B_AYUV},
  {GST_VIDEO_FORMAT_Y42B, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y444,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y42B_Y444},

  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y444_I420},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_YUY2,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y444_YUY2},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_UYVY,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y444_UYVY},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_AYUV,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y444_AYUV},
  {GST_VIDEO_FORMAT_Y444, GST_VIDEO_COLOR_MATRIX_UNKNOWN, GST_VIDEO_FORMAT_Y42B,
      GST_VIDEO_COLOR_MATRIX_UNKNOWN, TRUE, convert_Y444_Y42B},

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_ARGB,
      GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_ARGB},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_BGRA,
      GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_BGRA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_xRGB, GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_ARGB},   /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_BGRx, GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_BGRA},   /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_ABGR,
      GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_ABGR},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_RGBA,
      GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_RGBA},
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_xBGR, GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_ABGR},   /* alias */
  {GST_VIDEO_FORMAT_AYUV, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_RGBx, GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_AYUV_RGBA},   /* alias */

  {GST_VIDEO_FORMAT_I420, GST_VIDEO_COLOR_MATRIX_BT601, GST_VIDEO_FORMAT_BGRA,
      GST_VIDEO_COLOR_MATRIX_RGB, FALSE, convert_I420_BGRA},
#endif
};

static gboolean
videoconvert_convert_lookup_fastpath (VideoConvert * convert)
{
  int i;
  GstVideoFormat in_format, out_format;
  GstVideoColorMatrix in_matrix, out_matrix;

  in_format = GST_VIDEO_INFO_FORMAT (&convert->in_info);
  out_format = GST_VIDEO_INFO_FORMAT (&convert->out_info);

  in_matrix = convert->in_info.colorimetry.matrix;
  out_matrix = convert->out_info.colorimetry.matrix;

  for (i = 0; i < sizeof (transforms) / sizeof (transforms[0]); i++) {
    if (transforms[i].in_format == in_format &&
        transforms[i].out_format == out_format &&
        (transforms[i].keeps_color_matrix ||
            (transforms[i].in_matrix == in_matrix &&
                transforms[i].out_matrix == out_matrix))) {
      convert->convert = transforms[i].convert;
      return TRUE;
    }
  }
  return FALSE;
}
