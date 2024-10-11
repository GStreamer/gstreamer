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

enum GstD3D12IpcIOMode
{
  GST_D3D12_IPC_IO_COPY,
  GST_D3D12_IPC_IO_IMPORT,
};

#define GST_TYPE_D3D12_IPC_IO_MODE (gst_d3d12_ipc_io_mode_get_type ())
GType gst_d3d12_ipc_io_mode_get_type (void);

#define GST_TYPE_D3D12_IPC_CLIENT (gst_d3d12_ipc_client_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12IpcClient, gst_d3d12_ipc_client,
    GST, D3D12_IPC_CLIENT, GstObject);

void                gst_d3d12_ipc_client_deinit (void);

GstD3D12IpcClient * gst_d3d12_ipc_client_new (const std::string & address,
                                              GstD3D12Device * device,
                                              GstD3D12IpcIOMode io_mode,
                                              guint timeout);

GstFlowReturn       gst_d3d12_ipc_client_get_sample (GstD3D12IpcClient * client,
                                                     GstSample ** sample);

void                gst_d3d12_ipc_client_set_flushing (GstD3D12IpcClient * client,
                                                       bool flushing);

GstCaps *           gst_d3d12_ipc_client_get_caps     (GstD3D12IpcClient * client);

GstFlowReturn       gst_d3d12_ipc_client_run          (GstD3D12IpcClient * client);

void                gst_d3d12_ipc_client_stop         (GstD3D12IpcClient * client);

G_END_DECLS
