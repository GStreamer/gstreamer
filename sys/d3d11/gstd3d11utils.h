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

#ifndef __GST_D3D11_UTILS_H__
#define __GST_D3D11_UTILS_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstd3d11_fwd.h"

G_BEGIN_DECLS

typedef enum
{
  GST_D3D11_DEVICE_VENDOR_UNKNOWN = 0,
  GST_D3D11_DEVICE_VENDOR_AMD,
  GST_D3D11_DEVICE_VENDOR_INTEL,
  GST_D3D11_DEVICE_VENDOR_NVIDIA,
  GST_D3D11_DEVICE_VENDOR_QUALCOMM,
  GST_D3D11_DEVICE_VENDOR_XBOX,
} GstD3D11DeviceVendor;

gboolean        gst_d3d11_handle_set_context        (GstElement * element,
                                                     GstContext * context,
                                                     gint adapter,
                                                     GstD3D11Device ** device);

gboolean        gst_d3d11_handle_context_query      (GstElement * element,
                                                     GstQuery * query,
                                                     GstD3D11Device * device);

gboolean        gst_d3d11_ensure_element_data       (GstElement * element,
                                                     gint adapter,
                                                     GstD3D11Device ** device);

gboolean        gst_d3d11_is_windows_8_or_greater   (void);

GstD3D11DeviceVendor gst_d3d11_get_device_vendor    (GstD3D11Device * device);

gboolean       _gst_d3d11_result                    (HRESULT hr,
                                                     GstD3D11Device * device,
                                                     GstDebugCategory * cat,
                                                     const gchar * file,
                                                     const gchar * function,
                                                     gint line);
/**
 * gst_d3d11_result:
 * @result: D3D11 API return code #HRESULT
 * @device: (nullable): Associated #GstD3D11Device
 *
 * Returns: %TRUE if D3D11 API call result is SUCCESS
 */
#define gst_d3d11_result(result,device) \
    _gst_d3d11_result (result, device, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)


G_END_DECLS

#endif /* __GST_D3D11_UTILS_H__ */
