/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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
#include "esdsink.h"
#if 0
#include "esdmon.h"
#endif

#include "gst/gst-i18n-plugin.h"

GST_DEBUG_CATEGORY (esd_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "esdsink", GST_RANK_MARGINAL,
          GST_TYPE_ESDSINK))
    return FALSE;

#if 0
  if (!gst_element_register (plugin, "esdmon", GST_RANK_NONE, GST_TYPE_ESDMON))
    return FALSE;
#endif

  GST_DEBUG_CATEGORY_INIT (esd_debug, "esd", 0, "ESounD elements");

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "esdsink",
    "ESD Element Plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
