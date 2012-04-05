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

static struct
{
  PVR2DERROR code;
  const gchar *errstring;
} pvr2derrorcodestring[] = {
  {
  PVR2D_OK, "OK (0)"}, {
  PVR2DERROR_INVALID_PARAMETER, "Invalid Parameter (-1)"}, {
  PVR2DERROR_DEVICE_UNAVAILABLE, "Device Unavailable (-2)"}, {
  PVR2DERROR_INVALID_CONTEXT, "Invalid Context (-3)"}, {
  PVR2DERROR_MEMORY_UNAVAILABLE, "Memory Unavailable (-4)"}, {
  PVR2DERROR_DEVICE_NOT_PRESENT, "Device not present (-5)"}, {
  PVR2DERROR_IOCTL_ERROR, "ioctl Error (-6)"}, {
  PVR2DERROR_GENERIC_ERROR, "Generic Error (-7)"}, {
  PVR2DERROR_BLT_NOTCOMPLETE, "blt not complete (-8)"}, {
  PVR2DERROR_HW_FEATURE_NOT_SUPPORTED, "Hardware feature not supported (-9)"}, {
  PVR2DERROR_NOT_YET_IMPLEMENTED, "Not yet implemented (-10)"}, {
  PVR2DERROR_MAPPING_FAILED, "Mapping failed (-11)"}
};

const gchar *
gst_pvr2d_error_get_string (PVR2DERROR code)
{
  if (code <= PVR2D_OK && code >= PVR2DERROR_MAPPING_FAILED)
    return pvr2derrorcodestring[PVR2D_OK - code].errstring;
  return "Uknown Error";
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
    pvr,
    "Pvr2d based plugin",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
