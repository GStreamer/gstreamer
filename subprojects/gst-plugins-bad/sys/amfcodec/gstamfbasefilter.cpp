/* GStreamer
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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

#include "gstamfbasefilter.h"
#include "gstamfd3dcommon.h"

#include <core/Factory.h>
#include <string.h>

#include <gst/video/video-color.h>
#include <components/VideoDecoderUVD.h>

#ifdef G_OS_WIN32
#include "gst/d3d12/gstd3d12memory.h"
#include "gst/d3d12/gstd3d12_fwd.h"
#include <gst/d3d11/gstd3d11.h>
#include <wrl.h>
#elif defined HAVE_GST_VULKAN
#include <gst/vulkan/vulkan.h>
#include <core/VulkanAMF.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_amf_base_filter_debug);
#define GST_CAT_DEFAULT gst_amf_base_filter_debug

#ifdef G_OS_WIN32
/* Same GUID as in gstamfencoder.cpp – used to tag the texture array index
 * as D3D11 private data so AMF can retrieve it. */
static GUID AMFTextureArrayIndexGUID = { 0x28115527, 0xe7c3, 0x4b66, 0x99, 0xd3,
  0x4f, 0x2a, 0xe6, 0xb4, 0x7f, 0xaf
};
#endif

/* *INDENT-OFF* */
using namespace amf;
#ifdef G_OS_WIN32
using namespace Microsoft::WRL;
#endif
/* *INDENT-ON* */

struct _GstAmfBaseFilterPrivate
{
  guint device_index = 0;
#ifdef G_OS_WIN32
  /* See GstAmfApiState (gstamfutils.h). Dispatch reads api.active,
   * never api.configured. */
  GstAmfApiState api;
  GstAmfD3DPrivate d3d;
  /* D3D11/D3D12 pool of SHADER_RESOURCE|SHARED textures used when the
   * upstream texture cannot be fed directly to CreateSurfaceFromDX11Native
   * / CreateSurfaceFromDX12Native (wrong usage/bind flags or heap type, or
   * cross-device same-GPU scenario). */
  GstBufferPool *input_pool = nullptr;
#endif

  AMFContext *context = nullptr;
  AMFComponent *component = nullptr;
};

#define gst_amf_base_filter_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstAmfBaseFilter, gst_amf_base_filter,
    GST_TYPE_BASE_TRANSFORM);

static void gst_amf_base_filter_dispose (GObject * object);
static void gst_amf_base_filter_finalize (GObject * object);
static void gst_amf_base_filter_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_amf_base_filter_start (GstBaseTransform * trans);
static gboolean gst_amf_base_filter_stop (GstBaseTransform * trans);
static gboolean gst_amf_base_filter_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_amf_base_filter_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_amf_base_filter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static GstFlowReturn gst_amf_base_filter_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_amf_base_filter_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static gboolean gst_amf_base_filter_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_amf_base_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);

static gboolean gst_amf_base_filter_open_context (GstAmfBaseFilter * self);
static gboolean gst_amf_base_filter_close_context (GstAmfBaseFilter * self);
static gboolean gst_amf_base_filter_open_component (GstAmfBaseFilter * self);
static void gst_amf_base_filter_close_component (GstAmfBaseFilter * self);

static AMF_SURFACE_FORMAT
gst_amf_base_filter_video_format_to_amf (GstVideoFormat fmt);

static void
gst_amf_base_filter_class_init (GstAmfBaseFilterClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->dispose = gst_amf_base_filter_dispose;
  gobject_class->finalize = gst_amf_base_filter_finalize;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_amf_base_filter_set_context);

  /* Even if upstream and downstream caps look identical we still want
   * to push frames through AMF (the user may have set non-default
   * properties affecting color profile / sharpness / ...). The
   * concrete subclasses are free to flip this back to TRUE. */
  trans_class->passthrough_on_same_caps = FALSE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_amf_base_filter_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_amf_base_filter_stop);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_amf_base_filter_set_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_amf_base_filter_get_unit_size);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_amf_base_filter_query);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_amf_base_filter_transform);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_amf_base_filter_transform_meta);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_amf_base_filter_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_amf_base_filter_decide_allocation);

  GST_DEBUG_CATEGORY_INIT (gst_amf_base_filter_debug,
      "amfbasefilter", 0, "amf base filter");

  gst_type_mark_as_plugin_api (GST_TYPE_AMF_BASE_FILTER, (GstPluginAPIFlags) 0);
}

static void
gst_amf_base_filter_init (GstAmfBaseFilter * self)
{
  self->priv = new GstAmfBaseFilterPrivate ();
#ifdef G_OS_WIN32
  gst_amf_api_state_init (&self->priv->api);
  gst_amf_d3d_private_init (&self->priv->d3d);
#endif
  gst_video_info_init (&self->in_info);
  gst_video_info_init (&self->out_info);
}

static void
gst_amf_base_filter_dispose (GObject * object)
{
#ifdef G_OS_WIN32
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (object);
  GstAmfBaseFilterPrivate *priv = self->priv;
  gst_amf_d3d_private_clear (&priv->d3d);
#endif

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_amf_base_filter_finalize (GObject * object)
{
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (object);

  delete self->priv;
  self->priv = nullptr;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Set by registrators: which adapter / Vulkan device to bind to and what
 * the cdata-derived pad templates look like. Mirrors
 * gst_amf_encoder_set_subclass_data(). */
void
gst_amf_base_filter_set_subclass_data (GstAmfBaseFilter * filter,
    gint64 adapter_luid, guint device_index)
{
  g_return_if_fail (GST_IS_AMF_BASE_FILTER (filter));

#ifdef G_OS_WIN32
  filter->priv->d3d.adapter_luid = adapter_luid;
#endif
  filter->priv->device_index = device_index;
}

AMFContext *
gst_amf_base_filter_get_context (GstAmfBaseFilter * filter)
{
  g_return_val_if_fail (GST_IS_AMF_BASE_FILTER (filter), nullptr);

  return filter->priv->context;
}

#ifdef G_OS_WIN32
/* Returns the active D3D11 device, or NULL if the filter hasn't opened
 * one yet (or api is not d3d11). Not reffed. Windows only. */
GstD3D11Device *
gst_amf_base_filter_get_d3d11_device (GstAmfBaseFilter * filter)
{
  g_return_val_if_fail (GST_IS_AMF_BASE_FILTER (filter), nullptr);

  return filter->priv->d3d.d3d11_device;
}

#ifdef HAVE_GST_D3D12
/* Returns the active D3D12 device, or NULL if the filter hasn't opened
 * one yet (or api is not d3d12). Not reffed. Windows only. */
GstD3D12Device *
gst_amf_base_filter_get_d3d12_device (GstAmfBaseFilter * filter)
{
  g_return_val_if_fail (GST_IS_AMF_BASE_FILTER (filter), nullptr);

  return filter->priv->d3d.d3d12_device;
}
#endif

/* Concrete api in use -- never AUTO, regardless of api.configured (see
 * gst_amf_resolve_active_api()). */
GstAmfApi
gst_amf_base_filter_get_api (GstAmfBaseFilter * filter)
{
  g_return_val_if_fail (GST_IS_AMF_BASE_FILTER (filter),
      gst_amf_get_concrete_default_api ());

  return filter->priv->api.active;
}
#endif

static void
gst_amf_base_filter_set_context (GstElement * element, GstContext * context)
{
#ifdef G_OS_WIN32
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (element);
  GstAmfBaseFilterPrivate *priv = self->priv;

  gst_amf_d3d_set_context (element, context, priv->api.active, &priv->d3d);
#endif

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

#ifdef G_OS_WIN32
static gboolean
gst_amf_base_filter_open_context (GstAmfBaseFilter * self)
{
  GstAmfBaseFilterPrivate *priv = self->priv;
  gpointer context = nullptr;

  if (!gst_amf_context_new (GST_ELEMENT (self), &context))
    return FALSE;

  priv->context = (AMFContext *) context;

  if (!gst_amf_d3d_open_context (GST_ELEMENT (self), priv->api.configured,
          &priv->d3d, priv->context)) {
    priv->context->Release ();
    priv->context = nullptr;
    return FALSE;
  }
  return TRUE;
}
#else /* G_OS_WIN32 */
static gboolean
gst_amf_base_filter_open_context (GstAmfBaseFilter * self)
{
  GstAmfBaseFilterPrivate *priv = self->priv;
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMF_RESULT result;

  if (!factory) {
    GST_ERROR_OBJECT (self, "AMF factory is not available");
    return FALSE;
  }

  result = factory->CreateContext (&priv->context);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to create AMF context, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return FALSE;
  }

  result = AMFContext1Ptr (priv->context)->InitVulkan (NULL);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to init AMF Vulkan context, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    priv->context->Release ();
    priv->context = nullptr;
    return FALSE;
  }

  return TRUE;
}
#endif /* G_OS_WIN32 */

static gboolean
gst_amf_base_filter_close_context (GstAmfBaseFilter * self)
{
  GstAmfBaseFilterPrivate *priv = self->priv;

  gst_amf_base_filter_close_component (self);

  if (priv->context) {
    priv->context->Terminate ();
    priv->context->Release ();
    priv->context = nullptr;
  }

#ifdef G_OS_WIN32
  if (priv->input_pool) {
    gst_buffer_pool_set_active (priv->input_pool, FALSE);
    gst_clear_object (&priv->input_pool);
  }
  gst_amf_d3d_private_clear (&priv->d3d);
#endif

  return TRUE;
}

static void
gst_amf_base_filter_close_component (GstAmfBaseFilter * self)
{
  GstAmfBaseFilterPrivate *priv = self->priv;

  if (priv->component) {
    priv->component->Terminate ();
    priv->component->Release ();
    priv->component = nullptr;
  }
}

static gboolean
gst_amf_base_filter_open_component (GstAmfBaseFilter * self)
{
  GstAmfBaseFilterPrivate *priv = self->priv;
  GstAmfBaseFilterClass *klass = GST_AMF_BASE_FILTER_GET_CLASS (self);
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMFComponentPtr comp;
  AMF_RESULT result;
  const wchar_t *component_id;
  AMF_SURFACE_FORMAT in_amf_fmt;

  if (!klass->get_component_id || !klass->configure_component) {
    GST_ERROR_OBJECT (self, "Subclass did not implement required vmethods");
    return FALSE;
  }

  gst_amf_base_filter_close_component (self);

  if (!priv->context) {
    GST_ERROR_OBJECT (self, "AMF context is not initialized");
    return FALSE;
  }

  in_amf_fmt =
      gst_amf_base_filter_video_format_to_amf (GST_VIDEO_INFO_FORMAT
      (&self->in_info));
  if (in_amf_fmt == AMF_SURFACE_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Unsupported input format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&self->in_info)));
    return FALSE;
  }

  component_id = klass->get_component_id (self);
  if (!component_id) {
    GST_ERROR_OBJECT (self, "Subclass returned no component id");
    return FALSE;
  }

  result = factory->CreateComponent (priv->context, component_id, &comp);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "Failed to create AMF component, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return FALSE;
  }

  if (!klass->configure_component (self, comp.GetPtr (), &self->in_info,
          &self->out_info)) {
    GST_ERROR_OBJECT (self, "Subclass failed to configure component");
    return FALSE;
  }

  result = comp->Init (in_amf_fmt, GST_VIDEO_INFO_WIDTH (&self->in_info),
      GST_VIDEO_INFO_HEIGHT (&self->in_info));
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "AMF component Init() failed, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return FALSE;
  }

  priv->component = comp.Detach ();

  GST_INFO_OBJECT (self, "AMF component initialized: %dx%d -> %dx%d",
      GST_VIDEO_INFO_WIDTH (&self->in_info),
      GST_VIDEO_INFO_HEIGHT (&self->in_info),
      GST_VIDEO_INFO_WIDTH (&self->out_info),
      GST_VIDEO_INFO_HEIGHT (&self->out_info));

  return TRUE;
}

static gboolean
gst_amf_base_filter_start (GstBaseTransform * trans)
{
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (trans);

  if (!gst_amf_base_filter_open_context (self)) {
    GST_ERROR_OBJECT (self, "Failed to open AMF context");
    gst_amf_base_filter_close_context (self);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_amf_base_filter_stop (GstBaseTransform * trans)
{
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (trans);

  gst_amf_base_filter_close_context (self);

  return TRUE;
}

static gboolean
gst_amf_base_filter_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (trans);
  GstAmfBaseFilterClass *klass = GST_AMF_BASE_FILTER_GET_CLASS (self);
  GstVideoInfo in_info, out_info;

  if (!gst_video_info_from_caps (&in_info, incaps)) {
    GST_ERROR_OBJECT (self, "invalid input caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }
  if (!gst_video_info_from_caps (&out_info, outcaps)) {
    GST_ERROR_OBJECT (self, "invalid output caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  if (klass->validate_caps && !klass->validate_caps (self, &in_info, &out_info)) {
    GST_ERROR_OBJECT (self, "subclass refused caps");
    return FALSE;
  }

  self->in_info = in_info;
  self->out_info = out_info;

#ifdef G_OS_WIN32
  GstAmfBaseFilterPrivate *priv = self->priv;

  if (priv->api.configured == GST_AMF_API_AUTO) {
    /* start() already opened both backends against a shared AMFContext
     * so GST_QUERY_CONTEXT could be answered for either before caps were
     * known. Resolve which one is actually in use now that they are.
     * Both priv->d3d.d3d11_device/d3d12_device stay open regardless. */
    gst_amf_resolve_active_api_from_caps (&priv->api, incaps);
  }
#endif

  if (!gst_amf_base_filter_open_component (self))
    return FALSE;

#ifdef G_OS_WIN32
  if (priv->input_pool) {
    gst_buffer_pool_set_active (priv->input_pool, FALSE);
    gst_clear_object (&priv->input_pool);
  }

#ifdef HAVE_GST_D3D12
  if (priv->api.active == GST_AMF_API_D3D12 && priv->d3d.d3d12_device) {
    /* Same flag pairing as propose_allocation's D3D12 branch
     * (ALLOW_SIMULTANEOUS_ACCESS + HEAP_FLAG_SHARED). */
    priv->input_pool = gst_amf_d3d12_pool_new (priv->d3d.d3d12_device,
        incaps, &in_info, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
        D3D12_HEAP_FLAG_SHARED, nullptr, TRUE);
    if (!priv->input_pool) {
      GST_WARNING_OBJECT (self, "Failed to set up D3D12 input pool; "
          "cross-device / wrong-heap inputs will fall back to host copy");
    }
  } else
#endif
  if (priv->d3d.d3d11_device) {
    priv->input_pool = gst_amf_d3d11_pool_new (priv->d3d.d3d11_device,
        incaps, &in_info, D3D11_BIND_SHADER_RESOURCE,
        D3D11_RESOURCE_MISC_SHARED, nullptr, TRUE);
    if (!priv->input_pool) {
      GST_WARNING_OBJECT (self, "Failed to set up D3D11 input pool; "
          "cross-device / wrong-usage inputs will fall back to host copy");
    }
  }
#endif

  return TRUE;
}

static gboolean
gst_amf_base_filter_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  *size = GST_VIDEO_INFO_SIZE (&info);
  return TRUE;
}

static gboolean
gst_amf_base_filter_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
#ifdef G_OS_WIN32
      GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (trans);
      GstAmfBaseFilterPrivate *priv = self->priv;
      if (gst_amf_d3d_handle_context_query (GST_ELEMENT (self), query,
              &priv->d3d)) {
        return TRUE;
      }
#endif
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static gboolean
gst_amf_base_filter_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  const GstMetaInfo *info = meta->info;
  const gchar *const *tags;

  /* Forward video-tagged metas (e.g. crop) and drop everything else,
   * mirroring what gstd3d11basefilter does. */
  tags = gst_meta_api_type_get_tags (info->api);
  if (tags && g_strv_length ((gchar **) tags) == 1
      && gst_meta_api_type_has_tag (info->api,
          g_quark_from_static_string (GST_META_TAG_VIDEO_STR))) {
    return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans, outbuf,
      meta, inbuf);
}

/* ============================================================
 *   Allocation queries
 * ============================================================ */

static gboolean
gst_amf_base_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
#ifdef G_OS_WIN32
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (trans);
  GstAmfBaseFilterPrivate *priv = self->priv;
  GstCaps *caps;
  GstVideoInfo info;
  GstCapsFeatures *features;
  GstBufferPool *pool;
  guint size;

  gst_query_parse_allocation (query, &caps, nullptr);
  if (!caps || !gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "No caps in allocation query");
    return FALSE;
  }

  features = gst_caps_get_features (caps, 0);

#ifdef HAVE_GST_D3D12
  if (priv->api.active == GST_AMF_API_D3D12 && priv->d3d.d3d12_device
      && features
      && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
    /* Mirrors set_caps's D3D12 input pool flags: SRV usability is
     * implicit in D3D12 (no BIND_SHADER_RESOURCE equivalent needed),
     * ALLOW_SIMULTANEOUS_ACCESS is for the CPU-staging map path,
     * HEAP_FLAG_SHARED mirrors D3D11_RESOURCE_MISC_SHARED. */
    pool = gst_amf_d3d12_pool_new (priv->d3d.d3d12_device, caps, &info,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_HEAP_FLAG_SHARED,
        &size, FALSE);
    if (!pool)
      return FALSE;

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
    gst_object_unref (pool);
    return TRUE;
  }
#endif

  if (priv->d3d.d3d11_device && features
      && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    pool = gst_amf_d3d11_pool_new (priv->d3d.d3d11_device, caps, &info,
        D3D11_BIND_SHADER_RESOURCE, 0, &size, FALSE);
    if (!pool)
      return FALSE;

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
    gst_object_unref (pool);
    return TRUE;
  }
#endif

  return GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);
}

static gboolean
gst_amf_base_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
#ifdef G_OS_WIN32
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (trans);
  GstAmfBaseFilterPrivate *priv = self->priv;
  GstCaps *outcaps;
  GstVideoInfo out_info;
  GstCapsFeatures *features;
  gboolean pool_set = FALSE;

  gst_query_parse_allocation (query, &outcaps, nullptr);
#ifdef HAVE_GST_D3D12
  if (priv->api.active == GST_AMF_API_D3D12 && priv->d3d.d3d12_device && outcaps
      && gst_video_info_from_caps (&out_info, outcaps)) {
    features = gst_caps_get_features (outcaps, 0);
    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
      GstBufferPool *pool;
      guint size;

      /* Not activated here: GstBaseTransform's default decide_allocation
       * (called below) reconfigures whatever pool is in the query via
       * gst_buffer_pool_set_config(), which fails on an already-active
       * pool and silently falls back to a plain system-memory pool. */
      pool = gst_amf_d3d12_pool_new (priv->d3d.d3d12_device, outcaps,
          &out_info, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS
          | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_HEAP_FLAG_SHARED,
          &size, FALSE);
      if (pool) {
        if (gst_query_get_n_allocation_pools (query) > 0)
          gst_query_set_nth_allocation_pool (query, 0, pool, size, 0, 0);
        else
          gst_query_add_allocation_pool (query, pool, size, 0, 0);

        gst_object_unref (pool);
        pool_set = TRUE;
      }
    }
  }
#endif

  if (!pool_set && priv->d3d.d3d11_device && outcaps
      && gst_video_info_from_caps (&out_info, outcaps)) {
    features = gst_caps_get_features (outcaps, 0);
    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
      GstBufferPool *pool;
      guint size;

      /* Not activated here; see the D3D12 branch above for why. */
      pool = gst_amf_d3d11_pool_new (priv->d3d.d3d11_device, outcaps,
          &out_info, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
          0, &size, FALSE);
      if (pool) {
        if (gst_query_get_n_allocation_pools (query) > 0)
          gst_query_set_nth_allocation_pool (query, 0, pool, size, 0, 0);
        else
          gst_query_add_allocation_pool (query, pool, size, 0, 0);

        gst_object_unref (pool);
      }
    }
  }
#endif

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

#ifdef G_OS_WIN32

/* ============================================================
 *   D3D11/D3D12 zero-copy helpers
 * ============================================================ */

#ifdef HAVE_GST_D3D12
static void
gst_amf_surface_ptr_free (gpointer data)
{
  delete (AMFSurfacePtr *) data;
}

/* D3D12 sibling of copy_amf_to_d3d11. Unlike D3D11, this batches all
 * planes' CopyTextureRegion into one command list with a single
 * Close/Execute (via gst_amf_d3d12_submit_copy_regions()), since D3D12 has
 * no per-call immediate context to piggyback on. */
static gboolean
gst_amf_base_filter_copy_amf_to_d3d12 (GstAmfBaseFilter * self,
    AMFSurface * surface, GstBuffer * outbuf)
{
  GstAmfBaseFilterPrivate *priv = self->priv;
  GstMemory *dst_mem;
  GstD3D12Memory *d3d_mem;
  amf_size num_planes;
  GstD3D12Frame frame;
  gboolean frame_mapped = FALSE;
  GstAmfD3D12CopyRegion regions[GST_VIDEO_MAX_PLANES];
  ComPtr < ID3D12Fence > wait_fences[GST_VIDEO_MAX_PLANES];
  ID3D12Fence *wait_fence_ptrs[GST_VIDEO_MAX_PLANES];
  guint64 wait_values[GST_VIDEO_MAX_PLANES];
  guint num_wait = 0;
  ID3D12Resource *prev_src_tex = nullptr;
  guint src_subresource = 0;
  gboolean ret = FALSE;

  dst_mem = gst_buffer_peek_memory (outbuf, 0);
  if (!gst_is_d3d12_memory (dst_mem))
    return FALSE;
  d3d_mem = GST_D3D12_MEMORY_CAST (dst_mem);
  if (!gst_d3d12_device_is_equal (d3d_mem->device, priv->d3d.d3d12_device))
    return FALSE;

  if (surface->GetMemoryType () != AMF_MEMORY_DX12) {
    GST_WARNING_OBJECT (self, "AMF surface is not DX12");
    if (surface->Convert (AMF_MEMORY_DX12) != AMF_OK) {
      GST_WARNING_OBJECT (self, "AMF surface Convert(DX12) failed");
      return FALSE;
    }
  }

  num_planes = surface->GetPlanesCount ();
  if (num_planes == 0 || num_planes > GST_VIDEO_MAX_PLANES)
    return FALSE;

  if (!gst_d3d12_frame_map (&frame, &self->out_info, outbuf,
          GST_MAP_WRITE_D3D12, GST_D3D12_FRAME_MAP_FLAG_NONE)) {
    GST_LOG_OBJECT (self, "D3D12 copy failed, couldn't map output buffer, "
        "falling back to host copy");
    return FALSE;
  }

  if (GST_VIDEO_INFO_N_PLANES (&frame.info) != num_planes) {
    GST_LOG_OBJECT (self, "D3D12 buffer has %u planes but AMF surface has %"
        G_GSIZE_FORMAT " planes, falling back to host copy",
        GST_VIDEO_INFO_N_PLANES (&frame.info), num_planes);
    goto out;
  }

  /* gst_d3d12_frame_map() already flattens the output buffer's own
   * per-memory subresource packing into one entry per AMF plane in
   * frame.data/subresource_index/plane_rect. */
  for (guint i = 0; i < num_planes; i++) {
    AMFPlane *plane = surface->GetPlaneAt (i);
    ID3D12Resource *src_tex;
    D3D12_RESOURCE_DESC src_desc;

    if (!plane) {
      GST_LOG_OBJECT (self, "D3D12 copy failed, missing plane %u, "
          "falling back to host copy", i);
      goto out;
    }

    src_tex = (ID3D12Resource *) plane->GetNative ();
    if (!src_tex) {
      GST_LOG_OBJECT (self, "D3D12 copy failed, no native resource for "
          "plane %u, falling back to host copy", i);
      goto out;
    }

    /* For a planar format like NV12, AMF packs every logical plane into
     * subresources of the *same* native ID3D12Resource (confirmed: plane 0
     * and plane 1's GetNative() return an identical pointer) rather than
     * handing back one single-subresource resource per plane -- so the
     * source subresource has to track how many consecutive planes share
     * the same resource, not just always be 0. */
    if (src_tex == prev_src_tex) {
      src_subresource++;
    } else {
      src_subresource = 0;
      prev_src_tex = src_tex;
    }
    {
      ID3D12Fence *plane_fence = nullptr;
      guint64 plane_value = 0;

      if (gst_amf_d3d12_get_resource_amf_fence (src_tex, &plane_fence,
              &plane_value)) {
        wait_fences[num_wait].Attach (plane_fence);
        wait_fence_ptrs[num_wait] = plane_fence;
        wait_values[num_wait] = plane_value;
        num_wait++;
      }
    }

    src_desc = src_tex->GetDesc ();

    regions[i].dst_resource = frame.data[i];
    regions[i].dst_subresource = frame.subresource_index[i];
    regions[i].src_resource = src_tex;
    regions[i].src_subresource = src_subresource;
    regions[i].src_box.left = 0;
    regions[i].src_box.top = 0;
    regions[i].src_box.front = 0;
    regions[i].src_box.right = (UINT) MIN (src_desc.Width,
        (UINT) (frame.plane_rect[i].right - frame.plane_rect[i].left));
    regions[i].src_box.bottom = (UINT) MIN (src_desc.Height,
        (UINT) (frame.plane_rect[i].bottom - frame.plane_rect[i].top));
    regions[i].src_box.back = 1;
  }
  frame_mapped = TRUE;

  {
    ComPtr < ID3D12Fence > fence;
    guint64 fence_value = 0;
    /* Keeps the AMF surface (and hence the native resources
     * regions[].src_resource point into) alive until the GPU copy is done,
     * so AMF can't recycle/overwrite it out from under an in-flight read. */
    AMFSurfacePtr *src_surface = new AMFSurfacePtr (surface);

    /* Async -- doesn't block for the copy to finish. Attach the completion
     * fence to outbuf so downstream D3D12-fence-aware consumers wait on it
     * via gst_d3d12_memory_get_fence(). */
    if (!gst_amf_d3d12_submit_copy_regions (GST_ELEMENT (self), &priv->d3d,
            regions, (guint) num_planes, wait_fence_ptrs, wait_values,
            num_wait, src_surface, gst_amf_surface_ptr_free, &fence,
            &fence_value)) {
      goto out;
    }

    gst_d3d12_buffer_set_fence (outbuf, fence.Get (), fence_value, FALSE);
  }

  ret = TRUE;

out:
  if (frame_mapped)
    gst_d3d12_frame_unmap (&frame);

  if (!ret) {
    GST_LOG_OBJECT (self, "D3D12 copy failed, falling back to host copy "
        "(AMF format %d, memory type %d, planes %" G_GSIZE_FORMAT ", "
        "buffer memories %u, output %s)", (gint) surface->GetFormat (),
        (gint) surface->GetMemoryType (), num_planes,
        gst_buffer_n_memory (outbuf),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&self->out_info)));
  }

  return ret;
}
#endif /* HAVE_GST_D3D12 */

/* GPU-copy one AMF DX11 plane into one GstD3D11Memory via
 * CopySubresourceRegion.  @ctx must already be valid. */
static gboolean
gst_amf_base_filter_copy_amf_plane_to_d3d11 (ID3D11DeviceContext * ctx,
    AMFPlane * plane, GstMemory * dst_mem)
{
  GstMapInfo dst_info;
  GstD3D11Memory *d3d_mem;
  ID3D11Texture2D *src_tex, *dst_tex;
  D3D11_TEXTURE2D_DESC src_desc, dst_desc;
  D3D11_BOX src_box;
  guint subresource_idx;

  if (!plane)
    return FALSE;

  src_tex = (ID3D11Texture2D *) plane->GetNative ();
  if (!src_tex)
    return FALSE;

  d3d_mem = GST_D3D11_MEMORY_CAST (dst_mem);

  if (!gst_memory_map (dst_mem, &dst_info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11)))
    return FALSE;

  dst_tex = (ID3D11Texture2D *) dst_info.data;
  subresource_idx = gst_d3d11_memory_get_subresource_index (d3d_mem);

  gst_d3d11_memory_get_texture_desc (d3d_mem, &dst_desc);
  src_tex->GetDesc (&src_desc);

  src_box.left = 0;
  src_box.top = 0;
  src_box.front = 0;
  src_box.back = 1;
  src_box.right = MIN (src_desc.Width, dst_desc.Width);
  src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

  ctx->CopySubresourceRegion (dst_tex, subresource_idx, 0, 0, 0,
      src_tex, 0, &src_box);

  gst_memory_unmap (dst_mem, &dst_info);

  return TRUE;
}

/* Copy the AMF output surface (which must be in DX11 memory, i.e.
 * AMF_MEMORY_DX11) into the pre-allocated D3D11 output GstBuffer via
 * a GPU CopySubresourceRegion.  Returns FALSE if outbuf is not a
 * D3D11 buffer on the same device (caller falls back to host copy). */
static gboolean
gst_amf_base_filter_copy_amf_to_d3d11 (GstAmfBaseFilter * self,
    AMFSurface * surface, GstBuffer * outbuf)
{
  GstAmfBaseFilterPrivate *priv = self->priv;
  GstMemory *dst_mem;
  GstD3D11Memory *d3d_mem;
  ID3D11DeviceContext *ctx;
  amf_size num_planes;
  guint n_mem;

  dst_mem = gst_buffer_peek_memory (outbuf, 0);
  if (!gst_is_d3d11_memory (dst_mem))
    return FALSE;
  d3d_mem = GST_D3D11_MEMORY_CAST (dst_mem);
  if (d3d_mem->device != priv->d3d.d3d11_device)
    return FALSE;

  /* Ensure the AMF surface is accessible via DX11. */
  if (surface->GetMemoryType () != AMF_MEMORY_DX11) {
    GST_WARNING_OBJECT (self, "AMF surface is not DX11");
    if (surface->Convert (AMF_MEMORY_DX11) != AMF_OK) {
      GST_WARNING_OBJECT (self, "AMF surface Convert(DX11) failed");
      return FALSE;
    }
  }

  n_mem = gst_buffer_n_memory (outbuf);
  if (n_mem == 0)
    return FALSE;

  num_planes = surface->GetPlanesCount ();

  if (n_mem > 1 && n_mem != num_planes) {
    GST_LOG_OBJECT (self, "D3D11 buffer has %u memories but AMF surface has %"
        G_GSIZE_FORMAT " planes, falling back to host copy", n_mem, num_planes);
    return FALSE;
  }

  for (guint i = 0; i < n_mem; i++) {
    if (!gst_is_d3d11_memory (gst_buffer_peek_memory (outbuf, i)))
      return FALSE;
  }

  ctx = gst_d3d11_device_get_device_context_handle (priv->d3d.d3d11_device);
  gst_d3d11_device_lock (priv->d3d.d3d11_device);

  /* Native DXGI outputs (NV12, BGRA, ...): one D3D11 texture, but AMF
   * may still report multiple logical planes.  AMD documents that the
   * ID3D11Texture2D lives in GetPlaneAt(0)->GetNative() for these. */
  for (guint i = 0; i < n_mem; i++) {
    AMFPlane *plane = surface->GetPlaneAt (i);
    dst_mem = gst_buffer_peek_memory (outbuf, i);

    if (!gst_amf_base_filter_copy_amf_plane_to_d3d11 (ctx, plane, dst_mem)) {
      gst_d3d11_device_unlock (priv->d3d.d3d11_device);
      GST_LOG_OBJECT (self, "D3D11 copy failed, falling back to host copy "
          "(AMF format %d, memory type %d, planes %" G_GSIZE_FORMAT ", "
          "buffer memories %u, output %s)", (gint) surface->GetFormat (),
          (gint) surface->GetMemoryType (), num_planes, n_mem,
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&self->out_info)));
      return FALSE;
    }
  }

  if (!priv->d3d.d3d11_fence)
    priv->d3d.d3d11_fence =
        gst_d3d11_device_create_fence (priv->d3d.d3d11_device);
  if (priv->d3d.d3d11_fence) {
    gst_d3d11_fence_signal (priv->d3d.d3d11_fence);
    gst_d3d11_fence_wait (priv->d3d.d3d11_fence);
  }
  gst_d3d11_device_unlock (priv->d3d.d3d11_device);

  return TRUE;
}

#endif /* G_OS_WIN32 */

/* ============================================================
 *   Surface conversion (system memory <-> AMF surfaces)
 * ============================================================ */

static AMF_SURFACE_FORMAT
gst_amf_base_filter_video_format_to_amf (GstVideoFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_NV12:
      return AMF_SURFACE_NV12;
    case GST_VIDEO_FORMAT_P010_10LE:
      return AMF_SURFACE_P010;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      return AMF_SURFACE_BGRA;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      return AMF_SURFACE_RGBA;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      return AMF_SURFACE_YUV420P;
    case GST_VIDEO_FORMAT_YUY2:
      return AMF_SURFACE_YUY2;
    case GST_VIDEO_FORMAT_GRAY8:
      return AMF_SURFACE_GRAY8;
    default:
      break;
  }
  return AMF_SURFACE_UNKNOWN;
}

static AMFSurfacePtr
gst_amf_base_filter_alloc_input_surface (GstAmfBaseFilter * self,
    GstBuffer * inbuf)
{
  GstAmfBaseFilterPrivate *priv = self->priv;
  AMFSurfacePtr surface;
  GstVideoFrame frame;
  AMF_SURFACE_FORMAT amf_fmt;
  AMF_RESULT result;

  amf_fmt =
      gst_amf_base_filter_video_format_to_amf (GST_VIDEO_INFO_FORMAT
      (&self->in_info));
  if (amf_fmt == AMF_SURFACE_UNKNOWN)
    return nullptr;

  result = priv->context->AllocSurface (AMF_MEMORY_HOST, amf_fmt,
      GST_VIDEO_INFO_WIDTH (&self->in_info),
      GST_VIDEO_INFO_HEIGHT (&self->in_info), &surface);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self, "AllocSurface(HOST) failed, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return nullptr;
  }

  if (!gst_video_frame_map (&frame, &self->in_info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return nullptr;
  }

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&frame); i++) {
    AMFPlane *plane = surface->GetPlaneAt (i);
    guint8 *src_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, i);
    guint8 *dst_data = (guint8 *) plane->GetNative ();
    guint src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, i);
    guint dst_stride = (guint) plane->GetHPitch ();
    guint width_in_bytes = GST_VIDEO_FRAME_COMP_WIDTH (&frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&frame, i);
    guint height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, i);
    guint copy_bytes = MIN (src_stride, dst_stride);

    copy_bytes = MIN (copy_bytes, width_in_bytes);
    for (guint j = 0; j < height; j++) {
      memcpy (dst_data + j * dst_stride, src_data + j * src_stride, copy_bytes);
    }
  }

  gst_video_frame_unmap (&frame);

  return surface;
}

static gboolean
gst_amf_base_filter_copy_surface_to_buffer (GstAmfBaseFilter * self,
    AMFSurface * surface, GstBuffer * outbuf)
{
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &self->out_info, outbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    return FALSE;
  }

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&frame); i++) {
    AMFPlane *plane = surface->GetPlaneAt (i);
    if (!plane) {
      GST_ERROR_OBJECT (self,
          "AMF surface has fewer planes than expected (plane %u missing)", i);
      gst_video_frame_unmap (&frame);
      return FALSE;
    }

    guint8 *dst_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, i);
    guint8 *src_data = (guint8 *) plane->GetNative ();
    guint dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, i);
    guint src_stride = (guint) plane->GetHPitch ();
    guint width_in_bytes = GST_VIDEO_FRAME_COMP_WIDTH (&frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&frame, i);
    guint height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, i);
    guint copy_bytes = MIN (src_stride, dst_stride);

    copy_bytes = MIN (copy_bytes, width_in_bytes);
    for (guint j = 0; j < height; j++) {
      memcpy (dst_data + j * dst_stride, src_data + j * src_stride, copy_bytes);
    }
  }

  gst_video_frame_unmap (&frame);
  return TRUE;
}

static GstFlowReturn
gst_amf_base_filter_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstAmfBaseFilter *self = GST_AMF_BASE_FILTER (trans);
  GstAmfBaseFilterPrivate *priv = self->priv;
  AMFSurfacePtr in_surface;
  AMFDataPtr out_data;
  AMFSurfacePtr out_surface;
  AMF_RESULT result;
#ifdef G_OS_WIN32
  /* Kept alive while AMF holds a reference to the input D3D11/D3D12
   * texture. */
  GstBuffer *d3d11_input_buf = nullptr;
  GstMapInfo d3d11_input_map = { };
  gboolean d3d11_input_mapped = FALSE;
#ifdef HAVE_GST_D3D12
  GstBuffer *d3d12_input_buf = nullptr;
  GstMapInfo d3d12_input_map = { };
  gboolean d3d12_input_mapped = FALSE;
#endif
#endif

  if (!priv->component) {
    GST_ERROR_OBJECT (self, "AMF component is not initialized");
    return GST_FLOW_ERROR;
  }

#ifdef HAVE_GST_D3D12
  if (priv->api.active == GST_AMF_API_D3D12) {
    /* --- D3D12 zero-copy input path ---
     * Mirrors the D3D11 path below: hand the resource directly to AMF via
     * CreateSurfaceFromDX12Native when possible, avoiding a CPU copy. The
     * resource must stay mapped until QueryOutput returns. */
    if (gst_amf_d3d12_get_input_buffer (GST_ELEMENT (self), &priv->d3d,
            priv->context, &self->in_info, priv->input_pool, inbuf,
            &d3d12_input_buf)) {
      GstMemory *mem = gst_buffer_peek_memory (d3d12_input_buf, 0);

      if (gst_memory_map (mem, &d3d12_input_map, GST_MAP_READ_D3D12)) {
        ID3D12Resource *resource = (ID3D12Resource *) d3d12_input_map.data;
        d3d12_input_mapped = TRUE;

        result =
            AMFContext2Ptr (priv->
            context)->CreateSurfaceFromDX12Native (resource, &in_surface,
            nullptr);

        if (result != AMF_OK) {
          GST_WARNING_OBJECT (self,
              "CreateSurfaceFromDX12Native failed (%" GST_AMF_RESULT_FORMAT
              "), falling back to host copy", GST_AMF_RESULT_ARGS (result));
          in_surface = nullptr;
        }
      }
    }

    if (!in_surface) {
      /* Release D3D12 resources and fall back to host copy below. */
      if (d3d12_input_mapped) {
        gst_memory_unmap (gst_buffer_peek_memory (d3d12_input_buf, 0),
            &d3d12_input_map);
        d3d12_input_mapped = FALSE;
      }
      gst_clear_buffer (&d3d12_input_buf);
    }
  } else
#endif /* HAVE_GST_D3D12 */
#ifdef G_OS_WIN32
    /* --- D3D11 zero-copy input path ---
     * When the upstream provides a D3D11 texture we can hand it directly
     * to AMF via CreateSurfaceFromDX11Native, avoiding any CPU memcpy.
     * The texture must remain mapped (i.e. the GstD3D11Memory must stay
     * "opened" for D3D11 access) until QueryOutput returns, because AMF
     * reads the texture during GPU processing. */
  if (gst_amf_d3d11_get_input_buffer (GST_ELEMENT (self), &priv->d3d,
          priv->input_pool, inbuf, &d3d11_input_buf)) {
    GstMemory *mem = gst_buffer_peek_memory (d3d11_input_buf, 0);
    GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
    guint subresource_idx = gst_d3d11_memory_get_subresource_index (dmem);

    if (gst_memory_map (mem, &d3d11_input_map,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
      ID3D11Texture2D *texture = (ID3D11Texture2D *) d3d11_input_map.data;
      d3d11_input_mapped = TRUE;

      gst_d3d11_device_lock (priv->d3d.d3d11_device);
      texture->SetPrivateData (AMFTextureArrayIndexGUID, sizeof (guint),
          &subresource_idx);
      result = priv->context->CreateSurfaceFromDX11Native (texture,
          &in_surface, nullptr);
      gst_d3d11_device_unlock (priv->d3d.d3d11_device);

      if (result != AMF_OK) {
        GST_WARNING_OBJECT (self,
            "CreateSurfaceFromDX11Native failed (%" GST_AMF_RESULT_FORMAT
            "), falling back to host copy", GST_AMF_RESULT_ARGS (result));
        in_surface = nullptr;
      }
    }
  }

  if (!in_surface) {
    /* Release D3D11 resources and fall back to host copy below. */
    if (d3d11_input_mapped) {
      gst_memory_unmap (gst_buffer_peek_memory (d3d11_input_buf, 0),
          &d3d11_input_map);
      d3d11_input_mapped = FALSE;
    }
    gst_clear_buffer (&d3d11_input_buf);
  }
#endif /* G_OS_WIN32 */

  if (!in_surface) {
    /* Linux Vulkan path (and Windows fallback): copy through host memory.
     * AMF_MEMORY_HOST surfaces work fine; AMF transfers to GPU internally. */
    in_surface = gst_amf_base_filter_alloc_input_surface (self, inbuf);
    if (!in_surface) {
      GST_ERROR_OBJECT (self, "Failed to build AMF input surface");
      return GST_FLOW_ERROR;
    }
  }

  if (GST_BUFFER_PTS_IS_VALID (inbuf))
    in_surface->SetPts (GST_BUFFER_PTS (inbuf) / 100);
  if (GST_BUFFER_DURATION_IS_VALID (inbuf))
    in_surface->SetDuration (GST_BUFFER_DURATION (inbuf) / 100);
  in_surface->SetCrop (0, 0, GST_VIDEO_INFO_WIDTH (&self->in_info),
      GST_VIDEO_INFO_HEIGHT (&self->in_info));

  result = priv->component->SubmitInput (in_surface);
  if (result != AMF_OK && result != AMF_NEED_MORE_INPUT) {
    GST_ERROR_OBJECT (self, "SubmitInput failed, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
#ifdef G_OS_WIN32
    goto error;
#else
    return GST_FLOW_ERROR;
#endif
  }

  /* The AMF VPP-style components are 1-in/1-out, so a poll loop is
   * enough; we do not need a queue of pending PTS like the encoder. */
  for (int retries = 0; retries < 100; retries++) {
    result = priv->component->QueryOutput (&out_data);
    if (result == AMF_OK)
      break;
    if (result != AMF_REPEAT)
      break;
    g_usleep (1000);
  }

#ifdef G_OS_WIN32
  /* AMF has finished reading the input texture; release the D3D11/D3D12
   * map. */
#ifdef HAVE_GST_D3D12
  if (d3d12_input_mapped) {
    gst_memory_unmap (gst_buffer_peek_memory (d3d12_input_buf, 0),
        &d3d12_input_map);
    d3d12_input_mapped = FALSE;
  }
  gst_clear_buffer (&d3d12_input_buf);
#endif
  if (d3d11_input_mapped) {
    gst_memory_unmap (gst_buffer_peek_memory (d3d11_input_buf, 0),
        &d3d11_input_map);
    d3d11_input_mapped = FALSE;
  }
  gst_clear_buffer (&d3d11_input_buf);
#endif

  if (result != AMF_OK || out_data == nullptr) {
    GST_ERROR_OBJECT (self, "QueryOutput failed or empty, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return GST_FLOW_ERROR;
  }

  out_surface = AMFSurfacePtr (out_data);
  if (!out_surface) {
    GST_ERROR_OBJECT (self, "AMF output is not a surface");
    return GST_FLOW_ERROR;
  }

#ifdef HAVE_GST_D3D12
  /* --- D3D12 zero-copy output path ---
   * GPU-copy the AMF output surface into the pre-allocated D3D12 output
   * buffer.  Falls through to host copy when outbuf is not a D3D12
   * buffer on the same device. */
  if (priv->api.active == GST_AMF_API_D3D12
      && gst_amf_base_filter_copy_amf_to_d3d12 (self, out_surface, outbuf))
    goto output_done;
#endif

#ifdef G_OS_WIN32
  /* --- D3D11 zero-copy output path ---
   * GPU-copy the AMF output surface into the pre-allocated D3D11 output
   * buffer via CopySubresourceRegion.  No CPU readback involved.
   * Falls through to host copy when outbuf is not a D3D11 buffer on
   * the same device (e.g. downstream does not negotiate D3D11 caps). */
  if (gst_amf_base_filter_copy_amf_to_d3d11 (self, out_surface, outbuf))
    goto output_done;
#endif

  /* Host-copy output path (Linux Vulkan path and Windows fallback). */
  result = out_surface->Convert (AMF_MEMORY_HOST);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (self,
        "Failed to bring AMF output to host memory, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return GST_FLOW_ERROR;
  }

  if (!gst_amf_base_filter_copy_surface_to_buffer (self, out_surface, outbuf))
    return GST_FLOW_ERROR;

#ifdef G_OS_WIN32
output_done:
#endif
  GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (inbuf);
  GST_BUFFER_DTS (outbuf) = GST_BUFFER_DTS (inbuf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
  return GST_FLOW_OK;

#ifdef G_OS_WIN32
error:
#ifdef HAVE_GST_D3D12
  if (d3d12_input_mapped)
    gst_memory_unmap (gst_buffer_peek_memory (d3d12_input_buf, 0),
        &d3d12_input_map);
  gst_clear_buffer (&d3d12_input_buf);
#endif
  if (d3d11_input_mapped)
    gst_memory_unmap (gst_buffer_peek_memory (d3d11_input_buf, 0),
        &d3d11_input_map);
  gst_clear_buffer (&d3d11_input_buf);
  return GST_FLOW_ERROR;
#endif
}
