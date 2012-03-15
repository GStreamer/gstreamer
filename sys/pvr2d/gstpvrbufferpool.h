/*
 * GStreamer
 * Copyright (c) 2010, 2011 Texas Instruments Incorporated
 * Copyright (c) 2011, Collabora Ltda
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __GSTPVRBUFFERPOOL_H__
#define __GSTPVRBUFFERPOOL_H__

#include "gstpvr.h"
#include <pvr2d.h>

G_BEGIN_DECLS

typedef struct _GstPVRMeta GstPVRMeta;

typedef struct _GstPVRBufferPool GstPVRBufferPool;
typedef struct _GstPVRBufferPoolClass GstPVRBufferPoolClass;

#include "gstpvrvideosink.h"

const GstMetaInfo * gst_pvr_meta_get_info (void);
#define GST_PVR_META_INFO  (gst_pvr_meta_get_info())

#define gst_buffer_get_pvr_meta(b) ((GstPVRMeta*)gst_buffer_get_meta((b),GST_PVR_META_INFO))

struct _GstPVRMeta
{
  GstMeta meta;

  PVR2DMEMINFO *src_mem;	/* Memory wrapped by pvr */
  GstElement *sink;		/* sink, holds a ref */
};

GstPVRMeta *
gst_buffer_add_pvr_meta(GstBuffer *buffer, GstElement *pvrsink);

#define GST_TYPE_PVR_BUFFER_POOL      (gst_pvr_buffer_pool_get_type())
#define GST_IS_PVR_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PVR_BUFFER_POOL))
#define GST_PVR_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PVR_BUFFER_POOL, GstPVRBufferPool))
#define GST_PVR_BUFFER_POOL_CAST(obj) ((GstPVRBufferPool*)(obj))

struct _GstPVRBufferPool
{
  GstBufferPool parent;

  /* output (padded) size including any codec padding: */
  gint padded_width, padded_height;
  guint size;
  GstAllocationParams params;

  GstElement *pvrsink;

  GstCaps *caps;
  GstVideoInfo info;
  gboolean add_metavideo;
};

struct _GstPVRBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_pvr_buffer_pool_get_type (void);
GstBufferPool *gst_pvr_buffer_pool_new (GstElement *pvrsink);

G_END_DECLS

#endif /* __GSTPVRBUFFERPOOL_H__ */
