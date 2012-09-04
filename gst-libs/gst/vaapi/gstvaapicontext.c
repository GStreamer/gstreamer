/*
 *  gstvaapicontext.c - VA context abstraction
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
 * SECTION:gstvaapicontext
 * @short_description: VA context abstraction
 */

#include "sysdeps.h"
#include <assert.h>
#include "gstvaapicompat.h"
#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapisurfaceproxy.h"
#include "gstvaapiimage.h"
#include "gstvaapisubpicture.h"
#include "gstvaapiminiobject.h"
#include "gstvaapiutils.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiContext, gst_vaapi_context, GST_VAAPI_TYPE_OBJECT)

#define GST_VAAPI_CONTEXT_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_CONTEXT,	\
                                 GstVaapiContextPrivate))

typedef struct _GstVaapiOverlayRectangle GstVaapiOverlayRectangle;
struct _GstVaapiOverlayRectangle {
    GstVaapiContext    *context;
    GstVaapiSubpicture *subpicture;
    GstVaapiRectangle   render_rect;
    guint               seq_num;
    guint               layer_id;
    GstBuffer          *rect_buffer;
    GstVideoOverlayRectangle *rect;
    guint               is_associated   : 1;
};

/* XXX: optimize for the effective number of reference frames */
struct _GstVaapiContextPrivate {
    VAConfigID          config_id;
    GPtrArray          *surfaces;
    GstVaapiVideoPool  *surfaces_pool;
    GPtrArray          *overlays[2];
    guint               overlay_id;
    GstVaapiProfile     profile;
    GstVaapiEntrypoint  entrypoint;
    guint               width;
    guint               height;
    guint               ref_frames;
    guint               is_constructed  : 1;
};

enum {
    PROP_0,

    PROP_PROFILE,
    PROP_ENTRYPOINT,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_REF_FRAMES
};

static guint
get_max_ref_frames(GstVaapiProfile profile)
{
    guint ref_frames;

    switch (gst_vaapi_profile_get_codec(profile)) {
    case GST_VAAPI_CODEC_H264:  ref_frames = 16; break;
    case GST_VAAPI_CODEC_JPEG:  ref_frames =  0; break;
    default:                    ref_frames =  2; break;
    }
    return ref_frames;
}

static inline void
gst_video_overlay_rectangle_replace(GstVideoOverlayRectangle **old_rect_ptr,
    GstVideoOverlayRectangle *new_rect)
{
    gst_mini_object_replace((GstMiniObject **)old_rect_ptr,
        GST_MINI_OBJECT_CAST(new_rect));
}

#define overlay_rectangle_ref(overlay) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(overlay))

#define overlay_rectangle_unref(overlay) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(overlay))

#define overlay_rectangle_replace(old_overlay_ptr, new_overlay) \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_overlay_ptr), \
        (GstVaapiMiniObject *)(new_overlay))

static void
overlay_rectangle_finalize(GstVaapiOverlayRectangle *overlay);

static gboolean
overlay_rectangle_associate(GstVaapiOverlayRectangle *overlay);

static gboolean
overlay_rectangle_deassociate(GstVaapiOverlayRectangle *overlay);

static inline const GstVaapiMiniObjectClass *
overlay_rectangle_class(void)
{
    static const GstVaapiMiniObjectClass GstVaapiOverlayRectangleClass = {
        sizeof(GstVaapiOverlayRectangle),
        (GDestroyNotify)overlay_rectangle_finalize
    };
    return &GstVaapiOverlayRectangleClass;
}

static GstVaapiOverlayRectangle *
overlay_rectangle_new(GstVideoOverlayRectangle *rect, GstVaapiContext *context,
    guint layer_id)
{
    GstVaapiOverlayRectangle *overlay;
    GstVaapiRectangle *render_rect;
    guint width, height, flags;
    gint x, y;

    overlay = (GstVaapiOverlayRectangle *)
        gst_vaapi_mini_object_new0(overlay_rectangle_class());
    if (!overlay)
        return NULL;

    overlay->context    = context;
    overlay->seq_num    = gst_video_overlay_rectangle_get_seqnum(rect);
    overlay->layer_id   = layer_id;
    overlay->rect       = gst_video_overlay_rectangle_ref(rect);

    flags = gst_video_overlay_rectangle_get_flags(rect);
    gst_buffer_replace(&overlay->rect_buffer,
        gst_video_overlay_rectangle_get_pixels_unscaled_raw(rect, flags));
    if (!overlay->rect_buffer)
        goto error;

    overlay->subpicture = gst_vaapi_subpicture_new_from_overlay_rectangle(
        GST_VAAPI_OBJECT_DISPLAY(context), rect);
    if (!overlay->subpicture)
        goto error;

    gst_video_overlay_rectangle_get_render_rectangle(rect,
        &x, &y, &width, &height);
    render_rect = &overlay->render_rect;
    render_rect->x = x;
    render_rect->y = y;
    render_rect->width = width;
    render_rect->height = height;
    return overlay;

error:
    overlay_rectangle_unref(overlay);
    return NULL;
}

static void
overlay_rectangle_finalize(GstVaapiOverlayRectangle *overlay)
{
    gst_buffer_replace(&overlay->rect_buffer, NULL);
    gst_video_overlay_rectangle_unref(overlay->rect);

    if (overlay->subpicture) {
        overlay_rectangle_deassociate(overlay);
        g_object_unref(overlay->subpicture);
        overlay->subpicture = NULL;
    }
}

static gboolean
overlay_rectangle_associate(GstVaapiOverlayRectangle *overlay)
{
    GstVaapiSubpicture * const subpicture = overlay->subpicture;
    GPtrArray * const surfaces = overlay->context->priv->surfaces;
    guint i, n_associated;

    if (overlay->is_associated)
        return TRUE;

    n_associated = 0;
    for (i = 0; i < surfaces->len; i++) {
        GstVaapiSurface * const surface = g_ptr_array_index(surfaces, i);
        if (gst_vaapi_surface_associate_subpicture(surface, subpicture,
                NULL, &overlay->render_rect))
            n_associated++;
    }

    overlay->is_associated = TRUE;
    return n_associated == surfaces->len;
}

static gboolean
overlay_rectangle_deassociate(GstVaapiOverlayRectangle *overlay)
{
    GstVaapiSubpicture * const subpicture = overlay->subpicture;
    GPtrArray * const surfaces = overlay->context->priv->surfaces;
    guint i, n_associated;

    if (!overlay->is_associated)
        return TRUE;

    n_associated = surfaces->len;
    for (i = 0; i < surfaces->len; i++) {
        GstVaapiSurface * const surface = g_ptr_array_index(surfaces, i);
        if (gst_vaapi_surface_deassociate_subpicture(surface, subpicture))
            n_associated--;
    }

    overlay->is_associated = FALSE;
    return n_associated == 0;
}

static gboolean
overlay_rectangle_changed_pixels(GstVaapiOverlayRectangle *overlay,
    GstVideoOverlayRectangle *rect)
{
    guint flags;
    GstBuffer *buffer;

    if (overlay->seq_num == gst_video_overlay_rectangle_get_seqnum(rect))
        return FALSE;

    flags = to_GstVideoOverlayFormatFlags(
        gst_vaapi_subpicture_get_flags(overlay->subpicture));

    buffer = gst_video_overlay_rectangle_get_pixels_unscaled_raw(rect, flags);
    if (!buffer)
        return FALSE;
#if GST_CHECK_VERSION(1,0,0)
    {
        const guint n_blocks = gst_buffer_n_memory(buffer);
        gsize ofs;
        guint i;

        if (buffer == overlay->rect_buffer)
            return TRUE;

        if (n_blocks != gst_buffer_n_memory(overlay->rect_buffer))
            return FALSE;

        for (i = 0; i < n_blocks; i++) {
            GstMemory * const mem1 = gst_buffer_peek_memory(buffer, i);
            GstMemory * const mem2 =
                gst_buffer_peek_memory(overlay->rect_buffer, i);
            if (!gst_memory_is_span(mem1, mem2, &ofs))
                return FALSE;
        }
    }
#else
    if (GST_BUFFER_DATA(overlay->rect_buffer) != GST_BUFFER_DATA(buffer))
        return FALSE;
#endif
    return TRUE;
}

static gboolean
overlay_rectangle_changed_render_rect(GstVaapiOverlayRectangle *overlay,
    GstVideoOverlayRectangle *rect)
{
    GstVaapiRectangle * const render_rect = &overlay->render_rect;
    guint width, height;
    gint x, y;

    gst_video_overlay_rectangle_get_render_rectangle(rect,
        &x, &y, &width, &height);

    if (x == render_rect->x &&
        y == render_rect->y &&
        width == render_rect->width &&
        height == render_rect->height)
        return FALSE;

    render_rect->x = x;
    render_rect->y = y;
    render_rect->width = width;
    render_rect->height = height;
    return TRUE;
}

static inline gboolean
overlay_rectangle_update_global_alpha(GstVaapiOverlayRectangle *overlay,
    GstVideoOverlayRectangle *rect)
{
#ifdef HAVE_GST_VIDEO_OVERLAY_HWCAPS
    const guint flags = gst_video_overlay_rectangle_get_flags(rect);
    if (!(flags & GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA))
        return TRUE;
#endif
    return gst_vaapi_subpicture_set_global_alpha(overlay->subpicture,
        gst_video_overlay_rectangle_get_global_alpha(rect));
}

static gboolean
overlay_rectangle_update(GstVaapiOverlayRectangle *overlay,
    GstVideoOverlayRectangle *rect, gboolean *reassociate_ptr)
{
    if (overlay_rectangle_changed_pixels(overlay, rect))
        return FALSE;
    if (overlay_rectangle_changed_render_rect(overlay, rect))
        *reassociate_ptr = TRUE;
    if (!overlay_rectangle_update_global_alpha(overlay, rect))
        return FALSE;
    gst_video_overlay_rectangle_replace(&overlay->rect, rect);
    return TRUE;
}

static inline GPtrArray *
overlay_new(void)
{
    return g_ptr_array_new_with_free_func(
        (GDestroyNotify)gst_vaapi_mini_object_unref);
}

static void
overlay_destroy(GPtrArray **overlay_ptr)
{
    GPtrArray * const overlay = *overlay_ptr;

    if (!overlay)
        return;
    g_ptr_array_unref(overlay);
    *overlay_ptr = NULL;
}

static void
overlay_clear(GPtrArray *overlay)
{
    if (overlay && overlay->len > 0)
        g_ptr_array_remove_range(overlay, 0, overlay->len);
}

static GstVaapiOverlayRectangle *
overlay_lookup(GPtrArray *overlays, GstVideoOverlayRectangle *rect)
{
    guint i;

    for (i = 0; i < overlays->len; i++) {
        GstVaapiOverlayRectangle * const overlay =
            g_ptr_array_index(overlays, i);

        if (overlay->rect == rect)
            return overlay;
    }
    return NULL;
}

static gboolean
overlay_reassociate(GPtrArray *overlays)
{
    guint i;

    for (i = 0; i < overlays->len; i++)
        overlay_rectangle_deassociate(g_ptr_array_index(overlays, i));

    for (i = 0; i < overlays->len; i++) {
        if (!overlay_rectangle_associate(g_ptr_array_index(overlays, i)))
            return FALSE;
    }
    return TRUE;
}

static void
gst_vaapi_context_clear_overlay(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;

    overlay_clear(priv->overlays[0]);
    overlay_clear(priv->overlays[1]);
    priv->overlay_id = 0;
}

static inline void
gst_vaapi_context_destroy_overlay(GstVaapiContext *context)
{
    gst_vaapi_context_clear_overlay(context);
}

static void
unref_surface_cb(gpointer data, gpointer user_data)
{
    GstVaapiSurface * const surface = GST_VAAPI_SURFACE(data);

    gst_vaapi_surface_set_parent_context(surface, NULL);
    g_object_unref(surface);
}

static void
gst_vaapi_context_destroy_surfaces(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;

    gst_vaapi_context_destroy_overlay(context);

    if (priv->surfaces) {
        g_ptr_array_foreach(priv->surfaces, unref_surface_cb, NULL);
        g_ptr_array_free(priv->surfaces, TRUE);
        priv->surfaces = NULL;
    }

    g_clear_object(&priv->surfaces_pool);
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
gst_vaapi_context_create_overlay(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;

    if (!priv->overlays[0] || !priv->overlays[1])
        return FALSE;

    gst_vaapi_context_clear_overlay(context);
    return TRUE;
}

static gboolean
gst_vaapi_context_create_surfaces(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;
    GstCaps *caps;
    GstVaapiSurface *surface;
    guint i, num_surfaces;

    /* Number of scratch surfaces beyond those used as reference */
    const guint SCRATCH_SURFACES_COUNT = 4;

    if (!gst_vaapi_context_create_overlay(context))
        return FALSE;

    if (!priv->surfaces) {
        priv->surfaces = g_ptr_array_new();
        if (!priv->surfaces)
            return FALSE;
    }

    if (!priv->surfaces_pool) {
        caps = gst_caps_new_simple(
            GST_VAAPI_SURFACE_CAPS_NAME,
            "type", G_TYPE_STRING, "vaapi",
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

    num_surfaces = priv->ref_frames + SCRATCH_SURFACES_COUNT;
    gst_vaapi_video_pool_set_capacity(priv->surfaces_pool, num_surfaces);

    for (i = priv->surfaces->len; i < num_surfaces; i++) {
        surface = gst_vaapi_surface_new(
            GST_VAAPI_OBJECT_DISPLAY(context),
            GST_VAAPI_CHROMA_TYPE_YUV420,
            priv->width, priv->height
        );
        if (!surface)
            return FALSE;
        gst_vaapi_surface_set_parent_context(surface, context);
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
    GstVaapiContextPrivate * const priv = context->priv;

    overlay_destroy(&priv->overlays[0]);
    overlay_destroy(&priv->overlays[1]);
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
    case PROP_REF_FRAMES:
        priv->ref_frames = g_value_get_uint(value);
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
    case PROP_REF_FRAMES:
        g_value_set_uint(value, priv->ref_frames);
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

    g_object_class_install_property
        (object_class,
         PROP_REF_FRAMES,
         g_param_spec_uint("ref-frames",
                           "Reference Frames",
                           "The number of reference frames",
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
    priv->overlays[0]   = overlay_new();
    priv->overlays[1]   = overlay_new();
    priv->profile       = 0;
    priv->entrypoint    = 0;
    priv->width         = 0;
    priv->height        = 0;
    priv->ref_frames    = 0;
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
    guint               width,
    guint               height
)
{
    GstVaapiContextInfo info;

    info.profile    = profile;
    info.entrypoint = entrypoint;
    info.width      = width;
    info.height     = height;
    info.ref_frames = get_max_ref_frames(profile);
    return gst_vaapi_context_new_full(display, &info);
}

/**
 * gst_vaapi_context_new_full:
 * @display: a #GstVaapiDisplay
 * @cip: a pointer to the #GstVaapiContextInfo
 *
 * Creates a new #GstVaapiContext with the configuration specified by
 * @cip, thus including profile, entry-point, encoded size and maximum
 * number of reference frames reported by the bitstream.
 *
 * Return value: the newly allocated #GstVaapiContext object
 */
GstVaapiContext *
gst_vaapi_context_new_full(GstVaapiDisplay *display, GstVaapiContextInfo *cip)
{
    GstVaapiContext *context;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(cip->profile, NULL);
    g_return_val_if_fail(cip->entrypoint, NULL);
    g_return_val_if_fail(cip->width > 0, NULL);
    g_return_val_if_fail(cip->height > 0, NULL);

    context = g_object_new(
        GST_VAAPI_TYPE_CONTEXT,
        "display",      display,
        "id",           GST_VAAPI_ID(VA_INVALID_ID),
        "profile",      cip->profile,
        "entrypoint",   cip->entrypoint,
        "width",        cip->width,
        "height",       cip->height,
        "ref-frames",   cip->ref_frames,
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
    GstVaapiContextInfo info;

    info.profile    = profile;
    info.entrypoint = entrypoint;
    info.width      = width;
    info.height     = height;
    info.ref_frames = priv->ref_frames;

    return gst_vaapi_context_reset_full(context, &info);
}

/**
 * gst_vaapi_context_reset_full:
 * @context: a #GstVaapiContext
 * @cip: a pointer to the new #GstVaapiContextInfo details
 *
 * Resets @context to the configuration specified by @cip, thus
 * including profile, entry-point, encoded size and maximum number of
 * reference frames reported by the bitstream.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_context_reset_full(GstVaapiContext *context, GstVaapiContextInfo *cip)
{
    GstVaapiContextPrivate * const priv = context->priv;
    gboolean size_changed, codec_changed;

    size_changed = priv->width != cip->width || priv->height != cip->height;
    if (size_changed) {
        gst_vaapi_context_destroy_surfaces(context);
        priv->width  = cip->width;
        priv->height = cip->height;
    }

    codec_changed = priv->profile != cip->profile || priv->entrypoint != cip->entrypoint;
    if (codec_changed) {
        gst_vaapi_context_destroy(context);
        priv->profile    = cip->profile;
        priv->entrypoint = cip->entrypoint;
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
 * gst_vaapi_context_get_surface_proxy:
 * @context: a #GstVaapiContext
 *
 * Acquires a free surface, wrapped into a #GstVaapiSurfaceProxy. The
 * returned surface will be automatically released when the proxy is
 * destroyed. So, it is enough to call gst_vaapi_surface_proxy_unref()
 * after usage.
 *
 * This function returns %NULL if there is no free surface available
 * in the pool. The surfaces are pre-allocated during context creation
 * though.
 *
 * Return value: a free surface, or %NULL if none is available
 */
GstVaapiSurfaceProxy *
gst_vaapi_context_get_surface_proxy(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), NULL);

    return gst_vaapi_surface_proxy_new_from_pool(
        GST_VAAPI_SURFACE_POOL(context->priv->surfaces_pool));
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
 * gst_vaapi_context_apply_composition:
 * @context: a #GstVaapiContext
 * @composition: a #GstVideoOverlayComposition
 *
 * Applies video composition planes to all surfaces bound to @context.
 * This helper function resets any additional subpictures the user may
 * have associated himself. A %NULL @composition will also clear all
 * the existing subpictures.
 *
 * Return value: %TRUE if all composition planes could be applied,
 *   %FALSE otherwise
 */
gboolean
gst_vaapi_context_apply_composition(
    GstVaapiContext            *context,
    GstVideoOverlayComposition *composition
)
{
    GstVaapiContextPrivate *priv;
    GPtrArray *curr_overlay, *next_overlay;
    guint i, n_rectangles;
    gboolean reassociate = FALSE;

    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), FALSE);

    priv = context->priv;

    if (!priv->surfaces)
        return FALSE;

    if (!composition) {
        gst_vaapi_context_clear_overlay(context);
        return TRUE;
    }

    curr_overlay = priv->overlays[priv->overlay_id];
    next_overlay = priv->overlays[priv->overlay_id ^ 1];
    overlay_clear(next_overlay);

    n_rectangles = gst_video_overlay_composition_n_rectangles(composition);
    for (i = 0; i < n_rectangles; i++) {
        GstVideoOverlayRectangle * const rect =
            gst_video_overlay_composition_get_rectangle(composition, i);
        GstVaapiOverlayRectangle *overlay;

        overlay = overlay_lookup(curr_overlay, rect);
        if (overlay && overlay_rectangle_update(overlay, rect, &reassociate)) {
            overlay_rectangle_ref(overlay);
            if (overlay->layer_id != i)
                reassociate = TRUE;
        }
        else {
            overlay = overlay_rectangle_new(rect, context, i);
            if (!overlay) {
                GST_WARNING("could not create VA overlay rectangle");
                goto error;
            }
            reassociate = TRUE;
        }
        g_ptr_array_add(next_overlay, overlay);
    }

    overlay_clear(curr_overlay);
    priv->overlay_id ^= 1;

    if (reassociate && !overlay_reassociate(next_overlay))
        return FALSE;
    return TRUE;

error:
    gst_vaapi_context_clear_overlay(context);
    return FALSE;
}
