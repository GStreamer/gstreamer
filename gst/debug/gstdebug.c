/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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


gboolean gst_break_my_data_plugin_init (GstPlugin * plugin);
gboolean gst_negotiation_plugin_init (GstPlugin * plugin);
gboolean gst_navseek_plugin_init (GstPlugin * plugin);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_break_my_data_plugin_init (plugin);
  gst_negotiation_plugin_init (plugin);
  gst_navseek_plugin_init (plugin);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "debug",
    "elements for testing and debugging",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
