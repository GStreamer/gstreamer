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
#include "gstd3d11format.h"
#include "gmodule.h"

#if HAVE_D3D11SDKLAYERS_H
#include <d3d11sdklayers.h>
static GModule *d3d11_debug_module = NULL;

/* mingw header does not define D3D11_RLDO_IGNORE_INTERNAL
 * D3D11_RLDO_SUMMARY = 0x1,
   D3D11_RLDO_DETAIL = 0x2,
 * D3D11_RLDO_IGNORE_INTERNAL = 0x4
 */
#define GST_D3D11_RLDO_FLAGS (0x2 | 0x4)
#endif

#if HAVE_DXGIDEBUG_H
#include <dxgidebug.h>
typedef HRESULT (WINAPI * DXGIGetDebugInterface_t) (REFIID riid,
    void **ppDebug);
static GModule *dxgi_debug_module = NULL;
static DXGIGetDebugInterface_t GstDXGIGetDebugInterface = NULL;

#endif

#if (HAVE_D3D11SDKLAYERS_H || HAVE_DXGIDEBUG_H)
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_debug_layer_debug);
#endif
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_device_debug);
#define GST_CAT_DEFAULT gst_d3d11_device_debug

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
  PROP_HARDWARE,
  PROP_DESCRIPTION,
  PROP_ALLOW_TEARING,
};

#define DEFAULT_ADAPTER 0

struct _GstD3D11DevicePrivate
{
  guint adapter;
  guint device_id;
  guint vendor_id;
  gboolean hardware;
  gchar *description;
  gboolean allow_tearing;

  ID3D11Device *device;
  ID3D11DeviceContext *device_context;

  IDXGIFactory1 *factory;
  GstD3D11DXGIFactoryVersion factory_ver;
  D3D_FEATURE_LEVEL feature_level;
  GstD3D11Format format_table[GST_D3D11_N_FORMATS];

  GRecMutex extern_lock;

#if HAVE_D3D11SDKLAYERS_H
  ID3D11Debug *d3d11_debug;
  ID3D11InfoQueue *d3d11_info_queue;
#endif

#if HAVE_DXGIDEBUG_H
  IDXGIDebug *dxgi_debug;
  IDXGIInfoQueue *dxgi_info_queue;
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

#if HAVE_D3D11SDKLAYERS_H
static gboolean
gst_d3d11_device_enable_d3d11_debug (void)
{
  static volatile gsize _init = 0;

  /* If all below libraries are unavailable, d3d11 device would fail with
   * D3D11_CREATE_DEVICE_DEBUG flag */
  if (g_once_init_enter (&_init)) {
    d3d11_debug_module =
        g_module_open ("d3d11sdklayers.dll", G_MODULE_BIND_LAZY);

    if (!d3d11_debug_module)
      d3d11_debug_module =
          g_module_open ("d3d11_1sdklayers.dll", G_MODULE_BIND_LAZY);

    g_once_init_leave (&_init, 1);
  }

  return ! !d3d11_debug_module;
}

static inline GstDebugLevel
d3d11_message_severity_to_gst (D3D11_MESSAGE_SEVERITY level)
{
  switch (level) {
    case D3D11_MESSAGE_SEVERITY_CORRUPTION:
    case D3D11_MESSAGE_SEVERITY_ERROR:
      return GST_LEVEL_ERROR;
    case D3D11_MESSAGE_SEVERITY_WARNING:
      return GST_LEVEL_WARNING;
    case D3D11_MESSAGE_SEVERITY_INFO:
      return GST_LEVEL_INFO;
    case D3D11_MESSAGE_SEVERITY_MESSAGE:
      return GST_LEVEL_DEBUG;
    default:
      break;
  }

  return GST_LEVEL_LOG;
}

void
gst_d3d11_device_d3d11_debug (GstD3D11Device * device,
    const gchar * file, const gchar * function, gint line)
{
  GstD3D11DevicePrivate *priv = device->priv;
  D3D11_MESSAGE *msg;
  SIZE_T msg_len = 0;
  HRESULT hr;
  UINT64 num_msg, i;

  if (!priv->d3d11_info_queue)
    return;

  num_msg = ID3D11InfoQueue_GetNumStoredMessages (priv->d3d11_info_queue);

  for (i = 0; i < num_msg; i++) {
    GstDebugLevel level;

    hr = ID3D11InfoQueue_GetMessage (priv->d3d11_info_queue, i, NULL, &msg_len);

    if (FAILED (hr) || msg_len == 0) {
      return;
    }

    msg = (D3D11_MESSAGE *) g_alloca (msg_len);
    hr = ID3D11InfoQueue_GetMessage (priv->d3d11_info_queue, i, msg, &msg_len);

    level = d3d11_message_severity_to_gst (msg->Severity);
    gst_debug_log (gst_d3d11_debug_layer_debug, level, file, function, line,
        G_OBJECT (device), "D3D11InfoQueue: %s", msg->pDescription);
  }

  ID3D11InfoQueue_ClearStoredMessages (priv->d3d11_info_queue);

  return;
}
#else
void
gst_d3d11_device_d3d11_debug (GstD3D11Device * device,
    const gchar * file, const gchar * function, gint line)
{
  /* do nothing */
  return;
}
#endif

#if HAVE_DXGIDEBUG_H
static gboolean
gst_d3d11_device_enable_dxgi_debug (void)
{
  static volatile gsize _init = 0;
  gboolean ret = FALSE;

  /* If all below libraries are unavailable, d3d11 device would fail with
   * D3D11_CREATE_DEVICE_DEBUG flag */
  if (g_once_init_enter (&_init)) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    dxgi_debug_module = g_module_open ("dxgidebug.dll", G_MODULE_BIND_LAZY);

    if (dxgi_debug_module)
      g_module_symbol (dxgi_debug_module,
          "DXGIGetDebugInterface", (gpointer *) & GstDXGIGetDebugInterface);
    ret = ! !GstDXGIGetDebugInterface;
#elif (DXGI_HEADER_VERSION >= 3)
    ret = TRUE;
#endif
    g_once_init_leave (&_init, 1);
  }

  return ret;
}

static HRESULT
gst_d3d11_device_dxgi_get_device_interface (REFIID riid, void **debug)
{
#if (!GST_D3D11_WINAPI_ONLY_APP)
  if (GstDXGIGetDebugInterface) {
    return GstDXGIGetDebugInterface (riid, debug);
  }
#elif (DXGI_HEADER_VERSION >= 3)
  return DXGIGetDebugInterface1 (0, riid, debug);
#endif

  return E_NOINTERFACE;
}

static inline GstDebugLevel
dxgi_info_queue_message_severity_to_gst (DXGI_INFO_QUEUE_MESSAGE_SEVERITY level)
{
  switch (level) {
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION:
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR:
      return GST_LEVEL_ERROR;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING:
      return GST_LEVEL_WARNING;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO:
      return GST_LEVEL_INFO;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE:
      return GST_LEVEL_DEBUG;
    default:
      break;
  }

  return GST_LEVEL_LOG;
}

void
gst_d3d11_device_dxgi_debug (GstD3D11Device * device,
    const gchar * file, const gchar * function, gint line)
{
  GstD3D11DevicePrivate *priv = device->priv;
  DXGI_INFO_QUEUE_MESSAGE *msg;
  SIZE_T msg_len = 0;
  HRESULT hr;
  UINT64 num_msg, i;

  if (!priv->dxgi_info_queue)
    return;

  num_msg = IDXGIInfoQueue_GetNumStoredMessages (priv->dxgi_info_queue,
      DXGI_DEBUG_ALL);

  for (i = 0; i < num_msg; i++) {
    GstDebugLevel level;

    hr = IDXGIInfoQueue_GetMessage (priv->dxgi_info_queue,
        DXGI_DEBUG_ALL, i, NULL, &msg_len);

    if (FAILED (hr) || msg_len == 0) {
      return;
    }

    msg = (DXGI_INFO_QUEUE_MESSAGE *) g_alloca (msg_len);
    hr = IDXGIInfoQueue_GetMessage (priv->dxgi_info_queue,
        DXGI_DEBUG_ALL, i, msg, &msg_len);

    level = dxgi_info_queue_message_severity_to_gst (msg->Severity);
    gst_debug_log (gst_d3d11_debug_layer_debug, level, file, function, line,
        G_OBJECT (device), "DXGIInfoQueue: %s", msg->pDescription);
  }

  IDXGIInfoQueue_ClearStoredMessages (priv->dxgi_info_queue, DXGI_DEBUG_ALL);

  return;
}
#else
void
gst_d3d11_device_dxgi_debug (GstD3D11Device * device,
    const gchar * file, const gchar * function, gint line)
{
  /* do nothing */
  return;
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

  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0,
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
}

static void
gst_d3d11_device_init (GstD3D11Device * self)
{
  GstD3D11DevicePrivate *priv;

  priv = gst_d3d11_device_get_instance_private (self);
  priv->adapter = DEFAULT_ADAPTER;

  g_rec_mutex_init (&priv->extern_lock);

  self->priv = priv;
}

static gboolean
can_support_format (GstD3D11Device * self, DXGI_FORMAT format)
{
  GstD3D11DevicePrivate *priv = self->priv;
  ID3D11Device *handle = priv->device;
  HRESULT hr;
  UINT supported;
  D3D11_FORMAT_SUPPORT flags =
      (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET |
      D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);

  if (!gst_d3d11_is_windows_8_or_greater ()) {
    GST_WARNING_OBJECT (self, "DXGI format %d needs Windows 8 or greater",
        (guint) format);
    return FALSE;
  }

  hr = ID3D11Device_CheckFormatSupport (handle, format, &supported);
  if (!gst_d3d11_result (hr, NULL)) {
    GST_WARNING_OBJECT (self, "DXGI format %d is not supported by device",
        (guint) format);
    return FALSE;
  }

  if ((supported & flags) != flags) {
    GST_WARNING_OBJECT (self,
        "DXGI format %d doesn't support flag 0x%x (supported flag 0x%x)",
        (guint) format, (guint) supported, (guint) flags);
    return FALSE;
  }

  GST_INFO_OBJECT (self, "Device supports DXGI format %d", (guint) format);

  return TRUE;
}

static void
gst_d3d11_device_setup_format_table (GstD3D11Device * self)
{
  GstD3D11DevicePrivate *priv = self->priv;
  guint n_formats = 0;

  /* RGB formats */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_BGRA;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
  priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_RGBA;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_RGB10A2_LE;
  priv->format_table[n_formats].resource_format[0] =
      DXGI_FORMAT_R10G10B10A2_UNORM;
  priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;
  n_formats++;

  /* YUV packed */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_VUYA;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (can_support_format (self, DXGI_FORMAT_AYUV))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_AYUV;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  /* YUV semi-planar */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_NV12;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R8G8_UNORM;
  if (can_support_format (self, DXGI_FORMAT_NV12))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_NV12;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_P010_10LE;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R16_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R16G16_UNORM;
  if (can_support_format (self, DXGI_FORMAT_P010))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_P010;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_P016_LE;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R16_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R16G16_UNORM;
  if (can_support_format (self, DXGI_FORMAT_P016))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_P016;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  /* YUV plannar */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_I420;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R8_UNORM;
  priv->format_table[n_formats].resource_format[2] = DXGI_FORMAT_R8_UNORM;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_I420_10LE;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R16_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R16_UNORM;
  priv->format_table[n_formats].resource_format[2] = DXGI_FORMAT_R16_UNORM;
  n_formats++;

  g_assert (n_formats == GST_D3D11_N_FORMATS);
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

  GST_DEBUG_OBJECT (self,
      "Built with DXGI header version %d", DXGI_HEADER_VERSION);

#if HAVE_DXGIDEBUG_H
  if (gst_debug_category_get_threshold (gst_d3d11_debug_layer_debug) >
      GST_LEVEL_NONE) {
    if (gst_d3d11_device_enable_dxgi_debug ()) {
      IDXGIDebug *debug = NULL;
      IDXGIInfoQueue *info_queue = NULL;

      GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
          "dxgi debug library was loaded");
      hr = gst_d3d11_device_dxgi_get_device_interface (&IID_IDXGIDebug,
          (void **) &debug);

      if (SUCCEEDED (hr)) {
        GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
            "IDXGIDebug interface available");
        priv->dxgi_debug = debug;

        hr = gst_d3d11_device_dxgi_get_device_interface (&IID_IDXGIInfoQueue,
            (void **) &info_queue);
        if (SUCCEEDED (hr)) {
          GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
              "IDXGIInfoQueue interface available");
          priv->dxgi_info_queue = info_queue;
        }
      }
    } else {
      GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
          "couldn't load dxgi debug library");
    }
  }
#endif

#if (DXGI_HEADER_VERSION >= 5)
  hr = CreateDXGIFactory1 (&IID_IDXGIFactory5, (void **) &factory);
  if (!gst_d3d11_result (hr, NULL)) {
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

  if (!gst_d3d11_result (hr, NULL)) {
    GST_ERROR_OBJECT (self, "cannot create dxgi factory, hr: 0x%x", (guint) hr);
    goto error;
  }

  if (IDXGIFactory1_EnumAdapters1 (factory, priv->adapter,
          &adapter) == DXGI_ERROR_NOT_FOUND) {
    GST_WARNING_OBJECT (self, "No adapter for index %d", priv->adapter);
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

      priv->vendor_id = desc.VendorId;
      priv->device_id = desc.DeviceId;
      priv->hardware = is_hardware;
      priv->description = description;
    }
  }

#if HAVE_D3D11SDKLAYERS_H
  if (gst_debug_category_get_threshold (gst_d3d11_debug_layer_debug) >
      GST_LEVEL_NONE) {
    /* DirectX SDK should be installed on system for this */
    if (gst_d3d11_device_enable_d3d11_debug ()) {
      GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
          "d3d11 debug library was loaded");
      d3d11_flags |= D3D11_CREATE_DEVICE_DEBUG;
    } else {
      GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
          "couldn't load d3d11 debug library");
    }
  }
#endif

  hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
      NULL, d3d11_flags, feature_levels, G_N_ELEMENTS (feature_levels),
      D3D11_SDK_VERSION, &priv->device, &selected_level, &priv->device_context);
  priv->feature_level = selected_level;

  if (!gst_d3d11_result (hr, NULL)) {
    /* Retry if the system could not recognize D3D_FEATURE_LEVEL_11_1 */
    hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
        NULL, d3d11_flags, &feature_levels[1],
        G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION, &priv->device,
        &selected_level, &priv->device_context);
    priv->feature_level = selected_level;
  }

  /* if D3D11_CREATE_DEVICE_DEBUG was enabled but couldn't create device,
   * try it without the flag again */
  if (FAILED (hr) && (d3d11_flags & D3D11_CREATE_DEVICE_DEBUG) ==
      D3D11_CREATE_DEVICE_DEBUG) {
    GST_WARNING_OBJECT (self, "Couldn't create d3d11 device with debug flag");

    d3d11_flags &= ~D3D11_CREATE_DEVICE_DEBUG;

    hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
        NULL, d3d11_flags, feature_levels, G_N_ELEMENTS (feature_levels),
        D3D11_SDK_VERSION, &priv->device, &selected_level,
        &priv->device_context);
    priv->feature_level = selected_level;

    if (!gst_d3d11_result (hr, NULL)) {
      /* Retry if the system could not recognize D3D_FEATURE_LEVEL_11_1 */
      hr = D3D11CreateDevice ((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN,
          NULL, d3d11_flags, &feature_levels[1],
          G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION, &priv->device,
          &selected_level, &priv->device_context);
      priv->feature_level = selected_level;
    }
  }

  if (gst_d3d11_result (hr, NULL)) {
    GST_DEBUG_OBJECT (self, "Selected feature level 0x%x", selected_level);
  } else {
    GST_ERROR_OBJECT (self, "cannot create d3d11 device, hr: 0x%x", (guint) hr);
    goto error;
  }

  priv->factory = factory;

#if HAVE_D3D11SDKLAYERS_H
  if ((d3d11_flags & D3D11_CREATE_DEVICE_DEBUG) == D3D11_CREATE_DEVICE_DEBUG) {
    ID3D11Debug *debug;
    ID3D11InfoQueue *info_queue;

    hr = ID3D11Device_QueryInterface (priv->device,
        &IID_ID3D11Debug, (void **) &debug);

    if (SUCCEEDED (hr)) {
      GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
          "D3D11Debug interface available");
      priv->d3d11_debug = debug;

      hr = ID3D11Device_QueryInterface (priv->device,
          &IID_ID3D11InfoQueue, (void **) &info_queue);
      if (SUCCEEDED (hr)) {
        GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
            "ID3D11InfoQueue interface available");
        priv->d3d11_info_queue = info_queue;
      }
    }
  }
#endif

  IDXGIAdapter1_Release (adapter);
  gst_d3d11_device_setup_format_table (self);

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
    case PROP_VENDOR_ID:
      g_value_set_uint (value, priv->vendor_id);
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

  if (priv->factory) {
    IDXGIFactory1_Release (priv->factory);
    priv->factory = NULL;
  }
#if HAVE_D3D11SDKLAYERS_H
  if (priv->d3d11_debug) {
    ID3D11Debug_ReportLiveDeviceObjects (priv->d3d11_debug,
        (D3D11_RLDO_FLAGS) GST_D3D11_RLDO_FLAGS);
    ID3D11Debug_Release (priv->d3d11_debug);
    priv->d3d11_debug = NULL;
  }

  if (priv->d3d11_info_queue) {
    gst_d3d11_device_d3d11_debug (self, __FILE__, GST_FUNCTION, __LINE__);

    ID3D11InfoQueue_Release (priv->d3d11_info_queue);
    priv->d3d11_info_queue = NULL;
  }
#endif

#if HAVE_DXGIDEBUG_H
  if (priv->dxgi_debug) {
    IDXGIDebug_ReportLiveObjects (priv->dxgi_debug, DXGI_DEBUG_ALL,
        (DXGI_DEBUG_RLO_FLAGS) GST_D3D11_RLDO_FLAGS);
    IDXGIDebug_Release (priv->dxgi_debug);
    priv->dxgi_debug = NULL;
  }

  if (priv->dxgi_info_queue) {
    gst_d3d11_device_dxgi_debug (self, __FILE__, GST_FUNCTION, __LINE__);

    IDXGIInfoQueue_Release (priv->dxgi_info_queue);
    priv->dxgi_info_queue = NULL;
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

  g_rec_mutex_clear (&priv->extern_lock);
  g_free (priv->description);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
    GST_WARNING ("Cannot create d3d11 device with adapter %d", adapter);
    gst_clear_object (&device);
  } else {
    gst_object_ref_sink (device);
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
  GstD3D11DevicePrivate *priv;
  HRESULT hr;
  IDXGISwapChain *swap_chain = NULL;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;

  gst_d3d11_device_lock (device);
  hr = IDXGIFactory1_CreateSwapChain (priv->factory, (IUnknown *) priv->device,
      (DXGI_SWAP_CHAIN_DESC *) desc, &swap_chain);
  gst_d3d11_device_unlock (device);

  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (device, "Cannot create SwapChain Object: 0x%x",
        (guint) hr);
    swap_chain = NULL;
  }

  return swap_chain;
}

#if (DXGI_HEADER_VERSION >= 2)
#if (!GST_D3D11_WINAPI_ONLY_APP)
/**
 * gst_d3d11_device_create_swap_chain_for_hwnd:
 * @device: a #GstD3D11Device
 * @hwnd: HWND handle
 * @desc: a DXGI_SWAP_CHAIN_DESC1 structure for swapchain
 * @fullscreen_desc: (nullable): a DXGI_SWAP_CHAIN_FULLSCREEN_DESC
 *   structure for swapchain
 * @output: (nullable): a IDXGIOutput interface for the output to restrict content to
 *
 * Create a IDXGISwapChain1 object. Caller must release returned swap chain object
 * via IDXGISwapChain1_Release()
 *
 * Returns: (transfer full) (nullable): a new IDXGISwapChain1 or %NULL
 * when failed to create swap chain with given @desc
 */
IDXGISwapChain1 *
gst_d3d11_device_create_swap_chain_for_hwnd (GstD3D11Device * device,
    HWND hwnd, const DXGI_SWAP_CHAIN_DESC1 * desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC * fullscreen_desc,
    IDXGIOutput * output)
{
  GstD3D11DevicePrivate *priv;
  IDXGISwapChain1 *swap_chain = NULL;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;

  gst_d3d11_device_lock (device);
  hr = IDXGIFactory2_CreateSwapChainForHwnd ((IDXGIFactory2 *) priv->factory,
      (IUnknown *) priv->device, hwnd, desc, fullscreen_desc,
      output, &swap_chain);
  gst_d3d11_device_unlock (device);

  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (device, "Cannot create SwapChain Object: 0x%x",
        (guint) hr);
    swap_chain = NULL;
  }

  return swap_chain;
}
#endif /* GST_D3D11_WINAPI_ONLY_APP */

#if GST_D3D11_WINAPI_ONLY_APP
/**
 * gst_d3d11_device_create_swap_chain_for_core_window:
 * @device: a #GstD3D11Device
 * @core_window: CoreWindow handle
 * @desc: a DXGI_SWAP_CHAIN_DESC1 structure for swapchain
 * @output: (nullable): a IDXGIOutput interface for the output to restrict content to
 *
 * Create a IDXGISwapChain1 object. Caller must release returned swap chain object
 * via IDXGISwapChain1_Release()
 *
 * Returns: (transfer full) (nullable): a new IDXGISwapChain1 or %NULL
 * when failed to create swap chain with given @desc
 */
IDXGISwapChain1 *
gst_d3d11_device_create_swap_chain_for_core_window (GstD3D11Device * device,
    guintptr core_window, const DXGI_SWAP_CHAIN_DESC1 * desc,
    IDXGIOutput * output)
{
  GstD3D11DevicePrivate *priv;
  IDXGISwapChain1 *swap_chain = NULL;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;

  gst_d3d11_device_lock (device);
  hr = IDXGIFactory2_CreateSwapChainForCoreWindow ((IDXGIFactory2 *)
      priv->factory, (IUnknown *) priv->device, (IUnknown *) core_window, desc,
      output, &swap_chain);
  gst_d3d11_device_unlock (device);

  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (device, "Cannot create SwapChain Object: 0x%x",
        (guint) hr);
    swap_chain = NULL;
  }

  return swap_chain;
}

/**
 * gst_d3d11_device_create_swap_chain_for_composition:
 * @device: a #GstD3D11Device
 * @desc: a DXGI_SWAP_CHAIN_DESC1 structure for swapchain
 * @output: (nullable): a IDXGIOutput interface for the output to restrict content to
 *
 * Create a IDXGISwapChain1 object. Caller must release returned swap chain object
 * via IDXGISwapChain1_Release()
 *
 * Returns: (transfer full) (nullable): a new IDXGISwapChain1 or %NULL
 * when failed to create swap chain with given @desc
 */
IDXGISwapChain1 *
gst_d3d11_device_create_swap_chain_for_composition (GstD3D11Device * device,
    const DXGI_SWAP_CHAIN_DESC1 * desc, IDXGIOutput * output)
{
  GstD3D11DevicePrivate *priv;
  IDXGISwapChain1 *swap_chain = NULL;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;

  gst_d3d11_device_lock (device);
  hr = IDXGIFactory2_CreateSwapChainForComposition ((IDXGIFactory2 *)
      priv->factory, (IUnknown *) priv->device, desc, output, &swap_chain);
  gst_d3d11_device_unlock (device);

  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (device, "Cannot create SwapChain Object: 0x%x",
        (guint) hr);
    swap_chain = NULL;
  }

  return swap_chain;
}
#endif /* GST_D3D11_WINAPI_ONLY_APP */
#endif /* (DXGI_HEADER_VERSION >= 2) */

/**
 * gst_d3d11_device_release_swap_chain:
 * @device: a #GstD3D11Device
 * @swap_chain: a IDXGISwapChain
 *
 * Release a @swap_chain from device thread
 */
void
gst_d3d11_device_release_swap_chain (GstD3D11Device * device,
    IDXGISwapChain * swap_chain)
{
  g_return_if_fail (GST_IS_D3D11_DEVICE (device));
  g_return_if_fail (swap_chain != NULL);

  gst_d3d11_device_lock (device);
  IDXGISwapChain_Release (swap_chain);
  gst_d3d11_device_unlock (device);
}

ID3D11Texture2D *
gst_d3d11_device_create_texture (GstD3D11Device * device,
    const D3D11_TEXTURE2D_DESC * desc,
    const D3D11_SUBRESOURCE_DATA * inital_data)
{
  GstD3D11DevicePrivate *priv;
  HRESULT hr;
  ID3D11Texture2D *texture;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (desc != NULL, NULL);

  priv = device->priv;

  hr = ID3D11Device_CreateTexture2D (priv->device, desc, inital_data, &texture);
  if (!gst_d3d11_result (hr, device)) {
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
    texture = NULL;
  }

  return texture;
}

void
gst_d3d11_device_release_texture (GstD3D11Device * device,
    ID3D11Texture2D * texture)
{
  g_return_if_fail (GST_IS_D3D11_DEVICE (device));
  g_return_if_fail (texture != NULL);

  ID3D11Texture2D_Release (texture);
}

void
gst_d3d11_device_lock (GstD3D11Device * device)
{
  GstD3D11DevicePrivate *priv;

  g_return_if_fail (GST_IS_D3D11_DEVICE (device));

  priv = device->priv;

  GST_TRACE_OBJECT (device, "device locking");
  g_rec_mutex_lock (&priv->extern_lock);
  GST_TRACE_OBJECT (device, "device locked");
}

void
gst_d3d11_device_unlock (GstD3D11Device * device)
{
  GstD3D11DevicePrivate *priv;

  g_return_if_fail (GST_IS_D3D11_DEVICE (device));

  priv = device->priv;

  g_rec_mutex_unlock (&priv->extern_lock);
  GST_TRACE_OBJECT (device, "device unlocked");
}

const GstD3D11Format *
gst_d3d11_device_format_from_gst (GstD3D11Device * device,
    GstVideoFormat format)
{
  GstD3D11DevicePrivate *priv;
  gint i;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;

  for (i = 0; i < G_N_ELEMENTS (priv->format_table); i++) {
    if (priv->format_table[i].format == format)
      return &priv->format_table[i];
  }

  return NULL;
}
