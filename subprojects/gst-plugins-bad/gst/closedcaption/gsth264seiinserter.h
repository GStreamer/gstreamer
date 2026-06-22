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

#include "gstcodecseiinserter.h"
#include "gsth264reorder.h"

G_BEGIN_DECLS

#define GST_TYPE_H264_BASE_SEI_INSERTER             (gst_h264_base_sei_inserter_get_type())
#define GST_H264_BASE_SEI_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_BASE_SEI_INSERTER,GstH264BaseSEIInserter))
#define GST_H264_BASE_SEI_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_BASE_SEI_INSERTER,GstH264BaseSEIInserterClass))
#define GST_IS_H264_BASE_SEI_INSERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_BASE_SEI_INSERTER))
#define GST_IS_H264_BASE_SEI_INSERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_BASE_SEI_INSERTER))
#define GST_H264_BASE_SEI_INSERTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H264_BASE_SEI_INSERTER,GstH264BaseSEIInserterClass))

typedef struct _GstH264BaseSEIInserter GstH264BaseSEIInserter;
typedef struct _GstH264BaseSEIInserterClass GstH264BaseSEIInserterClass;

struct _GstH264BaseSEIInserter
{
  GstCodecSEIInserter parent;

  GstH264Reorder *reorder;
  GArray *sei_array;
};

struct _GstH264BaseSEIInserterClass
{
  GstCodecSEIInserterClass parent_class;
};

GType gst_h264_base_sei_inserter_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstH264BaseSEIInserter, gst_object_unref)

#define GST_TYPE_H264_CC_INSERTER (gst_h264_cc_inserter_get_type())
G_DECLARE_FINAL_TYPE (GstH264CCInserter, gst_h264_cc_inserter,
    GST, H264_CC_INSERTER, GstH264BaseSEIInserter)
GST_ELEMENT_REGISTER_DECLARE (h264ccinserter);

#define GST_TYPE_H264_SEI_INSERTER (gst_h264_sei_inserter_get_type())
G_DECLARE_FINAL_TYPE (GstH264SEIInserter, gst_h264_sei_inserter,
    GST, H264_SEI_INSERTER, GstH264BaseSEIInserter)
GST_ELEMENT_REGISTER_DECLARE (h264seiinserter);

#define GST_TYPE_H264_TIMESTAMPER (gst_h264_timestamper_get_type())
G_DECLARE_FINAL_TYPE (GstH264Timestamper, gst_h264_timestamper,
    GST, H264_TIMESTAMPER, GstH264BaseSEIInserter)
GST_ELEMENT_REGISTER_DECLARE (h264timestamper);

G_END_DECLS
