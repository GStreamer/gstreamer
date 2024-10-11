/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/gst.h>
#include "gstd3d12ipc.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D12_IPC_SERVER (gst_d3d12_ipc_server_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12IpcServer, gst_d3d12_ipc_server,
    GST, D3D12_IPC_SERVER, GstObject);

GstD3D12IpcServer * gst_d3d12_ipc_server_new (const std::string & address,
                                              gint64 adapter_luid,
                                              ID3D12Fence * fence);

GstFlowReturn       gst_d3d12_ipc_server_send_data (GstD3D12IpcServer * server,
                                                    GstSample * sample,
                                                    const GstD3D12IpcMemLayout & layout,
                                                    HANDLE handle,
                                                    GstClockTime pts);

void                gst_d3d12_ipc_server_stop      (GstD3D12IpcServer * server);

gint64              gst_d3d12_ipc_server_get_adapter_luid (GstD3D12IpcServer * server);

G_END_DECLS
