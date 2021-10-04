/* GStreamer
 * Copyright (C) 2007-2008 Wouter Cloetens <wouter@mind.be>
 * Copyright (C) 2021 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include "gstsoupelements.h"
#include "gstsouploader.h"

GST_DEBUG_CATEGORY (gst_soup_debug);

#define GST_CAT_DEFAULT gst_soup_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_soup_debug, "soup", 0, "soup");

  if (!gst_soup_load_library ()) {
    GST_WARNING ("Failed to load libsoup library");
    return TRUE;
  }

  ret |= GST_ELEMENT_REGISTER (souphttpsrc, plugin);
  ret |= GST_ELEMENT_REGISTER (souphttpclientsink, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    soup,
    "libsoup HTTP client src/sink",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
