/*
 * Copyright (C) 2023, Ratchanan Srirattanamet <peathot@hotmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#include "../gstamc.h"
#include "gstamc-jni.h"
#include "../ndk/gstamc-ndk.h"

#define GST_CAT_DEFAULT gst_amc_debug

gboolean
gst_amc_static_init (void)
{
  if (!gst_amc_codeclist_jni_static_init ())
    return FALSE;

  if (!gst_amc_surface_texture_jni_static_init ())
    return FALSE;

  if (!gst_amc_codec_ndk_static_init ())
    return FALSE;

  if (!gst_amc_format_ndk_static_init ())
    return FALSE;

  if (g_strcmp0 (g_getenv ("GST_AMC_PREFERED_IMPL"), "jni") == 0)
    GST_WARNING ("GST_AMC_PREFERED_IMPL is no longer used, only ndk is used");

  gst_amc_format_vtable = &gst_amc_format_ndk_vtable;
  gst_amc_codec_vtable = &gst_amc_codec_ndk_vtable;

  return TRUE;
}
