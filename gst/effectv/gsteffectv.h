/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 *  EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

#include <gst/gst.h>

GType gst_edgetv_get_type (void);
GType gst_agingtv_get_type (void);
GType gst_dicetv_get_type (void);
GType gst_warptv_get_type (void);
GType gst_shagadelictv_get_type (void);
GType gst_vertigotv_get_type (void);
GType gst_revtv_get_type (void);
GType gst_quarktv_get_type (void);

extern GstStaticPadTemplate gst_effectv_sink_template;
extern GstStaticPadTemplate gst_effectv_src_template;
