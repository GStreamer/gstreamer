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

#include "gstwin32ipcmmf.h"
#include <string>

GST_DEBUG_CATEGORY_EXTERN (gst_win32_ipc_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_debug

struct GstWin32IpcMmf
{
  explicit GstWin32IpcMmf (HANDLE f, void *b, SIZE_T s)
  : file (f), buffer (b), size (s), ref_count (1)
  {
  }

   ~GstWin32IpcMmf ()
  {
    GST_TRACE ("Freeing %p", this);

    if (buffer)
      UnmapViewOfFile (buffer);
    if (file)
      CloseHandle (file);
  }

  HANDLE file;
  void *buffer;
  SIZE_T size;
  ULONG ref_count;
};

static GstWin32IpcMmf *
gst_win32_ipc_mmf_new (HANDLE file, SIZE_T size)
{
  auto buffer = MapViewOfFile (file, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!buffer) {
    auto err_code = GetLastError ();
    auto msg = g_win32_error_message (err_code);
    GST_ERROR ("MapViewOfFile failed with 0x%x (%s)", (guint) err_code, msg);
    g_free (msg);
    CloseHandle (file);
    return nullptr;
  }

  return new GstWin32IpcMmf (file, buffer, size);
}

/**
 * gst_win32_ipc_mmf_alloc:
 * @size: Size of memory to allocate
 *
 * Creates shared memory
 *
 * Returns: a new GstWin32IpcMmf object
 */
GstWin32IpcMmf *
gst_win32_ipc_mmf_alloc (SIZE_T size)
{
  if (!size) {
    GST_ERROR ("Zero size is not allowed");
    return nullptr;
  }

  ULARGE_INTEGER alloc_size;
  alloc_size.QuadPart = size;

  auto file = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr,
      PAGE_READWRITE | SEC_COMMIT, alloc_size.HighPart, alloc_size.LowPart,

      nullptr);
  if (!file) {
    auto err_code = GetLastError ();
    auto msg = g_win32_error_message (err_code);
    GST_ERROR ("CreateFileMappingW failed with 0x%x (%s)",
        (guint) err_code, msg);
    g_free (msg);
    return nullptr;
  }

  return gst_win32_ipc_mmf_new (file, size);
}

/**
 * gst_win32_ipc_mmf_open:
 * @size: Size of memory to allocate
 * @file: (transfer full): File mapping handle
 *
 * Opens named shared memory
 *
 * Returns: a new GstWin32IpcMmf object
 */
GstWin32IpcMmf *
gst_win32_ipc_mmf_open (SIZE_T size, HANDLE file)
{
  if (!size) {
    GST_ERROR ("Zero size is not allowed");

    if (file)
      CloseHandle (file);

    return nullptr;
  }

  return gst_win32_ipc_mmf_new (file, size);
}

/**
 * gst_win32_ipc_mmf_get_size:
 * @mmf: a GstWin32IpcMmf object
 *
 * Returns: the size of allocated memory
 */
SIZE_T
gst_win32_ipc_mmf_get_size (GstWin32IpcMmf * mmf)
{
  if (!mmf)
    return 0;

  return mmf->size;
}

/**
 * gst_win32_ipc_mmf_get_raw:
 * @mmf: a GstWin32IpcMmf object
 *
 * Returns: the address of allocated memory
 */
void *
gst_win32_ipc_mmf_get_raw (GstWin32IpcMmf * mmf)
{
  if (!mmf)
    return nullptr;

  return mmf->buffer;
}

HANDLE
gst_win32_ipc_mmf_get_handle (GstWin32IpcMmf * mmf)
{
  if (!mmf)
    return nullptr;

  return mmf->file;
}

/**
 * gst_win32_ipc_mmf_ref:
 * @mmf: a GstWin32IpcMmf object
 *
 * Increase ref count
 */
GstWin32IpcMmf *
gst_win32_ipc_mmf_ref (GstWin32IpcMmf * mmf)
{
  if (!mmf)
    return nullptr;

  InterlockedIncrement (&mmf->ref_count);

  return mmf;
}

/**
 * gst_win32_ipc_mmf_unref:
 * @mmf: a GstWin32IpcMmf object
 *
 * Decrease ref count
 */
void
gst_win32_ipc_mmf_unref (GstWin32IpcMmf * mmf)
{
  ULONG count;

  if (!mmf)
    return;

  count = InterlockedDecrement (&mmf->ref_count);
  if (count == 0)
    delete mmf;
}
