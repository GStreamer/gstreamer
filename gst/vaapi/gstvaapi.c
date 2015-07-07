/*
 *  gstvaapi.c - VA-API element registration
 *
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *  Copyright (C) 2011 Collabora Ltd.
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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
#include "gstvaapidecode.h"
#include "gstvaapipostproc.h"
#include "gstvaapisink.h"
#include "gstvaapidecodebin.h"

#if USE_ENCODERS
#include "gstvaapiencode_h264.h"
#include "gstvaapiencode_mpeg2.h"
#endif

#if USE_JPEG_ENCODER
#include "gstvaapiencode_jpeg.h"
#endif

#if USE_VP8_ENCODER
#include "gstvaapiencode_vp8.h"
#endif

#if USE_H265_ENCODER
#include "gstvaapiencode_h265.h"
#endif

#define PLUGIN_NAME     "vaapi"
#define PLUGIN_DESC     "VA-API based elements"
#define PLUGIN_LICENSE  "LGPL"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "vaapidecode",
      GST_RANK_PRIMARY + 1, GST_TYPE_VAAPIDECODE);
  gst_element_register (plugin, "vaapipostproc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIPOSTPROC);
  gst_element_register (plugin, "vaapisink",
      GST_RANK_PRIMARY, GST_TYPE_VAAPISINK);
#if USE_ENCODERS
  gst_element_register (plugin, "vaapiencode_h264",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_H264);
  gst_element_register (plugin, "vaapiencode_mpeg2",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_MPEG2);
#endif
#if USE_JPEG_ENCODER
  gst_element_register (plugin, "vaapiencode_jpeg",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_JPEG);
#endif
#if USE_VP8_ENCODER
  gst_element_register (plugin, "vaapiencode_vp8",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_VP8);
#endif

#if USE_H265_ENCODER
  gst_element_register (plugin, "vaapiencode_h265",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_H265);
#endif

#if GST_CHECK_VERSION(1,4,0)
  gst_element_register (plugin, "vaapidecodebin",
      GST_RANK_MARGINAL, GST_TYPE_VAAPI_DECODE_BIN);
#endif
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    vaapi, PLUGIN_DESC, plugin_init,
    PACKAGE_VERSION, PLUGIN_LICENSE, PACKAGE, PACKAGE_BUGREPORT)
