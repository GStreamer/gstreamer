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
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif
#include <string.h>
#include <gst/gst.h>

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
		      ##props)					\
	:							\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT_RANGE (16, 4096),	\
		      "height", GST_PROPS_INT_RANGE (16, 4096),	\
		      ##props)

/* same for audio - now with channels/sample rate
 */

#define GST_FF_AUD_CAPS_NEW(name, mimetype, props...)			\
	(context != NULL) ?						\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT (context->sample_rate),	\
		      "channels", GST_PROPS_INT (context->channels),	\
		      ##props)						\
	:								\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT_RANGE (8000, 96000),	\
		      "channels", GST_PROPS_INT_RANGE (1, 2),		\
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
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_mpeg1video",
                                  "video/mpeg",
                                    "mpegversion",  GST_PROPS_INT (1),
                                    "systemstream", GST_PROPS_BOOLEAN (FALSE)
                                 );
      break;

    case CODEC_ID_H263P:
    case CODEC_ID_H263I:
    case CODEC_ID_H263:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_h263",
                                  "video/h263"
                                 );
      break;

    case CODEC_ID_RV10:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_rv10",
                                  "video/realvideo"
                                 );
      break;

    case CODEC_ID_MP2:
      caps = GST_CAPS_NEW ("ffmpeg_mp2",
                           "audio/x-mp3",
                             "layer", GST_PROPS_INT (2)
                          );
      break;

    case CODEC_ID_MP3LAME:
      caps = GST_CAPS_NEW ("ffmpeg_mp3",
                           "audio/x-mp3",
                             "layer", GST_PROPS_INT (3)
                          );
      break;

    case CODEC_ID_VORBIS:
      caps = GST_CAPS_NEW ("ffmpeg_vorbis",
		           "application/x-ogg",
			   NULL
			  );
      break;
      
    case CODEC_ID_AC3:
      caps = GST_CAPS_NEW ("ffmpeg_ac3",
		           "audio/ac3",
			   NULL
			  );
      break;

    case CODEC_ID_MJPEG:
    case CODEC_ID_MJPEGB:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_mjpeg",
                                  "video/jpeg"
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
                                  "video/divx",
				    "divxversion",  GST_PROPS_INT (5)
                                 ));
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_xvid",
                                  "video/xvid"
                                 ));
      break;

    case CODEC_ID_MSMPEG4V1:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_msmpeg4v1",
                                  "video/x-msmpeg",
                                    "mpegversion", GST_PROPS_INT (41)
                                 );
      break;

    case CODEC_ID_MSMPEG4V2:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_msmpeg4v2",
                                  "video/x-msmpeg",
                                    "mpegversion", GST_PROPS_INT (42)
                                 );
      break;

    case CODEC_ID_MSMPEG4V3:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_msmpeg4v3",
                                  "video/x-msmpeg",
                                    "mpegversion", GST_PROPS_INT (43)
                                 );
      break;

    case CODEC_ID_WMV1:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_wmv1",
                                  "video/wmv",
                                    "wmvversion", GST_PROPS_INT (1)
                                 );
      break;

    case CODEC_ID_WMV2:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_wmv2",
                                  "video/wmv",
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

    case CODEC_ID_DVAUDIO: /* ??? */
    case CODEC_ID_DVVIDEO:
      if (!context) {
        caps = GST_FF_VID_CAPS_NEW ("ffmpeg_dvvideo",
                                    "video/dv",
                                      "format",  GST_PROPS_LIST (
                                                   GST_PROPS_STRING ("NTSC"),
                                                   GST_PROPS_STRING ("PAL")
                                                 )
                                   );
      } else {
        GstPropsEntry *normentry;

	if (context->height == 576) {
	  normentry = gst_props_entry_new("format", GST_PROPS_STRING ("PAL"));
	} else {
	  normentry = gst_props_entry_new("format", GST_PROPS_STRING ("NTSC"));
	}

        caps = GST_FF_VID_CAPS_NEW ("ffmpeg_dvvideo",
                                    "video/dv",
                                   );
	gst_props_add_entry(caps->properties, normentry);
      }
      break;

    case CODEC_ID_WMAV1:
      caps = GST_CAPS_NEW ("ffmpeg_wma1",
                           "audio/wma",
                             "wmaversion", GST_PROPS_INT (1)
                          );
      break;

    case CODEC_ID_WMAV2:
      caps = GST_CAPS_NEW ("ffmpeg_wma2",
                           "audio/wma",
                             "wmaversion", GST_PROPS_INT (2)
                          );
      break;

    case CODEC_ID_MACE3:
      /* .. */
      break;

    case CODEC_ID_MACE6:
      /* .. */
      break;

    case CODEC_ID_HUFFYUV:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_huffyuv",
                                  "video/huffyuv"
                                 );
      break;

    case CODEC_ID_CYUV:
      /* .. */
      break;

    case CODEC_ID_H264:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_h264",
                                  "video/h264"
                                 );
      break;

    case CODEC_ID_INDEO3:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_indeo3",
                                  "video/indeo3"
                                 );
      break;

    case CODEC_ID_VP3:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_vp3",
                                  "video/vp3"
                                 );
      caps = gst_caps_append(caps,
             GST_FF_VID_CAPS_NEW ("ffmpeg_theora",
                                  "video/x-theora"
                                 ));
      break;

    case CODEC_ID_AAC:
      /* .. */
      break;

    case CODEC_ID_MPEG4AAC:
      caps = GST_FF_VID_CAPS_NEW ("ffmpeg_mpeg4aac",
                                  "audio/mpeg",
                                    "systemstream", GST_PROPS_BOOLEAN (FALSE),
                                    "mpegversion",  GST_PROPS_INT (4)
                                 );
      break;

    case CODEC_ID_ASV1:
      /* .. */
      break;

    case CODEC_ID_ADPCM_IMA_QT:
      /* .. */
      break;

    case CODEC_ID_ADPCM_IMA_WAV:
      /* .. */
      break;

    case CODEC_ID_ADPCM_MS:
      /* .. */
      break;

    case CODEC_ID_AMR_NB:
      /* .. */
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
      endianness = G_LITTLE_ENDIAN;
      r_mask = 0xff0000; g_mask = 0x00ff00; b_mask = 0x0000ff;
      break;
    case PIX_FMT_YUV422P:
      /* .. */
      break;
    case PIX_FMT_YUV444P:
      /* .. */
      break;
    case PIX_FMT_RGBA32:
      bpp = depth = 32;
      endianness = G_BYTE_ORDER;
      r_mask = 0x00ff0000; g_mask = 0x0000ff00; b_mask = 0x000000ff;
      break;
    case PIX_FMT_YUV410P:
      /* .. */
      break;
    case PIX_FMT_YUV411P:
      fmt = GST_MAKE_FOURCC ('Y','4','1','P');
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
    fmt = GST_MAKE_FOURCC ('R','G','B',' ');
    caps = GST_FF_VID_CAPS_NEW ("ffmpeg_rawvideo",
                                "video/raw",
                                  "format",     GST_PROPS_FOURCC (fmt),
                                  "bpp",        GST_PROPS_INT (bpp),
                                  "depth",      GST_PROPS_INT (depth),
                                  "red_mask",   GST_PROPS_INT (r_mask),
                                  "green_mask", GST_PROPS_INT (g_mask),
                                  "blue_mask",  GST_PROPS_INT (b_mask),
                                  "endianness", GST_PROPS_INT (endianness)
                                );
  } else if (fmt) {
    caps = GST_FF_VID_CAPS_NEW ("ffmpeg_rawvideo",
                                "video/raw",
                                  "format",     GST_PROPS_FOURCC (fmt)
                               );
  }

  if (caps != NULL) {
    char *str = g_strdup_printf("The caps that belongs to pix_fmt=%d",
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
                                "audio/raw",
                                  "signed",     GST_PROPS_BOOLEAN (signedness),
                                  "endianness", GST_PROPS_INT (G_BYTE_ORDER),
                                  "width",      GST_PROPS_INT (bpp),
                                  "depth",      GST_PROPS_INT (bpp),
                                  "law",        GST_PROPS_INT (0),
                                  "format",     GST_PROPS_STRING ("int")
                                );
  }

  if (caps != NULL) {
    char *str = g_strdup_printf("The caps that belongs to sample_fmt=%d",
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

  if (caps != NULL) {
    char *str = g_strdup_printf("The caps that belongs to codec_type=%d",
				codec_type);
    gst_caps_debug(caps, str);
    g_free(str);
  }

  return caps;
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
    gst_caps_get_int (caps, "rate", &context->channels);
  }

  if (gst_caps_has_property_typed (caps, "rate",
				   GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "rate", &context->sample_rate);
  }
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
      case GST_MAKE_FOURCC ('Y','4','1','P'):
        context->pix_fmt = PIX_FMT_YUV411P;
        break;
      case GST_MAKE_FOURCC ('R','G','B',' '):
        if (gst_caps_has_property_typed (caps, "depth",
					 GST_PROPS_INT_TYPE) &&
	    gst_caps_has_property_typed (caps, "endianness",
					 GST_PROPS_INT_TYPE)) {
          gint depth = 0, endianness = 0;
          gst_caps_get_int (caps, "depth", &depth);
          gst_caps_get_int (caps, "endianness", &endianness);

          switch (depth) {
            case 32:
              if (endianness == G_BYTE_ORDER)
                context->pix_fmt = PIX_FMT_RGBA32;
              break;
            case 24:
              switch (endianness) {
                case G_LITTLE_ENDIAN:
                  context->pix_fmt = PIX_FMT_BGR24;
                  break;
                case G_BIG_ENDIAN:
                  context->pix_fmt = PIX_FMT_RGB24;
                  break;
                default:
                  /* nothing */
                  break;
              }
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
        break;
      default:
        /* nothing */
        break;
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
			 "audio/x-pn-realaudio",
                           NULL
                        );
  } else if (!strcmp (format_name, "asf")) {
    caps = GST_CAPS_NEW ("ffmpeg_asf",
			 "video/x-ms-asf",
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
			 "audio/basic",
                           NULL
                        );
  } else if (!strcmp (format_name, "mov")) {
    caps = GST_CAPS_NEW ("ffmpeg_quicktime",
			 "video/quicktime",
                           NULL
                        );
  } else if (!strcmp (format_name, "dv")) {
    caps = GST_CAPS_NEW ("ffmpeg_dv",
			 "video/dv",
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
