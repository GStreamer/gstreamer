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

GType gst_ducati_buffer_get_type (void);
#define GST_TYPE_DUCATIBUFFER (gst_ducati_buffer_get_type())
#define GST_IS_DUCATIBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DUCATIBUFFER))
#define GST_DUCATIBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DUCATIBUFFER, GstDucatiBuffer))

GType gst_pvr_bufferpool_get_type (void);
#define GST_TYPE_PVRBUFFERPOOL (gst_pvr_bufferpool_get_type())
#define GST_IS_PVRBUFFERPOOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PVRBUFFERPOOL))
#define GST_PVRBUFFERPOOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PVRBUFFERPOOL, \
                               GstPvrBufferPool))

typedef struct _GstPvrBufferPool GstPvrBufferPool;
typedef struct _GstDucatiBuffer GstDucatiBuffer;

struct _GstPvrBufferPool
{
  GstMiniObject parent;

  /* output (padded) size including any codec padding: */
  gint padded_width, padded_height;
  gint size;
  PVR2DCONTEXTHANDLE pvr_context;

  GstCaps *caps;
  GMutex *lock;
  gboolean running;  /* with lock */
  GstElement *element;  /* the element that owns us.. */
  GQueue *free_buffers;
  GQueue *used_buffers;
  guint buffer_count;
};

GstPvrBufferPool * gst_pvr_bufferpool_new (GstElement * element,
    GstCaps * caps, gint num_buffers, gint size,
    PVR2DCONTEXTHANDLE pvr_context);
void gst_pvr_bufferpool_stop_running (GstPvrBufferPool * pool, gboolean unwrap);
GstDucatiBuffer * gst_pvr_bufferpool_get (GstPvrBufferPool * self,
    GstBuffer * orig);

#define GST_PVR_BUFFERPOOL_LOCK(self)     g_mutex_lock ((self)->lock)
#define GST_PVR_BUFFERPOOL_UNLOCK(self)   g_mutex_unlock ((self)->lock)

struct _GstDucatiBuffer {
  GstBuffer parent;

  GstPvrBufferPool *pool; /* buffer-pool that this buffer belongs to */
  GstBuffer       *orig;     /* original buffer, if we need to copy output */
  PVR2DMEMINFO *src_mem; /* Memory wrapped by pvr */
  gboolean wrapped;
};

GstBuffer * gst_ducati_buffer_get (GstDucatiBuffer * self);
PVR2DMEMINFO * gst_ducati_buffer_get_meminfo (GstDucatiBuffer * self);

G_END_DECLS

#endif /* __GSTPVRBUFFERPOOL_H__ */
