/* GStreamer
 * Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
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
#  include "config.h"
#endif

#include <gst/gst.h>

GType gst_checksum_sink_get_type (void);
GType fps_display_sink_get_type (void);
GType gst_chop_my_data_get_type (void);
GType gst_compare_get_type (void);
GType gst_debug_spy_get_type (void);
GType gst_error_ignore_get_type (void);
GType gst_watchdog_get_type (void);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "checksumsink", GST_RANK_NONE,
      gst_checksum_sink_get_type ());
  gst_element_register (plugin, "fpsdisplaysink", GST_RANK_NONE,
      fps_display_sink_get_type ());
  gst_element_register (plugin, "chopmydata", GST_RANK_NONE,
      gst_chop_my_data_get_type ());
  gst_element_register (plugin, "compare", GST_RANK_NONE,
      gst_compare_get_type ());
  gst_element_register (plugin, "debugspy", GST_RANK_NONE,
      gst_debug_spy_get_type ());
  gst_element_register (plugin, "watchdog", GST_RANK_NONE,
      gst_watchdog_get_type ());
  gst_element_register (plugin, "errorignore", GST_RANK_NONE,
      gst_error_ignore_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    debugutilsbad,
    "Collection of elements that may or may not be useful for debugging",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
