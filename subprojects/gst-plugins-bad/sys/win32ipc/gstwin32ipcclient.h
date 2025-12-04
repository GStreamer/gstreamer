/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
#include "gstwin32ipcmmf.h"
#include "gstwin32ipc.h"
#include <string>
#include <vector>

G_BEGIN_DECLS

#define GST_TYPE_WIN32_IPC_CLIENT (gst_win32_ipc_client_get_type())
G_DECLARE_FINAL_TYPE (GstWin32IpcClient, gst_win32_ipc_client,
    GST, WIN32_IPC_CLIENT, GstObject);

GstWin32IpcClient * gst_win32_ipc_client_new (const std::string & address,
                                              guint timeout,
                                              guint64 max_buffers,
                                              GstWin32IpcLeakyType leaky);

GstFlowReturn       gst_win32_ipc_client_get_sample (GstWin32IpcClient * client,
                                                     GstSample ** sample);

void                gst_win32_ipc_client_set_flushing (GstWin32IpcClient * client,
                                                       bool flushing);

GstCaps *           gst_win32_ipc_client_get_caps     (GstWin32IpcClient * client);

GstFlowReturn       gst_win32_ipc_client_run          (GstWin32IpcClient * client);

void                gst_win32_ipc_client_stop         (GstWin32IpcClient * client);

void                gst_win32_ipc_client_set_leaky (GstWin32IpcClient * client,
                                                    GstWin32IpcLeakyType leaky);

void                gst_win32_ipc_client_set_max_buffers (GstWin32IpcClient * client,
                                                          guint64 max_buffers);

guint64             gst_win32_ipc_client_get_current_level_buffers (GstWin32IpcClient * client);

G_END_DECLS
