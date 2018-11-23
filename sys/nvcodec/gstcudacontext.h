/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_CUDA_CONTEXT_H__
#define __GST_CUDA_CONTEXT_H__

#include <gst/gst.h>
#include <cuda.h>

G_BEGIN_DECLS

#define GST_TYPE_CUDA_CONTEXT             (gst_cuda_context_get_type())
#define GST_CUDA_CONTEXT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CUDA_CONTEXT,GstCudaContext))
#define GST_CUDA_CONTEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),  GST_TYPE_CUDA_CONTEXT,GstCudaContextClass))
#define GST_CUDA_CONTEXT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),  GST_TYPE_CUDA_CONTEXT,GstCudaContextClass))
#define GST_IS_CUDA_CONTEXT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),GST_TYPE_CUDA_CONTEXT))
#define GST_IS_CUDA_CONTEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_CUDA_CONTEXT))
#define GST_CUDA_CONTEXT_CAST(obj)        ((GstCudaContext*)(obj))

#define GST_CUDA_CONTEXT_TYPE "gst.cuda.context"

typedef struct _GstCudaContext GstCudaContext;
typedef struct _GstCudaContextClass GstCudaContextClass;
typedef struct _GstCudaContextPrivate GstCudaContextPrivate;

/*
 * GstCudaContext:
 */
struct _GstCudaContext
{
  GstObject object;

  /*< private >*/
  GstCudaContextPrivate *priv;
};

/*
 * GstCudaContextClass:
 */
struct _GstCudaContextClass
{
  GstObjectClass parent_class;
};

GType            gst_cuda_context_get_type    (void);

G_GNUC_INTERNAL
GstCudaContext * gst_cuda_context_new         (gint device_id);

G_GNUC_INTERNAL
gboolean         gst_cuda_context_push        (GstCudaContext * ctx);

G_GNUC_INTERNAL
gboolean         gst_cuda_context_pop         (CUcontext * cuda_ctx);

G_GNUC_INTERNAL
gpointer         gst_cuda_context_get_handle  (GstCudaContext * ctx);

G_END_DECLS

#endif /* __GST_CUDA_CONTEXT_H__ */
