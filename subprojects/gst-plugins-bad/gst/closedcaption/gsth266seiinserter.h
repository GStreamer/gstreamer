/* GStreamer
  * Copyright (C) 2026 Fluendo S.A.
 *   Author: Diego Nieto <dnieto@fluendo.com>
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
#include "gsth266reorder.h"

G_BEGIN_DECLS

#define GST_TYPE_H266_SEI_INSERTER             (gst_h266_sei_inserter_get_type())
#define GST_H266_SEI_INSERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H266_SEI_INSERTER,GstH266SEIInserter))
#define GST_H266_SEI_INSERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H266_SEI_INSERTER,GstH266SEIInserterClass))
#define GST_IS_H266_SEI_INSERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H266_SEI_INSERTER))
#define GST_IS_H266_SEI_INSERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H266_SEI_INSERTER))
#define GST_H266_SEI_INSERTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_H266_SEI_INSERTER,GstH266SEIInserterClass))

typedef struct _GstH266SEIInserter GstH266SEIInserter;
typedef struct _GstH266SEIInserterClass GstH266SEIInserterClass;

struct _GstH266SEIInserter
{
  GstCodecSEIInserter parent;

  GstH266Reorder *reorder;
  GArray *sei_array;
};

struct _GstH266SEIInserterClass
{
  GstCodecSEIInserterClass parent_class;
};

GType gst_h266_sei_inserter_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (h266seiinserter);

G_END_DECLS
