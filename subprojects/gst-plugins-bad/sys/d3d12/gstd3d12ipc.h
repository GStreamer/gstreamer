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
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12.h>
#include <string>
#include <vector>
#include <windows.h>

/*
 * Communication Sequence
 *
 *              +--------+                      +--------+
 *              | client |                      | server |
 *              +--------+                      +--------+
 *                  |                               |
 *                  |                               |
 *                  |<---------- CONFIG ------------+
 *                  |                               |
 *                  +--------- NEED-DATA ---------->|
 *                  |                               +-------+
 *                  |                               |     Export
 *                  |                               |   D3D12 memory
 *                  |                               |<------+
 *                  |<-------- HAVE-DATA -----------+
 *         +--------+                               |
 *       Import     |                               |
 *    D3D12 memory  |                               |
 *         +------->+                               |
 *                  +--------- READ-DONE ---------->|
 *         +--------+                               |
 *      Release     |                               |
 *   D3D12 memory   |                               |
 *         +------->|                               |
 *                  +-------- RELEASE-DATA -------->|
 *                  |                               |
 *                  +--------- NEED-DATA ---------->|
 *                  |                               |
 *                  |<----------- EOS --------------+
 *         +--------+                               |
 *    Cleanup all   |                               |
 * shared resources |                               |
 *         +------->|                               |
 *                  +------------ FIN ------------->|
 */

enum class GstD3D12IpcPktType : guint8
{
  UNKNOWN,
  CONFIG,
  NEED_DATA,
  HAVE_DATA,
  READ_DONE,
  RELEASE_DATA,
  EOS,
  FIN,
};

#pragma pack(push, 1)
struct GstD3D12IpcPacketHeader
{
  GstD3D12IpcPktType type;
  guint32 payload_size;
  guint32 magic;
};

struct GstD3D12IpcMemLayout
{
  guint32 pitch;
  guint32 offset[4];
};
#pragma pack(pop)

constexpr guint GST_D3D12_IPC_PKT_HEADER_SIZE = sizeof (GstD3D12IpcPacketHeader);

#define GST_D3D12_IPC_FORMATS \
    "{ RGBA64_LE, RGB10A2_LE, BGRA, RGBA, BGRx, RGBx, VUYA, NV12, NV21, " \
    "P010_10LE, P012_LE, P016_LE }"

bool gst_d3d12_ipc_pkt_identify (std::vector<guint8> & buf,
                                 GstD3D12IpcPacketHeader & header);

bool gst_d3d12_ipc_pkt_build_config (std::vector<guint8> & buf,
                                     DWORD pid,
                                     gint64 adapter_luid,
                                     const HANDLE fence_handle,
                                     GstCaps * caps);

bool gst_d3d12_ipc_pkt_parse_config (std::vector<guint8> & buf,
                                     DWORD & pid,
                                     gint64 & adapter_luid,
                                     HANDLE & fence_handle,
                                     GstCaps ** caps);

void gst_d3d12_ipc_pkt_build_need_data (std::vector<guint8> & buf);

bool gst_d3d12_ipc_pkt_build_have_data (std::vector<guint8> & buf,
                                        GstClockTime pts,
                                        const GstD3D12IpcMemLayout & layout,
                                        const HANDLE handle,
                                        guint64 fence_value,
                                        GstCaps * caps);

bool gst_d3d12_ipc_pkt_parse_have_data (const std::vector<guint8> & buf,
                                        GstClockTime & pts,
                                        GstD3D12IpcMemLayout & layout,
                                        HANDLE & handle,
                                        guint64 & fence_value,
                                        GstCaps ** caps);

void gst_d3d12_ipc_pkt_build_read_done (std::vector<guint8> & buf);

void gst_d3d12_ipc_pkt_build_release_data (std::vector<guint8> & buf,
                                           const HANDLE handle);

bool gst_d3d12_ipc_pkt_parse_release_data (std::vector<guint8> & buf,
                                           HANDLE & handle);

void gst_d3d12_ipc_pkt_build_eos (std::vector<guint8> & buf);

void gst_d3d12_ipc_pkt_build_fin (std::vector<guint8> & buf);

bool gst_d3d12_ipc_clock_is_system (GstClock * clock);

std::string gst_d3d12_ipc_wstring_to_string (const std::wstring & str);

std::wstring gst_d3d12_ipc_string_to_wstring (const std::string & str);

std::string gst_d3d12_ipc_win32_error_to_string (guint err);

