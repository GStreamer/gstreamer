/* GStreamer
 * Copyright (C) 2021, Collabora Ltd.
 *   @author: Jakub Adam <jakub.adam@collabora.com>
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

#ifndef __GST_VPX_ENUM_H__
#define __GST_VPX_ENUM_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstVPXAQ:
 *
 * VPX Adaptive Quantization modes.
 *
 * Since: 1.20
 */
typedef enum
{
  GST_VPX_AQ_OFF = 0,
  GST_VPX_AQ_VARIANCE = 1,
  GST_VPX_AQ_COMPLEXITY = 2,
  GST_VPX_AQ_CYCLIC_REFRESH = 3,
  GST_VPX_AQ_EQUATOR360 = 4,
  GST_VPX_AQ_PERCEPTUAL = 5,
  GST_VPX_AQ_PSNR = 6,
  GST_VPX_AQ_LOOKAHEAD = 7,
} GstVPXAQ;

G_END_DECLS

#endif // __GST_VPX_ENUM_H__
