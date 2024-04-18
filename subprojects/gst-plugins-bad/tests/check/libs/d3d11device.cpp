/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/d3d11/gstd3d11.h>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

static gboolean have_multiple_adapters = FALSE;

GST_START_TEST (test_device_new)
{
  GstD3D11Device *device = nullptr;
  guint adapter_index = G_MAXINT;

  device = gst_d3d11_device_new (0, 0);
  fail_unless (GST_IS_D3D11_DEVICE (device));

  g_object_get (device, "adapter", &adapter_index, nullptr);
  fail_unless_equals_int (adapter_index, 0);
  gst_clear_object (&device);

  if (have_multiple_adapters) {
    device = gst_d3d11_device_new (1, 0);
    fail_unless (GST_IS_D3D11_DEVICE (device));

    g_object_get (device, "adapter", &adapter_index, nullptr);
    fail_unless_equals_int (adapter_index, 1);
  }

  gst_clear_object (&device);
}

GST_END_TEST;

GST_START_TEST (test_device_for_adapter_luid)
{
  GstD3D11Device *device = nullptr;
  HRESULT hr;
  ComPtr < IDXGIAdapter1 > adapter;
  ComPtr < IDXGIFactory1 > factory;
  DXGI_ADAPTER_DESC desc;
  guint adapter_index = G_MAXINT;
  gint64 adapter_luid = 0;
  gint64 luid;

  hr = CreateDXGIFactory1 (IID_PPV_ARGS (&factory));
  if (SUCCEEDED (hr))
    hr = factory->EnumAdapters1 (0, &adapter);

  if (SUCCEEDED (hr))
    hr = adapter->GetDesc (&desc);

  if (SUCCEEDED (hr)) {
    luid = gst_d3d11_luid_to_int64 (&desc.AdapterLuid);
    device = gst_d3d11_device_new_for_adapter_luid (luid, 0);
    fail_unless (GST_IS_D3D11_DEVICE (device));

    g_object_get (device, "adapter", &adapter_index, "adapter-luid",
        &adapter_luid, nullptr);

    /* adapter_luid is corresponding to the first enumerated adapter,
     * so adapter index should be zero here */
    fail_unless_equals_int (adapter_index, 0);
    fail_unless_equals_int64 (adapter_luid, luid);
  }

  gst_clear_object (&device);
  adapter = nullptr;

  if (have_multiple_adapters) {
    if (SUCCEEDED (hr))
      hr = factory->EnumAdapters1 (1, &adapter);

    if (SUCCEEDED (hr))
      hr = adapter->GetDesc (&desc);

    if (SUCCEEDED (hr)) {
      luid = gst_d3d11_luid_to_int64 (&desc.AdapterLuid);
      device = gst_d3d11_device_new_for_adapter_luid (luid, 0);
      fail_unless (GST_IS_D3D11_DEVICE (device));

      g_object_get (device, "adapter", &adapter_index, "adapter-luid",
          &adapter_luid, nullptr);

      fail_unless_equals_int (adapter_index, 1);
      fail_unless_equals_int64 (adapter_luid, luid);
    }
  }

  gst_clear_object (&device);
}

GST_END_TEST;

GST_START_TEST (test_device_new_wrapped)
{
  GstD3D11Device *device = nullptr;
  GstD3D11Device *device_clone = nullptr;
  ID3D11Device *device_handle, *device_handle_clone;
  ID3D11DeviceContext *context_handle, *context_handle_clone;
  guint adapter_index = 0;
  guint index;
  gint64 luid, luid_clone;

  if (have_multiple_adapters)
    adapter_index = 1;

  device = gst_d3d11_device_new (adapter_index, 0);
  fail_unless (GST_IS_D3D11_DEVICE (device));

  device_handle = gst_d3d11_device_get_device_handle (device);
  fail_unless (device_handle != nullptr);

  context_handle = gst_d3d11_device_get_device_context_handle (device);
  fail_unless (context_handle != nullptr);

  g_object_get (device, "adapter", &index, "adapter-luid", &luid, nullptr);
  fail_unless_equals_int (index, adapter_index);

  device_clone = gst_d3d11_device_new_wrapped (device_handle);
  fail_unless (GST_IS_D3D11_DEVICE (device_clone));

  device_handle_clone = gst_d3d11_device_get_device_handle (device_clone);
  fail_unless_equals_pointer (device_handle, device_handle_clone);

  context_handle_clone =
      gst_d3d11_device_get_device_context_handle (device_clone);
  fail_unless_equals_pointer (context_handle, context_handle_clone);

  g_object_get (device_clone,
      "adapter", &index, "adapter-luid", &luid_clone, nullptr);
  fail_unless_equals_int (index, adapter_index);
  fail_unless_equals_int64 (luid, luid_clone);

  gst_clear_object (&device);
  gst_clear_object (&device_clone);
}

GST_END_TEST;

static gboolean
check_d3d11_available (void)
{
  HRESULT hr;
  ComPtr < IDXGIAdapter1 > adapter;
  ComPtr < IDXGIFactory1 > factory;

  hr = CreateDXGIFactory1 (IID_PPV_ARGS (&factory));
  if (FAILED (hr))
    return FALSE;

  hr = factory->EnumAdapters1 (0, &adapter);
  if (FAILED (hr))
    return FALSE;

  adapter = nullptr;
  hr = factory->EnumAdapters1 (1, &adapter);
  if (SUCCEEDED (hr))
    have_multiple_adapters = TRUE;

  return TRUE;
}

static gboolean stopping = FALSE;

static gpointer
test_device_new_concurrency_thread (gpointer)
{
  GstVideoInfo in_info, out_info;

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_I420, 320, 240);
  gst_video_info_set_format (&out_info, GST_VIDEO_FORMAT_RGBx, 1920, 1080);

  while (!g_atomic_int_get (&stopping)) {
    GstD3D11Converter *converter;
    GstD3D11Device *device = gst_d3d11_device_new (0, D3D11_CREATE_DEVICE_BGRA_SUPPORT);

    gst_d3d11_device_lock (device);
    converter = gst_d3d11_converter_new (device, &in_info, &out_info,
        gst_structure_new ("converter-config",
        GST_D3D11_CONVERTER_OPT_BACKEND, GST_TYPE_D3D11_CONVERTER_BACKEND,
        GST_D3D11_CONVERTER_BACKEND_SHADER,
        GST_D3D11_CONVERTER_OPT_GAMMA_MODE,
        GST_TYPE_VIDEO_GAMMA_MODE, GST_VIDEO_GAMMA_MODE_NONE,
        GST_D3D11_CONVERTER_OPT_PRIMARIES_MODE,
        GST_TYPE_VIDEO_PRIMARIES_MODE, GST_VIDEO_PRIMARIES_MODE_NONE,
					      nullptr));
    gst_d3d11_device_unlock (device);

    g_usleep (g_random_int_range (10, 1000));

    gst_d3d11_device_lock (device);
    gst_object_unref (converter);
    gst_d3d11_device_unlock (device);

    gst_object_unref (device);
  }

  return NULL;
}

GST_START_TEST (test_device_new_concurrency)
{
#define NUM_THREADS 32
  GThread *threads[NUM_THREADS];

  for (int t = 0; t < NUM_THREADS; t++)
    threads[t] = g_thread_new (NULL, test_device_new_concurrency_thread, NULL);

  g_usleep (20 * G_TIME_SPAN_SECOND);
  g_atomic_int_set (&stopping, TRUE);
  for (int t = 0; t < NUM_THREADS; t++)
    g_thread_join(threads[t]);

}

GST_END_TEST;

GST_START_TEST (test_device_lock_recursiveness)
{
  GstD3D11Device *device = gst_d3d11_device_new (0, 0);

  gst_d3d11_device_lock (device);
  gst_d3d11_device_lock (device);
  gst_d3d11_device_unlock (device);
  gst_d3d11_device_unlock (device);

  gst_object_unref (device);
}

GST_END_TEST;

static Suite *
d3d11device_suite (void)
{
  Suite *s = suite_create ("d3d11device");
  TCase *tc_basic = tcase_create ("general");

  suite_add_tcase (s, tc_basic);

  if (!check_d3d11_available ())
    goto out;

  tcase_add_test (tc_basic, test_device_new);
  tcase_add_test (tc_basic, test_device_for_adapter_luid);
  tcase_add_test (tc_basic, test_device_new_wrapped);
  tcase_add_test (tc_basic, test_device_new_concurrency);
  tcase_add_test (tc_basic, test_device_lock_recursiveness);

out:
  return s;
}

GST_CHECK_MAIN (d3d11device);
