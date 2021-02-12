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


#ifndef __GST_GST1394_H__
#define __GST_GST1394_H__


#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include "gst1394clock.h"

#include <libraw1394/raw1394.h>
#ifdef HAVE_LIBIEC61883
#include <libiec61883/iec61883.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_DV1394SRC (gst_dv1394src_get_type())
G_DECLARE_FINAL_TYPE (GstDV1394Src, gst_dv1394src, GST, DV1394SRC, GstPushSrc)

struct _GstDV1394Src {
  GstPushSrc element;

  // consecutive=2, skip=4 will skip 4 frames, then let 2 consecutive ones through
  gint consecutive;
  gint skip;
  gboolean drop_incomplete;

  gint num_ports;
  gint port;
  gint channel;
  octlet_t guid;
  gint avc_node;
  gboolean use_avc;

  struct raw1394_portinfo pinfo[16];
  raw1394handle_t handle;

  GstBuffer *buf;
  
  GstBuffer *frame;
  guint frame_size;
  guint frame_rate;
  guint bytes_in_frame;
  guint frame_sequence;

  int control_sock[2];

  gchar *uri;

  gchar *device_name;

  gboolean connected;
  #ifdef HAVE_LIBIEC61883
  iec61883_dv_fb_t iec61883dv;
  #endif

  Gst1394Clock *provided_clock;
};

GST_ELEMENT_REGISTER_DECLARE (dv1394src);

G_END_DECLS

#endif /* __GST_GST1394_H__ */
