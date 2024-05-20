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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvcomp.h"
#include <mutex>

static const GEnumValue nvcomp_methods[] = {
  {GST_NV_COMP_LZ4, "LZ4", "lz4"},
  {GST_NV_COMP_SNAPPY, "SNAPPY", "snappy"},
  {GST_NV_COMP_GDEFLATE, "GDEFLATE", "gdeflate"},
  {GST_NV_COMP_DEFLATE, "DEFLATE", "deflate"},
  {GST_NV_COMP_ZSTD, "ZSTD", "zstd"},
  {GST_NV_COMP_CASCADED, "CASCADED", "cascaded"},
  {GST_NV_COMP_BITCOMP, "BITCOMP", "bitcomp"},
  {GST_NV_COMP_ANS, "ANS", "ans"},
  {0, nullptr, nullptr},
};

GType
gst_nv_comp_method_get_type (void)
{
  static GType method_type = 0;
  static std::once_flag once;

  std::call_once (once,[&] {
        method_type = g_enum_register_static ("GstNvCompMethod",
            nvcomp_methods);
      });

  return method_type;
}

const gchar *
gst_nv_comp_method_to_string (GstNvCompMethod method)
{
  if (method >= GST_NV_COMP_LAST)
    return nullptr;

  return nvcomp_methods[method].value_nick;
}
