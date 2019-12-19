/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
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
#  include <config.h>
#endif

#include "gstd3d11upload.h"
#include "gstd3d11memory.h"
#include "gstd3d11device.h"
#include "gstd3d11bufferpool.h"
#include "gstd3d11utils.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_upload_debug);
#define GST_CAT_DEFAULT gst_d3d11_upload_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_D3D11_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
            GST_D3D11_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS)));

#define gst_d3d11_upload_parent_class parent_class
G_DEFINE_TYPE (GstD3D11Upload, gst_d3d11_upload, GST_TYPE_D3D11_BASE_FILTER);

static GstCaps *gst_d3d11_upload_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_d3d11_upload_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d11_upload_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static GstFlowReturn gst_d3d11_upload_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static void
gst_d3d11_upload_class_init (GstD3D11UploadClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 uploader", "Filter/Video",
      "Uploads data into D3D11 texture memory",
      "Seungha Yang <seungha.yang@navercorp.com>");

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_upload_transform_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_upload_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_upload_decide_allocation);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d11_upload_transform);

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_upload_debug,
      "d3d11upload", 0, "d3d11upload Element");
}

static void
gst_d3d11_upload_init (GstD3D11Upload * upload)
{
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++)
    gst_caps_set_features (tmp, i,
        gst_caps_features_from_string (feature_name));

  return tmp;
}

static GstCaps *
gst_d3d11_upload_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    tmp = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  } else {
    GstCaps *newcaps;
    tmp = gst_caps_ref (caps);

    newcaps = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
    tmp = gst_caps_merge (tmp, newcaps);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_d3d11_upload_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;

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

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstCapsFeatures *features;
    GstStructure *config;
    gboolean is_d3d11 = FALSE;

    features = gst_caps_get_features (caps, 0);

    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
      GST_DEBUG_OBJECT (filter, "upstream support d3d11 memory");
      pool = gst_d3d11_buffer_pool_new (filter->device);
      is_d3d11 = TRUE;
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    /* d3d11 pool does not support video alignment */
    if (!is_d3d11) {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    }

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;

    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

    /* d3d11 buffer pool might update buffer size by self */
    if (is_d3d11)
      size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

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
gst_d3d11_upload_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstStructure *config;
  gboolean update_pool = FALSE;
  GstVideoInfo vinfo;
  gint i;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  gst_video_info_from_caps (&vinfo, outcaps);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool && !GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }

    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = max = 0;
  }

  if (!pool) {
    GST_DEBUG_OBJECT (trans, "create our pool");

    pool = gst_d3d11_buffer_pool_new (filter->device);
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);

  {
    GstQuery *usage_query;
    gboolean can_dynamic = FALSE;

    usage_query = gst_query_new_d3d11_usage (D3D11_USAGE_DYNAMIC);
    gst_pad_peer_query (GST_BASE_TRANSFORM_SRC_PAD (trans), usage_query);
    gst_query_parse_d3d11_usage_result (usage_query, &can_dynamic);
    gst_query_unref (usage_query);

    if (can_dynamic) {
      GstD3D11AllocationParams *d3d11_params;

      GST_DEBUG_OBJECT (trans, "downstream support dynamic usage");

      d3d11_params =
          gst_buffer_pool_config_get_d3d11_allocation_params (config);
      if (!d3d11_params) {
        /* dynamic usage should have at least one bind flag.
         * but followings are not allowed in this case
         * D3D11_BIND_STREAM_OUTPUT
         * D3D11_BIND_RENDER_TARGET
         * D3D11_BIND_DEPTH_STENCIL
         * D3D11_BIND_UNORDERED_ACCESS */
        d3d11_params = gst_d3d11_allocation_params_new (&vinfo,
            GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT,
            D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE);
      } else {
        for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
          d3d11_params->desc[i].Usage = D3D11_USAGE_DYNAMIC;
          d3d11_params->desc[i].CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
      }

      gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
      gst_d3d11_allocation_params_free (d3d11_params);
    }
  }

  gst_buffer_pool_set_config (pool, config);

  /* update size with calculated one */
  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static GstFlowReturn
upload_transform_dynamic (GstD3D11BaseFilter * filter,
    GstD3D11Device * device, GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstVideoFrame in_frame;
  gint i, j, k;
  GstFlowReturn ret = GST_FLOW_OK;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (device);

  if (!gst_video_frame_map (&in_frame, &filter->in_info, inbuf,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))
    goto invalid_buffer;

  gst_d3d11_device_lock (device);
  for (i = 0, j = 0; i < gst_buffer_n_memory (outbuf); i++) {
    GstD3D11Memory *dmem =
        (GstD3D11Memory *) gst_buffer_peek_memory (outbuf, i);
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr;
    D3D11_TEXTURE2D_DESC *desc = &dmem->desc;
    gsize offset[GST_VIDEO_MAX_PLANES];
    gint stride[GST_VIDEO_MAX_PLANES];
    gsize dummy;

    hr = ID3D11DeviceContext_Map (device_context,
        (ID3D11Resource *) dmem->texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

    if (!gst_d3d11_result (hr)) {
      GST_ERROR_OBJECT (filter,
          "Failed to map staging texture (0x%x)", (guint) hr);
      gst_d3d11_device_unlock (device);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    gst_d3d11_dxgi_format_get_size (desc->Format, desc->Width, desc->Height,
        map.RowPitch, offset, stride, &dummy);

    for (k = 0; k < gst_d3d11_dxgi_format_n_planes (dmem->desc.Format); k++) {
      gint h, width;
      guint8 *dst, *src;

      dst = (guint8 *) map.pData + offset[k];
      src = GST_VIDEO_FRAME_PLANE_DATA (&in_frame, j);
      width = GST_VIDEO_FRAME_COMP_WIDTH (&in_frame, j) *
          GST_VIDEO_FRAME_COMP_PSTRIDE (&in_frame, j);

      for (h = 0; h < GST_VIDEO_FRAME_COMP_HEIGHT (&in_frame, j); h++) {
        memcpy (dst, src, width);
        dst += stride[k];
        src += GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, j);
      }

      j++;
    }

    ID3D11DeviceContext_Unmap (device_context,
        (ID3D11Resource *) dmem->texture, 0);
  }
  gst_d3d11_device_unlock (device);

done:
  gst_video_frame_unmap (&in_frame);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    GST_ELEMENT_WARNING (filter, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
upload_transform (GstD3D11BaseFilter * filter, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoFrame in_frame, out_frame;
  gint i;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!gst_video_frame_map (&in_frame, &filter->in_info, inbuf,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, &filter->out_info, outbuf,
          GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&in_frame); i++) {
    if (!gst_video_frame_copy_plane (&out_frame, &in_frame, i)) {
      GST_ERROR_OBJECT (filter, "Couldn't copy %dth plane", i);
      ret = GST_FLOW_ERROR;
      break;
    }
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    GST_ELEMENT_WARNING (filter, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_d3d11_upload_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstMemory *mem;
  GstD3D11Device *device;

  mem = gst_buffer_peek_memory (outbuf, 0);
  if (gst_is_d3d11_memory (mem)) {
    GstD3D11Memory *dmem = (GstD3D11Memory *) mem;
    device = dmem->device;

    if (dmem->desc.Usage == D3D11_USAGE_DYNAMIC) {
      return upload_transform_dynamic (filter, device, inbuf, outbuf);
    }
  }

  return upload_transform (filter, inbuf, outbuf);
}
