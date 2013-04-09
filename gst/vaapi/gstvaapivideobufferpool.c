/*
 *  gstvaapivideobufferpool.c - Gstreamer/VA video buffer pool
 *
 *  Copyright (C) 2013 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapivideomemory.h"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapivideopool);
#define GST_CAT_DEFAULT gst_debug_vaapivideopool

G_DEFINE_TYPE(GstVaapiVideoBufferPool,
              gst_vaapi_video_buffer_pool,
              GST_TYPE_BUFFER_POOL)

enum {
    PROP_0,

    PROP_DISPLAY,
};

struct _GstVaapiVideoBufferPoolPrivate {
    GstAllocator       *allocator;
    GstVaapiDisplay    *display;
    guint               has_video_meta  : 1;
};

#define GST_VAAPI_VIDEO_BUFFER_POOL_GET_PRIVATE(obj)    \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                 \
        GST_VAAPI_TYPE_VIDEO_BUFFER_POOL,               \
        GstVaapiVideoBufferPoolPrivate))

static void
gst_vaapi_video_buffer_pool_finalize(GObject *object)
{
    GstVaapiVideoBufferPoolPrivate * const priv =
        GST_VAAPI_VIDEO_BUFFER_POOL(object)->priv;

    G_OBJECT_CLASS(gst_vaapi_video_buffer_pool_parent_class)->finalize(object);

    g_clear_object(&priv->display);
    g_clear_object(&priv->allocator);
}

static void
gst_vaapi_video_buffer_pool_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
    GstVaapiVideoBufferPoolPrivate * const priv =
        GST_VAAPI_VIDEO_BUFFER_POOL(object)->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        priv->display = g_object_ref(g_value_get_object(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_video_buffer_pool_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
    GstVaapiVideoBufferPoolPrivate * const priv =
        GST_VAAPI_VIDEO_BUFFER_POOL(object)->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, priv->display);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static const gchar **
gst_vaapi_video_buffer_pool_get_options(GstBufferPool *pool)
{
    static const gchar *g_options[] = {
        GST_BUFFER_POOL_OPTION_VIDEO_META,
        GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META,
        NULL,
    };
    return g_options;
}

static gboolean
gst_vaapi_video_buffer_pool_set_config(GstBufferPool *pool,
    GstStructure *config)
{
    GstVaapiVideoBufferPoolPrivate * const priv =
        GST_VAAPI_VIDEO_BUFFER_POOL(pool)->priv;
    GstCaps *caps = NULL;

    if (!gst_buffer_pool_config_get_params(config, &caps, NULL, NULL, NULL))
        goto error_invalid_config;
    if (!caps)
        goto error_no_caps;

    g_clear_object(&priv->allocator);
    priv->allocator = gst_vaapi_video_allocator_new(priv->display, caps);
    if (!priv->allocator)
        goto error_create_allocator;

    if (!gst_buffer_pool_config_has_option(config,
            GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META))
        goto error_no_vaapi_video_meta_option;

    priv->has_video_meta = gst_buffer_pool_config_has_option(config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    return GST_BUFFER_POOL_CLASS(gst_vaapi_video_buffer_pool_parent_class)->
        set_config(pool, config);

    /* ERRORS */
error_invalid_config:
    {
        GST_ERROR("invalid config");
        return FALSE;
    }
error_no_caps:
    {
        GST_ERROR("no caps in config");
        return FALSE;
    }
error_create_allocator:
    {
        GST_ERROR("failed to create GstVaapiVideoAllocator object");
        return FALSE;
    }
error_no_vaapi_video_meta_option:
    {
        GST_ERROR("no GstVaapiVideoMeta option");
        return FALSE;
    }
}

static GstFlowReturn
gst_vaapi_video_buffer_pool_alloc_buffer(GstBufferPool *pool,
    GstBuffer **out_buffer_ptr, GstBufferPoolAcquireParams *params)
{
    GstVaapiVideoBufferPoolPrivate * const priv =
        GST_VAAPI_VIDEO_BUFFER_POOL(pool)->priv;
    GstVaapiVideoMeta *meta;
    GstMemory *mem;
    GstBuffer *buffer;

    if (!priv->allocator)
        goto error_no_allocator;

    meta = gst_vaapi_video_meta_new(priv->display);
    if (!meta)
        goto error_create_meta;

    buffer = gst_vaapi_video_buffer_new(meta);
    if (!buffer)
        goto error_create_buffer;

    mem = gst_vaapi_video_memory_new(priv->allocator, meta);
    if (!mem)
        goto error_create_memory;
    gst_vaapi_video_meta_unref(meta);
    gst_buffer_append_memory(buffer, mem);

    if (priv->has_video_meta) {
        GstVideoInfo * const vip =
            &GST_VAAPI_VIDEO_ALLOCATOR_CAST(priv->allocator)->image_info;
        GstVideoMeta *vmeta;

        vmeta = gst_buffer_add_video_meta_full(buffer, 0,
            GST_VIDEO_INFO_FORMAT(vip), GST_VIDEO_INFO_WIDTH(vip),
            GST_VIDEO_INFO_HEIGHT(vip), GST_VIDEO_INFO_N_PLANES(vip),
            &GST_VIDEO_INFO_PLANE_OFFSET(vip, 0),
            &GST_VIDEO_INFO_PLANE_STRIDE(vip, 0));
        vmeta->map = gst_video_meta_map_vaapi_memory;
        vmeta->unmap = gst_video_meta_unmap_vaapi_memory;
    }

    *out_buffer_ptr = buffer;
    return GST_FLOW_OK;

    /* ERRORS */
error_no_allocator:
    {
        GST_ERROR("no GstAllocator in buffer pool");
        return GST_FLOW_ERROR;
    }
error_create_meta:
    {
        GST_ERROR("failed to allocate vaapi video meta");
        return GST_FLOW_ERROR;
    }
error_create_buffer:
    {
        GST_ERROR("failed to create video buffer");
        gst_vaapi_video_meta_unref(meta);
        return GST_FLOW_ERROR;
    }
error_create_memory:
    {
        GST_ERROR("failed to create video memory");
        gst_buffer_unref(buffer);
        gst_vaapi_video_meta_unref(meta);
        return GST_FLOW_ERROR;
    }
}

static void
gst_vaapi_video_buffer_pool_reset_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
    GstVaapiVideoMeta * const meta = gst_buffer_get_vaapi_video_meta(buffer);

    /* Release the underlying surface proxy */
    gst_vaapi_video_meta_set_surface_proxy(meta, NULL);

    GST_BUFFER_POOL_CLASS(gst_vaapi_video_buffer_pool_parent_class)->
        reset_buffer(pool, buffer);
}

static void
gst_vaapi_video_buffer_pool_class_init(GstVaapiVideoBufferPoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstBufferPoolClass * const pool_class = GST_BUFFER_POOL_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapivideopool,
        "vaapivideopool", 0, "VA-API video pool");

    g_type_class_add_private(klass, sizeof(GstVaapiVideoBufferPoolPrivate));

    object_class->finalize      = gst_vaapi_video_buffer_pool_finalize;
    object_class->set_property  = gst_vaapi_video_buffer_pool_set_property;
    object_class->get_property  = gst_vaapi_video_buffer_pool_get_property;
    pool_class->get_options     = gst_vaapi_video_buffer_pool_get_options;
    pool_class->set_config      = gst_vaapi_video_buffer_pool_set_config;
    pool_class->alloc_buffer    = gst_vaapi_video_buffer_pool_alloc_buffer;
    pool_class->reset_buffer    = gst_vaapi_video_buffer_pool_reset_buffer;

    /**
     * GstVaapiVideoBufferPool:display:
     *
     * The #GstVaapiDisplay this object is bound to.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay to use for this video pool",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_video_buffer_pool_init(GstVaapiVideoBufferPool *pool)
{
    GstVaapiVideoBufferPoolPrivate * const priv =
        GST_VAAPI_VIDEO_BUFFER_POOL_GET_PRIVATE(pool);

    pool->priv = priv;
}

GstBufferPool *
gst_vaapi_video_buffer_pool_new(GstVaapiDisplay *display)
{
    return g_object_new(GST_VAAPI_TYPE_VIDEO_BUFFER_POOL,
        "display", display, NULL);
}
