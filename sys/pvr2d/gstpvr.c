/*
 * GStreamer
 * Copyright (c) 2010, Texas Instruments Incorporated
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstpvr.h"
#include "gstpvrvideosink.h"

GST_DEBUG_CATEGORY (gst_debug_pvrvideosink);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_debug_pvrvideosink, "pvrvideosink", 0,
      "pvrvideosink");

  return gst_element_register (plugin, "pvrvideosink", GST_RANK_PRIMARY,
      GST_TYPE_PVRVIDEOSINK);
}

void *
gst_ducati_alloc_1d (gint sz)
{
  MemAllocBlock block = {
    .pixelFormat = PIXEL_FMT_PAGE,
    .dim.len = sz,
  };
  return MemMgr_Alloc (&block, 1);
}

void *
gst_ducati_alloc_2d (gint width, gint height, guint * sz)
{
  MemAllocBlock block[] = { {
          .pixelFormat = PIXEL_FMT_8BIT,
          .dim = {.area = {
                      .width = width,
                      .height = ALIGN2 (height, 1),
                  }},
      .stride = 4096}, {
        .pixelFormat = PIXEL_FMT_16BIT,
        .dim = {.area = {
                    .width = width,
                    .height = ALIGN2 (height, 1) / 2,
                }},
      .stride = 4096}
  };
  if (sz) {
    *sz = (4096 * ALIGN2 (height, 1) * 3) / 2;
  }
  return MemMgr_Alloc (block, 2);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#  define PACKAGE "ducati"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "pvr",
    "Pvr2d based plugin",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
