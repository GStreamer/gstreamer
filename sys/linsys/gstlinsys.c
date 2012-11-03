/* GStreamer
 * Copyright (C) 2010 FIXME <fixme@example.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

#include "gstlinsyssdisink.h"
#include "gstlinsyssdisrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{

  gst_element_register (plugin, "linsyssdisrc", GST_RANK_NONE,
      gst_linsys_sdi_src_get_type ());
  gst_element_register (plugin, "linsyssdisink", GST_RANK_NONE,
      gst_linsys_sdi_sink_get_type ());

  return TRUE;
}

#define PACKAGE_ORIGIN "http://FIXME.org/"

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    linsys, "FIXME", plugin_init, VERSION, "LGPL", PACKAGE_NAME, PACKAGE_ORIGIN)
