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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include "gstosxaudioelement.h"
#include "gstosxaudiosink.h"
#include "gstosxaudiosrc.h"
extern gchar *__gst_osxaudio_plugin_dir;

GST_DEBUG_CATEGORY (osxaudio_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstaudio"))
    return FALSE;


  if (!gst_element_register (plugin, "osxaudiosink", GST_RANK_PRIMARY,
          GST_TYPE_OSXAUDIOSINK)) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "osxaudiosrc", GST_RANK_PRIMARY,
          GST_TYPE_OSXAUDIOSRC)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (osxaudio_debug, "osx", 0, "OSX audio elements");


  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "osxaudio",
    "OSX (Mac OS X) audio support for GStreamer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
