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

#include <windows.h>
#include <vector>
#include <string>

enum class GstWin32IpcPktType : UINT32
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
struct GstWin32IpcPktHdr
{
  UINT64 magic;
  GstWin32IpcPktType type;
  UINT32 payload_size;
};
#pragma pack(pop)

const char *     gst_win32_ipc_pkt_type_to_string  (GstWin32IpcPktType type);

GstWin32IpcPktType  gst_win32_ipc_pkt_type_from_raw   (UINT32 type);

UINT32           gst_win32_ipc_pkt_type_to_raw     (GstWin32IpcPktType type);

bool             gst_win32_ipc_pkt_identify        (std::vector<UINT8> & buf,
                                                    GstWin32IpcPktHdr & header);

bool             gst_win32_ipc_pkt_build_config    (std::vector<UINT8> & buf,
                                                    DWORD pid,
                                                    const std::string & caps);

bool             gst_win32_ipc_pkt_parse_config    (const std::vector<UINT8> & buf,
                                                    DWORD & pid,
                                                    std::string & caps);

bool             gst_win32_ipc_pkt_build_need_data (std::vector<UINT8> & buf);

bool             gst_win32_ipc_pkt_build_have_data (std::vector<UINT8> & buf,
                                                    SIZE_T mmf_size,
                                                    UINT64 pts,
                                                    UINT64 dts,
                                                    UINT64 dur,
                                                    UINT buf_flags,
                                                    const HANDLE handle,
                                                    const char * caps,
                                                    const std::vector<UINT8> & meta);

bool             gst_win32_ipc_pkt_parse_have_data (const std::vector<UINT8> & buf,
                                                    SIZE_T & mmf_size,
                                                    UINT64 & pts,
                                                    UINT64 & dts,
                                                    UINT64 & dur,
                                                    UINT & buf_flags,
                                                    HANDLE & handle,
                                                    std::string & caps,
                                                    std::vector<UINT8> & meta);

bool             gst_win32_ipc_pkt_build_read_done (std::vector<UINT8> & buf);

bool             gst_win32_ipc_pkt_build_release_data (std::vector<UINT8> & buf,
                                                   const HANDLE handle);

bool             gst_win32_ipc_pkt_parse_release_data (const std::vector<UINT8> & buf,
                                                   HANDLE & handle);

bool             gst_win32_ipc_pkt_build_eos (std::vector<UINT8> & buf);

bool             gst_win32_ipc_pkt_build_fin (std::vector<UINT8> & buf);