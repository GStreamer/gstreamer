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

#pragma once

#include <gst/gst.h>

#define GST_DWRITE_TEXT_META_NAME "GstDWriteTextMeta"

#define GST_DWRITE_CAPS \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "," \
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, \
        GST_D3D11_ALL_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, \
        GST_D3D11_ALL_FORMATS) "; " \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY "," \
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, GST_VIDEO_FORMATS_ALL) ";" \
    GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)

G_BEGIN_DECLS

gboolean gst_dwrite_is_windows_10_or_greater (void);

G_END_DECLS

#ifdef __cplusplus
#include <string>
#include <mutex>

static inline std::wstring
gst_dwrite_string_to_wstring (const std::string & str)
{
  auto tmp = g_utf8_to_utf16 (str.c_str (), -1, nullptr, nullptr, nullptr);
  if (!tmp)
    return std::wstring ();

  std::wstring ret = (wchar_t *) tmp;
  g_free (tmp);

  return ret;
}

static inline std::string
gst_dwrite_wstring_to_string (const std::wstring & str)
{
  auto tmp = g_utf16_to_utf8 ((const gunichar2 *) str.c_str (),
      -1, nullptr, nullptr, nullptr);
  if (!tmp)
    return std::string ();

  std::string ret = tmp;
  g_free (tmp);

  return ret;
}

#define GST_DWRITE_CALL_ONCE_BEGIN \
    static std::once_flag __once_flag; \
    std::call_once (__once_flag, [&]()

#define GST_DWRITE_CALL_ONCE_END )

#endif /* __cplusplus */
