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
#include "vtenc.h"
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
  gboolean res = FALSE;

  gst_apple_core_video_memory_init ();

#if TARGET_OS_IOS || TARGET_OS_TV
  gst_ios_gl_memory_init ();
#endif

#if TARGET_OS_IOS
  res |= GST_ELEMENT_REGISTER (iosassetsrc, plugin);
#endif

#if TARGET_OS_OSX
  enable_mt_mode ();
#endif

#ifdef HAVE_AVFOUNDATION
  res |= GST_ELEMENT_REGISTER (avfassetsrc, plugin);
  res |= GST_ELEMENT_REGISTER (avsamplebufferlayersink, plugin);
#if HAVE_AVCAPTUREDEVICE
  res |= GST_ELEMENT_REGISTER (avfvideosrc, plugin);
  res |= GST_DEVICE_PROVIDER_REGISTER (avfdeviceprovider, plugin);
#endif
#endif

#ifdef HAVE_VIDEOTOOLBOX
  /* Check if the framework actually exists at runtime */
  if (&VTCompressionSessionCreate != NULL) {
    res |= gst_vtdec_register_elements (plugin);
    res |= gst_vtenc_register_elements (plugin);
  }
#endif

  return res;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    applemedia,
    "Elements for capture and codec access on Apple macOS and iOS",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
