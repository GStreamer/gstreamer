/* G-Streamer Video4linux2 video-capture plugin
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <gst/gst.h>

#include "gst/gst-i18n-plugin.h"

#include "gstv4l2element.h"
#include "gstv4l2src.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* actually, we can survive without it, but I'll create
   * that handling later on. */
  if (!gst_library_load ("xwindowlistener"))
    return FALSE;

  if (!gst_element_register (plugin, "v4l2element",
	  GST_RANK_NONE, GST_TYPE_V4L2ELEMENT) ||
      !gst_element_register (plugin, "v4l2src",
	  GST_RANK_NONE, GST_TYPE_V4L2SRC))
    return FALSE;

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "video4linux2",
    "elements for Video 4 Linux 2",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
