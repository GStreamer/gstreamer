/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_VALUE_H__
#define __GST_VALUE_H__

#include <gst/gstconfig.h>

G_BEGIN_DECLS

typedef int (* GstValueCompareFunc) (const GValue *value1,
    const GValue *value2);
typedef int (* GstValueUnionFunc) (GValue *dest, const GValue *value1,
    const GValue *value2);
typedef int (* GstValueIntersectFunc) (GValue *dest, const GValue *value1,
    const GValue *value2);

#define GST_VALUE_HOLDS_FOURCC(x) TRUE

#define GST_TYPE_FOURCC gst_type_fourcc
#define GST_TYPE_INT_RANGE gst_type_int_range
#define GST_TYPE_DOUBLE_RANGE gst_type_double_range

extern GType gst_type_fourcc;
extern GType gst_type_int_range;
extern GType gst_type_double_range;

void gst_value_set_fourcc (GValue *value, guint32 fourcc);
guint32 gst_value_get_fourcc (const GValue *value);

void gst_value_set_int_range (GValue *value, int start, int end);
int gst_value_get_int_range_min (const GValue *value);
int gst_value_get_int_range_max (const GValue *value);

void gst_value_set_double_range (GValue *value, double start, double end);
double gst_value_get_double_range_start (const GValue *value);
double gst_value_get_double_range_end (const GValue *value);

void _gst_value_initialize (void);

G_END_DECLS

#endif


