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

#define GST_TYPE_WIN32_IPC_SERVER (gst_win32_ipc_server_get_type())
G_DECLARE_FINAL_TYPE (GstWin32IpcServer, gst_win32_ipc_server,
    GST, WIN32_IPC_SERVER, GstObject);

GstWin32IpcServer * gst_win32_ipc_server_new (const std::string & address,
                                              guint64 max_buffers,
                                              GstWin32IpcLeakyType leaky);

GstFlowReturn       gst_win32_ipc_server_send_data (GstWin32IpcServer * server,
                                                    GstBuffer * buffer,
                                                    GstCaps * caps,
                                                    GByteArray * meta,
                                                    GstClockTime pts,
                                                    GstClockTime dts,
                                                    gsize size);

void                gst_win32_ipc_server_set_flushing (GstWin32IpcServer * server,
                                                       gboolean flushing);

void                gst_win32_ipc_server_set_max_buffers (GstWin32IpcServer * server,
                                                          guint64 max_buffers);

void                gst_win32_ipc_server_set_leaky (GstWin32IpcServer * server,
                                                    GstWin32IpcLeakyType leaky);

guint64             gst_win32_ipc_server_get_current_level_buffers (GstWin32IpcServer * server);


G_END_DECLS
