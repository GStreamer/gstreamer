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

#include "gsthip-enums.h"
#include <mutex>

GType
gst_hip_vendor_get_type (void)
{
  static std::once_flag once;
  static GType type = 0;
  static const GEnumValue vendor[] = {
    {GST_HIP_VENDOR_UNKNOWN, "Unknown", "unknown"},
    {GST_HIP_VENDOR_AMD, "AMD", "amd"},
    {GST_HIP_VENDOR_NVIDIA, "NVIDIA", "nvidia"},
    {0, nullptr, nullptr},
  };

  std::call_once (once,[&]() {
        type = g_enum_register_static ("GstHipVendor", vendor);
      });

  return type;
}
