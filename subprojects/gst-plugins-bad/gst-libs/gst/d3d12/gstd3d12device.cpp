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

#include "gstd3d12.h"
#include "gstd3d12-private.h"
#include "gstd3d12commandlistpool.h"
#include <directx/d3dx12.h>
#include <d3d11on12.h>
#include <wrl.h>
#include <vector>
#include <string.h>
#include <mutex>
#include <condition_variable>
#include <string>
#include <locale>
#include <codecvt>
#include <algorithm>
#include <d3d12sdklayers.h>
#include <memory>
#include <queue>
#include <unordered_map>
#include <thread>
#include <gmodule.h>
#include <atomic>
#include <sstream>
#include <ios>

#ifdef HAVE_DXGIDEBUG_H
#include <dxgidebug.h>
/* *INDENT-OFF* */
typedef HRESULT (WINAPI * DXGIGetDebugInterface_t) (REFIID riid, void **iface);
static DXGIGetDebugInterface_t GstDXGIGetDebugInterface = nullptr;
static IDXGIInfoQueue *g_dxgi_info_queue = nullptr;
static std::mutex g_dxgi_debug_lock;
/* *INDENT-ON* */
#endif

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_sdk_debug);
GST_DEBUG_CATEGORY_STATIC (gst_d3d12_dred_debug);
GST_DEBUG_CATEGORY_STATIC (gst_d3d12_dxgi_debug);

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12device", 0, "d3d12device");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */

static PFN_D3D11ON12_CREATE_DEVICE GstD3D11On12CreateDevice = nullptr;
static gboolean gst_d3d12_device_enable_dred (void);

static gboolean
load_d3d11on12_symbol (void)
{
  static gboolean ret = FALSE;
  static GModule *d3d11_lib_module = nullptr;
  GST_D3D12_CALL_ONCE_BEGIN {
    d3d11_lib_module = g_module_open ("d3d11.dll", G_MODULE_BIND_LAZY);
    if (!d3d11_lib_module)
      return;

    if (!g_module_symbol (d3d11_lib_module, "D3D11On12CreateDevice",
            (gpointer *) & GstD3D11On12CreateDevice)) {
      return;
    }

    ret = TRUE;
  }
  GST_D3D12_CALL_ONCE_END;

  return ret;
}

enum
{
  PROP_0,
  PROP_ADAPTER_INDEX,
  PROP_ADAPTER_LUID,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
  PROP_HARDWARE,
  PROP_DESCRIPTION,
  PROP_DEVICE_REMOVED_REASON,
};

static GParamSpec *pspec_removed_reason = nullptr;

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct DeviceInner
{
  DeviceInner ()
  {
    dev_removed_event = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  }

  ~DeviceInner ()
  {
    Drain ();

    gst_clear_object (&direct_queue);
    gst_clear_object (&copy_queue);
    for (guint i = 0; i < num_decode_queue; i++)
      gst_clear_object (&decode_queue[i]);

    gst_clear_object (&direct_ca_pool);
    gst_clear_object (&direct_cl_pool);

    gst_clear_object (&copy_ca_pool);
    gst_clear_object (&copy_cl_pool);

    gst_clear_object (&fence_data_pool);

    factory = nullptr;
    adapter = nullptr;

    if (removed_reason == S_OK)
      ReportLiveObjects ();

    if (dev_removed_monitor_handle)
      UnregisterWait (dev_removed_monitor_handle);

    CloseHandle (dev_removed_event);
  }

  void Drain ()
  {
    if (direct_queue)
      gst_d3d12_command_queue_drain (direct_queue);

    if (copy_queue)
      gst_d3d12_command_queue_drain (copy_queue);

    for (guint i = 0; i < num_decode_queue; i++)
      gst_d3d12_command_queue_drain (decode_queue[i]);
  }

  void ReportLiveObjects ()
  {
    if (!info_queue || !device)
      return;

    ComPtr <ID3D12DebugDevice> debug_dev;
    device.As (&debug_dev);
    if (!debug_dev)
      return;

    debug_dev->ReportLiveDeviceObjects (D3D12_RLDO_DETAIL |
        D3D12_RLDO_IGNORE_INTERNAL);

    GST_DEBUG ("Begin live object report %s", description.c_str ());

    UINT64 num_msg = info_queue->GetNumStoredMessages ();
    for (UINT64 i = 0; i < num_msg; i++) {
      HRESULT hr;
      SIZE_T msg_len;
      D3D12_MESSAGE *msg;

      hr = info_queue->GetMessage (i, nullptr, &msg_len);
      if (FAILED (hr) || msg_len == 0)
        continue;

      msg = (D3D12_MESSAGE *) g_malloc0 (msg_len);
      hr = info_queue->GetMessage (i, msg, &msg_len);
      if (FAILED (hr) || msg_len == 0) {
        g_free (msg);
        continue;
      }

      gst_debug_log (gst_d3d12_sdk_debug, GST_LEVEL_INFO,
          __FILE__, GST_FUNCTION, __LINE__, nullptr,
          "D3D12InfoQueue: %s", msg->pDescription);
      g_free (msg);
    }

    GST_DEBUG ("End live object report %s", description.c_str ());

    info_queue->ClearStoredMessages ();
  }

  void AddClient (GstD3D12Device * client)
  {
    std::lock_guard <std::mutex> lk (lock);
    clients.push_back (client);
  }

  void RemoveClient (GstD3D12Device * client)
  {
    std::lock_guard <std::mutex> lk (lock);
    for (auto it = clients.begin (); it != clients.end(); it++) {
      if (*it == client) {
        clients.erase (it);
        return;
      }
    }
  }

  ComPtr<ID3D12Device> device;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIFactory2> factory;
  ComPtr<ID3D11On12Device> device11on12;
  std::unordered_map<GstVideoFormat, GstD3D12Format> format_table;
  std::recursive_mutex extern_lock;
  std::recursive_mutex device11on12_lock;
  std::mutex lock;
  CD3DX12FeatureSupport feature_support;

  ComPtr<ID3D12InfoQueue> info_queue;

  GstD3D12CommandQueue *direct_queue = nullptr;
  GstD3D12CommandQueue *copy_queue = nullptr;
  GstD3D12CommandQueue *decode_queue[2] = { nullptr, };
  guint num_decode_queue = 0;
  guint decode_queue_index = 0;
  std::recursive_mutex decoder_lock;
  GstD3D12WAFlags wa_flags = GST_D3D12_WA_NONE;

  GstD3D12CommandListPool *direct_cl_pool = nullptr;
  GstD3D12CommandAllocatorPool *direct_ca_pool = nullptr;

  GstD3D12CommandListPool *copy_cl_pool = nullptr;
  GstD3D12CommandAllocatorPool *copy_ca_pool = nullptr;

  GstD3D12FenceDataPool *fence_data_pool = nullptr;

  guint rtv_inc_size;

  guint adapter_index = 0;
  guint device_id = 0;
  guint vendor_id = 0;
  std::string description;
  gint64 adapter_luid = 0;

  HANDLE dev_removed_monitor_handle = nullptr;
  HANDLE dev_removed_event;
  ComPtr<ID3D12Fence> dev_removed_fence;
  std::atomic<HRESULT> removed_reason = { S_OK };

  std::vector<GstD3D12Device*> clients;
};

typedef std::shared_ptr<DeviceInner> DeviceInnerPtr;

struct _GstD3D12DevicePrivate
{
  DeviceInnerPtr inner;
};

enum GstD3D12DeviceConstructType
{
  GST_D3D12_DEVICE_CONSTRUCT_FOR_INDEX,
  GST_D3D12_DEVICE_CONSTRUCT_FOR_LUID,
};

struct GstD3D12DeviceConstructData
{
  union
  {
    guint index;
    gint64 luid;
  } data;
  GstD3D12DeviceConstructType type;
};

static GstD3D12Device *
gst_d3d12_device_new_internal (const GstD3D12DeviceConstructData * data);

class DeviceCacheManager
{
public:
  DeviceCacheManager (const DeviceCacheManager &) = delete;
  DeviceCacheManager& operator= (const DeviceCacheManager &) = delete;
  static DeviceCacheManager * GetInstance()
  {
    static DeviceCacheManager *inst = nullptr;
    GST_D3D12_CALL_ONCE_BEGIN {
      inst = new DeviceCacheManager ();
    } GST_D3D12_CALL_ONCE_END;

    return inst;
  }

  GstD3D12Device * GetDevice (const GstD3D12DeviceConstructData * data)
  {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    auto it = std::find_if (list_.begin (), list_.end (),
        [&] (const auto & device) {
          if (data->type == GST_D3D12_DEVICE_CONSTRUCT_FOR_INDEX)
            return device->adapter_index == data->data.index;

          return device->adapter_luid == data->data.luid;
        });

    if (it != list_.end ()) {
      auto device = (GstD3D12Device *)
          g_object_new (GST_TYPE_D3D12_DEVICE, nullptr);
      gst_object_ref_sink (device);
      device->priv->inner = *it;

      auto name = buildObjectName ((*it)->adapter_index);
      gst_object_set_name (GST_OBJECT (device), name.c_str ());

      GST_DEBUG_OBJECT (device, "Reusing created device");

      device->priv->inner->AddClient (device);

      return device;
    }

    auto device = gst_d3d12_device_new_internal (data);
    if (!device)
      return nullptr;

    auto name = buildObjectName (device->priv->inner->adapter_index);
    gst_object_set_name (GST_OBJECT (device), name.c_str ());

    GST_DEBUG_OBJECT (device, "Created new device");

    list_.push_back (device->priv->inner);

    device->priv->inner->AddClient (device);

    return device;
  }

  void ReleaseDevice (gint64 luid)
  {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    for (const auto & it : list_) {
      if (it->adapter_luid == luid) {
        if (it.use_count () == 1) {
          it->Drain ();
          it->ReportLiveObjects ();
        }
        return;
      }
    }
  }

  void OnDeviceRemoved (gint64 luid)
  {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    DeviceInnerPtr ptr;

    {
      auto it = std::find_if (list_.begin (), list_.end (),
          [&] (const auto & device) {
            return device->adapter_luid == luid;
          });

      if (it == list_.end ())
        return;

      ptr = *it;
      list_.erase (it);
    }

    UnregisterWait (ptr->dev_removed_monitor_handle);
    ptr->dev_removed_monitor_handle = nullptr;

    ptr->removed_reason = ptr->device->GetDeviceRemovedReason ();
    if (SUCCEEDED (ptr->removed_reason))
      ptr->removed_reason = DXGI_ERROR_DEVICE_REMOVED;

    auto error_text = g_win32_error_message ((guint) ptr->removed_reason);
    GST_ERROR ("Adapter LUID: %" G_GINT64_FORMAT
        ", DeviceRemovedReason: 0x%x, %s", ptr->adapter_luid,
        (guint) ptr->removed_reason, GST_STR_NULL (error_text));
    g_free (error_text);

    if (gst_d3d12_device_enable_dred ()) {
      ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
      auto hr = ptr->device.As (&dred1);
      if (SUCCEEDED (hr)) {
        ComPtr<ID3D12DeviceRemovedExtendedData2> dred2;
        hr = dred1.As (&dred2);
        if (SUCCEEDED (hr)) {
          GST_CAT_ERROR (gst_d3d12_dred_debug, "D3D12_DRED_DEVICE_STATE: %d",
              dred2->GetDeviceState ());
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs_output;
        hr = dred1->GetAutoBreadcrumbsOutput (&breadcrumbs_output);
        if (SUCCEEDED (hr)) {
          guint node_idx = 0;
          const D3D12_AUTO_BREADCRUMB_NODE *node =
              breadcrumbs_output.pHeadAutoBreadcrumbNode;
          GST_CAT_ERROR (gst_d3d12_dred_debug,
              "Reporting GetAutoBreadcrumbsOutput");
          while (node) {
            GST_CAT_ERROR (gst_d3d12_dred_debug, "  [%u]%s:%s - "
                "pLastBreadcrumbValue (%u) BreadcrumbCount (%u)", node_idx,
                GST_STR_NULL (node->pCommandQueueDebugNameA),
                GST_STR_NULL (node->pCommandListDebugNameA),
                node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0,
                node->BreadcrumbCount);
            for (UINT32 count = 0; count < node->BreadcrumbCount; count++) {
              GST_CAT_ERROR (gst_d3d12_dred_debug,
                  "    [%u][%u] D3D12_AUTO_BREADCRUMB_OP: %u",
                  node_idx, count, node->pCommandHistory[count]);
            }

            node_idx++;
            node = node->pNext;
          }
        } else {
          GST_CAT_ERROR (gst_d3d12_dred_debug,
              "GetAutoBreadcrumbsOutput() return 0x%x", (guint) hr);
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT fault_output;
        hr = dred1->GetPageFaultAllocationOutput (&fault_output);
        if (SUCCEEDED (hr)) {
          GST_ERROR ("Reporting GetPageFaultAllocationOutput");
          guint node_idx = 0;
          const D3D12_DRED_ALLOCATION_NODE *node =
              fault_output.pHeadExistingAllocationNode;
          GST_CAT_ERROR (gst_d3d12_dred_debug, "  Existing allocation nodes: ");
          while (node) {
            GST_CAT_ERROR (gst_d3d12_dred_debug, "    [%u]%s: %d", node_idx,
                GST_STR_NULL (node->ObjectNameA), node->AllocationType);
            node_idx++;
            node = node->pNext;
          }

          GST_ERROR ("  Recently freed allocation nodes: ");
          node_idx = 0;
          node = fault_output.pHeadRecentFreedAllocationNode;
          while (node) {
            GST_CAT_ERROR (gst_d3d12_dred_debug,"    [%u]%s: %d", node_idx,
                GST_STR_NULL (node->ObjectNameA), node->AllocationType);
            node_idx++;
            node = node->pNext;
          }
        } else {
          GST_CAT_ERROR (gst_d3d12_dred_debug,
              "GetPageFaultAllocationOutput () return 0x%x", (guint) hr);
        }
      }
    }

    std::vector<GstD3D12Device *> clients;
    {
      std::lock_guard<std::mutex> client_lk (ptr->lock);
      for (auto it : ptr->clients) {
        gst_object_ref (it);
        clients.push_back (it);
      }
    }

    for (auto it : clients) {
      g_object_notify_by_pspec (G_OBJECT (it), pspec_removed_reason);
      gst_object_unref (it);
    }
  }

private:
  DeviceCacheManager () {}
  ~DeviceCacheManager () {}

  std::string buildObjectName (UINT adapter_index)
  {
    auto name_it = name_map_.find (adapter_index);
    UINT idx = 0;
    if (name_it == name_map_.end ()) {
      name_map_.insert ({adapter_index, 0});
    } else {
      name_it->second++;
      idx = name_it->second;
    }

    return std::string ("d3d12device") + std::to_string (adapter_index) + "-" +
        std::to_string (idx);
  }

private:
  std::recursive_mutex lock_;
  std::vector<DeviceInnerPtr> list_;
  std::unordered_map<UINT,UINT> name_map_;
};
/* *INDENT-ON* */

static VOID NTAPI
on_device_removed (PVOID context, BOOLEAN unused)
{
  DeviceInner *inner = (DeviceInner *) context;
  auto manager = DeviceCacheManager::GetInstance ();
  manager->OnDeviceRemoved (inner->adapter_luid);
}

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
  }
  GST_D3D12_CALL_ONCE_END;

  return enabled;
}

static gboolean
gst_d3d12_device_enable_dred (void)
{
  static gboolean enabled = FALSE;

  GST_D3D12_CALL_ONCE_BEGIN {
    GST_DEBUG_CATEGORY_INIT (gst_d3d12_dred_debug,
        "d3d12dred", 0, "d3d12 Device Removed Extended(DRED) debug");

    if (gst_debug_category_get_threshold (gst_d3d12_dred_debug) >
        GST_LEVEL_ERROR) {
      HRESULT hr;
      ComPtr < ID3D12DeviceRemovedExtendedDataSettings1 > settings;
      hr = D3D12GetDebugInterface (IID_PPV_ARGS (&settings));
      if (FAILED (hr)) {
        GST_CAT_WARNING (gst_d3d12_dred_debug,
            "ID3D12DeviceRemovedExtendedDataSettings1 interface unavailable");
        return;
      }

      settings->SetAutoBreadcrumbsEnablement (D3D12_DRED_ENABLEMENT_FORCED_ON);
      settings->SetPageFaultEnablement (D3D12_DRED_ENABLEMENT_FORCED_ON);
      GST_CAT_INFO (gst_d3d12_dred_debug,
          "D3D12 DRED (Device Removed Extended Data) is enabled");

      enabled = TRUE;
    }
  }
  GST_D3D12_CALL_ONCE_END;

  return enabled;
}

static gboolean
gst_d3d12_device_enable_dxgi_debug (void)
{
  static gboolean enabled = FALSE;
#ifdef HAVE_DXGIDEBUG_H
  static GModule *dxgi_debug_module = nullptr;
  GST_D3D12_CALL_ONCE_BEGIN {
    GST_DEBUG_CATEGORY_INIT (gst_d3d12_dxgi_debug,
        "d3d12dxgidebug", 0, "d3d12dxgidebug");

    if (!g_getenv ("GST_ENABLE_D3D12_DXGI_DEBUG"))
      return;

    dxgi_debug_module = g_module_open ("dxgidebug.dll", G_MODULE_BIND_LAZY);
    if (!dxgi_debug_module)
      return;

    if (!g_module_symbol (dxgi_debug_module, "DXGIGetDebugInterface",
            (gpointer *) & GstDXGIGetDebugInterface)) {
      return;
    }

    auto hr = GstDXGIGetDebugInterface (IID_PPV_ARGS (&g_dxgi_info_queue));
    if (FAILED (hr))
      return;

    GST_INFO ("DXGI debug is enabled");

    enabled = TRUE;
  }
  GST_D3D12_CALL_ONCE_END;
#endif

  return enabled;
}

#define gst_d3d12_device_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Device, gst_d3d12_device, GST_TYPE_OBJECT);

static void gst_d3d12_device_dispose (GObject * object);
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

  gobject_class->dispose = gst_d3d12_device_dispose;
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

  pspec_removed_reason =
      g_param_spec_int ("device-removed-reason", "Device Removed Reason",
      "HRESULT code returned from ID3D12Device::GetDeviceRemovedReason",
      G_MININT32, G_MAXINT32, 0, readable_flags);
  g_object_class_install_property (gobject_class, PROP_DEVICE_REMOVED_REASON,
      pspec_removed_reason);
}

static void
gst_d3d12_device_init (GstD3D12Device * self)
{
  self->priv = new GstD3D12DevicePrivate ();
}

static void
gst_d3d12_device_dispose (GObject * object)
{
  auto self = GST_D3D12_DEVICE (object);

  GST_DEBUG_OBJECT (self, "Dispose");

  if (self->priv->inner)
    self->priv->inner->RemoveClient (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d12_device_finalize (GObject * object)
{
  auto self = GST_D3D12_DEVICE (object);

  GST_DEBUG_OBJECT (self, "Finalize");

  gint64 luid = 0;
  if (self->priv->inner)
    luid = self->priv->inner->adapter_luid;

  delete self->priv;

  auto manager = DeviceCacheManager::GetInstance ();
  manager->ReleaseDevice (luid);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_DEVICE (object);
  auto priv = self->priv->inner;

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
    case PROP_DEVICE_REMOVED_REASON:
      g_value_set_int (value, priv->removed_reason);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
make_buffer_format (GstVideoFormat format, GstD3D12Format * d3d12_format)
{
  d3d12_format->format = format;
  d3d12_format->dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d3d12_format->dxgi_format = DXGI_FORMAT_UNKNOWN;
  d3d12_format->support1 = D3D12_FORMAT_SUPPORT1_NONE;
  d3d12_format->support2 = D3D12_FORMAT_SUPPORT2_NONE;
  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    d3d12_format->resource_format[i] = DXGI_FORMAT_UNKNOWN;
    d3d12_format->uav_format[i] = DXGI_FORMAT_UNKNOWN;
  }
}

static void
gst_d3d12_device_setup_format_table (GstD3D12Device * self)
{
  auto priv = self->priv->inner;
  auto & fs = priv->feature_support;
  HRESULT hr;

  for (guint f = 0; f < GST_VIDEO_FORMAT_LAST; f++) {
    GstD3D12Format format = { };
    if (!gst_d3d12_get_format ((GstVideoFormat) f, &format))
      continue;

    g_assert (format.dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
        format.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

    D3D12_FORMAT_SUPPORT1 support1 = D3D12_FORMAT_SUPPORT1_NONE;
    D3D12_FORMAT_SUPPORT2 support2 = D3D12_FORMAT_SUPPORT2_NONE;
    bool supported = false;
    auto dxgi_format = format.dxgi_format;
    if (format.dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      /* Buffer type is always supported */
      supported = true;
    }

    if (!supported && dxgi_format != DXGI_FORMAT_UNKNOWN) {
      /* packed or yuv semi-planar */
      hr = fs.FormatSupport (format.dxgi_format, support1, support2);
      if (SUCCEEDED (hr) && (support1 & format.support1) == format.support1 &&
          (support2 & format.support2) == format.support2) {
        supported = true;
      } else if (dxgi_format == DXGI_FORMAT_B5G6R5_UNORM ||
          dxgi_format == DXGI_FORMAT_B5G5R5A1_UNORM) {
        /* This format may not be supported by old OS. Use R16_UINT
         * with compute shader */
        format.dxgi_format = DXGI_FORMAT_R16_UINT;
        format.format_flags = GST_D3D12_FORMAT_FLAG_OUTPUT_UAV;
        fs.FormatSupport (DXGI_FORMAT_R16_UINT, support1, support2);
        format.support1 = support1;
        format.support2 = support2;
        format.resource_format[0] = DXGI_FORMAT_R16_UINT;
        format.uav_format[0] = DXGI_FORMAT_R16_UINT;
        supported = true;
      } else {
        format.dxgi_format = DXGI_FORMAT_UNKNOWN;
      }
    }

    if (!supported) {
      bool check_failed = false;
      for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
        auto resource_format = format.resource_format[i];
        if (resource_format == DXGI_FORMAT_UNKNOWN)
          break;

        hr = fs.FormatSupport (resource_format, support1, support2);
        if (FAILED (hr) || (support1 & format.support1) != format.support1 ||
            (support2 & format.support2) != format.support2) {
          check_failed = true;
          break;
        }
      }

      if (!check_failed)
        supported = true;
    }

    if (!supported) {
      /* Use buffer format */
      format.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      format.dxgi_format = DXGI_FORMAT_UNKNOWN;
      format.support1 = D3D12_FORMAT_SUPPORT1_NONE;
      format.support2 = D3D12_FORMAT_SUPPORT2_NONE;
      for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
        format.resource_format[i] = DXGI_FORMAT_UNKNOWN;
        format.uav_format[i] = DXGI_FORMAT_UNKNOWN;
      }
    } else {
      format.support1 = support1;
      format.support2 = support2;
    }

    if (format.dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      GST_LOG_OBJECT (self, "Format %s support: buffer",
          gst_video_format_to_string (format.format));
    } else {
      GST_LOG_OBJECT (self, "Format %s support: dxgi-format: %s, "
          "resource-format: [%s, %s, %s, %s]",
          gst_video_format_to_string (format.format),
          D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetName (format.dxgi_format),
          D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::
          GetName (format.resource_format[0]),
          D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::
          GetName (format.resource_format[1]),
          D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::
          GetName (format.resource_format[2]),
          D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::
          GetName (format.resource_format[3]));
    }

    priv->format_table[format.format] = format;
  }
}

static HRESULT
gst_d3d12_device_find_adapter (const GstD3D12DeviceConstructData * data,
    IDXGIFactory2 * factory, guint * index, IDXGIAdapter1 ** rst)
{
  HRESULT hr;

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

static gboolean
is_intel_gen11_or_older (UINT vendor_id, D3D_FEATURE_LEVEL feature_level,
    const std::string & description)
{
  if (vendor_id != 0x8086)
    return FALSE;

  /* Arc GPU supports feature level 12.2 and iGPU Xe does 12.1 */
  if (feature_level <= D3D_FEATURE_LEVEL_12_0)
    return TRUE;

  /* gen 11 is UHD xxx, older ones are HD xxx */
  if (description.find ("HD") != std::string::npos)
    return TRUE;

  return FALSE;
}

/* *INDENT-OFF* */
#ifndef GST_DISABLE_GST_DEBUG
static void
dump_feature_support (GstD3D12Device * self)
{
  auto priv = self->priv->inner;
  auto &fs = priv->feature_support;
  std::stringstream dump;

  dump << "Device feature supports of " << priv->description
  << "\nD3D12_OPTIONS:"
  << "\n    DoublePrecisionFloatShaderOps: "
  << fs.DoublePrecisionFloatShaderOps()
  << "\n    OutputMergerLogicOp: " << fs.OutputMergerLogicOp()
  << std::showbase << std::hex
  << "\n    MinPrecisionSupport: " << fs.MinPrecisionSupport()
  << std::noshowbase << std::dec
  << "\n    TiledResourcesTier: " << fs.TiledResourcesTier()
  << "\n    ResourceBindingTier: " << fs.ResourceBindingTier()
  << "\n    PSSpecifiedStencilRefSupported: "
  << fs.PSSpecifiedStencilRefSupported()
  << "\n    TypedUAVLoadAdditionalFormats: "
  << fs.TypedUAVLoadAdditionalFormats()
  << "\n    ROVsSupported: " << fs.ROVsSupported()
  << "\n    ConservativeRasterizationTier: "
  << fs.ConservativeRasterizationTier()
  << "\n    MaxGPUVirtualAddressBitsPerResource: "
  << fs.MaxGPUVirtualAddressBitsPerResource()
  << "\n    StandardSwizzle64KBSupported: " << fs.StandardSwizzle64KBSupported()
  << "\n    CrossNodeSharingTier: " << fs.CrossNodeSharingTier()
  << "\n    CrossAdapterRowMajorTextureSupported: "
  << fs.CrossAdapterRowMajorTextureSupported()
  << "\n    VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation: "
  << fs.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation()
  << "\n    ResourceHeapTier: " << fs.ResourceHeapTier()
  << std::showbase << std::hex
  << "\nMaxSupportedFeatureLevel: " << fs.MaxSupportedFeatureLevel()
  << "\nHighestShaderModel: " << fs.HighestShaderModel()
  << std::noshowbase << std::dec
  << "\nMaxGPUVirtualAddressBitsPerProcess: "
  << fs.MaxGPUVirtualAddressBitsPerProcess()
  << "\nD3D12_OPTIONS1:"
  << "\n    WaveOps: " << fs.WaveOps()
  << "\n    WaveLaneCountMin: " << fs.WaveLaneCountMin()
  << "\n    WaveLaneCountMax: " << fs.WaveLaneCountMax()
  << "\n    TotalLaneCount: " << fs.TotalLaneCount()
  << "\n    ExpandedComputeResourceStates: "
  << fs.ExpandedComputeResourceStates()
  << "\n    Int64ShaderOps: " << fs.Int64ShaderOps()
  << std::showbase << std::hex
  << "\nProtectedResourceSessionSupport: "
  << fs.ProtectedResourceSessionSupport()
  << "\nHighestRootSignatureVersion: " << fs.HighestRootSignatureVersion()
  << std::noshowbase << std::dec
  << "\nARCHITECTURE1:"
  << "\n    TileBasedRenderer: " << fs.TileBasedRenderer()
  << "\n    UMA: " << fs.UMA()
  << "\n    CacheCoherentUMA: " << fs.CacheCoherentUMA()
  << "\n    IsolatedMMU: " << fs.IsolatedMMU()
  << "\nD3D12_OPTIONS2:"
  << "\n    DepthBoundsTestSupported: " << fs.DepthBoundsTestSupported()
  << "\n    ProgrammableSamplePositionsTier: "
  << fs.ProgrammableSamplePositionsTier()
  << std::showbase << std::hex
  << "\nShaderCacheSupportFlags: " << fs.ShaderCacheSupportFlags()
  << std::noshowbase << std::dec
  << "\nD3D12_OPTIONS3:"
  << "\n    CopyQueueTimestampQueriesSupported: "
  << fs.CopyQueueTimestampQueriesSupported()
  << "\n    CastingFullyTypedFormatSupported: "
  << fs.CastingFullyTypedFormatSupported()
  << std::showbase << std::hex
  << "\n    WriteBufferImmediateSupportFlags: "
  << fs.WriteBufferImmediateSupportFlags()
  << std::noshowbase << std::dec
  << "\n    ViewInstancingTier: " << fs.ViewInstancingTier()
  << "\n    BarycentricsSupported: " << fs.BarycentricsSupported()
  << "\nExistingHeapsSupported: " << fs.ExistingHeapsSupported()
  << "\nD3D12_OPTIONS4:"
  << "\n    MSAA64KBAlignedTextureSupported: "
  << fs.MSAA64KBAlignedTextureSupported()
  << "\n    SharedResourceCompatibilityTier: "
  << fs.SharedResourceCompatibilityTier()
  << "\n    Native16BitShaderOpsSupported: "
  << fs.Native16BitShaderOpsSupported()
  << "\nHeapSerializationTier: " << fs.HeapSerializationTier()
  << "\nCrossNodeAtomicShaderInstructions: "
  << fs.CrossNodeAtomicShaderInstructions()
  << "\nD3D12_OPTIONS5:"
  << "\n    SRVOnlyTiledResourceTier3: " << fs.SRVOnlyTiledResourceTier3()
  << "\n    RenderPassesTier: " << fs.RenderPassesTier()
  << "\n    RaytracingTier: " << fs.RaytracingTier()
  << "\nDisplayableTexture: " << fs.DisplayableTexture()
  << "\nD3D12_OPTIONS6:"
  << "\n    AdditionalShadingRatesSupported: "
  << fs.AdditionalShadingRatesSupported()
  << "\n    PerPrimitiveShadingRateSupportedWithViewportIndexing: "
  << fs.PerPrimitiveShadingRateSupportedWithViewportIndexing()
  << "\n    VariableShadingRateTier: " << fs.VariableShadingRateTier()
  << "\n    ShadingRateImageTileSize: " << fs.ShadingRateImageTileSize()
  << "\n    BackgroundProcessingSupported: "
  << fs.BackgroundProcessingSupported()
  << "\nD3D12_OPTIONS7:"
  << "\n    MeshShaderTier: " << fs.MeshShaderTier()
  << "\n    SamplerFeedbackTier: " << fs.SamplerFeedbackTier()
  << "\nD3D12_OPTIONS8:"
  << "\n    UnalignedBlockTexturesSupported: "
  << fs.UnalignedBlockTexturesSupported()
  << "\nD3D12_OPTIONS9:"
  << "\n    MeshShaderPipelineStatsSupported: "
  << fs.MeshShaderPipelineStatsSupported()
  << "\n    MeshShaderSupportsFullRangeRenderTargetArrayIndex: "
  << fs.MeshShaderSupportsFullRangeRenderTargetArrayIndex()
  << "\n    AtomicInt64OnTypedResourceSupported: "
  << fs.AtomicInt64OnTypedResourceSupported()
  << "\n    AtomicInt64OnGroupSharedSupported: "
  << fs.AtomicInt64OnGroupSharedSupported()
  << "\n    DerivativesInMeshAndAmplificationShadersSupported: "
  << fs.DerivativesInMeshAndAmplificationShadersSupported()
  << "\n    WaveMMATier: " << fs.WaveMMATier()
  << "\nD3D12_OPTIONS10:"
  << "\n    VariableRateShadingSumCombinerSupported: "
  << fs.VariableRateShadingSumCombinerSupported()
  << "\n    MeshShaderPerPrimitiveShadingRateSupported: "
  << fs.MeshShaderPerPrimitiveShadingRateSupported()
  << "\nD3D12_OPTIONS11:"
  << "\n    AtomicInt64OnDescriptorHeapResourceSupported: "
  << fs.AtomicInt64OnDescriptorHeapResourceSupported()
  << "\nD3D12_OPTIONS12:"
  << "\n    MSPrimitivesPipelineStatisticIncludesCulledPrimitives: "
  << fs.MSPrimitivesPipelineStatisticIncludesCulledPrimitives()
  << "\n    EnhancedBarriersSupported: " << fs.EnhancedBarriersSupported()
  << "\n    RelaxedFormatCastingSupported: "
  << fs.RelaxedFormatCastingSupported()
  << "\nD3D12_OPTIONS13:"
  << "\n    UnrestrictedBufferTextureCopyPitchSupported: "
  << fs.UnrestrictedBufferTextureCopyPitchSupported()
  << "\n    UnrestrictedVertexElementAlignmentSupported: "
  << fs.UnrestrictedVertexElementAlignmentSupported()
  << "\n    InvertedViewportHeightFlipsYSupported: "
  << fs.InvertedViewportHeightFlipsYSupported()
  << "\n    InvertedViewportDepthFlipsZSupported: "
  << fs.InvertedViewportDepthFlipsZSupported()
  << "\n    TextureCopyBetweenDimensionsSupported: "
  << fs.TextureCopyBetweenDimensionsSupported()
  << "\n    AlphaBlendFactorSupported: " << fs.AlphaBlendFactorSupported()
  << "\nD3D12_OPTIONS14:"
  << "\n    AdvancedTextureOpsSupported: " << fs.AdvancedTextureOpsSupported()
  << "\n    WriteableMSAATexturesSupported: "
  << fs.WriteableMSAATexturesSupported()
  << "\n    IndependentFrontAndBackStencilRefMaskSupported: "
  << fs.IndependentFrontAndBackStencilRefMaskSupported()
  << "\nD3D12_OPTIONS15:"
  << "\n    TriangleFanSupported: " << fs.TriangleFanSupported()
  << "\n    DynamicIndexBufferStripCutSupported: "
  << fs.DynamicIndexBufferStripCutSupported()
  << "\nD3D12_OPTIONS16:"
  << "\n    DynamicDepthBiasSupported: " << fs.DynamicDepthBiasSupported()
  << "\n    GPUUploadHeapSupported: " << fs.GPUUploadHeapSupported();

  auto dump_str = dump.str ();
  GST_DEBUG_OBJECT (self, "%s", dump_str.c_str ());
}
#endif
/* *INDENT-ON* */

struct TestFormatInfo
{
  DXGI_FORMAT format;
  D3D12_FORMAT_SUPPORT1 support1;
  D3D12_FORMAT_SUPPORT2 support2;
};

static GstD3D12Device *
gst_d3d12_device_new_internal (const GstD3D12DeviceConstructData * data)
{
  ComPtr < IDXGIFactory2 > factory;
  ComPtr < IDXGIAdapter1 > adapter;
  ComPtr < ID3D12Device > device;
  HRESULT hr;
  UINT factory_flags = 0;
  guint index = 0;
  /* *INDENT-OFF* */
  const TestFormatInfo required_formats[] = {
    { DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
          D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE
    },
    { DXGI_FORMAT_R10G10B10A2_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
          D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE
    },
    { DXGI_FORMAT_R16G16B16A16_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
          D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE
    },
    { DXGI_FORMAT_B8G8R8A8_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_NONE
    },
    { DXGI_FORMAT_R8_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_NONE
    },
    { DXGI_FORMAT_R8G8_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_NONE
    },
    { DXGI_FORMAT_R16_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_NONE
    },
    { DXGI_FORMAT_R16G16_UNORM,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
          D3D12_FORMAT_SUPPORT1_RENDER_TARGET,
      D3D12_FORMAT_SUPPORT2_NONE
    },
    { DXGI_FORMAT_R16_UINT,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
          D3D12_FORMAT_SUPPORT1_SHADER_LOAD,
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE
    },
    { DXGI_FORMAT_R32_UINT,
      D3D12_FORMAT_SUPPORT1_TEXTURE2D |
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
          D3D12_FORMAT_SUPPORT1_SHADER_LOAD,
      D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE
    },
  };
  /* *INDENT-ON* */

  gst_d3d12_device_enable_debug ();
  gst_d3d12_device_enable_dred ();
  gst_d3d12_device_enable_dxgi_debug ();

  hr = CreateDXGIFactory2 (factory_flags, IID_PPV_ARGS (&factory));
  if (FAILED (hr)) {
    GST_WARNING ("Could create dxgi factory, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  hr = gst_d3d12_device_find_adapter (data, factory.Get (), &index, &adapter);
  if (FAILED (hr)) {
    GST_INFO ("Could not find adapter, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  DXGI_ADAPTER_DESC1 desc;
  hr = adapter->GetDesc1 (&desc);
  if (FAILED (hr)) {
    GST_WARNING ("Could not get adapter desc, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  hr = D3D12CreateDevice (adapter.Get (), D3D_FEATURE_LEVEL_11_0,
      IID_PPV_ARGS (&device));
  if (FAILED (hr)) {
    GST_WARNING ("Could not create device, hr: 0x%x", (guint) hr);
    return nullptr;
  }

  auto self = (GstD3D12Device *) g_object_new (GST_TYPE_D3D12_DEVICE, nullptr);
  gst_object_ref_sink (self);
  self->priv->inner = std::make_shared < DeviceInner > ();
  auto priv = self->priv->inner;

  priv->factory = factory;
  priv->adapter = adapter;
  priv->device = device;
  priv->adapter_luid = gst_d3d12_luid_to_int64 (&desc.AdapterLuid);
  priv->vendor_id = desc.VendorId;
  priv->device_id = desc.DeviceId;
  priv->adapter_index = index;

  std::wstring_convert < std::codecvt_utf8 < wchar_t >, wchar_t >converter;
  priv->description = converter.to_bytes (desc.Description);

  priv->feature_support.Init (device.Get ());

  GST_INFO_OBJECT (self,
      "adapter index %d: D3D12 device vendor-id: 0x%04x, device-id: 0x%04x, "
      "Flags: 0x%x, adapter-luid: %" G_GINT64_FORMAT ", is-UMA: %d, "
      "feature-level: 0x%x, %s",
      priv->adapter_index, desc.VendorId, desc.DeviceId, desc.Flags,
      priv->adapter_luid, priv->feature_support.UMA (),
      priv->feature_support.MaxSupportedFeatureLevel (),
      priv->description.c_str ());

  /* Minimum required format support. Feature level 11.0 device should support
   * below formats */
  for (guint i = 0; i < G_N_ELEMENTS (required_formats); i++) {
    D3D12_FORMAT_SUPPORT1 support1;
    D3D12_FORMAT_SUPPORT2 support2;
    const auto & format = required_formats[i];
    hr = priv->feature_support.FormatSupport (format.format,
        support1, support2);
    if (FAILED (hr) || (support1 & format.support1) != format.support1 ||
        (support2 & format.support2) != format.support2) {
      auto format_name =
          D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetName (format.format);
      GST_WARNING_OBJECT (self, "Device does not support DXGI format %d (%s)",
          format.format, format_name);
      gst_object_unref (self);
      return nullptr;
    }
  }

#ifndef GST_DISABLE_GST_DEBUG
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_DEBUG)
    dump_feature_support (self);
#endif

  gst_d3d12_device_setup_format_table (self);
  if (priv->feature_support.UMA () && is_intel_gen11_or_older (priv->vendor_id,
          priv->feature_support.MaxSupportedFeatureLevel (),
          priv->description)) {
    priv->wa_flags |= GST_D3D12_WA_DECODER_RACE;
  }

  if (gst_d3d12_device_enable_debug ()) {
    ComPtr < ID3D12InfoQueue > info_queue;
    device.As (&info_queue);
    priv->info_queue = info_queue;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc = { };
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  priv->direct_queue = gst_d3d12_command_queue_new (device.Get (),
      &queue_desc, D3D12_FENCE_FLAG_SHARED, 32);
  if (!priv->direct_queue)
    goto error;

  priv->direct_cl_pool = gst_d3d12_command_list_pool_new (device.Get (),
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  if (!priv->direct_cl_pool)
    goto error;

  priv->direct_ca_pool = gst_d3d12_command_allocator_pool_new (device.Get (),
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  if (!priv->direct_ca_pool)
    goto error;

  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  priv->copy_queue = gst_d3d12_command_queue_new (device.Get (),
      &queue_desc, D3D12_FENCE_FLAG_NONE, 32);
  if (!priv->copy_queue)
    goto error;

  priv->copy_cl_pool = gst_d3d12_command_list_pool_new (device.Get (),
      D3D12_COMMAND_LIST_TYPE_COPY);
  if (!priv->copy_cl_pool)
    goto error;

  priv->copy_ca_pool = gst_d3d12_command_allocator_pool_new (device.Get (),
      D3D12_COMMAND_LIST_TYPE_COPY);
  if (!priv->copy_ca_pool)
    goto error;

  priv->rtv_inc_size =
      device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  priv->fence_data_pool = gst_d3d12_fence_data_pool_new ();

  {
    ComPtr < ID3D12VideoDevice > video_device;
    auto hr = device.As (&video_device);
    if (SUCCEEDED (hr)) {
      queue_desc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
      for (guint i = 0; i < G_N_ELEMENTS (priv->decode_queue); i++) {
        priv->decode_queue[i] = gst_d3d12_command_queue_new (device.Get (),
            &queue_desc, D3D12_FENCE_FLAG_NONE, 8);
        if (!priv->decode_queue[i])
          break;

        GST_OBJECT_FLAG_SET (priv->decode_queue[i],
            GST_OBJECT_FLAG_MAY_BE_LEAKED);
        priv->num_decode_queue++;

        /* XXX: Old Intel iGPU crashes with multiple decode queues */
        if ((priv->wa_flags & GST_D3D12_WA_DECODER_RACE) ==
            GST_D3D12_WA_DECODER_RACE) {
          break;
        }
      }
    }
  }

  GST_OBJECT_FLAG_SET (priv->direct_queue, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_OBJECT_FLAG_SET (priv->direct_cl_pool, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_OBJECT_FLAG_SET (priv->direct_ca_pool, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  GST_OBJECT_FLAG_SET (priv->copy_queue, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_OBJECT_FLAG_SET (priv->copy_cl_pool, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_OBJECT_FLAG_SET (priv->copy_ca_pool, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  GST_OBJECT_FLAG_SET (priv->fence_data_pool, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  hr = device->CreateFence (0,
      D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (&priv->dev_removed_fence));
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create device removed monitor fence");
    gst_object_unref (self);
    return nullptr;
  }

  hr = priv->dev_removed_fence->SetEventOnCompletion (G_MAXUINT64,
      priv->dev_removed_event);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "SetEventOnCompletion failed");
    gst_object_unref (self);
    return nullptr;
  }

  RegisterWaitForSingleObject (&priv->dev_removed_monitor_handle,
      priv->dev_removed_event, on_device_removed, priv.get (), INFINITE,
      WT_EXECUTEONLYONCE);

  return self;

error:
  gst_object_unref (self);
  return nullptr;
}

/**
 * gst_d3d12_device_new:
 * @adapter_index: DXGI adapter index
 *
 * Returns: (transfer full) (nullable): a new #GstD3D12Device for @adapter_index
 * or %NULL when failed to create D3D12 device with given adapter index.
 *
 * Since: 1.26
 */
GstD3D12Device *
gst_d3d12_device_new (guint adapter_index)
{
  auto manager = DeviceCacheManager::GetInstance ();
  GstD3D12DeviceConstructData data;
  data.data.index = adapter_index;
  data.type = GST_D3D12_DEVICE_CONSTRUCT_FOR_INDEX;

  return manager->GetDevice (&data);
}

/**
 * gst_d3d12_device_new_for_adapter_luid:
 * @adapter_luid: an int64 representation of the DXGI adapter LUID
 *
 * Returns: (transfer full) (nullable): a new #GstD3D12Device for @adapter_luid
 * or %NULL when failed to create D3D12 device with given adapter luid.
 *
 * Since: 1.26
 */
GstD3D12Device *
gst_d3d12_device_new_for_adapter_luid (gint64 adapter_luid)
{
  auto manager = DeviceCacheManager::GetInstance ();
  GstD3D12DeviceConstructData data;
  data.data.luid = adapter_luid;
  data.type = GST_D3D12_DEVICE_CONSTRUCT_FOR_LUID;

  return manager->GetDevice (&data);
}

/**
 * gst_d3d12_device_get_device_handle:
 * @device: a #GstD3D12Device
 *
 * Gets ID3D12Device handle
 *
 * Returns: (transfer none): ID3D12Device handle
 *
 * Since: 1.26
 */
ID3D12Device *
gst_d3d12_device_get_device_handle (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  return device->priv->inner->device.Get ();
}

/**
 * gst_d3d12_device_get_adapter_handle:
 * @device: a #GstD3D12Device
 *
 * Gets IDXGIAdapter1 handle
 *
 * Returns: (transfer none): IDXGIAdapter1 handle
 *
 * Since: 1.26
 */
IDXGIAdapter1 *
gst_d3d12_device_get_adapter_handle (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  return device->priv->inner->adapter.Get ();
}

/**
 * gst_d3d12_device_get_factory_handle:
 * @device: a #GstD3D12Device
 *
 * Gets IDXGIFactory2 handle
 *
 * Returns: (transfer none): IDXGIFactory2 handle
 *
 * Since: 1.26
 */
IDXGIFactory2 *
gst_d3d12_device_get_factory_handle (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  return device->priv->inner->factory.Get ();
}

/**
 * gst_d3d12_device_get_fence_handle:
 * @device: a #GstD3D12Device
 * @queue_type: a D3D12_COMMAND_LIST_TYPE
 *
 * Gets fence handle of command queue
 *
 * Returns: (transfer none): ID3D12Fence handle
 *
 * Since: 1.26
 */
ID3D12Fence *
gst_d3d12_device_get_fence_handle (GstD3D12Device * device,
    D3D12_COMMAND_LIST_TYPE queue_type)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto priv = device->priv->inner;
  GstD3D12CommandQueue *queue;

  switch (queue_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      queue = priv->direct_queue;
      break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      queue = priv->copy_queue;
      break;
    default:
      GST_ERROR_OBJECT (device, "Not supported queue type %d", queue_type);
      return nullptr;
  }

  return gst_d3d12_command_queue_get_fence_handle (queue);
}

/**
 * gst_d3d12_device_get_format:
 * @device: a #GstD3D12Device
 * @format: a #GstVideoFormat
 * @device_format: (out caller-allocates): a #GstD3D11Format
 *
 * Converts @format to #GstD3D12Format if the @format is supported
 * by device
 *
 * Returns: %TRUE if @format is supported by @device
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_device_get_format (GstD3D12Device * device,
    GstVideoFormat format, GstD3D12Format * device_format)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), FALSE);
  g_return_val_if_fail (device_format != nullptr, FALSE);

  auto priv = device->priv->inner;
  const auto & target = priv->format_table.find (format);
  if (target == priv->format_table.end ())
    return FALSE;

  *device_format = target->second;

  return TRUE;
}

/**
 * gst_d3d12_device_get_command_queue:
 * @device: a #GstD3D12Device
 * @queue_type: a D3D12_COMMAND_LIST_TYPE
 *
 * Gets #GstD3D12CommandQueue corresponding to @queue_type
 *
 * Returns: (transfer none): a #GstD3D12CommandQueue
 *
 * Since: 1.26
 */
GstD3D12CommandQueue *
gst_d3d12_device_get_command_queue (GstD3D12Device * device,
    D3D12_COMMAND_LIST_TYPE queue_type)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto priv = device->priv->inner;

  switch (queue_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      return priv->direct_queue;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      return priv->copy_queue;
    default:
      break;
  }

  GST_ERROR_OBJECT (device, "Not supported queue type %d", queue_type);

  return nullptr;
}

/**
 * gst_d3d12_device_execute_command_lists:
 * @device: a #GstD3D12Device
 * @queue_type: a D3D12_COMMAND_LIST_TYPE
 * @num_command_lists: command list size
 * @command_lists: array of ID3D12CommandList
 * @fence_value: (out) (optional): fence value of submitted command
 *
 * Exectues gst_d3d12_command_queue_execute_command_lists ()
 * using a #GstD3D12CommandQueue corresponding to @queue_type
 *
 * Returns: HRESULT code
 *
 * Since: 1.26
 */
HRESULT
gst_d3d12_device_execute_command_lists (GstD3D12Device * device,
    D3D12_COMMAND_LIST_TYPE queue_type, guint num_command_lists,
    ID3D12CommandList ** command_lists, guint64 * fence_value)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), E_INVALIDARG);

  auto priv = device->priv->inner;
  GstD3D12CommandQueue *queue;

  switch (queue_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      queue = priv->direct_queue;
      break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      queue = priv->copy_queue;
      break;
    default:
      GST_ERROR_OBJECT (device, "Not supported queue type %d", queue_type);
      return E_INVALIDARG;
  }

  return gst_d3d12_command_queue_execute_command_lists (queue,
      num_command_lists, command_lists, fence_value);
}

/**
 * gst_d3d12_device_get_completed_value:
 * @device: a #GstD3D12Device
 * @queue_type: a D3D12_COMMAND_LIST_TYPE
 *
 * Exectues gst_d3d12_command_queue_get_completed_value ()
 * using a #GstD3D12CommandQueue corresponding to @queue_type
 *
 * Returns: Completed fence value
 *
 * Since: 1.26
 */
guint64
gst_d3d12_device_get_completed_value (GstD3D12Device * device,
    D3D12_COMMAND_LIST_TYPE queue_type)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), G_MAXUINT64);

  auto priv = device->priv->inner;
  GstD3D12CommandQueue *queue;

  switch (queue_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      queue = priv->direct_queue;
      break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      queue = priv->copy_queue;
      break;
    default:
      GST_ERROR_OBJECT (device, "Not supported queue type %d", queue_type);
      return G_MAXUINT64;
  }

  return gst_d3d12_command_queue_get_completed_value (queue);
}

/**
 * gst_d3d12_device_set_fence_notify:
 * @device: a #GstD3D12Device
 * @queue_type: a D3D12_COMMAND_LIST_TYPE
 * @fence_value: target fence value
 * @fence_data: user data
 * @notify: a #GDestroyNotify
 *
 * Exectues gst_d3d12_command_queue_set_notify ()
 * using a #GstD3D12CommandQueue corresponding to @queue_type
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_device_set_fence_notify (GstD3D12Device * device,
    D3D12_COMMAND_LIST_TYPE queue_type, guint64 fence_value,
    gpointer fence_data, GDestroyNotify notify)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), FALSE);
  g_return_val_if_fail (fence_data, FALSE);

  auto priv = device->priv->inner;
  GstD3D12CommandQueue *queue;

  switch (queue_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      queue = priv->direct_queue;
      break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      queue = priv->copy_queue;
      break;
    default:
      GST_ERROR_OBJECT (device, "Not supported queue type %d", queue_type);
      return FALSE;
  }

  gst_d3d12_command_queue_set_notify (queue, fence_value, fence_data, notify);

  return TRUE;
}

/**
 * gst_d3d12_device_fence_wait:
 * @device: a #GstD3D12Device
 * @queue_type: a D3D12_COMMAND_LIST_TYPE
 * @fence_value: target fence value
 * @handle: (nullable) (transfer none): event handle used for fence wait
 *
 * Exectues gst_d3d12_command_queue_fence_wait ()
 * using a #GstD3D12CommandQueue corresponding to @queue_type
 *
 * Returns: HRESULT code
 *
 * Since: 1.26
 */
HRESULT
gst_d3d12_device_fence_wait (GstD3D12Device * device,
    D3D12_COMMAND_LIST_TYPE queue_type, guint64 fence_value,
    HANDLE event_handle)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), E_INVALIDARG);

  auto priv = device->priv->inner;
  GstD3D12CommandQueue *queue;

  switch (queue_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      queue = priv->direct_queue;
      break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      queue = priv->copy_queue;
      break;
    default:
      GST_ERROR_OBJECT (device, "Not supported queue type %d", queue_type);
      return E_INVALIDARG;
  }

  return gst_d3d12_command_queue_fence_wait (queue, fence_value, event_handle);
}

gboolean
gst_d3d12_device_copy_texture_region (GstD3D12Device * device,
    guint num_args, const GstD3D12CopyTextureRegionArgs * args,
    GstD3D12FenceData * fence_data, guint num_fences_to_wait,
    ID3D12Fence ** fences_to_wait, const guint64 * fence_values_to_wait,
    D3D12_COMMAND_LIST_TYPE command_type, guint64 * fence_value)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), FALSE);
  g_return_val_if_fail (num_args > 0, FALSE);
  g_return_val_if_fail (args, FALSE);

  HRESULT hr;
  auto priv = device->priv->inner;
  GstD3D12CommandAllocatorPool *ca_pool;
  GstD3D12CommandAllocator *gst_ca = nullptr;
  GstD3D12CommandListPool *cl_pool;
  GstD3D12CommandList *gst_cl = nullptr;
  GstD3D12CommandQueue *queue = nullptr;
  guint64 fence_val = 0;

  if (!fence_data)
    gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  switch (command_type) {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
      queue = priv->direct_queue;
      ca_pool = priv->direct_ca_pool;
      cl_pool = priv->direct_cl_pool;
      break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
      queue = priv->copy_queue;
      ca_pool = priv->copy_ca_pool;
      cl_pool = priv->copy_cl_pool;
      break;
    default:
      GST_ERROR_OBJECT (device, "Not supported command list type %d",
          command_type);
      gst_d3d12_fence_data_unref (fence_data);
      return FALSE;
  }

  gst_d3d12_command_allocator_pool_acquire (ca_pool, &gst_ca);
  if (!gst_ca) {
    GST_ERROR_OBJECT (device, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto ca = gst_d3d12_command_allocator_get_handle (gst_ca);
  gst_d3d12_command_list_pool_acquire (cl_pool, ca, &gst_cl);

  if (!gst_cl) {
    GST_ERROR_OBJECT (device, "Couldn't acquire command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  ComPtr < ID3D12CommandList > cl_base;
  ComPtr < ID3D12GraphicsCommandList > cl;

  cl_base = gst_d3d12_command_list_get_handle (gst_cl);
  cl_base.As (&cl);

  for (guint i = 0; i < num_args; i++) {
    const auto arg = args[i];
    cl->CopyTextureRegion (&arg.dst, arg.dst_x, arg.dst_y, arg.dst_z,
        &arg.src, arg.src_box);
  }

  hr = cl->Close ();
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (device, "Couldn't close command list");
    gst_clear_d3d12_command_list (&gst_cl);
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  ID3D12CommandList *cmd_list[] = { cl.Get () };

  hr = gst_d3d12_command_queue_execute_command_lists_full (queue,
      num_fences_to_wait, fences_to_wait, fence_values_to_wait, 1, cmd_list,
      &fence_val);
  auto ret = gst_d3d12_result (hr, device);

  /* We can release command list since command list pool will hold it */
  gst_d3d12_command_list_unref (gst_cl);

  if (ret) {
    gst_d3d12_command_queue_set_notify (queue, fence_val, fence_data,
        (GDestroyNotify) gst_d3d12_fence_data_unref);
  } else {
    gst_d3d12_fence_data_unref (fence_data);
  }

  if (fence_value)
    *fence_value = fence_val;

  return ret;
}

gboolean
gst_d3d12_device_acquire_fence_data (GstD3D12Device * device,
    GstD3D12FenceData ** fence_data)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), FALSE);
  g_return_val_if_fail (fence_data, FALSE);

  auto priv = device->priv->inner;

  return gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, fence_data);
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

void
gst_d3d12_device_d3d12_debug (GstD3D12Device * device, const gchar * file,
    const gchar * function, gint line)
{
  g_return_if_fail (GST_IS_D3D12_DEVICE (device));

  auto priv = device->priv->inner;
  if (priv->info_queue) {
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

#ifdef HAVE_DXGIDEBUG_H
  if (gst_d3d12_device_enable_dxgi_debug ()) {
    std::lock_guard < std::mutex > lk (g_dxgi_debug_lock);

    UINT64 num_msg = g_dxgi_info_queue->GetNumStoredMessages (DXGI_DEBUG_ALL);
    for (UINT64 i = 0; i < num_msg; i++) {
      SIZE_T msg_len;
      auto hr = g_dxgi_info_queue->GetMessage (DXGI_DEBUG_ALL,
          i, nullptr, &msg_len);
      if (FAILED (hr) || msg_len == 0)
        continue;

      auto msg = (DXGI_INFO_QUEUE_MESSAGE *) g_malloc0 (msg_len);
      hr = g_dxgi_info_queue->GetMessage (DXGI_DEBUG_ALL, i, msg, &msg_len);

      GstDebugLevel level = GST_LEVEL_LOG;
      switch (msg->Severity) {
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION:
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR:
          level = GST_LEVEL_ERROR;
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING:
          level = GST_LEVEL_WARNING;
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO:
          level = GST_LEVEL_INFO;
        case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE:
          level = GST_LEVEL_DEBUG;
        default:
          break;
      }

      gst_debug_log (gst_d3d12_dxgi_debug, level, file, function, line,
          G_OBJECT (device), "DXGIInfoQueue: %s", msg->pDescription);
      g_free (msg);
    }

    g_dxgi_info_queue->ClearStoredMessages (DXGI_DEBUG_ALL);
  }
#endif
}

void
gst_d3d12_device_clear_yuv_texture (GstD3D12Device * device, GstMemory * mem)
{
  auto priv = device->priv->inner;
  auto dmem = GST_D3D12_MEMORY_CAST (mem);

  auto resource = gst_d3d12_memory_get_resource_handle (dmem);
  auto desc = GetDesc (resource);

  if (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010 &&
      desc.Format != DXGI_FORMAT_P016) {
    return;
  }

  auto heap = gst_d3d12_memory_get_render_target_view_heap (dmem);
  if (!heap)
    return;

  D3D12_RECT rect = { };
  if (!gst_d3d12_memory_get_plane_rectangle (dmem, 1, &rect))
    return;

  GstD3D12CommandAllocator *gst_ca = nullptr;
  gst_d3d12_command_allocator_pool_acquire (priv->direct_ca_pool, &gst_ca);
  if (!gst_ca)
    return;

  auto ca = gst_d3d12_command_allocator_get_handle (gst_ca);

  GstD3D12CommandList *gst_cl = nullptr;
  gst_d3d12_command_list_pool_acquire (priv->direct_cl_pool, ca, &gst_cl);
  if (!gst_cl) {
    gst_d3d12_command_allocator_unref (gst_ca);
    return;
  }

  ComPtr < ID3D12CommandList > cl_base;
  ComPtr < ID3D12GraphicsCommandList > cl;

  cl_base = gst_d3d12_command_list_get_handle (gst_cl);
  cl_base.As (&cl);

  auto rtv_handle =
      CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart (heap),
      priv->rtv_inc_size);

  const FLOAT clear_color[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
  cl->ClearRenderTargetView (rtv_handle, clear_color, 1, &rect);

  auto hr = cl->Close ();
  if (!gst_d3d12_result (hr, device)) {
    gst_clear_d3d12_command_list (&gst_cl);
    gst_clear_d3d12_command_allocator (&gst_ca);
    return;
  }

  ID3D12CommandList *cmd_list[] = { cl.Get () };
  guint64 fence_val = 0;
  auto fence = gst_d3d12_command_queue_get_fence_handle (priv->direct_queue);
  hr = gst_d3d12_command_queue_execute_command_lists (priv->direct_queue,
      1, cmd_list, &fence_val);
  auto ret = gst_d3d12_result (hr, device);
  gst_d3d12_command_list_unref (gst_cl);

  if (ret) {
    gst_d3d12_command_queue_set_notify (priv->direct_queue, fence_val,
        gst_ca, (GDestroyNotify) gst_d3d12_command_allocator_unref);
    gst_d3d12_memory_set_fence (dmem, fence, fence_val, FALSE);
  } else {
    gst_d3d12_command_allocator_unref (gst_ca);
  }
}

/**
 * gst_d3d12_device_is_equal:
 * @device1: (transfer none) (nullable): a #GstD3D12Device
 * @device2: (transfer none) (nullable): a #GstD3D12Device
 *
 * Checks if the given devices represent the same device
 *
 * Returns: %TRUE if both devices are valid and equal
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_device_is_equal (GstD3D12Device * device1, GstD3D12Device * device2)
{
  if (!device1 || !device2)
    return FALSE;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device1), FALSE);
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device2), FALSE);

  if (device1 == device2)
    return TRUE;

  if (device1->priv->inner == device2->priv->inner)
    return TRUE;

  return FALSE;
}

IUnknown *
gst_d3d12_device_get_11on12_handle (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto priv = device->priv->inner;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (!priv->device11on12) {
    if (!load_d3d11on12_symbol ()) {
      GST_WARNING_OBJECT (device, "D3D11On12CreateDevice symbol was not found");
      return nullptr;
    }

    static const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_12_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
    };

    IUnknown *cq[] =
        { gst_d3d12_command_queue_get_handle (priv->direct_queue) };
    ComPtr < ID3D11Device > device11;
    auto hr = GstD3D11On12CreateDevice (priv->device.Get (),
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels,
        G_N_ELEMENTS (feature_levels), cq, 1, 0, &device11, nullptr, nullptr);
    if (FAILED (hr)) {
      GST_WARNING_OBJECT (device, "Couldn't create 11on12 device, hr: 0x%x",
          (guint) hr);
      return nullptr;
    }

    hr = device11.As (&priv->device11on12);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't get 11on12 interface");
      return nullptr;
    }
  }

  return priv->device11on12.Get ();
}

void
gst_d3d12_device_11on12_lock (GstD3D12Device * device)
{
  g_return_if_fail (GST_IS_D3D12_DEVICE (device));

  auto priv = device->priv->inner;
  priv->device11on12_lock.lock ();
}

void
gst_d3d12_device_11on12_unlock (GstD3D12Device * device)
{
  g_return_if_fail (GST_IS_D3D12_DEVICE (device));

  auto priv = device->priv->inner;
  priv->device11on12_lock.unlock ();
}

void
gst_d3d12_device_check_device_removed (GstD3D12Device * device)
{
  g_return_if_fail (GST_IS_D3D12_DEVICE (device));

  auto priv = device->priv->inner;
  auto hr = priv->device->GetDeviceRemovedReason ();
  if (FAILED (hr)) {
    auto manager = DeviceCacheManager::GetInstance ();
    manager->OnDeviceRemoved (priv->adapter_luid);
  }
}

GstD3D12CommandQueue *
gst_d3d12_device_get_decode_queue (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  auto priv = device->priv->inner;

  if (!priv->num_decode_queue)
    return nullptr;

  std::lock_guard < std::mutex > lk (priv->lock);
  auto queue = priv->decode_queue[priv->decode_queue_index];
  priv->decode_queue_index++;
  priv->decode_queue_index %= priv->num_decode_queue;

  return queue;
}

void
gst_d3d12_device_decoder_lock (GstD3D12Device * device)
{
  g_return_if_fail (GST_IS_D3D12_DEVICE (device));

  auto priv = device->priv->inner;
  if ((priv->wa_flags & GST_D3D12_WA_DECODER_RACE) == GST_D3D12_WA_DECODER_RACE)
    priv->decoder_lock.lock ();
}

void
gst_d3d12_device_decoder_unlock (GstD3D12Device * device)
{
  g_return_if_fail (GST_IS_D3D12_DEVICE (device));

  auto priv = device->priv->inner;
  if ((priv->wa_flags & GST_D3D12_WA_DECODER_RACE) == GST_D3D12_WA_DECODER_RACE)
    priv->decoder_lock.unlock ();
}

GstD3D12WAFlags
gst_d3d12_device_get_workaround_flags (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), GST_D3D12_WA_NONE);

  return device->priv->inner->wa_flags;
}
