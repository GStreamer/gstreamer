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

#define GST_TYPE_H264_CC_INSERTER             (gst_h264_cc_inserter_get_type())
#define GST_H264_CC_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_CC_INSERTER,GstH264CCInserter))
#define GST_H264_CC_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_CC_INSERTER,GstH264CCInserterClass))
#define GST_IS_H264_CC_INSERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_CC_INSERTER))
#define GST_IS_H264_CC_INSERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_CC_INSERTER))
#define GST_H264_CC_INSERTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H264_CC_INSERTER,GstH264CCInserterClass))

typedef struct _GstH264CCInserter GstH264CCInserter;
typedef struct _GstH264CCInserterClass GstH264CCInserterClass;

struct _GstH264CCInserter
{
  GstCodecSEIInserter parent;

  GstH264Reorder *reorder;
  GArray *sei_array;
};

struct _GstH264CCInserterClass
{
  GstCodecSEIInserterClass parent_class;
};

GType gst_h264_cc_inserter_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (h264ccinserter);

#define GST_TYPE_H264_SEI_INSERTER             (gst_h264_sei_inserter_get_type())
#define GST_H264_SEI_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_SEI_INSERTER,GstH264SEIInserter))
#define GST_H264_SEI_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_SEI_INSERTER,GstH264SEIInserterClass))
#define GST_IS_H264_SEI_INSERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_SEI_INSERTER))
#define GST_IS_H264_SEI_INSERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_SEI_INSERTER))
#define GST_H264_SEI_INSERTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H264_SEI_INSERTER,GstH264SEIInserterClass))

typedef struct _GstH264SEIInserter GstH264SEIInserter;
typedef struct _GstH264SEIInserterClass GstH264SEIInserterClass;

struct _GstH264SEIInserter
{
  GstH264CCInserter parent;
};

struct _GstH264SEIInserterClass
{
  GstH264CCInserterClass parent_class;
};

GType gst_h264_sei_inserter_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (h264seiinserter);

G_END_DECLS
