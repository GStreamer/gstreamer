/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_CAPS_PRIV_H__
#define __GST_CAPS_PRIV_H__

#include <gst/gstcaps.h>

typedef struct _GstCapsEntry GstCapsEntry;

struct _GstCapsEntry {
  GQuark    propid;
  GstCapsId capstype;		

  union {
    /* flat values */
    gboolean bool_data;
    guint32  fourcc_data;
    gint     int_data;

    /* structured values */
    struct {
      GList *entries;
    } list_data;
    struct {
      gint min;
      gint max;
    } int_range_data;
  } data;
};

#endif /* __GST_CAPS_PRIV_H__ */
