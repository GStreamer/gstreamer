/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_D3D11_DEVICE_H__
#define __GST_D3D11_DEVICE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d11/gstd3d11_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D11_DEVICE             (gst_d3d11_device_get_type())
#define GST_D3D11_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_DEVICE,GstD3D11Device))
#define GST_D3D11_DEVICE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D11_DEVICE,GstD3D11DeviceClass))
#define GST_D3D11_DEVICE_GET_CLASS(obj)   (GST_D3D11_DEVICE_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3D11_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_DEVICE))
#define GST_IS_D3D11_DEVICE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D11_DEVICE))
#define GST_D3D11_DEVICE_CAST(obj)        ((GstD3D11Device*)(obj))

#define GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE "gst.d3d11.device.handle"

struct _GstD3D11Device
{
  GstObject parent;

  GstD3D11DevicePrivate *priv;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstD3D11DeviceClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D11_API
GType                 gst_d3d11_device_get_type           (void);

GST_D3D11_API
GstD3D11Device *      gst_d3d11_device_new                (guint adapter_index,
                                                           guint flags);

GST_D3D11_API
GstD3D11Device *      gst_d3d11_device_new_for_adapter_luid (gint64 adapter_luid,
                                                             guint flags);

GST_D3D11_API
GstD3D11Device *      gst_d3d11_device_new_wrapped        (ID3D11Device * device);

GST_D3D11_API
ID3D11Device *        gst_d3d11_device_get_device_handle  (GstD3D11Device * device);

GST_D3D11_API
ID3D11DeviceContext * gst_d3d11_device_get_device_context_handle (GstD3D11Device * device);

GST_D3D11_API
IDXGIFactory1 *       gst_d3d11_device_get_dxgi_factory_handle (GstD3D11Device * device);

GST_D3D11_API
ID3D11VideoDevice *   gst_d3d11_device_get_video_device_handle (GstD3D11Device * device);

GST_D3D11_API
ID3D11VideoContext *  gst_d3d11_device_get_video_context_handle (GstD3D11Device * device);

GST_D3D11_API
void                  gst_d3d11_device_lock               (GstD3D11Device * device);

GST_D3D11_API
void                  gst_d3d11_device_unlock             (GstD3D11Device * device);

GST_D3D11_API
const GstD3D11Format * gst_d3d11_device_format_from_gst (GstD3D11Device * device,
                                                         GstVideoFormat format);

G_END_DECLS

#endif /* __GST_D3D11_DEVICE_H__ */
