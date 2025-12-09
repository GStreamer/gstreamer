/*
 * GStreamer gstreamer-ssdtensordec
 * Copyright (C) 2021,2025 Collabora Ltd
 *
 * gstssdtensordec.h
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

#ifndef __GST_SSD_TENSOR_DEC_H__
#define __GST_SSD_TENSOR_DEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_SSD_TENSOR_DEC            (gst_ssd_tensor_dec_get_type())
G_DECLARE_FINAL_TYPE (GstSsdTensorDec, gst_ssd_tensor_dec, GST, SSD_TENSOR_DEC, GstBaseTransform)

#define GST_SSD_TENSOR_DEC_META_NAME "ssd-tensor-dec"
#define GST_SSD_TENSOR_DEC_META_PARAM_NAME "extra-data"
#define GST_SSD_TENSOR_DEC_META_FIELD_LABEL "label"
#define GST_SSD_TENSOR_DEC_META_FIELD_SCORE "score"

/*
 * GstSsdTensorDec:
 *
 * @label_file label file
 * @score_threshold score threshold
 *
 * Since: 1.20
 */
struct _GstSsdTensorDec
{
  GstBaseTransform basetransform;
  gchar *label_file;
  GArray *labels;
  gfloat score_threshold;
  gfloat size_threshold;
  GstVideoInfo video_info;
};

/**
 * GstSsdTensorDecClass:
 *
 * @parent_class base transform base class
 *
 * Since: 1.20
 */
struct _GstSsdTensorDecClass
{
  GstBaseTransformClass parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (ssd_tensor_dec)

G_END_DECLS

#endif /* __GST_SSD_TENSOR_DEC_H__ */
