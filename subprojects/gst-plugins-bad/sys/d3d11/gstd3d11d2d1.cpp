/*
 * GStreamer
 * Copyright (C) 2023 Aleksandr Slobodeniuk <aslobodeniuk@fluendo.com>
 *                    Marek Olejnik <molejnik@fluendo.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-d3d11d2d1
 *
 * FIXME:Describe d3d11d2d1 here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * 
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <d2d1.h>
#include "gstd3d11d2d1.h"

/* Filter signals and args */
enum
{
    SIGNAL_DRAW = 0,
    SIGNAL_LAST,
};
static guint gst_d3d11_d2d1_signals[SIGNAL_LAST] = { 0 };

enum
{
  PROP_0,
  PROP_ENABLED,
};

#define GST_D3D11_D2D1_SUPPORTED_FORMATS "{ BGRA, RGBA, BGRx, RGBx }"

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticCaps sink_template_caps =
GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
    GST_D3D11_D2D1_SUPPORTED_FORMATS));

static GstStaticCaps src_template_caps =
GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
    GST_D3D11_D2D1_SUPPORTED_FORMATS));

#define gst_d3d11_d2d1_parent_class parent_class
G_DEFINE_TYPE (Gstd3d11d2d1, gst_d3d11_d2d1, GST_TYPE_D3D11_BASE_FILTER);

GST_ELEMENT_REGISTER_DEFINE (d3d11_d2d1, "d3d11d2d1", GST_RANK_NONE,
    GST_TYPE_D3D11D2D1);

GST_DEBUG_CATEGORY_EXTERN(gst_d3d11_d2d1_debug);
#define GST_CAT_DEFAULT gst_d3d11_d2d1_debug

static void gst_d3d11_d2d1_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_d2d1_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean
gst_d3d11_d2d1_decide_allocation(GstBaseTransform* trans, GstQuery* query);
static GstFlowReturn
gst_d3d11_d2d1_transform(GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf);

/* GObject vmethod implementations */

/* initialize the d3d11d2d1's class */
static void
gst_d3d11_d2d1_class_init (Gstd3d11d2d1Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass* trans_class = GST_BASE_TRANSFORM_CLASS(klass);
  GstCaps* caps;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_d3d11_d2d1_set_property;
  gobject_class->get_property = gst_d3d11_d2d1_get_property;

  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled", "Enabled", "Emit 'draw' signal",
          FALSE, G_PARAM_READWRITE));

  caps = gst_d3d11_get_updated_template_caps(&sink_template_caps);
  gst_element_class_add_pad_template(gstelement_class,
      gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
  gst_caps_unref(caps);

  caps = gst_d3d11_get_updated_template_caps(&src_template_caps);
  gst_element_class_add_pad_template(gstelement_class,
      gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
  gst_caps_unref(caps);

  gst_element_class_set_details_simple (gstelement_class,
      "d3d11d2d1",
      "Drawing with Direct2D on top of the video",
      "Filter/Video/Hardware",
	  "Aleksandr Slobodeniuk <aslobodeniuk@fluendo.com>, "
	  "Marek Olejnik <molejnik@fluendo.com>");

  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR(gst_d3d11_d2d1_decide_allocation);
  trans_class->transform = GST_DEBUG_FUNCPTR(gst_d3d11_d2d1_transform);
  trans_class->passthrough_on_same_caps = FALSE;

  gst_d3d11_d2d1_signals[SIGNAL_DRAW] =
      g_signal_new("draw", G_TYPE_FROM_CLASS(klass),
          G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
          G_TYPE_POINTER, G_TYPE_UINT64);
}

static GstFlowReturn
gst_d3d11_d2d1_transform(GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf)
{
    GstD3D11BaseFilter* filter = GST_D3D11_BASE_FILTER(trans);
    Gstd3d11d2d1* d2d1 = GST_D3D11D2D1(trans);
    ID3D11Device* device_handle;
    ID3D11DeviceContext* context_handle;
    GstMapInfo in_map;
    GstMapInfo out_map;
    GstD3D11Device* device = filter->device;
    guint subidx;
    D3D11_BOX src_box = { 0, };
    D3D11_TEXTURE2D_DESC dst_desc;
    D3D11_TEXTURE2D_DESC src_desc;
    GstFlowReturn ret = GST_FLOW_ERROR;
    ID2D1RenderTarget* pRenderTarget = NULL;
    IDXGISurface* pDxgiSurface = NULL;
    HRESULT hr = S_OK;

    g_return_val_if_fail(gst_buffer_n_memory(inbuf) == 1, GST_FLOW_ERROR);
    g_return_val_if_fail(gst_buffer_n_memory(outbuf) == 1, GST_FLOW_ERROR);

    device_handle = gst_d3d11_device_get_device_handle(device);
    context_handle = gst_d3d11_device_get_device_context_handle(device);
    if (!gst_d3d11_buffer_map(inbuf, device_handle, &in_map, GST_MAP_READ)) {
        goto invalid_memory;
    }

    if (!gst_d3d11_buffer_map(outbuf, device_handle, &out_map, GST_MAP_WRITE)) {
        gst_d3d11_buffer_unmap(inbuf, &in_map);
        goto invalid_memory;
    }

    gst_d3d11_device_lock(device);
    {
        GstD3D11Memory* mem = (GstD3D11Memory*)gst_buffer_peek_memory(outbuf, 0);
        GstD3D11Memory* src_dmem = (GstD3D11Memory*)gst_buffer_peek_memory(inbuf, 0);

        subidx = gst_d3d11_memory_get_subresource_index(mem);
        gst_d3d11_memory_get_texture_desc(mem, &dst_desc);
        gst_d3d11_memory_get_texture_desc(src_dmem, &src_desc);

        if (dst_desc.Width != src_desc.Width ||
            dst_desc.Height != src_desc.Height) {
            GST_ERROR_OBJECT(filter, "Src and dest dimensions do not match (%dx%d) -> (%dx%d)",
                src_desc.Width, src_desc.Height, dst_desc.Width, dst_desc.Height);
            goto cleanup;
        }

        src_box.left = 0;
        src_box.top = 0;
        src_box.front = 0;
        src_box.back = 1;
        src_box.right = dst_desc.Width;
        src_box.bottom = dst_desc.Height;

        guint src_subidx = gst_d3d11_memory_get_subresource_index(src_dmem);

        context_handle->CopySubresourceRegion((ID3D11Resource*)out_map.data,
            subidx, 0, 0, 0, (ID3D11Resource*)in_map.data, src_subidx, &src_box);

        hr = ((ID3D11Resource*)out_map.data)->QueryInterface(&pDxgiSurface);
        if (!gst_d3d11_result(hr, device)) {
            GST_ERROR("Could not query IDXGISurface, hr: 0x%x", (guint)hr);
            goto cleanup;
        }

        D2D1_RENDER_TARGET_PROPERTIES props =
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96,
                96);

        static ID2D1Factory* direct2DFactory;
        if (!direct2DFactory)
        {
            // FIXME: cache in the d3d11context!!
            hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                &direct2DFactory);

            if (!gst_d3d11_result(hr, device)) {
                GST_ERROR("Could not create ID2D1Factory, hr: 0x%x", (guint)hr);
                goto cleanup;
            }
        }

        hr = direct2DFactory->CreateDxgiSurfaceRenderTarget(
            pDxgiSurface,
            &props,
            &pRenderTarget);

        if (!gst_d3d11_result(hr, device)) {
            GST_ERROR("Could not create CreateDxgiSurfaceRenderTarget, hr: 0x%x", (guint)hr);
            goto cleanup;
        }

        GST_DEBUG_OBJECT(d2d1, "Emit signal to the user");
        g_signal_emit(d2d1, gst_d3d11_d2d1_signals[SIGNAL_DRAW], 0, pRenderTarget, GST_BUFFER_PTS (inbuf));
    }

    ret = GST_FLOW_OK;
cleanup:
    // FIXME: cache this in gstmemory
    if (pRenderTarget)
        pRenderTarget->Release();
    if (pDxgiSurface)
        pDxgiSurface->Release();
    gst_d3d11_buffer_unmap(inbuf, &in_map);
    gst_d3d11_buffer_unmap(outbuf, &out_map);
    gst_d3d11_device_unlock(device);

    return ret;

invalid_memory:
    GST_ERROR_OBJECT(d2d1, "invalid memory");
    return GST_FLOW_ERROR;
}

static gboolean
gst_d3d11_d2d1_decide_allocation(GstBaseTransform* trans, GstQuery* query)
{
    GstD3D11BaseFilter* filter = GST_D3D11_BASE_FILTER(trans);
    GstCaps* outcaps = NULL;
    GstBufferPool* pool = NULL;
    guint size, min, max;
    GstStructure* config;
    GstVideoInfo vinfo;
    const GstD3D11Format* d3d11_format;
    GstD3D11AllocationParams* d3d11_params;
    const guint bind_flags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
    UINT supported = 0;
    HRESULT hr;
    ID3D11Device* device_handle;

    gst_query_parse_allocation(query, &outcaps, NULL);

    GST_DEBUG_OBJECT(filter, "Decide allocation for caps %" GST_PTR_FORMAT, outcaps);

    if (!outcaps)
        return FALSE;

    gst_video_info_from_caps(&vinfo, outcaps);

    if (GST_VIDEO_INFO_N_PLANES(&vinfo) != 1) {
        GST_DEBUG_OBJECT(filter, "Unexpected number of planes (%d)",
            GST_VIDEO_INFO_N_PLANES(&vinfo));
        return FALSE;
    }

    d3d11_format = gst_d3d11_device_format_from_gst(filter->device,
        GST_VIDEO_INFO_FORMAT(&vinfo));
    if (!d3d11_format) {
        GST_ERROR_OBJECT(filter, "Unknown format caps %" GST_PTR_FORMAT, outcaps);
        return FALSE;
    }

    if (d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
        dxgi_format = d3d11_format->resource_format[0];
    }
    else {
        dxgi_format = d3d11_format->dxgi_format;
    }

    device_handle = gst_d3d11_device_get_device_handle(filter->device);

    hr = device_handle->CheckFormatSupport(dxgi_format, &supported);
    if (gst_d3d11_result(hr, filter->device)) {
        if ((supported & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) !=
            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) {
            GST_ERROR_OBJECT(filter, "Device doesn't support shader sample texture binding format");
            return FALSE;
        }

        if ((supported & D3D11_FORMAT_SUPPORT_RENDER_TARGET) !=
            D3D11_FORMAT_SUPPORT_RENDER_TARGET) {
            GST_ERROR_OBJECT(filter, "Device doesn't support render target texture binding format");
            return FALSE;
        }
    }

    guint n_pools = gst_query_get_n_allocation_pools(query);
    GST_DEBUG_OBJECT(filter, "Downstream proposed %d pools", n_pools);
    for (guint p = 0; p < n_pools; p++) {
        gboolean use_proposed_pool = FALSE;
        gst_query_parse_nth_allocation_pool(query, p, &pool, &size, &min, &max);

        if (pool && GST_IS_D3D11_BUFFER_POOL(pool) &&
            GST_D3D11_BUFFER_POOL(pool)->device == filter->device) {
            GstStructure* config;
            GstD3D11AllocationParams* d3d11_params;

            config = gst_buffer_pool_get_config(pool);
            d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params(config);

            use_proposed_pool = ((d3d11_params->desc[0].BindFlags & bind_flags) == bind_flags);
            GST_DEBUG_OBJECT(filter, "Bind flags (%u) are %scompatible to required ones (%u)",
                d3d11_params->desc[0].BindFlags, use_proposed_pool ? "" : "not ", bind_flags);

            gst_structure_free(config);
            gst_d3d11_allocation_params_free(d3d11_params);
        }

        if (use_proposed_pool) {
            GST_DEBUG_OBJECT(filter, "Proposed pool %d is going to be used", p);
            gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
            gst_object_unref(pool);
            return GST_BASE_TRANSFORM_CLASS(parent_class)->decide_allocation(trans,
                query);
        }

        gst_clear_object(&pool);
    }

    size = GST_VIDEO_INFO_SIZE(&vinfo);
    min = max = 0;

    GST_DEBUG_OBJECT(filter, "Creating a new buffer pool");
    pool = gst_d3d11_buffer_pool_new(filter->device);

    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_set_params(config, outcaps, size, min, max);

    g_warn_if_fail (NULL ==
        gst_buffer_pool_config_get_d3d11_allocation_params(config));

    d3d11_params = gst_d3d11_allocation_params_new(filter->device, &vinfo,
            (GstD3D11AllocationFlags)0, bind_flags);

    gst_buffer_pool_config_set_d3d11_allocation_params(config, d3d11_params);
    gst_d3d11_allocation_params_free(d3d11_params);

    gst_buffer_pool_set_config(pool, config);

    /* d3d11 buffer pool will update buffer size based on allocated texture,
     * get size from config again */
    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_get_params(config, NULL, &size, NULL, NULL);
    gst_structure_free(config);

    if (n_pools)
        gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
    else
        gst_query_add_allocation_pool(query, pool, size, min, max);

    gst_object_unref(pool);
    return GST_BASE_TRANSFORM_CLASS(parent_class)->decide_allocation(trans,
        query);
}

static void
gst_d3d11_d2d1_init (Gstd3d11d2d1 * d2d1)
{
    d2d1->enabled = FALSE;
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(d2d1), !d2d1->enabled);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(d2d1), FALSE);
}

static void
gst_d3d11_d2d1_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstd3d11d2d1 *d2d1 = GST_D3D11D2D1 (object);

  switch (prop_id) {
    case PROP_ENABLED:
      d2d1->enabled = g_value_get_boolean (value);
      gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(d2d1), !d2d1->enabled);
      GST_INFO_OBJECT(d2d1, "Drawing is %s", d2d1->enabled ? "enabled" : "disabled");
      gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(d2d1));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_d2d1_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstd3d11d2d1 *d2d1 = GST_D3D11D2D1 (object);

  switch (prop_id) {
    case PROP_ENABLED:
      g_value_set_boolean (value, d2d1->enabled);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
