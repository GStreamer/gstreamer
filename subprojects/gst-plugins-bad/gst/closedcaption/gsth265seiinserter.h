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
#include "gsth265reorder.h"

G_BEGIN_DECLS

#define GST_TYPE_H265_CC_INSERTER             (gst_h265_cc_inserter_get_type())
#define GST_H265_CC_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H265_CC_INSERTER,GstH265CCInserter))
#define GST_H265_CC_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H265_CC_INSERTER,GstH265CCInserterClass))
#define GST_IS_H265_CC_INSERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H265_CC_INSERTER))
#define GST_IS_H265_CC_INSERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H265_CC_INSERTER))
#define GST_H265_CC_INSERTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H265_CC_INSERTER,GstH265CCInserterClass))

typedef struct _GstH265CCInserter GstH265CCInserter;
typedef struct _GstH265CCInserterClass GstH265CCInserterClass;

struct _GstH265CCInserter
{
  GstCodecSEIInserter parent;

  GstH265Reorder *reorder;
  GArray *sei_array;
};

struct _GstH265CCInserterClass
{
  GstCodecSEIInserterClass parent_class;
};

GType gst_h265_cc_inserter_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (h265ccinserter);

#define GST_TYPE_H265_SEI_INSERTER             (gst_h265_sei_inserter_get_type())
#define GST_H265_SEI_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H265_SEI_INSERTER,GstH265SEIInserter))
#define GST_H265_SEI_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H265_SEI_INSERTER,GstH265SEIInserterClass))
#define GST_IS_H265_SEI_INSERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H265_SEI_INSERTER))
#define GST_IS_H265_SEI_INSERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H265_SEI_INSERTER))
#define GST_H265_SEI_INSERTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H265_SEI_INSERTER,GstH265SEIInserterClass))

typedef struct _GstH265SEIInserter GstH265SEIInserter;
typedef struct _GstH265SEIInserterClass GstH265SEIInserterClass;

struct _GstH265SEIInserter
{
  GstH265CCInserter parent;
};

struct _GstH265SEIInserterClass
{
  GstH265CCInserterClass parent_class;
};

GType gst_h265_sei_inserter_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (h265seiinserter);

G_END_DECLS
