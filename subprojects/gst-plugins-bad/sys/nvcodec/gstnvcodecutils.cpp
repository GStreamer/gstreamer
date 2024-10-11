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

#include "gstnvcodecutils.h"
#include <gst/cuda/gstcuda-private.h>

gboolean
gst_nvcodec_is_windows_10_or_greater (void)
{
  static gboolean ret = FALSE;

#ifdef G_OS_WIN32
  GST_CUDA_CALL_ONCE_BEGIN {
    ret = g_win32_check_windows_version (10, 0, 0, G_WIN32_OS_ANY);
  } GST_CUDA_CALL_ONCE_END;
#endif

  return ret;
}
