/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosssink.c: 
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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <config.h>
#include "gstosscommon.h"

static gboolean 
gst_ossformat_get (gint law, gint endianness, gboolean sign, gint width, gint depth,
		   gint *format, gint *bps) 
{
  if (width != depth) 
    return FALSE;

  *bps = 1;

  if (law == 0) {
    if (width == 16) {
      if (sign == TRUE) {
        if (endianness == G_LITTLE_ENDIAN) {
	  *format = AFMT_S16_LE;
	  GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	             "16 bit signed LE, no law (%d)", *format);
	}
        else if (endianness == G_BIG_ENDIAN) {
	  *format = AFMT_S16_BE;
	  GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	             "16 bit signed BE, no law (%d)", *format);
	}
      }
      else {
        if (endianness == G_LITTLE_ENDIAN) {
	  *format = AFMT_U16_LE;
	  GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	             "16 bit unsigned LE, no law (%d)", *format);
	}
        else if (endianness == G_BIG_ENDIAN) {
	  *format = AFMT_U16_BE;
	  GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	             "16 bit unsigned BE, no law (%d)", *format);
	}
      }
      *bps = 2;
    }
    else if (width == 8) {
      if (sign == TRUE) {
	*format = AFMT_S8;
	GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	           "8 bit signed, no law (%d)", *format);
      }
      else {
        *format = AFMT_U8;
	GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	           "8 bit unsigned, no law (%d)", *format);
      }
      *bps = 1;
    }
  } else if (law == 1) {
    *format = AFMT_MU_LAW;
    GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	       "mu law (%d)", *format);
  } else if (law == 2) {
    *format = AFMT_A_LAW;
    GST_DEBUG (GST_CAT_PLUGIN_INFO, 
	       "a law (%d)", *format);
  } else {
    g_critical ("unknown law");
    return FALSE;
  }

  return TRUE;
}

void 
gst_osscommon_init (GstOssCommon *common) 
{
  common->device = g_strdup ("/dev/dsp");
  common->fd = -1;

  gst_osscommon_reset (common);
}

void 
gst_osscommon_reset (GstOssCommon *common) 
{
  common->law = 0;
  common->endianness = G_BYTE_ORDER;
  common->sign = TRUE;
  common->width = 16;
  common->depth = 16;
  common->channels = 2;
  common->rate = 44100;
  common->fragment = 6;
  common->bps = 0;

/* AFMT_*_BE not available on all OSS includes (e.g. FBSD) */
#ifdef WORDS_BIGENDIAN
  common->format = AFMT_S16_BE;
#else
  common->format = AFMT_S16_LE;
#endif /* WORDS_BIGENDIAN */  
}

gboolean 
gst_osscommon_parse_caps (GstOssCommon *common, GstCaps *caps) 
{
  gint bps, format;

  gst_caps_get_int (caps, "width", &common->width);
  gst_caps_get_int (caps, "depth", &common->depth);
	        
  if (common->width != common->depth) 
    return FALSE;
		  
  gst_caps_get_int (caps, "law", &common->law); 
  gst_caps_get_int (caps, "endianness", &common->endianness);
  gst_caps_get_boolean (caps, "signed", &common->sign);
			    
  if (!gst_ossformat_get (common->law, common->endianness, common->sign, 
                          common->width, common->depth, &format, &bps))
  { 
     GST_DEBUG (GST_CAT_PLUGIN_INFO, "could not get format");
     return FALSE;
  }

  gst_caps_get_int (caps, "channels", &common->channels);
  gst_caps_get_int (caps, "rate", &common->rate);
			      
  common->bps = bps * common->channels * common->rate;
  common->format = format;

  return TRUE;
}

#define GET_FIXED_INT(caps, name, dest)         \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_caps_get_int (caps, name, dest);        \
} G_STMT_END
#define GET_FIXED_BOOLEAN(caps, name, dest)     \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_caps_get_boolean (caps, name, dest);    \
} G_STMT_END

gboolean 
gst_osscommon_merge_fixed_caps (GstOssCommon *common, GstCaps *caps) 
{
  gint bps, format;

  /* peel off fixed stuff from the caps */
  GET_FIXED_INT         (caps, "law",        &common->law);
  GET_FIXED_INT         (caps, "endianness", &common->endianness);
  GET_FIXED_BOOLEAN     (caps, "signed",     &common->sign);
  GET_FIXED_INT         (caps, "width",      &common->width);
  GET_FIXED_INT         (caps, "depth",      &common->depth);

  if (!gst_ossformat_get (common->law, common->endianness, common->sign, 
                          common->width, common->depth, &format, &bps))
  { 
     return FALSE;
  }

  GET_FIXED_INT         (caps, "rate",       &common->rate);
  GET_FIXED_INT         (caps, "channels",   &common->channels);
			      
  common->bps = bps * common->channels * common->rate;
  common->format = format;
					  
  return TRUE;
}

gboolean 
gst_osscommon_sync_parms (GstOssCommon *common) 
{
  audio_buf_info space;
  int frag;
  gint target_format;
  gint target_channels;
  gint target_rate;
  gint fragscale, frag_ln;

  if (common->fd == -1)
    return FALSE;
  
  if (common->fragment >> 16)
    frag = common->fragment;
  else
    frag = 0x7FFF0000 | common->fragment;
  
  GST_INFO (GST_CAT_PLUGIN_INFO, 
            "common: setting sound card to %dHz %d format %s (%08x fragment)",
            common->rate, common->format,
            (common->channels == 2) ? "stereo" : "mono", frag);

  ioctl (common->fd, SNDCTL_DSP_SETFRAGMENT, &frag);
  ioctl (common->fd, SNDCTL_DSP_RESET, 0);

  target_format   = common->format;
  target_channels = common->channels;
  target_rate     = common->rate;

  ioctl (common->fd, SNDCTL_DSP_SETFMT,   &common->format);
  ioctl (common->fd, SNDCTL_DSP_CHANNELS, &common->channels);
  ioctl (common->fd, SNDCTL_DSP_SPEED,    &common->rate);

  ioctl (common->fd, SNDCTL_DSP_GETBLKSIZE, &common->fragment_size);

  if (common->mode == GST_OSSCOMMON_WRITE) {
    ioctl (common->fd, SNDCTL_DSP_GETOSPACE, &space);
  }
  else {
    ioctl (common->fd, SNDCTL_DSP_GETISPACE, &space);
  }

  /* calculate new fragment using a poor man's logarithm function */
  fragscale = 1;
  frag_ln = 0;
  while (fragscale < space.fragsize) {
    fragscale <<= 1;
    frag_ln++;
  }
  common->fragment = space.fragstotal << 16 | frag_ln;
	  
  GST_INFO (GST_CAT_PLUGIN_INFO, 
            "common: set sound card to %dHz, %d format, %s "
	    "(%d bytes buffer, %08x fragment)",
            common->rate, common->format,
            (common->channels == 2) ? "stereo" : "mono", 
	    space.bytes, common->fragment);

  common->fragment_time = (GST_SECOND * common->fragment_size) / common->bps;
  GST_INFO (GST_CAT_PLUGIN_INFO, "fragment time %u %llu\n", 
            common->bps, common->fragment_time);

  if (target_format   != common->format   ||
      target_channels != common->channels ||
      target_rate     != common->rate) 
  {
    g_warning ("couldn't set requested OSS parameters, enjoy the noise :)");
    /* we could eventually return FALSE here, or just do some additional tests
     * to see that the frequencies don't differ too much etc.. */
  }
  return TRUE;
}

gboolean
gst_osscommon_open_audio (GstOssCommon *common, GstOssOpenMode mode, gchar **error)
{
  gint caps;
  g_return_val_if_fail (common->fd == -1, FALSE);

  GST_INFO (GST_CAT_PLUGIN_INFO, "common: attempting to open sound device");

  /* first try to open the sound card */
  /* FIXME: this code is dubious, why do we need to open and close this ?*/
  if (mode == GST_OSSCOMMON_WRITE) {
    common->fd = open (common->device, O_WRONLY | O_NONBLOCK);
    if (errno == EBUSY) {
      g_warning ("osscommon: unable to open the sound device (in use ?)\n");
    }

    if (common->fd >= 0)
      close (common->fd);
  
    /* re-open the sound device in blocking mode */
    common->fd = open (common->device, O_WRONLY);
  }
  else {
    common->fd = open (common->device, O_RDONLY);
  }

  if (common->fd < 0) {
    switch (errno) {
      case EISDIR:
	*error = g_strdup_printf ("osscommon: Device %s is a directory",
			   common->device);
	break;
      case EACCES:
      case ETXTBSY:
	*error = g_strdup_printf ( "osscommon: Cannot access %s, check permissions",
			   common->device);
	break;
      case ENXIO:
      case ENODEV:
      case ENOENT:
	*error = g_strdup_printf ("osscommon: Cannot access %s, does it exist ?",
			   common->device);
	break;
      case EROFS:
	*error = g_strdup_printf ("osscommon: Cannot access %s, read-only filesystem ?",
			   common->device);
      default:
	/* FIXME: strerror is not threadsafe */
	*error = g_strdup_printf ("osscommon: Cannot open %s, generic error: %s",
			   common->device, strerror (errno));
	break;
    }
    return FALSE;
  }

  common->mode = mode;

  /* we have it, set the default parameters and go have fun */
  /* set card state */
  ioctl (common->fd, SNDCTL_DSP_GETCAPS, &caps);

  GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon: Capabilities %08x", caps);

  if (caps & DSP_CAP_DUPLEX)	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Full duplex");
  if (caps & DSP_CAP_REALTIME) 	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Realtime");
  if (caps & DSP_CAP_BATCH)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Batch");
  if (caps & DSP_CAP_COPROC)   	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Has coprocessor");
  if (caps & DSP_CAP_TRIGGER)  	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Trigger");
  if (caps & DSP_CAP_MMAP)     	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Direct access");

#ifdef DSP_CAP_MULTI
  if (caps & DSP_CAP_MULTI)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Multiple open");
#endif /* DSP_CAP_MULTI */

#ifdef DSP_CAP_BIND
  if (caps & DSP_CAP_BIND)     	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   Channel binding");
#endif /* DSP_CAP_BIND */

  ioctl(common->fd, SNDCTL_DSP_GETFMTS, &caps);

  GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon: Formats %08x", caps);
  if (caps & AFMT_MU_LAW)  	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   MU_LAW");
  if (caps & AFMT_A_LAW)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   A_LAW");
  if (caps & AFMT_IMA_ADPCM)   	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   IMA_ADPCM");
  if (caps & AFMT_U8)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   U8");
  if (caps & AFMT_S16_LE)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   S16_LE");
  if (caps & AFMT_S16_BE)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   S16_BE");
  if (caps & AFMT_S8)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   S8");
  if (caps & AFMT_U16_LE)       GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   U16_LE");
  if (caps & AFMT_U16_BE)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   U16_BE");
  if (caps & AFMT_MPEG)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   MPEG");
#ifdef AFMT_AC3
  if (caps & AFMT_AC3)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osscommon:   AC3");
#endif

  GST_INFO (GST_CAT_PLUGIN_INFO, 
		   "osscommon: opened audio (%s) with fd=%d", common->device, common->fd);

  common->caps = caps;

  return TRUE;
}

void
gst_osscommon_close_audio (GstOssCommon *common)
{
  if (common->fd < 0) 
    return;

  close(common->fd);
  common->fd = -1;
}

gboolean
gst_osscommon_convert (GstOssCommon *common, GstFormat src_format, gint64 src_value,
	               GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (common->bps == 0 || common->channels == 0 || common->width == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
	  *dest_value = src_value * GST_SECOND / common->bps;
          break;
        case GST_FORMAT_UNITS:
	  *dest_value = src_value / (common->channels * common->width);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * common->bps / GST_SECOND;
          break;
        case GST_FORMAT_UNITS:
	  *dest_value = src_value * common->rate / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_UNITS:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
	  *dest_value = src_value * GST_SECOND / common->rate;
          break;
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * common->channels * common->width;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

