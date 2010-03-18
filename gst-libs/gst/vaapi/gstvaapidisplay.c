/*
 *  gstvaapidisplay.c - VA display abstraction
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "config.h"
#include "gstvaapiutils.h"
#include "gstvaapidisplay.h"
#include <va/va_backend.h>

#define DEBUG 1
#include "gstvaapidebug.h"

GST_DEBUG_CATEGORY(gst_debug_vaapi);

G_DEFINE_TYPE(GstVaapiDisplay, gst_vaapi_display, G_TYPE_OBJECT);

#define GST_VAAPI_DISPLAY_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DISPLAY,	\
                                 GstVaapiDisplayPrivate))

struct _GstVaapiDisplayPrivate {
    GStaticMutex        mutex;
    VADisplay           display;
    gboolean            create_display;
    VAProfile          *profiles;
    guint               num_profiles;
    VAImageFormat      *image_formats;
    guint               num_image_formats;
    VAImageFormat      *subpicture_formats;
    guint              *subpicture_flags;
    guint               num_subpicture_formats;
};

enum {
    PROP_0,

    PROP_DISPLAY
};

static void
append_format(
    VAImageFormat     **pva_formats,
    guint              *pnum_va_formats,
    GstVaapiImageFormat format
)
{
    const VAImageFormat *va_format;
    VAImageFormat *new_va_formats;

    va_format = gst_vaapi_image_format_get_va_format(format);
    if (!va_format)
        return;

    new_va_formats = realloc(
        *pva_formats,
        sizeof(new_va_formats[0]) * (1 + *pnum_va_formats)
    );
    if (!new_va_formats)
        return;

    new_va_formats[(*pnum_va_formats)++] = *va_format;
    *pva_formats = new_va_formats;
}

static void
filter_formats(VAImageFormat **pva_formats, guint *pnum_va_formats)
{
    guint i = 0;
    gboolean has_YV12 = FALSE;
    gboolean has_I420 = FALSE;

    while (i < *pnum_va_formats) {
        VAImageFormat * const va_format = &(*pva_formats)[i];
        const GstVaapiImageFormat format = gst_vaapi_image_format(va_format);
        if (format) {
            ++i;
            switch (format) {
            case GST_VAAPI_IMAGE_YV12:
                has_YV12 = TRUE;
                break;
            case GST_VAAPI_IMAGE_I420:
                has_I420 = TRUE;
                break;
            default:
                break;
            }
        }
        else {
            /* Remove any format that is not supported by libgstvaapi */
            GST_DEBUG("unsupported format %c%c%c%c",
                      va_format->fourcc & 0xff,
                      (va_format->fourcc >> 8) & 0xff,
                      (va_format->fourcc >> 16) & 0xff,
                      (va_format->fourcc >> 24) & 0xff);
            *va_format = (*pva_formats)[--(*pnum_va_formats)];
        }
    }

    /* Append I420 (resp. YV12) format if YV12 (resp. I420) is not
       supported by the underlying driver */
    if (has_YV12 && !has_I420)
        append_format(pva_formats, pnum_va_formats, GST_VAAPI_IMAGE_I420);
    else if (has_I420 && !has_YV12)
        append_format(pva_formats, pnum_va_formats, GST_VAAPI_IMAGE_YV12);
}

/* Sort image formats. Prefer YUV formats first */
static int
compare_yuv_formats(const void *a, const void *b)
{
    const GstVaapiImageFormat fmt1 = gst_vaapi_image_format((VAImageFormat *)a);
    const GstVaapiImageFormat fmt2 = gst_vaapi_image_format((VAImageFormat *)b);

    g_assert(fmt1 && fmt2);

    const gboolean is_fmt1_yuv = gst_vaapi_image_format_is_yuv(fmt1);
    const gboolean is_fmt2_yuv = gst_vaapi_image_format_is_yuv(fmt2);

    if (is_fmt1_yuv != is_fmt2_yuv)
        return is_fmt1_yuv ? -1 : 1;

    return ((int)gst_vaapi_image_format_get_score(fmt1) -
            (int)gst_vaapi_image_format_get_score(fmt2));
}

/* Sort subpicture formats. Prefer RGB formats first */
static int
compare_rgb_formats(const void *a, const void *b)
{
    const GstVaapiImageFormat fmt1 = gst_vaapi_image_format((VAImageFormat *)a);
    const GstVaapiImageFormat fmt2 = gst_vaapi_image_format((VAImageFormat *)b);

    g_assert(fmt1 && fmt2);

    const gboolean is_fmt1_rgb = gst_vaapi_image_format_is_rgb(fmt1);
    const gboolean is_fmt2_rgb = gst_vaapi_image_format_is_rgb(fmt2);

    if (is_fmt1_rgb != is_fmt2_rgb)
        return is_fmt1_rgb ? -1 : 1;

    return ((int)gst_vaapi_image_format_get_score(fmt1) -
            (int)gst_vaapi_image_format_get_score(fmt2));
}

static void
gst_vaapi_display_destroy(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;

    if (priv->profiles) {
        g_free(priv->profiles);
        priv->profiles = NULL;
    }

    if (priv->image_formats) {
        g_free(priv->image_formats);
        priv->image_formats = NULL;
        priv->num_image_formats = 0;
    }

    if (priv->subpicture_formats) {
        g_free(priv->subpicture_formats);
        priv->subpicture_formats = NULL;
        priv->num_subpicture_formats = 0;
    }

    if (priv->subpicture_flags) {
        g_free(priv->subpicture_flags);
        priv->subpicture_flags = NULL;
    }

    if (priv->display) {
        vaTerminate(priv->display);
        priv->display = NULL;
    }

    if (priv->create_display) {
        GstVaapiDisplayClass *klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
        if (klass->close_display)
            klass->close_display(display);
    }
}

static gboolean
gst_vaapi_display_create(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;
    VAStatus status;
    int major_version, minor_version;
    guint i;

    if (!priv->display && priv->create_display) {
        GstVaapiDisplayClass *klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
        if (klass->open_display && !klass->open_display(display))
            return FALSE;
        if (klass->get_display)
            priv->display = klass->get_display(display);
    }
    if (!priv->display)
        return FALSE;

    status = vaInitialize(priv->display, &major_version, &minor_version);
    if (!vaapi_check_status(status, "vaInitialize()"))
        return FALSE;
    GST_DEBUG("VA-API version %d.%d", major_version, minor_version);

    /* VA profiles */
    priv->num_profiles = vaMaxNumProfiles(priv->display);
    priv->profiles = g_new(VAProfile, priv->num_profiles);
    if (!priv->profiles)
        return FALSE;
    status = vaQueryConfigProfiles(priv->display,
                                   priv->profiles,
                                   &priv->num_profiles);
    if (!vaapi_check_status(status, "vaQueryConfigProfiles()"))
        return FALSE;

    GST_DEBUG("%d profiles", priv->num_profiles);
    for (i = 0; i < priv->num_profiles; i++)
        GST_DEBUG("  %s", string_of_VAProfile(priv->profiles[i]));

    /* VA image formats */
    priv->num_image_formats = vaMaxNumImageFormats(priv->display);
    priv->image_formats = g_new(VAImageFormat, priv->num_image_formats);
    if (!priv->image_formats)
        return FALSE;
    status = vaQueryImageFormats(
        priv->display,
        priv->image_formats,
        &priv->num_image_formats
    );
    if (!vaapi_check_status(status, "vaQueryImageFormats()"))
        return FALSE;

    GST_DEBUG("%d image formats", priv->num_image_formats);
    for (i = 0; i < priv->num_image_formats; i++)
        GST_DEBUG("  %s", string_of_FOURCC(priv->image_formats[i].fourcc));

    filter_formats(&priv->image_formats, &priv->num_image_formats);
    qsort(
        priv->image_formats,
        priv->num_image_formats,
        sizeof(priv->image_formats[0]),
        compare_yuv_formats
    );

    /* VA subpicture formats */
    priv->num_subpicture_formats = vaMaxNumSubpictureFormats(priv->display);
    priv->subpicture_formats = g_new(VAImageFormat, priv->num_subpicture_formats);
    if (!priv->subpicture_formats)
        return FALSE;
    priv->subpicture_flags = g_new(guint, priv->num_subpicture_formats);
    if (!priv->subpicture_flags)
        return FALSE;
    status = vaQuerySubpictureFormats(
        priv->display,
        priv->subpicture_formats,
        priv->subpicture_flags,
        &priv->num_subpicture_formats
    );
    if (!vaapi_check_status(status, "vaQuerySubpictureFormats()"))
        return FALSE;

    filter_formats(&priv->subpicture_formats, &priv->num_subpicture_formats);
    qsort(
        priv->subpicture_formats,
        priv->num_subpicture_formats,
        sizeof(priv->subpicture_formats[0]),
        compare_rgb_formats
    );

    GST_DEBUG("%d subpicture formats", priv->num_subpicture_formats);
    for (i = 0; i < priv->num_subpicture_formats; i++)
        GST_DEBUG("  %s", string_of_FOURCC(priv->subpicture_formats[i].fourcc));

    return TRUE;
}

static void
gst_vaapi_display_lock_default(GstVaapiDisplay *display)
{
    g_static_mutex_lock(&display->priv->mutex);
}

static void
gst_vaapi_display_unlock_default(GstVaapiDisplay *display)
{
    g_static_mutex_unlock(&display->priv->mutex);
}

static void
gst_vaapi_display_finalize(GObject *object)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    gst_vaapi_display_destroy(display);

    G_OBJECT_CLASS(gst_vaapi_display_parent_class)->finalize(object);
}

static void
gst_vaapi_display_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        display->priv->display = g_value_get_pointer(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_display_get_display(display));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_constructed(GObject *object)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);
    GObjectClass *parent_class;

    display->priv->create_display = display->priv->display == NULL;
    gst_vaapi_display_create(display);

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_display_class_init(GstVaapiDisplayClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapi, "vaapi", 0, "VA-API helper");

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayPrivate));

    object_class->finalize      = gst_vaapi_display_finalize;
    object_class->set_property  = gst_vaapi_display_set_property;
    object_class->get_property  = gst_vaapi_display_get_property;
    object_class->constructed   = gst_vaapi_display_constructed;

    dpy_class->lock_display     = gst_vaapi_display_lock_default;
    dpy_class->unlock_display   = gst_vaapi_display_unlock_default;

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_pointer("display",
                              "VA display",
                              "VA display",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_display_init(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate *priv = GST_VAAPI_DISPLAY_GET_PRIVATE(display);

    display->priv                = priv;
    priv->display                = NULL;
    priv->create_display         = TRUE;
    priv->profiles               = 0;
    priv->num_profiles           = 0;
    priv->image_formats          = NULL;
    priv->num_image_formats      = 0;
    priv->subpicture_formats     = NULL;
    priv->subpicture_flags       = NULL;
    priv->num_subpicture_formats = 0;

    g_static_mutex_init(&priv->mutex);
}

GstVaapiDisplay *
gst_vaapi_display_new_with_display(VADisplay va_display)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY,
                        "display", va_display,
                        NULL);
}

void
gst_vaapi_display_lock(GstVaapiDisplay *display)
{
    GstVaapiDisplayClass *klass;

    g_return_if_fail(GST_VAAPI_IS_DISPLAY(display));

    klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
    if (klass->lock_display)
        klass->lock_display(display);
}

void
gst_vaapi_display_unlock(GstVaapiDisplay *display)
{
    GstVaapiDisplayClass *klass;

    g_return_if_fail(GST_VAAPI_IS_DISPLAY(display));

    klass = GST_VAAPI_DISPLAY_GET_CLASS(display);
    if (klass->unlock_display)
        klass->unlock_display(display);
}

VADisplay
gst_vaapi_display_get_display(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return display->priv->display;
}

gboolean
gst_vaapi_display_has_profile(GstVaapiDisplay *display, VAProfile profile)
{
    guint i;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    for (i = 0; i < display->priv->num_profiles; i++)
        if (display->priv->profiles[i] == profile)
            return TRUE;
    return FALSE;
}

static gboolean
_gst_vaapi_display_has_format(
    GstVaapiDisplay     *display,
    GstVaapiImageFormat  format,
    const VAImageFormat *va_formats,
    guint                num_va_formats
)
{
    guint i;

    g_return_val_if_fail(format != 0, FALSE);

    for (i = 0; i < num_va_formats; i++)
        if (gst_vaapi_image_format(&va_formats[i]) == format)
            return TRUE;
    return FALSE;
}

static GstCaps *
_gst_vaapi_display_get_caps(
    GstVaapiDisplay     *display,
    const VAImageFormat *va_formats,
    guint                num_va_formats
)
{
    GstCaps *out_caps;
    guint i;

    out_caps = gst_caps_new_empty();
    if (!out_caps)
        return NULL;

    for (i = 0; i < num_va_formats; i++) {
        GstVaapiImageFormat format = gst_vaapi_image_format(&va_formats[i]);
        if (format) {
            GstCaps * const caps = gst_vaapi_image_format_get_caps(format);
            if (caps)
                gst_caps_append(out_caps, caps);
        }
    }
    return out_caps;
}

GstCaps *
gst_vaapi_display_get_image_caps(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return _gst_vaapi_display_get_caps(display,
                                       display->priv->image_formats,
                                       display->priv->num_image_formats);
}

gboolean
gst_vaapi_display_has_image_format(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    return _gst_vaapi_display_has_format(display, format,
                                         display->priv->image_formats,
                                         display->priv->num_image_formats);
}

GstCaps *
gst_vaapi_display_get_subpicture_caps(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return _gst_vaapi_display_get_caps(display,
                                       display->priv->subpicture_formats,
                                       display->priv->num_subpicture_formats);
}

gboolean
gst_vaapi_display_has_subpicture_format(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);

    return _gst_vaapi_display_has_format(display, format,
                                         display->priv->subpicture_formats,
                                         display->priv->num_subpicture_formats);
}
