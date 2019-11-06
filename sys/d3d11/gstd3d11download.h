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

#ifndef __GST_D3D11_DOWNLOAD_H__
#define __GST_D3D11_DOWNLOAD_H__

#include "gstd3d11basefilter.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_DOWNLOAD             (gst_d3d11_download_get_type())
#define GST_D3D11_DOWNLOAD(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_DOWNLOAD,GstD3D11Download))
#define GST_D3D11_DOWNLOAD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_D3D11_DOWNLOAD,GstD3D11DownloadClass))
#define GST_D3D11_DOWNLOAD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D11_DOWNLOAD,GstD3D11DownloadClass))
#define GST_IS_D3D11_DOWNLOAD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_DOWNLOAD))
#define GST_IS_D3D11_DOWNLOAD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D11_DOWNLOAD))

struct _GstD3D11Download
{
  GstD3D11BaseFilter parent;
};

struct _GstD3D11DownloadClass
{
  GstD3D11BaseFilterClass parent_class;
};

GType gst_d3d11_download_get_type (void);

G_END_DECLS

#endif /* __GST_D3D11_DOWNLOAD_H__ */
