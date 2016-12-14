/* GStreamer
 * Copyright (C) <2009> Kapil Agrawal <kapil@mediamagictechnologies.com>
 *
 * gstopencv.cpp: plugin registering
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
#include "config.h"
#endif

#include "gstcvdilate.h"
#include "gstcvequalizehist.h"
#include "gstcverode.h"
#include "gstcvlaplace.h"
#include "gstcvsmooth.h"
#include "gstcvsobel.h"
#include "gstedgedetect.h"
#include "gstfaceblur.h"
#include "gstfacedetect.h"
#include "gstmotioncells.h"
#include "gsttemplatematch.h"
#include "gsttextoverlay.h"
#include "gsthanddetect.h"
#include "gstskindetect.h"
#include "gstretinex.h"
#include "gstsegmentation.h"
#include "gstgrabcut.h"
#include "gstdisparity.h"
#include "gstdewarp.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_cv_dilate_plugin_init (plugin))
    return FALSE;

  if (!gst_cv_equalize_hist_plugin_init (plugin))
    return FALSE;

  if (!gst_cv_erode_plugin_init (plugin))
    return FALSE;

  if (!gst_cv_laplace_plugin_init (plugin))
    return FALSE;

  if (!gst_cv_smooth_plugin_init (plugin))
    return FALSE;

  if (!gst_cv_sobel_plugin_init (plugin))
    return FALSE;

  if (!gst_edge_detect_plugin_init (plugin))
    return FALSE;

  if (!gst_face_blur_plugin_init (plugin))
    return FALSE;

  if (!gst_face_detect_plugin_init (plugin))
    return FALSE;

  if (!gst_motion_cells_plugin_init (plugin))
    return FALSE;

  if (!gst_template_match_plugin_init (plugin))
    return FALSE;

  if (!gst_opencv_text_overlay_plugin_init (plugin))
    return FALSE;

  if (!gst_handdetect_plugin_init (plugin))
    return FALSE;

  if (!gst_skin_detect_plugin_init (plugin))
    return FALSE;

  if (!gst_retinex_plugin_init (plugin))
    return FALSE;

  if (!gst_segmentation_plugin_init (plugin))
    return FALSE;

  if (!gst_grabcut_plugin_init (plugin))
    return FALSE;

  if (!gst_disparity_plugin_init (plugin))
    return FALSE;

  if (!gst_dewarp_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    opencv,
    "GStreamer OpenCV Plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
