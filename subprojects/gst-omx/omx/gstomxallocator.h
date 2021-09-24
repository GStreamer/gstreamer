/*
 * Copyright (C) 2019, Collabora Ltd.
 *   Author: George Kiagiadakis <george.kiagiadakis@collabora.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_ALLOCATOR_H__
#define __GST_OMX_ALLOCATOR_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomx.h"

G_BEGIN_DECLS

#define GST_OMX_MEMORY_QUARK gst_omx_memory_quark ()

#define GST_TYPE_OMX_ALLOCATOR   (gst_omx_allocator_get_type())
#define GST_IS_OMX_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OMX_ALLOCATOR))
#define GST_OMX_ALLOCATOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_OMX_ALLOCATOR, GstOMXAllocator))

typedef struct _GstOMXMemory GstOMXMemory;
typedef struct _GstOMXAllocator GstOMXAllocator;
typedef struct _GstOMXAllocatorClass GstOMXAllocatorClass;

typedef enum {
  GST_OMX_ALLOCATOR_FOREIGN_MEM_NONE,
  GST_OMX_ALLOCATOR_FOREIGN_MEM_DMABUF,
  GST_OMX_ALLOCATOR_FOREIGN_MEM_OTHER_POOL,
} GstOMXAllocatorForeignMemMode;

struct _GstOMXMemory
{
  GstMemory mem;
  GstOMXBuffer *buf;

  /* TRUE if the memory is in use outside the allocator */
  gboolean acquired;

  /* memory allocated from the foreign_allocator
   * or planted externally when using a foreign buffer pool */
  GstMemory *foreign_mem;
  /* the original dispose function of foreign_mem */
  GstMiniObjectDisposeFunction foreign_dispose;
};

struct _GstOMXAllocator
{
  GstAllocator parent;

  GstOMXComponent *component;
  GstOMXPort *port;

  GstOMXAllocatorForeignMemMode foreign_mode;
  GstAllocator *foreign_allocator;

  /* array of GstOMXMemory */
  GPtrArray *memories;
  guint n_memories;

  guint n_outstanding;
  gboolean active;

  GMutex lock;
  GCond cond;
};

struct _GstOMXAllocatorClass
{
  GstAllocatorClass parent_class;
};

GType gst_omx_allocator_get_type (void);

GQuark gst_omx_memory_quark (void);

GstOMXBuffer * gst_omx_memory_get_omx_buf (GstMemory * mem);

GstOMXAllocator * gst_omx_allocator_new (GstOMXComponent * component,
    GstOMXPort * port);

gboolean gst_omx_allocator_configure (GstOMXAllocator * allocator, guint count,
    GstOMXAllocatorForeignMemMode mode);
gboolean gst_omx_allocator_set_active (GstOMXAllocator * allocator,
    gboolean active);
void gst_omx_allocator_wait_inactive (GstOMXAllocator * allocator);

GstFlowReturn gst_omx_allocator_acquire (GstOMXAllocator * allocator,
    GstMemory ** memory, gint index, GstOMXBuffer * omx_buf);

GstMemory * gst_omx_allocator_allocate (GstOMXAllocator * allocator, gint index,
    GstMemory * foreign_mem);

G_END_DECLS

#endif
