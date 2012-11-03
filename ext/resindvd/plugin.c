/* GStreamer
 * Copyright (C) 2008 Jan Schmidt <thaytan@noraisin.net>
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
#  include <config.h>
#endif

#include <gst/gst.h>

#include "resindvdbin.h"
#include "gstmpegdemux.h"
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY (resindvd_debug);
#define GST_CAT_DEFAULT resindvd_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean result = TRUE;

  GST_DEBUG_CATEGORY_INIT (resindvd_debug, "resindvd",
      0, "DVD playback elements from resindvd");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

  result &= gst_element_register (plugin, "rsndvdbin",
      GST_RANK_PRIMARY, RESIN_TYPE_DVDBIN);

  result &= gst_flups_demux_plugin_init (plugin);

  return result;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    resindvd,
    "Resin DVD playback elements",
    plugin_init, VERSION, "GPL", "GStreamer", "http://gstreamer.net/")
