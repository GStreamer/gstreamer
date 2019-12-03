/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) <2019> Jeongki Kim <jeongki.kim@jeongki.kim>
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

/**
 * SECTION:element-d3d11colorconvert
 * @title: d3d11colorconvert
 *
 * Convert video frames between supported video formats.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw,format=NV12 ! d3d11upload ! d3d11videocolorconvert ! d3d11videosink
 * ]|
 *  This will output a test video (generated in NV12 format) in a video
 * window. If the video sink selected does not support NV12
 * d3d11colorconvert will automatically convert the video to a format understood
 * by the video sink.
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstd3d11colorconvert.h"
#include "gstd3d11utils.h"
#include "gstd3d11memory.h"
#include "gstd3d11device.h"
#include "gstd3d11bufferpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_color_convert_debug);
#define GST_CAT_DEFAULT gst_d3d11_color_convert_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS))
    );

#define gst_d3d11_color_convert_parent_class parent_class
G_DEFINE_TYPE (GstD3D11ColorConvert,
    gst_d3d11_color_convert, GST_TYPE_D3D11_BASE_FILTER);

static void gst_d3d11_color_convert_dispose (GObject * object);
static GstCaps *gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d11_color_convert_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d11_color_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
static gboolean
gst_d3d11_color_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean
gst_d3d11_color_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_d3d11_color_convert_start (GstBaseTransform * trans);

static GstFlowReturn gst_d3d11_color_convert_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d11_color_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static gboolean gst_d3d11_color_convert_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);

/* copies the given caps */
static GstCaps *
gst_d3d11_color_convert_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_is_equal (f, feature))
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          NULL);

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

static void
gst_d3d11_color_convert_class_init (GstD3D11ColorConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstD3D11BaseFilterClass *bfilter_class = GST_D3D11_BASE_FILTER_CLASS (klass);

  gobject_class->dispose = gst_d3d11_color_convert_dispose;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 Colorspace converter",
      "Filter/Converter/Video/Hardware",
      "Converts video from one colorspace to another using D3D11",
      "Seungha Yang <seungha.yang@navercorp.com>, "
      "Jeongki Kim <jeongki.kim@jeongki.kim>");

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_fixate_caps);
  trans_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_filter_meta);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_decide_allocation);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_transform);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_start);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_query);

  bfilter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_set_info);

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_color_convert_debug,
      "d3d11colorconvert", 0, "Video Colorspace Convert via D3D11");
}

static void
gst_d3d11_color_convert_init (GstD3D11ColorConvert * self)
{
}

static void
clear_shader_resource (GstD3D11Device * device, GstD3D11ColorConvert * self)
{
  gint i;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (self->shader_resource_view[i]) {
      ID3D11ShaderResourceView_Release (self->shader_resource_view[i]);
      self->shader_resource_view[i] = NULL;
    }

    if (self->render_target_view[i]) {
      ID3D11RenderTargetView_Release (self->render_target_view[i]);
      self->render_target_view[i] = NULL;
    }
  }

  self->num_input_view = 0;
  self->num_output_view = 0;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (self->in_texture[i]) {
      ID3D11Texture2D_Release (self->in_texture[i]);
      self->in_texture[i] = NULL;
    }

    if (self->out_texture[i]) {
      ID3D11Texture2D_Release (self->out_texture[i]);
      self->out_texture[i] = NULL;
    }
  }

  if (self->converter)
    gst_d3d11_color_converter_free (self->converter);
  self->converter = NULL;
}

static void
gst_d3d11_color_convert_clear_shader_resource (GstD3D11ColorConvert * self)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (self);

  if (filter->device) {
    gst_d3d11_device_thread_add (filter->device,
        (GstD3D11DeviceThreadFunc) clear_shader_resource, self);
  }
}

static void
gst_d3d11_color_convert_dispose (GObject * object)
{
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (object);

  gst_d3d11_color_convert_clear_shader_resource (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (trans);
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  if (self->can_convert)
    tmp = gst_d3d11_color_convert_caps_remove_format_info (caps);
  else
    tmp = gst_caps_copy (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static GstCaps *
gst_d3d11_color_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  GST_DEBUG_OBJECT (trans, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  result = gst_d3d11_caps_fixate_format (caps, gst_caps_ref (othercaps));

  if (!result)
    result = othercaps;
  else
    gst_caps_unref (othercaps);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    }
  }

  return result;
}

static gboolean
gst_d3d11_color_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params)
{
  /* This element cannot passthrough the crop meta, because it would convert the
   * wrong sub-region of the image, and worst, our output image may not be large
   * enough for the crop to be applied later */
  if (api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  /* propose all other metadata upstream */
  return TRUE;
}

static gboolean
gst_d3d11_color_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstCaps *caps;
  guint n_pools, i;
  GstStructure *config;
  guint size;
  GstD3D11AllocationParams *d3d11_params;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (decide_query == NULL)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, NULL, NULL, NULL);
    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params) {
    d3d11_params = gst_d3d11_allocation_params_new (&info,
        GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT, D3D11_USAGE_DEFAULT,
        D3D11_BIND_SHADER_RESOURCE);
  } else {
    /* Set bind flag */
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      d3d11_params->desc[i].BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  /* size will be updated by d3d11 buffer pool */
  gst_buffer_pool_config_set_params (config, caps, 0, 0, 0);

  if (!gst_buffer_pool_set_config (pool, config))
    goto config_failed;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;
  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

static gboolean
gst_d3d11_color_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min = 0, max = 0;
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;
  gboolean update_pool = FALSE;
  GstVideoInfo info;
  gint i;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, outcaps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool && !GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params) {
    d3d11_params = gst_d3d11_allocation_params_new (&info,
        GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT, D3D11_USAGE_DEFAULT,
        D3D11_BIND_RENDER_TARGET);
  } else {
    /* Set bind flag */
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      d3d11_params->desc[i].BindFlags |=
          (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    }
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_d3d11_color_convert_start (GstBaseTransform * trans)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (trans);
  gboolean is_hardware;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans))
    return FALSE;

  g_object_get (filter->device, "hardware", &is_hardware, NULL);

  if (!is_hardware) {
    GST_WARNING_OBJECT (trans, "D3D11 device is running on software emulation");
    self->can_convert = FALSE;
  } else {
    self->can_convert = TRUE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_color_convert_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  if (gst_query_is_d3d11_usage (query) && direction == GST_PAD_SINK) {
    D3D11_USAGE usage = D3D11_USAGE_DEFAULT;

    gst_query_parse_d3d11_usage (query, &usage);
    if (usage == D3D11_USAGE_DEFAULT || usage == D3D11_USAGE_DYNAMIC)
      gst_query_set_d3d11_usage_result (query, TRUE);
    else
      gst_query_set_d3d11_usage_result (query, FALSE);

    return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static gboolean
create_shader_input_resource (GstD3D11ColorConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc = { 0, };
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = { 0 };
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11ShaderResourceView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  if (self->num_input_view)
    return TRUE;

  device_handle = gst_d3d11_device_get_device_handle (device);

  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
        break;

      texture_desc.Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      texture_desc.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      texture_desc.Format = format->resource_format[i];

      hr = ID3D11Device_CreateTexture2D (device_handle,
          &texture_desc, NULL, &tex[i]);
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
        goto error;
      }
    }
  } else {
    texture_desc.Width = GST_VIDEO_INFO_WIDTH (info);
    texture_desc.Height = GST_VIDEO_INFO_HEIGHT (info);
    texture_desc.Format = format->dxgi_format;

    hr = ID3D11Device_CreateTexture2D (device_handle,
        &texture_desc, NULL, &tex[0]);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (format->dxgi_format == DXGI_FORMAT_NV12 ||
        format->dxgi_format == DXGI_FORMAT_P010) {
      ID3D11Resource_AddRef (tex[0]);
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipLevels = 1;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = ID3D11Device_CreateShaderResourceView (device_handle,
        (ID3D11Resource *) tex[i], &view_desc, &view[i]);

    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self,
          "Failed to create resource view (0x%x)", (guint) hr);
      goto error;
    }
  }

  self->num_input_view = i;

  GST_DEBUG_OBJECT (self,
      "%d shader resource view created", self->num_input_view);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    self->in_texture[i] = tex[i];
    self->shader_resource_view[i] = view[i];
  }

  return TRUE;

error:
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (view[i])
      ID3D11ShaderResourceView_Release (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (tex[i])
      ID3D11Texture2D_Release (tex[i]);
  }

  return FALSE;
}

static gboolean
create_shader_output_resource (GstD3D11ColorConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc = { 0, };
  D3D11_RENDER_TARGET_VIEW_DESC view_desc = { 0, };
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  if (self->num_output_view)
    return TRUE;

  device_handle = gst_d3d11_device_get_device_handle (device);

  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
        break;

      texture_desc.Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      texture_desc.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      texture_desc.Format = format->resource_format[i];

      hr = ID3D11Device_CreateTexture2D (device_handle,
          &texture_desc, NULL, &tex[i]);
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
        goto error;
      }
    }
  } else {
    texture_desc.Width = GST_VIDEO_INFO_WIDTH (info);
    texture_desc.Height = GST_VIDEO_INFO_HEIGHT (info);
    texture_desc.Format = format->dxgi_format;

    hr = ID3D11Device_CreateTexture2D (device_handle,
        &texture_desc, NULL, &tex[0]);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (format->dxgi_format == DXGI_FORMAT_NV12 ||
        format->dxgi_format == DXGI_FORMAT_P010) {
      ID3D11Resource_AddRef (tex[0]);
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipSlice = 0;
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = ID3D11Device_CreateRenderTargetView (device_handle,
        (ID3D11Resource *) tex[i], &view_desc, &view[i]);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self,
          "Failed to create %dth render target view (0x%x)", i, (guint) hr);
      goto error;
    }
  }

  self->num_output_view = i;

  GST_DEBUG_OBJECT (self, "%d render view created", self->num_output_view);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    self->out_texture[i] = tex[i];
    self->render_target_view[i] = view[i];
  }

  return TRUE;

error:
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (view[i])
      ID3D11RenderTargetView_Release (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (tex[i])
      ID3D11Texture2D_Release (tex[i]);
  }

  return FALSE;
}

static gboolean
gst_d3d11_color_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (filter);
  const GstVideoInfo *unknown_info;

  gst_d3d11_color_convert_clear_shader_resource (self);

  GST_DEBUG_OBJECT (self, "Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  /* these must match */
  if (in_info->width != out_info->width || in_info->height != out_info->height
      || in_info->fps_n != out_info->fps_n || in_info->fps_d != out_info->fps_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_info->par_n != out_info->par_n || in_info->par_d != out_info->par_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_info->interlace_mode != out_info->interlace_mode)
    goto format_mismatch;

  /* do not need to setup converter */
  if (!self->can_convert)
    return TRUE;

  self->in_d3d11_format =
      gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (in_info));
  if (!self->in_d3d11_format) {
    unknown_info = in_info;
    goto format_unknown;
  }

  self->out_d3d11_format =
      gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (out_info));
  if (!self->out_d3d11_format) {
    unknown_info = out_info;
    goto format_unknown;
  }

  self->converter = gst_d3d11_color_converter_new (filter->device,
      in_info, out_info);

  if (!self->converter) {
    GST_ERROR_OBJECT (self, "couldn't set converter");
    return FALSE;
  }

  return TRUE;

  /* ERRORS */
format_mismatch:
  {
    GST_ERROR_OBJECT (self, "input and output formats do not match");
    return FALSE;
  }
format_unknown:
  {
    GST_ERROR_OBJECT (self,
        "%s couldn't be converted to d3d11 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (unknown_info)));
    return FALSE;
  }
}

typedef struct
{
  GstD3D11ColorConvert *self;
  GstBuffer *in_buf;
  GstBuffer *out_buf;

  gboolean ret;
} DoConvertData;

static void
do_convert (GstD3D11Device * device, DoConvertData * data)
{
  GstD3D11ColorConvert *self = data->self;
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (self);
  ID3D11DeviceContext *context_handle;
  ID3D11ShaderResourceView *resource_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView *render_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i, j, view_index;
  gboolean copy_input = FALSE;
  gboolean copy_output = FALSE;

  context_handle = gst_d3d11_device_get_device_context_handle (device);

  view_index = 0;
  for (i = 0; i < gst_buffer_n_memory (data->in_buf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (data->in_buf, i);
    GstD3D11Memory *d3d11_mem;
    GstMapInfo info;

    g_assert (gst_is_d3d11_memory (mem));

    d3d11_mem = (GstD3D11Memory *) mem;
    /* map to transfer pending staging data if any */
    if (d3d11_mem->desc.Usage == D3D11_USAGE_DEFAULT) {
      gst_memory_map (mem, &info, GST_MAP_READ | GST_MAP_D3D11);
      gst_memory_unmap (mem, &info);
    }

    if (gst_d3d11_memory_ensure_shader_resource_view (mem)) {
      GST_TRACE_OBJECT (self, "Use input texture resource without copy");

      for (j = 0; j < d3d11_mem->num_shader_resource_views; j++) {
        resource_view[view_index] = d3d11_mem->shader_resource_view[j];
        view_index++;
      }
    } else {
      GST_TRACE_OBJECT (self, "Render using fallback input texture");
      copy_input = TRUE;

      if (!create_shader_input_resource (self, device,
              self->in_d3d11_format, &filter->in_info)) {
        GST_ERROR_OBJECT (self, "Failed to configure fallback input texture");
        data->ret = FALSE;
        return;
      }
      break;
    }
  }

  /* if input memory has no resource view,
   * copy texture into our fallback texture */
  if (copy_input) {
    for (i = 0; i < gst_buffer_n_memory (data->in_buf); i++) {
      GstMemory *mem = gst_buffer_peek_memory (data->in_buf, i);
      GstD3D11Memory *d3d11_mem;

      g_assert (gst_is_d3d11_memory (mem));

      d3d11_mem = (GstD3D11Memory *) mem;

      ID3D11DeviceContext_CopySubresourceRegion (context_handle,
          (ID3D11Resource *) self->in_texture[i], 0, 0, 0, 0,
          (ID3D11Resource *) d3d11_mem->texture, 0, NULL);
    }
  }

  view_index = 0;
  for (i = 0; i < gst_buffer_n_memory (data->out_buf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (data->out_buf, i);
    GstD3D11Memory *d3d11_mem;

    g_assert (gst_is_d3d11_memory (mem));

    d3d11_mem = (GstD3D11Memory *) mem;

    if (gst_d3d11_memory_ensure_render_target_view (mem)) {
      GST_TRACE_OBJECT (self, "Render to output texture directly");

      for (j = 0; j < d3d11_mem->num_render_target_views; j++) {
        render_view[view_index] = d3d11_mem->render_target_view[j];
        view_index++;
      }
    } else {
      GST_TRACE_OBJECT (self, "Render to fallback output texture");
      copy_output = TRUE;

      if (!create_shader_output_resource (self, device, self->out_d3d11_format,
              &filter->out_info)) {
        GST_ERROR_OBJECT (self, "Failed to configure fallback output texture");
        data->ret = FALSE;
        return;
      }
      break;
    }
  }

  data->ret = gst_d3d11_color_converter_convert (self->converter,
      copy_input ? self->shader_resource_view : resource_view,
      copy_output ? self->render_target_view : render_view);

  if (data->ret && copy_output) {
    for (i = 0; i < gst_buffer_n_memory (data->out_buf); i++) {
      GstMemory *mem = gst_buffer_peek_memory (data->out_buf, i);
      GstD3D11Memory *d3d11_mem;

      g_assert (gst_is_d3d11_memory (mem));

      d3d11_mem = (GstD3D11Memory *) mem;

      ID3D11DeviceContext_CopySubresourceRegion (context_handle,
          (ID3D11Resource *) d3d11_mem->texture, 0, 0, 0, 0,
          (ID3D11Resource *) self->out_texture[i], 0, NULL);
    }
  }
}

static GstFlowReturn
gst_d3d11_color_convert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (trans);
  DoConvertData data;

  data.self = self;
  data.in_buf = inbuf;
  data.out_buf = outbuf;
  data.ret = TRUE;

  gst_d3d11_device_thread_add (filter->device,
      (GstD3D11DeviceThreadFunc) do_convert, &data);

  if (!data.ret)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}
