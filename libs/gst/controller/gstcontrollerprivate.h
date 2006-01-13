/* GStreamer
 *
 * Copyright (C) <2005> Stefan Kost <ensonic at users dot sf dot net>
 *
 * gstcontrollerprivate.h: dynamic parameter control subsystem
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

#ifndef __GST_CONTROLLER_PRIVATE_H__
#define __GST_CONTROLLER_PRIVATE_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>

#include "gstcontroller.h"

G_BEGIN_DECLS

struct _GstControlledProperty;

typedef GValue *(*InterpolateGet) (struct _GstControlledProperty * prop,
        GstClockTime timestamp);
typedef gboolean (*InterpolateGetValueArray) (struct _GstControlledProperty * prop,
        GstClockTime timestamp, GstValueArray * value_array);

/**
 * GstInterpolateMethod:
 *
 * Function pointer structure to do user-defined interpolation methods
 */
typedef struct _GstInterpolateMethod
{
  InterpolateGet get_int;
  InterpolateGetValueArray get_int_value_array;
  InterpolateGet get_uint;
  InterpolateGetValueArray get_uint_value_array;
  InterpolateGet get_long;
  InterpolateGetValueArray get_long_value_array;
  InterpolateGet get_ulong;
  InterpolateGetValueArray get_ulong_value_array;
  InterpolateGet get_float;
  InterpolateGetValueArray get_float_value_array;
  InterpolateGet get_double;
  InterpolateGetValueArray get_double_value_array;
  InterpolateGet get_boolean;
  InterpolateGetValueArray get_boolean_value_array;
  InterpolateGet get_enum;
  InterpolateGetValueArray get_enum_value_array;
  InterpolateGet get_string;
  InterpolateGetValueArray get_string_value_array;
} GstInterpolateMethod;

/**
 * GstControlledProperty:
 */
typedef struct _GstControlledProperty
{
  gchar *name;                  /* name of the property */
  GType type;                   /* type of the handled property */
  GType base;                   /* base-type of the handled property */
  GValue default_value;         /* default value for the handled property */
  GValue result_value;          /* result value location for the interpolation method */
  GstTimedValue last_value;     /* the last value a _sink call wrote */
  GstTimedValue live_value;     /* temporary value override for live input */
  gulong notify_handler_id;     /* id of the notify::<name> signal handler */
  GstInterpolateMode interpolation;     /* Interpolation mode */
  /* TODO instead of *method, have pointers to get() and get_value_array() here
     gst_controller_set_interpolation_mode() will pick the right ones for the
     properties value type
     GstInterpolateMethod *method; // User-implemented handler (if interpolation == GST_INTERPOLATE_USER)
  */
  InterpolateGet get;
  InterpolateGetValueArray get_value_array;

  GList *values;                /* List of GstTimedValue */
  /* TODO keep the last search result to be able to continue
     GList      *last_value;     // last search result, can be used for incremental searches
   */

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING];
} GstControlledProperty;

#define GST_CONTROLLED_PROPERTY(obj)    ((GstControlledProperty *)(obj))

G_END_DECLS

#endif /* __GST_CONTROLLER_PRIVATE_H__ */
