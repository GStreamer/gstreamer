/* GStreamer
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


#include <gst/gst.h>


#include "dv1394src.h"


static GstElementDetails gst_dv1394src_details = {
  "Firewire (1394) DV Source",
  "Source/1394/DV",
  "Source for DV video data from firewire port",
  VERSION,
  "Erik Walthinsen <omega@temple-baptist.com>",
  "(C) 2001",
};


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("dv1394src",GST_TYPE_DV1394SRC,
                                   &gst_dv1394src_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = { 
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gst1394",
  plugin_init
};

