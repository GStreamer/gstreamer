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

#include <gst/d3d11/gstd3d11.h>
#include "gstdwrite-utils.h"

gboolean
gst_dwrite_is_windows_10_or_greater (void)
{
  static gboolean ret = FALSE;

  GST_DWRITE_CALL_ONCE_BEGIN {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    OSVERSIONINFOEXW osverinfo;
    typedef NTSTATUS (WINAPI fRtlGetVersion) (PRTL_OSVERSIONINFOEXW);
    fRtlGetVersion *RtlGetVersion = NULL;
    HMODULE hmodule = NULL;

    memset (&osverinfo, 0, sizeof (OSVERSIONINFOEXW));
    osverinfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);

    hmodule = LoadLibraryW (L"ntdll.dll");
    if (!hmodule)
      return;

    RtlGetVersion =
        (fRtlGetVersion *) GetProcAddress (hmodule, "RtlGetVersion");
    if (RtlGetVersion) {
      RtlGetVersion (&osverinfo);
      if (osverinfo.dwMajorVersion > 10 || osverinfo.dwMajorVersion == 10)
        ret = TRUE;
    }

    if (hmodule)
      FreeLibrary (hmodule);
#else
    ret = TRUE;
#endif
  } GST_DWRITE_CALL_ONCE_END;

  return ret;
}
