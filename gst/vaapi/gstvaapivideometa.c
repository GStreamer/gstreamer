/*
 *  gstvaapivideometa.c - Gst VA video meta
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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

/**
 * SECTION:gstvaapivideometa
 * @short_description: VA video meta for GStreamer
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapiimagepool.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include "gstvaapivideometa.h"

#define GST_VAAPI_TYPE_VIDEO_META \
    (gst_vaapi_video_meta_get_type())

#define GST_VAAPI_VIDEO_META(obj) \
    ((GstVaapiVideoMeta *)(obj))

#define GST_VAAPI_IS_VIDEO_META(obj) \
    (GST_VAAPI_VIDEO_META(obj) != NULL)

struct _GstVaapiVideoMeta {
    gint                        ref_count;
    GstVaapiDisplay            *display;
    GstVaapiVideoPool          *image_pool;
    GstVaapiImage              *image;
    GstVaapiVideoPool          *surface_pool;
    GstVaapiSurface            *surface;
    GstVaapiSurfaceProxy       *proxy;
    GFunc                       converter;
    guint                       render_flags;
};

static void
set_display(GstVaapiVideoMeta *meta, GstVaapiDisplay *display)
{
    g_clear_object(&meta->display);

    if (display)
        meta->display = g_object_ref(display);
}

static inline void
set_image(GstVaapiVideoMeta *meta, GstVaapiImage *image)
{
    meta->image = g_object_ref(image);
    set_display(meta, gst_vaapi_object_get_display(GST_VAAPI_OBJECT(image)));
}

static inline void
set_surface(GstVaapiVideoMeta *meta, GstVaapiSurface *surface)
{
    meta->surface = g_object_ref(surface);
    set_display(meta, gst_vaapi_object_get_display(GST_VAAPI_OBJECT(surface)));
}

static void
gst_vaapi_video_meta_destroy_image(GstVaapiVideoMeta *meta)
{
    if (meta->image) {
        if (meta->image_pool)
            gst_vaapi_video_pool_put_object(meta->image_pool, meta->image);
        g_object_unref(meta->image);
        meta->image = NULL;
    }
    g_clear_object(&meta->image_pool);
}

static void
gst_vaapi_video_meta_destroy_surface(GstVaapiVideoMeta *meta)
{
    gst_vaapi_surface_proxy_replace(&meta->proxy, NULL);

    if (meta->surface) {
        if (meta->surface_pool)
            gst_vaapi_video_pool_put_object(meta->surface_pool, meta->surface);
        g_object_unref(meta->surface);
        meta->surface = NULL;
    }
    g_clear_object(&meta->surface_pool);
}

static GType
gst_vaapi_video_meta_get_type(void)
{
    static gsize g_type;

    if (g_once_init_enter(&g_type)) {
        GType type;
        type = g_boxed_type_register_static("GstVaapiVideoMeta",
            (GBoxedCopyFunc)gst_vaapi_video_meta_ref,
            (GBoxedFreeFunc)gst_vaapi_video_meta_unref);
        g_once_init_leave(&g_type, type);
    }
    return (GType)g_type;
}

static void
gst_vaapi_video_meta_finalize(GstVaapiVideoMeta *meta)
{
    gst_vaapi_video_meta_destroy_image(meta);
    gst_vaapi_video_meta_destroy_surface(meta);
    g_clear_object(&meta->display);
}

static void
gst_vaapi_video_meta_init(GstVaapiVideoMeta *meta)
{
    meta->ref_count     = 1;
    meta->display       = NULL;
    meta->image_pool    = NULL;
    meta->image         = NULL;
    meta->surface_pool  = NULL;
    meta->surface       = NULL;
    meta->proxy         = NULL;
    meta->converter     = NULL;
    meta->render_flags  = 0;
}

static inline GstVaapiVideoMeta *
_gst_vaapi_video_meta_new(void)
{
    GstVaapiVideoMeta *meta;

    meta = g_slice_alloc(sizeof(*meta));
    if (!meta)
        return NULL;
    gst_vaapi_video_meta_init(meta);
    return meta;
}

static inline void
_gst_vaapi_video_meta_free(GstVaapiVideoMeta *meta)
{
    g_atomic_int_inc(&meta->ref_count);

    gst_vaapi_video_meta_finalize(meta);

    if (G_LIKELY(g_atomic_int_dec_and_test(&meta->ref_count)))
        g_slice_free1(sizeof(*meta), meta);
}

/**
 * gst_vaapi_video_meta_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates an empty #GstVaapiVideoMeta. The caller is responsible for completing
 * the initialization of the meta with the gst_vaapi_video_meta_set_*()
 * functions.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstVaapiVideoMeta, or %NULL or error
 */
GstVaapiVideoMeta *
gst_vaapi_video_meta_new(GstVaapiDisplay *display)
{
    GstVaapiVideoMeta *meta;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    meta = _gst_vaapi_video_meta_new();
    if (G_UNLIKELY(!meta))
        return NULL;

    set_display(meta, display);
    return meta;
}

/**
 * gst_vaapi_video_meta_new_from_pool:
 * @pool: a #GstVaapiVideoPool
 *
 * Creates a #GstVaapiVideoMeta with a video object allocated from a @pool.
 * Only #GstVaapiSurfacePool and #GstVaapiImagePool pools are supported.
 *
 * The meta object is destroyed through the last call to
 * gst_vaapi_video_meta_unref() and the video objects are pushed back
 * to their respective pools.
 *
 * Return value: the newly allocated #GstVaapiVideoMeta, or %NULL on error
 */
GstVaapiVideoMeta *
gst_vaapi_video_meta_new_from_pool(GstVaapiVideoPool *pool)
{
    GstVaapiVideoMeta *meta;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    meta = _gst_vaapi_video_meta_new();
    if (G_UNLIKELY(!meta))
        return NULL;

    if (!(GST_VAAPI_IS_IMAGE_POOL(pool) &&
          gst_vaapi_video_meta_set_image_from_pool(meta, pool)) &&
        !(GST_VAAPI_IS_SURFACE_POOL(pool) &&
          gst_vaapi_video_meta_set_surface_from_pool(meta, pool)))
        goto error;

    set_display(meta, gst_vaapi_video_pool_get_display(pool));
    return meta;

error:
    gst_vaapi_video_meta_unref(meta);
    return NULL;
}

/**
 * gst_vaapi_video_meta_new_with_image:
 * @image: a #GstVaapiImage
 *
 * Creates a #GstVaapiVideoMeta with the specified @image. The resulting
 * meta holds an additional reference to the @image.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstVaapiVideoMeta, or %NULL on error
 */
GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_image(GstVaapiImage *image)
{
    GstVaapiVideoMeta *meta;

    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);

    meta = _gst_vaapi_video_meta_new();
    if (G_UNLIKELY(!meta))
        return NULL;

    gst_vaapi_video_meta_set_image(meta, image);
    return meta;
}

/**
 * gst_vaapi_video_meta_new_with_surface:
 * @surface: a #GstVaapiSurface
 *
 * Creates a #GstVaapiVideoMeta with the specified @surface. The resulting
 * meta holds an additional reference to the @surface.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstVaapiVideoMeta, or %NULL on error
 */
GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_surface(GstVaapiSurface *surface)
{
    GstVaapiVideoMeta *meta;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    meta = _gst_vaapi_video_meta_new();
    if (G_UNLIKELY(!meta))
        return NULL;

    gst_vaapi_video_meta_set_surface(meta, surface);
    return meta;
}

/**
 * gst_vaapi_video_meta_new_with_surface_proxy:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Creates a #GstVaapiVideoMeta with the specified surface @proxy. The
 * resulting meta holds an additional reference to the @proxy.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstVaapiVideoMeta, or %NULL on error
 */
GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_surface_proxy(GstVaapiSurfaceProxy *proxy)
{
    GstVaapiVideoMeta *meta;

    g_return_val_if_fail(proxy != NULL, NULL);

    meta = _gst_vaapi_video_meta_new();
    if (G_UNLIKELY(!meta))
        return NULL;

    gst_vaapi_video_meta_set_surface_proxy(meta, proxy);
    return meta;
}

/**
 * gst_vaapi_video_meta_ref:
 * @meta: a #GstVaapiVideoMeta
 *
 * Atomically increases the reference count of the given @meta by one.
 *
 * Returns: The same @meta argument
 */
GstVaapiVideoMeta *
gst_vaapi_video_meta_ref(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(meta != NULL, NULL);

    g_atomic_int_inc(&meta->ref_count);
    return meta;
}

/**
 * gst_vaapi_video_meta_unref:
 * @meta: a #GstVaapiVideoMeta
 *
 * Atomically decreases the reference count of the @meta by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_video_meta_unref(GstVaapiVideoMeta *meta)
{
    g_return_if_fail(meta != NULL);
    g_return_if_fail(meta->ref_count > 0);

    if (g_atomic_int_dec_and_test(&meta->ref_count))
        _gst_vaapi_video_meta_free(meta);
}

/**
 * gst_vaapi_video_meta_replace:
 * @old_meta_ptr: a pointer to a #GstVaapiVideoMeta
 * @new_meta: a #GstVaapiVideoMeta
 *
 * @new_meta. This means that @old_meta_ptr shall reference a valid
 * Atomically replaces the meta object held in @old_meta_ptr with
 * object. However, @new_meta can be NULL.
 */
void
gst_vaapi_video_meta_replace(GstVaapiVideoMeta **old_meta_ptr,
    GstVaapiVideoMeta *new_meta)
{
    GstVaapiVideoMeta *old_meta;

    g_return_if_fail(old_meta_ptr != NULL);

    old_meta = g_atomic_pointer_get((gpointer *)old_meta_ptr);

    if (old_meta == new_meta)
        return;

    if (new_meta)
        gst_vaapi_video_meta_ref(new_meta);

    while (!g_atomic_pointer_compare_and_exchange((gpointer *)old_meta_ptr,
               old_meta, new_meta))
        old_meta = g_atomic_pointer_get((gpointer *)old_meta_ptr);

    if (old_meta)
        gst_vaapi_video_meta_unref(old_meta);
}

/**
 * gst_vaapi_video_meta_get_display:
 * @meta: a #GstVaapiVideoMeta
 *
 * Retrieves the #GstVaapiDisplay the @meta is bound to. The @meta
 * owns the returned #GstVaapiDisplay object so the caller is
 * responsible for calling g_object_ref() when needed.
 *
 * Return value: the #GstVaapiDisplay the @meta is bound to
 */
GstVaapiDisplay *
gst_vaapi_video_meta_get_display(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), NULL);

    return meta->display;
}

/**
 * gst_vaapi_video_meta_get_image:
 * @meta: a #GstVaapiVideoMeta
 *
 * Retrieves the #GstVaapiImage bound to the @meta. The @meta owns
 * the #GstVaapiImage so the caller is responsible for calling
 * g_object_ref() when needed.
 *
 * Return value: the #GstVaapiImage bound to the @meta, or %NULL if
 *   there is none
 */
GstVaapiImage *
gst_vaapi_video_meta_get_image(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), NULL);

    return meta->image;
}

/**
 * gst_vaapi_video_meta_set_image:
 * @meta: a #GstVaapiVideoMeta
 * @image: a #GstVaapiImage
 *
 * Binds @image to the @meta. If the @meta contains another image
 * previously allocated from a pool, it's pushed back to its parent
 * pool and the pool is also released.
 */
void
gst_vaapi_video_meta_set_image(GstVaapiVideoMeta *meta, GstVaapiImage *image)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_META(meta));
    g_return_if_fail(GST_VAAPI_IS_IMAGE(image));

    gst_vaapi_video_meta_destroy_image(meta);

    if (image)
        set_image(meta, image);
}

/**
 * gst_vaapi_video_meta_set_image_from_pool
 * @meta: a #GstVaapiVideoMeta
 * @pool: a #GstVaapiVideoPool
 *
 * Binds a newly allocated video object from the @pool. The @pool
 * shall be of type #GstVaapiImagePool. Previously allocated objects
 * are released and returned to their parent pools, if any.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_video_meta_set_image_from_pool(GstVaapiVideoMeta *meta,
    GstVaapiVideoPool *pool)
{
    GstVaapiImage *image;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE_POOL(pool), FALSE);

    gst_vaapi_video_meta_destroy_image(meta);

    if (pool) {
        image = gst_vaapi_video_pool_get_object(pool);
        if (!image)
            return FALSE;
        set_image(meta, image);
        meta->image_pool = g_object_ref(pool);
    }
    return TRUE;
}

/**
 * gst_vaapi_video_meta_get_surface:
 * @meta: a #GstVaapiVideoMeta
 *
 * Retrieves the #GstVaapiSurface bound to the @meta. The @meta
 * owns the #GstVaapiSurface so the caller is responsible for calling
 * g_object_ref() when needed.
 *
 * Return value: the #GstVaapiSurface bound to the @meta, or %NULL if
 *   there is none
 */
GstVaapiSurface *
gst_vaapi_video_meta_get_surface(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), NULL);

    return meta->surface;
}

/**
 * gst_vaapi_video_meta_set_surface:
 * @meta: a #GstVaapiVideoMeta
 * @surface: a #GstVaapiSurface
 *
 * Binds @surface to the @meta. If the @meta contains another
 * surface previously allocated from a pool, it's pushed back to its
 * parent pool and the pool is also released.
 */
void
gst_vaapi_video_meta_set_surface(GstVaapiVideoMeta *meta,
    GstVaapiSurface *surface)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_META(meta));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    gst_vaapi_video_meta_destroy_surface(meta);

    if (surface)
        set_surface(meta, surface);
}

/**
 * gst_vaapi_video_meta_set_surface_from_pool
 * @meta: a #GstVaapiVideoMeta
 * @pool: a #GstVaapiVideoPool
 *
 * Binds a newly allocated video object from the @pool. The @pool
 * shall be of type #GstVaapiSurfacePool. Previously allocated objects
 * are released and returned to their parent pools, if any.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_video_meta_set_surface_from_pool(GstVaapiVideoMeta *meta,
    GstVaapiVideoPool *pool)
{
    GstVaapiSurface *surface;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool), FALSE);

    gst_vaapi_video_meta_destroy_surface(meta);

    if (pool) {
        surface = gst_vaapi_video_pool_get_object(pool);
        if (!surface)
            return FALSE;
        set_surface(meta, surface);
        meta->surface_pool = g_object_ref(pool);
    }
    return TRUE;
}

/**
 * gst_vaapi_video_meta_get_surface_proxy:
 * @meta: a #GstVaapiVideoMeta
 *
 * Retrieves the #GstVaapiSurfaceProxy bound to the @meta. The @meta
 * owns the #GstVaapiSurfaceProxy so the caller is responsible for calling
 * g_object_ref() when needed.
 *
 * Return value: the #GstVaapiSurfaceProxy bound to the @meta, or
 *   %NULL if there is none
 */
GstVaapiSurfaceProxy *
gst_vaapi_video_meta_get_surface_proxy(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), NULL);

    return meta->proxy;
}

/**
 * gst_vaapi_video_meta_set_surface_proxy:
 * @meta: a #GstVaapiVideoMeta
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Binds surface @proxy to the @meta. If the @meta contains another
 * surface previously allocated from a pool, it's pushed back to its
 * parent pool and the pool is also released.
 */
void
gst_vaapi_video_meta_set_surface_proxy(GstVaapiVideoMeta *meta,
    GstVaapiSurfaceProxy *proxy)
{
    GstVaapiSurface *surface;

    g_return_if_fail(GST_VAAPI_IS_VIDEO_META(meta));
    g_return_if_fail(proxy != NULL);

    gst_vaapi_video_meta_destroy_surface(meta);

    if (proxy) {
        surface = GST_VAAPI_SURFACE_PROXY_SURFACE(proxy);
        if (!surface)
            return;
        set_surface(meta, surface);
        meta->proxy = gst_vaapi_surface_proxy_ref(proxy);
    }
}

/**
 * gst_vaapi_video_meta_get_surface_converter:
 * @meta: a #GstVaapiVideoMeta
 *
 * Retrieves the surface converter bound to the @meta.
 *
 * Return value: the surface converter associated with the video @meta
 */
GFunc
gst_vaapi_video_meta_get_surface_converter(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), NULL);

    return meta->converter;
}

/**
 * gst_vaapi_video_meta_set_surface_converter:
 * @meta: a #GstVaapiVideoMeta
 * @func: a pointer to the surface converter function
 *
 * Sets the @meta surface converter function to @func.
 */
void
gst_vaapi_video_meta_set_surface_converter(GstVaapiVideoMeta *meta, GFunc func)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_META(meta));

    meta->converter = func;
}

/**
 * gst_vaapi_video_meta_get_render_flags:
 * @meta: a #GstVaapiVideoMeta
 *
 * Retrieves the surface render flags bound to the @meta.
 *
 * Return value: a combination for #GstVaapiSurfaceRenderFlags
 */
guint
gst_vaapi_video_meta_get_render_flags(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_META(meta), 0);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(meta->surface), 0);

    return meta->render_flags;
}

/**
 * gst_vaapi_video_meta_set_render_flags:
 * @meta: a #GstVaapiVideoMeta
 * @flags: a set of surface render flags
 *
 * Sets #GstVaapiSurfaceRenderFlags to the @meta.
 */
void
gst_vaapi_video_meta_set_render_flags(GstVaapiVideoMeta *meta, guint flags)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_META(meta));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(meta->surface));

    meta->render_flags = flags;
}

#define GST_VAAPI_VIDEO_META_QUARK gst_vaapi_video_meta_quark_get()
static GQuark
gst_vaapi_video_meta_quark_get(void)
{
    static gsize g_quark;

    if (g_once_init_enter(&g_quark)) {
        gsize quark = (gsize)g_quark_from_static_string("GstVaapiVideoMeta");
        g_once_init_leave(&g_quark, quark);
    }
    return g_quark;
}

#define META_QUARK meta_quark_get()
static GQuark
meta_quark_get(void)
{
    static gsize g_quark;

    if (g_once_init_enter(&g_quark)) {
        gsize quark = (gsize)g_quark_from_static_string("meta");
        g_once_init_leave(&g_quark, quark);
    }
    return g_quark;
}

GstVaapiVideoMeta *
gst_buffer_get_vaapi_video_meta(GstBuffer *buffer)
{
    const GstStructure *structure;
    const GValue *value;

    g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);

    structure = gst_buffer_get_qdata(buffer, GST_VAAPI_VIDEO_META_QUARK);
    if (!structure)
        return NULL;

    value = gst_structure_id_get_value(structure, META_QUARK);
    if (!value)
        return NULL;

    return GST_VAAPI_VIDEO_META(g_value_get_boxed(value));
}

void
gst_buffer_set_vaapi_video_meta(GstBuffer *buffer, GstVaapiVideoMeta *meta)
{
    g_return_if_fail(GST_IS_BUFFER(buffer));
    g_return_if_fail(GST_VAAPI_IS_VIDEO_META(meta));

    gst_buffer_set_qdata(buffer, GST_VAAPI_VIDEO_META_QUARK,
        gst_structure_id_new(GST_VAAPI_VIDEO_META_QUARK,
            META_QUARK, GST_VAAPI_TYPE_VIDEO_META, meta, NULL));
}
