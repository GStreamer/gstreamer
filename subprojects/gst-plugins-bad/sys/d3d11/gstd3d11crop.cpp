/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2022  <<user@hostname.org>>
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
 * SECTION:element-d3d11crop
 *
 * FIXME:Describe d3d11crop here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! d3d11crop ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstd3d11crop.h"

//GST_DEBUG_CATEGORY_STATIC (gst_d3d11crop_debug);
//#define GST_CAT_DEFAULT gst_d3d11crop_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_RIGHT,
  PROP_LEFT,
  PROP_BOTTOM,
  PROP_TOP
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticCaps sink_template_caps =
GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(GST_D3D11_ALL_FORMATS) "; "
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_D3D11_ALL_FORMATS) "; "
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
        GST_D3D11_ALL_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY
        "," GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_D3D11_ALL_FORMATS));

static GstStaticCaps src_template_caps =
GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE_WITH_FEATURES
(GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_ALL_FORMATS) "; "
GST_VIDEO_CAPS_MAKE_WITH_FEATURES
(GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
    GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
    GST_D3D11_ALL_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE(GST_D3D11_ALL_FORMATS) "; "
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_D3D11_ALL_FORMATS));

#define gst_d3d11crop_parent_class parent_class
G_DEFINE_TYPE (Gstd3d11crop, gst_d3d11crop, GST_TYPE_D3D11_BASE_FILTER);

GST_ELEMENT_REGISTER_DEFINE (d3d11crop, "d3d11crop", GST_RANK_NONE,
    GST_TYPE_D3D11CROP);

static void gst_d3d11crop_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11crop_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_d3d11crop_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_d3d11crop_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);

static gboolean
gst_d3d11_crop_decide_allocation(GstBaseTransform* trans, GstQuery* query);
static GstFlowReturn
gst_d3d11_crop_transform(GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf);
static gboolean
gst_d3d11_crop_set_info(GstD3D11BaseFilter* filter,
    GstCaps* incaps, GstVideoInfo* in_info, GstCaps* outcaps,
    GstVideoInfo* out_info);
static GstCaps*
gst_d3d11_crop_transform_caps(GstBaseTransform* trans,
    GstPadDirection direction, GstCaps* caps, GstCaps* filter);
static void
gst_d3d11_crop_before_transform(GstBaseTransform* trans, GstBuffer* in);

/* GObject vmethod implementations */

/* initialize the d3d11crop's class */
static void
gst_d3d11crop_class_init (Gstd3d11cropClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass* trans_class = GST_BASE_TRANSFORM_CLASS(klass);
  GstD3D11BaseFilterClass* bfilter_class = GST_D3D11_BASE_FILTER_CLASS(klass);
  GstCaps* caps;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_d3d11crop_set_property;
  gobject_class->get_property = gst_d3d11crop_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_LEFT,
      g_param_spec_int("left",
          "LEFT",
          "left position",
          0,
          G_MAXINT16,
          1,
          G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_RIGHT,
      g_param_spec_int("right",
          "RIGHT",
          "right position",
          0,
          G_MAXINT16,
          1,
          G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_BOTTOM,
      g_param_spec_int("bottom",
          "bottom",
          "bottom position",
          0,
          G_MAXINT16,
          1,
          G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class, PROP_TOP,
      g_param_spec_int("top",
          "top",
          "top position",
          0,
          G_MAXINT16,
          1,
          G_PARAM_READWRITE));

  caps = gst_d3d11_get_updated_template_caps(&sink_template_caps);
  gst_element_class_add_pad_template(gstelement_class,
      gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
  gst_caps_unref(caps);

  caps = gst_d3d11_get_updated_template_caps(&src_template_caps);
  gst_element_class_add_pad_template(gstelement_class,
      gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
  gst_caps_unref(caps);

  gst_element_class_set_details_simple (gstelement_class,
      "d3d11crop",
      "FIXME:Generic",
      "FIXME:Generic Template Element", " <<user@hostname.org>>");

  trans_class->passthrough_on_same_caps = TRUE;
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR(gst_d3d11_crop_before_transform);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR(gst_d3d11_crop_transform_caps);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR(gst_d3d11_crop_decide_allocation);
  trans_class->transform = GST_DEBUG_FUNCPTR(gst_d3d11_crop_transform);
  bfilter_class->set_info = GST_DEBUG_FUNCPTR(gst_d3d11_crop_set_info);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR(gst_d3d11_crop_transform_caps);
}

static void
gst_d3d11_crop_before_transform(GstBaseTransform* trans, GstBuffer* in)
{
    Gstd3d11crop* crop = GST_D3D11CROP(trans);
    GstClockTime timestamp, stream_time;

    timestamp = GST_BUFFER_TIMESTAMP(in);
    stream_time =
        gst_segment_to_stream_time(&trans->segment, GST_FORMAT_TIME, timestamp);

    GST_DEBUG_OBJECT(crop, "sync to %" GST_TIME_FORMAT,
        GST_TIME_ARGS(timestamp));

    if (GST_CLOCK_TIME_IS_VALID(stream_time))
        gst_object_sync_values(GST_OBJECT(crop), stream_time);
}

static GstCaps*
_set_caps_features(const GstCaps* caps, const gchar* feature_name)
{
    guint i, j, m, n;
    GstCaps* tmp;
    GstCapsFeatures* overlay_feature =
        gst_caps_features_from_string
        (GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

    tmp = gst_caps_new_empty();

    n = gst_caps_get_size(caps);
    for (i = 0; i < n; i++) {
        GstCapsFeatures* features, * orig_features;
        GstStructure* s = gst_caps_get_structure(caps, i);

        orig_features = gst_caps_get_features(caps, i);
        features = gst_caps_features_new(feature_name, NULL);

        if (gst_caps_features_is_any(orig_features)) {
            gst_caps_append_structure_full(tmp, gst_structure_copy(s),
                gst_caps_features_copy(features));

            if (!gst_caps_features_contains(features,
                GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION))
                gst_caps_features_add(features,
                    GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
        }
        else {
            m = gst_caps_features_get_size(orig_features);
            for (j = 0; j < m; j++) {
                const gchar* feature = gst_caps_features_get_nth(orig_features, j);

                /* if we already have the features */
                if (gst_caps_features_contains(features, feature))
                    continue;

                if (g_strcmp0(feature, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY) == 0)
                    continue;

                if (gst_caps_features_contains(overlay_feature, feature)) {
                    gst_caps_features_add(features, feature);
                }
            }
        }

        gst_caps_append_structure_full(tmp, gst_structure_copy(s), features);
    }

    gst_caps_features_free(overlay_feature);

    return tmp;
}




static gint
gst_d3d11crop_transform_dimension(gint val, gint delta)
{
    gint64 new_val = (gint64)val + (gint64)delta;

    new_val = CLAMP(new_val, 1, G_MAXINT);

    return (gint)new_val;
}

static gboolean
gst_d3d11crop_transform_dimension_value(const GValue* src_val,
    gint delta, GValue* dest_val, GstPadDirection direction, gboolean dynamic)
{
    gboolean ret = TRUE;

    if (G_VALUE_HOLDS_INT(src_val)) {
        gint ival = g_value_get_int(src_val);
        ival = gst_d3d11crop_transform_dimension(ival, delta);

        if (dynamic) {
            if (direction == GST_PAD_SRC) {
                if (ival == G_MAXINT) {
                    g_value_init(dest_val, G_TYPE_INT);
                    g_value_set_int(dest_val, ival);
                }
                else {
                    g_value_init(dest_val, GST_TYPE_INT_RANGE);
                    gst_value_set_int_range(dest_val, ival, G_MAXINT);
                }
            }
            else {
                if (ival == 1) {
                    g_value_init(dest_val, G_TYPE_INT);
                    g_value_set_int(dest_val, ival);
                }
                else {
                    g_value_init(dest_val, GST_TYPE_INT_RANGE);
                    gst_value_set_int_range(dest_val, 1, ival);
                }
            }
        }
        else {
            g_value_init(dest_val, G_TYPE_INT);
            g_value_set_int(dest_val, ival);
        }
    }
    else if (GST_VALUE_HOLDS_INT_RANGE(src_val)) {
        gint min = gst_value_get_int_range_min(src_val);
        gint max = gst_value_get_int_range_max(src_val);

        min = gst_d3d11crop_transform_dimension(min, delta);
        max = gst_d3d11crop_transform_dimension(max, delta);

        if (dynamic) {
            if (direction == GST_PAD_SRC)
                max = G_MAXINT;
            else
                min = 1;
        }

        if (min == max) {
            g_value_init(dest_val, G_TYPE_INT);
            g_value_set_int(dest_val, min);
        }
        else {
            g_value_init(dest_val, GST_TYPE_INT_RANGE);
            gst_value_set_int_range(dest_val, min, max);
        }
    }
    else if (GST_VALUE_HOLDS_LIST(src_val)) {
        gint i;

        g_value_init(dest_val, GST_TYPE_LIST);

        for (i = 0; i < gst_value_list_get_size(src_val); ++i) {
            const GValue* list_val;
            GValue newval = G_VALUE_INIT;

            list_val = gst_value_list_get_value(src_val, i);
            if (gst_d3d11crop_transform_dimension_value(list_val, delta, &newval,
                direction, dynamic))
                gst_value_list_append_value(dest_val, &newval);

            g_value_unset(&newval);
        }

        if (gst_value_list_get_size(dest_val) == 0) {
            g_value_unset(dest_val);
            ret = FALSE;
        }
    }
    else {
        ret = FALSE;
    }

    return ret;
}

static GstCaps* transform_caps_set(Gstd3d11crop *vcrop, GstCaps *caps, GstCaps *filter_caps, GstPadDirection direction, int dx, int dy)
{
    GstCaps *other_caps = gst_caps_new_empty();

    for (int i = 0; i < gst_caps_get_size(caps); ++i) {
        const GValue* v;
        GstStructure* structure, * new_structure;
        GValue w_val = G_VALUE_INIT, h_val = G_VALUE_INIT;
        GstCapsFeatures* features;

        structure = gst_caps_get_structure(caps, i);
        features = gst_caps_get_features(caps, i);

        v = gst_structure_get_value(structure, "width");
        if (!gst_d3d11crop_transform_dimension_value(v, dx, &w_val, direction,
            0)) {
            GST_WARNING_OBJECT(vcrop, "could not transform width value with dx=%d"
                ", caps structure=%" GST_PTR_FORMAT, dx, structure);
            continue;
        }

        v = gst_structure_get_value(structure, "height");
        if (!gst_d3d11crop_transform_dimension_value(v, dy, &h_val, direction,
            0)) {
            g_value_unset(&w_val);
            GST_WARNING_OBJECT(vcrop, "could not transform height value with dy=%d"
                ", caps structure=%" GST_PTR_FORMAT, dy, structure);
            continue;
        }

        new_structure = gst_structure_copy(structure);
        gst_structure_set_value(new_structure, "width", &w_val);
        gst_structure_set_value(new_structure, "height", &h_val);
        g_value_unset(&w_val);
        g_value_unset(&h_val);

        GST_LOG_OBJECT(vcrop, "transformed structure %2d: %" GST_PTR_FORMAT
            " => %" GST_PTR_FORMAT "features %" GST_PTR_FORMAT, i, structure,
            new_structure, features);
        gst_caps_append_structure(other_caps, new_structure);

        gst_caps_set_features(other_caps, i, gst_caps_features_copy(features));
    }

    if (!gst_caps_is_empty(other_caps) && filter_caps) {
        GstCaps* tmp = gst_caps_intersect_full(filter_caps, other_caps,
            GST_CAPS_INTERSECT_FIRST);
        gst_caps_replace(&other_caps, tmp);
        gst_caps_unref(tmp);
    }

    return other_caps;
}


static GstCaps*
gst_d3d11_crop_transform_caps(GstBaseTransform* trans,
    GstPadDirection direction, GstCaps* caps, GstCaps* filter_caps)
{
    GstCaps* tmp, * tmp2;
    GstCaps* result;
    Gstd3d11crop* crop = GST_D3D11CROP(trans);
    int width = 0;
    int height = 0;
    int dx, dy;

    width  = crop->right  - crop->left;
    height = crop->bottom - crop->top;
    if (direction == GST_PAD_SRC) {
        dx = crop->left + crop->right;
        dy = crop->top + crop->bottom;
    }
    else {
        dx = -(crop->left + crop->right);
        dy = -(crop->top + crop->bottom);
    }

    /* Get all possible caps that we can transform to */
    //tmp = gst_d3d11_base_convert_caps_rangify_size_info(caps, width, height);
    tmp = transform_caps_set(crop, caps, filter_caps, direction, dx, dy);
    if (filter_caps) {
        tmp2 = gst_caps_intersect_full(filter_caps, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(tmp);
        tmp = tmp2;
    }

    result = tmp;
    GST_DEBUG_OBJECT(trans, "transformed %" GST_PTR_FORMAT " into %"
        GST_PTR_FORMAT, caps, result);

    return result;

}

static GstFlowReturn
gst_d3d11_crop_transform(GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf)
{
    GstD3D11BaseFilter* filter = GST_D3D11_BASE_FILTER(trans);
    Gstd3d11crop* crop = GST_D3D11CROP(trans);
    ID3D11Device* device_handle;
    ID3D11DeviceContext* context_handle;
    GstMapInfo in_map[GST_VIDEO_MAX_PLANES];
    GstMapInfo out_map[GST_VIDEO_MAX_PLANES];
    GstD3D11Device* device = filter->device;

    if ((crop->need_update == TRUE))
    {

        gst_base_transform_set_passthrough(trans, FALSE);
        gst_base_transform_set_in_place   (trans, FALSE);
        crop->need_update = FALSE;

    }

    device_handle = gst_d3d11_device_get_device_handle(device);
    context_handle = gst_d3d11_device_get_device_context_handle(device);
    if (!gst_d3d11_buffer_map(inbuf, device_handle, in_map, GST_MAP_READ)) {
        goto invalid_memory;
    }

    if (!gst_d3d11_buffer_map(outbuf, device_handle, out_map, GST_MAP_WRITE)) {
        gst_d3d11_buffer_unmap(inbuf, in_map);
        goto invalid_memory;
    }
    gst_d3d11_device_lock(device);
    for (int i = 0; i < gst_buffer_n_memory(inbuf); i++) {
        GstD3D11Memory* mem = (GstD3D11Memory*)gst_buffer_peek_memory(outbuf, i);
        GstD3D11Memory* src_dmem = (GstD3D11Memory*) gst_buffer_peek_memory(inbuf, i);

        guint subidx;
        D3D11_BOX src_box = { 0, };
        D3D11_TEXTURE2D_DESC src_desc;
        D3D11_TEXTURE2D_DESC dst_desc;

        subidx = gst_d3d11_memory_get_subresource_index(mem);
        gst_d3d11_memory_get_texture_desc(mem, &dst_desc);
        int x = crop->left;
        int y = crop->top;
        int w = (crop->width - crop->right) - (crop->left);//crop->left + crop->right;
        int h = (crop->height - crop->bottom) - (crop->top);//crop->top  + crop->bottom;


        if (i == 0)
        {
            src_box.left = x;
            src_box.top  = y;
            src_box.front = 0;
            src_box.back = 1;
            src_box.right  = (x+w);//MIN(src_desc.Width, dst_desc.Width);
            src_box.bottom = (y+h);//MIN(src_desc.Height, dst_desc.Height);
        }
        else
        {
            src_box.left = (x/2);
            src_box.top  = (y/2);
            src_box.front = 0;
            src_box.back = 1;
            src_box.right  = ((x+w)/2);//MIN(src_desc.Width, dst_desc.Width);
            src_box.bottom = ((y+h)/2);//MIN(src_desc.Height, dst_desc.Height);

        }

        guint src_subidx = gst_d3d11_memory_get_subresource_index(src_dmem);

        context_handle->CopySubresourceRegion((ID3D11Resource*)out_map[i].data,
            subidx, 0, 0, 0, (ID3D11Resource*)in_map[i].data, src_subidx, &src_box);
    }
    gst_d3d11_device_unlock(device);

    gst_d3d11_buffer_unmap(inbuf, in_map);
    gst_d3d11_buffer_unmap(outbuf, out_map);

    return GST_FLOW_OK;

invalid_memory:
    {
        g_print("invalid memory\n");
        return GST_FLOW_ERROR;
    }

}

static gboolean
gst_d3d11_crop_set_info(GstD3D11BaseFilter* filter,
    GstCaps* incaps, GstVideoInfo* in_info, GstCaps* outcaps,
    GstVideoInfo* out_info)
{
    Gstd3d11crop* self = GST_D3D11CROP(filter);
    //g_print("@@@@@@@@@@@@@ video RES %d*%d\n", in_info->width, in_info->height);
    self->width = in_info->width;
    self->height = in_info->height;
    return TRUE;
}

static gboolean
gst_d3d11_crop_decide_allocation(GstBaseTransform* trans, GstQuery* query)
{
    GstD3D11BaseFilter* filter = GST_D3D11_BASE_FILTER(trans);
    GstCaps* outcaps = NULL;
    GstBufferPool* pool = NULL;
    guint size, min, max;
    GstStructure* config;
    gboolean update_pool = FALSE;
    GstVideoInfo vinfo;
    const GstD3D11Format* d3d11_format;
    GstD3D11AllocationParams* d3d11_params;
    guint bind_flags = 0;
    guint i;
    DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
    UINT supported = 0;
    HRESULT hr;
    ID3D11Device* device_handle;

    gst_query_parse_allocation(query, &outcaps, NULL);

    if (!outcaps)
        return FALSE;

    gst_video_info_from_caps(&vinfo, outcaps);

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
        if ((supported & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ==
            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) {
            bind_flags |= D3D11_BIND_SHADER_RESOURCE;
        }

        if ((supported & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ==
            D3D11_FORMAT_SUPPORT_RENDER_TARGET) {
            bind_flags |= D3D11_BIND_RENDER_TARGET;
        }
    }

    if (gst_query_get_n_allocation_pools(query) > 0) {
        gst_query_parse_nth_allocation_pool(query, 0, &pool, &size, &min, &max);
        if (pool) {
            if (!GST_IS_D3D11_BUFFER_POOL(pool)) {
                gst_clear_object(&pool);
            }
            else {
                GstD3D11BufferPool* dpool = GST_D3D11_BUFFER_POOL(pool);
                if (dpool->device != filter->device)
                    gst_clear_object(&pool);
            }
        }

        update_pool = TRUE;
    }
    else {
        size = GST_VIDEO_INFO_SIZE(&vinfo);
        min = max = 0;
    }

    if (!pool) {
        GST_DEBUG_OBJECT(trans, "create our pool");

        pool = gst_d3d11_buffer_pool_new(filter->device);
    }

    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_set_params(config, outcaps, size, min, max);

    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params(config);
    if (!d3d11_params) {
        d3d11_params = gst_d3d11_allocation_params_new(filter->device, &vinfo,
            (GstD3D11AllocationFlags)0, bind_flags);
    }
    else {
        /* Set bind flag */
        for (i = 0; i < GST_VIDEO_INFO_N_PLANES(&vinfo); i++) {
            d3d11_params->desc[i].BindFlags |= bind_flags;
        }
    }

    gst_buffer_pool_config_set_d3d11_allocation_params(config, d3d11_params);
    gst_d3d11_allocation_params_free(d3d11_params);

    gst_buffer_pool_set_config(pool, config);

    /* d3d11 buffer pool will update buffer size based on allocated texture,
     * get size from config again */
    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_get_params(config, nullptr, &size, nullptr, nullptr);
    gst_structure_free(config);

    if (update_pool)
        gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
    else
        gst_query_add_allocation_pool(query, pool, size, min, max);

    gst_object_unref(pool);

    return GST_BASE_TRANSFORM_CLASS(parent_class)->decide_allocation(trans,
        query);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_d3d11crop_init (Gstd3d11crop * filter)
{
    filter->left   = 0;
    filter->right  = 0;
    filter->top    = 0;
    filter->bottom = 0;
    filter->need_update = FALSE;
}

static inline void
gst_video_crop_set_crop(Gstd3d11crop* filter, gint new_value, gint* prop)
{
    if (*prop != new_value) {
        //Property needs to be even
        *prop = new_value - (new_value%2);
        filter->need_update = TRUE;
    }
}

static void
gst_d3d11crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstd3d11crop *filter = GST_D3D11CROP (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_LEFT:
      gst_video_crop_set_crop(filter, g_value_get_int(value),
            &filter->left);
        break;
    case PROP_RIGHT:
        gst_video_crop_set_crop(filter, g_value_get_int(value),
            &filter->right);
        break;
    case PROP_TOP:
        gst_video_crop_set_crop(filter, g_value_get_int(value),
            &filter->top);
        break;
    case PROP_BOTTOM:
        gst_video_crop_set_crop(filter, g_value_get_int(value),
            &filter->bottom);
        break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(filter));
}

static void
gst_d3d11crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstd3d11crop *filter = GST_D3D11CROP (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_LEFT:
        g_value_set_int(value, filter->left);
        break;
    case PROP_RIGHT:
        g_value_set_int(value, filter->right);        
        break;
    case PROP_TOP:
        g_value_set_int(value, filter->top);
        break;
    case PROP_BOTTOM:
        g_value_set_int(value, filter->bottom);
        break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_d3d11crop_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  Gstd3d11crop *filter;
  gboolean ret;

  filter = GST_D3D11CROP (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      /* do something with the caps */

      /* and forward */
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

