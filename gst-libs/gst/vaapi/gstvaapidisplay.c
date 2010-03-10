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
#include "vaapi_utils.h"
#include "gstvaapidisplay.h"
#include <va/va_backend.h>

#define DEBUG 1
#include "vaapi_debug.h"

GST_DEBUG_CATEGORY(gst_debug_vaapi);

G_DEFINE_TYPE(GstVaapiDisplay, gst_vaapi_display, G_TYPE_OBJECT);

#define GST_VAAPI_DISPLAY_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DISPLAY,	\
                                 GstVaapiDisplayPrivate))

struct _GstVaapiDisplayPrivate {
    VADisplay           display;
    VAProfile          *profiles;
    unsigned int        num_profiles;
    VAImageFormat      *image_formats;
    unsigned int        num_image_formats;
    VAImageFormat      *subpicture_formats;
    unsigned int       *subpicture_flags;
    unsigned int        num_subpicture_formats;
};

enum {
    PROP_0,

    PROP_DISPLAY
};

static void
gst_vaapi_display_set_display(GstVaapiDisplay *display, VADisplay va_display);

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
}

static gboolean
gst_vaapi_display_create(GstVaapiDisplay *display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;
    VAStatus status;
    int major_version, minor_version;
    unsigned int i;

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
    status = vaQueryImageFormats(priv->display,
                                 priv->image_formats,
                                 &priv->num_image_formats);
    if (!vaapi_check_status(status, "vaQueryImageFormats()"))
        return FALSE;

    GST_DEBUG("%d image formats", priv->num_image_formats);
    for (i = 0; i < priv->num_image_formats; i++)
        GST_DEBUG("  %s", string_of_FOURCC(priv->image_formats[i].fourcc));

    /* VA subpicture formats */
    priv->num_subpicture_formats = vaMaxNumSubpictureFormats(priv->display);
    priv->subpicture_formats = g_new(VAImageFormat, priv->num_subpicture_formats);
    if (!priv->subpicture_formats)
        return FALSE;
    priv->subpicture_flags = g_new(unsigned int, priv->num_subpicture_formats);
    if (!priv->subpicture_flags)
        return FALSE;
    status = vaQuerySubpictureFormats(priv->display,
                                      priv->subpicture_formats,
                                      priv->subpicture_flags,
                                      &priv->num_subpicture_formats);
    if (!vaapi_check_status(status, "vaQuerySubpictureFormats()"))
        return FALSE;

    GST_DEBUG("%d subpicture formats", priv->num_subpicture_formats);
    for (i = 0; i < priv->num_subpicture_formats; i++)
        GST_DEBUG("  %s", string_of_FOURCC(priv->subpicture_formats[i].fourcc));

    return TRUE;
}

static void
gst_vaapi_display_finalize(GObject *object)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    gst_vaapi_display_destroy(display);

    G_OBJECT_CLASS(gst_vaapi_display_parent_class)->finalize(object);
}

static void
gst_vaapi_display_set_property(GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    GstVaapiDisplay * const display = GST_VAAPI_DISPLAY(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        gst_vaapi_display_set_display(display, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_get_property(GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
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
gst_vaapi_display_class_init(GstVaapiDisplayClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapi, "vaapi", 0, "VA-API helper");

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayPrivate));

    object_class->finalize     = gst_vaapi_display_finalize;
    object_class->set_property = gst_vaapi_display_set_property;
    object_class->get_property = gst_vaapi_display_get_property;

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
    priv->profiles               = 0;
    priv->num_profiles           = 0;
    priv->image_formats          = NULL;
    priv->num_image_formats      = 0;
    priv->subpicture_formats     = NULL;
    priv->subpicture_flags       = NULL;
    priv->num_subpicture_formats = 0;
}

GstVaapiDisplay *
gst_vaapi_display_new_with_display(VADisplay va_display)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY,
                        "display", va_display,
                        NULL);
}

VADisplay
gst_vaapi_display_get_display(GstVaapiDisplay *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return display->priv->display;
}

void
gst_vaapi_display_set_display(GstVaapiDisplay *display, VADisplay va_display)
{
    GstVaapiDisplayPrivate * const priv = display->priv;

    if (priv->display)
        gst_vaapi_display_destroy(display);

    if (va_display) {
        priv->display = va_display;
        if (!gst_vaapi_display_create(display)) {
            gst_vaapi_display_destroy(display);
            return;
        }
    }
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
    unsigned int         num_va_formats
)
{
    unsigned int i;

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
    unsigned int         num_va_formats
)
{
    GstCaps *out_caps;
    unsigned int i;

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
