/*
 * GStreamer gstreamer-classifiertensordecoder
 * Copyright (C) 2025 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@dmohub.org>
 *
 * gstclassifiertensordecoder.h
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


#ifndef __GST_CLASSIFIER_TENSOR_DECODER_H__
#define __GST_CLASSIFIER_TENSOR_DECODER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_CLASSIFIER_TENSOR_DECODER (gst_classifier_tensor_decoder_get_type ())
G_DECLARE_FINAL_TYPE (GstClassifierTensorDecoder, gst_classifier_tensor_decoder,
    GST, CLASSIFIER_TENSOR_DECODER, GstBaseTransform)

/**
 * GstClassifierTensorDecoder:
 *
 * @threshold: Class confidence threshold
 * @labels_file: Path where to read class labels
 * @class_quark: Class labels quark representation
 * @softmax_res: Soft-max of output vector
 *
 * Since: 1.24
 */
struct _GstClassifierTensorDecoder
{
  GstBaseTransform basetransform;
  gfloat threshold;
  gchar *labels_file;
  GArray *class_quark;
  GArray *softmax_res;
};

struct _GstClassifierTensorDecoderClass
{
  GstBaseTransformClass parent_class;

  /* TODO: Add vmethod to allow overwriting: decode, postprocess, load_labels */
};

GST_ELEMENT_REGISTER_DECLARE (classifier_tensor_decoder)

G_END_DECLS
#endif /* __GST_CLASSIFIER_TENSOR_DECODER_H__ */
