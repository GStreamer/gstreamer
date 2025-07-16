/*
 * GStreamer gstreamer-tflitevsiinference
 * Copyright (C) 2025 Collabora Ltd
 *
 * gsttflitevsiinference.h
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

#ifndef __GST_TFLITE_VSI_INFERENCE_H__
#define __GST_TFLITE_VSI_INFERENCE_H__

#include "gsttfliteinference.h"


G_BEGIN_DECLS

#define GST_TYPE_TFLITE_VSI_INFERENCE    (gst_tflite_vsi_inference_get_type())
G_DECLARE_FINAL_TYPE (GstTFliteVsiInference, gst_tflite_vsi_inference, GST,
    TFLITE_VSI_INFERENCE, GstTFliteInference)

GST_ELEMENT_REGISTER_DECLARE (tflite_vsi_inference)

G_END_DECLS

#endif /* __GST_TFLITE_INFERENCE_H__ */
