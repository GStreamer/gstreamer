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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwin32ipc.h"
#include <mutex>

/**
 * GstWin32IpcLeakyType:
 *
 * Since: 1.28
 */
GType
gst_win32_ipc_leaky_type_get_type (void)
{
  static GType type = 0;
  static std::once_flag once;
  static const GEnumValue leaky_types[] = {
    {GST_WIN32_IPC_LEAKY_NONE, "None", "none"},
    {GST_WIN32_IPC_LEAKY_UPSTREAM, "Upstream", "upstream"},
    {GST_WIN32_IPC_LEAKY_DOWNSTREAM, "Downstream", "downstream"},
    {0, nullptr, nullptr},
  };

  std::call_once (once,[&] {
        type = g_enum_register_static ("GstWin32IpcLeakyType", leaky_types);
      });

  return type;
}
