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
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif
#include <string.h>

#include "gstffmpegcodecmap.h"

/* this macro makes a caps width fixed or unfixed width/height
 * properties depending on whether we've got a context.
 *
 * See below for why we use this.
 */

#define GST_FF_VID_CAPS_NEW(mimetype, props...)			\
    (context != NULL) ?						\
    gst_caps_new_simple (mimetype,			      	\
	"width",     G_TYPE_INT,   context->width,	      	\
	"height",    G_TYPE_INT,   context->height,	  	\
	"framerate", G_TYPE_DOUBLE, 1. * context->frame_rate /  \
				   context->frame_rate_base,    \
	##props, NULL)	  					\
    :	  							\
    gst_caps_new_simple (mimetype,			      	\
	"width",     GST_TYPE_INT_RANGE, 16, 4096,      	\
	"height",    GST_TYPE_INT_RANGE, 16, 4096,	      	\
	"framerate", GST_TYPE_DOUBLE_RANGE, 0., G_MAXDOUBLE,	\
	##props, NULL)

/* same for audio - now with channels/sample rate
 */

#define GST_FF_AUD_CAPS_NEW(mimetype, props...)			\
    (context != NULL) ?					      	\
    gst_caps_new_simple (mimetype,	      			\
	"rate", G_TYPE_INT, context->sample_rate,		\
	"channels", G_TYPE_INT, context->channels,		\
	##props, NULL)						\
    :								\
    gst_caps_new_simple (mimetype,	      			\
	##props, NULL)

/* Convert a FFMPEG codec ID and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * CodecID is primarily meant for compressed data GstCaps!
 */

GstCaps *
gst_ffmpeg_codecid_to_caps (enum CodecID    codec_id,
                            AVCodecContext *context)
{
  GstCaps *caps = NULL;

  switch (codec_id) {
    case CODEC_ID_MPEG1VIDEO:
      /* this caps doesn't need width/height/framerate */
      caps = gst_caps_new_simple ("video/mpeg",
	  "mpegversion",  G_TYPE_INT,	  1,
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          NULL);
      break;
      
    case CODEC_ID_H263P:
    case CODEC_ID_H263I:
    case CODEC_ID_H263:
      caps = GST_FF_VID_CAPS_NEW ("video/x-h263");
      break;

    case CODEC_ID_RV10:
      caps = GST_FF_VID_CAPS_NEW ("video/x-pn-realvideo",
	  "systemstream", G_TYPE_BOOLEAN, FALSE,
	  NULL);
      break;

    case CODEC_ID_MP2:
      caps = GST_FF_AUD_CAPS_NEW ("audio/mpeg",
	  "mpegversion", G_TYPE_INT, 1,
	  "layer", G_TYPE_INT, 2,
          NULL);
      break;

    case CODEC_ID_MP3LAME:
      caps = GST_FF_AUD_CAPS_NEW ("audio/mpeg",
	  "mpegversion", G_TYPE_INT, 1,
	  "layer", G_TYPE_INT, 3,
	  NULL);
      break;

    /* FIXME: This could become problematic when we fully switched to seperation
       of ogg and vorbis, because ffmpeg does ass ume ogg == ogg/vorbis 
       Maybe we want to disable this? */
    case CODEC_ID_VORBIS:
      caps = GST_FF_AUD_CAPS_NEW ("application/ogg");
      break;
      
    case CODEC_ID_AC3:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-ac3");
      break;

    case CODEC_ID_MJPEG:
    case CODEC_ID_MJPEGB:
    /*case CODEC_ID_LJPEG:*/
      caps = GST_FF_VID_CAPS_NEW ("video/x-jpeg");
      break;

    case CODEC_ID_MPEG4:
      caps = GST_FF_VID_CAPS_NEW ("video/mpeg",
	  "mpegversion",  G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE,
	  NULL);
      gst_caps_append(caps,
	  GST_FF_VID_CAPS_NEW ("video/x-divx",
	      "divxversion", GST_TYPE_INT_RANGE, 4, 5,
	      NULL));
      gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("video/x-xvid"));
      gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("video/x-3ivx"));
      break;

    /* weird quasi-codecs for the demuxers only */
    case CODEC_ID_RAWVIDEO:
      /* we use a shortcut to the raw-video pad function */
      return gst_ffmpeg_codectype_to_caps (CODEC_TYPE_VIDEO, context);

    case CODEC_ID_MSMPEG4V1:
      caps = GST_FF_VID_CAPS_NEW ("video/x-msmpeg",
	  "msmpegversion", G_TYPE_INT, 41,
	  NULL);
      break;

    case CODEC_ID_MSMPEG4V2:
      caps = GST_FF_VID_CAPS_NEW ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 42,
          NULL);
      break;

    case CODEC_ID_MSMPEG4V3:
      caps = GST_FF_VID_CAPS_NEW ("video/x-msmpeg",
	  "msmpegversion", G_TYPE_INT, 43,
	  NULL);
      gst_caps_append(caps,
          GST_FF_VID_CAPS_NEW ("video/x-divx",
	      "divxversion", G_TYPE_INT, 3,
              NULL));
      break;

    case CODEC_ID_WMV1:
      caps = GST_FF_VID_CAPS_NEW ("video/x-wmv",
	  "wmvversion", G_TYPE_INT, 1,
          NULL);
      break;

    case CODEC_ID_WMV2:
      caps = GST_FF_VID_CAPS_NEW ("video/x-wmv",
	  "wmvversion", G_TYPE_INT, 2,
          NULL);
      break;

    case CODEC_ID_SVQ1:
      caps = GST_FF_VID_CAPS_NEW ("video/x-svq",
	  "svqversion", G_TYPE_INT, 1,
          NULL);
      break;

    case CODEC_ID_SVQ3:
      caps = GST_FF_VID_CAPS_NEW ("video/x-svq",
	  "svqversion", G_TYPE_INT, 3,
          NULL);
      break;

    case CODEC_ID_DVAUDIO:
        caps = GST_FF_AUD_CAPS_NEW ("audio/x-dv");
        break;

    case CODEC_ID_DVVIDEO:
      caps = GST_FF_VID_CAPS_NEW ("video/dv");
      break;

    case CODEC_ID_WMAV1:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-wma",
	  "wmaversion",  G_TYPE_INT,	      1,
          "flags1",      GST_TYPE_INT_RANGE,  G_MININT, G_MAXINT,
          "flags2",      GST_TYPE_INT_RANGE,  G_MININT, G_MAXINT,
          "block_align", GST_TYPE_INT_RANGE,  0, G_MAXINT,
          "bitrate",     GST_TYPE_INT_RANGE,  0, G_MAXINT,
	  NULL);
      break;

    case CODEC_ID_WMAV2:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-wma",
	  "wmaversion",  G_TYPE_INT,	      2,
          "flags1",      GST_TYPE_INT_RANGE,  G_MININT, G_MAXINT,
          "flags2",      GST_TYPE_INT_RANGE,  G_MININT, G_MAXINT,
          "block_align", GST_TYPE_INT_RANGE,  0, G_MAXINT,
          "bitrate",     GST_TYPE_INT_RANGE,  0, G_MAXINT,
	  NULL);
      break;

    case CODEC_ID_MACE3:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-mace",
	  "maceversion", G_TYPE_INT, 3,
          NULL);
      break;

    case CODEC_ID_MACE6:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-mace",
	  "maceversion", G_TYPE_INT, 6,
          NULL);
      break;

    case CODEC_ID_HUFFYUV:
      caps = GST_FF_VID_CAPS_NEW ("video/x-huffyuv");
      break;

    case CODEC_ID_CYUV:
      /* .. */
      break;

    case CODEC_ID_H264:
      caps = GST_FF_VID_CAPS_NEW ("video/x-h264");
      break;

    case CODEC_ID_INDEO3:
      caps = GST_FF_VID_CAPS_NEW ("video/x-indeo",
	  "indeoversion", G_TYPE_INT, 3,
	  NULL);
      break;

    case CODEC_ID_VP3:
      caps = GST_FF_VID_CAPS_NEW ("video/x-vp3");
      gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("video/x-theora"));
      break;

    case CODEC_ID_AAC:
      caps = GST_FF_AUD_CAPS_NEW ("audio/mpeg",
	  "systemstream", G_TYPE_BOOLEAN, FALSE,
	  "mpegversion",  G_TYPE_INT,	  2,
	  NULL);
      break;

    case CODEC_ID_MPEG4AAC:
      caps = GST_FF_AUD_CAPS_NEW ("audio/mpeg",
	  "systemstream", G_TYPE_BOOLEAN, FALSE,
	  "mpegversion",  G_TYPE_INT,	  4,
	  NULL);
      break;

    case CODEC_ID_ASV1:
      /* .. */
      break;

    case CODEC_ID_FFV1:
      caps = GST_FF_VID_CAPS_NEW ("video/x-ffv",
	  "ffvversion", G_TYPE_INT, 1,
	  NULL);
      break;

    case CODEC_ID_4XM:
      caps = GST_FF_VID_CAPS_NEW ("video/x-4xm");
      break;

    /* weird quasi-codecs for the demuxers only */
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
      do {
        gint width = 0, depth = 0, endianness = 0;
	gboolean signedness = FALSE; /* blabla */

        switch (codec_id) {
          case CODEC_ID_PCM_S16LE:
            width = 16; depth = 16;
            endianness = G_LITTLE_ENDIAN;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_S16BE:
            width = 16; depth = 16;
            endianness = G_BIG_ENDIAN;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_U16LE:
            width = 16; depth = 16;
            endianness = G_LITTLE_ENDIAN;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_U16BE:
            width = 16; depth = 16;
            endianness = G_BIG_ENDIAN;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_S8:
            width = 8;  depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_U8:
            width = 8;  depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = FALSE;
            break;
          default:
            g_assert(0); /* don't worry, we never get here */
            break;
        }

        caps = GST_FF_AUD_CAPS_NEW ("audio/x-raw-int",
	    "width",      G_TYPE_INT,	  width,
	    "depth",      G_TYPE_INT,	  depth,
	    "endianness", G_TYPE_INT,	  endianness,
	    "signed",     G_TYPE_BOOLEAN, signedness,
	    NULL);
      } while (0);
      break;

    case CODEC_ID_PCM_MULAW:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-mulaw");
      break;

    case CODEC_ID_PCM_ALAW:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-alaw");
      break;

    case CODEC_ID_ADPCM_IMA_QT:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-adpcm",
	  "layout", G_TYPE_STRING, "quicktime",
          NULL);
      break;

    case CODEC_ID_ADPCM_IMA_WAV:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-adpcm",
	  "layout", G_TYPE_STRING, "wav",
          NULL);
      break;

    case CODEC_ID_ADPCM_MS:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-adpcm",
	  "layout", G_TYPE_STRING, "microsoft",
          NULL);
      break;

    case CODEC_ID_ADPCM_4XM:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-adpcm",
	  "layout", G_TYPE_STRING, "4xm",
          NULL);
      break;

    case CODEC_ID_AMR_NB:
      /* .. */
      break;

    case CODEC_ID_RA_144:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-pn-realaudio",
	  "bitrate", G_TYPE_INT, 14400,
          NULL);
      break;

    case CODEC_ID_RA_288:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-pn-realaudio",
	  "bitrate", G_TYPE_INT, 28800,
          NULL);
      break;

    default:
      /* .. */
      break;
  }

  if (caps != NULL) {
    char *str = gst_caps_to_string (caps);
    GST_DEBUG ("caps for codec_id=%d: %s", codec_id, str);
    g_free(str);
  } else {
    GST_WARNING ("No caps found for codec_id=%d", codec_id);
  }

  return caps;
}

/* Convert a FFMPEG Pixel Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_pixfmt_to_caps (enum PixelFormat  pix_fmt,
                           AVCodecContext   *context)
{
  GstCaps *caps = NULL;

  int bpp = 0, depth = 0, endianness = 0;
  gulong g_mask = 0, r_mask = 0, b_mask = 0;
  guint32 fmt = 0;

  switch (pix_fmt) {
    case PIX_FMT_YUV420P:
      fmt = GST_MAKE_FOURCC ('I','4','2','0');
      break;
    case PIX_FMT_YUV422:
      fmt = GST_MAKE_FOURCC ('Y','U','Y','2');
      break;
    case PIX_FMT_RGB24:
      bpp = depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0xff0000; g_mask = 0x00ff00; b_mask = 0x0000ff;
      break;
    case PIX_FMT_BGR24:
      bpp = depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x0000ff; g_mask = 0x00ff00; b_mask = 0xff0000;
      break;
    case PIX_FMT_YUV422P:
      fmt = GST_MAKE_FOURCC ('Y','4','2','B');
      break;
    case PIX_FMT_YUV444P:
      /* .. */
      break;
    case PIX_FMT_RGBA32:
      bpp = 32; depth = 24;
      endianness = G_BIG_ENDIAN;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      r_mask = 0x00ff0000; g_mask = 0x0000ff00; b_mask = 0x000000ff;
#else 
      r_mask = 0x0000ff00; g_mask = 0x00ff0000; b_mask = 0xff000000;
#endif
      break;
    case PIX_FMT_YUV410P:
      fmt = GST_MAKE_FOURCC ('Y','U','V','9');
      break;
    case PIX_FMT_YUV411P:
      fmt = GST_MAKE_FOURCC ('Y','4','1','B');
      break;
    case PIX_FMT_RGB565:
      bpp = depth = 16;
      endianness = G_BYTE_ORDER;
      r_mask = 0xf800; g_mask = 0x07e0; b_mask = 0x001f;
      break;
    case PIX_FMT_RGB555:
      bpp = 16; depth = 15;
      endianness = G_BYTE_ORDER;
      r_mask = 0x7c00; g_mask = 0x03e0; b_mask = 0x001f;
      break;
    default:
      /* give up ... */
      break;
  }

  if (bpp != 0) {
    caps = GST_FF_VID_CAPS_NEW ("video/x-raw-rgb",
	"bpp",	      G_TYPE_INT, bpp,
        "depth",      G_TYPE_INT, depth,
        "red_mask",   G_TYPE_INT, r_mask,
        "green_mask", G_TYPE_INT, g_mask,
        "blue_mask",  G_TYPE_INT, b_mask,
	"endianness", G_TYPE_INT, endianness,
        NULL);
  } else if (fmt) {
    caps = GST_FF_VID_CAPS_NEW ("video/x-raw-yuv",
	"format",     GST_TYPE_FOURCC, fmt,
        NULL);
  }

  if (caps != NULL) {
    char *str = gst_caps_to_string (caps);
    GST_DEBUG ("caps for pix_fmt=%d: %s", pix_fmt, str);
    g_free(str);
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
gst_ffmpeg_smpfmt_to_caps (enum SampleFormat  sample_fmt,
                           AVCodecContext    *context)
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
	"signed",     G_TYPE_BOOLEAN, signedness,
        "endianness", G_TYPE_INT,     G_BYTE_ORDER,
        "width",      G_TYPE_INT,     bpp,
        "depth",      G_TYPE_INT,     bpp,
        NULL);
  }

  if (caps != NULL) {
    char *str = gst_caps_to_string (caps);
    GST_DEBUG ("caps for sample_fmt=%d: %s", sample_fmt, str);
    g_free(str);
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
gst_ffmpeg_codectype_to_caps (enum CodecType  codec_type,
                              AVCodecContext *context)
{
  GstCaps *caps;

  switch (codec_type) {
    case CODEC_TYPE_VIDEO:
      if (context) {
        caps = gst_ffmpeg_pixfmt_to_caps (context->pix_fmt, context);
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


/* Construct the context extradata from caps
 * when needed.
 */
static void
gst_ffmpeg_caps_to_extradata (const GstCaps *caps,
                              AVCodecContext *context)
{
  GstStructure *structure;
  const gchar *mimetype;

  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);
  
  mimetype = gst_structure_get_name (structure);

  if (!strcmp(mimetype, "audio/x-wma")) {
    gint flags1, flags2, wmaversion = 0;

    if (!gst_structure_get_int (structure, "flags1", &flags1) ||
	!gst_structure_get_int (structure, "flags2", &flags2) ||
	!gst_structure_get_int (structure, "wmaversion", &wmaversion)) {
      g_warning ("invalid caps for audio/x-wma");
      return;
    }

    /* 
     * Rebuild context data from flags1 & flags2 
     * see wmadec in ffmpeg/libavcodec/wmadec.c 
     */
    gst_structure_get_int (structure, "wmaversion", &wmaversion);
    switch (wmaversion) {
      case 1:
	/* FIXME: is this freed with g_free? If not, don't use g_malloc */
	context->extradata = (guint8 *) g_malloc0 (4); 
	((guint8 *)context->extradata)[0] = flags1;
	((guint8 *)context->extradata)[2] = flags2;
	context->extradata_size = 4;
	break;
      case 2:
	/* FIXME: is this freed with g_free? If not, don't use g_malloc */
	context->extradata = (guint8 *) g_malloc0 (6);        
	((guint8 *) context->extradata)[0] = flags1;
	((guint8 *) context->extradata)[1] = flags1 >> 8;
	((guint8 *) context->extradata)[2] = flags1 >> 16;
	((guint8 *) context->extradata)[3] = flags1 >> 24;
	((guint8 *) context->extradata)[4] = flags2; 
	((guint8 *) context->extradata)[5] = flags2 >> 8;
	context->extradata_size = 6;
	break;
      default:
	g_warning ("Unknown wma version %d\n", wmaversion);
	break;
    }
  }
}


/* Convert a GstCaps (audio/raw) to a FFMPEG SampleFmt
 * and other audio properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_smpfmt (const GstCaps *caps,
                           AVCodecContext *context)
{
  GstStructure *structure;
  gint depth = 0, width = 0, endianness = 0;
  gboolean signedness = FALSE;

  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);
  
  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "depth", &depth) &&
      gst_structure_get_int (structure, "signed", &signedness) &&
      gst_structure_get_int (structure, "endianness", &endianness)) {
    if (width == 16 && depth == 16 &&
        endianness == G_BYTE_ORDER && signedness == TRUE) {
      context->sample_fmt = SAMPLE_FMT_S16;
    }
  }

  gst_structure_get_int (structure, "channels", &context->channels);
  gst_structure_get_int (structure, "rate", &context->sample_rate);
  gst_structure_get_int (structure, "block_align", &context->block_align);
  gst_structure_get_int (structure, "bitrate", &context->bit_rate);

  gst_ffmpeg_caps_to_extradata (caps, context);
}


/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 * and other video properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_pixfmt (const GstCaps *caps,
                           AVCodecContext *context)
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

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    guint32 fourcc;
    
    if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
      switch (fourcc) {
	case GST_MAKE_FOURCC ('Y','U','Y','2'):
	  context->pix_fmt = PIX_FMT_YUV422;
	  break;
	case GST_MAKE_FOURCC ('I','4','2','0'):
	  context->pix_fmt = PIX_FMT_YUV420P;
	  break;
	case GST_MAKE_FOURCC ('Y','4','1','B'):
	  context->pix_fmt = PIX_FMT_YUV411P;
	  break;
	case GST_MAKE_FOURCC ('Y','4','2','B'):
	  context->pix_fmt = PIX_FMT_YUV422P;
	  break;
	case GST_MAKE_FOURCC ('Y','U','V','9'):
	  context->pix_fmt = PIX_FMT_YUV410P;
	  break;
#if 0
	case FIXME:
	  context->pix_fmt = PIX_FMT_YUV444P;
	  break;
#endif
      }
    }
  } else if (strcmp (gst_structure_get_name (structure), "video/x-raw-rgb") == 0) {
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
            context->pix_fmt = PIX_FMT_RGBA32;
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
gst_ffmpeg_caps_to_codectype (enum CodecType  type,
                              const GstCaps *caps,
                              AVCodecContext *context)
{
  if (context == NULL)
    return;

  switch (type) {
    case CODEC_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context);
      break;

    case CODEC_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context);
      break;

    default:
      /* unknown */
      break;
  }
}

/* _formatid_to_caps () is meant for muxers/demuxers, it
 * transforms a name (ffmpeg way of ID'ing these, why don't
 * they have unique numerical IDs?) to the corresponding
 * caps belonging to that mux-format
 *
 * Note: we don't need any additional info because the caps
 * isn't supposed to contain any useful info besides the
 * media type anyway
 */

GstCaps *
gst_ffmpeg_formatid_to_caps (const gchar *format_name)
{
  GstCaps *caps = NULL;

  if (!strcmp (format_name, "mpeg")) {
    caps = gst_caps_new_simple ("video/mpeg",
	"systemstream", G_TYPE_BOOLEAN, TRUE,
        NULL);
  } else if (!strcmp (format_name, "mpegts")) {
    caps = gst_caps_new_simple ("video/mpegts",
	"systemstream", G_TYPE_BOOLEAN, TRUE,
        NULL);
  } else if (!strcmp (format_name, "rm")) {
    caps = gst_caps_new_simple ("ffmpeg_rm", "audio/x-pn-realvideo",
	"systemstream", G_TYPE_BOOLEAN, TRUE,
        NULL);
  } else if (!strcmp (format_name, "asf")) {
    caps = gst_caps_new_simple ("video/x-ms-asf",
	NULL);
  } else if (!strcmp (format_name, "avi")) {
    caps = gst_caps_new_simple ("video/x-msvideo",
	NULL);
  } else if (!strcmp (format_name, "wav")) {
    caps = gst_caps_new_simple ("video/x-wav",
	NULL);
  } else if (!strcmp (format_name, "swf")) {
    caps = gst_caps_new_simple ("application/x-shockwave-flash",
	NULL);
  } else if (!strcmp (format_name, "au")) {
    caps = gst_caps_new_simple ("audio/x-au",
	NULL);
  } else if (!strcmp (format_name, "mov")) {
    caps = gst_caps_new_simple ("video/quicktime",
	NULL);
  } else if (!strcmp (format_name, "dv")) {
    caps = gst_caps_new_simple ("video/x-dv",
	"systemstream", G_TYPE_BOOLEAN, TRUE,
	NULL);
  } else if (!strcmp (format_name, "4xm")) {
    caps = gst_caps_new_simple ("video/x-4xm",
	NULL);
  } else {
    /* unknown! */
  }

  return caps;
}

/* Convert a GstCaps to a FFMPEG codec ID. Size et all
 * are omitted, that can be queried by the user itself,
 * we're not eating the GstCaps or anything
 * A pointer to an allocated context is also needed for
 * optional extra info (not used yet, though)
 */

enum CodecID
gst_ffmpeg_caps_to_codecid (const GstCaps *caps,
                            AVCodecContext *context)
{
  enum CodecID id = CODEC_ID_NONE;
  const gchar *mimetype;
  const GstStructure *structure;
  gboolean video = FALSE, audio = FALSE; /* we want to be sure! */

  g_return_val_if_fail (caps != NULL, CODEC_ID_NONE);
  g_return_val_if_fail (gst_caps_get_size (caps) == 1, CODEC_ID_NONE);
  structure = gst_caps_get_structure (caps, 0);
  
  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw-rgb")) {

    id = CODEC_ID_RAWVIDEO;

    if (context != NULL) {
      gint bpp = 0, endianness = 0, rmask = 0;
      enum PixelFormat pix_fmt = -1;

      gst_structure_get_int (structure, "bpp",        &bpp);
      gst_structure_get_int (structure, "endianness", &endianness);
      gst_structure_get_int (structure, "rmask",      &rmask);
  
      switch (bpp) {
        case 15:
          if (endianness == G_BYTE_ORDER) {
            pix_fmt = PIX_FMT_RGB555;
          }
          break;
        case 16:
          if (endianness == G_BYTE_ORDER) {
            pix_fmt = PIX_FMT_RGB565;
          }
          break;
        case 24:
          if (rmask == 0xff0000) {
            pix_fmt = PIX_FMT_RGB24;
          } else {
            pix_fmt = PIX_FMT_BGR24;
          }
          break;
        case 32:
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
          if (rmask == 0x00ff0000) {
#else
          if (rmask == 0x0000ff00) {
#endif
            pix_fmt = PIX_FMT_RGBA32;
          }
          break;
        default:
          /* ... */
          break;
      }

      /* only set if actually recognized! */
      if (pix_fmt != -1) {
        video = TRUE;
        context->pix_fmt = pix_fmt;
      } else {
        id = CODEC_ID_NONE;
      }
    }

  } else if (!strcmp (mimetype, "video/x-raw-yuv")) {

    id = CODEC_ID_RAWVIDEO;

    if (context != NULL) {
      guint32 fmt_fcc = 0;
      enum PixelFormat pix_fmt = -1;

      gst_structure_get_fourcc (structure, "format", &fmt_fcc);

      switch (fmt_fcc) {
        case GST_MAKE_FOURCC ('Y','U','Y','2'):
          pix_fmt = PIX_FMT_YUV422;
          break;
        case GST_MAKE_FOURCC ('I','4','2','0'):
          pix_fmt = PIX_FMT_YUV420P;
          break;
        case GST_MAKE_FOURCC ('Y','4','1','B'):
          pix_fmt = PIX_FMT_YUV411P;
          break;
        case GST_MAKE_FOURCC ('Y','4','2','B'):
          pix_fmt = PIX_FMT_YUV422P;
          break;
        case GST_MAKE_FOURCC ('Y','U','V','9'):
          pix_fmt = PIX_FMT_YUV410P;
          break;
        default:
          /* ... */
          break;
      }

      /* only set if actually recognized! */
      if (pix_fmt != -1) {
        video = TRUE;
        context->pix_fmt = pix_fmt;
      } else {
        id = CODEC_ID_NONE;
      }
    }

  } else if (!strcmp (mimetype, "audio/x-raw-int")) {

    gint depth = 0, width = 0, endianness = 0;
    gboolean signedness = FALSE; /* bla default value */

    gst_structure_get_int (structure, "endianness", &endianness);
    gst_structure_get_boolean (structure, "signed", &signedness);
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "depth", &depth);

    if (context) {
      context->sample_rate = 0;
      context->channels = 0;
      gst_structure_get_int (structure, "channels", &context->channels);
      gst_structure_get_int (structure, "rate", &context->sample_rate);
    }

    if (depth == width) {
      switch (depth) {
	case 8:
	  if (signedness) {
	    id = CODEC_ID_PCM_S8;
	  } else {
	    id = CODEC_ID_PCM_U8;
	  }
	  break;
	case 16:
	  switch (endianness) {
	    case G_BIG_ENDIAN:
	      if (signedness) {
		id = CODEC_ID_PCM_S16BE;
	      } else {
		id = CODEC_ID_PCM_U16BE;
	      }
	      break;
	    case G_LITTLE_ENDIAN:
	      if (signedness) {
		id = CODEC_ID_PCM_S16LE;
	      } else {
		id = CODEC_ID_PCM_U16LE;
	      }
	      break;
	  }
	  break;
      }

      if (id != CODEC_ID_NONE) {
	audio = TRUE;
      }
    }

  } else if (!strcmp(mimetype, "audio/x-mulaw")) {

    id = CODEC_ID_PCM_MULAW;
    audio = TRUE;

  } else if (!strcmp(mimetype, "audio/x-alaw")) {

    id = CODEC_ID_PCM_ALAW;
    audio = TRUE;

  } else if (!strcmp(mimetype, "video/x-dv")) {

    id = CODEC_ID_DVVIDEO;
    video = TRUE;

  } else if (!strcmp(mimetype, "audio/x-dv")) { /* ??? */

    id = CODEC_ID_DVAUDIO;
    audio = TRUE;

  } else if (!strcmp(mimetype, "video/x-h263")) {

    id = CODEC_ID_H263; /* or H263[IP] */
    video = TRUE;

  } else if (!strcmp(mimetype, "video/mpeg")) {

    gboolean sys_strm = TRUE;
    gint mpegversion = 0;
    gst_structure_get_boolean (structure, "systemstream", &sys_strm);
    gst_structure_get_int (structure, "mpegversion", &mpegversion);
    if (!sys_strm) {
      switch (mpegversion) {
        case 1:
          id = CODEC_ID_MPEG1VIDEO;
          break;
        case 4:
          id = CODEC_ID_MPEG4;
          break;
        default:
          /* ... */
          break;
      }
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp(mimetype, "video/x-jpeg")) {

    id = CODEC_ID_MJPEG; /* A... B... */
    video = TRUE;

  } else if (!strcmp(mimetype, "video/x-wmv")) {
    gint wmvversion = 0;

    gst_structure_get_int (structure, "wmvversion", &wmvversion);

    switch (wmvversion) {
      case 1:
	id = CODEC_ID_WMV1;
	break;
      case 2:
	id = CODEC_ID_WMV2;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp(mimetype, "application/ogg")) {

    id = CODEC_ID_VORBIS;

  } else if (!strcmp(mimetype, "audio/mpeg")) {
    gint layer = 0;
    gint mpegversion = 0;

    if (gst_structure_get_int (structure, "mpegversion", &mpegversion)) {
      switch (mpegversion) {
        case 2: /* ffmpeg uses faad for both... */
        case 4:
          id = CODEC_ID_MPEG4AAC;
          break;
        case 1:
          if (gst_structure_get_int (structure, "layer", &layer)) {
            switch (layer) {
              case 1:
              case 2:
                id = CODEC_ID_MP2;
                break;
              case 3:
                id = CODEC_ID_MP3;
                break;
              default:
                /* ... */
                break;
            }
          }
        default:
          /* ... */
          break;
      }
    }

    if (id != CODEC_ID_NONE) {
      audio = TRUE;
    }

  } else if (!strcmp(mimetype, "audio/x-wma")) {
    gint wmaversion = 0;

    gst_structure_get_int (structure, "wmaversion", &wmaversion);

    switch (wmaversion) {
      case 1:
	id = CODEC_ID_WMAV1;
	break;
      case 2:
	id = CODEC_ID_WMAV2;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      audio = TRUE;
    }

  } else if (!strcmp(mimetype, "audio/x-ac3")) {

    id = CODEC_ID_AC3;

  } else if (!strcmp(mimetype, "video/x-msmpeg")) {
    gint msmpegversion = 0;

    gst_structure_get_int (structure, "msmpegversion", &msmpegversion);

    switch (msmpegversion) {
      case 41:
	id = CODEC_ID_MSMPEG4V1;
	break;
      case 42:
	id = CODEC_ID_MSMPEG4V2;
	break;
      case 43:
	id = CODEC_ID_MSMPEG4V3;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp(mimetype, "video/x-svq")) {
    gint svqversion = 0;

    gst_structure_get_int (structure, "svqversion", &svqversion);

    switch (svqversion) {
      case 1:
	id = CODEC_ID_SVQ1;
	break;
      case 3:
	id = CODEC_ID_SVQ3;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-huffyuv")) {

    id = CODEC_ID_HUFFYUV;
    video = TRUE;

  } else if (!strcmp (mimetype, "audio/x-mace")) {
    gint maceversion = 0;

    gst_structure_get_int (structure, "maceversion", &maceversion);
    switch (maceversion) {
      case 3:
	id = CODEC_ID_MACE3;
	break;
      case 6:
	id = CODEC_ID_MACE6;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      audio = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-theora") ||
             !strcmp (mimetype, "video/x-vp3")) {

    id = CODEC_ID_VP3;
    video = TRUE;

  } else if (!strcmp (mimetype, "video/x-indeo")) {
    gint indeoversion = 0;

    gst_structure_get_int (structure, "indeoversion", &indeoversion);
    switch (indeoversion) {
      case 3:
	id = CODEC_ID_INDEO3;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-divx")) {
    gint divxversion = 0;

    gst_structure_get_int (structure, "divxversion", &divxversion);
    switch (divxversion) {
      case 3:
	id = CODEC_ID_MSMPEG4V3;
	break;
      case 4:
      case 5:
	id = CODEC_ID_MPEG4;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-3ivx")) {

    id = CODEC_ID_MPEG4;
    video = TRUE;

    if (context) {
      context->codec_tag = GST_MAKE_FOURCC ('3','I','V','X');
    }

  } else if (!strcmp (mimetype, "video/x-xvid")) {

    id = CODEC_ID_MPEG4;
    video = TRUE;

    if (context) {
      context->codec_tag = GST_MAKE_FOURCC ('X','V','I','D');
    }

  } else if (!strcmp (mimetype, "video/x-ffv")) {
    gint ffvversion = 0;

    gst_structure_get_int (structure, "ffvversion", &ffvversion);
    switch (ffvversion) {
      case 1:
	id = CODEC_ID_FFV1;
	break;
      default:
	/* ... */
	break;
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "x-adpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (layout == NULL) {
      /* break */
    } else if (!strcmp (layout, "quicktime")) {
      id = CODEC_ID_ADPCM_IMA_QT;
    } else if (!strcmp (layout, "microsoft")) {
      id = CODEC_ID_ADPCM_MS;
    } else if (!strcmp (layout, "wav")) {
      id = CODEC_ID_ADPCM_IMA_WAV;
    } else if (!strcmp (layout, "4xm")) {
      id = CODEC_ID_ADPCM_4XM;
    }

    if (id != CODEC_ID_NONE) {
      audio = TRUE;
    }
    
  } else if (!strcmp (mimetype, "video/x-4xm")) {

    id = CODEC_ID_4XM;
    video = TRUE;

  }

  /* TODO: realvideo/audio (well, we can't write them anyway) */

  if (context != NULL) {
    if (video == TRUE) {
      gst_ffmpeg_caps_to_pixfmt (caps, context);
      context->codec_type = CODEC_TYPE_VIDEO;
    } else if (audio == TRUE) {
      gst_ffmpeg_caps_to_smpfmt (caps, context);
      context->codec_type = CODEC_TYPE_AUDIO;
    }

    context->codec_id = id;
  }

  if (id != CODEC_ID_NONE) {
    char *str = gst_caps_to_string (caps);
    GST_DEBUG ("The id=%d belongs to the caps %s", id, str);
    g_free(str);
  }

  return id;
}
