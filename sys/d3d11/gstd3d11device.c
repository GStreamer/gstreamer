/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include "gstd3d11device.h"
#include "gmodule.h"

#ifdef HAVE_D3D11SDKLAYER_H
#include <d3d11sdklayers.h>
#endif

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);
GST_DEBUG_CATEGORY_STATIC (gst_d3d11_device_debug);
#define GST_CAT_DEFAULT gst_d3d11_device_debug

#ifdef HAVE_D3D11SDKLAYER_H
static GModule *sdk_layer = NULL;
#endif

enum
{
  PROP_0,
  PROP_ADAPTER
};

#define DEFAULT_ADAPTER -1

struct _GstD3D11DevicePrivate
{
  gint adapter;

  ID3D11Device *device;
  ID3D11DeviceContext *device_context;

  IDXGIFactory1 *factory;
  GstD3D11DXGIFactoryVersion factory_ver;

  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;

  GMutex lock;
  GCond cond;
  GThread *thread;
  GThread *active_thread;
  GMainLoop *loop;
  GMainContext *main_context;

#ifdef HAVE_D3D11SDKLAYER_H
  ID3D11Debug *debug;
  ID3D11InfoQueue *info_queue;
#endif
};

#define gst_d3d11_device_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11Device, gst_d3d11_device, GST_TYPE_OBJECT);

static void gst_d3d11_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_device_constructed (GObject * object);
static void gst_d3d11_device_dispose (GObject * object);
static void gst_d3d11_device_finalize (GObject * object);

static gpointer gst_d3d11_device_thread_func (gpointer data);

#ifdef HAVE_D3D11SDKLAYER_H
static gboolean
gst_d3d11_device_enable_debug_layer (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    sdk_layer = g_module_open ("d3d11sdklayers.dll", G_MODULE_BIND_LAZY);

    if (!sdk_layer)
      sdk_layer = g_module_open ("d3d11_1sdklayers.dll", G_MODULE_BIND_LAZY);

    g_once_init_leave (&_init, 1);
  }

  return ! !sdk_layer;
}

static gboolean
gst_d3d11_device_get_message (GstD3D11Device * self)
{
  GstD3D11DevicePrivate *priv = self->priv;
  D3D11_MESSAGE *msg;
  SIZE_T msg_len = 0;
  HRESULT hr;
  UINT64 num_msg, i;

  num_msg = ID3D11InfoQueue_GetNumStoredMessages (priv->info_queue);

  for (i = 0; i < num_msg; i++) {
    hr = ID3D11InfoQueue_GetMessage (priv->info_queue, i, NULL, &msg_len);

    if (FAILED (hr) || msg_len == 0) {
      return G_SOURCE_CONTINUE;
    }

    msg = (D3D11_MESSAGE *) g_malloc0 (msg_len);
    hr = ID3D11InfoQueue_GetMessage (priv->info_queue, i, msg, &msg_len);

    GST_TRACE_OBJECT (self, "D3D11 Message - %s", msg->pDescription);
    g_free (msg);
  }

  ID3D11InfoQueue_ClearStoredMessages (priv->info_queue);

  return G_SOURCE_CONTINUE;
}
#endif

static void
gst_d3d11_device_class_init (GstD3D11DeviceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_device_set_property;
  gobject_class->get_property = gst_d3d11_device_get_property;
  gobject_class->constructed = gst_d3d11_device_constructed;
  gobject_class->dispose = gst_d3d11_device_dispose;
  gobject_class->finalize = gst_d3d11_device_finalize;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_device_debug,
      "d3d11device", 0, "d3d11 device");
  GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
}

static void
gst_d3d11_device_init (GstD3D11Device * self)
{
  GstD3D11DevicePrivate *priv;

  priv = gst_d3d11_device_get_instance_private (self);
  priv->adapter = DEFAULT_ADAPTER;

  g_mutex_init (&priv->lock);
  g_cond_init (&priv->cond);

  priv->main_context = g_main_context_new ();
  priv->loop = g_main_loop_new (priv->main_context, FALSE);

  self->priv = priv;
}

static void
_relase_adapter (IDXGIAdapter1 * adapter)
{
  IDXGIAdapter1_Release (adapter);
}

static void
gst_d3d11_device_constructed (GObject * object)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (object);
  GstD3D11DevicePrivate *priv = self->priv;
  IDXGIAdapter1 *adapter = NULL;
  GList *adapter_list = NULL;
  GList *hw_adapter_list = NULL;
  IDXGIFactory1 *factory = NULL;
  HRESULT hr;
  guint i;
  guint num_adapter = 0;
  UINT d3d11_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

  static const D3D_DRIVER_TYPE driver_types[] = {
    D3D_DRIVER_TYPE_HARDWARE,
    D3D_DRIVER_TYPE_WARP,
    D3D_DRIVER_TYPE_UNKNOWN
  };
  static const D3D_FEATURE_LEVEL feature_levels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1
  };
  D3D_FEATURE_LEVEL selected_level;

#ifdef HAVE_DXGI_1_5_H
  hr = CreateDXGIFactory1 (&IID_IDXGIFactory5, (void **) &factory);
  if (FAILED (hr)) {
    GST_INFO_OBJECT (self, "IDXGIFactory5 was unavailable");
    factory = NULL;
  }

  priv->factory_ver = GST_D3D11_DXGI_FACTORY_5;
#endif

  if (!factory) {
    priv->factory_ver = GST_D3D11_DXGI_FACTORY_1;
    hr = CreateDXGIFactory1 (&IID_IDXGIFactory1, (void **) &factory);
  }

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "cannot create dxgi factory, hr: 0x%x", (guint) hr);
    goto error;
  }

  while (IDXGIFactory1_EnumAdapters1 (factory, num_adapter,
          &adapter) != DXGI_ERROR_NOT_FOUND) {
    DXGI_ADAPTER_DESC1 desc;

    hr = IDXGIAdapter1_GetDesc1 (adapter, &desc);
    if (SUCCEEDED (hr)) {
      gchar *vender = NULL;

      vender = g_utf16_to_utf8 (desc.Description, -1, NULL, NULL, NULL);
      GST_DEBUG_OBJECT (self,
          "adapter index %d: D3D11 device vendor-id: 0x%04x, device-id: 0x%04x, "
          "Flags: 0x%x, %s",
          num_adapter, desc.VendorId, desc.DeviceId, desc.Flags, vender);
      g_free (vender);

      /* DXGI_ADAPTER_FLAG_SOFTWARE is missing in dxgi.h of mingw */
      if ((desc.Flags & 0x2) != 0x2) {
        hw_adapter_list = g_list_append (hw_adapter_list, adapter);
        IDXGIAdapter1_AddRef (adapter);
      }
    }

    adapter_list = g_list_append (adapter_list, adapter);

    num_adapter++;

    if (priv->adapter >= 0 && priv->adapter < num_adapter)
      break;
  }

  adapter = NULL;
  if (priv->adapter >= 0) {
    if (priv->adapter >= num_adapter) {
      GST_WARNING_OBJECT (self,
          "Requested index %d is out of scope for adapter", priv->adapter);
    } else {
      adapter = (IDXGIAdapter1 *) g_list_nth_data (adapter_list, priv->adapter);
    }
  } else if (hw_adapter_list) {
    adapter = (IDXGIAdapter1 *) g_list_nth_data (hw_adapter_list, 0);
  } else if (adapter_list) {
    adapter = (IDXGIAdapter1 *) g_list_nth_data (adapter_list, 0);
  }

  if (adapter)
    IDXGIAdapter1_AddRef (adapter);

#ifdef HAVE_D3D11SDKLAYER_H
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
    /* DirectX SDK should be installed on system for this */
    if (gst_d3d11_device_enable_debug_layer ()) {
      GST_INFO_OBJECT (self, "sdk layer library was loaded");
      d3d11_flags |= D3D11_CREATE_DEVICE_DEBUG;
    }
  }
#endif

  if (adapter) {
    hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
        NULL, d3d11_flags, feature_levels, G_N_ELEMENTS (feature_levels),
        D3D11_SDK_VERSION, &priv->device, &selected_level,
        &priv->device_context);

    if (FAILED (hr)) {
      /* Retry if the system could not recognize D3D_FEATURE_LEVEL_11_1 */
      hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
          NULL, d3d11_flags, &feature_levels[1],
          G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION, &priv->device,
          &selected_level, &priv->device_context);
    }

    if (SUCCEEDED (hr)) {
      GST_DEBUG_OBJECT (self, "Selected feature level 0x%x", selected_level);
    }
  } else {
    for (i = 0; i < G_N_ELEMENTS (driver_types); i++) {
      hr = D3D11CreateDevice (NULL, driver_types[i], NULL,
          d3d11_flags,
          feature_levels, G_N_ELEMENTS (feature_levels),
          D3D11_SDK_VERSION, &priv->device, &selected_level,
          &priv->device_context);

      if (FAILED (hr)) {
        /* Retry if the system could not recognize D3D_FEATURE_LEVEL_11_1 */
        hr = D3D11CreateDevice (NULL, driver_types[i], NULL,
            d3d11_flags,
            &feature_levels[1], G_N_ELEMENTS (feature_levels) - 1,
            D3D11_SDK_VERSION, &priv->device, &selected_level,
            &priv->device_context);
      }

      if (SUCCEEDED (hr)) {
        GST_DEBUG_OBJECT (self, "Selected driver type 0x%x, feature level 0x%x",
            driver_types[i], selected_level);
        break;
      }
    }
  }

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "cannot create d3d11 device, hr: 0x%x", (guint) hr);
    goto error;
  }

  priv->factory = factory;

  if (adapter)
    IDXGIAdapter1_Release (adapter);

  if (adapter_list)
    g_list_free_full (adapter_list, (GDestroyNotify) _relase_adapter);

  if (hw_adapter_list)
    g_list_free_full (hw_adapter_list, (GDestroyNotify) _relase_adapter);

#ifdef HAVE_D3D11SDKLAYER_H
  if ((d3d11_flags & D3D11_CREATE_DEVICE_DEBUG) == D3D11_CREATE_DEVICE_DEBUG) {
    ID3D11Debug *debug;
    ID3D11InfoQueue *info_queue;

    hr = ID3D11Device_QueryInterface (priv->device,
        &IID_ID3D11Debug, (void **) &debug);

    if (SUCCEEDED (hr)) {
      GST_DEBUG_OBJECT (self, "D3D11Debug interface available");
      ID3D11Debug_ReportLiveDeviceObjects (debug, D3D11_RLDO_DETAIL);
      priv->debug = debug;
    }

    hr = ID3D11Device_QueryInterface (priv->device,
        &IID_ID3D11InfoQueue, (void **) &info_queue);
    if (SUCCEEDED (hr)) {
      GSource *source;

      GST_DEBUG_OBJECT (self, "D3D11InfoQueue interface available");
      priv->info_queue = info_queue;

      source = g_idle_source_new ();
      g_source_set_callback (source, (GSourceFunc) gst_d3d11_device_get_message,
          self, NULL);

      g_source_attach (source, priv->main_context);
      g_source_unref (source);
    }
  }
#endif

  g_mutex_lock (&priv->lock);
  priv->thread = g_thread_new ("GstD3D11Device",
      (GThreadFunc) gst_d3d11_device_thread_func, self);
  while (!g_main_loop_is_running (priv->loop))
    g_cond_wait (&priv->cond, &priv->lock);
  g_mutex_unlock (&priv->lock);

  G_OBJECT_CLASS (parent_class)->constructed (object);

  return;

error:
  if (factory)
    IDXGIFactory1_Release (factory);

  if (adapter)
    IDXGIAdapter1_Release (adapter);

  if (adapter_list)
    g_list_free_full (adapter_list, (GDestroyNotify) _relase_adapter);

  if (hw_adapter_list)
    g_list_free_full (hw_adapter_list, (GDestroyNotify) _relase_adapter);

  G_OBJECT_CLASS (parent_class)->constructed (object);

  return;
}

static void
gst_d3d11_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (object);
  GstD3D11DevicePrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_ADAPTER:
      priv->adapter = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (object);
  GstD3D11DevicePrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_device_dispose (GObject * object)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (object);
  GstD3D11DevicePrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "dispose");

  if (priv->loop) {
    g_main_loop_quit (priv->loop);
  }

  if (priv->thread) {
    g_thread_join (priv->thread);
    priv->thread = NULL;
  }

  if (priv->loop) {
    g_main_loop_unref (priv->loop);
    priv->loop = NULL;
  }

  if (priv->main_context) {
    g_main_context_unref (priv->main_context);
    priv->main_context = NULL;
  }
#ifdef HAVE_D3D11SDKLAYER_H
  if (priv->debug) {
    ID3D11Debug_Release (priv->debug);
    priv->debug = NULL;
  }

  if (priv->info_queue) {
    ID3D11InfoQueue_ClearStoredMessages (priv->info_queue);
    ID3D11InfoQueue_Release (priv->info_queue);
    priv->info_queue = NULL;
  }
#endif

  if (priv->device) {
    ID3D11Device_Release (priv->device);
    priv->device = NULL;
  }

  if (priv->device_context) {
    ID3D11DeviceContext_Release (priv->device_context);
    priv->device_context = NULL;
  }

  if (priv->video_device) {
    ID3D11VideoDevice_Release (priv->video_device);
    priv->video_device = NULL;
  }

  if (priv->video_context) {
    ID3D11VideoContext_Release (priv->video_context);
    priv->video_context = NULL;
  }

  if (priv->factory) {
    IDXGIFactory1_Release (priv->factory);
    priv->factory = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_device_finalize (GObject * object)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (object);
  GstD3D11DevicePrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "finalize");

  g_mutex_clear (&priv->lock);
  g_cond_clear (&priv->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
running_cb (gpointer user_data)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (user_data);
  GstD3D11DevicePrivate *priv = self->priv;

  GST_TRACE_OBJECT (self, "Main loop running now");

  g_mutex_lock (&priv->lock);
  g_cond_signal (&priv->cond);
  g_mutex_unlock (&priv->lock);

  return G_SOURCE_REMOVE;
}

static gpointer
gst_d3d11_device_thread_func (gpointer data)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (data);
  GstD3D11DevicePrivate *priv = self->priv;
  GSource *source;

  GST_DEBUG_OBJECT (self, "Enter loop");
  g_main_context_push_thread_default (priv->main_context);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) running_cb, self, NULL);
  g_source_attach (source, priv->main_context);
  g_source_unref (source);

  priv->active_thread = g_thread_self ();
  g_main_loop_run (priv->loop);

  g_main_context_pop_thread_default (priv->main_context);

  GST_DEBUG_OBJECT (self, "Exit loop");

  return NULL;
}

/**
 * gst_d3d11_device_new:
 * @adapter: the index of adapter for creating d3d11 device (-1 for default)
 *
 * Returns: (transfer full) (nullable): a new #GstD3D11Device for @adapter or %NULL
 * when failed to create D3D11 device with given adapter index.
 */
GstD3D11Device *
gst_d3d11_device_new (gint adapter)
{
  GstD3D11Device *device = NULL;
  GstD3D11DevicePrivate *priv;
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_d3d11_device_debug, "d3d11device", 0,
        "d3d11 device");
    g_once_init_leave (&_init, 1);
  }

  device = g_object_new (GST_TYPE_D3D11_DEVICE, "adapter", adapter, NULL);

  priv = device->priv;

  if (!priv->device || !priv->device_context) {
    GST_ERROR ("Cannot create d3d11 device");
    g_object_unref (device);
    device = NULL;
  }

  return device;
}

/**
 * gst_d3d11_device_get_device:
 * @device: a #GstD3D11Device
 *
 * Used for various D3D11 APIs directly.
 * Caller must not destroy returned device object.
 *
 * Returns: (transfer none): the ID3D11Device
 */
ID3D11Device *
gst_d3d11_device_get_device (GstD3D11Device * device)
{
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  return device->priv->device;
}

/**
 * gst_d3d11_device_get_device_context:
 * @device: a #GstD3D11Device
 *
 * Used for various D3D11 APIs directly.
 * Caller must not destroy returned device object.
 *
 * Returns: (transfer none): the ID3D11DeviceContext
 */
ID3D11DeviceContext *
gst_d3d11_device_get_device_context (GstD3D11Device * device)
{
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  return device->priv->device_context;
}

GstD3D11DXGIFactoryVersion
gst_d3d11_device_get_chosen_dxgi_factory_version (GstD3D11Device * device)
{
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device),
      GST_D3D11_DXGI_FACTORY_UNKNOWN);

  return device->priv->factory_ver;
}

typedef struct
{
  GstD3D11Device *device;
  GstD3D11DeviceThreadFunc func;

  gpointer data;
  gboolean fired;
} MessageData;

static gboolean
gst_d3d11_device_message_callback (MessageData * msg)
{
  GstD3D11Device *self = msg->device;
  GstD3D11DevicePrivate *priv = self->priv;

  msg->func (self, msg->data);

  g_mutex_lock (&priv->lock);
  msg->fired = TRUE;
  g_cond_broadcast (&priv->cond);
  g_mutex_unlock (&priv->lock);

  return G_SOURCE_REMOVE;
}

/**
 * gst_d3d11_device_thread_add:
 * @device: a #GstD3D11Device
 * @func: (scope call): a #GstD3D11DeviceThreadFunc
 * @data: (closure): user data to call @func with
 *
 * Execute @func in the D3DDevice thread of @device with @data
 *
 * MT-safe
 */
void
gst_d3d11_device_thread_add (GstD3D11Device * device,
    GstD3D11DeviceThreadFunc func, gpointer data)
{
  GstD3D11DevicePrivate *priv;
  MessageData msg = { 0, };

  g_return_if_fail (GST_IS_D3D11_DEVICE (device));
  g_return_if_fail (func != NULL);

  priv = device->priv;

  if (priv->active_thread == g_thread_self ()) {
    func (device, data);
    return;
  }

  msg.device = gst_object_ref (device);
  msg.func = func;
  msg.data = data;
  msg.fired = FALSE;

  g_main_context_invoke (priv->main_context,
      (GSourceFunc) gst_d3d11_device_message_callback, &msg);

  g_mutex_lock (&priv->lock);
  while (!msg.fired)
    g_cond_wait (&priv->cond, &priv->lock);
  g_mutex_unlock (&priv->lock);

  gst_object_unref (device);
}

typedef struct
{
  IDXGISwapChain *swap_chain;
  const DXGI_SWAP_CHAIN_DESC *desc;
} CreateSwapChainData;

static void
gst_d3d11_device_create_swap_chain_internal (GstD3D11Device * device,
    CreateSwapChainData * data)
{
  GstD3D11DevicePrivate *priv = device->priv;
  HRESULT hr;

  hr = IDXGIFactory1_CreateSwapChain (priv->factory, (IUnknown *) priv->device,
      (DXGI_SWAP_CHAIN_DESC *) data->desc, &data->swap_chain);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (device, "Cannot create SwapChain Object: 0x%x",
        (guint) hr);
    data->swap_chain = NULL;
  }
}

/**
 * gst_d3d11_device_create_swap_chain:
 * @device: a #GstD3D11Device
 * @desc: a DXGI_SWAP_CHAIN_DESC structure for swapchain
 *
 * Create a IDXGISwapChain object. Caller must release returned swap chain object
 * via IDXGISwapChain_Release()
 *
 * Returns: (transfer full) (nullable): a new IDXGISwapChain or %NULL
 * when failed to create swap chain with given @desc
 */
IDXGISwapChain *
gst_d3d11_device_create_swap_chain (GstD3D11Device * device,
    const DXGI_SWAP_CHAIN_DESC * desc)
{
  CreateSwapChainData data = { 0, };

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  data.swap_chain = NULL;
  data.desc = desc;

  gst_d3d11_device_thread_add (device, (GstD3D11DeviceThreadFunc)
      gst_d3d11_device_create_swap_chain_internal, &data);

  return data.swap_chain;
}

static void
gst_d3d11_device_release_swap_chain_internal (GstD3D11Device * device,
    IDXGISwapChain * swap_chain)
{
  IDXGISwapChain_Release (swap_chain);
}

/**
 * gst_d3d11_device_release_swap_chain:
 * @device: a #GstD3D11Device
 * @swap_chain: a IDXGISwapChain
 *
 * Release a @swap_chain from device thread
 *
 */
void
gst_d3d11_device_release_swap_chain (GstD3D11Device * device,
    IDXGISwapChain * swap_chain)
{
  g_return_if_fail (GST_IS_D3D11_DEVICE (device));

  gst_d3d11_device_thread_add (device,
      (GstD3D11DeviceThreadFunc) gst_d3d11_device_release_swap_chain_internal,
      swap_chain);
}

typedef struct
{
  ID3D11Texture2D *texture;
  const D3D11_TEXTURE2D_DESC *desc;
  const D3D11_SUBRESOURCE_DATA *inital_data;
} CreateTextureData;

static void
gst_d3d11_device_create_texture_internal (GstD3D11Device * device,
    CreateTextureData * data)
{
  GstD3D11DevicePrivate *priv = device->priv;
  HRESULT hr;

  hr = ID3D11Device_CreateTexture2D (priv->device, data->desc,
      data->inital_data, &data->texture);
  if (FAILED (hr)) {
    GST_ERROR ("Failed to create staging texture (0x%x)", (guint) hr);
    data->texture = NULL;
  }
}

ID3D11Texture2D *
gst_d3d11_device_create_texture (GstD3D11Device * device,
    const D3D11_TEXTURE2D_DESC * desc,
    const D3D11_SUBRESOURCE_DATA * inital_data)
{
  CreateTextureData data;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (desc != NULL, NULL);

  data.texture = NULL;
  data.desc = desc;
  data.inital_data = inital_data;

  gst_d3d11_device_thread_add (device, (GstD3D11DeviceThreadFunc)
      gst_d3d11_device_create_texture_internal, &data);

  return data.texture;
}

static void
gst_d3d11_device_release_texture_internal (GstD3D11Device * device,
    ID3D11Texture2D * texture)
{
  ID3D11Texture2D_Release (texture);
}

void
gst_d3d11_device_release_texture (GstD3D11Device * device,
    ID3D11Texture2D * texture)
{
  g_return_if_fail (GST_IS_D3D11_DEVICE (device));
  g_return_if_fail (texture != NULL);

  gst_d3d11_device_thread_add (device, (GstD3D11DeviceThreadFunc)
      gst_d3d11_device_release_texture_internal, texture);
}

/**
 * gst_context_set_d3d11_device:
 * @context: a #GstContext
 * @device: (transfer none): resulting #GstD3D11Device
 *
 * Sets @device on @context
 */
void
gst_context_set_d3d11_device (GstContext * context, GstD3D11Device * device)
{
  GstStructure *s;
  const gchar *context_type;

  g_return_if_fail (GST_IS_CONTEXT (context));
  g_return_if_fail (GST_IS_D3D11_DEVICE (device));

  context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE) != 0)
    return;

  GST_CAT_LOG (GST_CAT_CONTEXT,
      "setting GstD3DDevice(%" GST_PTR_FORMAT ") on context(%" GST_PTR_FORMAT
      ")", device, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, "device", GST_TYPE_D3D11_DEVICE, device, NULL);
}

/**
 * gst_context_get_d3d11_device:
 * @context: a #GstContext
 * @device: (out) (transfer full): resulting #GstD3D11Device
 *
 * Returns: Whether @device was in @context
 */
gboolean
gst_context_get_d3d11_device (GstContext * context, GstD3D11Device ** device)
{
  const GstStructure *s;
  const gchar *context_type;
  gboolean ret;

  g_return_val_if_fail (GST_IS_CONTEXT (context), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  context_type = gst_context_get_context_type (context);
  if (g_strcmp0 (context_type, GST_D3D11_DEVICE_HANDLE_CONTEXT_TYPE) != 0)
    return FALSE;

  s = gst_context_get_structure (context);
  ret = gst_structure_get (s, "device", GST_TYPE_D3D11_DEVICE, device, NULL);

  GST_CAT_LOG (GST_CAT_CONTEXT, "got GstD3DDevice(%p) from context(%p)",
      *device, context);

  return ret;
}
