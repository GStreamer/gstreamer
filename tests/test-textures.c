/*
 *  test-textures.c - Test GstVaapiTexture
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

#include <gst/vaapi/gstvaapidisplay_glx.h>
#include <gst/vaapi/gstvaapiwindow_glx.h>

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
    GstVaapiWindowGLX  *glx_window;
    GLXContext          glx_context;
    Display            *x11_display;
    Window              x11_window;

    static const GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
    static const guint              width       = 320;
    static const guint              height      = 240;
    static const guint              win_width   = 640;
    static const guint              win_height  = 480;

    gst_init(&argc, &argv);

    display = gst_vaapi_display_glx_new(NULL);
    if (!display)
        g_error("could not create Gst/VA display");

    window = gst_vaapi_window_glx_new(display, win_width, win_height);
    if (!window)
        g_error("could not create window");

    gst_vaapi_window_show(window);

    glx_window  = GST_VAAPI_WINDOW_GLX(window);
    glx_context = gst_vaapi_window_glx_get_context(glx_window);
    x11_display = gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(display));
    x11_window  = gst_vaapi_object_get_id(GST_VAAPI_OBJECT(window));

    if (!glXMakeCurrent(x11_display, x11_window, glx_context))
        g_error("could not make VA/GLX window context current");

    pause();

    g_object_unref(window);
    g_object_unref(display);
    gst_deinit();
    return 0;
}
