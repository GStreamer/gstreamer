/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2002> Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include "video.h"

#define NUM_UNITS 1000000000

/* This is simply a convenience function, nothing more or less */

gdouble
gst_video_frame_rate (GstPad *pad)
{
  GstFormat dest_format = GST_FORMAT_UNITS;
  gint64 dest_value = 0;
  gdouble fps;

  /* do a convert request on the source pad */
  if (!gst_pad_convert(pad,
			GST_FORMAT_TIME, GST_SECOND * NUM_UNITS,
			&dest_format, &dest_value))
  {
    g_warning("gstvideo: pad %s:%s failed to convert time to unit!\n",
		GST_ELEMENT_NAME(gst_pad_get_parent (pad)), GST_PAD_NAME(pad));
    return 0.;
  }

  fps = ((gdouble) dest_value) / NUM_UNITS;

  GST_DEBUG(GST_CAT_ELEMENT_PADS, "Framerate request on pad %s:%s - %f fps",
		GST_ELEMENT_NAME(gst_pad_get_parent (pad)), GST_PAD_NAME(pad), fps);

  return fps;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  gst_plugin_set_longname (plugin, "Convenience routines for video plugins");
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstvideo",
  plugin_init
};
