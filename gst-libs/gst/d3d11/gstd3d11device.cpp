/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
#include "gstd3d11_private.h"
#include "gstd3d11memory.h"
#include <gmodule.h>
#include <wrl.h>

#include <windows.h>
#include <versionhelpers.h>

/**
 * SECTION:gstd3d11device
 * @short_description: Direct3D11 device abstraction
 * @title: GstD3D11Device
 *
 * #GstD3D11Device wraps ID3D11Device and ID3D11DeviceContext for GPU resources
 * to be able to be shared among various elements. Caller can get native
 * Direct3D11 handles via getter method.
 * Basically Direct3D11 API doesn't require dedicated thread like that of
 * OpenGL context, and ID3D11Device APIs are supposed to be thread-safe.
 * But concurrent call for ID3D11DeviceContext and DXGI API are not allowed.
 * To protect such object, callers need to make use of gst_d3d11_device_lock()
 * and gst_d3d11_device_unlock()
 */

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

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
GST_DEBUG_CATEGORY_STATIC (gst_d3d11_debug_layer_debug);
#endif
GST_DEBUG_CATEGORY_STATIC (gst_d3d11_device_debug);
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
  PROP_CREATE_FLAGS,
  PROP_ADAPTER_LUID,
};

#define DEFAULT_ADAPTER 0
#define DEFAULT_CREATE_FLAGS 0

#define GST_D3D11_N_FORMATS 18

struct _GstD3D11DevicePrivate
{
  guint adapter;
  guint device_id;
  guint vendor_id;
  gboolean hardware;
  gchar *description;
  gboolean allow_tearing;
  guint create_flags;
  gint64 adapter_luid;

  ID3D11Device *device;
  ID3D11DeviceContext *device_context;

  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;

  IDXGIFactory1 *factory;
  GstD3D11Format format_table[GST_D3D11_N_FORMATS];

  GRecMutex extern_lock;
  GMutex resource_lock;

#if HAVE_D3D11SDKLAYERS_H
  ID3D11Debug *d3d11_debug;
  ID3D11InfoQueue *d3d11_info_queue;
#endif

#if HAVE_DXGIDEBUG_H
  IDXGIDebug *dxgi_debug;
  IDXGIInfoQueue *dxgi_info_queue;
#endif
};

static void
do_debug_init (void)
{
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_device_debug,
      "d3d11device", 0, "d3d11 device object");
#if defined(HAVE_D3D11SDKLAYERS_H) || defined(HAVE_DXGIDEBUG_H)
  GST_DEBUG_CATEGORY_INIT (gst_d3d11_debug_layer_debug,
      "d3d11debuglayer", 0, "native d3d11 and dxgi debug");
#endif
}

#define gst_d3d11_device_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstD3D11Device, gst_d3d11_device, GST_TYPE_OBJECT,
    G_ADD_PRIVATE (GstD3D11Device); do_debug_init ());

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
  static gsize _init = 0;

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

  if (d3d11_debug_module)
    return TRUE;

  return FALSE;
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
  ID3D11InfoQueue *info_queue = priv->d3d11_info_queue;

  if (!info_queue)
    return;

  num_msg = info_queue->GetNumStoredMessages ();

  for (i = 0; i < num_msg; i++) {
    GstDebugLevel level;

    hr = info_queue->GetMessage (i, NULL, &msg_len);

    if (FAILED (hr) || msg_len == 0) {
      return;
    }

    msg = (D3D11_MESSAGE *) g_alloca (msg_len);
    hr = info_queue->GetMessage (i, msg, &msg_len);

    level = d3d11_message_severity_to_gst (msg->Severity);
    if (msg->Category == D3D11_MESSAGE_CATEGORY_STATE_CREATION &&
        level > GST_LEVEL_ERROR) {
      /* Do not warn for live object, since there would be live object
       * when ReportLiveDeviceObjects was called */
      level = GST_LEVEL_INFO;
    }

    gst_debug_log (gst_d3d11_debug_layer_debug, level, file, function, line,
        G_OBJECT (device), "D3D11InfoQueue: %s", msg->pDescription);
  }

  info_queue->ClearStoredMessages ();

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
  static gsize _init = 0;
  gboolean ret = FALSE;

  /* If all below libraries are unavailable, d3d11 device would fail with
   * D3D11_CREATE_DEVICE_DEBUG flag */
  if (g_once_init_enter (&_init)) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    dxgi_debug_module = g_module_open ("dxgidebug.dll", G_MODULE_BIND_LAZY);

    if (dxgi_debug_module)
      g_module_symbol (dxgi_debug_module,
          "DXGIGetDebugInterface", (gpointer *) & GstDXGIGetDebugInterface);
    if (GstDXGIGetDebugInterface)
      ret = TRUE;
#elif (GST_D3D11_DXGI_HEADER_VERSION >= 3)
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
#elif (GST_D3D11_DXGI_HEADER_VERSION >= 3)
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
  IDXGIInfoQueue *info_queue = priv->dxgi_info_queue;

  if (!info_queue)
    return;

  num_msg = info_queue->GetNumStoredMessages (DXGI_DEBUG_ALL);

  for (i = 0; i < num_msg; i++) {
    GstDebugLevel level;

    hr = info_queue->GetMessage (DXGI_DEBUG_ALL, i, NULL, &msg_len);

    if (FAILED (hr) || msg_len == 0) {
      return;
    }

    msg = (DXGI_INFO_QUEUE_MESSAGE *) g_alloca (msg_len);
    hr = info_queue->GetMessage (DXGI_DEBUG_ALL, i, msg, &msg_len);

    level = dxgi_info_queue_message_severity_to_gst (msg->Severity);
    gst_debug_log (gst_d3d11_debug_layer_debug, level, file, function, line,
        G_OBJECT (device), "DXGIInfoQueue: %s", msg->pDescription);
  }

  info_queue->ClearStoredMessages (DXGI_DEBUG_ALL);

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
  GParamFlags rw_construct_only_flags =
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_STATIC_STRINGS);
  GParamFlags readable_flags =
      (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  gobject_class->set_property = gst_d3d11_device_set_property;
  gobject_class->get_property = gst_d3d11_device_get_property;
  gobject_class->constructed = gst_d3d11_device_constructed;
  gobject_class->dispose = gst_d3d11_device_dispose;
  gobject_class->finalize = gst_d3d11_device_finalize;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_uint ("adapter", "Adapter",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, DEFAULT_ADAPTER, rw_construct_only_flags));

  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0, readable_flags));

  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0, readable_flags));

  g_object_class_install_property (gobject_class, PROP_HARDWARE,
      g_param_spec_boolean ("hardware", "Hardware",
          "Whether hardware device or not", TRUE, readable_flags));

  g_object_class_install_property (gobject_class, PROP_DESCRIPTION,
      g_param_spec_string ("description", "Description",
          "Human readable device description", NULL, readable_flags));

  g_object_class_install_property (gobject_class, PROP_ALLOW_TEARING,
      g_param_spec_boolean ("allow-tearing", "Allow tearing",
          "Whether dxgi device supports allow-tearing feature or not", FALSE,
          readable_flags));

  g_object_class_install_property (gobject_class, PROP_CREATE_FLAGS,
      g_param_spec_uint ("create-flags", "Create flags",
          "D3D11_CREATE_DEVICE_FLAG flags used for D3D11CreateDevice",
          0, G_MAXUINT32, DEFAULT_CREATE_FLAGS, rw_construct_only_flags));

  g_object_class_install_property (gobject_class, PROP_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier) of created device",
          0, G_MAXINT64, 0, readable_flags));

  gst_d3d11_memory_init_once ();
}

static void
gst_d3d11_device_init (GstD3D11Device * self)
{
  GstD3D11DevicePrivate *priv;

  priv = (GstD3D11DevicePrivate *)
      gst_d3d11_device_get_instance_private (self);
  priv->adapter = DEFAULT_ADAPTER;

  g_rec_mutex_init (&priv->extern_lock);
  g_mutex_init (&priv->resource_lock);

  self->priv = priv;
}

static gboolean
is_windows_8_or_greater (void)
{
  static gsize version_once = 0;
  static gboolean ret = FALSE;

  if (g_once_init_enter (&version_once)) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    if (IsWindows8OrGreater ())
      ret = TRUE;
#else
    ret = TRUE;
#endif

    g_once_init_leave (&version_once, 1);
  }

  return ret;
}

inline D3D11_FORMAT_SUPPORT
operator | (D3D11_FORMAT_SUPPORT lhs, D3D11_FORMAT_SUPPORT rhs)
{
  return static_cast < D3D11_FORMAT_SUPPORT > (static_cast < UINT >
      (lhs) | static_cast < UINT > (rhs));
}

inline D3D11_FORMAT_SUPPORT
operator |= (D3D11_FORMAT_SUPPORT lhs, D3D11_FORMAT_SUPPORT rhs)
{
  return lhs | rhs;
}

static gboolean
can_support_format (GstD3D11Device * self, DXGI_FORMAT format,
    D3D11_FORMAT_SUPPORT extra_flags)
{
  GstD3D11DevicePrivate *priv = self->priv;
  ID3D11Device *handle = priv->device;
  HRESULT hr;
  UINT supported;
  D3D11_FORMAT_SUPPORT flags = D3D11_FORMAT_SUPPORT_TEXTURE2D;

  flags |= extra_flags;

  if (!is_windows_8_or_greater ()) {
    GST_INFO_OBJECT (self, "DXGI format %d needs Windows 8 or greater",
        (guint) format);
    return FALSE;
  }

  hr = handle->CheckFormatSupport (format, &supported);
  if (FAILED (hr)) {
    GST_DEBUG_OBJECT (self, "DXGI format %d is not supported by device",
        (guint) format);
    return FALSE;
  }

  if ((supported & flags) != flags) {
    GST_DEBUG_OBJECT (self,
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

  /* Identical to BGRA, but alpha will be ignored */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_BGRx;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
  priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_RGBA;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  n_formats++;

  /* Identical to RGBA, but alpha will be ignored */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_RGBx;
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
  if (can_support_format (self, DXGI_FORMAT_AYUV,
          D3D11_FORMAT_SUPPORT_RENDER_TARGET |
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_AYUV;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  /* NOTE: packted yuv 4:2:2 YUY2, UYVY, and VYUY formats are not natively
   * supported render target view formats
   * (i.e., cannot be output format of shader pipeline) */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_YUY2;
  if (can_support_format (self, DXGI_FORMAT_YUY2,
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
    priv->format_table[n_formats].resource_format[0] =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_YUY2;
  } else {
    /* If DXGI_FORMAT_YUY2 format is not supported, use this format,
     * it's analogous to YUY2 */
    priv->format_table[n_formats].resource_format[0] =
        DXGI_FORMAT_G8R8_G8B8_UNORM;
  }
  n_formats++;

  /* No native DXGI format available for UYVY */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_UYVY;
  priv->format_table[n_formats].resource_format[0] =
      DXGI_FORMAT_R8G8_B8G8_UNORM;
  n_formats++;

  /* No native DXGI format available for VYUY */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_VYUY;
  priv->format_table[n_formats].resource_format[0] =
      DXGI_FORMAT_R8G8_B8G8_UNORM;
  n_formats++;

  /* Y210 and Y410 formats cannot support rtv */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_Y210;
  priv->format_table[n_formats].resource_format[0] =
      DXGI_FORMAT_R16G16B16A16_UNORM;
  if (can_support_format (self, DXGI_FORMAT_Y210,
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_Y210;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_Y410;
  priv->format_table[n_formats].resource_format[0] =
      DXGI_FORMAT_R10G10B10A2_UNORM;
  if (can_support_format (self, DXGI_FORMAT_Y410,
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_Y410;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  /* YUV semi-planar */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_NV12;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R8G8_UNORM;
  if (can_support_format (self, DXGI_FORMAT_NV12,
          D3D11_FORMAT_SUPPORT_RENDER_TARGET |
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_NV12;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  /* no native format for NV21 */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_NV21;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R8G8_UNORM;
  priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_P010_10LE;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R16_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R16G16_UNORM;
  if (can_support_format (self, DXGI_FORMAT_P010,
          D3D11_FORMAT_SUPPORT_RENDER_TARGET |
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_P010;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_P016_LE;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R16_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R16G16_UNORM;
  if (can_support_format (self, DXGI_FORMAT_P016,
          D3D11_FORMAT_SUPPORT_RENDER_TARGET |
          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_P016;
  else
    priv->format_table[n_formats].dxgi_format = DXGI_FORMAT_UNKNOWN;
  n_formats++;

  /* YUV planar */
  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_I420;
  priv->format_table[n_formats].resource_format[0] = DXGI_FORMAT_R8_UNORM;
  priv->format_table[n_formats].resource_format[1] = DXGI_FORMAT_R8_UNORM;
  priv->format_table[n_formats].resource_format[2] = DXGI_FORMAT_R8_UNORM;
  n_formats++;

  priv->format_table[n_formats].format = GST_VIDEO_FORMAT_YV12;
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
  ComPtr < IDXGIAdapter1 > adapter;
  ComPtr < IDXGIFactory1 > factory;
  HRESULT hr;
  UINT d3d11_flags = priv->create_flags;

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
      "Built with DXGI header version %d", GST_D3D11_DXGI_HEADER_VERSION);

#if HAVE_DXGIDEBUG_H
  if (gst_debug_category_get_threshold (gst_d3d11_debug_layer_debug) >
      GST_LEVEL_NONE) {
    if (gst_d3d11_device_enable_dxgi_debug ()) {
      IDXGIDebug *debug = NULL;
      IDXGIInfoQueue *info_queue = NULL;

      GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
          "dxgi debug library was loaded");
      hr = gst_d3d11_device_dxgi_get_device_interface (IID_PPV_ARGS (&debug));

      if (SUCCEEDED (hr)) {
        GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
            "IDXGIDebug interface available");
        priv->dxgi_debug = debug;

        hr = gst_d3d11_device_dxgi_get_device_interface (IID_PPV_ARGS
            (&info_queue));
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

  hr = CreateDXGIFactory1 (IID_PPV_ARGS (&factory));
  if (!gst_d3d11_result (hr, NULL)) {
    GST_ERROR_OBJECT (self, "cannot create dxgi factory, hr: 0x%x", (guint) hr);
    goto out;
  }
#if (GST_D3D11_DXGI_HEADER_VERSION >= 5)
  {
    ComPtr < IDXGIFactory5 > factory5;
    BOOL allow_tearing;

    hr = factory.As (&factory5);
    if (SUCCEEDED (hr)) {
      hr = factory5->CheckFeatureSupport (DXGI_FEATURE_PRESENT_ALLOW_TEARING,
          (void *) &allow_tearing, sizeof (allow_tearing));

      priv->allow_tearing = SUCCEEDED (hr) && allow_tearing;
    }
  }
#endif

  if (factory->EnumAdapters1 (priv->adapter, &adapter) == DXGI_ERROR_NOT_FOUND) {
    GST_DEBUG_OBJECT (self, "No adapter for index %d", priv->adapter);
    goto out;
  } else {
    DXGI_ADAPTER_DESC1 desc;

    hr = adapter->GetDesc1 (&desc);
    if (SUCCEEDED (hr)) {
      gchar *description = NULL;
      gboolean is_hardware = FALSE;
      gint64 adapter_luid;

      /* DXGI_ADAPTER_FLAG_SOFTWARE is missing in dxgi.h of mingw */
      if ((desc.Flags & 0x2) != 0x2) {
        is_hardware = TRUE;
      }

      adapter_luid = (((gint64) desc.AdapterLuid.HighPart) << 32) |
          ((gint64) desc.AdapterLuid.LowPart);
      description = g_utf16_to_utf8 ((gunichar2 *) desc.Description,
          -1, NULL, NULL, NULL);
      GST_DEBUG_OBJECT (self,
          "adapter index %d: D3D11 device vendor-id: 0x%04x, device-id: 0x%04x, "
          "Flags: 0x%x, adapter-luid: %" G_GINT64_FORMAT ", %s",
          priv->adapter, desc.VendorId, desc.DeviceId, desc.Flags, adapter_luid,
          description);

      priv->vendor_id = desc.VendorId;
      priv->device_id = desc.DeviceId;
      priv->hardware = is_hardware;
      priv->description = description;
      priv->adapter_luid = adapter_luid;
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

  hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN,
      NULL, d3d11_flags, feature_levels, G_N_ELEMENTS (feature_levels),
      D3D11_SDK_VERSION, &priv->device, &selected_level, &priv->device_context);

  if (FAILED (hr)) {
    /* Retry if the system could not recognize D3D_FEATURE_LEVEL_11_1 */
    hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN,
        NULL, d3d11_flags, &feature_levels[1],
        G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION, &priv->device,
        &selected_level, &priv->device_context);
  }

  /* if D3D11_CREATE_DEVICE_DEBUG was enabled but couldn't create device,
   * try it without the flag again */
  if (FAILED (hr) && (d3d11_flags & D3D11_CREATE_DEVICE_DEBUG) ==
      D3D11_CREATE_DEVICE_DEBUG) {
    GST_WARNING_OBJECT (self, "Couldn't create d3d11 device with debug flag");

    d3d11_flags &= ~D3D11_CREATE_DEVICE_DEBUG;

    hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN,
        NULL, d3d11_flags, feature_levels, G_N_ELEMENTS (feature_levels),
        D3D11_SDK_VERSION, &priv->device, &selected_level,
        &priv->device_context);

    if (FAILED (hr)) {
      /* Retry if the system could not recognize D3D_FEATURE_LEVEL_11_1 */
      hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN,
          NULL, d3d11_flags, &feature_levels[1],
          G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION, &priv->device,
          &selected_level, &priv->device_context);
    }
  }

  if (SUCCEEDED (hr)) {
    GST_DEBUG_OBJECT (self, "Selected feature level 0x%x", selected_level);
  } else {
    GST_INFO_OBJECT (self,
        "cannot create d3d11 device for adapter index %d with flags 0x%x, "
        "hr: 0x%x", priv->adapter, d3d11_flags, (guint) hr);
    goto out;
  }

  priv->factory = factory.Detach ();

#if HAVE_D3D11SDKLAYERS_H
  if ((d3d11_flags & D3D11_CREATE_DEVICE_DEBUG) == D3D11_CREATE_DEVICE_DEBUG) {
    ID3D11Debug *debug;
    ID3D11InfoQueue *info_queue;

    hr = priv->device->QueryInterface (IID_PPV_ARGS (&debug));

    if (SUCCEEDED (hr)) {
      GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
          "D3D11Debug interface available");
      priv->d3d11_debug = debug;

      hr = priv->device->QueryInterface (IID_PPV_ARGS (&info_queue));
      if (SUCCEEDED (hr)) {
        GST_CAT_INFO_OBJECT (gst_d3d11_debug_layer_debug, self,
            "ID3D11InfoQueue interface available");
        priv->d3d11_info_queue = info_queue;
      }
    }
  }
#endif

  /* Update final create flags here, since D3D11_CREATE_DEVICE_DEBUG
   * might be added by us */
  priv->create_flags = d3d11_flags;

  gst_d3d11_device_setup_format_table (self);

out:
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
    case PROP_CREATE_FLAGS:
      priv->create_flags = g_value_get_uint (value);
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
    case PROP_CREATE_FLAGS:
      g_value_set_uint (value, priv->create_flags);
      break;
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, priv->adapter_luid);
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

  GST_D3D11_CLEAR_COM (priv->video_device);
  GST_D3D11_CLEAR_COM (priv->video_context);
  GST_D3D11_CLEAR_COM (priv->device);
  GST_D3D11_CLEAR_COM (priv->device_context);
  GST_D3D11_CLEAR_COM (priv->factory);
#if HAVE_D3D11SDKLAYERS_H
  if (priv->d3d11_debug) {
    priv->d3d11_debug->ReportLiveDeviceObjects ((D3D11_RLDO_FLAGS)
        GST_D3D11_RLDO_FLAGS);
  }
  GST_D3D11_CLEAR_COM (priv->d3d11_debug);

  if (priv->d3d11_info_queue)
    gst_d3d11_device_d3d11_debug (self, __FILE__, GST_FUNCTION, __LINE__);

  GST_D3D11_CLEAR_COM (priv->d3d11_info_queue);
#endif

#if HAVE_DXGIDEBUG_H
  if (priv->dxgi_debug) {
    priv->dxgi_debug->ReportLiveObjects (DXGI_DEBUG_ALL,
        (DXGI_DEBUG_RLO_FLAGS) GST_D3D11_RLDO_FLAGS);
  }
  GST_D3D11_CLEAR_COM (priv->dxgi_debug);

  if (priv->dxgi_info_queue)
    gst_d3d11_device_dxgi_debug (self, __FILE__, GST_FUNCTION, __LINE__);

  GST_D3D11_CLEAR_COM (priv->dxgi_info_queue);
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
  g_mutex_clear (&priv->resource_lock);
  g_free (priv->description);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_d3d11_device_new:
 * @adapter: the index of adapter for creating d3d11 device
 * @flags: a D3D11_CREATE_DEVICE_FLAG value used for creating d3d11 device
 *
 * Returns: (transfer full) (nullable): a new #GstD3D11Device for @adapter
 * or %NULL when failed to create D3D11 device with given adapter index.
 *
 * Since: 1.20
 */
GstD3D11Device *
gst_d3d11_device_new (guint adapter, guint flags)
{
  GstD3D11Device *device = NULL;
  GstD3D11DevicePrivate *priv;

  device = (GstD3D11Device *)
      g_object_new (GST_TYPE_D3D11_DEVICE, "adapter", adapter,
      "create-flags", flags, NULL);

  priv = device->priv;

  if (!priv->device || !priv->device_context) {
    GST_DEBUG ("Cannot create d3d11 device with adapter %d", adapter);
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
 * Used for various D3D11 APIs directly. Caller must not destroy returned device
 * object.
 *
 * Returns: (transfer none): the ID3D11Device handle
 *
 * Since: 1.20
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
 * Used for various D3D11 APIs directly. Caller must not destroy returned device
 * object. Any ID3D11DeviceContext call needs to be protected by
 * gst_d3d11_device_lock() and gst_d3d11_device_unlock() method.
 *
 * Returns: (transfer none): the immeidate ID3D11DeviceContext handle
 *
 * Since: 1.20
 */
ID3D11DeviceContext *
gst_d3d11_device_get_device_context_handle (GstD3D11Device * device)
{
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  return device->priv->device_context;
}

/**
 * gst_d3d11_device_get_dxgi_factory_handle:
 * @device: a #GstD3D11Device
 *
 * Used for various D3D11 APIs directly. Caller must not destroy returned device
 * object.
 *
 * Returns: (transfer none): the IDXGIFactory1 handle
 *
 * Since: 1.20
 */
IDXGIFactory1 *
gst_d3d11_device_get_dxgi_factory_handle (GstD3D11Device * device)
{
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  return device->priv->factory;
}

/**
 * gst_d3d11_device_get_video_device_handle:
 * @device: a #GstD3D11Device
 *
 * Used for various D3D11 APIs directly. Caller must not destroy returned device
 * object.
 *
 * Returns: (nullable) (transfer none) : the ID3D11VideoDevice handle or %NULL
 * if ID3D11VideoDevice is unavailable.
 *
 * Since: 1.20
 */
ID3D11VideoDevice *
gst_d3d11_device_get_video_device_handle (GstD3D11Device * device)
{
  GstD3D11DevicePrivate *priv;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;
  g_mutex_lock (&priv->resource_lock);
  if (!priv->video_device) {
    HRESULT hr;
    ID3D11VideoDevice *video_device = NULL;

    hr = priv->device->QueryInterface (IID_PPV_ARGS (&video_device));
    if (gst_d3d11_result (hr, device))
      priv->video_device = video_device;
  }
  g_mutex_unlock (&priv->resource_lock);

  return priv->video_device;
}

/**
 * gst_d3d11_device_get_video_context_handle:
 * @device: a #GstD3D11Device
 *
 * Used for various D3D11 APIs directly. Caller must not destroy returned device
 * object.
 *
 * Returns: (nullable) (transfer none): the ID3D11VideoContext handle or %NULL
 * if ID3D11VideoContext is unavailable.
 *
 * Since: 1.20
 */
ID3D11VideoContext *
gst_d3d11_device_get_video_context_handle (GstD3D11Device * device)
{
  GstD3D11DevicePrivate *priv;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;
  g_mutex_lock (&priv->resource_lock);
  if (!priv->video_context) {
    HRESULT hr;
    ID3D11VideoContext *video_context = NULL;

    hr = priv->device_context->QueryInterface (IID_PPV_ARGS (&video_context));
    if (gst_d3d11_result (hr, device))
      priv->video_context = video_context;
  }
  g_mutex_unlock (&priv->resource_lock);

  return priv->video_context;
}

/**
 * gst_d3d11_device_lock:
 * @device: a #GstD3D11Device
 *
 * Take lock for @device. Any thread-unsafe API call needs to be
 * protected by this method. This call must be paired with
 * gst_d3d11_device_unlock()
 *
 * Since: 1.20
 */
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

/**
 * gst_d3d11_device_unlock:
 * @device: a #GstD3D11Device
 *
 * Release lock for @device. This call must be paired with
 * gst_d3d11_device_lock()
 *
 * Since: 1.20
 */
void
gst_d3d11_device_unlock (GstD3D11Device * device)
{
  GstD3D11DevicePrivate *priv;

  g_return_if_fail (GST_IS_D3D11_DEVICE (device));

  priv = device->priv;

  g_rec_mutex_unlock (&priv->extern_lock);
  GST_TRACE_OBJECT (device, "device unlocked");
}

/**
 * gst_d3d11_device_format_from_gst:
 * @device: a #GstD3D11Device
 * @format: a #GstVideoFormat
 *
 * Returns: (transfer none) (nullable): a pointer to #GstD3D11Format
 * or %NULL if @format is not supported by @device
 *
 * Since: 1.20
 */
const GstD3D11Format *
gst_d3d11_device_format_from_gst (GstD3D11Device * device,
    GstVideoFormat format)
{
  GstD3D11DevicePrivate *priv;
  guint i;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  priv = device->priv;

  for (i = 0; i < G_N_ELEMENTS (priv->format_table); i++) {
    if (priv->format_table[i].format == format)
      return &priv->format_table[i];
  }

  return NULL;
}
