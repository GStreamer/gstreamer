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

GstD3D11Device *      gst_d3d11_device_new                (guint adapter);

ID3D11Device *        gst_d3d11_device_get_device_handle  (GstD3D11Device * device);

ID3D11DeviceContext * gst_d3d11_device_get_device_context_handle (GstD3D11Device * device);

GstD3D11DXGIFactoryVersion gst_d3d11_device_get_chosen_dxgi_factory_version (GstD3D11Device * device);

D3D_FEATURE_LEVEL     gst_d3d11_device_get_chosen_feature_level (GstD3D11Device * device);

IDXGISwapChain *      gst_d3d11_device_create_swap_chain  (GstD3D11Device * device,
                                                           const DXGI_SWAP_CHAIN_DESC * desc);

#if (DXGI_HEADER_VERSION >= 2)
#if (!GST_D3D11_WINAPI_ONLY_APP)
IDXGISwapChain1 *     gst_d3d11_device_create_swap_chain_for_hwnd (GstD3D11Device * device,
                                                                   HWND hwnd,
                                                                   const DXGI_SWAP_CHAIN_DESC1 * desc,
                                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC * fullscreen_desc,
                                                                   IDXGIOutput * output);
#endif

#if GST_D3D11_WINAPI_ONLY_APP
IDXGISwapChain1 *      gst_d3d11_device_create_swap_chain_for_core_window (GstD3D11Device * device,
                                                                           guintptr core_window,
                                                                           const DXGI_SWAP_CHAIN_DESC1 * desc,
                                                                           IDXGIOutput * output);

IDXGISwapChain1 *      gst_d3d11_device_create_swap_chain_for_composition (GstD3D11Device * device,
                                                                           const DXGI_SWAP_CHAIN_DESC1 * desc,
                                                                           IDXGIOutput * output);

#endif /* GST_D3D11_WINAPI_ONLY_APP */
#endif /* (DXGI_HEADER_VERSION >= 2) */

void                  gst_d3d11_device_release_swap_chain (GstD3D11Device * device,
                                                           IDXGISwapChain * swap_chain);

ID3D11Texture2D *     gst_d3d11_device_create_texture     (GstD3D11Device * device,
                                                           const D3D11_TEXTURE2D_DESC * desc,
                                                           const D3D11_SUBRESOURCE_DATA *inital_data);

void                  gst_d3d11_device_release_texture    (GstD3D11Device * device,
                                                           ID3D11Texture2D * texture);

void                  gst_d3d11_device_lock               (GstD3D11Device * device);

void                  gst_d3d11_device_unlock             (GstD3D11Device * device);

void                  gst_d3d11_device_d3d11_debug (GstD3D11Device * device,
                                                    const gchar * file,
                                                    const gchar * function,
                                                    gint line);

void                  gst_d3d11_device_dxgi_debug  (GstD3D11Device * device,
                                                    const gchar * file,
                                                    const gchar * function,
                                                    gint line);

const GstD3D11Format * gst_d3d11_device_format_from_gst (GstD3D11Device * device,
                                                         GstVideoFormat format);

G_END_DECLS

#endif /* __GST_D3D11_DEVICE_H__ */
