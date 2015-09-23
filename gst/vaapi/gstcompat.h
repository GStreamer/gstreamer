/*
 *  gstcompat.h - Compatibility glue for GStreamer
 *
 *  Copyright (C) 2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#ifndef GST_COMPAT_H
#define GST_COMPAT_H

#include "gst/vaapi/sysdeps.h"

#if !GST_CHECK_VERSION (1,5,0)
static inline GstBuffer *
gst_buffer_copy_deep (const GstBuffer * buffer)
{
  GstBuffer *copy;

  g_return_val_if_fail (buffer != NULL, NULL);

  copy = gst_buffer_new ();

  if (!gst_buffer_copy_into (copy, (GstBuffer *) buffer,
      GST_BUFFER_COPY_ALL | GST_BUFFER_COPY_DEEP, 0, -1))
    gst_buffer_replace (&copy, NULL);

#if GST_CHECK_VERSION (1,4,0)
  if (copy)
    GST_BUFFER_FLAG_UNSET (copy, GST_BUFFER_FLAG_TAG_MEMORY);
#endif

  return copy;
}
#endif

#endif /* GST_COMPAT_H */
