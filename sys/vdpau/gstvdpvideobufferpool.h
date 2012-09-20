/*
 * gst-plugins-bad
 * Copyright (C) Carl-Anton Ingmarsson 2010 <ca.ingmarsson@gmail.com>
 *               2012 Edward Hervey <edward@collabora.com>
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

#ifndef _GST_VDP_VIDEO_BUFFERPOOL_H_
#define _GST_VDP_VIDEO_BUFFERPOOL_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include "gstvdpdevice.h"

G_BEGIN_DECLS

#define GST_VDPAU_SURFACE_META_GET(buf) ((GstVdpauMeta *)gst_buffer_get_meta(buf,gst_vdpau_surface_meta_api_get_type()))
#define GST_VDPAU_SURFACE_META_ADD(buf) ((GstVdpauMeta *)gst_buffer_add_meta(buf,gst_vdpau_surface_meta_get_info(),NULL))

struct _GstVdpauSurfaceMeta {
	GstMeta meta;

	GstVdpDevice *device;
	VdpVideoSurface surface;
};

GType gst_vdpau_surface_meta_api_get_type (void);

const GstMetaInfo * gst_vdpau_surface_meta_get_info (void);
/**
 * GST_BUFFER_POOL_OPTION_VDP_VIDEO_META:
 *
 * An option that can be activated on bufferpool to request VdpVideo metadata
 * on buffers from the pool.
 */
#define GST_BUFFER_POOL_OPTION_VDP_VIDEO_META "GstBufferPoolOptionVdpVideoMeta"

typedef struct _GstVdpVideoBufferPool GstVdpVideoBufferPool;
typedef struct _GstVdpVideoBufferPoolClass GstVdpVideoBufferPoolClass;

/* buffer pool functions */
#define GST_TYPE_VDP_VIDEO_BUFFER_POOL      (gst_vdp_video_buffer_pool_get_type())
#define GST_IS_VDP_VIDEO_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_VIDEO_BUFFER_POOL))
#define GST_VDP_VIDEO_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_VIDEO_BUFFER_POOL, GstVdpVideoBufferPool))
#define GST_VDP_VIDEO_BUFFER_POOL_CAST(obj) ((GstVdpVideoBufferPool*)(obj))

struct _GstVdpVideoBufferPool
{
  GstBufferPool bufferpool;
	
  GstVdpDevice *device;

  GstVideoInfo  info;
  VdpChromaType chroma_type;
	
  gboolean      add_videometa;
  gboolean      add_vdpmeta;
};

struct _GstVdpVideoBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_vdp_video_buffer_pool_get_type (void);
GstBufferPool *gst_vdp_video_buffer_pool_new (GstVdpDevice *device);

GstCaps *gst_vdp_video_buffer_get_caps (gboolean filter, VdpChromaType chroma_type);
#if 0
GstCaps *gst_vdp_video_buffer_get_allowed_caps (GstVdpDevice * device);

gboolean gst_vdp_video_buffer_calculate_size (guint32 fourcc, gint width, gint height, guint *size);
/* FIXME : Replace with map/unmap  */
gboolean gst_vdp_video_buffer_download (GstVdpVideoBuffer *inbuf, GstBuffer *outbuf, guint32 fourcc, gint width, gint height);
gboolean gst_vdp_video_buffer_upload (GstVdpVideoBuffer *video_buf, GstBuffer *src_buf, guint fourcc, gint width, gint height);
#endif


G_END_DECLS

#endif /* _GST_VDP_VIDEO_BUFFER_POOL_H_ */
