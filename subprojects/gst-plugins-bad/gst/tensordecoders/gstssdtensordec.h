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

GType gst_ssd_tensor_dec_get_type (void);

#define GST_TYPE_SSD_TENSOR_DEC            (gst_ssd_tensor_dec_get_type())
#define GST_SSD_TENSOR_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SSD_TENSOR_DEC, GstSsdTensorDec))
#define GST_SSD_TENSOR_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SSD_TENSOR_DEC, GstSsdTensorDecClass))
#define GST_IS_SSD_TENSOR_DEC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SSD_TENSOR_DEC))
#define GST_IS_SSD_TENSOR_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SSD_TENSOR_DEC))
#define GST_SSD_TENSOR_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SSD_TENSOR_DEC, GstSsdTensorDecClass))

typedef struct _GstSsdTensorDec GstSsdTensorDec;
typedef struct _GstSsdTensorDecClass GstSsdTensorDecClass;


#define GST_SSD_TENSOR_DEC_META_NAME "ssd-tensor-dec"
#define GST_SSD_TENSOR_DEC_META_PARAM_NAME "extra-data"
#define GST_SSD_TENSOR_DEC_META_FIELD_LABEL "label"
#define GST_SSD_TENSOR_DEC_META_FIELD_SCORE "score"

/**
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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstSsdTensorDec, g_object_unref)

#define GST_TYPE_SSD_OBJECT_DETECTOR   (gst_ssd_object_detector_get_type())
G_DECLARE_FINAL_TYPE (GstSsdObjectDetector, gst_ssd_object_detector, GST, SSD_OBJECT_DETECTOR, GstSsdTensorDec)

/**
 * GstSsdObjectDetector:
 *
 * Since: 1.20
 * Deprecated: 1.28 : Use GstSsdTensorDec instead.
 */
struct _GstSsdObjectDetector
{
  GstSsdTensorDec parent;
};

/**
 * GstSsdObjectDetectorClass:
 *
 * @parent_class base transform base class
 *
 * Since: 1.20
 * Deprecated: 1.28 : Use GstSsdTensorDecClass instead.
 */
struct _GstSsdObjectDetectorClass
{
  GstSsdTensorDecClass parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (ssd_object_detector)

G_END_DECLS

#endif /* __GST_SSD_TENSOR_DEC_H__ */
