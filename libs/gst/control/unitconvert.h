/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * unitconvert.c: Conversion between units of measurement
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

#ifndef __GST_UNITCONVERT_H__
#define __GST_UNITCONVERT_H__

#include <gst/gstobject.h>

G_BEGIN_DECLS
#define GST_TYPE_UNIT_CONVERT			(gst_unitconv_get_type ())
#define GST_UNIT_CONVERT(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_UNIT_CONVERT,GstUnitConvert))
#define GST_UNIT_CONVERT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_UNIT_CONVERT,GstUnitConvert))
#define GST_IS_UNIT_CONVERT(obj)			(G_TYPE_CHECK_INSTANCE_TYPE	((obj), GST_TYPE_UNIT_CONVERT))
#define GST_IS_UNIT_CONVERT_CLASS(obj)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_UNIT_CONVERT))
#define GST_UNIT_CONVERT_NAME(unitconv)				(GST_OBJECT_NAME(unitconv))
#define GST_UNIT_CONVERT_PARENT(unitconv)			(GST_OBJECT_PARENT(unitconv))
typedef struct _GstUnitConvertClass GstUnitConvertClass;
typedef struct _GstUnitConvert GstUnitConvert;

typedef void (*GstUnitConvertFunc) (GstUnitConvert * unitconv,
    GValue * from_val, GValue * to_val);

struct _GstUnitConvert
{
  GstObject object;

  GHashTable *convert_params;
  GSList *convert_func_chain;
  GSList *temp_gvalues;
};

struct _GstUnitConvertClass
{
  GstObjectClass parent_class;

  /* signal callbacks */
};

GType gst_unitconv_get_type (void);

GstUnitConvert *gst_unitconv_new (void);
void _gst_unitconv_initialize (void);

gboolean gst_unitconv_set_convert_units (GstUnitConvert * unitconv,
    gchar * from_unit_named, gchar * to_unit_named);
gboolean gst_unitconv_convert_value (GstUnitConvert * unitconv,
    GValue * from_value, GValue * to_value);

GParamSpec *gst_unitconv_unit_spec (gchar * unit_name);
gboolean gst_unitconv_unit_exists (gchar * unit_name);
gboolean gst_unitconv_unit_is_logarithmic (gchar * unit_name);

gboolean gst_unitconv_register_unit (const gchar * domain_name,
    gboolean is_domain_default,
    gboolean is_logarithmic, GParamSpec * unit_spec);

gboolean gst_unitconv_register_convert_func (gchar * from_unit_named,
    gchar * to_unit_named, GstUnitConvertFunc convert_func);
gboolean gst_unitconv_register_convert_property (gchar * unit_name,
    GParamSpec * convert_prop_spec);

G_END_DECLS
#endif /* __GST_UNITCONVERT_H__ */
