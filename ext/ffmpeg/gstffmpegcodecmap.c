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

#include <gst/gst.h>

GstCaps *
gst_ffmpegcodec_codec_context_to_caps (AVCodecContext *context, int codec_id)
{
  switch (codec_id) {
    case CODEC_ID_NONE:
      return GST_CAPS_NEW ("ffmpeg_none",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_MPEG1VIDEO:
      return GST_CAPS_NEW ("ffmpeg_mpeg1video",
		           "video/mpeg",
			     "mpegversion",  GST_PROPS_INT (1),
			     "systemstream", GST_PROPS_BOOLEAN (FALSE)
			  );
      break;
    case CODEC_ID_H263:
      return GST_CAPS_NEW ("ffmpeg_h263",
		           "video/H263",
			   NULL);
      break;
    case CODEC_ID_RV10:
      return GST_CAPS_NEW ("ffmpeg_rv10",
		           "video/x-rv10",
			   NULL);
      break;
    case CODEC_ID_MP2:
      return GST_CAPS_NEW ("ffmpeg_mp2",
		           "audio/x-mp3",
			   NULL);
      break;
    case CODEC_ID_MP3LAME:
      return GST_CAPS_NEW ("ffmpeg_mp3",
		           "audio/x-mp3",
			   NULL);
      break;
    case CODEC_ID_VORBIS:
      return GST_CAPS_NEW ("ffmpeg_vorbis",
		           "application/x-ogg",
			   NULL);
      break;
    case CODEC_ID_AC3:
      return GST_CAPS_NEW ("ffmpeg_ac3",
		           "audio/ac3",
			   NULL);
      break;
    case CODEC_ID_MJPEG:
      return GST_CAPS_NEW ("ffmpeg_mjpeg",
		           "video/x-mjpeg",
			   NULL);
      break;
    case CODEC_ID_MJPEGB:
      return GST_CAPS_NEW ("ffmpeg_mjpeg",
		           "video/x-mjpegb",
			   NULL);
      break;
    case CODEC_ID_MPEG4:
      if (context) {
        return GST_CAPS_NEW ("ffmpeg_mpeg4",
		             "video/avi",
			       "format",  GST_PROPS_STRING ("strf_vids"),
			        "compression",  GST_PROPS_FOURCC (context->fourcc),
			        "width",   GST_PROPS_INT (context->width),
			        "height",  GST_PROPS_INT (context->height)
			    );
      }
      else {
        return GST_CAPS_NEW ("ffmpeg_mpeg4",
		             "video/avi",
			       "format",  GST_PROPS_STRING ("strf_vids"),
			        "compression",  GST_PROPS_FOURCC (GST_STR_FOURCC ("DIV3")),
			        "width",   GST_PROPS_INT_RANGE (0, 4096),
			        "height",  GST_PROPS_INT_RANGE (0, 4096)
			    );
      }
      break;
    case CODEC_ID_RAWVIDEO:
      return GST_CAPS_NEW ("ffmpeg_rawvideo",
		           "video/raw",
			   NULL);
      break;
    case CODEC_ID_MSMPEG4V1:
      if (context) {
        return GST_CAPS_NEW ("ffmpeg_msmpeg4v1",
		             "video/avi",
			       "format",  	GST_PROPS_STRING ("strf_vids"),
			        "compression", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("MPG4")),
			        "width",   	GST_PROPS_INT (context->width),
			        "height",  	GST_PROPS_INT (context->height)
			    );
      }
      else {
        return GST_CAPS_NEW ("ffmpeg_msmpeg4v1",
		             "video/avi",
			       "format",  	GST_PROPS_STRING ("strf_vids"),
			        "compression", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("MPG4")),
			        "width",   	GST_PROPS_INT_RANGE (0, 4096),
			        "height",  	GST_PROPS_INT_RANGE (0, 4096)
			    );
      }
      break;
    case CODEC_ID_MSMPEG4V2:
      if (context) {
        return GST_CAPS_NEW ("ffmpeg_msmpeg4v2",
		             "video/avi",
			       "format",  GST_PROPS_STRING ("strf_vids"),
			        "compression",  GST_PROPS_FOURCC (GST_STR_FOURCC ("MP42")),
			        "width",   GST_PROPS_INT (context->width),
			        "height",  GST_PROPS_INT (context->height)
			    );
      }
      else {
        return GST_CAPS_NEW ("ffmpeg_msmpeg4v2",
		             "video/avi",
			       "format",  GST_PROPS_STRING ("strf_vids"),
			        "compression",  GST_PROPS_FOURCC (GST_STR_FOURCC ("MP42")),
			        "width",   GST_PROPS_INT_RANGE (0, 4096),
			        "height",  GST_PROPS_INT_RANGE (0, 4096)
			    );
      }
      break;
    case CODEC_ID_MSMPEG4V3:
      if (context) {
        return GST_CAPS_NEW ("ffmpeg_msmpeg4v3",
		             "video/avi",
			       "format",  GST_PROPS_STRING ("strf_vids"),
			        "compression",  GST_PROPS_FOURCC (GST_STR_FOURCC ("DIV3")),
			        "width",   GST_PROPS_INT (context->width),
			        "height",  GST_PROPS_INT (context->height)
			    );
      }
      else {
        return GST_CAPS_NEW ("ffmpeg_msmpeg4v3",
		             "video/avi",
			       "format",  GST_PROPS_STRING ("strf_vids"),
			        "compression",  GST_PROPS_FOURCC (GST_STR_FOURCC ("DIV3")),
			        "width",   GST_PROPS_INT_RANGE (0, 4096),
			        "height",  GST_PROPS_INT_RANGE (0, 4096)
			    );
      }
      break;
    case CODEC_ID_WMV1:
      if (context) {
        return GST_CAPS_NEW ("ffmpeg_wmv1",
		             "video/avi",
			       "format",  	GST_PROPS_STRING ("strf_vids"),
			        "compression", 	GST_PROPS_FOURCC (GST_STR_FOURCC ("WMV1")),
			        "width",   	GST_PROPS_INT (context->width),
			        "height",  	GST_PROPS_INT (context->height)
			    );
      }
      else {
        return GST_CAPS_NEW ("ffmpeg_wmv1",
		             "video/x-wmv1",
			     NULL
			    );
      }
      break;
    case CODEC_ID_WMV2:
      return GST_CAPS_NEW ("ffmpeg_wmv2",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_H263P:
      return GST_CAPS_NEW ("ffmpeg_h263p",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_H263I:
      return GST_CAPS_NEW ("ffmpeg_h263i",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_SVQ1:
      return GST_CAPS_NEW ("ffmpeg_svq1",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_DVVIDEO:
      return GST_CAPS_NEW ("ffmpeg_dvvideo",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_DVAUDIO: 
      return GST_CAPS_NEW ("ffmpeg_dvaudio",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_WMAV1:
      return GST_CAPS_NEW ("ffmpeg_wmav1",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_WMAV2:
      return GST_CAPS_NEW ("ffmpeg_wmav2",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_MACE3:
      return GST_CAPS_NEW ("ffmpeg_mace3",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_MACE6:
      return GST_CAPS_NEW ("ffmpeg_mace6",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_HUFFYUV:
      return GST_CAPS_NEW ("ffmpeg_huffyuv",
		           "video/x-huffyuv",
			   NULL);
      break;
    /* various pcm "codecs" */
    case CODEC_ID_PCM_S16LE:
      return GST_CAPS_NEW ("ffmpeg_s16le",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_PCM_S16BE:
      return GST_CAPS_NEW ("ffmpeg_s16be",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_PCM_U16LE:
      return GST_CAPS_NEW ("ffmpeg_u16le",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_PCM_U16BE:
      return GST_CAPS_NEW ("ffmpeg_u16be",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_PCM_S8:
      return GST_CAPS_NEW ("ffmpeg_s8",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_PCM_U8:
      return GST_CAPS_NEW ("ffmpeg_u8",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_PCM_MULAW:
      return GST_CAPS_NEW ("ffmpeg_mulaw",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_PCM_ALAW:
      return GST_CAPS_NEW ("ffmpeg_alaw",
		           "unknown/unknown",
			   NULL);
      break;
    /* various adpcm codecs */
    case CODEC_ID_ADPCM_IMA_QT:
      return GST_CAPS_NEW ("ffmpeg_adpcm_ima_qt",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_ADPCM_IMA_WAV:
      return GST_CAPS_NEW ("ffmpeg_adpcm_ima_wav",
		           "unknown/unknown",
			   NULL);
      break;
    case CODEC_ID_ADPCM_MS:
      return GST_CAPS_NEW ("ffmpeg_adpcm_ms",
		           "unknown/unknown",
			   NULL);
      break;
    default:
      g_warning ("no caps found for codec id %d\n", codec_id);
      break;
  }

  return NULL;
}
