/* Gnome-Streamer
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


#include "gstudpsrc.h"
#include "gstudpsink.h"

/* elementfactory information */
extern GstElementDetails gst_udpsrc_details;
extern GstElementDetails gst_udpsink_details;

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *src, *sink;

  /* create an elementfactory for the udpsrc element */
  sink = gst_elementfactory_new ("udpsink",GST_TYPE_UDPSINK,
                                   &gst_udpsink_details);
  g_return_val_if_fail (sink != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (sink));

  src = gst_elementfactory_new ("udpsrc",GST_TYPE_UDPSRC,
                                   &gst_udpsrc_details);
  g_return_val_if_fail (src != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (src));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "udp",
  plugin_init
};
