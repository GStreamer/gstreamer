/*
 * GStreamer gstreamer-executorchinference
 * Copyright (C) 2025 Collabora Ltd
 *
 * gstexecutorchinference.h
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

#ifndef __GST_EXECUTORCH_INFERENCE_H__
#define __GST_EXECUTORCH_INFERENCE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <gst/base/gstbasetransform.h>
#include <gst/analytics/analytics.h>

G_BEGIN_DECLS
#define GST_TYPE_EXECUTORCH_INFERENCE (gst_executorch_inference_get_type())
G_DECLARE_FINAL_TYPE (GstExecuTorchInference,
    gst_executorch_inference, GST, EXECUTORCH_INFERENCE, GstBaseTransform)
GST_ELEMENT_REGISTER_DECLARE (executorch_inference)
    G_END_DECLS
#endif                          /* __GST_EXECUTORCH_INFERENCE_H__ */
