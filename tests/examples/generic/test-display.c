/*
 *  test-display.c - Test GstVaapiDisplayX11
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

#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapidisplay_x11.h>

int
main(int argc, char *argv[])
{
    Display *x11_display;
    GstVaapiDisplay *display;

    gst_init(&argc, &argv);

    x11_display = XOpenDisplay(NULL);
    if (!x11_display)
        g_error("could not create X11 display");

    display = gst_vaapi_display_x11_new(x11_display);
    if (!display)
        g_error("could not create VA-API display");

    g_object_unref(G_OBJECT(display));
    return 0;
}
