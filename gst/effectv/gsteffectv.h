/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * EffecTV:
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * EffecTV is free software. We release this product under the terms of the
 * GNU General Public License version 2. The license is included in the file
 * COPYING.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 */

#include <gst/gst.h>

GType gst_edgetv_get_type (void);
extern GstElementDetails gst_edgetv_details;

GType gst_agingtv_get_type (void);
extern GstElementDetails gst_agingtv_details;

GType gst_dicetv_get_type (void);
extern GstElementDetails gst_dicetv_details;

GType gst_warptv_get_type (void);
extern GstElementDetails gst_warptv_details;

GType gst_shagadelictv_get_type (void);
extern GstElementDetails gst_shagadelictv_details;

extern GstPadTemplate *gst_effectv_sink_factory ();
extern GstPadTemplate *gst_effectv_src_factory ();
