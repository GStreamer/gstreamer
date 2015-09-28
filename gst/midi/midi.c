/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* GStreamer MIDI plugin initialisation
 * Copyright (C) <2013> Wim Taymans <wim.taymans@gmail.com>
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

#include <gst/tag/tag.h>

#include <gst/gst-i18n-plugin.h>

#include "midiparse.h"

GST_DEBUG_CATEGORY_STATIC (midi_debug);
#define GST_CAT_DEFAULT (midi_debug)

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  GST_DEBUG_CATEGORY_INIT (midi_debug, "midi", 0, "MIDI plugin");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

  ret = gst_element_register (plugin, "midiparse", GST_RANK_PRIMARY,
      GST_TYPE_MIDI_PARSE);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    midi,
    "Parse MIDI files",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
