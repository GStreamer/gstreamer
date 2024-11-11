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

#include "gstd3d12swapchainsink.h"
#include "gstd3d12pluginutils.h"
#include "gstd3d12overlaycompositor.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <wrl.h>
#include <vector>
#include <memory>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */


enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_FORCE_ASPECT_RATIO,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_BORDER_COLOR,
  PROP_SWAPCHAIN,
  PROP_SAMPLING_METHOD,
  PROP_MSAA,
  PROP_HUE,
  PROP_SATURATION,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,
};

#define DEFAULT_ADAPTER -1
#define DEFAULT_FORCE_ASPECT_RATIO TRUE
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_BORDER_COLOR (G_GUINT64_CONSTANT(0xffff000000000000))
#define DEFAULT_SAMPLING_METHOD GST_D3D12_SAMPLING_METHOD_BILINEAR
#define DEFAULT_MSAA GST_D3D12_MSAA_DISABLED
#define DEFAULT_HUE 0.0
#define DEFAULT_SATURATION 1.0
#define DEFAULT_BRIGHTNESS 0.0
#define DEFAULT_CONTRAST 1.0

#define BACK_BUFFER_COUNT 2

enum
{
  SIGNAL_RESIZE,
  SIGNAL_LAST
};

static guint d3d12_swapchain_sink_signals[SIGNAL_LAST] = { 0, };

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE (GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)));

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_swapchain_sink_debug);
#define GST_CAT_DEFAULT gst_d3d12_swapchain_sink_debug

/* *INDENT-OFF* */
struct BackBuffer
{
  BackBuffer (GstBuffer * buffer, ID3D12Resource * res)
  {
    backbuf = buffer;
    resource = res;
  }

  ~BackBuffer ()
  {
    gst_clear_buffer (&backbuf);
  }

  GstBuffer *backbuf = nullptr;
  ComPtr<ID3D12Resource> resource;
};

struct GstD3D12SwapChainSinkPrivate
{
  GstD3D12SwapChainSinkPrivate ()
  {
    const gchar *channels[4] = { "HUE", "SATURATION",
      "BRIGHTNESS", "CONTRAST"
    };

    convert_config = gst_structure_new_empty ("convert-config");
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
    gst_video_info_init (&info);
    gst_video_info_set_format (&display_info, GST_VIDEO_FORMAT_RGBA,
        width, height);
    update_border_color ();
    color_balance_channels = nullptr;
    for (guint i = 0; i < G_N_ELEMENTS (channels); i++) {
      auto ch = (GstColorBalanceChannel *)
          g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, nullptr);
      ch->label = g_strdup (channels[i]);
      ch->min_value = -1000;
      ch->max_value = 1000;

      color_balance_channels = g_list_append (color_balance_channels, ch);
    }
  }

  ~GstD3D12SwapChainSinkPrivate ()
  {
    stop ();

    gst_structure_free (convert_config);
    gst_clear_object (&comp);
    gst_clear_object (&ca_pool);
    gst_clear_object (&fence_data_pool);
    gst_clear_buffer (&msaa_buf);
    backbuf.clear ();

    auto iter = color_balance_channels;
    while (iter) {
      g_object_unref (iter->data);
      iter = iter->next;
    }

    g_list_free (color_balance_channels);
  }

  void stop ()
  {
    if (cq && swapchain && fence_val > 0)
     gst_d3d12_cmd_queue_idle_for_swapchain (cq, fence_val);
    if (pool) {
      gst_buffer_pool_set_active (pool, FALSE);
      gst_clear_object (&pool);
    }
    gst_clear_caps (&caps);
    gst_clear_buffer (&cached_buf);
    gst_clear_object (&conv);
    convert_format = GST_VIDEO_FORMAT_UNKNOWN;
    caps_updated = false;
    first_present = true;
  }

  void update_border_color ()
  {
    border_color_val[0] = ((border_color &
      G_GUINT64_CONSTANT(0x0000ffff00000000)) >> 32) / (FLOAT) G_MAXUINT16;
    border_color_val[1] = ((border_color &
      G_GUINT64_CONSTANT(0x00000000ffff0000)) >> 16) / (FLOAT) G_MAXUINT16;
    border_color_val[2] = (border_color &
      G_GUINT64_CONSTANT(0x000000000000ffff)) / (FLOAT) G_MAXUINT16;
    border_color_val[3] = ((border_color &
      G_GUINT64_CONSTANT(0xffff000000000000)) >> 48) / (FLOAT) G_MAXUINT16;
  }

  std::recursive_mutex lock;
  GstVideoInfo info;
  GstVideoInfo display_info;
  guint display_width;
  guint display_height;
  GstVideoFormat convert_format = GST_VIDEO_FORMAT_UNKNOWN;
  ComPtr<IDXGISwapChain4> swapchain;
  ComPtr<ID3D12GraphicsCommandList> cl;
  std::vector<std::shared_ptr<BackBuffer>> backbuf;
  GstStructure *convert_config = nullptr;
  GstD3D12FenceDataPool *fence_data_pool = nullptr;
  GstBufferPool *pool = nullptr;
  GstD3D12CmdQueue *cq = nullptr;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstBuffer *cached_buf = nullptr;
  GstBuffer *msaa_buf = nullptr;
  GstCaps *caps = nullptr;
  GstD3D12Converter *conv = nullptr;
  GstD3D12OverlayCompositor *comp = nullptr;
  guint64 fence_val = 0;
  bool caps_updated = false;
  bool first_present = true;
  bool output_updated = false;
  D3D12_BOX crop_rect = { };
  D3D12_BOX prev_crop_rect = { };
  FLOAT border_color_val[4];
  GstVideoRectangle viewport = { };
  gboolean auto_resize = FALSE;

  gint adapter = DEFAULT_ADAPTER;
  gint force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  guint width = DEFAULT_WIDTH;
  guint height = DEFAULT_HEIGHT;
  guint64 border_color = DEFAULT_BORDER_COLOR;
  GstD3D12SamplingMethod sampling_method = DEFAULT_SAMPLING_METHOD;
  GstD3D12MSAAMode msaa_mode = DEFAULT_MSAA;
  GList *color_balance_channels = nullptr;
  gdouble hue = DEFAULT_HUE;
  gdouble saturation = DEFAULT_SATURATION;
  gdouble brightness = DEFAULT_BRIGHTNESS;
  gdouble contrast = DEFAULT_CONTRAST;
};
/* *INDENT-ON* */

struct _GstD3D12SwapChainSink
{
  GstVideoSink parent;

  GstD3D12Device *device;

  GstD3D12SwapChainSinkPrivate *priv;
};

static void gst_d3d12_swapchain_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d12_swapchain_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d12_swapchain_sink_finalize (GObject * object);
static void gst_d3d12_swapchain_sink_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_d3d12_swapchain_sink_start (GstBaseSink * sink);
static gboolean gst_d3d12_swapchain_sink_stop (GstBaseSink * sink);
static gboolean gst_d3d12_swapchain_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query);
static gboolean gst_d3d12_swapchain_sink_query (GstBaseSink * sink,
    GstQuery * query);
static GstFlowReturn gst_d3d12_swapchain_sink_prepare (GstBaseSink * sink,
    GstBuffer * buf);
static gboolean gst_d3d12_swapchain_sink_set_info (GstVideoSink * sink,
    GstCaps * caps, const GstVideoInfo * info);
static GstFlowReturn gst_d3d12_swapchain_sink_show_frame (GstVideoSink * sink,
    GstBuffer * buf);
static void gst_d3d12_swapchain_sink_resize (GstD3D12SwapChainSink * self,
    guint width, guint height);
static void
gst_d3d12_swapchain_sink_resize_internal (GstD3D12SwapChainSink * self,
    guint width, guint height);

static void
gst_d3d12_swapchain_sink_color_balance_init (GstColorBalanceInterface * iface);

#define gst_d3d12_swapchain_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstD3D12SwapChainSink, gst_d3d12_swapchain_sink,
    GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE,
        gst_d3d12_swapchain_sink_color_balance_init));

static void
gst_d3d12_swapchain_sink_class_init (GstD3D12SwapChainSinkClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto basesink_class = GST_BASE_SINK_CLASS (klass);
  auto videosink_class = GST_VIDEO_SINK_CLASS (klass);

  object_class->set_property = gst_d3d12_swapchain_sink_set_property;
  object_class->get_property = gst_d3d12_swapchain_sink_get_property;
  object_class->finalize = gst_d3d12_swapchain_sink_finalize;

  g_object_class_install_property (object_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_WIDTH,
      g_param_spec_uint ("swapchain-width", "Swapchain Width",
          "Width of swapchain buffers",
          1, D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION, DEFAULT_WIDTH,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_HEIGHT,
      g_param_spec_uint ("swapchain-height", "Swapchain Height",
          "Height of swapchain buffers",
          1, D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION, DEFAULT_HEIGHT,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_BORDER_COLOR,
      g_param_spec_uint64 ("border-color", "Border Color",
          "ARGB64 representation of the border color to use",
          0, G_MAXUINT64, DEFAULT_BORDER_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_SWAPCHAIN,
      g_param_spec_pointer ("swapchain", "SwapChain",
          "DXGI swapchain handle",
          (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT | G_PARAM_READABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_SAMPLING_METHOD,
      g_param_spec_enum ("sampling-method", "Sampling method",
          "Sampler filter type to use", GST_TYPE_D3D12_SAMPLING_METHOD,
          DEFAULT_SAMPLING_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_MSAA,
      g_param_spec_enum ("msaa", "MSAA",
          "MSAA (Multi-Sampling Anti-Aliasing) level",
          GST_TYPE_D3D12_MSAA_MODE, DEFAULT_MSAA,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_HUE,
      g_param_spec_double ("hue", "Hue", "hue", -1.0, 1.0, DEFAULT_HUE,
          (GParamFlags) (GST_PARAM_CONTROLLABLE |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_SATURATION,
      g_param_spec_double ("saturation", "Saturation", "saturation", 0.0, 2.0,
          DEFAULT_SATURATION,
          (GParamFlags) (GST_PARAM_CONTROLLABLE |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_BRIGHTNESS,
      g_param_spec_double ("brightness", "Brightness", "brightness", -1.0, 1.0,
          DEFAULT_BRIGHTNESS,
          (GParamFlags) (GST_PARAM_CONTROLLABLE |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_CONTRAST,
      g_param_spec_double ("contrast", "Contrast", "contrast", 0.0, 2.0,
          DEFAULT_CONTRAST,
          (GParamFlags) (GST_PARAM_CONTROLLABLE |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  d3d12_swapchain_sink_signals[SIGNAL_RESIZE] =
      g_signal_new_class_handler ("resize", G_TYPE_FROM_CLASS (klass),
      (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_d3d12_swapchain_sink_resize), nullptr, nullptr, nullptr,
      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 SwapChain Sink", "Sink/Video",
      "DXGI composition swapchain sink",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  basesink_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_start);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_stop);
  basesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_propose_allocation);
  basesink_class->query = GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_query);
  basesink_class->prepare =
      GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_prepare);

  videosink_class->set_info =
      GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_set_info);
  videosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_d3d12_swapchain_sink_show_frame);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_swapchain_sink_debug,
      "d3d12swapchainsink", 0, "d3d12swapchainsink");

  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_SAMPLING_METHOD,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_MSAA_MODE, (GstPluginAPIFlags) 0);
}

static void
gst_d3d12_swapchain_sink_init (GstD3D12SwapChainSink * self)
{
  self->priv = new GstD3D12SwapChainSinkPrivate ();
}

static void
gst_d3d12_swapchain_sink_finalize (GObject * object)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (object);

  delete self->priv;

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_swapchain_sink_update_color_balance (GstD3D12SwapChainSink * self,
    const gchar * name, gdouble * prev, gdouble value)
{
  auto priv = self->priv;

  if (*prev != value) {
    GList *iter = priv->color_balance_channels;

    *prev = value;
    priv->output_updated = true;
    while (iter) {
      auto ch = (GstColorBalanceChannel *) iter->data;
      if (g_ascii_strcasecmp (ch->label, name) == 0) {
        auto iface = GST_COLOR_BALANCE (self);
        gst_color_balance_value_changed (iface, ch,
            gst_color_balance_get_value (iface, ch));
        break;
      }

      iter = g_list_next (iter);
    }
  }
}

static void
gst_d3d12_swapchain_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_ADAPTER:
      priv->adapter = g_value_get_int (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
    {
      auto val = g_value_get_boolean (value);
      if (val != priv->force_aspect_ratio) {
        priv->force_aspect_ratio = val;
        gst_d3d12_swapchain_sink_resize_internal (self,
            priv->width, priv->height);
      }
      break;
    }
    case PROP_BORDER_COLOR:
      priv->border_color = g_value_get_uint64 (value);
      priv->update_border_color ();
      break;
    case PROP_SAMPLING_METHOD:
    {
      auto sampling_method = (GstD3D12SamplingMethod) g_value_get_enum (value);
      if (priv->sampling_method != sampling_method) {
        priv->sampling_method = sampling_method;
        if (priv->conv) {
          g_object_set (priv->conv, "sampler-filter",
              gst_d3d12_sampling_method_to_native (priv->sampling_method),
              nullptr);
        }
      }
      break;
    }
    case PROP_MSAA:
    {
      auto msaa = (GstD3D12MSAAMode) g_value_get_enum (value);
      if (priv->msaa_mode != msaa) {
        priv->msaa_mode = msaa;
        gst_d3d12_swapchain_sink_resize_internal (self,
            priv->width, priv->height);
      }
      break;
    }
    case PROP_HUE:
      gst_d3d12_swapchain_sink_update_color_balance (self,
          "HUE", &priv->hue, g_value_get_double (value));
      break;
    case PROP_SATURATION:
      gst_d3d12_swapchain_sink_update_color_balance (self,
          "SATURATION", &priv->saturation, g_value_get_double (value));
      break;
    case PROP_BRIGHTNESS:
      gst_d3d12_swapchain_sink_update_color_balance (self,
          "BRIGHTNESS", &priv->brightness, g_value_get_double (value));
      break;
    case PROP_CONTRAST:
      gst_d3d12_swapchain_sink_update_color_balance (self,
          "CONTRAST", &priv->contrast, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d12_swapchain_sink_resize_unlocked (GstD3D12SwapChainSink * self,
    guint width, guint height)
{
  auto priv = self->priv;

  if (width != priv->width || height != priv->height) {
    GST_DEBUG_OBJECT (self, "Resizing swapchain, %ux%u -> %ux%u",
        priv->width, priv->height, width, height);
    if (priv->cq && priv->swapchain && priv->fence_val > 0)
      gst_d3d12_cmd_queue_idle_for_swapchain (priv->cq, priv->fence_val);

    priv->backbuf.clear ();
    priv->width = width;
    priv->height = height;
    priv->first_present = true;
    gst_video_info_set_format (&priv->display_info, GST_VIDEO_FORMAT_RGBA,
        width, height);
  }

  if (priv->swapchain && priv->backbuf.empty ()) {
    auto hr = priv->swapchain->ResizeBuffers (BACK_BUFFER_COUNT,
        priv->width, priv->height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Resize failed");
      return FALSE;
    }

    for (guint i = 0; i < BACK_BUFFER_COUNT; i++) {
      ComPtr < ID3D12Resource > backbuf;
      hr = priv->swapchain->GetBuffer (i, IID_PPV_ARGS (&backbuf));
      if (!gst_d3d12_result (hr, self->device)) {
        GST_ERROR_OBJECT (self, "Couldn't get backbuffer");
        priv->backbuf.clear ();
        return FALSE;
      }

      auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, self->device,
          backbuf.Get (), 0, nullptr, nullptr);
      auto buf = gst_buffer_new ();
      gst_buffer_append_memory (buf, mem);
      auto swapbuf = std::make_shared < BackBuffer > (buf, backbuf.Get ());
      priv->backbuf.push_back (swapbuf);
    }
  }

  gst_clear_buffer (&priv->msaa_buf);
  if (priv->swapchain) {
    DXGI_SAMPLE_DESC sample_desc = { };
    gst_d3d12_calculate_sample_desc_for_msaa (self->device,
        DXGI_FORMAT_R8G8B8A8_UNORM, priv->msaa_mode, &sample_desc);

    if (sample_desc.Count > 1) {
      auto device = gst_d3d12_device_get_device_handle (self->device);

      GST_DEBUG_OBJECT (self, "Enable MSAA x%d with quality level %d",
          sample_desc.Count, sample_desc.Quality);
      D3D12_HEAP_PROPERTIES heap_prop =
          CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
      D3D12_RESOURCE_DESC resource_desc =
          CD3DX12_RESOURCE_DESC::Tex2D (DXGI_FORMAT_R8G8B8A8_UNORM,
          priv->width, priv->height,
          1, 1, sample_desc.Count, sample_desc.Quality,
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
      D3D12_CLEAR_VALUE clear_value = { };
      clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      for (guint i = 0; i < 4; i++)
        clear_value.Color[i] = priv->border_color_val[i];

      ComPtr < ID3D12Resource > msaa_texture;
      auto hr = device->CreateCommittedResource (&heap_prop,
          D3D12_HEAP_FLAG_NONE,
          &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
          IID_PPV_ARGS (&msaa_texture));
      if (gst_d3d12_result (hr, self->device)) {
        auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, self->device,
            msaa_texture.Get (), 0, nullptr, nullptr);
        priv->msaa_buf = gst_buffer_new ();
        gst_buffer_append_memory (priv->msaa_buf, mem);
      }
    }
  }

  return TRUE;
}

static gboolean
gst_d3d12_swapchain_sink_ensure_swapchain (GstD3D12SwapChainSink * self)
{
  auto priv = self->priv;
  if (priv->swapchain)
    return TRUE;

  if (!gst_d3d12_ensure_element_data (GST_ELEMENT_CAST (self), priv->adapter,
          &self->device)) {
    GST_ERROR_OBJECT (self, "Cannot create device");
    return FALSE;
  }

  priv->cq = gst_d3d12_device_get_cmd_queue (self->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  auto cq = gst_d3d12_cmd_queue_get_handle (priv->cq);
  auto factory = gst_d3d12_device_get_factory_handle (self->device);

  DXGI_SWAP_CHAIN_DESC1 desc = { };
  desc.Width = priv->width;
  desc.Height = priv->height;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = BACK_BUFFER_COUNT;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

  ComPtr < IDXGISwapChain1 > swapchain;
  auto hr = factory->CreateSwapChainForComposition (cq, &desc, nullptr,
      &swapchain);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create swapchain");
    return FALSE;
  }

  hr = swapchain.As (&priv->swapchain);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't get IDXGISwapChain4 interface");
    return FALSE;
  }

  auto device = gst_d3d12_device_get_device_handle (self->device);
  priv->ca_pool = gst_d3d12_cmd_alloc_pool_new (device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  GstVideoInfo info;
  gst_video_info_set_format (&info,
      GST_VIDEO_FORMAT_RGBA, priv->width, priv->height);
  priv->comp = gst_d3d12_overlay_compositor_new (self->device, &info);

  return gst_d3d12_swapchain_sink_resize_unlocked (self,
      priv->width, priv->height);
}

static void
gst_d3d12_swapchain_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, priv->force_aspect_ratio);
      break;
    case PROP_WIDTH:
      g_value_set_uint (value, priv->width);
      break;
    case PROP_HEIGHT:
      g_value_set_uint (value, priv->height);
      break;
    case PROP_BORDER_COLOR:
      g_value_set_uint64 (value, priv->border_color);
      break;
    case PROP_SWAPCHAIN:
      gst_d3d12_swapchain_sink_ensure_swapchain (self);
      g_value_set_pointer (value, priv->swapchain.Get ());
      break;
    case PROP_SAMPLING_METHOD:
      g_value_set_enum (value, priv->sampling_method);
      break;
    case PROP_MSAA:
      g_value_set_enum (value, priv->msaa_mode);
      break;
    case PROP_HUE:
      g_value_set_double (value, priv->hue);
      break;
    case PROP_SATURATION:
      g_value_set_double (value, priv->saturation);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_double (value, priv->brightness);
      break;
    case PROP_CONTRAST:
      g_value_set_double (value, priv->contrast);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_swapchain_sink_set_context (GstElement * element,
    GstContext * context)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (element);
  auto priv = self->priv;

  gst_d3d12_handle_set_context (element, context, priv->adapter, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d12_swapchain_sink_set_info (GstVideoSink * sink, GstCaps * caps,
    const GstVideoInfo * info)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "set caps %" GST_PTR_FORMAT, caps);

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  gst_caps_replace (&priv->caps, caps);
  priv->info = *info;
  priv->caps_updated = true;

  auto video_width = GST_VIDEO_INFO_WIDTH (&priv->info);
  auto video_height = GST_VIDEO_INFO_HEIGHT (&priv->info);
  auto video_par_n = GST_VIDEO_INFO_PAR_N (&priv->info);
  auto video_par_d = GST_VIDEO_INFO_PAR_D (&priv->info);
  gint display_par_n = 1;
  gint display_par_d = 1;
  guint num, den;

  if (!gst_video_calculate_display_ratio (&num, &den, video_width,
          video_height, video_par_n, video_par_d, display_par_n,
          display_par_d)) {
    GST_ELEMENT_WARNING (self, CORE, NEGOTIATION, (nullptr),
        ("Error calculating the output display ratio of the video."));
    GST_VIDEO_SINK_WIDTH (self) = video_width;
    GST_VIDEO_SINK_HEIGHT (self) = video_height;
  } else {
    GST_DEBUG_OBJECT (self,
        "video width/height: %dx%d, calculated display ratio: %d/%d format: %s",
        video_width, video_height, num, den,
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&priv->info)));

    if (video_height % den == 0) {
      GST_DEBUG_OBJECT (self, "keeping video height");
      GST_VIDEO_SINK_WIDTH (self) = (guint)
          gst_util_uint64_scale_int (video_height, num, den);
      GST_VIDEO_SINK_HEIGHT (self) = video_height;
    } else if (video_width % num == 0) {
      GST_DEBUG_OBJECT (self, "keeping video width");
      GST_VIDEO_SINK_WIDTH (self) = video_width;
      GST_VIDEO_SINK_HEIGHT (self) = (guint)
          gst_util_uint64_scale_int (video_width, den, num);
    } else {
      GST_DEBUG_OBJECT (self, "approximating while keeping video height");
      GST_VIDEO_SINK_WIDTH (self) = (guint)
          gst_util_uint64_scale_int (video_height, num, den);
      GST_VIDEO_SINK_HEIGHT (self) = video_height;
    }
  }

  if (GST_VIDEO_SINK_WIDTH (self) <= 0) {
    GST_WARNING_OBJECT (self, "Invalid display width %d",
        GST_VIDEO_SINK_WIDTH (self));
    GST_VIDEO_SINK_WIDTH (self) = 8;
  }

  if (GST_VIDEO_SINK_HEIGHT (self) <= 0) {
    GST_WARNING_OBJECT (self, "Invalid display height %d",
        GST_VIDEO_SINK_HEIGHT (self));
    GST_VIDEO_SINK_HEIGHT (self) = 8;
  }

  GST_DEBUG_OBJECT (self, "scaling to %dx%d",
      GST_VIDEO_SINK_WIDTH (self), GST_VIDEO_SINK_HEIGHT (self));

  if (priv->pool) {
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_clear_object (&priv->pool);
  }

  priv->pool = gst_d3d12_buffer_pool_new (self->device);
  auto config = gst_buffer_pool_get_config (priv->pool);

  gst_buffer_pool_config_set_params (config, priv->caps, priv->info.size, 0, 0);
  if (!gst_buffer_pool_set_config (priv->pool, config) ||
      !gst_buffer_pool_set_active (priv->pool, TRUE)) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (nullptr),
        ("Couldn't setup buffer pool"));
    gst_clear_object (&priv->pool);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_swapchain_sink_foreach_meta (GstBuffer * buffer, GstMeta ** meta,
    GstBuffer * uploaded)
{
  if ((*meta)->info->api != GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE)
    return TRUE;

  auto cmeta = (GstVideoOverlayCompositionMeta *) (*meta);
  if (!cmeta->overlay)
    return TRUE;

  if (gst_video_overlay_composition_n_rectangles (cmeta->overlay) == 0)
    return TRUE;

  gst_buffer_add_video_overlay_composition_meta (uploaded, cmeta->overlay);

  return TRUE;
}

static gboolean
gst_d3d12_swapchain_sink_render (GstD3D12SwapChainSink * self)
{
  auto priv = self->priv;
  if (!priv->cached_buf) {
    GST_DEBUG_OBJECT (self, "No cached buffer");
    return TRUE;
  }

  auto crop_rect = priv->crop_rect;
  auto crop_meta = gst_buffer_get_video_crop_meta (priv->cached_buf);
  if (crop_meta) {
    crop_rect = CD3DX12_BOX (crop_meta->x, crop_meta->y,
        crop_meta->x + crop_meta->width, crop_meta->y + crop_meta->height);
  }

  if (crop_rect != priv->prev_crop_rect) {
    g_object_set (priv->conv, "src-x", (gint) crop_rect.left,
        "src-y", (gint) crop_rect.top,
        "src-width", (gint) (crop_rect.right - crop_rect.left),
        "src-height", (gint) (crop_rect.bottom - crop_rect.top), nullptr);
    priv->prev_crop_rect = crop_rect;
  }

  priv->lock.lock ();
  if (priv->first_present || priv->output_updated) {
    GstVideoRectangle dst_rect = { };
    dst_rect.w = priv->width;
    dst_rect.h = priv->height;

    if (priv->force_aspect_ratio) {
      GstVideoRectangle src_rect = { };
      src_rect.w = priv->display_width;
      src_rect.h = priv->display_height;

      gst_video_sink_center_rect (src_rect, dst_rect, &priv->viewport, TRUE);
    } else {
      priv->viewport = dst_rect;
    }

    g_object_set (priv->conv, "dest-x", priv->viewport.x,
        "dest-y", priv->viewport.y, "dest-width", priv->viewport.w,
        "dest-height", priv->viewport.h, "hue", priv->hue,
        "saturation", priv->saturation, "brightness", priv->brightness,
        "contrast", priv->contrast, nullptr);
    gst_d3d12_overlay_compositor_update_viewport (priv->comp, &priv->viewport);

    priv->first_present = false;
    priv->output_updated = false;
  }
  priv->lock.unlock ();

  gst_d3d12_overlay_compositor_upload (priv->comp, priv->cached_buf);

  GstD3D12CmdAlloc *gst_ca;
  if (!gst_d3d12_cmd_alloc_pool_acquire (priv->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    return FALSE;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_cmd_alloc_unref (gst_ca);
    return FALSE;
  }

  ComPtr < ID3D12GraphicsCommandList > cl;
  if (!priv->cl) {
    auto device_handle = gst_d3d12_device_get_device_handle (self->device);
    hr = device_handle->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&cl));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create command list");
      gst_d3d12_cmd_alloc_unref (gst_ca);
      return FALSE;
    }

    priv->cl = cl;
  } else {
    cl = priv->cl;
    hr = cl->Reset (ca, nullptr);
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't reset command list");
      gst_d3d12_cmd_alloc_unref (gst_ca);
      return FALSE;
    }
  }

  auto cur_idx = priv->swapchain->GetCurrentBackBufferIndex ();
  auto backbuf = priv->backbuf[cur_idx]->backbuf;

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto mem = (GstD3D12Memory *) gst_buffer_peek_memory (backbuf, 0);
  auto backbuf_texture = gst_d3d12_memory_get_resource_handle (mem);
  GstBuffer *conv_outbuf = backbuf;
  ID3D12Resource *msaa_resource = nullptr;

  if (priv->msaa_buf) {
    conv_outbuf = priv->msaa_buf;
    mem = (GstD3D12Memory *) gst_buffer_peek_memory (conv_outbuf, 0);
    msaa_resource = gst_d3d12_memory_get_resource_handle (mem);
    auto msaa_buf = gst_buffer_ref (priv->msaa_buf);
    gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (msaa_buf));
  } else {
    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier (1, &barrier);
  }

  if (priv->viewport.x != 0 || priv->viewport.y != 0 ||
      (guint) priv->viewport.w != priv->width ||
      (guint) priv->viewport.h != priv->height) {
    auto rtv_heap = gst_d3d12_memory_get_render_target_view_heap (mem);
    auto cpu_handle = GetCPUDescriptorHandleForHeapStart (rtv_heap);
    cl->ClearRenderTargetView (cpu_handle, priv->border_color_val, 0, nullptr);
  }

  if (!gst_d3d12_converter_convert_buffer (priv->conv,
          priv->cached_buf, conv_outbuf, fence_data, cl.Get (), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't build convert command");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  if (!gst_d3d12_overlay_compositor_draw (priv->comp,
          conv_outbuf, fence_data, cl.Get ())) {
    GST_ERROR_OBJECT (self, "Couldn't build overlay command");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  if (msaa_resource) {
    std::vector < D3D12_RESOURCE_BARRIER > barriers;
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (msaa_resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_DEST));
    cl->ResourceBarrier (barriers.size (), barriers.data ());

    cl->ResolveSubresource (backbuf_texture, 0, msaa_resource, 0,
        DXGI_FORMAT_R8G8B8A8_UNORM);

    barriers.clear ();
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (msaa_resource,
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET));
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
            D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COMMON));
    cl->ResourceBarrier (barriers.size (), barriers.data ());
  } else {
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    cl->ResourceBarrier (1, &barrier);
  }

  hr = cl->Close ();
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  ID3D12CommandList *cmd_list[] = { cl.Get () };
  hr = gst_d3d12_cmd_queue_execute_command_lists (priv->cq,
      1, cmd_list, &priv->fence_val);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Signal failed");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  gst_d3d12_cmd_queue_set_notify (priv->cq, priv->fence_val,
      fence_data, (GDestroyNotify) gst_d3d12_fence_data_unref);

  return TRUE;
}

static gboolean
gst_d3d12_swapchain_sink_set_buffer (GstD3D12SwapChainSink * self,
    GstBuffer * buffer, gboolean is_prepare)
{
  auto priv = self->priv;
  bool need_render = false;
  bool update_converter = false;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!priv->swapchain || priv->backbuf.empty ()) {
    GST_ERROR_OBJECT (self, "Swapchain was not configured");
    return FALSE;
  }

  if (is_prepare) {
    if (priv->caps_updated) {
      need_render = false;
      update_converter = false;
    } else {
      need_render = true;
      update_converter = false;
    }
  } else {
    if (priv->caps_updated) {
      need_render = true;
      update_converter = true;
      priv->caps_updated = false;
      if (priv->auto_resize) {
        gst_clear_buffer (&priv->cached_buf);
        gst_d3d12_swapchain_sink_resize_internal (self,
            GST_VIDEO_SINK_WIDTH (self), GST_VIDEO_SINK_HEIGHT (self));
      }
    } else {
      need_render = false;
      update_converter = false;
    }
  }

  if (update_converter) {
    gst_d3d12_cmd_queue_idle_for_swapchain (priv->cq, priv->fence_val);

    auto format = GST_VIDEO_INFO_FORMAT (&priv->info);
    if (priv->convert_format != format)
      gst_clear_object (&priv->conv);

    priv->display_width = GST_VIDEO_SINK_WIDTH (self);
    priv->display_height = GST_VIDEO_SINK_HEIGHT (self);
    priv->convert_format = format;
    priv->crop_rect = CD3DX12_BOX (0, 0, priv->info.width, priv->info.height);
    priv->prev_crop_rect = priv->crop_rect;
    priv->first_present = true;
    gst_clear_buffer (&priv->cached_buf);

    if (!priv->conv) {
      DXGI_SAMPLE_DESC sample_desc = { };
      gst_d3d12_calculate_sample_desc_for_msaa (self->device,
          DXGI_FORMAT_R8G8B8A8_UNORM, priv->msaa_mode, &sample_desc);

      gst_structure_set (priv->convert_config,
          GST_D3D12_CONVERTER_OPT_DEST_ALPHA_MODE,
          GST_TYPE_D3D12_CONVERTER_ALPHA_MODE,
          GST_VIDEO_INFO_HAS_ALPHA (&priv->info) ?
          GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED :
          GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED,
          GST_D3D12_CONVERTER_OPT_SAMPLER_FILTER,
          GST_TYPE_D3D12_CONVERTER_SAMPLER_FILTER,
          gst_d3d12_sampling_method_to_native (priv->sampling_method),
          GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_COUNT, G_TYPE_UINT,
          sample_desc.Count,
          GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_QUALITY, G_TYPE_UINT,
          sample_desc.Quality,
          GST_D3D12_CONVERTER_OPT_COLOR_BALANCE,
          GST_TYPE_D3D12_CONVERTER_COLOR_BALANCE,
          GST_D3D12_CONVERTER_COLOR_BALANCE_ENABLED, nullptr);

      priv->conv = gst_d3d12_converter_new (self->device, nullptr, &priv->info,
          &priv->display_info, nullptr, nullptr,
          gst_structure_copy (priv->convert_config));
      if (!priv->conv) {
        GST_ERROR_OBJECT (self, "Couldn't create converter");
        return FALSE;
      }
    } else {
      g_object_set (priv->conv, "src-x", 0, "src-y", 0,
          "src-width", priv->info.width, "src-height", priv->info.height,
          nullptr);
    }
  }

  if (!need_render)
    return TRUE;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d12_memory (mem)) {
    GstBuffer *upload = nullptr;
    gst_buffer_pool_acquire_buffer (priv->pool, &upload, nullptr);
    if (!upload) {
      GST_ERROR_OBJECT (self, "Couldn't allocate upload buffer");
      return FALSE;
    }

    GstVideoFrame in_frame, out_frame;
    if (!gst_video_frame_map (&in_frame, &priv->info, buffer, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "Couldn't map input frame");
      gst_buffer_unref (upload);
      return FALSE;
    }

    if (!gst_video_frame_map (&out_frame, &priv->info, upload, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "Couldn't map upload frame");
      gst_video_frame_unmap (&in_frame);
      gst_buffer_unref (upload);
      return FALSE;
    }

    auto copy_ret = gst_video_frame_copy (&out_frame, &in_frame);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&in_frame);
    if (!copy_ret) {
      GST_ERROR_OBJECT (self, "Couldn't copy frame");
      gst_buffer_unref (upload);
      return FALSE;
    }

    gst_buffer_foreach_meta (buffer,
        (GstBufferForeachMetaFunc) gst_d3d12_swapchain_sink_foreach_meta,
        upload);

    gst_clear_buffer (&priv->cached_buf);
    priv->cached_buf = upload;
  } else {
    gst_buffer_replace (&priv->cached_buf, buffer);
  }

  return gst_d3d12_swapchain_sink_render (self);
}

static void
gst_d3d12_swapchain_sink_resize_internal (GstD3D12SwapChainSink * self,
    guint width, guint height)
{
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!gst_d3d12_swapchain_sink_resize_unlocked (self, width, height)) {
    GST_ERROR_OBJECT (self, "Couldn't resize swapchain");
    return;
  }

  if (priv->swapchain && priv->cached_buf &&
      gst_d3d12_swapchain_sink_render (self)) {
    GST_DEBUG_OBJECT (self, "resize %ux%u", width, height);
    auto hr = priv->swapchain->Present (0, 0);
    if (!gst_d3d12_result (hr, self->device))
      GST_ERROR_OBJECT (self, "Present failed");

    gst_d3d12_cmd_queue_execute_command_lists (priv->cq,
        0, nullptr, &priv->fence_val);
  }
}

static void
gst_d3d12_swapchain_sink_resize (GstD3D12SwapChainSink * self, guint width,
    guint height)
{
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (width == 0 && height == 0) {
    GST_DEBUG_OBJECT (self, "Enable auto resize");
    priv->auto_resize = TRUE;
    if (GST_VIDEO_SINK_WIDTH (self) > 0 && GST_VIDEO_SINK_HEIGHT (self) > 0) {
      width = GST_VIDEO_SINK_WIDTH (self);
      height = GST_VIDEO_SINK_HEIGHT (self);
    } else {
      GST_DEBUG_OBJECT (self, "Caps is not configured yet");
      return;
    }
  } else {
    if (width == 0 || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
      GST_WARNING_OBJECT (self, "Invalid width value %u", width);
      return;
    }

    if (height == 0 || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
      GST_WARNING_OBJECT (self, "Invalid height value %u", height);
      return;
    }

    priv->auto_resize = FALSE;
  }

  gst_d3d12_swapchain_sink_resize_internal (self, width, height);
}

static gboolean
gst_d3d12_swapchain_sink_start (GstBaseSink * sink)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!gst_d3d12_swapchain_sink_ensure_swapchain (self)) {
    GST_ERROR_OBJECT (self, "Couldn't create swapchain");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_swapchain_sink_stop (GstBaseSink * sink)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (sink);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  priv->stop ();

  return TRUE;
}

static gboolean
gst_d3d12_swapchain_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (sink);
  GstCaps *caps;
  GstBufferPool *pool = nullptr;
  GstVideoInfo info;
  guint size;
  gboolean need_pool;

  if (!self->device) {
    GST_WARNING_OBJECT (self, "No configured device");
    return FALSE;
  }

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_WARNING_OBJECT (self, "no caps specified");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  size = info.size;

  bool is_d3d12 = false;
  auto features = gst_caps_get_features (caps, 0);
  if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
    is_d3d12 = true;
    GST_DEBUG_OBJECT (self, "upstream support d3d12 memory");
  }

  if (need_pool) {
    if (is_d3d12)
      pool = gst_d3d12_buffer_pool_new (self->device);
    else
      pool = gst_video_buffer_pool_new ();

    auto config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!is_d3d12) {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    }

    gst_buffer_pool_config_set_params (config, caps, size, 2, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (pool, "Couldn't set config");
      gst_object_unref (pool);

      return FALSE;
    }

    if (is_d3d12) {
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
          nullptr);
      gst_structure_free (config);
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 2, 0);
  gst_clear_object (&pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);
  if (is_d3d12) {
    gst_query_add_allocation_meta (query,
        GST_VIDEO_CROP_META_API_TYPE, nullptr);
  }

  return TRUE;
}

static gboolean
gst_d3d12_swapchain_sink_query (GstBaseSink * sink, GstQuery * query)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (sink);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d12_handle_context_query (GST_ELEMENT (self), query,
              self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->query (sink, query);
}

static GstFlowReturn
gst_d3d12_swapchain_sink_prepare (GstBaseSink * sink, GstBuffer * buffer)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (sink);
  auto priv = self->priv;

  auto pts = GST_BUFFER_PTS (buffer);
  if (GST_CLOCK_TIME_IS_VALID (pts)) {
    auto stream_time = gst_segment_to_stream_time (&sink->segment,
        GST_FORMAT_TIME, pts);
    if (GST_CLOCK_TIME_IS_VALID (stream_time))
      gst_object_sync_values (GST_OBJECT (self), stream_time);
  }

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!gst_d3d12_swapchain_sink_set_buffer (self, buffer, TRUE)) {
    GST_ERROR_OBJECT (self, "Set buffer failed");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_swapchain_sink_show_frame (GstVideoSink * sink, GstBuffer * buf)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (sink);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!gst_d3d12_swapchain_sink_set_buffer (self, buf, FALSE)) {
    GST_ERROR_OBJECT (self, "Set buffer failed");
    return GST_FLOW_ERROR;
  }

  auto hr = priv->swapchain->Present (0, 0);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Present failed");
    return GST_FLOW_ERROR;
  }

  /* To update fence value */
  gst_d3d12_cmd_queue_execute_command_lists (priv->cq,
      0, nullptr, &priv->fence_val);

  return GST_FLOW_OK;
}

static const GList *
gst_d3d12_swapchain_sink_color_balance_list_channels (GstColorBalance * iface)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (iface);
  auto priv = self->priv;

  return priv->color_balance_channels;
}

static void
gst_d3d12_swapchain_sink_color_balance_set_value (GstColorBalance * iface,
    GstColorBalanceChannel * channel, gint value)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (iface);
  auto priv = self->priv;
  gdouble new_val;

  g_return_if_fail (channel->label);

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
    gst_d3d12_swapchain_sink_update_color_balance (self,
        "HUE", &priv->hue, new_val);
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0;
    gst_d3d12_swapchain_sink_update_color_balance (self,
        "SATURATION", &priv->saturation, new_val);
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
    gst_d3d12_swapchain_sink_update_color_balance (self,
        "BRIGHTNESS", &priv->brightness, new_val);
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0;
    gst_d3d12_swapchain_sink_update_color_balance (self,
        "BRIGHTNESS", &priv->contrast, new_val);
  }
}

static gint
gst_d3d12_swapchain_sink_color_balance_get_value (GstColorBalance * iface,
    GstColorBalanceChannel * channel)
{
  auto self = GST_D3D12_SWAPCHAIN_SINK (iface);
  auto priv = self->priv;
  gint value = 0;

  g_return_val_if_fail (channel->label, 0);

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!g_ascii_strcasecmp (channel->label, "HUE")) {
      auto hue = priv->hue;
      value = static_cast < gint > ((hue + 1) * 2000.0 / 2.0 - 1000.0);
    } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
      auto saturation = priv->saturation;
      value = static_cast < gint > (saturation * 2000.0 / 2.0 - 1000.0);
    } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
      auto brightness = priv->brightness;
      value = static_cast < gint > ((brightness + 1) * 2000.0 / 2.0 - 1000.0);
    } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
      auto contrast = priv->contrast;
      value = static_cast < gint > (contrast * 2000.0 / 2.0 - 1000.0);
    }
  }

  return value;
}

static GstColorBalanceType
gst_d3d12_swapchain_sink_color_balance_get_balance_type (GstColorBalance *
    iface)
{
  return GST_COLOR_BALANCE_HARDWARE;
}

static void
gst_d3d12_swapchain_sink_color_balance_init (GstColorBalanceInterface * iface)
{
  iface->list_channels = gst_d3d12_swapchain_sink_color_balance_list_channels;
  iface->set_value = gst_d3d12_swapchain_sink_color_balance_set_value;
  iface->get_value = gst_d3d12_swapchain_sink_color_balance_get_value;
  iface->get_balance_type =
      gst_d3d12_swapchain_sink_color_balance_get_balance_type;
}
