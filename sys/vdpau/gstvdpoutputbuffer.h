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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_VDP_OUTPUT_BUFFER_H_
#define _GST_VDP_OUTPUT_BUFFER_H_

#include <gst/gst.h>

#include "gstvdpdevice.h"

GType gst_vdpau_output_meta_api_get_type (void);

const GstMetaInfo * gst_vdpau_output_meta_get_info (void);

#define GST_VDPAU_OUTPUT_META_GET(buf) ((GstVdpauMeta *)gst_buffer_get_meta(buf,gst_vdpau_output_meta_api_get_type()))
#define GST_VDPAU_OUTPUT_META_ADD(buf) ((GstVdpauMeta *)gst_buffer_add_meta(buf,gst_vdpau_output_meta_get_info(),NULL))

struct _GstVdpauOutputMeta {
  GstMeta meta;

  /* FIXME : Check we actually need all of this */
  GstVdpDevice *device;
  VdpRGBAFormat rgba_format;
  gint width, height;
  
  VdpOutputSurface surface;
};

#if 0
/* FIXME : Replace with GST_VIDEO_FORMAT... and GST_VIDEO_CHROMA_... */
GstCaps *gst_vdp_output_buffer_get_template_caps (void);
GstCaps *gst_vdp_output_buffer_get_allowed_caps (GstVdpDevice *device);
gboolean gst_vdp_caps_to_rgba_format (GstCaps *caps, VdpRGBAFormat *rgba_format);

gboolean gst_vdp_output_buffer_calculate_size (GstVdpOutputBuffer *output_buf, guint *size);
/* FIXME : Replace with map/unmap  */
gboolean gst_vdp_output_buffer_download (GstVdpOutputBuffer *output_buf, GstBuffer *outbuf, GError **error);

#define GST_VDP_OUTPUT_CAPS \
  "video/x-vdpau-output, " \
  "rgba-format = (int)[0,4], " \
  "width = (int)[1,8192], " \
  "height = (int)[1,8192]"
#endif
#endif
