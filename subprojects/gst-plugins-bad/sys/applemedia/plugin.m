/*
 * Copyright (C) 2009-2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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
# include <config.h>
#endif

#include <TargetConditionals.h>
#include <Foundation/Foundation.h>
#include "corevideomemory.h"

#if TARGET_OS_IOS
#include "iosassetsrc.h"
#endif

#if TARGET_OS_IOS || TARGET_OS_TV
#include "iosglmemory.h"
#endif

#ifdef HAVE_AVFOUNDATION
#include "avfassetsrc.h"
#include "avsamplevideosink.h"
#if HAVE_AVCAPTUREDEVICE
#include "avfvideosrc.h"
#include "avfdeviceprovider.h"
#endif
#endif

#ifdef HAVE_VIDEOTOOLBOX
#include "vtdec.h"
#endif

#ifdef HAVE_VIDEOTOOLBOX
void gst_vtenc_register_elements (GstPlugin * plugin);
#endif

#if TARGET_OS_OSX
static void
enable_mt_mode (void)
{
  NSThread * th = [[NSThread alloc] init];
  [th start];
  g_assert ([NSThread isMultiThreaded]);
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res = TRUE;

  gst_apple_core_video_memory_init ();

#if TARGET_OS_IOS || TARGET_OS_TV
  gst_ios_gl_memory_init ();
#endif

#if TARGET_OS_IOS
  res &= gst_element_register (plugin, "iosassetsrc", GST_RANK_SECONDARY,
      GST_TYPE_IOS_ASSET_SRC);
#endif

#if TARGET_OS_OSX
  enable_mt_mode ();
#endif

#ifdef HAVE_AVFOUNDATION
  res &= gst_element_register (plugin, "avfassetsrc", GST_RANK_PRIMARY,
      GST_TYPE_AVF_ASSET_SRC);
  res &= gst_element_register (plugin, "avsamplebufferlayersink",
      GST_RANK_NONE, GST_TYPE_AV_SAMPLE_VIDEO_SINK);
#if HAVE_AVCAPTUREDEVICE
  res &= gst_element_register (plugin, "avfvideosrc", GST_RANK_PRIMARY,
      GST_TYPE_AVF_VIDEO_SRC);
  res &= gst_device_provider_register (plugin, "avfdeviceprovider",
    GST_RANK_PRIMARY, GST_TYPE_AVF_DEVICE_PROVIDER);
#endif
#endif

#ifdef HAVE_VIDEOTOOLBOX
  /* Check if the framework actually exists at runtime */
  if (&VTCompressionSessionCreate != NULL) {
    gst_vtdec_register_elements (plugin);
    gst_vtenc_register_elements (plugin);
  }
#endif


  return res;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    applemedia,
    "Elements for capture and codec access on Apple macOS and iOS",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
