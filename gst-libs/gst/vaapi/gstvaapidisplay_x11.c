/*
 *  gstvaapidisplay_x11.c - VA/X11 display abstraction
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
#include "gstvaapidisplay_x11.h"

#define DEBUG 1
#include "vaapi_debug.h"

G_DEFINE_TYPE(GstVaapiDisplayX11,
              gst_vaapi_display_x11,
              GST_VAAPI_TYPE_DISPLAY);

#define GST_VAAPI_DISPLAY_X11_GET_PRIVATE(obj)                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DISPLAY_X11,	\
                                 GstVaapiDisplayX11Private))

struct _GstVaapiDisplayX11Private {
    gboolean    create_display;
    gchar      *display_name;
    Display    *x11_display;
    VADisplay  *va_display;
};

enum {
    PROP_0,

    PROP_DISPLAY_NAME,
    PROP_X11_DISPLAY
};

static void
gst_vaapi_display_x11_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_display_x11_parent_class)->finalize(object);
}

static void
set_display_name(GstVaapiDisplayX11 *display, const gchar *display_name)
{
    GstVaapiDisplayX11Private * const priv = display->priv;

    g_free(priv->display_name);

    if (display_name)
        priv->display_name = g_strdup(display_name);
    else
        priv->display_name = NULL;
}

static void
gst_vaapi_display_x11_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDisplayX11 * const display = GST_VAAPI_DISPLAY_X11(object);

    switch (prop_id) {
    case PROP_DISPLAY_NAME:
        set_display_name(display, g_value_get_string(value));
        break;
    case PROP_X11_DISPLAY:
        display->priv->x11_display = g_value_get_pointer(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_x11_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDisplayX11 * const display = GST_VAAPI_DISPLAY_X11(object);

    switch (prop_id) {
    case PROP_DISPLAY_NAME:
        g_value_set_string(value, display->priv->display_name);
        break;
    case PROP_X11_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_display_x11_get_display(display));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_x11_constructed(GObject *object)
{
    GstVaapiDisplayX11 * const display = GST_VAAPI_DISPLAY_X11(object);
    GObjectClass *parent_class;

    display->priv->create_display = display->priv->x11_display == NULL;

    /* Reset display-name if the user provided his own X11 display */
    if (!display->priv->create_display)
        set_display_name(display, XDisplayString(display->priv->x11_display));

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_x11_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static gboolean
gst_vaapi_display_x11_open_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;

    /* XXX: maintain an X11 display cache */
    if (!priv->x11_display && priv->create_display)
        priv->x11_display = XOpenDisplay(priv->display_name);
    if (!priv->x11_display)
        return FALSE;

    priv->va_display = vaGetDisplay(priv->x11_display);
    return priv->va_display != NULL;
}

static void
gst_vaapi_display_x11_close_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;

    if (priv->x11_display) {
        if (priv->create_display)
            XCloseDisplay(priv->x11_display);
        priv->x11_display = NULL;
    }

    if (priv->display_name) {
        g_free(priv->display_name);
        priv->display_name = NULL;
    }

    priv->va_display = NULL;
}

static VADisplay
gst_vaapi_display_x11_get_va_display(GstVaapiDisplay *display)
{
    return GST_VAAPI_DISPLAY_X11(display)->priv->va_display;
}

static void
gst_vaapi_display_x11_class_init(GstVaapiDisplayX11Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayX11Private));

    object_class->finalize      = gst_vaapi_display_x11_finalize;
    object_class->set_property  = gst_vaapi_display_x11_set_property;
    object_class->get_property  = gst_vaapi_display_x11_get_property;
    object_class->constructed   = gst_vaapi_display_x11_constructed;

    dpy_class->open_display     = gst_vaapi_display_x11_open_display;
    dpy_class->close_display    = gst_vaapi_display_x11_close_display;
    dpy_class->get_display      = gst_vaapi_display_x11_get_va_display;

    g_object_class_install_property
        (object_class,
         PROP_X11_DISPLAY,
         g_param_spec_pointer("x11-display",
                              "X11 display",
                              "X11 display",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_NAME,
         g_param_spec_string("display-name",
                             "X11 display name",
                             "X11 display name",
                             NULL,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_display_x11_init(GstVaapiDisplayX11 *display)
{
    GstVaapiDisplayX11Private *priv = GST_VAAPI_DISPLAY_X11_GET_PRIVATE(display);

    display->priv        = priv;
    priv->create_display = TRUE;
    priv->x11_display    = NULL;
    priv->display_name   = NULL;
}

GstVaapiDisplay *
gst_vaapi_display_x11_new(const gchar *display_name)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_X11,
                        "display-name", display_name,
                        NULL);
}

GstVaapiDisplay *
gst_vaapi_display_x11_new_with_display(Display *x11_display)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_X11,
                        "x11-display", x11_display,
                        NULL);
}

Display *
gst_vaapi_display_x11_get_display(GstVaapiDisplayX11 *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_X11(display), NULL);

    return display->priv->x11_display;
}
