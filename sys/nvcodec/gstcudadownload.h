/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_CUDA_DOWNLOAD_H__
#define __GST_CUDA_DOWNLOAD_H__

#include "gstcudabasetransform.h"

G_BEGIN_DECLS

#define GST_TYPE_CUDA_DOWNLOAD             (gst_cuda_download_get_type())
#define GST_CUDA_DOWNLOAD(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CUDA_DOWNLOAD,GstCudaDownload))
#define GST_CUDA_DOWNLOAD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CUDA_DOWNLOAD,GstCudaDownloadClass))
#define GST_CUDA_DOWNLOAD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CUDA_DOWNLOAD,GstCudaDownloadClass))
#define GST_IS_CUDA_DOWNLOAD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CUDA_DOWNLOAD))
#define GST_IS_CUDA_DOWNLOAD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CUDA_DOWNLOAD))

typedef struct _GstCudaDownload GstCudaDownload;
typedef struct _GstCudaDownloadClass GstCudaDownloadClass;

struct _GstCudaDownload
{
  GstCudaBaseTransform parent;
};

struct _GstCudaDownloadClass
{
  GstCudaBaseTransformClass parent_class;
};

GType gst_cuda_download_get_type (void);

G_END_DECLS

#endif /* __GST_CUDA_DOWNLOAD_H__ */
