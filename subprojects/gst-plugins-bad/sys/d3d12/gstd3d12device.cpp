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

#include "gstd3d12device.h"
#include "gstd3d12utils.h"
#include "gstd3d12format.h"
#include <wrl.h>
#include <vector>
#include <string.h>
#include <mutex>
#include <atomic>
#include <string>
#include <locale>
#include <codecvt>
#include <algorithm>

#ifdef HAVE_D3D12_SDKLAYERS_H
#include <d3d12sdklayers.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_device_debug);
GST_DEBUG_CATEGORY_STATIC (gst_d3d12_sdk_debug);
#define GST_CAT_DEFAULT gst_d3d12_device_debug

#define MAKE_FORMAT_MAP_YUV(g,d,r0,r1,r2,r3) \
  { GST_VIDEO_FORMAT_ ##g, DXGI_FORMAT_ ##d, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    (D3D12_FORMAT_SUPPORT1) (D3D12_FORMAT_SUPPORT1_TEXTURE2D | \
      D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_RENDER_TARGET) }

#define MAKE_FORMAT_MAP_YUV_FULL(g,d,r0,r1,r2,r3,f) \
  { GST_VIDEO_FORMAT_ ##g, DXGI_FORMAT_ ##d, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    (D3D12_FORMAT_SUPPORT1) (f) }

#define MAKE_FORMAT_MAP_RGB(g,d) \
  { GST_VIDEO_FORMAT_ ##g, DXGI_FORMAT_ ##d, \
    { DXGI_FORMAT_ ##d, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN }, \
    { DXGI_FORMAT_ ##d, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN }, \
    (D3D12_FORMAT_SUPPORT1) (D3D12_FORMAT_SUPPORT1_TEXTURE2D | \
      D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_RENDER_TARGET) }

#define MAKE_FORMAT_MAP_RGBP(g,d,a) \
  { GST_VIDEO_FORMAT_ ##g, DXGI_FORMAT_UNKNOWN, \
    { DXGI_FORMAT_ ##d, DXGI_FORMAT_ ##d, DXGI_FORMAT_ ##d, DXGI_FORMAT_ ##a }, \
    { DXGI_FORMAT_ ##d, DXGI_FORMAT_ ##d, DXGI_FORMAT_ ##d, DXGI_FORMAT_ ##a }, \
    (D3D12_FORMAT_SUPPORT1) (D3D12_FORMAT_SUPPORT1_TEXTURE2D | \
      D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_RENDER_TARGET) }

static const GstD3D12Format _gst_d3d12_default_format_map[] = {
  MAKE_FORMAT_MAP_RGB (BGRA, B8G8R8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (RGBA, R8G8B8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (BGRx, B8G8R8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (RGBx, R8G8B8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (RGB10A2_LE, R10G10B10A2_UNORM),
  MAKE_FORMAT_MAP_RGB (RGBA64_LE, R16G16B16A16_UNORM),
  MAKE_FORMAT_MAP_YUV (AYUV, UNKNOWN, R8G8B8A8_UNORM, UNKNOWN, UNKNOWN,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (AYUV64, UNKNOWN, R16G16B16A16_UNORM, UNKNOWN, UNKNOWN,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (VUYA, AYUV, R8G8B8A8_UNORM, UNKNOWN, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (NV12, NV12, R8_UNORM, R8G8_UNORM, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (NV21, UNKNOWN, R8_UNORM, R8G8_UNORM, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (P010_10LE, P010, R16_UNORM, R16G16_UNORM, UNKNOWN,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (P012_LE, P016, R16_UNORM, R16G16_UNORM, UNKNOWN,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (P016_LE, P016, R16_UNORM, R16G16_UNORM, UNKNOWN,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I420, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (YV12, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I420_10LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I420_12LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y42B, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I422_10LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I422_12LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444_10LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444_12LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM,
      UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444_16LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM,
      UNKNOWN),
  /* GRAY */
  /* NOTE: To support conversion by using video processor,
   * mark DXGI_FORMAT_{R8,R16}_UNORM formats as known dxgi_format.
   * Otherwise, d3d12 elements will not try to use video processor for
   * those formats */
  MAKE_FORMAT_MAP_RGB (GRAY8, R8_UNORM),
  MAKE_FORMAT_MAP_RGB (GRAY16_LE, R16_UNORM),
  MAKE_FORMAT_MAP_YUV_FULL (Y410, Y410, R10G10B10A2_UNORM, UNKNOWN, UNKNOWN,
      UNKNOWN,
      D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE),
  MAKE_FORMAT_MAP_YUV_FULL (YUY2, YUY2, R8G8B8A8_UNORM, UNKNOWN, UNKNOWN,
      UNKNOWN,
      D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE),
  MAKE_FORMAT_MAP_RGBP (RGBP, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_RGBP (BGRP, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_RGBP (GBR, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_RGBP (GBR_10LE, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_RGBP (GBR_12LE, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_RGBP (GBRA, R8_UNORM, R8_UNORM),
  MAKE_FORMAT_MAP_RGBP (GBRA_10LE, R16_UNORM, R16_UNORM),
  MAKE_FORMAT_MAP_RGBP (GBRA_12LE, R16_UNORM, R16_UNORM),
};

#undef MAKE_FORMAT_MAP_YUV
#undef MAKE_FORMAT_MAP_YUV_FULL
#undef MAKE_FORMAT_MAP_RGB

#define GST_D3D12_N_FORMATS G_N_ELEMENTS(_gst_d3d12_default_format_map)

enum
{
  PROP_0,
  PROP_ADAPTER_INDEX,
  PROP_ADAPTER_LUID,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
  PROP_HARDWARE,
  PROP_DESCRIPTION,
};

/* d3d12 devices are singtones per adapter. Keep track of live objects and
 * reuse already created object if possible */
/* *INDENT-OFF* */
std::mutex device_list_lock_;
std::vector<GstD3D12Device*> live_devices_;

using namespace Microsoft::WRL;

struct _GstD3D12DevicePrivate
{
  _GstD3D12DevicePrivate ()
  {
    fence_value = 1;
  }

  ComPtr<ID3D12Device> device;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIFactory2> factory;
  std::vector < GstD3D12Format> formats;
  std::mutex lock;
  std::recursive_mutex extern_lock;
  std::atomic<guint64> fence_value;

  ComPtr<ID3D12CommandQueue> copy_queue;

#ifdef HAVE_D3D12_SDKLAYERS_H
  ComPtr<ID3D12InfoQueue> info_queue;
#endif

  guint adapter_index = 0;
  guint device_id = 0;
  guint vendor_id = 0;
  std::string description;
  gint64 adapter_luid = 0;
};
/* *INDENT-ON* */

#ifdef HAVE_D3D12_SDKLAYERS_H
static gboolean
gst_d3d12_device_enable_debug (void)
{
  static gboolean enabled = FALSE;

  GST_D3D12_CALL_ONCE_BEGIN {
    GST_DEBUG_CATEGORY_INIT (gst_d3d12_sdk_debug,
        "d3d12debuglayer", 0, "d3d12 SDK layer debug");

    /* Enables debug layer only if it's requested, otherwise
     * already configured d3d12 devices (e.g., owned by application)
     * will be invalidated by ID3D12Debug::EnableDebugLayer() */
    if (!g_getenv ("GST_ENABLE_D3D12_DEBUG"))
      return;

    HRESULT hr;
    ComPtr < ID3D12Debug > d3d12_debug;
    hr = D3D12GetDebugInterface (IID_PPV_ARGS (&d3d12_debug));
    if (FAILED (hr))
      return;

    d3d12_debug->EnableDebugLayer ();
    enabled = TRUE;

    GST_INFO ("D3D12 debug layer is enabled");

#ifdef HAVE_D3D12DEBUG5
    ComPtr < ID3D12Debug5 > d3d12_debug5;
    hr = d3d12_debug.As (&d3d12_debug5);
    if (SUCCEEDED (hr))
      d3d12_debug5->SetEnableAutoName (TRUE);

    ComPtr < ID3D12Debug1 > d3d12_debug1;
    hr = d3d12_debug.As (&d3d12_debug1);
    if (FAILED (hr))
      return;

    d3d12_debug1->SetEnableSynchronizedCommandQueueValidation (TRUE);

    GST_INFO ("Enabled synchronized command queue validation");

    if (!g_getenv ("GST_ENABLE_D3D12_DEBUG_GPU_VALIDATION"))
      return;

    d3d12_debug1->SetEnableGPUBasedValidation (TRUE);

    GST_INFO ("Enabled GPU based validation");
#endif
  }
  GST_D3D12_CALL_ONCE_END;

  return enabled;
}
#endif

#define gst_d3d12_device_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Device, gst_d3d12_device, GST_TYPE_OBJECT);

static void gst_d3d12_device_finalize (GObject * object);
static void gst_d3d12_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d12_device_setup_format_table (GstD3D12Device * self);

static void
gst_d3d12_device_class_init (GstD3D12DeviceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamFlags readable_flags =
      (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  gobject_class->finalize = gst_d3d12_device_finalize;
  gobject_class->get_property = gst_d3d12_device_get_property;

  g_object_class_install_property (gobject_class, PROP_ADAPTER_INDEX,
      g_param_spec_uint ("adapter-index", "Adapter Index",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, 0, readable_flags));

  g_object_class_install_property (gobject_class, PROP_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier) of created device",
          0, G_MAXINT64, 0, readable_flags));

  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0, readable_flags));

  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0, readable_flags));

  g_object_class_install_property (gobject_class, PROP_DESCRIPTION,
      g_param_spec_string ("description", "Description",
          "Human readable device description", nullptr, readable_flags));
}

static void
gst_d3d12_device_init (GstD3D12Device * self)
{
  self->priv = new GstD3D12DevicePrivate ();
}

static void
gst_d3d12_device_finalize (GObject * object)
{
  GstD3D12Device *self = GST_D3D12_DEVICE (object);

  GST_DEBUG_OBJECT (self, "Finalize");

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D12Device *self = GST_D3D12_DEVICE (object);
  GstD3D12DevicePrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_ADAPTER_INDEX:
      g_value_set_uint (value, priv->adapter_index);
      break;
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, priv->adapter_luid);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, priv->device_id);
      break;
    case PROP_VENDOR_ID:
      g_value_set_uint (value, priv->vendor_id);
      break;
    case PROP_DESCRIPTION:
      g_value_set_string (value, priv->description.c_str ());
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
check_format_support (GstD3D12Device * self, DXGI_FORMAT format,
    guint flags, D3D12_FEATURE_DATA_FORMAT_SUPPORT * support)
{
  ID3D12Device *device = self->priv->device.Get ();
  HRESULT hr;

  support->Format = format;
  hr = device->CheckFeatureSupport (D3D12_FEATURE_FORMAT_SUPPORT, support,
      sizeof (D3D12_FEATURE_DATA_FORMAT_SUPPORT));
  if (FAILED (hr)) {
    GST_INFO_OBJECT (self,
        "Failed to check feature support for DXGI format %d", format);
    return FALSE;
  }

  if (((guint) support->Support1 & flags) != flags) {
    GST_INFO_OBJECT (self,
        "DXGI format %d supports1 flag 0x%x, required 0x%x", format,
        support->Support1, flags);

    return FALSE;
  }

  return TRUE;
}

static void
gst_d3d12_device_setup_format_table (GstD3D12Device * self)
{
  GstD3D12DevicePrivate *priv = self->priv;

  for (guint i = 0; i < G_N_ELEMENTS (_gst_d3d12_default_format_map); i++) {
    const GstD3D12Format *iter = &_gst_d3d12_default_format_map[i];
    GstD3D12Format format;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support[GST_VIDEO_MAX_PLANES];

    memset (support, 0, sizeof (support));

    switch (iter->format) {
        /* RGB/GRAY */
      case GST_VIDEO_FORMAT_BGRA:
      case GST_VIDEO_FORMAT_BGRx:
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_RGBx:
      case GST_VIDEO_FORMAT_RGB10A2_LE:
      case GST_VIDEO_FORMAT_RGBA64_LE:
      case GST_VIDEO_FORMAT_GRAY8:
      case GST_VIDEO_FORMAT_GRAY16_LE:
        if (!check_format_support (self, iter->dxgi_format,
                iter->format_support1[0], &support[0])) {
          continue;
        }
        break;
        /* YUV DXGI native formats */
      case GST_VIDEO_FORMAT_VUYA:
      case GST_VIDEO_FORMAT_Y410:
      case GST_VIDEO_FORMAT_NV12:
      case GST_VIDEO_FORMAT_P010_10LE:
      case GST_VIDEO_FORMAT_P012_LE:
      case GST_VIDEO_FORMAT_P016_LE:
      case GST_VIDEO_FORMAT_YUY2:
        if (!check_format_support (self, iter->dxgi_format,
                iter->format_support1[0], &support[0])) {
          continue;
        }
        break;
        /* non-DXGI native formats */
      case GST_VIDEO_FORMAT_NV21:
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
      case GST_VIDEO_FORMAT_I420_10LE:
      case GST_VIDEO_FORMAT_I420_12LE:
      case GST_VIDEO_FORMAT_Y42B:
      case GST_VIDEO_FORMAT_I422_10LE:
      case GST_VIDEO_FORMAT_I422_12LE:
      case GST_VIDEO_FORMAT_Y444:
      case GST_VIDEO_FORMAT_Y444_10LE:
      case GST_VIDEO_FORMAT_Y444_12LE:
      case GST_VIDEO_FORMAT_Y444_16LE:
      case GST_VIDEO_FORMAT_AYUV:
      case GST_VIDEO_FORMAT_AYUV64:
        /* RGB planar formats */
      case GST_VIDEO_FORMAT_RGBP:
      case GST_VIDEO_FORMAT_BGRP:
      case GST_VIDEO_FORMAT_GBR:
      case GST_VIDEO_FORMAT_GBR_10LE:
      case GST_VIDEO_FORMAT_GBR_12LE:
      case GST_VIDEO_FORMAT_GBRA:
      case GST_VIDEO_FORMAT_GBRA_10LE:
      case GST_VIDEO_FORMAT_GBRA_12LE:
      {
        bool supported = true;
        for (guint j = 0; j < GST_VIDEO_MAX_PLANES; j++) {
          if (iter->resource_format[j] == DXGI_FORMAT_UNKNOWN)
            break;

          if (!check_format_support (self, iter->resource_format[j],
                  iter->format_support1[0], &support[j])) {
            supported = false;
            break;
          }
        }

        if (!supported) {
          GST_INFO_OBJECT (self, "%s is not supported",
              gst_video_format_to_string (iter->format));
          continue;
        }

        break;
      }
      default:
        g_assert_not_reached ();
        return;
    }

    format = *iter;

    for (guint j = 0; j < GST_VIDEO_MAX_PLANES; j++) {
      format.format_support1[j] = support[j].Support1;
      format.format_support2[j] = support[j].Support2;
    }

    priv->formats.push_back (format);
  }
}

static void
gst_d3d12_device_weak_ref_notify (gpointer data, GstD3D12Device * device)
{
  std::lock_guard < std::mutex > lk (device_list_lock_);
  auto it = std::find (live_devices_.begin (), live_devices_.end (), device);
  if (it != live_devices_.end ())
    live_devices_.erase (it);
  else
    GST_WARNING ("Could not find %p from list", device);
}

typedef enum
{
  GST_D3D12_DEVICE_CONSTRUCT_FOR_INDEX,
  GST_D3D12_DEVICE_CONSTRUCT_FOR_LUID,
} GstD3D12DeviceConstructType;

typedef struct
{
  union
  {
    guint index;
    gint64 luid;
  } data;
  GstD3D12DeviceConstructType type;
} GstD3D12DeviceConstructData;

static HRESULT
gst_d3d12_device_find_adapter (const GstD3D12DeviceConstructData * data,
    IDXGIFactory2 * factory, guint * index, IDXGIAdapter1 ** rst)
{
  HRESULT hr;
  UINT factory_flags = 0;

#ifdef HAVE_D3D12_SDKLAYERS_H
  if (gst_d3d12_device_enable_debug ())
    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  hr = CreateDXGIFactory2 (factory_flags, IID_PPV_ARGS (&factory));
  if (FAILED (hr)) {
    GST_WARNING ("cannot create dxgi factory, hr: 0x%x", (guint) hr);
    return hr;
  }

  switch (data->type) {
    case GST_D3D12_DEVICE_CONSTRUCT_FOR_INDEX:{
      ComPtr < IDXGIAdapter1 > adapter;
      hr = factory->EnumAdapters1 (data->data.index, &adapter);
      if (FAILED (hr))
        return hr;

      *index = data->data.index;
      *rst = adapter.Detach ();
      return S_OK;
    }
    case GST_D3D12_DEVICE_CONSTRUCT_FOR_LUID:
      for (UINT i = 0;; i++) {
        ComPtr < IDXGIAdapter1 > adapter;
        DXGI_ADAPTER_DESC1 desc;

        hr = factory->EnumAdapters1 (i, &adapter);
        if (FAILED (hr))
          return hr;

        hr = adapter->GetDesc1 (&desc);
        if (FAILED (hr))
          return hr;

        if (gst_d3d12_luid_to_int64 (&desc.AdapterLuid) != data->data.luid) {
          continue;
        }

        *index = i;
        *rst = adapter.Detach ();

        return S_OK;
      }
    default:
      g_assert_not_reached ();
      break;
  }

  return E_FAIL;
}

static GstD3D12Device *
gst_d3d12_device_new_internal (const GstD3D12DeviceConstructData * data)
{
  ComPtr < IDXGIFactory2 > factory;
  ComPtr < IDXGIAdapter1 > adapter;
  ComPtr < ID3D12Device > device;
  HRESULT hr;
  UINT factory_flags = 0;
  guint index = 0;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_device_debug,
      "d3d12device", 0, "d3d12 device object");

#ifdef HAVE_D3D12_SDKLAYERS_H
  if (gst_d3d12_device_enable_debug ())
    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  hr = CreateDXGIFactory2 (factory_flags, IID_PPV_ARGS (&factory));
  if (FAILED (hr)) {
    GST_WARNING ("Could create dxgi factory, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  hr = gst_d3d12_device_find_adapter (data, factory.Get (), &index, &adapter);
  if (FAILED (hr)) {
    GST_WARNING ("Could not find adapter, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  DXGI_ADAPTER_DESC1 desc;
  hr = adapter->GetDesc1 (&desc);
  if (FAILED (hr)) {
    GST_WARNING ("Could not get adapter desc, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  hr = D3D12CreateDevice (adapter.Get (), D3D_FEATURE_LEVEL_12_0,
      IID_PPV_ARGS (&device));
  if (FAILED (hr)) {
    GST_WARNING ("Could not create device, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  GstD3D12Device *self = (GstD3D12Device *)
      g_object_new (GST_TYPE_D3D12_DEVICE, nullptr);
  GstD3D12DevicePrivate *priv = self->priv;

  priv->factory = factory;
  priv->adapter = adapter;
  priv->device = device;
  priv->adapter_luid = gst_d3d12_luid_to_int64 (&desc.AdapterLuid);
  priv->vendor_id = desc.VendorId;
  priv->device_id = desc.DeviceId;

  std::wstring_convert < std::codecvt_utf8 < wchar_t >, wchar_t >converter;
  priv->description = converter.to_bytes (desc.Description);

  GST_INFO_OBJECT (self,
      "adapter index %d: D3D12 device vendor-id: 0x%04x, device-id: 0x%04x, "
      "Flags: 0x%x, adapter-luid: %" G_GINT64_FORMAT ", %s",
      priv->adapter_index, desc.VendorId, desc.DeviceId, desc.Flags,
      priv->adapter_luid, priv->description.c_str ());

  gst_d3d12_device_setup_format_table (self);

#ifdef HAVE_D3D12_SDKLAYERS_H
  if (gst_d3d12_device_enable_debug ()) {
    ComPtr < ID3D12InfoQueue > info_queue;
    device.As (&info_queue);
    priv->info_queue = info_queue;
  }
#endif

  return self;
}

GstD3D12Device *
gst_d3d12_device_new (guint adapter_index)
{
  GstD3D12Device *self = nullptr;
  std::lock_guard < std::mutex > lk (device_list_lock_);

  /* *INDENT-OFF* */
  for (auto iter: live_devices_) {
    if (iter->priv->adapter_index == adapter_index) {
      self = (GstD3D12Device *) gst_object_ref (iter);
      break;
    }
  }
  /* *INDENT-ON* */

  if (!self) {
    GstD3D12DeviceConstructData data;
    data.data.index = adapter_index;
    data.type = GST_D3D12_DEVICE_CONSTRUCT_FOR_INDEX;

    self = gst_d3d12_device_new_internal (&data);
    if (!self) {
      GST_INFO ("Could not create device for index %d", adapter_index);
      return nullptr;
    }

    gst_object_ref_sink (self);
    g_object_weak_ref (G_OBJECT (self),
        (GWeakNotify) gst_d3d12_device_weak_ref_notify, nullptr);
    live_devices_.push_back (self);
  }

  return self;
}

GstD3D12Device *
gst_d3d12_device_new_for_adapter_luid (gint64 adapter_luid)
{
  GstD3D12Device *self = nullptr;
  std::lock_guard < std::mutex > lk (device_list_lock_);

  /* *INDENT-OFF* */
  for (auto iter: live_devices_) {
    if (iter->priv->adapter_luid == adapter_luid) {
      self = (GstD3D12Device *) gst_object_ref (iter);
      break;
    }
  }
  /* *INDENT-ON* */

  if (!self) {
    GstD3D12DeviceConstructData data;
    data.data.luid = adapter_luid;
    data.type = GST_D3D12_DEVICE_CONSTRUCT_FOR_LUID;

    self = gst_d3d12_device_new_internal (&data);
    if (!self) {
      GST_INFO ("Could not create device for LUID %" G_GINT64_FORMAT,
          adapter_luid);
      return nullptr;
    }

    gst_object_ref_sink (self);
    g_object_weak_ref (G_OBJECT (self),
        (GWeakNotify) gst_d3d12_device_weak_ref_notify, nullptr);
    live_devices_.push_back (self);
  }

  return self;
}

ID3D12Device *
gst_d3d12_device_get_device_handle (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  return device->priv->device.Get ();
}

IDXGIAdapter1 *
gst_d3d12_device_get_adapter_handle (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  return device->priv->adapter.Get ();
}

IDXGIFactory2 *
gst_d3d12_device_get_factory_handle (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  return device->priv->factory.Get ();
}

gboolean
gst_d3d12_device_get_format (GstD3D12Device * device,
    GstVideoFormat format, GstD3D12Format * device_format)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), FALSE);
  g_return_val_if_fail (device_format != nullptr, FALSE);

  /* *INDENT-OFF* */
  for (const auto &iter : device->priv->formats) {
    if (iter.format == format) {
      *device_format = iter;
      return TRUE;
    }
  }
  /* *INDENT-ON* */

  return FALSE;
}

guint64
gst_d3d12_device_get_fence_value (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), G_MAXUINT64);

  return device->priv->fence_value.fetch_add (1);
}

ID3D12CommandQueue *
gst_d3d12_device_get_copy_queue (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto priv = device->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->copy_queue) {
    HRESULT hr;
    D3D12_COMMAND_QUEUE_DESC desc = { };
    ComPtr < ID3D12CommandQueue > queue;

    desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = priv->device->CreateCommandQueue (&desc, IID_PPV_ARGS (&queue));
    if (!gst_d3d12_result (hr, device))
      return nullptr;

    priv->copy_queue = queue;
  }

  return priv->copy_queue.Get ();
}

static inline GstDebugLevel
d3d12_message_severity_to_gst (D3D12_MESSAGE_SEVERITY level)
{
  switch (level) {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    case D3D12_MESSAGE_SEVERITY_ERROR:
      return GST_LEVEL_ERROR;
    case D3D12_MESSAGE_SEVERITY_WARNING:
      return GST_LEVEL_WARNING;
    case D3D12_MESSAGE_SEVERITY_INFO:
      return GST_LEVEL_INFO;
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
      return GST_LEVEL_DEBUG;
    default:
      break;
  }

  return GST_LEVEL_LOG;
}

#ifdef HAVE_D3D12_SDKLAYERS_H
void
gst_d3d12_device_d3d12_debug (GstD3D12Device * device, const gchar * file,
    const gchar * function, gint line)
{
  g_return_if_fail (GST_IS_D3D12_DEVICE (device));

  if (!device->priv->info_queue)
    return;

  GstD3D12DevicePrivate *priv = device->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->extern_lock);
  ID3D12InfoQueue *info_queue = priv->info_queue.Get ();
  UINT64 num_msg = info_queue->GetNumStoredMessages ();
  for (guint64 i = 0; i < num_msg; i++) {
    HRESULT hr;
    SIZE_T msg_len;
    D3D12_MESSAGE *msg;
    GstDebugLevel msg_level;
    GstDebugLevel selected_level;

    hr = info_queue->GetMessage (i, nullptr, &msg_len);
    if (FAILED (hr) || msg_len == 0)
      continue;

    msg = (D3D12_MESSAGE *) g_malloc0 (msg_len);
    hr = info_queue->GetMessage (i, msg, &msg_len);
    if (FAILED (hr) || msg_len == 0) {
      g_free (msg);
      continue;
    }

    msg_level = d3d12_message_severity_to_gst (msg->Severity);
    if (msg->Category == D3D12_MESSAGE_CATEGORY_STATE_CREATION &&
        msg_level > GST_LEVEL_ERROR) {
      /* Do not warn for live object, since there would be live object
       * when ReportLiveDeviceObjects was called */
      selected_level = GST_LEVEL_INFO;
    } else {
      selected_level = msg_level;
    }

    gst_debug_log (gst_d3d12_sdk_debug, selected_level, file, function, line,
        G_OBJECT (device), "D3D12InfoQueue: %s", msg->pDescription);
    g_free (msg);
  }

  info_queue->ClearStoredMessages ();
}
#else
void
gst_d3d12_device_d3d12_debug (GstD3D12Device * device, const gchar * file,
    const gchar * function, gint line)
{
}
#endif
