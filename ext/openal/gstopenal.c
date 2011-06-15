/*
 * GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2009-2010 Chris Robinson <chris.kcat@gmail.com>
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
#include <gst/gst-i18n-plugin.h>

#include "gstopenalsink.h"
#include "gstopenalsrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "openalsink", GST_RANK_SECONDARY,
          GST_TYPE_OPENAL_SINK) ||
      !gst_element_register (plugin, "openalsrc", GST_RANK_SECONDARY,
          GST_TYPE_OPENAL_SRC))
    return FALSE;

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "openal",
    "OpenAL support for GStreamer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
