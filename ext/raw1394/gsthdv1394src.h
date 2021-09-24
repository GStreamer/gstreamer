/* GStreamer
 * Copyright (C) <2008> Edward Hervey <bilboed@bilboed.com>
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


#ifndef __GST_GSTHDV1394_H__
#define __GST_GSTHDV1394_H__


#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <libraw1394/raw1394.h>
#ifdef HAVE_LIBIEC61883
#include <libiec61883/iec61883.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_HDV1394SRC (gst_hdv1394src_get_type())
G_DECLARE_FINAL_TYPE (GstHDV1394Src, gst_hdv1394src, GST, HDV1394SRC,
    GstPushSrc)

struct _GstHDV1394Src {
  GstPushSrc element;

  gint num_ports;
  gint port;
  gint channel;
  octlet_t guid;
  gint avc_node;
  gboolean use_avc;

  struct raw1394_portinfo pinfo[16];
  raw1394handle_t handle;

  gpointer outdata;
  gsize outoffset;
  guint frame_size;
  guint frame_sequence;

  int control_sock[2];

  gchar *uri;

  gchar *device_name;

  gboolean connected;
  iec61883_mpeg2_t iec61883mpeg2;
};

GST_ELEMENT_REGISTER_DECLARE (hdv1394src);

G_END_DECLS

#endif /* __GST_GST1394_H__ */
