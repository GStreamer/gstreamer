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

#ifndef _GST_VDP_OUTPUT_SRC_PAD_H_
#define _GST_VDP_OUTPUT_SRC_PAD_H_

#include <gst/gst.h>

#include "gstvdpdevice.h"
#include "gstvdpoutputbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_OUTPUT_SRC_PAD             (gst_vdp_output_src_pad_get_type ())
#define GST_VDP_OUTPUT_SRC_PAD(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_OUTPUT_SRC_PAD, GstVdpOutputSrcPad))
#define GST_VDP_OUTPUT_SRC_PAD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VDP_OUTPUT_SRC_PAD, GstVdpOutputSrcPadClass))
#define GST_IS_VDP_OUTPUT_SRC_PAD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_OUTPUT_SRC_PAD))
#define GST_IS_VDP_OUTPUT_SRC_PAD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VDP_OUTPUT_SRC_PAD))
#define GST_VDP_OUTPUT_SRC_PAD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDP_OUTPUT_SRC_PAD, GstVdpOutputSrcPadClass))

typedef struct _GstVdpOutputSrcPad GstVdpOutputSrcPad;
typedef struct _GstVdpOutputSrcPadClass GstVdpOutputSrcPadClass;

GstFlowReturn gst_vdp_output_src_pad_push (GstVdpOutputSrcPad *vdp_pad, GstVdpOutputBuffer *output_buf, GError **error);
GstFlowReturn gst_vdp_output_src_pad_alloc_buffer (GstVdpOutputSrcPad *vdp_pad, GstVdpOutputBuffer **output_buf, GError **error);

GstFlowReturn gst_vdp_output_src_pad_get_device (GstVdpOutputSrcPad *vdp_pad, GstVdpDevice **device, GError **error);

GstVdpOutputSrcPad *gst_vdp_output_src_pad_new (GstPadTemplate *templ, const gchar *name);
GType gst_vdp_output_src_pad_get_type (void);

G_END_DECLS

#endif /* _GST_VDP_OUTPUT_SRC_PAD_H_ */
