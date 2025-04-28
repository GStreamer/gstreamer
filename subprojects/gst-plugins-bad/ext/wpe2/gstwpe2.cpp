/* Copyright (C) <2018, 2025> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018> Žan Doberšek <zdobersek@igalia.com>
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
#include <config.h>
#endif

#include "gstwpevideosrc.h"
#include "gstwpe.h"

GST_DEBUG_CATEGORY (wpe_video_src_debug);
GST_DEBUG_CATEGORY (wpe_view_debug);
GST_DEBUG_CATEGORY (wpe_src_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean result;

  GST_DEBUG_CATEGORY_INIT (wpe_video_src_debug, "wpevideosrc2", 0,
      "WPE Video Source");
  GST_DEBUG_CATEGORY_INIT (wpe_view_debug, "wpeview2", 0, "WPE Threaded View");
  GST_DEBUG_CATEGORY_INIT (wpe_src_debug, "wpesrc2", 0, "WPE Source");

  result = gst_element_register (plugin, "wpevideosrc2", GST_RANK_NONE,
      GST_TYPE_WPE_VIDEO_SRC);
  return result;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    wpe2, "WPE src plugin", plugin_init, VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN)
