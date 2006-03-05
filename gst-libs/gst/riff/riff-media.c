/* GStreamer RIFF I/O
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * riff-media.h: RIFF-id to/from caps routines
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

#include "riff-ids.h"
#include "riff-media.h"

#include <gst/audio/multichannel.h>

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (riff_debug);
#define GST_CAT_DEFAULT riff_debug

/**
 * gst_riff_create_video_caps_with_data:
 * @codec_fcc: fourCC codec for this codec.
 * @strh: pointer to the strh stream header structure.
 * @strf: pointer to the strf stream header structure, including any
 *        data that is within the range of strf.size, but excluding any
 *        additional data withint this chunk but outside strf.size.
 * @strf_data: a #GstBuffer containing the additional data in the strf
 *             chunk outside reach of strf.size. Ususally a palette.
 * @strd_data: a #GstBuffer containing the data in the strd stream header
 *             chunk. Usually codec initialization data.
 * @codec_name: if given, will be filled with a human-readable codec name.
 */

GstCaps *
gst_riff_create_video_caps (guint32 codec_fcc,
    gst_riff_strh * strh, gst_riff_strf_vids * strf,
    GstBuffer * strf_data, GstBuffer * strd_data, char **codec_name)
{
  GstCaps *caps = NULL;
  GstBuffer *palette = NULL;

  switch (codec_fcc) {
    case GST_MAKE_FOURCC ('D', 'I', 'B', ' '):
      caps = gst_caps_new_simple ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, 8,
          "depth", G_TYPE_INT, 8, "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
      palette = strf_data;
      strf_data = NULL;
      if (codec_name)
        *codec_name = g_strdup ("Palettized 8-bit RGB");
      break;

    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, codec_fcc, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Uncompressed planar YUV 4:2:0");
      break;

    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, codec_fcc, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Uncompressed packed YUV 4:2:2");
      break;

    case GST_MAKE_FOURCC ('M', 'J', 'P', 'G'): /* YUY2 MJPEG */
    case GST_MAKE_FOURCC ('A', 'V', 'R', 'n'):
    case GST_MAKE_FOURCC ('I', 'J', 'P', 'G'):
    case GST_MAKE_FOURCC ('i', 'j', 'p', 'g'):
    case GST_MAKE_FOURCC ('J', 'P', 'G', 'L'):
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Motion JPEG");
      break;

    case GST_MAKE_FOURCC ('J', 'P', 'E', 'G'): /* generic (mostly RGB) MJPEG */
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("JPEG Still Image");
      break;

    case GST_MAKE_FOURCC ('P', 'I', 'X', 'L'): /* Miro/Pinnacle fourccs */
    case GST_MAKE_FOURCC ('V', 'I', 'X', 'L'): /* Miro/Pinnacle fourccs */
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Miro/Pinnacle Motion JPEG Video");
      break;

    case GST_MAKE_FOURCC ('S', 'P', '5', '3'):
    case GST_MAKE_FOURCC ('S', 'P', '5', '4'):
    case GST_MAKE_FOURCC ('S', 'P', '5', '5'):
    case GST_MAKE_FOURCC ('S', 'P', '5', '6'):
    case GST_MAKE_FOURCC ('S', 'P', '5', '7'):
    case GST_MAKE_FOURCC ('S', 'P', '5', '8'):
      caps = gst_caps_new_simple ("video/sp5x", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Sp5x-like JPEG");
      break;

    case GST_MAKE_FOURCC ('H', 'F', 'Y', 'U'):
      caps = gst_caps_new_simple ("video/x-huffyuv", NULL);
      if (strf) {
        gst_caps_set_simple (caps, "bpp",
            G_TYPE_INT, (int) strf->bit_cnt, NULL);
      }
      if (codec_name)
        *codec_name = g_strdup ("Huffman Lossless Codec");
      break;

    case GST_MAKE_FOURCC ('M', 'P', 'E', 'G'):
    case GST_MAKE_FOURCC ('M', 'P', 'G', 'I'):
    case GST_MAKE_FOURCC ('m', 'p', 'g', '1'):
    case GST_MAKE_FOURCC ('M', 'P', 'G', '1'):
    case GST_MAKE_FOURCC ('P', 'I', 'M', '1'):
      caps = gst_caps_new_simple ("video/mpeg",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "mpegversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG-1 video");
      break;
    case GST_MAKE_FOURCC ('M', 'P', 'G', '2'):
    case GST_MAKE_FOURCC ('m', 'p', 'g', '2'):
      caps = gst_caps_new_simple ("video/mpeg",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "mpegversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG-2 video");
      break;

    case GST_MAKE_FOURCC ('H', '2', '6', '3'):
    case GST_MAKE_FOURCC ('h', '2', '6', '3'):
    case GST_MAKE_FOURCC ('i', '2', '6', '3'):
    case GST_MAKE_FOURCC ('U', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ITU H.26n");
      break;

    case GST_MAKE_FOURCC ('L', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Lead H.263");
      break;

    case GST_MAKE_FOURCC ('M', '2', '6', '3'):
    case GST_MAKE_FOURCC ('m', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft H.263");
      break;

    case GST_MAKE_FOURCC ('V', 'D', 'O', 'W'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("VDOLive");
      break;

    case GST_MAKE_FOURCC ('V', 'I', 'V', 'O'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Vivo H.263");
      break;

    case GST_MAKE_FOURCC ('x', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Xirlink H.263");
      break;

      /* apparently not standard H.263...? */
    case GST_MAKE_FOURCC ('I', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-intel-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Intel H.263");
      break;

    case GST_MAKE_FOURCC ('h', '2', '6', '4'):
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ITU H.264");
      break;

    case GST_MAKE_FOURCC ('V', 'S', 'S', 'H'):
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      if (codec_name)
        *codec_name = g_strdup ("VideoSoft H.264");
      break;

    case GST_MAKE_FOURCC ('D', 'I', 'V', '3'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '3'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '4'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '4'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '5'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '5'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '6'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', '6'):
    case GST_MAKE_FOURCC ('M', 'P', 'G', '3'):
    case GST_MAKE_FOURCC ('m', 'p', 'g', '3'):
    case GST_MAKE_FOURCC ('c', 'o', 'l', '0'):
    case GST_MAKE_FOURCC ('C', 'O', 'L', '0'):
    case GST_MAKE_FOURCC ('c', 'o', 'l', '1'):
    case GST_MAKE_FOURCC ('C', 'O', 'L', '1'):
    case GST_MAKE_FOURCC ('A', 'P', '4', '1'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MS-MPEG-4 Version 3");
      break;

    case GST_MAKE_FOURCC ('d', 'i', 'v', 'x'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MPEG-4 Version 4");
      break;

    case GST_MAKE_FOURCC ('B', 'L', 'Z', '0'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Blizzard DivX");
      break;

    case GST_MAKE_FOURCC ('D', 'X', '5', '0'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 5, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MPEG-4 Version 5");
      break;

    case GST_MAKE_FOURCC ('X', 'V', 'I', 'D'):
    case GST_MAKE_FOURCC ('x', 'v', 'i', 'd'):
      caps = gst_caps_new_simple ("video/x-xvid", NULL);
      if (codec_name)
        *codec_name = g_strdup ("XVID MPEG-4");
      break;

    case GST_MAKE_FOURCC ('M', 'P', 'G', '4'):
    case GST_MAKE_FOURCC ('M', 'P', '4', 'S'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 41, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.1");
      break;

    case GST_MAKE_FOURCC ('m', 'p', '4', '2'):
    case GST_MAKE_FOURCC ('M', 'P', '4', '2'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 42, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.2");
      break;

    case GST_MAKE_FOURCC ('m', 'p', '4', '3'):
    case GST_MAKE_FOURCC ('M', 'P', '4', '3'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 43, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.3");
      break;

    case GST_MAKE_FOURCC ('M', '4', 'S', '2'):
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft ISO MPEG-4 1.1");
      break;

    case GST_MAKE_FOURCC ('F', 'M', 'P', '4'):
    case GST_MAKE_FOURCC ('U', 'M', 'P', '4'):
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("FFmpeg MPEG-4");
      break;

    case GST_MAKE_FOURCC ('3', 'i', 'v', 'd'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', 'D'):
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.3");        /* FIXME? */
      return gst_caps_from_string ("video/x-msmpeg, msmpegversion = (int) 43");

    case GST_MAKE_FOURCC ('3', 'I', 'V', '1'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-3ivx", NULL);
      if (codec_name)
        *codec_name = g_strdup ("3ivx");
      break;

    case GST_MAKE_FOURCC ('D', 'V', 'S', 'D'):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
    case GST_MAKE_FOURCC ('C', 'D', 'V', 'C'):
      caps = gst_caps_new_simple ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Generic DV");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '1'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 7");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 8");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '3'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 9");
      break;

    case GST_MAKE_FOURCC ('c', 'v', 'i', 'd'):
      caps = gst_caps_new_simple ("video/x-cinepak", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Cinepak video");
      break;

    case GST_MAKE_FOURCC ('M', 'S', 'V', 'C'):
    case GST_MAKE_FOURCC ('m', 's', 'v', 'c'):
    case GST_MAKE_FOURCC ('C', 'R', 'A', 'M'):
    case GST_MAKE_FOURCC ('c', 'r', 'a', 'm'):
    case GST_MAKE_FOURCC ('W', 'H', 'A', 'M'):
    case GST_MAKE_FOURCC ('w', 'h', 'a', 'm'):
      caps = gst_caps_new_simple ("video/x-msvideocodec",
          "msvideoversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MS video v1");
      palette = strf_data;
      strf_data = NULL;
      break;

    case GST_MAKE_FOURCC ('R', 'L', 'E', ' '):
    case GST_MAKE_FOURCC ('m', 'r', 'l', 'e'):
    case GST_MAKE_FOURCC (0x1, 0x0, 0x0, 0x0): /* why, why, why? */
      caps = gst_caps_new_simple ("video/x-rle",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      palette = strf_data;
      strf_data = NULL;
      if (strf) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) strf->bit_cnt, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      if (codec_name)
        *codec_name = g_strdup ("Microsoft RLE");
      break;

    case GST_MAKE_FOURCC ('X', 'x', 'a', 'n'):
      caps = gst_caps_new_simple ("video/x-xan",
          "wcversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Xan Wing Commander 4");
      break;

    case GST_MAKE_FOURCC ('R', 'T', '2', '1'):
      caps = gst_caps_new_simple ("video/x-indeo",
          "indeoversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Intel Video 2");
      break;

    case GST_MAKE_FOURCC ('I', 'V', '3', '1'):
    case GST_MAKE_FOURCC ('I', 'V', '3', '2'):
    case GST_MAKE_FOURCC ('i', 'v', '3', '1'):
    case GST_MAKE_FOURCC ('i', 'v', '3', '2'):
      caps = gst_caps_new_simple ("video/x-indeo",
          "indeoversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Intel Video 3");
      break;

    case GST_MAKE_FOURCC ('I', 'V', '4', '1'):
    case GST_MAKE_FOURCC ('i', 'v', '4', '1'):
      caps = gst_caps_new_simple ("video/x-indeo",
          "indeoversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Intel Video 4");
      break;

    case GST_MAKE_FOURCC ('I', 'V', '5', '0'):
      caps = gst_caps_new_simple ("video/x-indeo",
          "indeoversion", G_TYPE_INT, 5, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Intel Video 5");
      break;

    case GST_MAKE_FOURCC ('M', 'S', 'Z', 'H'):
      caps = gst_caps_new_simple ("video/x-mszh", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Lossless MSZH Video");
      break;

    case GST_MAKE_FOURCC ('Z', 'L', 'I', 'B'):
      caps = gst_caps_new_simple ("video/x-zlib", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Lossless zlib video");
      break;

    case GST_MAKE_FOURCC ('C', 'L', 'J', 'R'):
      caps = gst_caps_new_simple ("video/x-cirrus-logic-accupak", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Cirrus Logipak AccuPak");
      break;

    case GST_MAKE_FOURCC ('C', 'Y', 'U', 'V'):
    case GST_MAKE_FOURCC ('c', 'y', 'u', 'v'):
      caps = gst_caps_new_simple ("video/x-compressed-yuv", NULL);
      if (codec_name)
        *codec_name = g_strdup ("CYUV Lossless");
      break;

    case GST_MAKE_FOURCC ('D', 'U', 'C', 'K'):
      caps = gst_caps_new_simple ("video/x-truemotion",
          "trueversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Duck Truemotion1");
      break;

    case GST_MAKE_FOURCC ('T', 'M', '2', '0'):
      caps = gst_caps_new_simple ("video/x-truemotion",
          "trueversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("TrueMotion 2.0");
      break;

    case GST_MAKE_FOURCC ('V', 'P', '3', '0'):
    case GST_MAKE_FOURCC ('v', 'p', '3', '0'):
    case GST_MAKE_FOURCC ('V', 'P', '3', '1'):
    case GST_MAKE_FOURCC ('v', 'p', '3', '1'):
    case GST_MAKE_FOURCC ('V', 'P', '3', ' '):
      caps = gst_caps_new_simple ("video/x-vp3", NULL);
      if (codec_name)
        *codec_name = g_strdup ("VP3");
      break;

    case GST_MAKE_FOURCC ('U', 'L', 'T', 'I'):
      caps = gst_caps_new_simple ("video/x-ultimotion", NULL);
      if (codec_name)
        *codec_name = g_strdup ("IBM UltiMotion");
      break;

    case GST_MAKE_FOURCC ('T', 'S', 'C', 'C'):
    case GST_MAKE_FOURCC ('t', 's', 'c', 'c'):
      caps = gst_caps_new_simple ("video/x-camtasia", NULL);
      if (codec_name)
        *codec_name = g_strdup ("TechSmith Camtasia");
      break;

    case GST_MAKE_FOURCC ('V', 'C', 'R', '1'):
      caps = gst_caps_new_simple ("video/x-ati-vcr",
          "vcrversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("ATI VCR 1");
      break;

    case GST_MAKE_FOURCC ('V', 'C', 'R', '2'):
      caps = gst_caps_new_simple ("video/x-ati-vcr",
          "vcrversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("ATI VCR 2");
      break;

    case GST_MAKE_FOURCC ('A', 'S', 'V', '1'):
      caps = gst_caps_new_simple ("video/x-asus",
          "asusversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Asus Video 1");
      break;

    case GST_MAKE_FOURCC ('A', 'S', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-asus",
          "asusversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Asus Video 2");
      break;

    case GST_MAKE_FOURCC ('M', 'P', 'N', 'G'):
    case GST_MAKE_FOURCC ('m', 'p', 'n', 'g'):
    case GST_MAKE_FOURCC ('P', 'N', 'G', ' '):
      caps = gst_caps_new_simple ("image/png", NULL);
      if (codec_name)
        *codec_name = g_strdup ("PNG image");
      break;

    case GST_MAKE_FOURCC ('F', 'L', 'V', '1'):
      caps = gst_caps_new_simple ("video/x-flash-video",
          "flvversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Flash Video 1");
      break;

    default:
      GST_WARNING ("Unknown video fourcc %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (codec_fcc));
      return NULL;
  }

  if (strh != NULL) {
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
        strh->rate, strh->scale, NULL);
  } else {
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  }

  if (strf != NULL) {
    gst_caps_set_simple (caps,
        "width", G_TYPE_INT, strf->width,
        "height", G_TYPE_INT, strf->height, NULL);
  } else {
    gst_caps_set_simple (caps,
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
  }

  /* extradata */
  if (strf_data || strd_data) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER,
        strf_data ? strf_data : strd_data, NULL);
  }

  /* palette */
  if (palette && GST_BUFFER_SIZE (palette) >= 256 * 4) {
    GstBuffer *copy = gst_buffer_copy (palette);

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    gint n;
    guint32 *data = (guint32 *) GST_BUFFER_DATA (copy);

    /* own endianness */
    for (n = 0; n < 256; n++)
      data[n] = GUINT32_FROM_LE (data[n]);
#endif
    gst_caps_set_simple (caps, "palette_data", GST_TYPE_BUFFER, copy, NULL);
    gst_buffer_unref (copy);
  }

  return caps;
}

static const struct
{
  const guint32 ms_mask;
  const GstAudioChannelPosition gst_pos;
} layout_mapping[] = {
  {
  0x00001, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  0x00002, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
  0x00004, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
  0x00008, GST_AUDIO_CHANNEL_POSITION_LFE}, {
  0x00010, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
  0x00020, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
  0x00040, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
  0x00080, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
  0x00100, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
  0x00200, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
  0x00400, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
  0x00800, GST_AUDIO_CHANNEL_POSITION_INVALID}, /* TOP_CENTER       */
  {
  0x01000, GST_AUDIO_CHANNEL_POSITION_INVALID}, /* TOP_FRONT_LEFT   */
  {
  0x02000, GST_AUDIO_CHANNEL_POSITION_INVALID}, /* TOP_FRONT_CENTER */
  {
  0x04000, GST_AUDIO_CHANNEL_POSITION_INVALID}, /* TOP_FRONT_RIGHT  */
  {
  0x08000, GST_AUDIO_CHANNEL_POSITION_INVALID}, /* TOP_BACK_LEFT    */
  {
  0x10000, GST_AUDIO_CHANNEL_POSITION_INVALID}, /* TOP_BACK_CENTER  */
  {
  0x20000, GST_AUDIO_CHANNEL_POSITION_INVALID}  /* TOP_BACK_RIGHT   */
};

#define MAX_CHANNEL_POSITIONS G_N_ELEMENTS (layout_mapping)

static gboolean
gst_riff_wavext_add_channel_layout (GstCaps * caps, guint32 layout)
{
  GstAudioChannelPosition pos[MAX_CHANNEL_POSITIONS];
  GstStructure *s;
  gint num_channels, i, p;

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (s, "channels", &num_channels))
    g_return_val_if_reached (FALSE);

  if (num_channels < 2 || num_channels > MAX_CHANNEL_POSITIONS) {
    GST_DEBUG ("invalid number of channels: %d", num_channels);
    return FALSE;
  }

  p = 0;
  for (i = 0; i < MAX_CHANNEL_POSITIONS; ++i) {
    if ((layout & layout_mapping[i].ms_mask) != 0) {
      if (p >= num_channels) {
        GST_WARNING ("More bits set in the channel layout map than there "
            "are channels! Broken file");
        return FALSE;
      }
      if (layout_mapping[i].gst_pos == GST_AUDIO_CHANNEL_POSITION_INVALID) {
        GST_WARNING ("Unsupported channel position (mask 0x%08x) in channel "
            "layout map - ignoring those channels", layout_mapping[i].ms_mask);
        /* what to do? just ignore it and let downstream deal with a channel
         * layout that has INVALID positions in it for now ... */
      }
      pos[p] = layout_mapping[i].gst_pos;
      ++p;
    }
  }

  if (p != num_channels) {
    GST_WARNING ("Only %d bits set in the channel layout map, but there are "
        "supposed to be %d channels! Broken file", p, num_channels);
    return FALSE;
  }

  gst_audio_set_channel_positions (s, pos);
  return TRUE;
}

GstCaps *
gst_riff_create_audio_caps (guint16 codec_id,
    gst_riff_strh * strh, gst_riff_strf_auds * strf,
    GstBuffer * strf_data, GstBuffer * strd_data, char **codec_name)
{
  gboolean block_align = FALSE, rate_chan = TRUE;
  GstCaps *caps = NULL;
  gint rate_min = 1000, rate_max = 96000;
  gint channels_max = 2;

  switch (codec_id) {
    case GST_RIFF_WAVE_FORMAT_MPEGL3:  /* mp3 */
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG-1 layer 3");
      break;

    case GST_RIFF_WAVE_FORMAT_MPEGL12: /* mp1 or mp2 */
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG-1 layer 2");
      break;

    case GST_RIFF_WAVE_FORMAT_PCM:     /* PCM */
      if (strf != NULL) {
        gint ba = strf->blockalign;
        gint ch = strf->channels;
        gint ws = strf->size;

        caps = gst_caps_new_simple ("audio/x-raw-int", "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, "channels", G_TYPE_INT, ch,   /* needed for _add_layout() */
            "width", G_TYPE_INT, (int) (ba * 8 / ch),
            "depth", G_TYPE_INT, ws, "signed", G_TYPE_BOOLEAN, ws != 8, NULL);

        /* Add default MS channel layout if we have more than 2 channels,
         * but the layout isn't specified like with WAVEEXT below. Not sure
         * if this is right, but at least it makes sound output work at all
         * in those cases. Somebody with a a 5.1 setup should double-check
         * with chan-id.wav */
        if (ch > 2) {
          guint32 channel_mask;

          switch (ch) {
            case 4:
              channel_mask = 0x33;
              break;
            case 6:
              channel_mask = 0x3f;
              break;
            default:
              GST_WARNING ("don't know default layout for %d channels", ch);
              channel_mask = 0;
              break;
          }

          if (channel_mask) {
            GST_DEBUG ("using default channel layout for %d channels", ch);
            if (!gst_riff_wavext_add_channel_layout (caps, channel_mask)) {
              GST_WARNING ("failed to add channel layout");
            }
          }
        }
      } else {
        /* FIXME: this is pretty useless - we need fixed caps */
        caps = gst_caps_from_string ("audio/x-raw-int, "
            "endianness = (int) LITTLE_ENDIAN, "
            "signed = (boolean) { true, false }, "
            "width = (int) { 8, 16, 24, 32 }, "
            "depth = (int) { 8, 16, 24, 32 }");
      }
      if (codec_name && strf)
        *codec_name = g_strdup_printf ("Uncompressed %d-bit PCM audio",
            strf->size);
      break;

    case GST_RIFF_WAVE_FORMAT_ADPCM:
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ADPCM audio");
      block_align = TRUE;
      break;

    case GST_RIFF_WAVE_FORMAT_DVI_ADPCM:
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "dvi", NULL);
      if (codec_name)
        *codec_name = g_strdup ("DVI ADPCM audio");
      block_align = TRUE;
      break;

    case GST_RIFF_WAVE_FORMAT_MULAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of mulaw audio, overwriting.",
            strf->size);
        strf->size = 8;
        strf->av_bps = 8;
        strf->blockalign = strf->av_bps * strf->channels;
      }
      if (strf != NULL && (strf->av_bps == 0 || strf->blockalign == 0)) {
        GST_WARNING ("fixing av_bps (%d) and blockalign (%d) of mulaw audio",
            strf->av_bps, strf->blockalign);
        strf->av_bps = strf->size;
        strf->blockalign = strf->av_bps * strf->channels;
      }
      caps = gst_caps_new_simple ("audio/x-mulaw", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Mu-law audio");
      break;

    case GST_RIFF_WAVE_FORMAT_ALAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of alaw audio, overwriting.",
            strf->size);
        strf->size = 8;
        strf->av_bps = 8;
        strf->blockalign = strf->av_bps * strf->channels;
      }
      if (strf != NULL && (strf->av_bps == 0 || strf->blockalign == 0)) {
        GST_WARNING ("fixing av_bps (%d) and blockalign (%d) of alaw audio",
            strf->av_bps, strf->blockalign);
        strf->av_bps = strf->size;
        strf->blockalign = strf->av_bps * strf->channels;
      }
      caps = gst_caps_new_simple ("audio/x-alaw", NULL);
      if (codec_name)
        *codec_name = g_strdup ("A-law audio");
      break;

    case GST_RIFF_WAVE_FORMAT_VORBIS1: /* ogg/vorbis mode 1 */
    case GST_RIFF_WAVE_FORMAT_VORBIS2: /* ogg/vorbis mode 2 */
    case GST_RIFF_WAVE_FORMAT_VORBIS3: /* ogg/vorbis mode 3 */
    case GST_RIFF_WAVE_FORMAT_VORBIS1PLUS:     /* ogg/vorbis mode 1+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS2PLUS:     /* ogg/vorbis mode 2+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS3PLUS:     /* ogg/vorbis mode 3+ */
      caps = gst_caps_new_simple ("audio/x-vorbis", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Vorbis");
      break;

    case GST_RIFF_WAVE_FORMAT_A52:
      channels_max = 6;
      caps = gst_caps_new_simple ("audio/x-ac3", NULL);
      if (codec_name)
        *codec_name = g_strdup ("AC-3 audio");
      break;
    case GST_RIFF_WAVE_FORMAT_WMAV1:
    case GST_RIFF_WAVE_FORMAT_WMAV2:
    case GST_RIFF_WAVE_FORMAT_WMAV3:
    {
      gint version = (codec_id - GST_RIFF_WAVE_FORMAT_WMAV1) + 1;

      channels_max = 6;

      block_align = TRUE;

      caps = gst_caps_new_simple ("audio/x-wma",
          "wmaversion", G_TYPE_INT, version, NULL);

      if (codec_name)
        *codec_name = g_strdup_printf ("WMA Version %d", version + 6);

      if (strf != NULL) {
        gst_caps_set_simple (caps,
            "bitrate", G_TYPE_INT, strf->av_bps * 8, NULL);
      } else {
        gst_caps_set_simple (caps,
            "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);
      }
      break;
    }
    case GST_RIFF_WAVE_FORMAT_SONY_ATRAC3:
      caps = gst_caps_new_simple ("audio/x-vnd.sony.atrac3", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Sony ATRAC3");
      break;

    case GST_RIFF_WAVE_FORMAT_EXTENSIBLE:{
      guint16 valid_bits_per_sample;
      guint32 channel_mask;
      guint32 subformat_guid[4];
      const guint8 *data;

      if (GST_BUFFER_SIZE (strf_data) != 22) {
        GST_WARNING ("WAVE_FORMAT_EXTENSIBLE data size is %d (expected: 22)",
            GST_BUFFER_SIZE (strf_data));
        return NULL;
      }

      data = GST_BUFFER_DATA (strf_data);
      valid_bits_per_sample = GST_READ_UINT16_LE (data);
      channel_mask = GST_READ_UINT32_LE (data + 2);
      subformat_guid[0] = GST_READ_UINT32_LE (data + 6);
      subformat_guid[1] = GST_READ_UINT32_LE (data + 10);
      subformat_guid[2] = GST_READ_UINT32_LE (data + 14);
      subformat_guid[3] = GST_READ_UINT32_LE (data + 18);

      GST_DEBUG ("valid bps    = %u", valid_bits_per_sample);
      GST_DEBUG ("channel mask = 0x%08x", channel_mask);
      GST_DEBUG ("GUID         = %08x-%08x-%08x-%08x", subformat_guid[0],
          subformat_guid[1], subformat_guid[2], subformat_guid[3]);

      if (subformat_guid[1] == 0x00100000 &&
          subformat_guid[2] == 0xaa000080 && subformat_guid[3] == 0x719b3800) {
        if (subformat_guid[0] == 0x00000001) {
          GST_DEBUG ("PCM");
          if (strf != NULL) {
            gint ba = strf->blockalign;
            gint ws = strf->size;
            gint depth = ws;

            if (valid_bits_per_sample != 0)
              depth = valid_bits_per_sample;

            caps = gst_caps_new_simple ("audio/x-raw-int",
                "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
                "channels", G_TYPE_INT, strf->channels,
                "width", G_TYPE_INT, (int) (ba * 8 / strf->channels),
                "depth", G_TYPE_INT, depth,
                "rate", G_TYPE_INT, strf->rate,
                "signed", G_TYPE_BOOLEAN, (depth > 8) ? TRUE : FALSE, NULL);

            if (!gst_riff_wavext_add_channel_layout (caps, channel_mask)) {
              GST_WARNING ("failed to add channel layout");
              gst_caps_unref (caps);
              caps = NULL;
            }
            rate_chan = FALSE;

            if (codec_name) {
              *codec_name = g_strdup_printf ("Uncompressed %d-bit PCM audio",
                  strf->size);
            }
          }
        } else if (subformat_guid[0] == 0x00000003) {
          GST_DEBUG ("FIXME: handle IEEE float format");
        }

        if (caps == NULL) {
          GST_WARNING ("Unknown WAVE_FORMAT_EXTENSIBLE audio format");
          return NULL;
        }
      }
      break;
    }
    default:
      GST_WARNING ("Unknown audio tag 0x%04x", codec_id);
      return NULL;
  }

  if (strf != NULL) {
    if (rate_chan) {
      gst_caps_set_simple (caps,
          "rate", G_TYPE_INT, strf->rate,
          "channels", G_TYPE_INT, strf->channels, NULL);
    }
    if (block_align) {
      gst_caps_set_simple (caps,
          "block_align", G_TYPE_INT, strf->blockalign, NULL);
    }
  } else {
    if (rate_chan) {
      gst_caps_set_simple (caps,
          "rate", GST_TYPE_INT_RANGE, rate_min, rate_max,
          "channels", GST_TYPE_INT_RANGE, 1, channels_max, NULL);
    }
    if (block_align) {
      gst_caps_set_simple (caps,
          "block_align", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    }
  }

  /* extradata */
  if (strf_data || strd_data) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER,
        strf_data ? strf_data : strd_data, NULL);
  }

  return caps;
}

GstCaps *
gst_riff_create_iavs_caps (guint32 codec_fcc,
    gst_riff_strh * strh, gst_riff_strf_iavs * strf,
    GstBuffer * init_data, GstBuffer * extra_data, char **codec_name)
{
  GstCaps *caps = NULL;

  switch (codec_fcc) {
      /* is this correct? */
    case GST_MAKE_FOURCC ('D', 'V', 'S', 'D'):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
      caps = gst_caps_new_simple ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Generic DV");
      break;

    default:
      GST_WARNING ("Unknown IAVS fourcc %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (codec_fcc));
      return NULL;
  }

  return caps;
}

/*
 * Functions below are for template caps. All is variable.
 */

GstCaps *
gst_riff_create_video_template_caps (void)
{
  guint32 tags[] = {
    GST_MAKE_FOURCC ('I', '4', '2', '0'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
    GST_MAKE_FOURCC ('M', 'J', 'P', 'G'),
    GST_MAKE_FOURCC ('D', 'V', 'S', 'D'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '1'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '2'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'),
    GST_MAKE_FOURCC ('M', 'P', 'G', '4'),
    GST_MAKE_FOURCC ('M', 'P', '4', '2'),
    GST_MAKE_FOURCC ('M', 'P', '4', '3'),
    GST_MAKE_FOURCC ('H', 'F', 'Y', 'U'),
    GST_MAKE_FOURCC ('D', 'I', 'V', '3'),
    GST_MAKE_FOURCC ('M', 'P', 'E', 'G'),
    GST_MAKE_FOURCC ('H', '2', '6', '3'),
    GST_MAKE_FOURCC ('I', '2', '6', '3'),
    GST_MAKE_FOURCC ('h', '2', '6', '4'),
    GST_MAKE_FOURCC ('D', 'I', 'V', 'X'),
    GST_MAKE_FOURCC ('D', 'X', '5', '0'),
    GST_MAKE_FOURCC ('X', 'V', 'I', 'D'),
    GST_MAKE_FOURCC ('3', 'I', 'V', '1'),
    GST_MAKE_FOURCC ('c', 'v', 'i', 'd'),
    GST_MAKE_FOURCC ('m', 's', 'v', 'c'),
    GST_MAKE_FOURCC ('R', 'L', 'E', ' '),
    GST_MAKE_FOURCC ('D', 'I', 'B', ' '),
    GST_MAKE_FOURCC ('X', 'x', 'a', 'n'),
    GST_MAKE_FOURCC ('I', 'V', '3', '2'),
    GST_MAKE_FOURCC ('I', 'V', '5', '0'),
    GST_MAKE_FOURCC ('M', '4', 'S', '2'),
    GST_MAKE_FOURCC ('M', 'S', 'Z', 'H'),
    GST_MAKE_FOURCC ('Z', 'L', 'I', 'B'),
    GST_MAKE_FOURCC ('A', 'S', 'V', '1'),
    GST_MAKE_FOURCC ('A', 'S', 'V', '2'),
    GST_MAKE_FOURCC ('V', 'C', 'R', '1'),
    GST_MAKE_FOURCC ('V', 'C', 'R', '2'),
    GST_MAKE_FOURCC ('C', 'L', 'J', 'R'),
    GST_MAKE_FOURCC ('I', 'V', '4', '1'),
    GST_MAKE_FOURCC ('R', 'T', '2', '1'),
    GST_MAKE_FOURCC ('D', 'U', 'C', 'K'),
    GST_MAKE_FOURCC ('T', 'M', '2', '0'),
    GST_MAKE_FOURCC ('U', 'L', 'T', 'I'),
    GST_MAKE_FOURCC ('V', 'P', '3', ' '),
    GST_MAKE_FOURCC ('T', 'S', 'C', 'C'),
    GST_MAKE_FOURCC ('S', 'P', '5', '3'),
    GST_MAKE_FOURCC ('P', 'N', 'G', ' '),
    GST_MAKE_FOURCC ('C', 'Y', 'U', 'V'),
    GST_MAKE_FOURCC ('F', 'L', 'V', '1'),
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_video_caps (tags[i], NULL, NULL, NULL, NULL, NULL);
    if (one)
      gst_caps_append (caps, one);
  }

  return caps;
}

GstCaps *
gst_riff_create_audio_template_caps (void)
{
  guint16 tags[] = {
    GST_RIFF_WAVE_FORMAT_MPEGL3,
    GST_RIFF_WAVE_FORMAT_MPEGL12,
    GST_RIFF_WAVE_FORMAT_PCM,
    GST_RIFF_WAVE_FORMAT_VORBIS1,
    GST_RIFF_WAVE_FORMAT_A52,
    GST_RIFF_WAVE_FORMAT_ALAW,
    GST_RIFF_WAVE_FORMAT_MULAW,
    GST_RIFF_WAVE_FORMAT_ADPCM,
    GST_RIFF_WAVE_FORMAT_DVI_ADPCM,
    GST_RIFF_WAVE_FORMAT_WMAV1,
    GST_RIFF_WAVE_FORMAT_WMAV2,
    GST_RIFF_WAVE_FORMAT_WMAV3,
    GST_RIFF_WAVE_FORMAT_SONY_ATRAC3,
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_audio_caps (tags[i], NULL, NULL, NULL, NULL, NULL);
    if (one)
      gst_caps_append (caps, one);
  }

  return caps;
}

GstCaps *
gst_riff_create_iavs_template_caps (void)
{
  guint32 tags[] = {
    GST_MAKE_FOURCC ('D', 'V', 'S', 'D'),
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_iavs_caps (tags[i], NULL, NULL, NULL, NULL, NULL);
    if (one)
      gst_caps_append (caps, one);
  }

  return caps;
}
