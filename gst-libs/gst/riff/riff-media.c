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

GstCaps *
gst_riff_create_video_caps (guint32 codec_fcc,
    gst_riff_strh * strh, gst_riff_strf_vids * strf, char **codec_name)
{
  GstCaps *caps = NULL;

  switch (codec_fcc) {
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

    case GST_MAKE_FOURCC ('H', 'F', 'Y', 'U'):
      caps = gst_caps_new_simple ("video/x-huffyuv", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Huffman Lossless Codec");
      break;

    case GST_MAKE_FOURCC ('M', 'P', 'E', 'G'):
    case GST_MAKE_FOURCC ('M', 'P', 'G', 'I'):
      caps = gst_caps_new_simple ("video/mpeg",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "mpegversion", G_TYPE_BOOLEAN, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG video");
      break;

    case GST_MAKE_FOURCC ('H', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ITU H.26n");
      break;
    case GST_MAKE_FOURCC ('i', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("ITU H.263");
      break;
    case GST_MAKE_FOURCC ('L', '2', '6', '3'):
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Lead H.263");
      break;
    case GST_MAKE_FOURCC ('M', '2', '6', '3'):
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

    case GST_MAKE_FOURCC ('D', 'I', 'V', '3'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '4'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '5'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '6'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MS-MPEG-4 Version 3");
      break;
    case GST_MAKE_FOURCC ('d', 'i', 'v', 'x'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MPEG-4 Version 4");
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
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 41, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.1");
      break;

    case GST_MAKE_FOURCC ('M', 'P', '4', '2'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 42, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.2");
      break;

    case GST_MAKE_FOURCC ('M', 'P', '4', '3'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 43, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.3");
      break;

    case GST_MAKE_FOURCC ('3', 'I', 'V', '1'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-3ivx", NULL);
      if (codec_name)
        *codec_name = g_strdup ("3ivx");
      break;

    case GST_MAKE_FOURCC ('D', 'V', 'S', 'D'):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
      caps = gst_caps_new_simple ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Generic DV");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '1'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Windows Media Video 7");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Windows Media Video 8");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '3'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Windows Media Video 9");
      break;

    case GST_MAKE_FOURCC ('c', 'v', 'i', 'd'):
      caps = gst_caps_new_simple ("video/x-cinepak", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Cinepak video");
      break;

    default:
      GST_WARNING ("Unkown video fourcc " GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (codec_fcc));
      return NULL;
  }

  if (strh != NULL) {
    gdouble fps = 1. * strh->rate / strh->scale;

    gst_caps_set_simple (caps, "framerate", G_TYPE_DOUBLE, fps, NULL);
  } else {
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_DOUBLE_RANGE, 0., G_MAXDOUBLE, NULL);
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

  return caps;
}

GstCaps *
gst_riff_create_audio_caps (guint16 codec_id,
    gst_riff_strh * strh, gst_riff_strf_auds * strf, char **codec_name)
{
  GstCaps *caps = NULL;

  switch (codec_id) {
    case GST_RIFF_WAVE_FORMAT_MPEGL3:  /* mp3 */
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG 1 layer 3");
      break;

    case GST_RIFF_WAVE_FORMAT_MPEGL12: /* mp1 or mp2 */
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("MPEG 1 layer 2");
      break;

    case GST_RIFF_WAVE_FORMAT_PCM:     /* PCM */
      if (strf != NULL) {
        gint ba = GUINT16_FROM_LE (strf->blockalign);
        gint ch = GUINT16_FROM_LE (strf->channels);
        gint ws = GUINT16_FROM_LE (strf->size);

        caps = gst_caps_new_simple ("audio/x-raw-int",
            "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
            "width", G_TYPE_INT, (int) (ba * 8 / ch),
            "depth", G_TYPE_INT, ws, "signed", G_TYPE_BOOLEAN, ws != 8, NULL);
      } else {
        caps = gst_caps_from_string ("audio/x-raw-int, "
            "endianness = (int) LITTLE_ENDIAN, "
            "signed = (boolean) { true, false }, "
            "width = (int) { 8, 16 }, " "height = (int) { 8, 16 }");
      }
      if (codec_name)
        *codec_name = g_strdup ("Uncompressed PCM audio");
      break;

    case GST_RIFF_WAVE_FORMAT_MULAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of mulaw audio, overwriting.",
            strf->size);
      }
      caps = gst_caps_new_simple ("audio/x-mulaw", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Mulaw");
      break;

    case GST_RIFF_WAVE_FORMAT_ALAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of alaw audio, overwriting.",
            strf->size);
      }
      caps = gst_caps_new_simple ("audio/x-alaw", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Alaw");
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
      caps = gst_caps_new_simple ("audio/x-ac3", NULL);
      if (codec_name)
        *codec_name = g_strdup ("AC3");
      break;

    default:
      GST_WARNING ("Unkown audio tag 0x%04x", codec_id);
      return NULL;
  }

  if (strf != NULL) {
    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, strf->rate,
        "channels", G_TYPE_INT, strf->channels, NULL);
  } else {
    gst_caps_set_simple (caps,
        "rate", GST_TYPE_INT_RANGE, 8000, 96000,
        "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
  }

  return caps;
}

GstCaps *
gst_riff_create_iavs_caps (guint32 codec_fcc,
    gst_riff_strh * strh, gst_riff_strf_iavs * strf, char **codec_name)
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

    default:
      GST_WARNING ("Unkown IAVS fourcc " GST_FOURCC_FORMAT,
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
    GST_MAKE_FOURCC ('M', 'P', 'G', '4'),
    GST_MAKE_FOURCC ('M', 'P', '4', '2'),
    GST_MAKE_FOURCC ('M', 'P', '4', '3'),
    GST_MAKE_FOURCC ('H', 'F', 'Y', 'U'),
    GST_MAKE_FOURCC ('D', 'I', 'V', '3'),
    GST_MAKE_FOURCC ('M', 'P', 'E', 'G'),
    GST_MAKE_FOURCC ('H', '2', '6', '3'),
    GST_MAKE_FOURCC ('D', 'I', 'V', 'X'),
    GST_MAKE_FOURCC ('X', 'V', 'I', 'D'),
    GST_MAKE_FOURCC ('3', 'I', 'V', '1'),
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_video_caps (tags[i], NULL, NULL, NULL);
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
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps, *one;

  caps = gst_caps_new_empty ();
  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_audio_caps (tags[i], NULL, NULL, NULL);
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
    one = gst_riff_create_iavs_caps (tags[i], NULL, NULL, NULL);
    if (one)
      gst_caps_append (caps, one);
  }

  return caps;
}
