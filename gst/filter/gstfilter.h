/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilter.h: element for filter plug-ins
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


#ifndef __GST_FILTER_H__
#define __GST_FILTER_H__

#include <gst/gst.h>
GType gst_iir_get_type (void);
GType gst_lpwsinc_get_type (void);
GType gst_bpwsinc_get_type (void);

extern GstStaticPadTemplate gst_filter_sink_template;
extern GstStaticPadTemplate gst_filter_src_template;

#endif /* __GST_FILTER_H__ */
