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
#include "gstvaapidecode.h"
#include "gstvaapipostproc.h"
#include "gstvaapisink.h"
#include "gstvaapidecodebin.h"

#if USE_ENCODERS
#include "gstvaapiencode_h264.h"
#include "gstvaapiencode_mpeg2.h"

#if USE_JPEG_ENCODER
#include "gstvaapiencode_jpeg.h"
#endif

#if USE_VP8_ENCODER
#include "gstvaapiencode_vp8.h"
#endif

#if USE_H265_ENCODER
#include "gstvaapiencode_h265.h"
#endif

#if USE_VP9_ENCODER
#include "gstvaapiencode_vp9.h"
#endif
#endif

#define PLUGIN_NAME     "vaapi"
#define PLUGIN_DESC     "VA-API based elements"
#define PLUGIN_LICENSE  "LGPL"

static void
plugin_add_dependencies (GstPlugin * plugin)
{
  const gchar *envvars[] = { "GST_VAAPI_ALL_DRIVERS", "LIBVA_DRIVER_NAME",
    NULL
  };
  const gchar *kernel_paths[] = { "/dev/dri", NULL };
  const gchar *kernel_names[] = { "card", "render" };

  /* features get updated upon changes in /dev/dri/card* */
  gst_plugin_add_dependency (plugin, NULL, kernel_paths, kernel_names,
      GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_PREFIX);

  /* features get updated upon changes in VA environment variables */
  gst_plugin_add_dependency (plugin, envvars, NULL, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  /* features get updated upon changes in default VA drivers
   * directory */
  gst_plugin_add_dependency_simple (plugin, "LIBVA_DRIVERS_PATH",
      VA_DRIVERS_PATH, "_drv_video.so",
      GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_SUFFIX |
      GST_PLUGIN_DEPENDENCY_FLAG_PATHS_ARE_DEFAULT_ONLY);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstVaapiDisplay *display;

  plugin_add_dependencies (plugin);

  display = gst_vaapi_create_test_display ();
  if (!display)
    goto error_no_display;

  gst_vaapidecode_register (plugin);

  gst_element_register (plugin, "vaapipostproc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIPOSTPROC);
  gst_element_register (plugin, "vaapisink",
      GST_RANK_PRIMARY, GST_TYPE_VAAPISINK);
#if USE_ENCODERS
  gst_element_register (plugin, "vaapih264enc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_H264);
  gst_element_register (plugin, "vaapimpeg2enc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_MPEG2);
#if USE_JPEG_ENCODER
  gst_element_register (plugin, "vaapijpegenc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_JPEG);
#endif
#if USE_VP8_ENCODER
  gst_element_register (plugin, "vaapivp8enc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_VP8);
#endif
#if USE_H265_ENCODER
  gst_element_register (plugin, "vaapih265enc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_H265);
#endif
#if USE_VP9_ENCODER
  gst_element_register (plugin, "vaapivp9enc",
      GST_RANK_PRIMARY, GST_TYPE_VAAPIENCODE_VP9);
#endif
#endif

  gst_element_register (plugin, "vaapidecodebin",
      GST_RANK_PRIMARY + 2, GST_TYPE_VAAPI_DECODE_BIN);

  gst_vaapi_display_unref (display);

  return TRUE;

  /* ERRORS: */
error_no_display:
  {
    GST_ERROR ("Cannot create a VA display");
    /* Avoid blacklisting: failure to create a display could be a
     * transient condition */
    return TRUE;
  }
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    vaapi, PLUGIN_DESC, plugin_init,
    PACKAGE_VERSION, PLUGIN_LICENSE, PACKAGE, PACKAGE_BUGREPORT)
