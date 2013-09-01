/*
 * GStreamer - SunAudio plugin
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
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
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include "gstsunaudiomixer.h"
#include "gstsunaudiosink.h"
#include "gstsunaudiosrc.h"

extern gchar *__gst_oss_plugin_dir;

GST_DEBUG_CATEGORY (sunaudio_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "sunaudiomixer", GST_RANK_NONE,
          GST_TYPE_SUNAUDIO_MIXER) ||
      !gst_element_register (plugin, "sunaudiosink", GST_RANK_SECONDARY,
          GST_TYPE_SUNAUDIO_SINK) ||
      !gst_element_register (plugin, "sunaudiosrc", GST_RANK_SECONDARY,
          GST_TYPE_SUNAUDIO_SRC)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (sunaudio_debug, "sunaudio", 0, "sunaudio elements");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    sunaudio,
    "Sun Audio support for GStreamer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
