/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpropsprivate.h: Private header for properties subsystem
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


#ifndef __GST_PROPS_PRIV_H__
#define __GST_PROPS_PRIV_H__

#include <gst/gstprops.h>

typedef struct _GstPropsEntry GstPropsEntry;

struct _GstPropsEntry {
  GQuark    propid;
  GstPropsId propstype;		

  union {
    /* flat values */
    gboolean bool_data;
    guint32  fourcc_data;
    gint     int_data;
    gfloat   float_data;

    /* structured values */
    struct {
      GList *entries;
    } list_data;
    struct {
      gchar *string;
    } string_data;
    struct {
      gint min;
      gint max;
    } int_range_data;
    struct {
      gfloat min;
      gfloat max;
    } float_range_data;
  } data;
};

#endif /* __GST_PROPS_PRIV_H__ */
