/* GStreamer
 * Copyright (C) 2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshow.h:
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

#ifndef _GST_DSHOW_UTIL_H_
#define _GST_DSHOW_UTIL_H_

#include <windows.h>
#include <objbase.h>
#include <dshow.h>
#include <Rpc.h>
#include <streams.h>
#include <strmif.h>

#include <glib.h>

/* get a pin from directshow filter */
IPin *gst_dshow_get_pin_from_filter (IBaseFilter *filter, PIN_DIRECTION pindir);

/* find and return a filter according to the input and output types */
gboolean gst_dshow_find_filter(CLSID input_majortype, CLSID input_subtype, 
                               CLSID output_majortype, CLSID output_subtype,
                               gchar * prefered_filter_name, IBaseFilter **filter);

#endif /* _GST_DSHOW_UTIL_H_ */
