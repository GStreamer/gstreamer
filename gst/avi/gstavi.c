/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
 *
 * gstavi.c: plugin registering
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

#include "gst/gst-i18n-plugin.h"

#include "gstavidemux.h"
#include "gstavimux.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("riff"))
    return FALSE;

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  return (gst_element_register (plugin, "avidemux",
          GST_RANK_PRIMARY,
          GST_TYPE_AVI_DEMUX) &&
      gst_element_register (plugin, "avimux", GST_RANK_NONE, GST_TYPE_AVIMUX));
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "avi",
    "AVI stream handling",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
