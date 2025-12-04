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

#include "gstwin32ipcprotocol.h"
#include <string.h>

constexpr UINT32 WIPC_TAG = 0x43504957u;        /* WIPC */
#define WIN32_IPC_VERSION 0x01u

#ifdef _WIN64
#define WIPC_IS_64BIT 1u
#else
#define WIPC_IS_64BIT 0u
#endif

#define WIN32_IPC_MAGIC64 \
    ( (UINT64(WIPC_TAG) << 32) | (UINT64(0) << 16) | (UINT64(WIPC_IS_64BIT) << 8) | UINT64(WIN32_IPC_VERSION) )

#define return_val_if_fail(expr, val) \
    do { \
      if (!(expr)) \
        return (val); \
    } while (0)

#define WRITE_TO(dst,src,size) \
  do { \
    memcpy (dst, src, size); \
    dst += size; \
  } while (0)

#define READ_FROM(dst,src,size) \
  do { \
    memcpy (dst, src, size); \
    src += size; \
  } while (0)

const char *
gst_win32_ipc_pkt_type_to_string (GstWin32IpcPktType type)
{
  switch (type) {
    case GstWin32IpcPktType::CONFIG:
      return "CONFIG";
    case GstWin32IpcPktType::NEED_DATA:
      return "NEED-DATA";
    case GstWin32IpcPktType::HAVE_DATA:
      return "HAVE-DATA";
    case GstWin32IpcPktType::READ_DONE:
      return "READ-DONE";
    case GstWin32IpcPktType::RELEASE_DATA:
      return "RELEASE-DATA";
    case GstWin32IpcPktType::EOS:
      return "EOS";
    case GstWin32IpcPktType::FIN:
      return "FIN";
    default:
      break;
  }

  return "Unknown";
}

GstWin32IpcPktType
gst_win32_ipc_pkt_type_from_raw (UINT32 type)
{
  return (GstWin32IpcPktType) type;
}

UINT32
gst_win32_ipc_pkt_type_to_raw (GstWin32IpcPktType type)
{
  return (UINT32) type;

}

struct PtrPos
{
  PtrPos (std::vector < UINT8 > &buf)
  {
    data = buf.data ();
    remaining = buf.size ();
  };

  PtrPos (const std::vector < UINT8 > &buf)
  {
    data = (UINT8 *) buf.data ();
    remaining = buf.size ();
  };

  UINT8 *data;
  SIZE_T remaining;
};

static inline bool
write_to (PtrPos & p, const void *src, SIZE_T size)
{
  if (p.remaining < size)
    return false;

  if (size == 0)
    return true;

  memcpy (p.data, src, size);
  p.data += size;
  p.remaining -= size;

  return true;
}

static inline bool
read_from (PtrPos & p, void *dst, SIZE_T size)
{
  if (p.remaining < size)
    return false;

  if (size == 0)
    return true;

  memcpy (dst, p.data, size);
  p.data += size;
  p.remaining -= size;

  return true;
}

static inline bool
assign_from (PtrPos & p, std::string & dst, SIZE_T size)
{
  if (p.remaining < size)
    return false;

  if (size > 0)
    dst.assign ((const char *) p.data, size);
  else
    dst.clear ();

  p.data += size;
  p.remaining -= size;

  return true;
}

#define WRITE_TO_T(p,s,type) \
  do { \
    if (!write_to (p, s, sizeof (type))) \
      return false; \
  } while (0)

#define WRITE_TO_S(p,s,size) \
  do { \
    if (!write_to (p, s, size)) \
      return false; \
  } while (0)

#define READ_FROM_T(p,d,type) \
  do { \
    if (!read_from (p, d, sizeof (type))) \
      return false; \
  } while (0)

#define READ_FROM_S(p,d,size) \
  do { \
    if (!read_from (p, d, size)) \
      return false; \
  } while (0)

#define ASSIGN_FROM(p,d,size) \
  do { \
    if (!assign_from (p, d, size)) \
      return false; \
  } while (0)

bool
gst_win32_ipc_pkt_identify (std::vector < UINT8 > &buf, GstWin32IpcPktHdr & hdr)
{
  PtrPos ptr (buf);

  READ_FROM_T (ptr, &hdr, GstWin32IpcPktHdr);

  if (hdr.magic != WIN32_IPC_MAGIC64)
    return false;

  const SIZE_T need = sizeof (GstWin32IpcPktHdr) + hdr.payload_size;
  const SIZE_T MAX_PKT_SIZE = 1024 * 1024 * 64;

  if (need > MAX_PKT_SIZE)
    return false;

  buf.resize (need);

  return true;
}

bool
gst_win32_ipc_pkt_build_config (std::vector < UINT8 > &buf, DWORD pid,
    const std::string & caps)
{
  GstWin32IpcPktHdr hdr = { };
  hdr.type = GstWin32IpcPktType::CONFIG;
  hdr.magic = WIN32_IPC_MAGIC64;
  hdr.payload_size = sizeof (DWORD) + sizeof (SIZE_T) + caps.size ();

  buf.resize (sizeof (GstWin32IpcPktHdr) + hdr.payload_size);

  PtrPos ptr (buf);

  WRITE_TO_T (ptr, &hdr, GstWin32IpcPktHdr);
  WRITE_TO_T (ptr, &pid, DWORD);

  auto caps_len = caps.size ();
  WRITE_TO_T (ptr, &caps_len, SIZE_T);
  WRITE_TO_S (ptr, caps.c_str (), caps_len);

  return true;
}

bool
gst_win32_ipc_pkt_parse_config (const std::vector < UINT8 > &buf, DWORD & pid,
    std::string & caps)
{
  const SIZE_T min_payload_size = sizeof (DWORD) + sizeof (SIZE_T);

  return_val_if_fail (buf.size () >=
      sizeof (GstWin32IpcPktHdr) + min_payload_size, false);

  PtrPos ptr (buf);

  GstWin32IpcPktHdr hdr = { };
  READ_FROM_T (ptr, &hdr, GstWin32IpcPktHdr);

  if (hdr.type != GstWin32IpcPktType::CONFIG ||
      hdr.magic != WIN32_IPC_MAGIC64 || hdr.payload_size < min_payload_size) {
    return false;
  }

  READ_FROM_T (ptr, &pid, DWORD);

  SIZE_T size;
  READ_FROM_T (ptr, &size, SIZE_T);
  ASSIGN_FROM (ptr, caps, size);

  return true;
}

bool
gst_win32_ipc_pkt_build_need_data (std::vector < UINT8 > &buf)
{
  GstWin32IpcPktHdr hdr = { };
  hdr.type = GstWin32IpcPktType::NEED_DATA;
  hdr.magic = WIN32_IPC_MAGIC64;
  hdr.payload_size = 0;

  buf.resize (sizeof (GstWin32IpcPktHdr));

  memcpy (buf.data (), &hdr, sizeof (GstWin32IpcPktHdr));

  return true;
}

bool
gst_win32_ipc_pkt_build_have_data (std::vector < UINT8 > &buf, SIZE_T mmf_size,
    UINT64 pts, UINT64 dts, UINT64 dur, UINT buf_flags, const HANDLE handle,
    const char *caps, const std::vector < UINT8 > &meta)
{
  GstWin32IpcPktHdr hdr = { };
  SIZE_T caps_len = 0;
  hdr.type = GstWin32IpcPktType::HAVE_DATA;
  hdr.magic = WIN32_IPC_MAGIC64;
  hdr.payload_size = 0;

  /* mmf size */
  hdr.payload_size += sizeof (SIZE_T);

  /* pts/dts/dur */
  hdr.payload_size += (sizeof (UINT64) * 3);

  /* buffer flags */
  hdr.payload_size += sizeof (UINT);

  /* Server handle value */
  hdr.payload_size += sizeof (HANDLE);

  /* caps size */
  hdr.payload_size += sizeof (SIZE_T);

  /* caps data */
  if (caps) {
    caps_len = strlen (caps);
    hdr.payload_size += caps_len;
  }

  /* metadata size */
  hdr.payload_size += sizeof (SIZE_T);

  /* metadata */
  hdr.payload_size += meta.size ();

  buf.resize (sizeof (GstWin32IpcPktHdr) + hdr.payload_size);

  PtrPos ptr (buf);

  WRITE_TO_T (ptr, &hdr, GstWin32IpcPktHdr);
  WRITE_TO_T (ptr, &mmf_size, SIZE_T);
  WRITE_TO_T (ptr, &pts, UINT64);
  WRITE_TO_T (ptr, &dts, UINT64);
  WRITE_TO_T (ptr, &dur, UINT64);
  WRITE_TO_T (ptr, &buf_flags, UINT);
  WRITE_TO_T (ptr, &handle, HANDLE);

  WRITE_TO_T (ptr, &caps_len, SIZE_T);
  if (caps_len)
    WRITE_TO_S (ptr, caps, caps_len);

  auto size = meta.size ();
  WRITE_TO_T (ptr, &size, SIZE_T);
  WRITE_TO_S (ptr, meta.data (), size);

  return true;
}

bool
gst_win32_ipc_pkt_parse_have_data (const std::vector < UINT8 > &buf,
    SIZE_T & mmf_size, UINT64 & pts, UINT64 & dts, UINT64 & dur,
    UINT & buf_flags, HANDLE & handle, std::string & caps,
    std::vector < UINT8 > &meta)
{
  const SIZE_T min_payload_size = sizeof (SIZE_T) + (sizeof (UINT64) * 3) +
      sizeof (UINT) + sizeof (HANDLE) + sizeof (SIZE_T) + sizeof (SIZE_T);

  return_val_if_fail (buf.size () >=
      sizeof (GstWin32IpcPktHdr) + min_payload_size, false);

  PtrPos ptr (buf);

  GstWin32IpcPktHdr hdr = { };
  READ_FROM_T (ptr, &hdr, GstWin32IpcPktHdr);

  if (hdr.type != GstWin32IpcPktType::HAVE_DATA ||
      hdr.magic != WIN32_IPC_MAGIC64 || hdr.payload_size < min_payload_size) {
    return false;
  }

  READ_FROM_T (ptr, &mmf_size, SIZE_T);
  READ_FROM_T (ptr, &pts, UINT64);
  READ_FROM_T (ptr, &dts, UINT64);
  READ_FROM_T (ptr, &dur, UINT64);
  READ_FROM_T (ptr, &buf_flags, UINT);
  READ_FROM_T (ptr, &handle, HANDLE);

  SIZE_T size;
  READ_FROM_T (ptr, &size, SIZE_T);
  ASSIGN_FROM (ptr, caps, size);

  READ_FROM_T (ptr, &size, SIZE_T);
  meta.resize (size);

  READ_FROM_S (ptr, meta.data (), size);

  return true;
}

bool
gst_win32_ipc_pkt_build_read_done (std::vector < UINT8 > &buf)
{
  GstWin32IpcPktHdr hdr = { };
  hdr.type = GstWin32IpcPktType::READ_DONE;
  hdr.magic = WIN32_IPC_MAGIC64;
  hdr.payload_size = 0;

  buf.resize (sizeof (GstWin32IpcPktHdr));

  memcpy (buf.data (), &hdr, sizeof (GstWin32IpcPktHdr));

  return true;
}

bool
gst_win32_ipc_pkt_build_release_data (std::vector < UINT8 > &buf,
    const HANDLE handle)
{
  GstWin32IpcPktHdr hdr = { };
  hdr.type = GstWin32IpcPktType::RELEASE_DATA;
  hdr.magic = WIN32_IPC_MAGIC64;
  hdr.payload_size = sizeof (HANDLE);

  buf.resize (sizeof (GstWin32IpcPktHdr) + hdr.payload_size);

  PtrPos ptr (buf);
  WRITE_TO_T (ptr, &hdr, GstWin32IpcPktHdr);
  WRITE_TO_T (ptr, &handle, HANDLE);

  return true;
}

bool
gst_win32_ipc_pkt_parse_release_data (const std::vector < UINT8 > &buf,
    HANDLE & handle)
{
  return_val_if_fail (buf.size () >=
      sizeof (GstWin32IpcPktHdr) + sizeof (HANDLE), false);

  PtrPos ptr (buf);

  GstWin32IpcPktHdr hdr = { };
  READ_FROM_T (ptr, &hdr, GstWin32IpcPktHdr);

  if (hdr.type != GstWin32IpcPktType::RELEASE_DATA ||
      hdr.magic != WIN32_IPC_MAGIC64 || hdr.payload_size != sizeof (HANDLE)) {
    return false;
  }

  READ_FROM_T (ptr, &handle, HANDLE);

  return true;
}

bool
gst_win32_ipc_pkt_build_eos (std::vector < UINT8 > &buf)
{
  GstWin32IpcPktHdr hdr = { };
  hdr.type = GstWin32IpcPktType::EOS;
  hdr.magic = WIN32_IPC_MAGIC64;
  hdr.payload_size = 0;

  buf.resize (sizeof (GstWin32IpcPktHdr));

  memcpy (buf.data (), &hdr, sizeof (GstWin32IpcPktHdr));

  return true;
}

bool
gst_win32_ipc_pkt_build_fin (std::vector < UINT8 > &buf)
{
  GstWin32IpcPktHdr hdr = { };
  hdr.type = GstWin32IpcPktType::FIN;
  hdr.magic = WIN32_IPC_MAGIC64;
  hdr.payload_size = 0;

  buf.resize (sizeof (GstWin32IpcPktHdr));

  memcpy (buf.data (), &hdr, sizeof (GstWin32IpcPktHdr));

  return true;
}
