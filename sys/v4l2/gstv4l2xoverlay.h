/* G-Streamer generic V4L2 element - X overlay interface implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstv4l2xoverlay.h: tv mixer interface implementation for V4L2
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

#ifndef __GST_V4L2_X_OVERLAY_H__
#define __GST_V4L2_X_OVERLAY_H__

#include <gst/gst.h>
#include <gst/xoverlay/xoverlay.h>

#include "gstv4l2element.h"

G_BEGIN_DECLS void gst_v4l2_xoverlay_interface_init (GstXOverlayClass * klass);

GstXWindowListener *gst_v4l2_xoverlay_new (GstV4l2Element * v4l2element);
void gst_v4l2_xoverlay_free (GstV4l2Element * v4l2element);

/* signal handlers */
void gst_v4l2_xoverlay_open (GstV4l2Element * v4l2element);
void gst_v4l2_xoverlay_close (GstV4l2Element * v4l2element);

#endif /* __GST_V4L2_X_OVERLAY_H__ */
