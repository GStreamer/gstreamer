/*
 *  test-windows.c - Test GstVaapiWindow
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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

#include <gst/vaapi/gstvaapidisplay_x11.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimage.h>
#include "image.h"

static inline void pause(void)
{
    g_print("Press any key to continue...\n");
    getchar();
}

int
main(int argc, char *argv[])
{
    GstVaapiDisplay    *display;
    GstVaapiWindow     *window;
    GstVaapiSurface    *surface;
    GstVaapiImage      *image   = NULL;
    guint flags = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    guint i;

    static const GstVaapiImageFormat image_formats[] = {
        GST_VAAPI_IMAGE_NV12,
        GST_VAAPI_IMAGE_YV12,
        GST_VAAPI_IMAGE_I420,
        GST_VAAPI_IMAGE_AYUV,
        GST_VAAPI_IMAGE_ARGB,
        GST_VAAPI_IMAGE_BGRA,
        GST_VAAPI_IMAGE_RGBA,
        GST_VAAPI_IMAGE_ABGR,
        0
    };

    static const GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
    static const guint              width       = 320;
    static const guint              height      = 240;
    static const guint              win_width   = 640;
    static const guint              win_height  = 480;

    gst_init(&argc, &argv);

    display = gst_vaapi_display_x11_new(NULL);
    if (!display)
        g_error("could not create Gst/VA display");

    surface = gst_vaapi_surface_new(display, chroma_type, width, height);
    if (!surface)
        g_error("could not create Gst/VA surface");

    for (i = 0; image_formats[i]; i++) {
        const GstVaapiImageFormat format = image_formats[i];

        image = image_generate(display, format, width, height);
        if (image) {
            if (image_upload(image, surface))
                break;
            g_object_unref(image);
        }
    }
    if (!image)
        g_error("could not create Gst/VA image");

    if (!gst_vaapi_surface_sync(surface))
        g_error("could not complete image upload");

    g_print("#\n");
    g_print("# Create window with gst_vaapi_window_x11_new()\n");
    g_print("#\n");
    {
        window = gst_vaapi_window_x11_new(display, win_width, win_height);
        if (!window)
            g_error("could not create window");

        gst_vaapi_window_show(window);

        if (!gst_vaapi_window_put_surface(window, surface, NULL, NULL, flags))
            g_error("could not render surface");

        pause();
        g_object_unref(window);
    }

    g_print("#\n");
    g_print("# Create window with gst_vaapi_window_x11_new_with_xid()\n");
    g_print("#\n");
    {
        Display * const dpy = gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(display));
        Window rootwin, win;
        int screen;
        unsigned long white_pixel, black_pixel;

        screen      = DefaultScreen(dpy);
        rootwin     = RootWindow(dpy, screen);
        white_pixel = WhitePixel(dpy, screen);
        black_pixel = BlackPixel(dpy, screen);

        win = XCreateSimpleWindow(
            dpy,
            rootwin,
            0, 0, win_width, win_height,
            0, black_pixel,
            white_pixel
        );
        if (!win)
            g_error("could not create X window");

        window = gst_vaapi_window_x11_new_with_xid(display, win);
        if (!window)
            g_error("could not create window");

        gst_vaapi_window_show(window);

        if (!gst_vaapi_window_put_surface(window, surface, NULL, NULL, flags))
            g_error("could not render surface");

        pause();
        g_object_unref(window);
        XUnmapWindow(dpy, win);
        XDestroyWindow(dpy, win);
    }

    g_object_unref(image);
    g_object_unref(surface);
    g_object_unref(display);
    gst_deinit();
    return 0;
}
