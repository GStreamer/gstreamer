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
gst_riff_create_video_caps (guint32             codec_fcc,
			    gst_riff_strh      *strh,
			    gst_riff_strf_vids *strf)
{
  GstCaps *caps = NULL;

  switch (codec_fcc) {
    case GST_MAKE_FOURCC('I','4','2','0'):
    case GST_MAKE_FOURCC('Y','U','Y','2'):
      caps = GST_CAPS_NEW (
                  "riff_video_raw",
                  "video/x-raw-yuv",
                    "format",  GST_PROPS_FOURCC (codec_fcc)
                );
      break;

    case GST_MAKE_FOURCC('M','J','P','G'): /* YUY2 MJPEG */
    case GST_MAKE_FOURCC('J','P','E','G'): /* generic (mostly RGB) MJPEG */
    case GST_MAKE_FOURCC('P','I','X','L'): /* Miro/Pinnacle fourccs */
    case GST_MAKE_FOURCC('V','I','X','L'): /* Miro/Pinnacle fourccs */
      caps = GST_CAPS_NEW (
                  "riff_video_jpeg",
                  "video/x-jpeg",
                    NULL
                );
      break;

    case GST_MAKE_FOURCC('H','F','Y','U'):
      caps = GST_CAPS_NEW (
                  "riff_video_hfyu",
                  "video/x-huffyuv",
                    NULL
                );
      break;

    case GST_MAKE_FOURCC('M','P','E','G'):
    case GST_MAKE_FOURCC('M','P','G','I'):
      caps = GST_CAPS_NEW (
                  "riff_video_mpeg1",
                  "video/mpeg",
                    "systemstream", GST_PROPS_BOOLEAN (FALSE),
		    "mpegversion", GST_PROPS_BOOLEAN (1)
                );
      break;

    case GST_MAKE_FOURCC('H','2','6','3'):
    case GST_MAKE_FOURCC('i','2','6','3'):
    case GST_MAKE_FOURCC('L','2','6','3'):
    case GST_MAKE_FOURCC('M','2','6','3'):
    case GST_MAKE_FOURCC('V','D','O','W'):
    case GST_MAKE_FOURCC('V','I','V','O'):
    case GST_MAKE_FOURCC('x','2','6','3'):
      caps = GST_CAPS_NEW (
                  "riff_video_h263",
                  "video/x-h263",
                    NULL
                );
      break;

    case GST_MAKE_FOURCC('D','I','V','3'):
    case GST_MAKE_FOURCC('D','I','V','4'):
    case GST_MAKE_FOURCC('D','I','V','5'):
      caps = GST_CAPS_NEW (
                  "riff_video_divx3",
                  "video/x-divx",
		    "divxversion", GST_PROPS_INT(3)
                );
      break;

    case GST_MAKE_FOURCC('d','i','v','x'):
    case GST_MAKE_FOURCC('D','I','V','X'):
    case GST_MAKE_FOURCC('D','X','5','0'):
      caps = GST_CAPS_NEW (
                  "riff_video_divx45",
                  "video/x-divx",
		    "divxversion", GST_PROPS_INT(5)
                );
      break;

    case GST_MAKE_FOURCC('X','V','I','D'):
    case GST_MAKE_FOURCC('x','v','i','d'):
      caps = GST_CAPS_NEW (
                  "riff_video_xvid",
                  "video/x-xvid",
                    NULL
                );
      break;

    case GST_MAKE_FOURCC('M','P','G','4'):
      caps = GST_CAPS_NEW (
                  "riff_video_msmpeg41",
                  "video/x-msmpeg",
		    "msmpegversion", GST_PROPS_INT (41)
                );
      break;

    case GST_MAKE_FOURCC('M','P','4','2'):
      caps = GST_CAPS_NEW (
                  "riff_video_msmpeg42",
                  "video/x-msmpeg",
		    "msmpegversion", GST_PROPS_INT (42)
                );
      break;

    case GST_MAKE_FOURCC('M','P','4','3'):
      caps = GST_CAPS_NEW (
                  "riff_video_msmpeg43",
                  "video/x-msmpeg",
		    "msmpegversion", GST_PROPS_INT (43)
                );
      break;

    case GST_MAKE_FOURCC('3','I','V','1'):
    case GST_MAKE_FOURCC('3','I','V','2'):
      caps = GST_CAPS_NEW (
		  "riff_video_3ivx",
		  "video/x-3ivx",
		    NULL
		);
      break;

    case GST_MAKE_FOURCC('D','V','S','D'):
    case GST_MAKE_FOURCC('d','v','s','d'):
      caps = GST_CAPS_NEW (
                  "riff_video_dv",
                  "video/x-dv",
                    "systemstream", GST_PROPS_BOOLEAN (FALSE)
                );
      break;

    case GST_MAKE_FOURCC('W','M','V','1'):
      caps = GST_CAPS_NEW (
                  "riff_video_wmv1",
                  "video/x-wmv",
                    "wmvversion", GST_PROPS_INT (1)
                );
      break;

    case GST_MAKE_FOURCC('W','M','V','2'):
      caps = GST_CAPS_NEW (
                  "riff_video_wmv2",
                  "video/x-wmv",
                    "wmvversion", GST_PROPS_INT (2)
                );
      break;

    default:
      GST_WARNING ("Unkown video fourcc " GST_FOURCC_FORMAT,
		   GST_FOURCC_ARGS (codec_fcc));
      break;
  }

  /* add general properties */
  if (caps != NULL) {
    GstPropsEntry *framerate, *width, *height;

    if (strh != NULL) {
      gfloat fps = 1. * strh->rate / strh->scale;

      framerate = gst_props_entry_new ("framerate",
			GST_PROPS_FLOAT (fps));
    } else {
      framerate = gst_props_entry_new ("framerate",
			GST_PROPS_FLOAT_RANGE (0., G_MAXFLOAT));
    }

    if (strf != NULL) {
      width = gst_props_entry_new ("width",
			GST_PROPS_INT (strf->width));
      height = gst_props_entry_new ("height",
			GST_PROPS_INT (strf->height));
    } else {
      width = gst_props_entry_new ("width",
			GST_PROPS_INT_RANGE (16, 4096));
      height = gst_props_entry_new ("height",
			GST_PROPS_INT_RANGE (16, 4096));
    }

    if (!caps->properties)
      caps->properties = gst_props_empty_new ();

    gst_props_add_entry (caps->properties, width);
    gst_props_add_entry (caps->properties, height);
    gst_props_add_entry (caps->properties, framerate);
  }

  return caps;
}

GstCaps *
gst_riff_create_audio_caps (guint16             codec_id,
			    gst_riff_strh      *strh,
			    gst_riff_strf_auds *strf)
{
  GstCaps *caps = NULL;

  switch (codec_id) {
    case GST_RIFF_WAVE_FORMAT_MPEGL3: /* mp3 */
      caps = GST_CAPS_NEW ("riff_audio_mp1l3",
			   "audio/mpeg",
			     "mpegversion", GST_PROPS_INT (1),
			     "layer", GST_PROPS_INT (3));
      break;

    case GST_RIFF_WAVE_FORMAT_MPEGL12: /* mp1 or mp2 */
      caps = GST_CAPS_NEW ("riff_audio_mp1l12",
				   "audio/mpeg",
				     "layer", GST_PROPS_INT (2));
      break;

    case GST_RIFF_WAVE_FORMAT_PCM: /* PCM/wav */ {
      GstPropsEntry *width = NULL, *depth = NULL, *signedness = NULL;

      if (strf != NULL) {
        gint ba = GUINT16_FROM_LE (strf->blockalign);
        gint ch = GUINT16_FROM_LE (strf->channels);
        gint ws = GUINT16_FROM_LE (strf->size);

        width = gst_props_entry_new ("width",
				     GST_PROPS_INT (ba * 8 / ch));
        depth = gst_props_entry_new ("depth",
				     GST_PROPS_INT (ws));
        signedness = gst_props_entry_new ("signed",
					  GST_PROPS_BOOLEAN (ws != 8));
      } else {
        signedness = gst_props_entry_new ("signed",
					  GST_PROPS_LIST (
					    GST_PROPS_BOOLEAN (TRUE),
					    GST_PROPS_BOOLEAN (FALSE)));
        width = gst_props_entry_new ("width",
				     GST_PROPS_LIST (
				       GST_PROPS_INT (8),
				       GST_PROPS_INT (16)));
        depth = gst_props_entry_new ("depth",
				     GST_PROPS_LIST (
				       GST_PROPS_INT (8),
				       GST_PROPS_INT (16)));
      }

      caps = GST_CAPS_NEW ("riff_audio_pcm",
				   "audio/x-raw-int",
				     "endianness",
				       GST_PROPS_INT (G_LITTLE_ENDIAN));
      gst_props_add_entry (caps->properties, width);
      gst_props_add_entry (caps->properties, depth);
      gst_props_add_entry (caps->properties, signedness);

    }
      break;

    case GST_RIFF_WAVE_FORMAT_MULAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of mulaw audio, overwriting.",
		     strf->size);
      }
      caps = GST_CAPS_NEW ("riff_audio_mulaw",
				   "audio/x-mulaw",
				     NULL);
      break;

    case GST_RIFF_WAVE_FORMAT_ALAW:
      if (strf != NULL && strf->size != 8) {
        GST_WARNING ("invalid depth (%d) of alaw audio, overwriting.",
		     strf->size);
      }
      caps = GST_CAPS_NEW ("riff_audio_alaw",
				   "audio/x-alaw",
				     NULL);
      break;

    case GST_RIFF_WAVE_FORMAT_VORBIS1: /* ogg/vorbis mode 1 */
    case GST_RIFF_WAVE_FORMAT_VORBIS2: /* ogg/vorbis mode 2 */
    case GST_RIFF_WAVE_FORMAT_VORBIS3: /* ogg/vorbis mode 3 */
    case GST_RIFF_WAVE_FORMAT_VORBIS1PLUS: /* ogg/vorbis mode 1+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS2PLUS: /* ogg/vorbis mode 2+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS3PLUS: /* ogg/vorbis mode 3+ */
      caps = GST_CAPS_NEW ("riff_audio_vorbis",
				   "audio/x-vorbis",
				     NULL);
      break;

    case GST_RIFF_WAVE_FORMAT_A52:
      caps = GST_CAPS_NEW ("riff_audio_ac3",
				   "audio/x-ac3",
				     NULL);
      break;

    default:
      GST_WARNING ("Unkown audio tag 0x%04x",
		   codec_id);
      break;
  }

  if (caps != NULL) {
    GstPropsEntry *samplerate, *channels;

    if (strf != NULL) {
      samplerate = gst_props_entry_new ("rate",
			GST_PROPS_INT (strf->rate));
      channels = gst_props_entry_new ("channels",
			GST_PROPS_INT (strf->channels));
    } else {
      samplerate = gst_props_entry_new ("rate",
			GST_PROPS_INT_RANGE (8000, 96000));
      channels = gst_props_entry_new ("channels",
			GST_PROPS_INT_RANGE (1, 2));
    }

    if (!caps->properties)
      caps->properties = gst_props_empty_new ();

    gst_props_add_entry (caps->properties, samplerate);
    gst_props_add_entry (caps->properties, channels);
  }

  return caps;
}

GstCaps *
gst_riff_create_iavs_caps (guint32             codec_fcc,
			   gst_riff_strh      *strh,
			   gst_riff_strf_iavs *strf)
{
  GstCaps *caps = NULL;

  switch (codec_fcc) {
    /* is this correct? */
    case GST_MAKE_FOURCC ('D','V','S','D'):
    case GST_MAKE_FOURCC ('d','v','s','d'):
      caps = GST_CAPS_NEW ("riff_iavs_dv", 
			   "video/x-dv", 
			     "systemstream", GST_PROPS_BOOLEAN (TRUE));

    default:
      GST_WARNING ("Unkown IAVS fourcc " GST_FOURCC_FORMAT,
		   GST_FOURCC_ARGS (codec_fcc));
      break;
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
    GST_MAKE_FOURCC ('I','4','2','0'),
    GST_MAKE_FOURCC ('Y','U','Y','2'),
    GST_MAKE_FOURCC ('M','J','P','G'),
    GST_MAKE_FOURCC ('D','V','S','D'),
    GST_MAKE_FOURCC ('W','M','V','1'),
    GST_MAKE_FOURCC ('W','M','V','2'),
    GST_MAKE_FOURCC ('M','P','G','4'),
    GST_MAKE_FOURCC ('M','P','4','2'),
    GST_MAKE_FOURCC ('M','P','4','3'),
    GST_MAKE_FOURCC ('H','F','Y','U'),
    GST_MAKE_FOURCC ('D','I','V','3'),
    GST_MAKE_FOURCC ('M','P','E','G'),
    GST_MAKE_FOURCC ('H','2','6','3'),
    GST_MAKE_FOURCC ('D','I','V','X'),
    GST_MAKE_FOURCC ('X','V','I','D'),
    GST_MAKE_FOURCC ('3','I','V','1'),
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps = NULL, *one;

  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_video_caps (tags[i], NULL, NULL);
    if (one)
      caps = gst_caps_append (caps, one);
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
  GstCaps *caps = NULL, *one;

  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_audio_caps (tags[i], NULL, NULL);
    if (one)
      caps = gst_caps_append (caps, one);
  }

  return caps;
}

GstCaps *
gst_riff_create_iavs_template_caps (void)
{
  guint32 tags[] = {
    GST_MAKE_FOURCC ('D','V','S','D'),
    /* FILL ME */
    0
  };
  guint i;
  GstCaps *caps = NULL, *one;

  for (i = 0; tags[i] != 0; i++) {
    one = gst_riff_create_iavs_caps (tags[i], NULL, NULL);
    if (one)
      caps = gst_caps_append (caps, one);
  }

  return caps;
}
