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
#include <gst/gstcaps.h>

G_BEGIN_DECLS

typedef int (* GstValueCompareFunc) (const GValue *value1,
    const GValue *value2);
typedef char * (* GstValueSerializeFunc) (const GValue *value1);
typedef gboolean (* GstValueDeserializeFunc) (GValue *dest, const char *s);
typedef int (* GstValueUnionFunc) (GValue *dest, const GValue *value1,
    const GValue *value2);
typedef int (* GstValueIntersectFunc) (GValue *dest, const GValue *value1,
    const GValue *value2);

typedef struct _GstValueTable GstValueTable;
struct _GstValueTable {
  GType type;
  GstValueCompareFunc compare;
  GstValueSerializeFunc serialize;
  GstValueDeserializeFunc unserialize;

  void *_gst_paddding [GST_PADDING];
};


#define GST_MAKE_FOURCC(a,b,c,d)        (guint32)((a)|(b)<<8|(c)<<16|(d)<<24)
#define GST_STR_FOURCC(f)               (guint32)(((f)[0])|((f)[1]<<8)|((f)[2]<<16)|((f)[3]<<24))

#define GST_FOURCC_FORMAT "%c%c%c%c"
#define GST_FOURCC_ARGS(fourcc) \
        ((gchar) ((fourcc)     &0xff)), \
        ((gchar) (((fourcc)>>8 )&0xff)), \
        ((gchar) (((fourcc)>>16)&0xff)), \
        ((gchar) (((fourcc)>>24)&0xff))

#define GST_VALUE_HOLDS_FOURCC(x)       (G_VALUE_HOLDS(x, gst_type_fourcc))
#define GST_VALUE_HOLDS_INT_RANGE(x)    (G_VALUE_HOLDS(x, gst_type_int_range))
#define GST_VALUE_HOLDS_DOUBLE_RANGE(x) (G_VALUE_HOLDS(x, gst_type_double_range))
#define GST_VALUE_HOLDS_LIST(x)         (G_VALUE_HOLDS(x, gst_type_list))
#define GST_VALUE_HOLDS_CAPS(x)         TRUE /* FIXME */

#define GST_TYPE_FOURCC gst_type_fourcc
#define GST_TYPE_INT_RANGE gst_type_int_range
#define GST_TYPE_DOUBLE_RANGE gst_type_double_range
#define GST_TYPE_LIST gst_type_list

#define GST_VALUE_LESS_THAN (-1)
#define GST_VALUE_EQUAL 0
#define GST_VALUE_GREATER_THAN 1
#define GST_VALUE_UNORDERED 2

extern GType gst_type_fourcc;
extern GType gst_type_int_range;
extern GType gst_type_double_range;
extern GType gst_type_list;

/* list */

void gst_value_list_prepend_value (GValue *value, const GValue *prepend_value);
void gst_value_list_append_value (GValue *value, const GValue *append_value);
guint gst_value_list_get_size (const GValue *value);
G_CONST_RETURN GValue *gst_value_list_get_value (const GValue *value, guint index);
void gst_value_list_concat (GValue *dest, const GValue *value1, const GValue *value2);

/* fourcc */

void gst_value_set_fourcc (GValue *value, guint32 fourcc);
guint32 gst_value_get_fourcc (const GValue *value);

/* int range */

void gst_value_set_int_range (GValue *value, int start, int end);
int gst_value_get_int_range_min (const GValue *value);
int gst_value_get_int_range_max (const GValue *value);

/* double range */

void gst_value_set_double_range (GValue *value, double start, double end);
double gst_value_get_double_range_min (const GValue *value);
double gst_value_get_double_range_max (const GValue *value);

/* caps */

G_CONST_RETURN GstCaps *gst_value_get_caps (const GValue *value);
void gst_value_set_caps (GValue *calue, const GstCaps *caps);

/* compare */

gboolean gst_value_can_compare (const GValue *value1, const GValue *value2);
int gst_value_compare (const GValue *value1, const GValue *value2);

/* union */

gboolean gst_value_can_union (const GValue *value1, const GValue *value2);
gboolean gst_value_union (GValue *dest, const GValue *value1, const GValue *value2);
void gst_value_register_union_func (GType type1, GType type2, GstValueUnionFunc func);

/* intersection */

gboolean gst_value_can_intersect (const GValue *value1, const GValue *value2);
gboolean gst_value_intersect (GValue *dest, const GValue *value1, const GValue *value2);
void gst_value_register_intersect_func (GType type1, GType type2, GstValueIntersectFunc func);

/* */

void gst_value_register (const GstValueTable *table);
void gst_value_init_and_copy (GValue *dest, const GValue *src);
void _gst_value_initialize (void);

G_END_DECLS

#endif


