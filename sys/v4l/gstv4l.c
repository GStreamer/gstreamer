/* G-Streamer video4linux plugins
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <gst/gst.h>

#include "gstv4lelement.h"
#include "gstv4lsrc.h"
#include "gstv4lmjpegsrc.h"
#include "gstv4lmjpegsink.h"

static gboolean
plugin_init (GstPlugin *plugin)
{
  /* actually, we can survive without it, but I'll create
   * that handling later on. */
  if (!gst_library_load ("xwindowlistener"))
    return FALSE;

  if (!gst_element_register (plugin, "v4lelement",
			     GST_RANK_NONE, GST_TYPE_V4LELEMENT) ||
      !gst_element_register (plugin, "v4lsrc",
			     GST_RANK_NONE, GST_TYPE_V4LSRC) ||
      !gst_element_register (plugin, "v4lmjpegsrc",
			     GST_RANK_NONE, GST_TYPE_V4LMJPEGSRC) ||
      !gst_element_register (plugin, "v4lmjpegsink",
			     GST_RANK_NONE, GST_TYPE_V4LMJPEGSINK))
    return FALSE;

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  textdomain (GETTEXT_PACKAGE);
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "video4linux",
  "elements for Video 4 Linux",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
