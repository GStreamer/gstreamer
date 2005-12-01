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

#include "gstfakesink.h"
#include "gstfakesrc.h"
#include "gstfdsrc.h"
#include "gstfilesink.h"
#include "gstfilesrc.h"
#include "gstidentity.h"
#include "gstqueue.h"
#include "gsttee.h"
#include "gsttypefindelement.h"

struct _elements_entry
{
  gchar *name;
  guint rank;
    GType (*type) (void);
};


/* this declaration is here because there's no gstcapsfilter.h */
extern GType gst_capsfilter_get_type (void);

static struct _elements_entry _elements[] = {
  {"capsfilter", GST_RANK_NONE, gst_capsfilter_get_type},
  {"fakesrc", GST_RANK_NONE, gst_fake_src_get_type},
  {"fakesink", GST_RANK_NONE, gst_fake_sink_get_type},
#if HAVE_SYS_SOCKET_H
  {"fdsrc", GST_RANK_NONE, gst_fd_src_get_type},
#endif
  {"filesrc", GST_RANK_NONE, gst_file_src_get_type},
  {"identity", GST_RANK_NONE, gst_identity_get_type},
  {"queue", GST_RANK_NONE, gst_queue_get_type},
  {"filesink", GST_RANK_NONE, gst_file_sink_get_type},
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
    "coreelements",
    "standard GStreamer elements",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
