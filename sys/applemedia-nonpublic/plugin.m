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

#ifdef HAVE_IOS
#include "celvideosrc.h"
#else
#include "miovideosrc.h"
#include <Foundation/Foundation.h>
#endif

#ifndef HAVE_IOS
static void
enable_mt_mode (void)
{
  NSThread * th = [[NSThread alloc] init];
  [th start];
  [th release];
  g_assert ([NSThread isMultiThreaded]);
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res = TRUE;

#ifdef HAVE_IOS
#if 0
  res &= gst_element_register (plugin, "celvideosrc", GST_RANK_NONE,
      GST_TYPE_CEL_VIDEO_SRC);
#endif
#else
  enable_mt_mode ();

#if 0
  res &= gst_element_register (plugin, "miovideosrc", GST_RANK_NONE,
      GST_TYPE_MIO_VIDEO_SRC);
#endif
#endif

  return res;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    applemedia_nonpublic,
    "Elements for capture and codec access on Apple OS X and iOS using private Frameworks",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
