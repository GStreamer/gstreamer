/* GStreamer
 *
 * gsttypefindfunctionsplugin_rs.c: plugin entry point
 *
 * Copyright (C) 2026 Edward Hervey <edward@centricular.com>
 * Copyright (C) 2026 Sebastian Dröge <sebastian@centricular.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * <https://mozilla.org/MPL/2.0/>.
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

/* All typefinders live in the rust crate and register through here.
 *
 * Note: this is a deliberate limitation of the implementation that the
 * plugin does not define individual per-finder registration functions.
 * Static builds still work fine when the plugin is used as a whole;
 * picking individual features would need extra support. */
extern gboolean gst_typefindfunctions_rs_register (GstPlugin * plugin);

GST_DEBUG_CATEGORY_STATIC (type_find_functions_debug);
#define GST_CAT_DEFAULT type_find_functions_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_plugin_set_static_features_flag (plugin);

  GST_DEBUG_CATEGORY_INIT (type_find_functions_debug, "typefindfunctions",
      GST_DEBUG_FG_GREEN | GST_DEBUG_BG_RED, "generic type find functions");

  return gst_typefindfunctions_rs_register (plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    typefindfunctions,
    "default typefind functions",
    plugin_init, VERSION, "MPL-2.0", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
