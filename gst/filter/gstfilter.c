/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilter.c: element for filter plug-ins
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
#include "gstfilter.h"
#include <gst/audio/audio.h>


struct _elements_entry {
  gchar *name;
  GType (*type) (void);
};

static struct _elements_entry _elements[] = {
  { "iir",  	gst_iir_get_type	},
  { "lpwsinc",  gst_lpwsinc_get_type	},
  { "bpwsinc",  gst_bpwsinc_get_type	},
  { NULL, 0 },
};

GstStaticPadTemplate gst_filter_src_template =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS )
);

GstStaticPadTemplate gst_filter_sink_template =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS )
);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gint i = 0;

  while (_elements[i].name) {
    if (!gst_element_register (plugin, _elements[i].name, GST_RANK_NONE, _elements[i].type()))
      return FALSE;

    i++;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "filter",
  "IIR, lpwsinc and bpwsinc audio filter elements",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
);
