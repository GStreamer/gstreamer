/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelements.c:
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
#  include "config.h"
#endif

#include <gst/gst.h>

#include "gstaggregator.h"
#include "gstfakesink.h"
#include "gstfakesrc.h"
#include "gstfdsink.h"
#include "gstfdsrc.h"
#include "gstfilesink.h"
#include "gstfilesrc.h"
#include "gstidentity.h"
#include "gstmd5sink.h"
#include "gstmultifilesrc.h"
#include "gstpipefilter.h"
#include "gstshaper.h"
#include "gststatistics.h"
#include "gsttee.h"
#include "gsttypefind.h"


struct _elements_entry
{
  gchar *name;
  guint rank;
    GType (*type) (void);
};


extern GType gst_filesrc_get_type (void);
extern GstElementDetails gst_filesrc_details;

static struct _elements_entry _elements[] = {
  {"aggregator", GST_RANK_NONE, gst_aggregator_get_type},
  {"fakesrc", GST_RANK_NONE, gst_fakesrc_get_type},
  {"fakesink", GST_RANK_NONE, gst_fakesink_get_type},
  {"fdsink", GST_RANK_NONE, gst_fdsink_get_type},
  {"fdsrc", GST_RANK_NONE, gst_fdsrc_get_type},
  {"filesrc", GST_RANK_NONE, gst_filesrc_get_type},
  {"filesink", GST_RANK_NONE, gst_filesink_get_type},
  {"identity", GST_RANK_NONE, gst_identity_get_type},
  {"md5sink", GST_RANK_NONE, gst_md5sink_get_type},
#ifndef HAVE_WIN32
  {"multifilesrc", GST_RANK_NONE, gst_multifilesrc_get_type},
#endif
  {"pipefilter", GST_RANK_NONE, gst_pipefilter_get_type},
  {"shaper", GST_RANK_NONE, gst_shaper_get_type},
  {"statistics", GST_RANK_NONE, gst_statistics_get_type},
  {"tee", GST_RANK_NONE, gst_tee_get_type},
  {"typefind", GST_RANK_NONE, gst_type_find_element_get_type},
  {NULL, 0},
};

static gboolean
plugin_init (GstPlugin * plugin)
{
  struct _elements_entry *my_elements = _elements;

  while ((*my_elements).name) {
    if (!gst_element_register (plugin, (*my_elements).name, (*my_elements).rank,
            ((*my_elements).type) ()))
      return FALSE;
    my_elements++;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstelements",
    "standard GStreamer elements",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
