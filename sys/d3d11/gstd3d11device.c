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
#include "gstd3d11utils.h"
#include "gmodule.h"

#ifdef HAVE_D3D11SDKLAYER_H
#include <d3d11sdklayers.h>
#endif

GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_device_debug);
#define GST_CAT_DEFAULT gst_d3d11_device_debug

#ifdef HAVE_D3D11SDKLAYER_H
static GModule *sdk_layer = NULL;

/* mingw header does not define D3D11_RLDO_IGNORE_INTERNAL
 * D3D11_RLDO_SUMMARY = 0x1,
   D3D11_RLDO_DETAIL = 0x2,
 * D3D11_RLDO_IGNORE_INTERNAL = 0x4
 */
#define GST_D3D11_RLDO_FLAGS (0x2 | 0x4)
#endif

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDER_ID,
  PROP_HARDWARE,
  PROP_DESCRIPTION,
  PROP_ALLOW_TEARING,
};

#define DEFAULT_ADAPTER 0

struct _GstD3D11DevicePrivate
{
  guint adapter;
  guint device_id;
  guint vender_id;
  gboolean hardware;
  gchar *description;
  gboolean allow_tearing;

  ID3D11Device *device;
  ID3D11DeviceContext *device_context;

  IDXGIFactory1 *factory;
  GstD3D11DXGIFactoryVersion factory_ver;
  D3D_FEATURE_LEVEL feature_level;

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

    if (!gst_d3d11_result (hr) || msg_len == 0) {
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
      g_param_spec_uint ("adapter", "Adapter",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, DEFAULT_ADAPTER,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VENDER_ID,
      g_param_spec_uint ("vender-id", "Vender Id",
          "DXGI Vender ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HARDWARE,
      g_param_spec_boolean ("hardware", "Hardware",
          "Whether hardware device or not", TRUE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DESCRIPTION,
      g_param_spec_string ("description", "Description",
          "Human readable device description", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALLOW_TEARING,
      g_param_spec_boolean ("allow-tearing", "Allow tearing",
          "Whether dxgi device supports allow-tearing feature or not", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

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
gst_d3d11_device_constructed (GObject * object)
{
  GstD3D11Device *self = GST_D3D11_DEVICE (object);
  GstD3D11DevicePrivate *priv = self->priv;
  IDXGIAdapter1 *adapter = NULL;
  IDXGIFactory1 *factory = NULL;
  HRESULT hr;
  UINT d3d11_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

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
  if (!gst_d3d11_result (hr)) {
    GST_INFO_OBJECT (self, "IDXGIFactory5 was unavailable");
    factory = NULL;
  } else {
    BOOL allow_tearing;

    hr = IDXGIFactory5_CheckFeatureSupport ((IDXGIFactory5 *) factory,
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, (void *) &allow_tearing,
        sizeof (allow_tearing));

    priv->allow_tearing = SUCCEEDED (hr) && allow_tearing;

    hr = S_OK;
  }

  priv->factory_ver = GST_D3D11_DXGI_FACTORY_5;
#endif

  if (!factory) {
    priv->factory_ver = GST_D3D11_DXGI_FACTORY_1;
    hr = CreateDXGIFactory1 (&IID_IDXGIFactory1, (void **) &factory);
  }

  if (!gst_d3d11_result (hr)) {
    GST_ERROR_OBJECT (self, "cannot create dxgi factory, hr: 0x%x", (guint) hr);
    goto error;
  }

  if (IDXGIFactory1_EnumAdapters1 (factory, priv->adapter,
          &adapter) == DXGI_ERROR_NOT_FOUND) {
    GST_ERROR_OBJECT (self, "No adapter for index %d", priv->adapter);
    goto error;
  } else {
    DXGI_ADAPTER_DESC1 desc;

    hr = IDXGIAdapter1_GetDesc1 (adapter, &desc);
    if (SUCCEEDED (hr)) {
      gchar *description = NULL;
      gboolean is_hardware = FALSE;

      /* DXGI_ADAPTER_FLAG_SOFTWARE is missing in dxgi.h of mingw */
      if ((desc.Flags & 0x2) != 0x2) {
        is_hardware = TRUE;
      }

      description = g_utf16_to_utf8 (desc.Description, -1, NULL, NULL, NULL);
      GST_DEBUG_OBJECT (self,
          "adapter index %d: D3D11 device vendor-id: 0x%04x, device-id: 0x%04x, "
          "Flags: 0x%x, %s",
          priv->adapter, desc.VendorId, desc.DeviceId, desc.Flags, description);

      priv->vender_id = desc.VendorId;
      priv->device_id = desc.DeviceId;
      priv->hardware = is_hardware;
      priv->description = description;
    }
  }

#ifdef HAVE_D3D11SDKLAYER_H
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
    /* DirectX SDK should be installed on system for this */
    if (gst_d3d11_device_enable_debug_layer ()) {
      GST_INFO_OBJECT (self, "sdk layer library was loaded");
      d3d11_flags |= D3D11_CREATE_DEVICE_DEBUG;
    }
  }
#endif

  hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
      NULL, d3d11_flags, feature_levels, G_N_ELEMENTS (feature_levels),
      D3D11_SDK_VERSION, &priv->device, &selected_level, &priv->device_context);
  priv->feature_level = selected_level;

  if (!gst_d3d11_result (hr)) {
    /* Retry if the system could not recognize D3D_FEATURE_LEVEL_11_1 */
    hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
        NULL, d3d11_flags, &feature_levels[1],
        G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION, &priv->device,
        &selected_level, &priv->device_context);
    priv->feature_level = selected_level;
  }

  if (gst_d3d11_result (hr)) {
    GST_DEBUG_OBJECT (self, "Selected feature level 0x%x", selected_level);
  } else {
    GST_ERROR_OBJECT (self, "cannot create d3d11 device, hr: 0x%x", (guint) hr);
    goto error;
  }

  priv->factory = factory;


#ifdef HAVE_D3D11SDKLAYER_H
  if ((d3d11_flags & D3D11_CREATE_DEVICE_DEBUG) == D3D11_CREATE_DEVICE_DEBUG) {
    ID3D11Debug *debug;
    ID3D11InfoQueue *info_queue;

    hr = ID3D11Device_QueryInterface (priv->device,
        &IID_ID3D11Debug, (void **) &debug);

    if (SUCCEEDED (hr)) {
      GST_DEBUG_OBJECT (self, "D3D11Debug interface available");
      ID3D11Debug_ReportLiveDeviceObjects (debug,
          (D3D11_RLDO_FLAGS) GST_D3D11_RLDO_FLAGS);
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

  IDXGIAdapter1_Release (adapter);

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
      priv->adapter = g_value_get_uint (value);
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
      g_value_set_uint (value, priv->adapter);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, priv->device_id);
      break;
    case PROP_VENDER_ID:
      g_value_set_uint (value, priv->vender_id);
      break;
    case PROP_HARDWARE:
      g_value_set_boolean (value, priv->hardware);
      break;
    case PROP_DESCRIPTION:
      g_value_set_string (value, priv->description);
      break;
    case PROP_ALLOW_TEARING:
      g_value_set_boolean (value, priv->allow_tearing);
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
    ID3D11Debug_ReportLiveDeviceObjects (priv->debug,
        (D3D11_RLDO_FLAGS) GST_D3D11_RLDO_FLAGS);
    ID3D11Debug_Release (priv->debug);
    priv->debug = NULL;
  }

  if (priv->info_queue) {
    gst_d3d11_device_get_message (self);

    ID3D11InfoQueue_Release (priv->info_queue);
    priv->info_queue = NULL;
  }
#endif

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
  g_free (priv->description);

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
 * @adapter: the index of adapter for creating d3d11 device
 *
 * Returns: (transfer full) (nullable): a new #GstD3D11Device for @adapter or %NULL
 * when failed to create D3D11 device with given adapter index.
 */
GstD3D11Device *
gst_d3d11_device_new (guint adapter)
{
  GstD3D11Device *device = NULL;
  GstD3D11DevicePrivate *priv;

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
 * gst_d3d11_device_get_device_handle:
 * @device: a #GstD3D11Device
 *
 * Used for various D3D11 APIs directly.
 * Caller must not destroy returned device object.
 *
 * Returns: (transfer none): the ID3D11Device
 */
ID3D11Device *
gst_d3d11_device_get_device_handle (GstD3D11Device * device)
{
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  return device->priv->device;
}

/**
 * gst_d3d11_device_get_device_context_handle:
 * @device: a #GstD3D11Device
 *
 * Used for various D3D11 APIs directly.
 * Caller must not destroy returned device object.
 *
 * Returns: (transfer none): the ID3D11DeviceContext
 */
ID3D11DeviceContext *
gst_d3d11_device_get_device_context_handle (GstD3D11Device * device)
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

D3D_FEATURE_LEVEL
gst_d3d11_device_get_chosen_feature_level (GstD3D11Device * device)
{
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), 0);

  return device->priv->feature_level;
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
  gst_d3d11_device_thread_add_full (device,
      G_PRIORITY_DEFAULT, func, data, NULL);
}

/**
 * gst_d3d11_device_thread_add_full:
 * @device: a #GstD3D11Device
 * @priority: the priority at which to run @func
 * @func: (scope call): a #GstD3D11DeviceThreadFunc
 * @data: (closure): user data to call @func with
 * @notify: (nullable): a function to call when @data is no longer in use, or %NULL.
 *
 * Execute @func in the D3DDevice thread of @device with @data with specified
 * @priority
 *
 * MT-safe
 */
void
gst_d3d11_device_thread_add_full (GstD3D11Device * device,
    gint priority, GstD3D11DeviceThreadFunc func, gpointer data,
    GDestroyNotify notify)
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

  g_main_context_invoke_full (priv->main_context, priority,
      (GSourceFunc) gst_d3d11_device_message_callback, &msg, notify);

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

  if (!gst_d3d11_result (hr)) {
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
  if (!gst_d3d11_result (hr)) {
    const D3D11_TEXTURE2D_DESC *desc = data->desc;

    GST_ERROR ("Failed to create texture (0x%x)", (guint) hr);

    GST_WARNING ("Direct3D11 Allocation params");
    GST_WARNING ("\t%dx%d, DXGI format %d",
        desc->Width, desc->Height, desc->Format);
    GST_WARNING ("\tMipLevel %d, ArraySize %d",
        desc->MipLevels, desc->ArraySize);
    GST_WARNING ("\tSampleDesc.Count %d, SampleDesc.Quality %d",
        desc->SampleDesc.Count, desc->SampleDesc.Quality);
    GST_WARNING ("\tUsage %d", desc->Usage);
    GST_WARNING ("\tBindFlags 0x%x", desc->BindFlags);
    GST_WARNING ("\tCPUAccessFlags 0x%x", desc->CPUAccessFlags);
    GST_WARNING ("\tMiscFlags 0x%x", desc->MiscFlags);
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
