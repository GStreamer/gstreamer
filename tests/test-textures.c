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

static void
print_caps(GstCaps *caps, const gchar *name)
{
    guint i, n_caps = gst_caps_get_size(caps);

    g_print("%u %s caps\n", n_caps, name);

    for (i = 0; i < gst_caps_get_size(caps); i++) {
        GstStructure * const structure = gst_caps_get_structure(caps, i);
        if (!structure)
            g_error("could not get caps structure %d", i);

        g_print("  %s:", gst_structure_get_name(structure));

        if (gst_structure_has_name(structure, "video/x-raw-yuv")) {
            guint32 fourcc;

            gst_structure_get_fourcc(structure, "format", &fourcc);

            g_print(" fourcc '%c%c%c%c'",
                    fourcc & 0xff,
                    (fourcc >> 8) & 0xff,
                    (fourcc >> 16) & 0xff,
                    (fourcc >> 24) & 0xff);
        }
        else {
            gint bpp, endian, rmask, gmask, bmask, amask;
            gboolean has_alpha;

            gst_structure_get_int(structure, "bpp",         &bpp);
            gst_structure_get_int(structure, "endianness",  &endian);
            gst_structure_get_int(structure, "red_mask",    &rmask);
            gst_structure_get_int(structure, "blue_mask",   &bmask);
            gst_structure_get_int(structure, "green_mask",  &gmask);
            has_alpha = gst_structure_get_int(structure, "alpha_mask", &amask);

            g_print(" %d bits per pixel, %s endian,",
                    bpp, endian == G_BIG_ENDIAN ? "big" : "little");
            g_print(" %s masks", has_alpha ? "rgba" : "rgb");
            g_print(" 0x%08x 0x%08x 0x%08x", rmask, gmask, bmask);
            if (has_alpha)
                g_print(" 0x%08x", amask);
        }
        g_print("\n");
    }
}

static void
dump_caps(GstVaapiDisplay *display)
{
    GstCaps *caps;

    caps = gst_vaapi_display_get_image_caps(display);
    if (!caps)
        g_error("could not get VA image caps");

    print_caps(caps, "image");
    gst_caps_unref(caps);

    caps = gst_vaapi_display_get_subpicture_caps(display);
    if (!caps)
        g_error("could not get VA subpicture caps");

    print_caps(caps, "subpicture");
    gst_caps_unref(caps);
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

    dump_caps(display);

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
