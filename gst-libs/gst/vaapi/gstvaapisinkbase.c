/*
 *  gstvaapisinkbase.c - VA sink interface
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
#include "gstvaapisinkbase.h"

static void
gst_vaapisink_base_base_init(gpointer g_class)
{
    static gboolean is_initialized = FALSE;

    if (!is_initialized) {
        is_initialized = TRUE;
    }
}

GType
gst_vaapisink_base_get_type(void)
{
    static GType iface_type = 0;

    if (G_UNLIKELY(!iface_type)) {
        static const GTypeInfo info = {
            sizeof(GstVaapiSinkBaseInterface),
            gst_vaapisink_base_base_init,       /* base_init */
            NULL,                               /* base_finalize */
        };

        iface_type = g_type_register_static(
            G_TYPE_INTERFACE,
            "GstVaapiSinkBase",
            &info,
            0
        );
    }
    return iface_type;
}

GstVaapiDisplay *
gst_vaapisink_base_get_display(GstVaapiSinkBase *sink)
{
    g_return_val_if_fail(GST_IS_VAAPISINK_BASE(sink), NULL);

    return GST_VAAPISINK_BASE_GET_INTERFACE(sink)->get_display(sink);
}

GstVaapiSinkBase *
gst_vaapisink_base_lookup(GstElement *element)
{
    GstVaapiSinkBase *sink = NULL;
    GstPad *pad, *peer;

    g_return_val_if_fail(GST_IS_ELEMENT(element), NULL);

    while (!sink) {
        pad = gst_element_get_static_pad(element, "src");
        if (!pad)
            break;

        peer = gst_pad_get_peer(pad);
        g_object_unref(pad);
        if (!peer)
            break;

        element = gst_pad_get_parent_element(peer);
        if (element) {
            if (GST_IS_VAAPISINK_BASE(element))
                sink = GST_VAAPISINK_BASE(element);
            g_object_unref(element);
        }
        g_object_unref(peer);
    }
    return sink;
}
