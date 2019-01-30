/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include "gstd3d11_fwd.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_DEVICE             (gst_d3d11_device_get_type())
#define GST_D3D11_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_DEVICE,GstD3D11Device))
#define GST_D3D11_DEVICE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D11_DEVICE,GstD3D11DeviceClass))
#define GST_D3D11_DEVICE_GET_CLASS(obj)   (GST_D3D11_DEVICE_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3D11_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_DEVICE))
#define GST_IS_D3D11_DEVICE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D11_DEVICE))
#define GST_D3D11_DEVICE_CAST(obj)        ((GstD3D11Device*)(obj))

#define GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE "gst.d3d11.device.handle"

/**
 * GstD3D11DeviceThreadFunc:
 * @device: a #GstD3D11Device
 * @data: user data
 *
 * Represents a function to run in the D3D11 device thread with @device and @data
 */
typedef void (*GstD3D11DeviceThreadFunc) (GstD3D11Device * device, gpointer data);

typedef enum
{
  GST_D3D11_DXGI_FACTORY_UNKNOWN = 0,
  GST_D3D11_DXGI_FACTORY_1,
  GST_D3D11_DXGI_FACTORY_2,
  GST_D3D11_DXGI_FACTORY_3,
  GST_D3D11_DXGI_FACTORY_4,
  GST_D3D11_DXGI_FACTORY_5,
} GstD3D11DXGIFactoryVersion;


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

GType                 gst_d3d11_device_get_type           (void);

GstD3D11Device *      gst_d3d11_device_new                (gint adapter);

ID3D11Device *        gst_d3d11_device_get_device         (GstD3D11Device * device);

ID3D11DeviceContext * gst_d3d11_device_get_device_context (GstD3D11Device * device);

GstD3D11DXGIFactoryVersion gst_d3d11_device_get_chosen_dxgi_factory_version (GstD3D11Device * device);

IDXGISwapChain *      gst_d3d11_device_create_swap_chain  (GstD3D11Device * device,
                                                           const DXGI_SWAP_CHAIN_DESC * desc);

void                  gst_d3d11_device_release_swap_chain (GstD3D11Device * device,
                                                           IDXGISwapChain * swap_chain);

void                  gst_d3d11_device_thread_add         (GstD3D11Device * device,
                                                           GstD3D11DeviceThreadFunc func,
                                                           gpointer data);

ID3D11Texture2D *     gst_d3d11_device_create_texture     (GstD3D11Device * device,
                                                           const D3D11_TEXTURE2D_DESC * desc,
                                                           const D3D11_SUBRESOURCE_DATA *inital_data);

void                  gst_d3d11_device_release_texture    (GstD3D11Device * device,
                                                           ID3D11Texture2D * texture);

void                  gst_context_set_d3d11_device        (GstContext * context,
                                                           GstD3D11Device * device);

gboolean              gst_context_get_d3d11_device        (GstContext * context,
                                                           GstD3D11Device ** device);

G_END_DECLS

#endif /* __GST_D3D11_DEVICE_H__ */
