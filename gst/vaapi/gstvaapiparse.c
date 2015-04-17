/*
 *  gstvaapiparse.c - Recent enough GStreamer video parsers
 *
 *  Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 *  Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstcompat.h"
#include <gst/gst.h>
#include "gstvaapiparse.h"
#include "gsth264parse.h"

#if GST_CHECK_VERSION(1,4,0)
#include "gsth265parse.h"
#endif

#define PLUGIN_NAME     "vaapiparse"
#define PLUGIN_DESC     "VA-API based elements"
#define PLUGIN_LICENSE  "LGPL"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean failure = FALSE;

  failure |= !gst_element_register (plugin, "vaapiparse_h264",
      GST_RANK_PRIMARY + 2, GST_TYPE_H264_PARSE);

#if GST_CHECK_VERSION(1,4,0)
  failure |= !gst_element_register (plugin, "vaapiparse_h265",
      GST_RANK_PRIMARY + 2, GST_TYPE_H265_PARSE);
#endif

  return !failure;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    vaapiparse, PLUGIN_DESC, plugin_init,
    PACKAGE_VERSION, PLUGIN_LICENSE, PACKAGE, PACKAGE_BUGREPORT)
