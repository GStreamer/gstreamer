/*
 * GStreamer gstreamer-tflitexnnpackinference
 * Copyright (C) 2026 Collabora Ltd
 *
 * gsttflitexnnpaxckinference.h
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

#ifndef __GST_TFLITE_XNNPACK_INFERENCE_H__
#define __GST_TFLITE_XNNPACK_INFERENCE_H__

#include "gsttfliteinference.h"


G_BEGIN_DECLS

#define GST_TYPE_TFLITE_XNNPACK_INFERENCE    (gst_tflite_xnnpack_inference_get_type())
G_DECLARE_FINAL_TYPE (GstTFliteXnnpackInference, gst_tflite_xnnpack_inference, GST,
    TFLITE_XNNPACK_INFERENCE, GstTFliteInference)

GST_ELEMENT_REGISTER_DECLARE (tflite_xnnpack_inference)

G_END_DECLS

#endif
