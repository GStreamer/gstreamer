/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsteffectv.h"


struct _elements_entry {
  gchar *name;
  GType (*type) (void);
};

static struct _elements_entry _elements[] = {
  { "edgeTV",  		gst_edgetv_get_type }, 
  { "agingTV", 		gst_agingtv_get_type },
  { "diceTV",  		gst_dicetv_get_type },
  { "warpTV",  		gst_warptv_get_type },
  { "shagadelicTV",  	gst_shagadelictv_get_type },
  { "vertigoTV",  	gst_vertigotv_get_type },
  { "revTV",  		gst_revtv_get_type },
  { "quarkTV", 		gst_quarktv_get_type },
  { NULL, 0 },
};


GstStaticPadTemplate gst_effectv_src_template =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_32 )
);

GstStaticPadTemplate gst_effectv_sink_template =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_32 )
);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gint i = 0;

  while (_elements[i].name) {
    if (!gst_element_register (plugin, _elements[i].name,
	  GST_RANK_NONE, (_elements[i].type) ()))
      return FALSE;
    i++;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "effectv",
  "effect plugins from the effectv project",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
);
