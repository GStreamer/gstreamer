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


#include "gstosscommon.h"
#include <sys/soundcard.h>

gboolean 
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
