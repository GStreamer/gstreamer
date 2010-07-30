/* 
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef _GST_VDP_OUTPUT_BUFFER_H_
#define _GST_VDP_OUTPUT_BUFFER_H_

#include <gst/gst.h>

#include "gstvdpbuffer.h"
#include "gstvdpdevice.h"

typedef struct _GstVdpOutputBuffer GstVdpOutputBuffer;

#define GST_TYPE_VDP_OUTPUT_BUFFER (gst_vdp_output_buffer_get_type())
#define GST_IS_VDP_OUTPUT_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_OUTPUT_BUFFER))
#define GST_VDP_OUTPUT_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_OUTPUT_BUFFER, GstVdpOutputBuffer))

struct _GstVdpOutputBuffer {
  GstVdpBuffer vdp_buffer;

  GstVdpDevice *device;
  VdpRGBAFormat rgba_format;
  gint width, height;
  
  VdpOutputSurface surface;
};

GType gst_vdp_output_buffer_get_type (void);

GstVdpOutputBuffer* gst_vdp_output_buffer_new (GstVdpDevice * device, VdpRGBAFormat rgba_format, gint width, gint height, GError **error);

GstCaps *gst_vdp_output_buffer_get_template_caps (void);
GstCaps *gst_vdp_output_buffer_get_allowed_caps (GstVdpDevice *device);
gboolean gst_vdp_caps_to_rgba_format (GstCaps *caps, VdpRGBAFormat *rgba_format);

gboolean gst_vdp_output_buffer_calculate_size (GstVdpOutputBuffer *output_buf, guint *size);
gboolean gst_vdp_output_buffer_download (GstVdpOutputBuffer *output_buf, GstBuffer *outbuf, GError **error);

#define GST_VDP_OUTPUT_CAPS \
  "video/x-vdpau-output, " \
  "rgba-format = (int)[0,4], " \
  "width = (int)[1,8192], " \
  "height = (int)[1,8192]"

#endif
