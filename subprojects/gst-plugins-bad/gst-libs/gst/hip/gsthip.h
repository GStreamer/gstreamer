/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
#pragma message ("The hip library from gst-plugins-bad is unstable API and may change in future.")
#pragma message ("You can define GST_USE_UNSTABLE_API to avoid this warning.")
#endif

#include <hip/hip_runtime.h>

#include <gst/gst.h>
#include <gst/hip/hip-gst.h>
#include <gst/hip/hip-prelude.h>
#include <gst/hip/gsthip_fwd.h>
#include <gst/hip/gsthip-enums.h>
#include <gst/hip/gsthip-interop.h>
#include <gst/hip/gsthipbufferpool.h>
#include <gst/hip/gsthipdevice.h>
#include <gst/hip/gsthipevent.h>
#include <gst/hip/gsthiploader.h>
#include <gst/hip/gsthipmemory.h>
#include <gst/hip/gsthiprtc.h>
#include <gst/hip/gsthipstream.h>
#include <gst/hip/gsthiputils.h>


