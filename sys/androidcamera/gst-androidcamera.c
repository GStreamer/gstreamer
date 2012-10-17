/*
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gst-dvm.h"
#include "gst-android-hardware-camera.h"
#include "gstahcsrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_dvm_init ())
    return FALSE;

  if (!gst_android_graphics_surfacetexture_init ())
    return FALSE;

  if (!gst_android_graphics_imageformat_init ()) {
    gst_android_graphics_surfacetexture_deinit ();
    return FALSE;
  }
  if (!gst_android_hardware_camera_init ()) {
    gst_android_graphics_surfacetexture_deinit ();
    gst_android_graphics_imageformat_deinit ();
    return FALSE;
  }

  return gst_element_register (plugin, "ahcsrc", GST_RANK_NONE,
      GST_TYPE_AHC_SRC);
}

#ifdef GST_PLUGIN_DEFINE2
GST_PLUGIN_DEFINE2 (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    androidcamera,
    "Android Camera plugin",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#else
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "androidcamera",
    "Android Camera plugin",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#endif
