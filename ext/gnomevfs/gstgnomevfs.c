/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gnomevfs.c: register gnomevfs elements
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

#include "gst/gst-i18n-plugin.h"

#include "gstgnomevfs.h"
#include <gst/gst.h>

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "gnomevfssrc",
          GST_RANK_SECONDARY, gst_gnomevfssrc_get_type ()) ||
      !gst_element_register (plugin, "gnomevfssink",
          GST_RANK_SECONDARY, gst_gnomevfssink_get_type ())) {
    return FALSE;
  }
#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gnomevfs",
    "elements to access the Gnome vfs",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
