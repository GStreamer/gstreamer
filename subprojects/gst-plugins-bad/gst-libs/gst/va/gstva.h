/* GStreamer
 *  Copyright (C) 2021 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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

#pragma once

#ifndef GST_USE_UNSTABLE_API
#pragma message ("The va library from gst-plugins-bad is unstable API and may change in future.")
#pragma message ("You can define GST_USE_UNSTABLE_API to avoid this warning.")
#endif

typedef enum
{
  GST_VA_FEATURE_DISABLED,
  GST_VA_FEATURE_ENABLED,
  GST_VA_FEATURE_AUTO,
} GstVaFeature;

#include <gst/va/va-prelude.h>
#include <gst/va/va-enumtypes.h>
#include <gst/va/gstvadisplay.h>
#include <gst/va/gstvadisplay_drm.h>
#include <gst/va/gstvadisplay_wrapped.h>

#include <gst/va/gstvaallocator.h>
#include <gst/va/gstvapool.h>

#include <gst/va/gstvautils.h>
#include <gst/va/gstvavideoformat.h>
