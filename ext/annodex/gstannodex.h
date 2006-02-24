/*
 * gstannodex.h - GStreamer annodex utility functions
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#ifndef __GST_ANNODEX_H__
#define __GST_ANNODEX_H__

#include <gst/gst.h>

GstClockTime gst_annodex_granule_to_time (gint64 granulepos,
    gint64 granulerate_n, gint64 granulerate_d, guint8 granuleshift);
gchar *gst_annodex_time_to_npt (GstClockTime time);
GValueArray *gst_annodex_parse_headers (const gchar * headers);

#endif /* __GST_ANNODEX_H__ */
