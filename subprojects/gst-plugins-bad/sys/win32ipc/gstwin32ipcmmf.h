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
#include <windows.h>

G_BEGIN_DECLS

struct GstWin32IpcMmf;

GstWin32IpcMmf * gst_win32_ipc_mmf_alloc (SIZE_T size);

GstWin32IpcMmf * gst_win32_ipc_mmf_open  (SIZE_T size,
                                          HANDLE file);

SIZE_T        gst_win32_ipc_mmf_get_size (GstWin32IpcMmf * mmf);

void *        gst_win32_ipc_mmf_get_raw  (GstWin32IpcMmf * mmf);

HANDLE        gst_win32_ipc_mmf_get_handle (GstWin32IpcMmf * mmf);

GstWin32IpcMmf * gst_win32_ipc_mmf_ref      (GstWin32IpcMmf * mmf);

void          gst_win32_ipc_mmf_unref    (GstWin32IpcMmf * mmf);

G_END_DECLS
