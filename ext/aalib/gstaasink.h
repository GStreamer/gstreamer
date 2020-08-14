/* GStreamer
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_AASINK_H__
#define __GST_AASINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

#include <aalib.h>

G_BEGIN_DECLS

#define GST_TYPE_AASINK (gst_aasink_get_type())
G_DECLARE_FINAL_TYPE (GstAASink, gst_aasink, GST, AASINK, GstVideoSink)

struct _GstAASink {
  GstVideoSink parent;

  GstVideoInfo info;

  gint frames_displayed;
  guint64 frame_time;

  aa_context *context;
  struct aa_hardware_params ascii_surf;
  struct aa_renderparams ascii_parms;
  aa_palette palette;
  gint aa_driver;
};

GST_ELEMENT_REGISTER_DECLARE (aasink);

G_END_DECLS

#endif /* __GST_AASINKE_H__ */
