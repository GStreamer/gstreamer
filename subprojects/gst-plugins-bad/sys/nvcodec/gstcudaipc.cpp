/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcudaipc.h"
#include <gst/cuda/gstcuda-private.h>
#include <string.h>
#include <cctype>
#include <locale>
#include <codecvt>
#include <algorithm>
#include <mutex>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#define GST_CUDA_IPC_MAGIC_NUMBER 0xC0DA10C0

constexpr guint GST_CUDA_IPC_PKT_HAVE_DATA_PAYLOAD_MIN_SIZE =
    sizeof (GstClockTime) + sizeof (CUipcMemHandle) +
    sizeof (GstCudaIpcMemLayout) + sizeof (guint8);
constexpr guint GST_CUDA_IPC_PKT_RELEASE_DATA_PAYLOAD_SIZE =
    sizeof (CUipcMemHandle);

bool
gst_cuda_ipc_pkt_identify (std::vector < guint8 > &buf,
    GstCudaIpcPacketHeader & header)
{
  g_return_val_if_fail (buf.size () >= GST_CUDA_IPC_PKT_HEADER_SIZE, false);

  memcpy (&header, &buf[0], GST_CUDA_IPC_PKT_HEADER_SIZE);

  if (header.magic != GST_CUDA_IPC_MAGIC_NUMBER)
    return false;

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE + header.payload_size);

  return true;
}

bool
gst_cuda_ipc_pkt_build_config (std::vector < guint8 > &buf,
    GstCudaPid pid, gboolean use_mmap, GstCaps * caps)
{
  GstCudaIpcPacketHeader header;
  guint8 *ptr;
  gchar *caps_str = nullptr;
  guint caps_size = 0;

  g_return_val_if_fail (GST_IS_CAPS (caps), false);

  caps_str = gst_caps_serialize (caps, GST_SERIALIZE_FLAG_NONE);
  if (!caps_str)
    return false;

  caps_size = strlen (caps_str) + 1;

  header.type = GstCudaIpcPktType::CONFIG;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = sizeof (GstCudaPid) + sizeof (gboolean) + caps_size;

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE + header.payload_size);

  ptr = &buf[0];

  memcpy (ptr, &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  *((GstCudaPid *) ptr) = pid;
  ptr += sizeof (GstCudaPid);

  *((gboolean *) ptr) = use_mmap;
  ptr += sizeof (gboolean);

  strcpy ((char *) ptr, caps_str);
  g_free (caps_str);

  return true;
}

bool
gst_cuda_ipc_pkt_parse_config (std::vector < guint8 > &buf, GstCudaPid * pid,
    gboolean * use_mmap, GstCaps ** caps)
{
  GstCudaIpcPacketHeader header;
  const guint8 *ptr;
  std::string str;

  g_return_val_if_fail (buf.size () > GST_CUDA_IPC_PKT_HEADER_SIZE, false);
  g_return_val_if_fail (caps, false);

  ptr = &buf[0];
  memcpy (&header, ptr, GST_CUDA_IPC_PKT_HEADER_SIZE);

  if (header.type != GstCudaIpcPktType::CONFIG ||
      header.magic != GST_CUDA_IPC_MAGIC_NUMBER ||
      header.payload_size <= sizeof (GstCudaPid) + sizeof (gboolean)) {
    return false;
  }

  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  memcpy (pid, ptr, sizeof (GstCudaPid));
  ptr += sizeof (GstCudaPid);

  memcpy (use_mmap, ptr, sizeof (gboolean));
  ptr += sizeof (gboolean);

  *caps = gst_caps_from_string ((const gchar *) ptr);
  if (*caps == nullptr)
    return false;

  return true;
}

void
gst_cuda_ipc_pkt_build_need_data (std::vector < guint8 > &buf)
{
  GstCudaIpcPacketHeader header;

  header.type = GstCudaIpcPktType::NEED_DATA;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = 0;

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE);

  memcpy (&buf[0], &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
}

/* *INDENT-OFF* */
bool
gst_cuda_ipc_pkt_build_have_data (std::vector < guint8 > &buf, GstClockTime pts,
    const GstVideoInfo & info, const CUipcMemHandle & handle, GstCaps * caps)
{
  GstCudaIpcPacketHeader header;
  GstCudaIpcMemLayout layout;
  guint8 *ptr;
  gchar *caps_str = nullptr;
  guint caps_size = 1;

  if (caps) {
    caps_str = gst_caps_serialize (caps, GST_SERIALIZE_FLAG_NONE);
    if (!caps_str)
      return false;

    caps_size += strlen (caps_str) + 1;
  }

  header.type = GstCudaIpcPktType::HAVE_DATA;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = sizeof (GstClockTime) + sizeof (CUipcMemHandle) +
    sizeof (GstCudaIpcMemLayout) + caps_size;

  layout.size = layout.max_size = info.size;
  layout.pitch = info.stride[0];
  for (guint i = 0; i < 4; i++)
    layout.offset[i] = info.offset[i];

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE + header.payload_size);

  ptr = &buf[0];
  memcpy (ptr, &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  memcpy (ptr, &pts, sizeof (GstClockTime));
  ptr += sizeof (GstClockTime);

  memcpy (ptr, &layout, sizeof (GstCudaIpcMemLayout));
  ptr += sizeof (GstCudaIpcMemLayout);

  memcpy (ptr, &handle, sizeof (CUipcMemHandle));
  ptr += sizeof (CUipcMemHandle);

  if (caps) {
    *ptr = 1;
    ptr++;

    strcpy ((char *) ptr, caps_str);
  } else {
    *ptr = 0;
  }

  g_free (caps_str);

  return true;
}
/* *INDENT-ON* */

bool
gst_cuda_ipc_pkt_parse_have_data (const std::vector < guint8 > &buf,
    GstClockTime & pts, GstCudaIpcMemLayout & layout, CUipcMemHandle & handle,
    GstCaps ** caps)
{
  GstCudaIpcPacketHeader header;
  const guint8 *ptr;
  std::string str;

  g_return_val_if_fail (buf.size () >=
      GST_CUDA_IPC_PKT_HEADER_SIZE +
      GST_CUDA_IPC_PKT_HAVE_DATA_PAYLOAD_MIN_SIZE, false);
  g_return_val_if_fail (caps, false);

  ptr = &buf[0];
  memcpy (&header, ptr, GST_CUDA_IPC_PKT_HEADER_SIZE);

  if (header.type != GstCudaIpcPktType::HAVE_DATA ||
      header.magic != GST_CUDA_IPC_MAGIC_NUMBER ||
      header.payload_size < GST_CUDA_IPC_PKT_HAVE_DATA_PAYLOAD_MIN_SIZE) {
    return false;
  }
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  memcpy (&pts, ptr, sizeof (GstClockTime));
  ptr += sizeof (GstClockTime);

  memcpy (&layout, ptr, sizeof (GstCudaIpcMemLayout));
  ptr += sizeof (GstCudaIpcMemLayout);

  memcpy (&handle, ptr, sizeof (CUipcMemHandle));
  ptr += sizeof (CUipcMemHandle);

  if (*ptr) {
    ptr++;

    *caps = gst_caps_from_string ((const gchar *) ptr);
    if (*caps == nullptr)
      return false;
  }

  return true;
}

/* *INDENT-OFF* */
bool
gst_cuda_ipc_pkt_build_have_mmap_data (std::vector<guint8> & buf,
    GstClockTime pts, const GstVideoInfo & info, guint32 max_size,
    GstCudaSharableHandle handle, GstCaps * caps)
{
  GstCudaIpcPacketHeader header;
  GstCudaIpcMemLayout layout;
  guint8 *ptr;
  gchar *caps_str = nullptr;
  guint caps_size = 1;

  if (caps) {
    caps_str = gst_caps_serialize (caps, GST_SERIALIZE_FLAG_NONE);
    if (!caps_str)
      return false;

    caps_size = strlen (caps_str) + 1;
  }

  header.type = GstCudaIpcPktType::HAVE_MMAP_DATA;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = sizeof (GstClockTime) + sizeof (GstCudaIpcMemLayout) +
      sizeof (GstCudaSharableHandle) + caps_size;

  layout.size = info.size;
  layout.max_size = max_size;
  layout.pitch = info.stride[0];
  for (guint i = 0; i < 4; i++)
    layout.offset[i] = info.offset[i];

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE + header.payload_size);

  ptr = &buf[0];
  memcpy (ptr, &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  memcpy (ptr, &pts, sizeof (GstClockTime));
  ptr += sizeof (GstClockTime);

  memcpy (ptr, &layout, sizeof (GstCudaIpcMemLayout));
  ptr += sizeof (GstCudaIpcMemLayout);

  *((GstCudaSharableHandle *) ptr) = handle;
  ptr += sizeof (GstCudaSharableHandle);

  if (caps) {
    *ptr = 1;
    ptr++;

    strcpy ((char *) ptr, caps_str);
  } else {
    *ptr = 0;
  }

  g_free (caps_str);

  return true;
}
/* *INDENT-ON* */

bool
gst_cuda_ipc_pkt_parse_have_mmap_data (const std::vector < guint8 > &buf,
    GstClockTime & pts, GstCudaIpcMemLayout & layout,
    GstCudaSharableHandle * handle, GstCaps ** caps)
{
  GstCudaIpcPacketHeader header;
  const guint8 *ptr;
  std::string str;

  g_return_val_if_fail (buf.size () >
      GST_CUDA_IPC_PKT_HEADER_SIZE +
      sizeof (GstClockTime) + sizeof (GstCudaIpcMemLayout) +
      sizeof (GstCudaSharableHandle), false);
  g_return_val_if_fail (caps, false);

  ptr = &buf[0];
  memcpy (&header, ptr, GST_CUDA_IPC_PKT_HEADER_SIZE);

  if (header.type != GstCudaIpcPktType::HAVE_MMAP_DATA ||
      header.magic != GST_CUDA_IPC_MAGIC_NUMBER ||
      header.payload_size <=
      sizeof (GstClockTime) + sizeof (GstCudaIpcMemLayout) +
      sizeof (GstCudaSharableHandle)) {
    return false;
  }
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  memcpy (&pts, ptr, sizeof (GstClockTime));
  ptr += sizeof (GstClockTime);

  memcpy (&layout, ptr, sizeof (GstCudaIpcMemLayout));
  ptr += sizeof (GstCudaIpcMemLayout);

  *handle = *((GstCudaSharableHandle *) ptr);
  ptr += sizeof (GstCudaSharableHandle);

  if (*ptr) {
    ptr++;

    *caps = gst_caps_from_string ((const gchar *) ptr);
    if (*caps == nullptr)
      return false;
  }

  return true;
}

void
gst_cuda_ipc_pkt_build_read_done (std::vector < guint8 > &buf)
{
  GstCudaIpcPacketHeader header;

  header.type = GstCudaIpcPktType::READ_DONE;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = 0;

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE);

  memcpy (&buf[0], &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
}

void
gst_cuda_ipc_pkt_build_release_data (std::vector < guint8 > &buf,
    const CUipcMemHandle & handle)
{
  GstCudaIpcPacketHeader header;
  guint8 *ptr;

  header.type = GstCudaIpcPktType::RELEASE_DATA;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = sizeof (CUipcMemHandle);

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE +
      GST_CUDA_IPC_PKT_RELEASE_DATA_PAYLOAD_SIZE);

  ptr = &buf[0];
  memcpy (ptr, &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  memcpy (ptr, &handle, GST_CUDA_IPC_PKT_RELEASE_DATA_PAYLOAD_SIZE);
}

bool
gst_cuda_ipc_pkt_parse_release_data (std::vector < guint8 > &buf,
    CUipcMemHandle & handle)
{
  g_return_val_if_fail (buf.size () >=
      GST_CUDA_IPC_PKT_HEADER_SIZE + GST_CUDA_IPC_PKT_RELEASE_DATA_PAYLOAD_SIZE,
      false);

  memcpy (&handle, &buf[0] + GST_CUDA_IPC_PKT_HEADER_SIZE,
      GST_CUDA_IPC_PKT_RELEASE_DATA_PAYLOAD_SIZE);

  return true;
}

void
gst_cuda_ipc_pkt_build_release_mmap_data (std::vector < guint8 > &buf,
    GstCudaSharableHandle handle)
{
  GstCudaIpcPacketHeader header;
  guint8 *ptr;

  header.type = GstCudaIpcPktType::RELEASE_MMAP_DATA;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = sizeof (GstCudaSharableHandle);

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE + header.payload_size);

  ptr = &buf[0];
  memcpy (ptr, &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  *((GstCudaSharableHandle *) ptr) = handle;
}

bool
gst_cuda_ipc_pkt_parse_release_mmap_data (std::vector < guint8 > &buf,
    GstCudaSharableHandle * handle)
{
  guint8 *ptr;

  g_return_val_if_fail (buf.size () >=
      GST_CUDA_IPC_PKT_HEADER_SIZE + sizeof (GstCudaSharableHandle), false);

  ptr = &buf[0];
  ptr += GST_CUDA_IPC_PKT_HEADER_SIZE;

  *handle = *((GstCudaSharableHandle *) ptr);

  return true;
}

void
gst_cuda_ipc_pkt_build_eos (std::vector < guint8 > &buf)
{
  GstCudaIpcPacketHeader header;

  header.type = GstCudaIpcPktType::EOS;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = 0;

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE);

  memcpy (&buf[0], &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
}

void
gst_cuda_ipc_pkt_build_fin (std::vector < guint8 > &buf)
{
  GstCudaIpcPacketHeader header;

  header.type = GstCudaIpcPktType::FIN;
  header.magic = GST_CUDA_IPC_MAGIC_NUMBER;
  header.payload_size = 0;

  buf.resize (GST_CUDA_IPC_PKT_HEADER_SIZE);

  memcpy (&buf[0], &header, GST_CUDA_IPC_PKT_HEADER_SIZE);
}

std::string
gst_cuda_ipc_mem_handle_to_string (const CUipcMemHandle & handle)
{
  std::string dump (68, '\0');
  guint val[16];
  for (guint i = 0; i < 16; i++) {
    val[i] = *((guint *) (handle.reserved + i * 4));
  }

  sprintf (&dump[0], "%x%x%x%x-%x%x%x%x-%x%x%x%x-%x%x%x%x", val[0],
      val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8],
      val[9], val[10], val[11], val[12], val[13], val[14], val[15]);

  return dump;
}

bool
gst_cuda_ipc_clock_is_system (GstClock * clock)
{
  GstClockType clock_type = GST_CLOCK_TYPE_MONOTONIC;
  GstClock *mclock;

  if (G_OBJECT_TYPE (clock) != GST_TYPE_SYSTEM_CLOCK)
    return false;

  g_object_get (clock, "clock-type", &clock_type, nullptr);
  if (clock_type != GST_CLOCK_TYPE_MONOTONIC)
    return false;

  mclock = gst_clock_get_master (clock);
  if (!mclock)
    return true;

  gst_object_unref (mclock);
  return false;
}

#ifdef G_OS_WIN32
/* *INDENT-OFF* */
static inline void rtrim(std::string &s) {
  s.erase (std::find_if (s.rbegin(), s.rend(),
      [](unsigned char ch) {
        return !std::isspace (ch);
      }).base (), s.end ());
}
/* *INDENT-ON* */

std::string
gst_cuda_ipc_win32_error_to_string (guint err)
{
  wchar_t buffer[1024];

  if (!FormatMessageW (FORMAT_MESSAGE_IGNORE_INSERTS |
          FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, buffer, 1024, nullptr)) {
    return std::string ("");
  }

  std::wstring_convert < std::codecvt_utf8 < wchar_t >, wchar_t >converter;
  std::string ret = converter.to_bytes (buffer);
  rtrim (ret);

  return ret;
}
#else
std::string
gst_cuda_ipc_win32_error_to_string (guint err)
{
  return std::string ("");
}
#endif /* G_OS_WIN32 */

bool
gst_cuda_ipc_handle_is_equal (const CUipcMemHandle & handle,
    const CUipcMemHandle & other)
{
  if (memcmp (&handle, &other, sizeof (CUipcMemHandle)) == 0)
    return true;

  return false;
}
