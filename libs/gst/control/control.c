/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstcontrol.c: GStreamer control utility library
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
 
#include <gst/gst.h>
#include "control.h"

static void 
gst_control_init_common()
{
	_gst_dpman_initialize ();
	_gst_unitconv_initialize ();	
}

void
gst_control_init (int *argc, char **argv[]) {
	gst_control_init_common();
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
	gst_control_init_common();
	gst_plugin_set_longname (plugin, "Dynamic Parameters");
	return TRUE;
}

GstPluginDesc plugin_desc = {
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"gstcontrol",
	plugin_init
};
