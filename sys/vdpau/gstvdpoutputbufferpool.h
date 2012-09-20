/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * gst-plugins-bad
 * Copyright (C) Carl-Anton Ingmarsson 2010 <ca.ingmarsson@gmail.com>
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

#ifndef _GST_VDP_OUTPUT_BUFFER_POOL_H_
#define _GST_VDP_OUTPUT_BUFFER_POOL_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VDP_OUTPUT_BUFFER_POOL             (gst_vdp_output_buffer_pool_get_type ())
#define GST_VDP_OUTPUT_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_OUTPUT_BUFFER_POOL, GstVdpOutputBufferPool))
#define GST_VDP_OUTPUT_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VDP_OUTPUT_BUFFER_POOL, GstVdpOutputBufferPoolClass))
#define GST_IS_VDP_OUTPUT_BUFFER_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_OUTPUT_BUFFER_POOL))
#define GST_IS_VDP_OUTPUT_BUFFER_POOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VDP_OUTPUT_BUFFER_POOL))
#define GST_VDP_OUTPUT_BUFFER_POOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDP_OUTPUT_BUFFER_POOL, GstVdpOutputBufferPoolClass))

typedef struct _GstVdpOutputBufferPool GstVdpOutputBufferPool;
typedef struct _GstVdpOutputBufferPoolClass GstVdpOutputBufferPoolClass;

struct _GstVdpOutputBufferPoolClass
{
	GstVdpBufferPoolClass buffer_pool_class;
};

GstVdpBufferPool *gst_vdp_output_buffer_pool_new (GstVdpDevice *device);

GType gst_vdp_output_buffer_pool_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* _GST_VDP_OUTPUT_BUFFER_POOL_H_ */
