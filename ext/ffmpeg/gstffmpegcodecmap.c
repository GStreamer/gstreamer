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
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif
#include <string.h>

#include "gstffmpegcodecmap.h"

/*
 * Read a palette from a caps.
 */

static void
gst_ffmpeg_get_palette (const GstCaps *caps, AVCodecContext *context)
{
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *palette_v;
  const GstBuffer *palette;

  /* do we have a palette? */
  if ((palette_v = gst_structure_get_value (str,
          "palette_data")) && context) {
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
gst_ffmpeg_set_palette (GstCaps *caps, AVCodecContext *context)
{
  if (context->palctrl) {
    GstBuffer *palette = gst_buffer_new_and_alloc (256 * 4);

    memcpy (GST_BUFFER_DATA (palette), context->palctrl->palette,
        AVPALETTE_SIZE);
    gst_caps_set_simple (caps,
        "palette_data", GST_TYPE_BUFFER, palette, NULL);
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

/* Convert a FFMPEG codec ID and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * CodecID is primarily meant for compressed data GstCaps!
 *
 * encode is a special parameter. gstffmpegdec will say
 * FALSE, gstffmpegenc will say TRUE. The output caps
 * depends on this, in such a way that it will be very
 * specific, defined, fixed and correct caps for encoders,
 * yet very wide, "forgiving" caps for decoders. Example
 * for mp3: decode: audio/mpeg,mpegversion=1,layer=[1-3]
 * but encode: audio/mpeg,mpegversion=1,layer=3,bitrate=x,
 * rate=x,channels=x.
 */

GstCaps *
gst_ffmpeg_codecid_to_caps (enum CodecID codec_id,
    AVCodecContext * context, gboolean encode)
{
  GstCaps *caps = NULL;
  gboolean buildcaps = FALSE;

  switch (codec_id) {
    case CODEC_ID_MPEG1VIDEO:
      /* For decoding, CODEC_ID_MPEG2VIDEO is preferred... So omit here */
      if (encode) {
        /* FIXME: bitrate */
        caps = GST_FF_VID_CAPS_NEW ("video/mpeg",
            "mpegversion", G_TYPE_INT, 1,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
      break;

    case CODEC_ID_MPEG2VIDEO:
      if (encode) {
        /* FIXME: bitrate */
        caps = GST_FF_VID_CAPS_NEW ("video/mpeg",
            "mpegversion", G_TYPE_INT, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      } else {
        /* decode both MPEG-1 and MPEG-2; width/height/fps are all in
         * the MPEG video stream headers, so may be omitted from caps. */
        caps = gst_caps_new_simple ("video/mpeg",
            "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
      break;

    case CODEC_ID_MPEG2VIDEO_XVMC:
      /* this is a special ID - don't need it in GStreamer, I think */
      break;

      /* I don't know the exact differences between those... Anyone? */
    case CODEC_ID_H263:
    case CODEC_ID_H263P:
    case CODEC_ID_H263I:
      caps = GST_FF_VID_CAPS_NEW ("video/x-h263", NULL);
      break;

    case CODEC_ID_RV10:
    case CODEC_ID_RV20:
      do {
        gint version = (codec_id == CODEC_ID_RV10) ? 1 : 2;

        /* FIXME: context->sub_id must be filled in during decoding */
        caps = GST_FF_VID_CAPS_NEW ("video/x-pn-realvideo",
            "systemstream", G_TYPE_BOOLEAN, FALSE,
            "rmversion", G_TYPE_INT, version, NULL);
      } while (0);
      break;

    case CODEC_ID_MP2:
      /* we use CODEC_ID_MP3 for decoding */
      if (encode) {
        /* FIXME: bitrate */
        caps = GST_FF_AUD_CAPS_NEW ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 2, NULL);
      }
      break;

    case CODEC_ID_MP3:
      if (encode) {
        /* FIXME: bitrate */
        caps = GST_FF_AUD_CAPS_NEW ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
      } else {
        /* Decodes MPEG-1 layer 1/2/3. Samplerate, channels et al are
         * in the MPEG audio header, so may be omitted from caps. */
        caps = gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1,
            "layer", GST_TYPE_INT_RANGE, 1, 3, NULL);
      }
      break;

    case CODEC_ID_VORBIS:
      /* This one is disabled for several reasons:
       * - GStreamer already has perfect Ogg and Vorbis support
       * - The ffmpeg implementation depends on libvorbis/libogg,
       *   which are not included in the ffmpeg that GStreamer ships.
       * - The ffmpeg implementation depends on shared objects between
       *   the ogg demuxer and vorbis decoder, which GStreamer doesn't.
       */
      break;

    case CODEC_ID_AC3:
      /* Decoding is disabled, because:
       * - it depends on liba52, which we don't ship in ffmpeg.
       * - we already have a liba52 plugin ourselves.
       */
      if (encode) {
        /* FIXME: bitrate */
        caps = GST_FF_AUD_CAPS_NEW ("audio/x-ac3", NULL);
      }
      break;

      /* MJPEG is normal JPEG, Motion-JPEG and Quicktime MJPEG-A. MJPEGB
       * is Quicktime's MJPEG-B. LJPEG is lossless JPEG. I don't know what
       * sp5x is, but it's apparently something JPEG... We don't separate
       * between those in GStreamer. Should we (at least between MJPEG,
       * MJPEG-B and sp5x decoding...)? */
    case CODEC_ID_MJPEG:
    case CODEC_ID_MJPEGB:
    case CODEC_ID_LJPEG:
    case CODEC_ID_SP5X:
      caps = GST_FF_VID_CAPS_NEW ("video/x-jpeg", NULL);
      break;

    case CODEC_ID_MPEG4:
      if (encode) {
        /* I'm not exactly sure what ffmpeg outputs... ffmpeg itself uses
         * the AVI fourcc 'DIVX', but 'mp4v' for Quicktime... */
        guint32 fourcc = 0;

        if (context)
          fourcc = context->codec_tag;

        switch (fourcc) {
          case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
            caps = GST_FF_VID_CAPS_NEW ("video/x-divx",
	        "divxversion", G_TYPE_INT, 5, NULL);
            break;
          case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
          default:
            /* FIXME: bitrate */
            caps = GST_FF_VID_CAPS_NEW ("video/mpeg",
                "systemstream", G_TYPE_BOOLEAN, FALSE,
                "mpegversion", G_TYPE_INT, 4, NULL);
            break;
        }
      } else {
        /* The trick here is to separate xvid, divx, mpeg4, 3ivx et al */
        caps = GST_FF_VID_CAPS_NEW ("video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
        gst_caps_append (caps, GST_FF_VID_CAPS_NEW ("video/x-divx",
            "divxversion", GST_TYPE_INT_RANGE, 4, 5, NULL));
        gst_caps_append (caps, GST_FF_VID_CAPS_NEW ("video/x-xvid", NULL));
        gst_caps_append (caps, GST_FF_VID_CAPS_NEW ("video/x-3ivx", NULL));
      }
      break;

    case CODEC_ID_RAWVIDEO:
      caps = gst_ffmpeg_codectype_to_caps (CODEC_TYPE_VIDEO, context);
      break;

    case CODEC_ID_MSMPEG4V1:
    case CODEC_ID_MSMPEG4V2:
    case CODEC_ID_MSMPEG4V3:
      do {
        gint version = 41 + codec_id - CODEC_ID_MSMPEG4V1;

        /* encode-FIXME: bitrate */
        caps = GST_FF_VID_CAPS_NEW ("video/x-msmpeg",
            "msmpegversion", G_TYPE_INT, version, NULL);
        if (!encode && codec_id == CODEC_ID_MSMPEG4V3) {
          gst_caps_append (caps, GST_FF_VID_CAPS_NEW ("video/x-divx",
              "divxversion", G_TYPE_INT, 3, NULL));
        }
      } while (0);
      break;

    case CODEC_ID_WMV1:
    case CODEC_ID_WMV2:
      do {
        gint version = (codec_id == CODEC_ID_WMV1) ? 1 : 2;

        caps = GST_FF_VID_CAPS_NEW ("video/x-wmv",
            "wmvversion", G_TYPE_INT, version, NULL);
      } while (0);
      break;

    case CODEC_ID_FLV1:
      buildcaps = TRUE;
      break;

    case CODEC_ID_SVQ1:
      caps = GST_FF_VID_CAPS_NEW ("video/x-svq",
          "svqversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_SVQ3:
      caps = GST_FF_VID_CAPS_NEW ("video/x-svq",
          "svqversion", G_TYPE_INT, 3,
          "halfpel_flag", GST_TYPE_INT_RANGE, 0, 1,
          "thirdpel_flag", GST_TYPE_INT_RANGE, 0, 1,
          "low_delay", GST_TYPE_INT_RANGE, 0, 1,
          "unknown_svq3_flag", GST_TYPE_INT_RANGE, 0, 1, NULL);
      break;

    case CODEC_ID_DVAUDIO:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-dv", NULL);
      break;

    case CODEC_ID_DVVIDEO:
      caps = GST_FF_VID_CAPS_NEW ("video/x-dv", NULL);
      break;

    case CODEC_ID_WMAV1:
    case CODEC_ID_WMAV2:
      do {
        gint version = (codec_id == CODEC_ID_WMAV1) ? 1 : 2;

        if (context) {
          caps = GST_FF_AUD_CAPS_NEW ("audio/x-wma",
             "wmaversion", G_TYPE_INT, version,
             "block_align", G_TYPE_INT, context->block_align,
             "bitrate", G_TYPE_INT, context->bit_rate, NULL);
        } else {
          caps = GST_FF_AUD_CAPS_NEW ("audio/x-wma",
             "wmaversion", G_TYPE_INT, version,
             "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
             "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);
        }
      } while (0);
      break;

    case CODEC_ID_MACE3:
    case CODEC_ID_MACE6:
      do {
        gint version = (codec_id == CODEC_ID_MACE3) ? 3 : 6;

        caps = GST_FF_AUD_CAPS_NEW ("audio/x-mace",
            "maceversion", G_TYPE_INT, version, NULL);
      } while (0);
      break;

    case CODEC_ID_HUFFYUV:
      caps = GST_FF_VID_CAPS_NEW ("video/x-huffyuv", NULL);
      break;

    case CODEC_ID_CYUV:
      buildcaps = TRUE;
      break;

    case CODEC_ID_H264:
      caps = GST_FF_VID_CAPS_NEW ("video/x-h264", NULL);
      break;

    case CODEC_ID_INDEO3:
      caps = GST_FF_VID_CAPS_NEW ("video/x-indeo",
          "indeoversion", G_TYPE_INT, 3, NULL);
      break;

    case CODEC_ID_VP3:
      caps = GST_FF_VID_CAPS_NEW ("video/x-vp3", NULL);
      break;

    case CODEC_ID_THEORA:
      caps = GST_FF_VID_CAPS_NEW ("video/x-theora", NULL);
      break;

    case CODEC_ID_AAC:
    case CODEC_ID_MPEG4AAC:
      /* ffmpeg uses libfaac/libfaad for those. We do not ship these as
       * part of ffmpeg, so defining those is useless. Besides, we have
       * our own faad/faac plugins. */
      break;

    case CODEC_ID_ASV1:
    case CODEC_ID_ASV2:
      buildcaps = TRUE;
      break;

    case CODEC_ID_FFV1:
      caps = GST_FF_VID_CAPS_NEW ("video/x-ffv",
          "ffvversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_4XM:
      caps = GST_FF_VID_CAPS_NEW ("video/x-4xm", NULL);
      break;

    case CODEC_ID_VCR1:
    case CODEC_ID_CLJR:
    case CODEC_ID_MDEC:
    case CODEC_ID_ROQ:
    case CODEC_ID_INTERPLAY_VIDEO:
    case CODEC_ID_XAN_WC3:
    case CODEC_ID_XAN_WC4:
      buildcaps = TRUE;
      break;

    case CODEC_ID_RPZA:
      caps = GST_FF_VID_CAPS_NEW ("video/x-apple-video", NULL);
      break;

    case CODEC_ID_CINEPAK:
      caps = GST_FF_VID_CAPS_NEW ("video/x-cinepak", NULL);
      break;

    /* WS_VQA belogns here (order) */

    case CODEC_ID_MSRLE:
      caps = GST_FF_VID_CAPS_NEW ("video/x-rle",
	  "layout", G_TYPE_STRING, "microsoft", NULL);
      if (context) {
        gst_caps_set_simple (caps,
	    "depth", G_TYPE_INT, (gint) context->bits_per_sample, NULL);
        gst_ffmpeg_set_palette (caps, context);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      break;

    case CODEC_ID_MSVIDEO1:
      caps = GST_FF_VID_CAPS_NEW ("video/x-msvideocodec",
	  "msvideoversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_WS_VQA:
    case CODEC_ID_IDCIN:
    case CODEC_ID_8BPS:
    case CODEC_ID_SMC:
    case CODEC_ID_FLIC:
    case CODEC_ID_TRUEMOTION1:
    case CODEC_ID_VMDVIDEO:
    case CODEC_ID_VMDAUDIO:
    case CODEC_ID_MSZH:
    case CODEC_ID_ZLIB:
    case CODEC_ID_QTRLE:
      buildcaps = TRUE;
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
        gboolean signedness = FALSE;    /* blabla */

        switch (codec_id) {
          case CODEC_ID_PCM_S16LE:
            width = 16;
            depth = 16;
            endianness = G_LITTLE_ENDIAN;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_S16BE:
            width = 16;
            depth = 16;
            endianness = G_BIG_ENDIAN;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_U16LE:
            width = 16;
            depth = 16;
            endianness = G_LITTLE_ENDIAN;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_U16BE:
            width = 16;
            depth = 16;
            endianness = G_BIG_ENDIAN;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_S8:
            width = 8;
            depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_U8:
            width = 8;
            depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = FALSE;
            break;
          default:
            g_assert (0);       /* don't worry, we never get here */
            break;
        }

        caps = GST_FF_AUD_CAPS_NEW ("audio/x-raw-int",
            "width", G_TYPE_INT, width,
            "depth", G_TYPE_INT, depth,
            "endianness", G_TYPE_INT, endianness,
            "signed", G_TYPE_BOOLEAN, signedness, NULL);
      } while (0);
      break;

    case CODEC_ID_PCM_MULAW:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-mulaw", NULL);
      break;

    case CODEC_ID_PCM_ALAW:
      caps = GST_FF_AUD_CAPS_NEW ("audio/x-alaw", NULL);
      break;

    case CODEC_ID_ADPCM_IMA_QT:
    case CODEC_ID_ADPCM_IMA_WAV:
    case CODEC_ID_ADPCM_IMA_DK3:
    case CODEC_ID_ADPCM_IMA_DK4:
    case CODEC_ID_ADPCM_IMA_WS:
    case CODEC_ID_ADPCM_IMA_SMJPEG:
    case CODEC_ID_ADPCM_MS:
    case CODEC_ID_ADPCM_4XM:
    case CODEC_ID_ADPCM_XA:
    case CODEC_ID_ADPCM_ADX:
    case CODEC_ID_ADPCM_EA:
    case CODEC_ID_ADPCM_G726:
      do {
        gchar *layout = NULL;

        switch (codec_id) {
          case CODEC_ID_ADPCM_IMA_QT:
            layout = "quicktime";
            break;
          case CODEC_ID_ADPCM_IMA_WAV:
            layout = "dvi";
            break;
          case CODEC_ID_ADPCM_IMA_DK3:
            layout = "dk3";
            break;
          case CODEC_ID_ADPCM_IMA_DK4:
            layout = "dk4";
            break;
          case CODEC_ID_ADPCM_IMA_WS:
            layout = "westwood";
            break;
          case CODEC_ID_ADPCM_IMA_SMJPEG:
            layout = "smjpeg";
            break;
          case CODEC_ID_ADPCM_MS:
            layout = "microsoft";
            break;
          case CODEC_ID_ADPCM_4XM:
            layout = "4xm";
            break;
          case CODEC_ID_ADPCM_XA:
            layout = "xa";
            break;
          case CODEC_ID_ADPCM_ADX:
            layout = "adx";
            break;
          case CODEC_ID_ADPCM_EA:
            layout = "ea";
            break;
          case CODEC_ID_ADPCM_G726:
            layout = "g726";
            break;
          default:
            g_assert (0);       /* don't worry, we never get here */
            break;
        }

        /* FIXME: someone please check whether we need additional properties
         * in this caps definition. */
        caps = GST_FF_AUD_CAPS_NEW ("audio/x-adpcm",
            "layout", G_TYPE_STRING, layout, NULL);
        if (context)
          gst_caps_set_simple (caps,
              "block_align", G_TYPE_INT, context->block_align,
              "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      } while (0);
      break;

    case CODEC_ID_AMR_NB:
    case CODEC_ID_AMR_WB:
      /* what's this? ffmpeg uses external libs here that we don't include
       * so there's no point in defining those. Still, I want to know what
       * it actually is... */
      break;

    case CODEC_ID_RA_144:
    case CODEC_ID_RA_288:
      do {
        gint version = (codec_id == CODEC_ID_RA_144) ? 1 : 2;

        /* FIXME: properties? */
        caps = GST_FF_AUD_CAPS_NEW ("audio/x-pn-realaudio",
            "raversion", G_TYPE_INT, version, NULL);
      } while (0);
      break;

    case CODEC_ID_ROQ_DPCM:
    case CODEC_ID_INTERPLAY_DPCM:
    case CODEC_ID_XAN_DPCM:
      do {
        gchar *layout = NULL;

        switch (codec_id) {
          case CODEC_ID_ROQ_DPCM:
            layout = "roq";
            break;
          case CODEC_ID_INTERPLAY_DPCM:
            layout = "interplay";
            break;
          case CODEC_ID_XAN_DPCM:
            layout = "xan";
            break;
          default:
            g_assert (0);       /* don't worry, we never get here */
            break;
        }

        /* FIXME: someone please check whether we need additional properties
         * in this caps definition. */
        caps = GST_FF_AUD_CAPS_NEW ("audio/x-dpcm",
            "layout", G_TYPE_STRING, layout, NULL);
        if (context)
          gst_caps_set_simple (caps,
              "block_align", G_TYPE_INT, context->block_align,
              "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      } while (0);
      break;

    case CODEC_ID_FLAC:
      /* Note that ffmpeg has no encoder yet, but just for safety. In the
       * encoder case, we want to add things like samplerate, channels... */
      if (!encode) {
        caps = gst_caps_new_simple ("audio/x-flac", NULL);
      }
      break;

    default:
      g_warning ("Unknown codec ID %d, please add here", codec_id);
      break;
  }

  if (buildcaps) {
    AVCodec *codec;

    if ((codec = avcodec_find_decoder (codec_id)) ||
        (codec = avcodec_find_encoder (codec_id))) {
      gchar *mime = NULL;

      switch (codec->type) {
        case CODEC_TYPE_VIDEO:
          mime = g_strdup_printf ("video/x-gst_ff-%s", codec->name);
          caps = GST_FF_VID_CAPS_NEW (mime, NULL);
          g_free (mime);
          break;
        case CODEC_TYPE_AUDIO:
          mime = g_strdup_printf ("audio/x-gst_ff-%s", codec->name);
          caps = GST_FF_AUD_CAPS_NEW (mime, NULL);
          if (context)
            gst_caps_set_simple (caps,
                "block_align", G_TYPE_INT, context->block_align,
                "bitrate", G_TYPE_INT, context->bit_rate, NULL);
          g_free (mime);
          break;
        default:
          break;
      }
    }
  }

  if (caps != NULL) {
    char *str;

    /* set private data */
    if (context && context->extradata_size > 0) {
      GstBuffer *data = gst_buffer_new_and_alloc (context->extradata_size);

      memcpy (GST_BUFFER_DATA (data), context->extradata,
          context->extradata_size);
      gst_caps_set_simple (caps,
          "codec_data", GST_TYPE_BUFFER, data, NULL);
      gst_buffer_unref (data);
    }

    str = gst_caps_to_string (caps);
    GST_DEBUG ("caps for codec_id=%d: %s", codec_id, str);
    g_free (str);

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
gst_ffmpeg_pixfmt_to_caps (enum PixelFormat pix_fmt, AVCodecContext * context)
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
    case PIX_FMT_PAL8:
      bpp = depth = 8;
      endianness = G_BYTE_ORDER;
      break;
    default:
      /* give up ... */
      break;
  }

  if (bpp != 0) {
    if (r_mask != 0) {
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
gst_ffmpeg_codectype_to_caps (enum CodecType codec_type,
    AVCodecContext * context)
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
  gst_structure_get_int (structure, "block_align", &context->block_align);
  gst_structure_get_int (structure, "bitrate", &context->bit_rate);

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
#if 0
        case FIXME:
          context->pix_fmt = PIX_FMT_YUV444P;
          break;
#endif
      }
    }
  } else if (strcmp (gst_structure_get_name (structure),
          "video/x-raw-rgb") == 0) {
    gint bpp = 0, rmask = 0, endianness = 0;

    if (gst_structure_get_int (structure, "bpp", &bpp) &&
        gst_structure_get_int (structure, "endianness", &endianness)) {
      if (gst_structure_get_int (structure, "red_mask", &rmask)) {
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
gst_ffmpeg_caps_with_codectype (enum CodecType type,
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
 * caps_with_codecid () transforms a GstCaps for a known codec
 * ID into a filled-in context.
 */
                                                                                
void
gst_ffmpeg_caps_with_codecid (enum CodecID codec_id,
    enum CodecType codec_type, const GstCaps *caps, AVCodecContext *context)
{
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *value;
  const GstBuffer *buf;

  if (!context)
    return;

  /* extradata parsing (esds [mpeg4], wma/wmv, msmpeg4v1/2/3, etc.) */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    buf = g_value_get_boxed (value);
    context->extradata = av_mallocz (GST_BUFFER_SIZE (buf));
    memcpy (context->extradata, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    context->extradata_size = GST_BUFFER_SIZE (buf);
  }

  switch (codec_id) {
    case CODEC_ID_MPEG4:
      do {
        const gchar *mime = gst_structure_get_name (str);

        if (!strcmp (mime, "video/x-divx"))
          context->codec_tag = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
        else if (!strcmp (mime, "video/x-xvid"))
          context->codec_tag = GST_MAKE_FOURCC ('X', 'V', 'I', 'D');
        else if (!strcmp (mime, "video/x-3ivx"))
          context->codec_tag = GST_MAKE_FOURCC ('3', 'I', 'V', '1');
        else if (!strcmp (mime, "video/mpeg"))
          context->codec_tag = GST_MAKE_FOURCC ('m', 'p', '4', 'v');
      } while (0);
      break;

    case CODEC_ID_SVQ3:
      do {
        gint halfpel_flag, thirdpel_flag, low_delay, unknown_svq3_flag;
        guint16 flags;

        if (gst_structure_get_int (str, "halfpel_flag", &halfpel_flag) ||
            gst_structure_get_int (str, "thirdpel_flag", &thirdpel_flag) ||
            gst_structure_get_int (str, "low_delay", &low_delay) ||
            gst_structure_get_int (str, "unknown_svq3_flag",
		&unknown_svq3_flag)) {
          context->extradata = (guint8 *) av_mallocz (0x64);
          g_stpcpy (context->extradata, "SVQ3");
          flags = 1 << 3;
          flags |= low_delay;
          flags = flags << 2;
          flags |= unknown_svq3_flag;
          flags = flags << 6;
          flags |= halfpel_flag;
          flags = flags << 1;
          flags |= thirdpel_flag;
          flags = flags << 3;

          flags = GUINT16_FROM_LE (flags);

          memcpy (context->extradata + 0x62, &flags, 2);
          context->extradata_size = 0x64;
        }
      } while (0);
      break;

    case CODEC_ID_MSRLE:
    case CODEC_ID_QTRLE:
      do {
        gint depth;

        if (gst_structure_get_int (str, "depth", &depth))
          context->bits_per_sample = depth;
        gst_ffmpeg_get_palette (caps, context);
      } while (0);
      break;

    default:
      break;
  }

  /* common properties (width, height, fps) */
  switch (codec_type) {
    case CODEC_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context, codec_id == CODEC_ID_RAWVIDEO);
      break;
    case CODEC_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context, FALSE);
      break;
    default:
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
gst_ffmpeg_formatid_to_caps (const gchar * format_name)
{
  GstCaps *caps = NULL;

  if (!strcmp (format_name, "mpeg")) {
    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "mpegts")) {
    caps = gst_caps_new_simple ("video/mpegts",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "rm")) {
    caps = gst_caps_new_simple ("application/x-pn-realmedia",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "asf")) {
    caps = gst_caps_new_simple ("video/x-ms-asf", NULL);
  } else if (!strcmp (format_name, "avi")) {
    caps = gst_caps_new_simple ("video/x-msvideo", NULL);
  } else if (!strcmp (format_name, "wav")) {
    caps = gst_caps_new_simple ("audio/x-wav", NULL);
  } else if (!strcmp (format_name, "swf")) {
    caps = gst_caps_new_simple ("application/x-shockwave-flash", NULL);
  } else if (!strcmp (format_name, "au")) {
    caps = gst_caps_new_simple ("audio/x-au", NULL);
  } else if (!strcmp (format_name, "mov_mp4_m4a_3gp")) {
    caps = gst_caps_new_simple ("video/quicktime", NULL);
  } else if (!strcmp (format_name, "dv")) {
    caps = gst_caps_new_simple ("video/x-dv",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "4xm")) {
    caps = gst_caps_new_simple ("video/x-4xm", NULL);
  } else if (!strcmp (format_name, "matroska")) {
    caps = gst_caps_new_simple ("video/x-matroska", NULL);
  } else {
    gchar *name;

    GST_WARNING ("Could not create stream format caps for %s", format_name);
    name = g_strdup_printf ("application/x-gst_ff-%s", format_name);
    caps = gst_caps_new_simple (name, NULL);
    g_free (name);
  }

  return caps;
}

/* Convert a GstCaps to a FFMPEG codec ID. Size et all
 * are omitted, that can be queried by the user itself,
 * we're not eating the GstCaps or anything
 * A pointer to an allocated context is also needed for
 * optional extra info
 */

enum CodecID
gst_ffmpeg_caps_to_codecid (const GstCaps * caps, AVCodecContext * context)
{
  enum CodecID id = CODEC_ID_NONE;
  const gchar *mimetype;
  const GstStructure *structure;
  gboolean video = FALSE, audio = FALSE;        /* we want to be sure! */

  g_return_val_if_fail (caps != NULL, CODEC_ID_NONE);
  g_return_val_if_fail (gst_caps_get_size (caps) == 1, CODEC_ID_NONE);
  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw-rgb") ||
      !strcmp (mimetype, "video/x-raw-yuv")) {
    id = CODEC_ID_RAWVIDEO;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-raw-int")) {
    gint depth, width, endianness;
    gboolean signedness;

    if (gst_structure_get_int (structure, "endianness", &endianness) &&
        gst_structure_get_boolean (structure, "signed", &signedness) &&
        gst_structure_get_int (structure, "width", &width) &&
        gst_structure_get_int (structure, "depth", &depth) &&
        depth == width) {
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
      if (id != CODEC_ID_NONE)
        audio = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-mulaw")) {
    id = CODEC_ID_PCM_MULAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-alaw")) {
    id = CODEC_ID_PCM_ALAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-dv")) {
    id = CODEC_ID_DVVIDEO;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dv")) {        /* ??? */
    id = CODEC_ID_DVAUDIO;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-h263")) {
    id = CODEC_ID_H263;         /* or H263[IP] */
    video = TRUE;
  } else if (!strcmp (mimetype, "video/mpeg")) {
    gboolean sys_strm;
    gint mpegversion;

    if (!gst_structure_get_boolean (structure, "systemstream", &sys_strm) &&
        !gst_structure_get_int (structure, "mpegversion", &mpegversion) &&
        !sys_strm) {
      switch (mpegversion) {
        case 1:
          id = CODEC_ID_MPEG1VIDEO;
          break;
        case 2:
          id = CODEC_ID_MPEG2VIDEO;
          break;
        case 4:
          id = CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-jpeg")) {
    id = CODEC_ID_MJPEG;        /* A... B... */
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-wmv")) {
    gint wmvversion = 0;

    if (gst_structure_get_int (structure, "wmvversion", &wmvversion)) {
      switch (wmvversion) {
        case 1:
          id = CODEC_ID_WMV1;
          break;
        case 2:
          id = CODEC_ID_WMV2;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-vorbis")) {
    id = CODEC_ID_VORBIS;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/mpeg")) {
    gint layer = 0;
    gint mpegversion = 0;

    if (gst_structure_get_int (structure, "mpegversion", &mpegversion)) {
      switch (mpegversion) {
        case 2:                /* ffmpeg uses faad for both... */
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
            }
          }
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-wma")) {
    gint wmaversion = 0;

    if (gst_structure_get_int (structure, "wmaversion", &wmaversion)) {
      switch (wmaversion) {
        case 1:
          id = CODEC_ID_WMAV1;
          break;
        case 2:
          id = CODEC_ID_WMAV2;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-ac3")) {
    id = CODEC_ID_AC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-msmpeg")) {
    gint msmpegversion = 0;

    if (gst_structure_get_int (structure, "msmpegversion", &msmpegversion)) {
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
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-svq")) {
    gint svqversion = 0;

    if (gst_structure_get_int (structure, "svqversion", &svqversion)) {
      switch (svqversion) {
        case 1:
          id = CODEC_ID_SVQ1;
          break;
        case 3:
          id = CODEC_ID_SVQ3;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-huffyuv")) {
    id = CODEC_ID_HUFFYUV;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-mace")) {
    gint maceversion = 0;

    if (gst_structure_get_int (structure, "maceversion", &maceversion)) {
      switch (maceversion) {
        case 3:
          id = CODEC_ID_MACE3;
          break;
        case 6:
          id = CODEC_ID_MACE6;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-theora")) {
    id = CODEC_ID_THEORA;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp3")) {
    id = CODEC_ID_VP3;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-indeo")) {
    gint indeoversion = 0;

    if (gst_structure_get_int (structure, "indeoversion", &indeoversion) &&
        indeoversion == 3) {
      id = CODEC_ID_INDEO3;
      video = TRUE;
    }
  } else if (!strcmp (mimetype, "video/x-divx")) {
    gint divxversion = 0;

    if (gst_structure_get_int (structure, "divxversion", &divxversion)) {
      switch (divxversion) {
        case 3:
          id = CODEC_ID_MSMPEG4V3;
          break;
        case 4:
        case 5:
          id = CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-3ivx")) {
    id = CODEC_ID_MPEG4;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-xvid")) {
    id = CODEC_ID_MPEG4;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-ffv")) {
    gint ffvversion = 0;

    if (gst_structure_get_int (structure, "ffvversion", &ffvversion) &&
        ffvversion == 1) {
      id = CODEC_ID_FFV1;
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
    } else if (!strcmp (layout, "dvi")) {
      id = CODEC_ID_ADPCM_IMA_WAV;
    } else if (!strcmp (layout, "4xm")) {
      id = CODEC_ID_ADPCM_4XM;
    } else if (!strcmp (layout, "smjpeg")) {
      id = CODEC_ID_ADPCM_IMA_SMJPEG;
    } else if (!strcmp (layout, "dk3")) {
      id = CODEC_ID_ADPCM_IMA_DK3;
    } else if (!strcmp (layout, "dk4")) {
      id = CODEC_ID_ADPCM_IMA_DK4;
    } else if (!strcmp (layout, "westwood")) {
      id = CODEC_ID_ADPCM_IMA_WS;
    } else if (!strcmp (layout, "xa")) {
      id = CODEC_ID_ADPCM_XA;
    } else if (!strcmp (layout, "adx")) {
      id = CODEC_ID_ADPCM_ADX;
    } else if (!strcmp (layout, "ea")) {
      id = CODEC_ID_ADPCM_EA;
    } else if (!strcmp (layout, "g726")) {
      id = CODEC_ID_ADPCM_G726;
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-4xm")) {
    id = CODEC_ID_4XM;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (!layout) {
      /* .. */
    } else if (!strcmp (layout, "roq")) {
      id = CODEC_ID_ROQ_DPCM;
    } else if (!strcmp (layout, "interplay")) {
      id = CODEC_ID_INTERPLAY_DPCM;
    } else if (!strcmp (layout, "xan")) {
      id = CODEC_ID_XAN_DPCM;
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-flac")) {
    id = CODEC_ID_FLAC;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-cinepak")) {
    id = CODEC_ID_CINEPAK;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-pn-realvideo")) {
    gint rmversion;

    if (gst_structure_get_int (structure, "rmversion", &rmversion)) {
      switch (rmversion) {
        case 1:
          id = CODEC_ID_RV10;
          break;
        case 2:
          id = CODEC_ID_RV20;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-pn-realaudio")) {
    gint raversion;

    if (gst_structure_get_int (structure, "raversion", &raversion)) {
      switch (raversion) {
        case 1:
          id = CODEC_ID_RA_144;
          break;
        case 2:
          id = CODEC_ID_RA_288;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-rle")) {
    const gchar *layout;

    if ((layout = gst_structure_get_string (structure, "layout"))) {
      if (!strcmp (layout, "microsoft")) {
        id = CODEC_ID_MSRLE;
        video = TRUE;
      }
    }
  } else if (!strncmp (mimetype, "audio/x-gst_ff-", 15) ||
      !strncmp (mimetype, "video/x-gst_ff-", 15)) {
    gchar ext[16];
    AVCodec *codec;

    if (strlen (mimetype) <= 30 &&
        sscanf (mimetype, "%*s/x-gst_ff-%s", ext) == 1) {
      if ((codec = avcodec_find_decoder_by_name (ext)) ||
          (codec = avcodec_find_encoder_by_name (ext))) {
        const GValue *data_v;
        const GstBuffer *data;

        id = codec->id;
        if (mimetype[0] == 'v')
          video = TRUE;
        else if (mimetype[0] == 'a')
          audio = TRUE;
      }
    }
  }

  if (context != NULL) {
    if (video == TRUE) {
      context->codec_type = CODEC_TYPE_VIDEO;
    } else if (audio == TRUE) {
      context->codec_type = CODEC_TYPE_AUDIO;
    } else {
      context->codec_type = CODEC_TYPE_UNKNOWN;
    }
    context->codec_id = id;
    gst_ffmpeg_caps_with_codecid (id, context->codec_type, caps, context);
  }

  if (id != CODEC_ID_NONE) {
    char *str = gst_caps_to_string (caps);

    GST_DEBUG ("The id=%d belongs to the caps %s", id, str);
    g_free (str);
  }

  return id;
}
