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
#include "vaapi_utils.h"
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
    Display *display;
};

enum {
    PROP_0,

    PROP_X11_DISPLAY
};

static void
gst_vaapi_display_x11_set_display(GstVaapiDisplayX11 *display,
                                  Display            *x11_display);

static void
gst_vaapi_display_x11_finalize(GObject *object)
{
    GstVaapiDisplayX11        *display = GST_VAAPI_DISPLAY_X11(object);
    GstVaapiDisplayX11Private *priv    = display->priv;

    G_OBJECT_CLASS(gst_vaapi_display_x11_parent_class)->finalize(object);
}

static void
gst_vaapi_display_x11_set_property(GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    GstVaapiDisplayX11        *display = GST_VAAPI_DISPLAY_X11(object);
    GstVaapiDisplayX11Private *priv    = display->priv;

    switch (prop_id) {
    case PROP_X11_DISPLAY:
        gst_vaapi_display_x11_set_display(display, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_x11_get_property(GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    GstVaapiDisplayX11        *display = GST_VAAPI_DISPLAY_X11(object);
    GstVaapiDisplayX11Private *priv    = display->priv;

    switch (prop_id) {
    case PROP_X11_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_display_x11_get_display(display));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_x11_class_init(GstVaapiDisplayX11Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayX11Private));

    object_class->finalize     = gst_vaapi_display_x11_finalize;
    object_class->set_property = gst_vaapi_display_x11_set_property;
    object_class->get_property = gst_vaapi_display_x11_get_property;

    g_object_class_install_property
        (object_class,
         PROP_X11_DISPLAY,
         g_param_spec_pointer("x11-display",
                              "X11 display",
                              "X11 display",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_display_x11_init(GstVaapiDisplayX11 *display)
{
    GstVaapiDisplayX11Private *priv = GST_VAAPI_DISPLAY_X11_GET_PRIVATE(display);

    display->priv = priv;
    priv->display = NULL;
}

Display *
gst_vaapi_display_x11_get_display(GstVaapiDisplayX11 *display)
{
    GstVaapiDisplayX11Private *priv = display->priv;

    return priv->display;
}

void
gst_vaapi_display_x11_set_display(GstVaapiDisplayX11 *display,
                                  Display            *x11_display)
{
    GstVaapiDisplayX11Private *priv = display->priv;

    if (x11_display) {
        VADisplay va_display = vaGetDisplay(x11_display);
        if (va_display)
            g_object_set(GST_VAAPI_DISPLAY(display),
                         "display", va_display,
                         NULL);
    }

    priv->display = x11_display;
}

GstVaapiDisplay *
gst_vaapi_display_x11_new(Display *x11_display)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_X11,
                        "x11-display", x11_display,
                        NULL);
}
