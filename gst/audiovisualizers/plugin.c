/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * plugin.c: scopes plugin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>

#include "gstspacescope.h"
#include "gstspectrascope.h"
#include "gstsynaescope.h"
#include "gstwavescope.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res = TRUE;

  res &= gst_space_scope_plugin_init (plugin);
  res &= gst_spectra_scope_plugin_init (plugin);
  res &= gst_synae_scope_plugin_init (plugin);
  res &= gst_wave_scope_plugin_init (plugin);
  return res;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    audiovisualizers,
    "Creates video visualizations of audio input",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
