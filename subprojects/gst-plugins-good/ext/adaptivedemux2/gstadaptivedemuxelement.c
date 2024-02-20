/* GStreamer
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
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
#  include <config.h>
#endif

#include "gstadaptivedemuxelements.h"
#include "../soup/gstsouploader.h"

GST_DEBUG_CATEGORY (adaptivedemux2_debug);
#define GST_CAT_DEFAULT adaptivedemux2_debug

gboolean
adaptivedemux2_base_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;
  if (g_once_init_enter (&res)) {
    GST_DEBUG_CATEGORY_INIT (adaptivedemux2_debug, "adaptivedemux2", 0,
        "adaptivedemux2");
    g_once_init_leave (&res, TRUE);
  }
#ifndef LINK_SOUP
  if (!gst_soup_load_library ()) {
    GST_WARNING ("Failed to load libsoup library");
    return FALSE;
  }
#endif
  return TRUE;
}
