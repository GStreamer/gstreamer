/*
 *  gstvaapicontext.c - VA context abstraction
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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
 * SECTION:gstvaapicontext
 * @short_description: VA context abstraction
 */

#include "config.h"
#include <assert.h>
#include "gstvaapicompat.h"
#include "gstvaapicontext.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapiutils.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiContext, gst_vaapi_context, GST_VAAPI_TYPE_OBJECT);

#define GST_VAAPI_CONTEXT_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_CONTEXT,	\
                                 GstVaapiContextPrivate))

/* XXX: optimize for the effective number of reference frames */
struct _GstVaapiContextPrivate {
    VAConfigID          config_id;
    GPtrArray          *surfaces;
    GstVaapiVideoPool  *surfaces_pool;
    GstVaapiProfile     profile;
    GstVaapiEntrypoint  entrypoint;
    guint               width;
    guint               height;
    guint               is_constructed  : 1;
};

enum {
    PROP_0,

    PROP_PROFILE,
    PROP_ENTRYPOINT,
    PROP_WIDTH,
    PROP_HEIGHT
};

static void
unref_surface_cb(gpointer data, gpointer user_data)
{
    g_object_unref(GST_VAAPI_SURFACE(data));
}

static void
gst_vaapi_context_destroy_surfaces(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;

    if (priv->surfaces) {
        g_ptr_array_foreach(priv->surfaces, unref_surface_cb, NULL);
        g_ptr_array_free(priv->surfaces, TRUE);
        priv->surfaces = NULL;
    }

    if (priv->surfaces_pool) {
        g_object_unref(priv->surfaces_pool);
        priv->surfaces_pool = NULL;
    }
}

static void
gst_vaapi_context_destroy(GstVaapiContext *context)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(context);
    GstVaapiContextPrivate * const priv = context->priv;
    VAContextID context_id;
    VAStatus status;

    context_id = GST_VAAPI_OBJECT_ID(context);
    GST_DEBUG("context %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(context_id));

    if (context_id != VA_INVALID_ID) {
        GST_VAAPI_DISPLAY_LOCK(display);
        status = vaDestroyContext(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            context_id
        );
        GST_VAAPI_DISPLAY_UNLOCK(display);
        if (!vaapi_check_status(status, "vaDestroyContext()"))
            g_warning("failed to destroy context %" GST_VAAPI_ID_FORMAT,
                      GST_VAAPI_ID_ARGS(context_id));
        GST_VAAPI_OBJECT_ID(context) = VA_INVALID_ID;
    }

    if (priv->config_id != VA_INVALID_ID) {
        GST_VAAPI_DISPLAY_LOCK(display);
        status = vaDestroyConfig(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            priv->config_id
        );
        GST_VAAPI_DISPLAY_UNLOCK(display);
        if (!vaapi_check_status(status, "vaDestroyConfig()"))
            g_warning("failed to destroy config %" GST_VAAPI_ID_FORMAT,
                      GST_VAAPI_ID_ARGS(priv->config_id));
        priv->config_id = VA_INVALID_ID;
    }
}

static gboolean
gst_vaapi_context_create_surfaces(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;
    GstCaps *caps;
    GstVaapiSurface *surface;
    guint i, num_ref_frames, num_surfaces;

    /* Number of scratch surfaces beyond those used as reference */
    const guint SCRATCH_SURFACES_COUNT = 4;

    if (!priv->surfaces) {
        priv->surfaces = g_ptr_array_new();
        if (!priv->surfaces)
            return FALSE;
    }

    if (!priv->surfaces_pool) {
        caps = gst_caps_new_simple(
            "video/x-vaapi-surface",
            "width",  G_TYPE_INT, priv->width,
            "height", G_TYPE_INT, priv->height,
            NULL
        );
        if (!caps)
            return FALSE;
        priv->surfaces_pool = gst_vaapi_surface_pool_new(
            GST_VAAPI_OBJECT_DISPLAY(context),
            caps
        );
        gst_caps_unref(caps);
        if (!priv->surfaces_pool)
            return FALSE;
    }

    num_ref_frames = 2;
    if (gst_vaapi_profile_get_codec(priv->profile) == GST_VAAPI_CODEC_H264)
        num_ref_frames = 16;
    num_surfaces = num_ref_frames + SCRATCH_SURFACES_COUNT;

    gst_vaapi_video_pool_set_capacity(priv->surfaces_pool, num_surfaces);

    for (i = priv->surfaces->len; i < num_surfaces; i++) {
        surface = gst_vaapi_surface_new(
            GST_VAAPI_OBJECT_DISPLAY(context),
            GST_VAAPI_CHROMA_TYPE_YUV420,
            priv->width, priv->height
        );
        if (!surface)
            return FALSE;
        g_ptr_array_add(priv->surfaces, surface);
        if (!gst_vaapi_video_pool_add_object(priv->surfaces_pool, surface))
            return FALSE;
    }
    return TRUE;
}

static gboolean
gst_vaapi_context_create(GstVaapiContext *context)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(context);
    GstVaapiContextPrivate * const priv = context->priv;
    VAProfile va_profile;
    VAEntrypoint va_entrypoint;
    VAConfigAttrib attrib;
    VAContextID context_id;
    VASurfaceID surface_id;
    VAStatus status;
    GArray *surfaces = NULL;
    gboolean success = FALSE;
    guint i;

    if (!priv->surfaces && !gst_vaapi_context_create_surfaces(context))
        goto end;

    surfaces = g_array_sized_new(
        FALSE,
        FALSE,
        sizeof(VASurfaceID),
        priv->surfaces->len
    );
    if (!surfaces)
        goto end;

    for (i = 0; i < priv->surfaces->len; i++) {
        GstVaapiSurface * const surface = g_ptr_array_index(priv->surfaces, i);
        if (!surface)
            goto end;
        surface_id = GST_VAAPI_OBJECT_ID(surface);
        g_array_append_val(surfaces, surface_id);
    }
    assert(surfaces->len == priv->surfaces->len);

    if (!priv->profile || !priv->entrypoint)
        goto end;
    va_profile    = gst_vaapi_profile_get_va_profile(priv->profile);
    va_entrypoint = gst_vaapi_entrypoint_get_va_entrypoint(priv->entrypoint);

    GST_VAAPI_DISPLAY_LOCK(display);
    attrib.type = VAConfigAttribRTFormat;
    status = vaGetConfigAttributes(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        va_profile,
        va_entrypoint,
        &attrib, 1
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaGetConfigAttributes()"))
        goto end;
    if (!(attrib.value & VA_RT_FORMAT_YUV420))
        goto end;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateConfig(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        va_profile,
        va_entrypoint,
        &attrib, 1,
        &priv->config_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateConfig()"))
        goto end;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateContext(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        priv->config_id,
        priv->width, priv->height,
        VA_PROGRESSIVE,
        (VASurfaceID *)surfaces->data, surfaces->len,
        &context_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateContext()"))
        goto end;

    GST_DEBUG("context %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(context_id));
    GST_VAAPI_OBJECT_ID(context) = context_id;
    success = TRUE;
end:
    if (surfaces)
        g_array_free(surfaces, TRUE);
    return success;
}

static void
gst_vaapi_context_finalize(GObject *object)
{
    GstVaapiContext * const context = GST_VAAPI_CONTEXT(object);

    gst_vaapi_context_destroy(context);
    gst_vaapi_context_destroy_surfaces(context);

    G_OBJECT_CLASS(gst_vaapi_context_parent_class)->finalize(object);
}

static void
gst_vaapi_context_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiContext        * const context = GST_VAAPI_CONTEXT(object);
    GstVaapiContextPrivate * const priv    = context->priv;

    switch (prop_id) {
    case PROP_PROFILE:
        gst_vaapi_context_set_profile(context, g_value_get_uint(value));
        break;
    case PROP_ENTRYPOINT:
        priv->entrypoint = g_value_get_uint(value);
        break;
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_context_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiContext        * const context = GST_VAAPI_CONTEXT(object);
    GstVaapiContextPrivate * const priv    = context->priv;

    switch (prop_id) {
    case PROP_PROFILE:
        g_value_set_uint(value, gst_vaapi_context_get_profile(context));
        break;
    case PROP_ENTRYPOINT:
        g_value_set_uint(value, gst_vaapi_context_get_entrypoint(context));
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, priv->height);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_context_class_init(GstVaapiContextClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiContextPrivate));

    object_class->finalize     = gst_vaapi_context_finalize;
    object_class->set_property = gst_vaapi_context_set_property;
    object_class->get_property = gst_vaapi_context_get_property;

    g_object_class_install_property
        (object_class,
         PROP_PROFILE,
         g_param_spec_uint("profile",
                           "Profile",
                           "The profile used for decoding",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class,
         PROP_ENTRYPOINT,
         g_param_spec_uint("entrypoint",
                           "Entrypoint",
                           "The decoder entrypoint",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "The width of decoded surfaces",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "Height",
                           "The height of the decoded surfaces",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_context_init(GstVaapiContext *context)
{
    GstVaapiContextPrivate *priv = GST_VAAPI_CONTEXT_GET_PRIVATE(context);

    context->priv       = priv;
    priv->config_id     = VA_INVALID_ID;
    priv->surfaces      = NULL;
    priv->surfaces_pool = NULL;
    priv->profile       = 0;
    priv->entrypoint    = 0;
    priv->width         = 0;
    priv->height        = 0;
}

/**
 * gst_vaapi_context_new:
 * @display: a #GstVaapiDisplay
 * @profile: a #GstVaapiProfile
 * @entrypoint: a #GstVaapiEntrypoint
 * @width: coded width from the bitstream
 * @height: coded height from the bitstream
 *
 * Creates a new #GstVaapiContext with the specified codec @profile
 * and @entrypoint.
 *
 * Return value: the newly allocated #GstVaapiContext object
 */
GstVaapiContext *
gst_vaapi_context_new(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    unsigned int        width,
    unsigned int        height
)
{
    GstVaapiContext *context;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(profile, NULL);
    g_return_val_if_fail(entrypoint, NULL);
    g_return_val_if_fail(width > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    context = g_object_new(
        GST_VAAPI_TYPE_CONTEXT,
        "display",      display,
        "id",           GST_VAAPI_ID(VA_INVALID_ID),
        "profile",      profile,
        "entrypoint",   entrypoint,
        "width",        width,
        "height",       height,
        NULL
    );
    if (!context->priv->is_constructed) {
        g_object_unref(context);
        return NULL;
    }
    return context;
}

/**
 * gst_vaapi_context_reset:
 * @context: a #GstVaapiContext
 * @profile: a #GstVaapiProfile
 * @entrypoint: a #GstVaapiEntrypoint
 * @width: coded width from the bitstream
 * @height: coded height from the bitstream
 *
 * Resets @context to the specified codec @profile and @entrypoint.
 * The surfaces will be reallocated if the coded size changed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_context_reset(
    GstVaapiContext    *context,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    unsigned int        width,
    unsigned int        height
)
{
    GstVaapiContextPrivate * const priv = context->priv;
    gboolean size_changed, codec_changed;

    size_changed = priv->width != width || priv->height != height;
    if (size_changed) {
        gst_vaapi_context_destroy_surfaces(context);
        priv->width  = width;
        priv->height = height;
    }

    codec_changed = priv->profile != profile || priv->entrypoint != entrypoint;
    if (codec_changed) {
        gst_vaapi_context_destroy(context);
        priv->profile    = profile;
        priv->entrypoint = entrypoint;
    }

    if (size_changed && !gst_vaapi_context_create_surfaces(context))
        return FALSE;

    if (codec_changed && !gst_vaapi_context_create(context))
        return FALSE;

    priv->is_constructed = TRUE;
    return TRUE;
}

/**
 * gst_vaapi_context_get_id:
 * @context: a #GstVaapiContext
 *
 * Returns the underlying VAContextID of the @context.
 *
 * Return value: the underlying VA context id
 */
GstVaapiID
gst_vaapi_context_get_id(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), VA_INVALID_ID);

    return GST_VAAPI_OBJECT_ID(context);
}

/**
 * gst_vaapi_context_get_profile:
 * @context: a #GstVaapiContext
 *
 * Returns the VA profile used by the @context.
 *
 * Return value: the VA profile used by the @context
 */
GstVaapiProfile
gst_vaapi_context_get_profile(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), 0);

    return context->priv->profile;
}

/**
 * gst_vaapi_context_set_profile:
 * @context: a #GstVaapiContext
 * @profile: the new #GstVaapiProfile to use
 *
 * Sets the new @profile to use with the @context. If @profile matches
 * the previous profile, this call has no effect. Otherwise, the
 * underlying VA context is recreated, while keeping the previously
 * allocated surfaces.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_context_set_profile(GstVaapiContext *context, GstVaapiProfile profile)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), FALSE);
    g_return_val_if_fail(profile, FALSE);

    return gst_vaapi_context_reset(context,
                                   profile,
                                   context->priv->entrypoint,
                                   context->priv->width,
                                   context->priv->height);
}

/**
 * gst_vaapi_context_get_entrypoint:
 * @context: a #GstVaapiContext
 *
 * Returns the VA entrypoint used by the @context
 *
 * Return value: the VA entrypoint used by the @context
 */
GstVaapiEntrypoint
gst_vaapi_context_get_entrypoint(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), 0);

    return context->priv->entrypoint;
}

/**
 * gst_vaapi_context_get_size:
 * @context: a #GstVaapiContext
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the size of the surfaces attached to @context.
 */
void
gst_vaapi_context_get_size(
    GstVaapiContext *context,
    guint           *pwidth,
    guint           *pheight
)
{
    g_return_if_fail(GST_VAAPI_IS_CONTEXT(context));

    if (pwidth)
        *pwidth = context->priv->width;

    if (pheight)
        *pheight = context->priv->height;
}

/**
 * gst_vaapi_context_get_surface:
 * @context: a #GstVaapiContext
 *
 * Acquires a free surface. The returned surface but be released with
 * gst_vaapi_context_put_surface(). This function returns %NULL if
 * there is no free surface available in the pool. The surfaces are
 * pre-allocated during context creation though.
 *
 * Return value: a free surface, or %NULL if none is available
 */
GstVaapiSurface *
gst_vaapi_context_get_surface(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), NULL);

    return gst_vaapi_video_pool_get_object(context->priv->surfaces_pool);
}

/**
 * gst_vaapi_context_get_surface_count:
 * @context: a #GstVaapiContext
 *
 * Retrieves the number of free surfaces left in the pool.
 *
 * Return value: the number of free surfaces available in the pool
 */
guint
gst_vaapi_context_get_surface_count(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), 0);

    return gst_vaapi_video_pool_get_size(context->priv->surfaces_pool);
}

/**
 * gst_vaapi_context_put_surface:
 * @context: a #GstVaapiContext
 * @surface: the #GstVaapiSurface to release
 *
 * Releases a surface acquired by gst_vaapi_context_get_surface().
 */
void
gst_vaapi_context_put_surface(GstVaapiContext *context, GstVaapiSurface *surface)
{
    g_return_if_fail(GST_VAAPI_IS_CONTEXT(context));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    gst_vaapi_video_pool_put_object(context->priv->surfaces_pool, surface);
}

/**
 * gst_vaapi_context_find_surface_by_id:
 * @context: a #GstVaapiContext
 * @id: the VA surface id to find
 *
 * Finds VA surface by @id in the list of surfaces attached to the @context.
 *
 * Return value: the matching #GstVaapiSurface object, or %NULL if
 *   none was found
 */
GstVaapiSurface *
gst_vaapi_context_find_surface_by_id(GstVaapiContext *context, GstVaapiID id)
{
    GstVaapiContextPrivate *priv;
    GstVaapiSurface *surface;
    guint i;

    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), NULL);

    priv = context->priv;
    g_return_val_if_fail(priv->surfaces, NULL);

    for (i = 0; i < priv->surfaces->len; i++) {
        surface = g_ptr_array_index(priv->surfaces, i);
        if (GST_VAAPI_OBJECT_ID(surface) == id)
            return surface;
    }
    return NULL;
}
