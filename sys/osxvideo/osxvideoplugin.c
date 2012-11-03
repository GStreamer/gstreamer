/* GStreamer
 * OSX video src
 * Copyright (C) 2004-6 Zaheer Abbas Merali <zaheerabbas at merali dot org>
 * Copyright (C) 2007 Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 *
 * The development of this code was made possible due to the involvement of
 * Pioneers of the Inevitable, the creators of the Songbird Music player.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Object header */
#include "osxvideosrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "osxvideosrc",
          GST_RANK_PRIMARY, GST_TYPE_OSX_VIDEO_SRC))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_osx_video_src, "osxvideosrc", 0,
      "osxvideosrc element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    osxvideosrc,
    "OSX native video input plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
