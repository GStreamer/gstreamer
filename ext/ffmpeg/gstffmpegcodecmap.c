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

#include "config.h"
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

#define GST_FF_VID_CAPS_NEW(name, mimetype, props...)		\
	(context != NULL) ?					\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT (context->width),	\
		      "height", GST_PROPS_INT (context->height),\
		      "framerate", GST_PROPS_FLOAT (            \
				1.*context->frame_rate/         \
				context->frame_rate_base) ,     \
		      ##props)					\
	:							\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT_RANGE (16, 4096),	\
		      "height", GST_PROPS_INT_RANGE (16, 4096),	\
		      "framerate", GST_PROPS_FLOAT_RANGE (0,    \
						G_MAXFLOAT) ,   \
		      ##props)

/* same for audio - now with channels/sample rate
 */

#define GST_FF_AUD_CAPS_NEW(name, mimetype, props...)			\
	(context != NULL) ?						\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT (context->sample_rate),	\
		      "channels", GST_PROPS_INT (context->channels) ,	\
		      ##props)						\
	:								\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      ##props)

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
      caps = GST_CAPS_NEW ("ffmpeg_mpeg1video",
                           "video/mpeg",
                             "mpegversion",  GST_PROPS_INT (1),
                             "systemstream", GST_PROPS_BOOLEAN (FALSE)
                          );
      break;

    case CODEC_ID_H263P:
    case CODEC_ID_H263I:
    case CODEC_ID_H263:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_h263",
                                  "video/x-h263"
                                 );
      break;

    case CODEC_ID_RV10:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_rv10",
                                  "video/x-pn-realvideo",
                                    "systemstream", GST_PROPS_BOOLEAN (FALSE)
                                 );
      break;

    case CODEC_ID_MP2:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_mp2",
                                  "audio/mpeg",
                                    "layer", GST_PROPS_INT (2)
                                 );
      break;

    case CODEC_ID_MP3LAME:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_mp3",
                                  "audio/mpeg",
                                    "layer", GST_PROPS_INT (3)
                                 );
      break;

    case CODEC_ID_VORBIS: /* FIXME? vorbis or ogg? */
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_vorbis",
		                  "application/ogg",
			            NULL
			         );
      break;
      
    case CODEC_ID_AC3:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_ac3",
		                  "audio/x-ac3",
			            NULL
			         );
      break;

    case CODEC_ID_MJPEG:
    case CODEC_ID_MJPEGB:
    /*case CODEC_ID_LJPEG:*/
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_mjpeg",
                                  "video/x-jpeg"
                                 );
      break;

    case CODEC_ID_MPEG4:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_mpeg4",
                                  "video/mpeg",
                                    "mpegversion",  GST_PROPS_INT (4),
                                    "systemstream", GST_PROPS_BOOLEAN (FALSE)
                                 );
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_divx",
                                  "video/x-divx",
				    "divxversion",  GST_PROPS_INT (5)
                                 ));
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_divx",
                                  "video/x-divx",
				    "divxversion",  GST_PROPS_INT (4)
                                 ));
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_xvid",
                                  "video/x-xvid"
                                 ));
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_3ivx",
                                  "video/x-3ivx"
                                 ));
      break;

    /* weird quasi-codecs for the demuxers only */
    case CODEC_ID_RAWVIDEO:
      /* we use a shortcut to the raw-video pad function */
      return gst_ffmpeg_codectype_to_caps (CODEC_TYPE_VIDEO, context);

    case CODEC_ID_MSMPEG4V1:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_msmpeg4v1",
                                  "video/x-msmpeg",
                                    "msmpegversion", GST_PROPS_INT (41)
                                 );
      break;

    case CODEC_ID_MSMPEG4V2:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_msmpeg4v2",
                                  "video/x-msmpeg",
                                    "msmpegversion", GST_PROPS_INT (42)
                                 );
      break;

    case CODEC_ID_MSMPEG4V3:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_msmpeg4v3",
                                  "video/x-msmpeg",
                                    "msmpegversion", GST_PROPS_INT (43)
                                 );
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_msmpeg4v3_divx3",
                                  "video/x-divx",
                                    "divxversion", GST_PROPS_INT (3)
                                 ));
      break;

    case CODEC_ID_WMV1:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_wmv1",
                                  "video/x-wmv",
                                    "wmvversion", GST_PROPS_INT (1)
                                 );
      break;

    case CODEC_ID_WMV2:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_wmv2",
                                  "video/x-wmv",
                                    "wmvversion", GST_PROPS_INT (2)
                                 );
      break;

    case CODEC_ID_SVQ1:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_svq1",
                                  "video/x-svq",
                                    "svqversion", GST_PROPS_INT (1)
                                 );
      break;

    case CODEC_ID_SVQ3:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_svq3",
                                  "video/x-svq",
                                    "svqversion", GST_PROPS_INT (3)
                                 );
      break;

    case CODEC_ID_DVAUDIO:
        caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_dvaudio",
                                    "audio/x-dv",
				    NULL
                                   );
        break;

    case CODEC_ID_DVVIDEO:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_dvvideo",
                                  "video/dv"
                                 );
      break;

    case CODEC_ID_WMAV1:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_wma1",
                                  "audio/x-wma",
                                    "wmaversion",  GST_PROPS_INT (1),
                                    "flags1",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                    "flags2",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                    "block_align", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                    "bitrate",     GST_PROPS_INT_RANGE (0, G_MAXINT)
                                 );
      break;

    case CODEC_ID_WMAV2:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_wma2",
                                  "audio/x-wma",
                                    "wmaversion",  GST_PROPS_INT (2),
                                    "flags1",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                    "flags2",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                    "block_align", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                    "bitrate",     GST_PROPS_INT_RANGE (0, G_MAXINT)
                                 );
      break;

    case CODEC_ID_MACE3:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_mace3",
                                  "audio/x-mace",
                                    "maceversion", GST_PROPS_INT (3)
                                 );
      break;

    case CODEC_ID_MACE6:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_mace6",
                                  "audio/x-mace",
                                    "maceversion", GST_PROPS_INT (6)
                                 );
      break;

    case CODEC_ID_HUFFYUV:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_huffyuv",
                                  "video/x-huffyuv"
                                 );
      break;

    case CODEC_ID_CYUV:
      /* .. */
      break;

    case CODEC_ID_H264:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_h264",
                                  "video/x-h264"
                                 );
      break;

    case CODEC_ID_INDEO3:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_indeo3",
                                  "video/x-indeo",
                                    "indeoversion", GST_PROPS_INT (3)
                                 );
      break;

    case CODEC_ID_VP3:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_vp3",
                                  "video/x-vp3"
                                 );
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_theora",
                                  "video/x-theora"
                                 ));
      break;

    case CODEC_ID_AAC:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_mpeg2aac",
				  "audio/mpeg",
				    "systemstream", GST_PROPS_BOOLEAN (FALSE),
				    "mpegversion",  GST_PROPS_INT (2)
				 );
      break;

    case CODEC_ID_MPEG4AAC:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_mpeg4aac",
                                  "audio/mpeg",
                                    "systemstream", GST_PROPS_BOOLEAN (FALSE),
                                    "mpegversion",  GST_PROPS_INT (4)
                                 );
      break;

    case CODEC_ID_ASV1:
      /* .. */
      break;

    case CODEC_ID_FFV1:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_ffv1",
				  "video/x-ffv",
                                    "ffvversion", GST_PROPS_INT (1)
				 );
      break;

    case CODEC_ID_4XM:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_4xmvideo",
                                  "video/x-4xm"
                                 );
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

        caps = GST_FF_AUD_CAPS_NEW (
                 "ffmpeg_pcmaudio",
		 "audio/x-raw-int",
		   "width",      GST_PROPS_INT (width),
		   "depth",      GST_PROPS_INT (depth),
		   "endianness", GST_PROPS_INT (endianness),
		   "signed",     GST_PROPS_BOOLEAN (signedness)
	       );
      } while (0);
      break;

    case CODEC_ID_PCM_MULAW:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_mulawaudio",
                                  "audio/x-mulaw",
				  NULL);
      break;

    case CODEC_ID_PCM_ALAW:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_alawaudio",
                                  "audio/x-alaw",
				  NULL);
      break;

    case CODEC_ID_ADPCM_IMA_QT:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_adpcm_ima_qt",
                                  "audio/x-adpcm",
                                    "layout", GST_PROPS_STRING ("quicktime")
                                 );
      break;

    case CODEC_ID_ADPCM_IMA_WAV:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_adpcm_ima_wav",
                                  "audio/x-adpcm",
                                    "layout", GST_PROPS_STRING ("wav")
                                 );
      break;

    case CODEC_ID_ADPCM_MS:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_adpcm_ms",
                                  "audio/x-adpcm",
                                    "layout", GST_PROPS_STRING ("microsoft")
                                 );
      break;

    case CODEC_ID_ADPCM_4XM:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_adpcm_4xm",
                                  "audio/x-adpcm",
                                    "layout", GST_PROPS_STRING ("4xm")
                                 );
      break;

    case CODEC_ID_AMR_NB:
      /* .. */
      break;

    case CODEC_ID_RA_144:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_realaudio_144",
                                  "audio/x-pn-realaudio",
                                    "bitrate", GST_PROPS_INT (14400)
                                 );
      break;

    case CODEC_ID_RA_288:
      caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_realaudio_288",
                                  "audio/x-pn-realaudio",
                                    "bitrate", GST_PROPS_INT (28800)
                                 );
      break;

    default:
      /* .. */
      break;
  }

  if (caps != NULL) {
    char *str = g_strdup_printf("The caps that belongs to codec_id=%d",
				codec_id);
    gst_caps_debug(caps, str);
    g_free(str);
  } else {
    char *str = g_strdup_printf("No caps found for codec_id=%d",
                                codec_id);
    gst_caps_debug(caps, str);
    g_free(str);
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
      bpp = depth = 32;
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
    caps = GST_FF_VID_CAPS_NEW ("ffmpeg_rawvideo",
                                "video/x-raw-rgb",
                                  "bpp",        GST_PROPS_INT (bpp),
                                  "depth",      GST_PROPS_INT (depth),
                                  "red_mask",   GST_PROPS_INT (r_mask),
                                  "green_mask", GST_PROPS_INT (g_mask),
                                  "blue_mask",  GST_PROPS_INT (b_mask),
                                  "endianness", GST_PROPS_INT (endianness)
                                );
  } else if (fmt) {
    caps = GST_FF_VID_CAPS_NEW ("ffmpeg_rawvideo",
                                "video/x-raw-yuv",
                                  "format",     GST_PROPS_FOURCC (fmt)
                               );
  }

  if (caps != NULL) {
    char *str = g_strdup_printf("The caps that belongs to pix_fmt=%d",
				pix_fmt);
    gst_caps_debug(caps, str);
    g_free(str);
  } else {
    char *str = g_strdup_printf("No caps found for pix_fmt=%d",
				pix_fmt);
    gst_caps_debug(caps, str);
    g_free(str);
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
    caps = GST_FF_AUD_CAPS_NEW ("ffmpeg_rawaudio",
                                "audio/x-raw-int",
                                  "signed",     GST_PROPS_BOOLEAN (signedness),
                                  "endianness", GST_PROPS_INT (G_BYTE_ORDER),
                                  "width",      GST_PROPS_INT (bpp),
                                  "depth",      GST_PROPS_INT (bpp)
                                );
  }

  if (caps != NULL) {
    char *str = g_strdup_printf("The caps that belongs to sample_fmt=%d",
				sample_fmt);
    gst_caps_debug(caps, str);
    g_free(str);
  } else {
    char *str = g_strdup_printf("No caps found for sample_fmt=%d",
				sample_fmt);
    gst_caps_debug(caps, str);
    g_free(str);
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
  GstCaps *caps = NULL;

  switch (codec_type) {
    case CODEC_TYPE_VIDEO:
      if (context) {
        caps = gst_ffmpeg_pixfmt_to_caps (context->pix_fmt, context);
      } else {
        GstCaps *temp;
        enum PixelFormat i;

        for (i = 0; i < PIX_FMT_NB; i++) {
          temp = gst_ffmpeg_pixfmt_to_caps (i, NULL);
          if (temp != NULL) {
            caps = gst_caps_append (caps, temp);
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

        for (i = 0; i <= SAMPLE_FMT_S16; i++) {
          temp = gst_ffmpeg_smpfmt_to_caps (i, NULL);
          if (temp != NULL) {
            caps = gst_caps_append (caps, temp);
          }
        }
      }
      break;

    default:
      /* .. */
      break;
  }

  return caps;
}


/* Construct the context extradata from caps
 * when needed.
 */
static void
gst_ffmpeg_caps_to_extradata (GstCaps        *caps,
                              AVCodecContext *context)
{
  const gchar *mimetype;

  mimetype = gst_caps_get_mime(caps);

  if (!strcmp(mimetype, "audio/x-wma")) {

    if (!gst_caps_has_property (caps, "flags1")) {
      g_warning ("Caps without flags1 property for %s", mimetype);
      return;
    }

    if (!gst_caps_has_property (caps, "flags2")) {
      g_warning ("Caps without flags2 property for %s", mimetype);
      return;
    }

    if (gst_caps_has_property (caps, "wmaversion")) {
      gint wmaversion = 0;
      gint value;

      /* 
       * Rebuild context data from flags1 & flags2 
       * see wmadec in ffmpeg/libavcodec/wmadec.c 
       */
      gst_caps_get_int (caps, "wmaversion", &wmaversion);
      switch (wmaversion) {
        case 1:
          context->extradata = (guint8 *) g_malloc0 (4); 
          gst_caps_get_int (caps, "flags1", &value);
          ((guint8 *)context->extradata)[0] = value;
          gst_caps_get_int (caps, "flags2", &value);
          ((guint8 *)context->extradata)[2] = value;
          context->extradata_size = 4;
          break;
        case 2:
          context->extradata = (guint8 *) g_malloc0 (6);        
          gst_caps_get_int (caps, "flags1", &value);
          ((guint8 *) context->extradata)[0] = value;
          ((guint8 *) context->extradata)[1] = value >> 8;
          ((guint8 *) context->extradata)[2] = value >> 16;
          ((guint8 *) context->extradata)[3] = value >> 24;
          gst_caps_get_int (caps, "flags2", &value);
          ((guint8 *) context->extradata)[4] = value; 
          ((guint8 *) context->extradata)[5] = value >> 8;
          context->extradata_size = 6;
          break;
        default:
          g_warning ("Unknown wma version %d\n", wmaversion);
          break;
      }
    }
  }
}


/* Convert a GstCaps (audio/raw) to a FFMPEG SampleFmt
 * and other audio properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_smpfmt (GstCaps        *caps,
                           AVCodecContext *context)
{
  if (gst_caps_has_property_typed (caps, "width",
				   GST_PROPS_INT_TYPE) &&
      gst_caps_has_property_typed (caps, "depth",
				   GST_PROPS_INT_TYPE) &&
      gst_caps_has_property_typed (caps, "signed",
				   GST_PROPS_BOOLEAN_TYPE) &&
      gst_caps_has_property_typed (caps, "endianness",
				   GST_PROPS_INT_TYPE)) {
    gint depth = 0, width = 0, endianness = 0;
    gboolean signedness = FALSE;
    gst_caps_get (caps,
                  "width",      &width,
                  "depth",      &depth,
                  "endianness", &endianness,
                  "signed",     &signedness,
                  NULL);
    if (width == 16 && depth == 16 &&
        endianness == G_BYTE_ORDER && signedness == TRUE) {
      context->sample_fmt = SAMPLE_FMT_S16;
    }
  }

  if (gst_caps_has_property_typed (caps, "channels",
				   GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "channels", &context->channels);
  }

  if (gst_caps_has_property_typed (caps, "rate",
				   GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "rate", &context->sample_rate);
  }

  if (gst_caps_has_property_typed (caps, "block_align",
				   GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "block_align", &context->block_align);
  }

  if (gst_caps_has_property_typed (caps, "bitrate",
				   GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "bitrate", &context->bit_rate);
  }

  gst_ffmpeg_caps_to_extradata (caps, context);
}


/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 * and other video properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_pixfmt (GstCaps        *caps,
                           AVCodecContext *context)
{
  if (gst_caps_has_property_typed (caps, "width",
				   GST_PROPS_INT_TYPE) &&
      gst_caps_has_property_typed (caps, "height",
				   GST_PROPS_INT_TYPE)) {
    gst_caps_get (caps,
                  "width",  &context->width,
                  "height", &context->height,
                  NULL);
  }

  if (gst_caps_has_property_typed (caps, "framerate",
			           GST_PROPS_FLOAT_TYPE)) {
    gfloat fps;
    gst_caps_get_float (caps, "framerate", &fps);
    context->frame_rate = fps * DEFAULT_FRAME_RATE_BASE;
    context->frame_rate_base = DEFAULT_FRAME_RATE_BASE;
  }

  if (strcmp (gst_caps_get_mime (caps), "video/x-raw-yuv") == 0) {
    if (gst_caps_has_property_typed (caps, "format",
				   GST_PROPS_FOURCC_TYPE)) {
      guint32 fourcc;
      gst_caps_get_fourcc_int (caps, "format", &fourcc);

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
  } else if (strcmp (gst_caps_get_mime (caps), "video/x-raw-rgb") == 0) {
    if (gst_caps_has_property_typed (caps, "bpp", GST_PROPS_INT_TYPE) &&
	gst_caps_has_property_typed (caps, "red_mask", GST_PROPS_INT_TYPE)) {
      gint bpp = 0, red_mask = 0;
      gst_caps_get_int (caps, "bpp", &bpp);
      gst_caps_get_int (caps, "red_mask", &red_mask);

      switch (bpp) {
        case 32:
          context->pix_fmt = PIX_FMT_RGBA32;
          break;
        case 24:
          switch (red_mask) {
            case 0x0000FF:
              context->pix_fmt = PIX_FMT_BGR24;
              break;
            case 0xFF0000:
              context->pix_fmt = PIX_FMT_RGB24;
	      break;
            default:
              /* nothing */
              break;
          }
	  break;
        case 16:
          context->pix_fmt = PIX_FMT_RGB565;
	  break;
        case 15:
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
                              GstCaps        *caps,
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
    caps = GST_CAPS_NEW ("ffmpeg_mpeg",
			 "video/mpeg",
                           "systemstream", GST_PROPS_BOOLEAN (TRUE)
                        );
  } else if (!strcmp (format_name, "mpegts")) {
    caps = GST_CAPS_NEW ("ffmpeg_mpegts",
			 "video/mpegts",
                           "systemstream", GST_PROPS_BOOLEAN (TRUE)
                        );
  } else if (!strcmp (format_name, "rm")) {
    caps = GST_CAPS_NEW ("ffmpeg_rm",
			 "audio/x-pn-realvideo",
                           "systemstream", GST_PROPS_BOOLEAN (TRUE)
                        );
  } else if (!strcmp (format_name, "asf")) {
    caps = GST_CAPS_NEW ("ffmpeg_asf",
			 "video/x-asf",
                           NULL
                        );
  } else if (!strcmp (format_name, "avi")) {
    caps = GST_CAPS_NEW ("ffmpeg_avi",
			 "video/avi",
                           NULL
                        );
  } else if (!strcmp (format_name, "wav")) {
    caps = GST_CAPS_NEW ("ffmpeg_wav",
			 "video/x-wav",
                           NULL
                        );
  } else if (!strcmp (format_name, "swf")) {
    caps = GST_CAPS_NEW ("ffmpeg_swf",
			 "application/x-shockwave-flash",
                           NULL
                        );
  } else if (!strcmp (format_name, "au")) {
    caps = GST_CAPS_NEW ("ffmpeg_au",
			 "audio/x-au",
                           NULL
                        );
  } else if (!strcmp (format_name, "mov")) {
    caps = GST_CAPS_NEW ("ffmpeg_quicktime",
			 "video/quicktime",
                           NULL
                        );
  } else if (!strcmp (format_name, "dv")) {
    caps = GST_CAPS_NEW ("ffmpeg_dv",
			 "video/x-dv",
                           "systemstream", GST_PROPS_BOOLEAN (TRUE)
                        );
  } else if (!strcmp (format_name, "4xm")) {
    caps = GST_CAPS_NEW ("ffmpeg_4xm",
			 "video/x-4xm",
                           NULL
                        );
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
gst_ffmpeg_caps_to_codecid (GstCaps        *caps,
                            AVCodecContext *context)
{
  enum CodecID id = CODEC_ID_NONE;
  const gchar *mimetype;
  gboolean video = FALSE, audio = FALSE; /* we want to be sure! */

  g_return_val_if_fail (caps != NULL, CODEC_ID_NONE);

  mimetype = gst_caps_get_mime(caps);

  if (!strcmp (mimetype, "video/x-raw-rgb") ||
      !strcmp (mimetype, "video/x-raw-yuv")) {

    id = CODEC_ID_RAWVIDEO;

    if (context != NULL) {
      gint depth = 0, endianness = 0;
      guint32 fmt_fcc = 0;
      enum PixelFormat pix_fmt = -1;

      if (gst_caps_has_property (caps, "format"))
        gst_caps_get_fourcc_int (caps, "format", &fmt_fcc);
      else
        fmt_fcc = GST_MAKE_FOURCC ('R','G','B',' ');

      switch (fmt_fcc) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
          gst_caps_get_int (caps, "endianness", &endianness);
          gst_caps_get_int (caps, "depth", &depth);
          switch (depth) {
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
              if (endianness == G_BIG_ENDIAN) {
                pix_fmt = PIX_FMT_RGB24;
              } else {
                pix_fmt = PIX_FMT_BGR24;
              }
              break;
            case 32:
              if (endianness == G_BIG_ENDIAN) {
                pix_fmt = PIX_FMT_RGBA32;
              }
              break;
            default:
              /* ... */
              break;
          }
          break;
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

    if (gst_caps_has_property(caps, "signed")) {
      gst_caps_get_int(caps, "endianness", &endianness);
      gst_caps_get_boolean(caps, "signed", &signedness);
      gst_caps_get_int(caps, "width", &width);
      gst_caps_get_int(caps, "depth", &depth);

      if (context) {
        context->sample_rate = 0;
        context->channels = 0;
        gst_caps_get_int(caps, "channels", &context->channels);
        gst_caps_get_int(caps, "rate", &context->sample_rate);
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
    if (gst_caps_has_property(caps, "systemstream")) {
      gst_caps_get_boolean(caps, "systemstream", &sys_strm);
    }
    if (!sys_strm && gst_caps_has_property(caps, "mpegversion")) {
      gst_caps_get_int(caps, "mpegversion", &mpegversion);
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

    if (gst_caps_has_property (caps, "wmvversion")) {
      gint wmvversion = 0;

      gst_caps_get_int (caps, "wmvversion", &wmvversion);
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
    }

  } else if (!strcmp(mimetype, "application/ogg")) {

    id = CODEC_ID_VORBIS;

  } else if (!strcmp(mimetype, "audio/mpeg")) {

    if (gst_caps_has_property (caps, "layer")) {
      gint layer = 0;

      gst_caps_get_int (caps, "layer", &layer);
      switch (layer) {
        case 1:
        case 2:
          id = CODEC_ID_MP2;
          break;
        case 3:
          id = CODEC_ID_MP3LAME;
          break;
        default:
          /* ... */
          break;
      }
    } else if (gst_caps_has_property (caps, "mpegversion")) {
      gint mpegversion = 0;

      gst_caps_get_int (caps, "mpegversion", &mpegversion);
      if (mpegversion == 4) {
        id = CODEC_ID_MPEG4AAC;
      }
    }

    if (id != CODEC_ID_NONE) {
      audio = TRUE;
    }

  } else if (!strcmp(mimetype, "audio/x-wma")) {

    if (gst_caps_has_property (caps, "wmaversion")) {
      gint wmaversion = 0;

      gst_caps_get_int (caps, "wmaversion", &wmaversion);
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
    }

    if (id != CODEC_ID_NONE) {
      audio = TRUE;
    }

  } else if (!strcmp(mimetype, "audio/x-ac3")) {

    id = CODEC_ID_AC3;

  } else if (!strcmp(mimetype, "video/x-msmpeg")) {

    if (gst_caps_has_property (caps, "msmpegversion")) {
      gint msmpegversion = 0;

      gst_caps_get_int (caps, "msmpegversion", &msmpegversion);
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
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp(mimetype, "video/x-svq")) {

    if (gst_caps_has_property (caps, "svqversion")) {
      gint svqversion = 0;

      gst_caps_get_int (caps, "svqversion", &svqversion);
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
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-huffyuv")) {

    id = CODEC_ID_HUFFYUV;
    video = TRUE;

  } else if (!strcmp (mimetype, "audio/x-mace")) {

    if (gst_caps_has_property (caps, "maceversion")) {
      gint maceversion;

      gst_caps_get_int (caps, "maceversion", &maceversion);
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
    }

    if (id != CODEC_ID_NONE) {
      audio = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-theora") ||
             !strcmp (mimetype, "video/x-vp3")) {

    id = CODEC_ID_VP3;
    video = TRUE;

  } else if (!strcmp (mimetype, "video/x-indeo")) {

    if (gst_caps_has_property (caps, "indeoversion")) {
      gint indeoversion = 0;

      gst_caps_get_int (caps, "indeoversion", &indeoversion);
      switch (indeoversion) {
        case 3:
          id = CODEC_ID_INDEO3;
          break;
        default:
          /* ... */
          break;
      }
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-divx")) {

    if (gst_caps_has_property (caps, "divxversion")) {
      gint divxversion = 0;

      gst_caps_get_int (caps, "divxversion", &divxversion);
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
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "video/x-3ivx") ||
             !strcmp (mimetype, "video/x-divx")) {

    id = CODEC_ID_MPEG4;
    video = TRUE;

  } else if (!strcmp (mimetype, "video/x-ffv")) {

    if (gst_caps_has_property (caps, "ffvversion")) {
      gint ffvversion = 0;

      gst_caps_get_int (caps, "ffvversion", &ffvversion);
      switch (ffvversion) {
        case 1:
          id = CODEC_ID_FFV1;
          break;
        default:
          /* ... */
          break;
      }
    }

    if (id != CODEC_ID_NONE) {
      video = TRUE;
    }

  } else if (!strcmp (mimetype, "x-adpcm")) {

    if (gst_caps_has_property (caps, "layout")) {
      const gchar *layout = "";

      gst_caps_get_string (caps, "layout", &layout);
      if (!strcmp (layout, "quicktime")) {
        id = CODEC_ID_ADPCM_IMA_QT;
      } else if (!strcmp (layout, "microsoft")) {
        id = CODEC_ID_ADPCM_MS;
      } else if (!strcmp (layout, "wav")) {
        id = CODEC_ID_ADPCM_IMA_WAV;
      } else if (!strcmp (layout, "4xm")) {
        id = CODEC_ID_ADPCM_4XM;
      }
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
    char *str = g_strdup_printf("The id=%d belongs to this caps", id);
    gst_caps_debug(caps, str);
    g_free(str);
  }

  return id;
}
