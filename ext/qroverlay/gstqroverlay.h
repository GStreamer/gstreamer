/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (c) 2020 Anthony Violo <anthony.violo@ubicast.eu>
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

#ifndef __GST_QR_OVERLAY_H__
#define __GST_QR_OVERLAY_H__

#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define GST_TYPE_QR_OVERLAY (gst_qr_overlay_get_type())
G_DECLARE_FINAL_TYPE (GstQROverlay, gst_qr_overlay, GST, QR_OVERLAY, GstVideoFilter);

struct _GstQROverlay
{
  GstVideoFilter parent;

  guint32 frame_number;
  gfloat qrcode_size;
  guint qrcode_quality;
  guint array_counter;
  guint array_size;
  guint span_frame;
  guint64 extra_data_interval_buffers;
  guint64 extra_data_span_buffers;
  QRecLevel level;
  gchar *framerate_string;
  gchar *extra_data_name;
  gchar *extra_data_str;
  gchar **extra_data_array;
  gfloat x_percent;
  gfloat y_percent;
  gboolean silent;
  gboolean extra_data_enabled;
};

G_END_DECLS
#endif /* __GST_QR_OVERLAY_H__ */
