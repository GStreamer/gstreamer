/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gstffmpegallcodecmap.h"


/* Convert a FFMPEG codec ID and optional AVCodecContext
 * to a GstCaps. If the context ix ommitted, no values for
 * video/audio size will be included in the GstCaps
 */

GstCaps *
gst_ffmpeg_codecid_to_caps (enum CodecID    codec_id,
                            AVCodecContext *context)
{
  GstCaps *caps = NULL;
  guint32 fourcc = 0;

  g_return_val_if_fail (codec_id != CODEC_ID_NONE, NULL);

  switch (codec_id) {
    case CODEC_ID_MPEG1VIDEO:
      fourcc = GST_MAKE_FOURCC('M','P','E','G');

      if (context) {
        caps = GST_CAPS_NEW ("ffmpeg_mpeg1video",
                             "video/mpeg",
                               "mpegversion",  GST_PROPS_INT (1),
                               "systemstream", GST_PROPS_BOOLEAN (FALSE),
                               "width",        GST_PROPS_INT (context->width),
                               "height",       GST_PROPS_INT (context->height),
                               NULL
                            );
      } else {
        caps = GST_CAPS_NEW ("ffmpeg_mpeg1video",
                             "video/mpeg",
                               "mpegversion",  GST_PROPS_INT (1),
                               "systemstream", GST_PROPS_BOOLEAN (FALSE),
                               "width",        GST_PROPS_INT_RANGE (16, 4096),
                               "height",       GST_PROPS_INT_RANGE (16, 4096),
                               NULL
                            );
      }
      break;

    case CODEC_ID_H263P:
    case CODEC_ID_H263I:
    case CODEC_ID_H263:
      fourcc = GST_MAKE_FOURCC('H','2','6','3');

      caps = GST_CAPS_NEW ("ffmpeg_h263",
                           "video/H263",
                             NULL
                          );
      break;

    case CODEC_ID_RV10:
      /* .. */
      break;

    case CODEC_ID_MP2:
    case CODEC_ID_MP3LAME:
      caps = GST_CAPS_NEW ("ffmpeg_mp2_mp3",
                           "audio/x-mp3",
                             NULL
                          );
      break;

    case CODEC_ID_VORBIS:
      caps = GST_CAPS_NEW ("ffmpeg_vorbis",
		           "application/x-ogg",
			     NULL);
      break;
      
    case CODEC_ID_AC3:
      caps = GST_CAPS_NEW ("ffmpeg_ac3",
		           "audio/ac3",
			     NULL);
      break;

    case CODEC_ID_MJPEG:
    case CODEC_ID_MJPEGB:
      fourcc = GST_MAKE_FOURCC ('M','J','P','G');

      if (context) {
        caps = GST_CAPS_NEW ("ffmpeg_mjpeg",
                             "video/jpeg",
                               "width",  GST_PROPS_INT (context->width),
                               "height", GST_PROPS_INT (context->height),
                               NULL
                            );
      } else {
        caps = GST_CAPS_NEW ("ffmpeg_mjpeg",
                             "video/jpeg",
                               "width",  GST_PROPS_INT_RANGE (16, 4096),
                               "height", GST_PROPS_INT_RANGE (16, 4096),
                               NULL
                            );
      }
      break;

    case CODEC_ID_MPEG4:
      fourcc = GST_MAKE_FOURCC ('D','I','V','X');
      break;

    case CODEC_ID_RAWVIDEO:
      if (context) {
        int bpp = 0, depth = 0, endianness = 0;
        gulong g_mask = 0, r_mask = 0, b_mask = 0;
        guint32 fmt = 0;

        switch (context->pix_fmt) {
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
          caps = GST_CAPS_NEW ("ffmpeg_rawvideo",
                               "video/raw",
                                 "format",     GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' ')),
                                 "width",      GST_PROPS_INT (context->width),
                                 "height",     GST_PROPS_INT (context->height),
                                 "bpp",        GST_PROPS_INT (bpp),
                                 "depth",      GST_PROPS_INT (depth),
                                 "red_mask",   GST_PROPS_INT (r_mask),
                                 "green_mask", GST_PROPS_INT (g_mask),
                                 "blue_mask",  GST_PROPS_INT (b_mask),
                                 "endianness", GST_PROPS_INT (endianness),
                                 NULL
                               );
        } else if (fmt) {
          caps = GST_CAPS_NEW ("ffmpeg_rawvideo",
                               "video/raw",
                                 "format",     GST_PROPS_FOURCC (fmt),
                                 "width",      GST_PROPS_INT (context->width),
                                 "height",     GST_PROPS_INT (context->height),
                                 NULL
                               );
        }
      } else {
        caps = GST_CAPS_NEW ("ffpeg_rawvideo",
                             "video/raw",
                               NULL
                            );
      }
      break;

    case CODEC_ID_MSMPEG4V1:
      fourcc = GST_MAKE_FOURCC ('M','P','G','4');
      break;

    case CODEC_ID_MSMPEG4V2:
      fourcc = GST_MAKE_FOURCC ('M','P','4','2');
      break;

    case CODEC_ID_MSMPEG4V3:
      fourcc = GST_MAKE_FOURCC ('M','P','4','3');
      break;

    case CODEC_ID_WMV1:
      fourcc = GST_MAKE_FOURCC ('W','M','V','1');
    case CODEC_ID_WMV2:
      if (!fourcc) /* EVIL! */
        fourcc = GST_MAKE_FOURCC ('W','M','V','2');

      if (context) {
        caps = GST_CAPS_NEW ("ffmpeg_wmv",
                             "video/wmv",
                               "width",  GST_PROPS_INT (context->width),
                               "height", GST_PROPS_INT (context->height),
                               NULL
                            );
      } else {
        caps = GST_CAPS_NEW ("ffmpeg_wmv",
                             "video/wmv",
                               "width",  GST_PROPS_INT_RANGE (16, 4096),
                               "height", GST_PROPS_INT_RANGE (16, 4096),
                               NULL
                            );
      }
      break;

    case CODEC_ID_SVQ1:
      /* .. */
      break;

    case CODEC_ID_DVVIDEO:
      fourcc = GST_MAKE_FOURCC('D','V','S','D');
      /* fall-through */
    case CODEC_ID_DVAUDIO:
      if (context) {
        caps = GST_CAPS_NEW ("ffmpeg_dvvideo",
                             "video/dv",
                               "format",  GST_PROPS_LIST (
                                            GST_PROPS_STRING ("NTSC"),
                                            GST_PROPS_STRING ("PAL")
                                          ),
                               "width",   GST_PROPS_INT_RANGE (16, 4096),
                               "height",  GST_PROPS_INT_RANGE (16, 4096),
                               NULL
                            );
      } else {
        caps = GST_CAPS_NEW ("ffmpeg_dvvideo",
                             "video/dv",
                               "format",  GST_PROPS_STRING ("NTSC"), /* FIXME */
                               "width",   GST_PROPS_INT (context->width),
                               "height",  GST_PROPS_INT (context->height),
                               NULL
                            );
      }
      break;

    case CODEC_ID_WMAV1:
    case CODEC_ID_WMAV2:
      caps = GST_CAPS_NEW ("ffmpeg_wma",
                           "audio/x-wma",
                             NULL
                          );
      break;

    case CODEC_ID_MACE3:
      /* .. */
      break;

    case CODEC_ID_MACE6:
      /* .. */
      break;

    case CODEC_ID_HUFFYUV:
      fourcc = GST_MAKE_FOURCC('H','F','Y','U');
      break;

    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_ALAW:
      do {
        gint law = -1, width = 0, depth = 0, endianness = 0;
	gboolean signedness = FALSE; /* blabla */

        switch (codec_id) {
          case CODEC_ID_PCM_S16LE:
            law = 0; width = 16; depth = 16;
            endianness = G_LITTLE_ENDIAN;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_S16BE:
            law = 0; width = 16; depth = 16;
            endianness = G_BIG_ENDIAN;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_U16LE:
            law = 0; width = 16; depth = 16;
            endianness = G_LITTLE_ENDIAN;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_U16BE:
            law = 0; width = 16; depth = 16;
            endianness = G_BIG_ENDIAN;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_S8:
            law = 0; width = 8;  depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = TRUE;
            break;
          case CODEC_ID_PCM_U8:
            law = 0; width = 8;  depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_MULAW:
            law = 1; width = 8;  depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = FALSE;
            break;
          case CODEC_ID_PCM_ALAW:
            law = 2; width = 8;  depth = 8;
            endianness = G_BYTE_ORDER;
            signedness = FALSE;
            break;
          default:
            g_assert(0); /* don't worry, we never get here */
            break;
        }

        if (context) {
          caps = GST_CAPS_NEW ("ffmpeg_pcmaudio",
                               "audio/raw",
                                 "format",     GST_PROPS_STRING ("int"),
                                 "law",        GST_PROPS_INT (law),
                                 "width",      GST_PROPS_INT (width),
                                 "depth",      GST_PROPS_INT (depth),
                                 "endianness", GST_PROPS_INT (endianness),
                                 "signed",     GST_PROPS_BOOLEAN (signedness),
                                 "rate",       GST_PROPS_INT (context->sample_rate),
                                 "channels",   GST_PROPS_INT (context->channels),
                                 NULL
                               );
        } else {
          caps = GST_CAPS_NEW ("ffmpeg_pcmaudio",
                               "audio/raw",
                                 "format",     GST_PROPS_STRING ("int"),
                                 "law",        GST_PROPS_INT (law),
                                 "width",      GST_PROPS_INT (width),
                                 "depth",      GST_PROPS_INT (depth),
                                 "endianness", GST_PROPS_INT (endianness),
                                 "signed",     GST_PROPS_BOOLEAN (signedness),
                                 "rate",       GST_PROPS_INT_RANGE (1000, 48000),
                                 "channels",   GST_PROPS_INT_RANGE (1, 2),
                                 NULL
                               );
        }
      } while (0);
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

    default:
      /* .. */
      break;
  }

  if (fourcc) {
    GstCaps *avi_caps;

    if (context) {
      avi_caps = GST_CAPS_NEW ("ffmpeg_mjpeg2",
                               "video/avi",
                                 "format",      GST_PROPS_STRING ("strf_vids"),
                                 "compression", GST_PROPS_FOURCC (fourcc),
                                 "width",       GST_PROPS_INT (context->width),
                                 "height",      GST_PROPS_INT (context->height),
                                 NULL
                              );
    } else {
      avi_caps = GST_CAPS_NEW ("ffmpeg_mjpeg2",
                               "video/avi",
                                 "format",      GST_PROPS_STRING ("strf_vids"),
                                 "compression", GST_PROPS_FOURCC (fourcc),
                                 "width",       GST_PROPS_INT_RANGE (16, 4096),
                                 "height",      GST_PROPS_INT_RANGE (16, 4096),
                                 NULL
                              );
    }

    if (caps)
      caps = gst_caps_append(caps, avi_caps);
    else
      caps = avi_caps;
  }

  if (caps != NULL) {
    char *str = g_strdup_printf("The caps that belongs to codec_id=%d", codec_id);
    gst_caps_debug(caps, str);
    g_free(str);
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
  gboolean video = FALSE;

  g_return_val_if_fail (caps != NULL, CODEC_ID_NONE);

  mimetype = gst_caps_get_mime(caps);

  if (!strcmp(mimetype, "video/avi")) {

    const gchar *format = NULL;
    if (gst_caps_has_property(caps, "format")) {
      gst_caps_get_string(caps, "format", &format);
    }
    if (format && !strcmp(format, "strf_vids")) {
      guint32 compression = 0;
      if (gst_caps_has_property(caps, "compression")) {
        gst_caps_get_fourcc_int(caps, "compression", &compression);
      }
      switch (compression) {
        case GST_MAKE_FOURCC('M','J','P','G'):
        case GST_MAKE_FOURCC('J','P','E','G'):
        case GST_MAKE_FOURCC('P','I','X','L'): /* these two are used by Pinnacle */
        case GST_MAKE_FOURCC('V','I','X','L'): /* and Miro for Zoran/JPEG codecs */
          id = CODEC_ID_MJPEG; /* or MJPEGB */
          break;
        case GST_MAKE_FOURCC('H','F','Y','U'):
          id = CODEC_ID_HUFFYUV;
          break;
        case GST_MAKE_FOURCC('D','V','S','D'):
        case GST_MAKE_FOURCC('d','v','s','d'):
          id = CODEC_ID_DVVIDEO;
          break;
        case GST_MAKE_FOURCC('M','P','E','G'):
        case GST_MAKE_FOURCC('M','P','G','I'):
          id = CODEC_ID_MPEG1VIDEO;
          break;
        case GST_MAKE_FOURCC('H','2','6','3'):
        case GST_MAKE_FOURCC('i','2','6','3'):
        case GST_MAKE_FOURCC('L','2','6','3'):
        case GST_MAKE_FOURCC('M','2','6','3'):
        case GST_MAKE_FOURCC('V','D','O','W'):
        case GST_MAKE_FOURCC('V','I','V','O'):
        case GST_MAKE_FOURCC('x','2','6','3'):
          id = CODEC_ID_H263; /* or H263[IP] */
          break;
        case GST_MAKE_FOURCC('d','i','v','x'):
        case GST_MAKE_FOURCC('D','I','V','3'):
        case GST_MAKE_FOURCC('D','I','V','4'):
        case GST_MAKE_FOURCC('D','I','V','5'):
        case GST_MAKE_FOURCC('M','P','4','3'):
          id = CODEC_ID_MSMPEG4V3;
          break;
        case GST_MAKE_FOURCC('D','I','V','X'):
        case GST_MAKE_FOURCC('D','X','5','0'):
        case GST_MAKE_FOURCC('X','V','I','D'):
        case GST_MAKE_FOURCC('x','v','i','d'):
          id = CODEC_ID_MPEG4;
          break;
        case GST_MAKE_FOURCC('M','P','G','4'):
          id = CODEC_ID_MSMPEG4V1;
          break;
        case GST_MAKE_FOURCC('M','P','4','2'):
          id = CODEC_ID_MSMPEG4V2;
          break;
        case GST_MAKE_FOURCC('W','M','V','1'):
          id = CODEC_ID_WMV1;
          break;
        case GST_MAKE_FOURCC('W','M','V','2'):
          id = CODEC_ID_WMV2;
          break;
      }

      video = TRUE;
    } else if (format && !strcmp(format, "strf_auds")) {
      /* .. */
    }

  } else if (!strcmp(mimetype, "video/raw")) {

    id = CODEC_ID_RAWVIDEO; /* don't we need to provide more info here? */

    if (context) {
      gint depth = 0, endianness = 0;
      guint32 fmt_fcc = 0;

      gst_caps_get_fourcc_int(caps, "format", &fmt_fcc);

      switch (fmt_fcc) {
        case GST_MAKE_FOURCC('R','G','B',' '):
          gst_caps_get_int(caps, "endianness", &endianness);
          gst_caps_get_int(caps, "depth", &depth);
          switch (depth) {
            case 15:
              context->pix_fmt = PIX_FMT_RGB555;
              break;
            case 16:
              context->pix_fmt = PIX_FMT_RGB565;
              break;
            case 24:
              if (endianness == G_BIG_ENDIAN) {
                context->pix_fmt = PIX_FMT_RGB24;
              } else {
                context->pix_fmt = PIX_FMT_BGR24;
              }
              break;
            case 32:
              context->pix_fmt = PIX_FMT_RGBA32;
              break;
          }
          break;
        case GST_MAKE_FOURCC('Y','U','Y','2'):
          context->pix_fmt = PIX_FMT_YUV422;
          break;
        case GST_MAKE_FOURCC('I','4','2','0'):
        case GST_MAKE_FOURCC('I','Y','U','V'):
          context->pix_fmt = PIX_FMT_YUV420P;
          break;
        case GST_MAKE_FOURCC('Y','4','1','P'):
          context->pix_fmt = PIX_FMT_YUV411P;
          break;
      }

      video = TRUE;
    }

  } else if (!strcmp(mimetype, "audio/raw")) {

    gint law = -1, depth = 0, width = 0, endianness = 0;
    gboolean signedness = FALSE; /* bla default value */

    if (gst_caps_has_property(caps, "signedness")) {
      gst_caps_get_int(caps, "endianness", &endianness);
      gst_caps_get_boolean(caps, "endianness", &signedness);
      gst_caps_get_int(caps, "law", &law);
      gst_caps_get_int(caps, "width", &width);
      gst_caps_get_int(caps, "depth", &depth);

      if (context) {
        context->sample_rate = 0;
        context->channels = 0;
        gst_caps_get_int(caps, "channels", &context->channels);
        gst_caps_get_int(caps, "rate", &context->sample_rate);
      }

      g_return_val_if_fail(depth == width, CODEC_ID_NONE);

      switch (law) {
        case 0:
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
          break;
        case 1:
          id = CODEC_ID_PCM_MULAW;
          break;
        case 2:
          id = CODEC_ID_PCM_ALAW;
          break;
      }
    }

  } else if (!strcmp(mimetype, "video/dv")) {

    id = CODEC_ID_DVVIDEO; /* or DVAUDIO */
    video = TRUE;

  } else if (!strcmp(mimetype, "video/H263")) {

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
      if (mpegversion == 1) {
        id = CODEC_ID_MPEG1VIDEO;
      }
    }

    video = TRUE;

  } else if (!strcmp(mimetype, "video/jpeg")) {

    id = CODEC_ID_MJPEG;
    video = TRUE;

  } else if (!strcmp(mimetype, "video/wmv")) {

    id = CODEC_ID_WMV2; /* or WMV1 */
    video = TRUE;

  } else if (!strcmp(mimetype, "application/x-ogg")) {

    id = CODEC_ID_VORBIS;

  } else if (!strcmp(mimetype, "audio/x-mp3")) {

    id = CODEC_ID_MP3LAME; /* or MP2 */

  } else if (!strcmp(mimetype, "audio/x-wma")) {

    id = CODEC_ID_WMAV2; /* or WMAV1 */

  } else if (!strcmp(mimetype, "audio/ac3")) {

    id = CODEC_ID_AC3;

  }

  if (video && context) {
    if (gst_caps_has_property(caps, "width"))
      gst_caps_get_int(caps, "width", &context->width);
    if (gst_caps_has_property(caps, "height"))
      gst_caps_get_int(caps, "height", &context->height);

    /* framerate (context->frame_rate)? but then, we'd need a GstPad* */

    context->codec_type = CODEC_TYPE_VIDEO;
  } else {
    context->codec_type = CODEC_TYPE_AUDIO;
  }

  context->codec_id = id;

  if (id != CODEC_ID_NONE) {
    char *str = g_strdup_printf("The id=%d belongs to this caps", id);
    gst_caps_debug(caps, str);
    g_free(str);
  }

  return id;
}
