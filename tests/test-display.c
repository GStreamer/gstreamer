/*
 *  test-display.c - Test GstVaapiDisplayX11
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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

#include "config.h"
#include <gst/video/video.h>
#if USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
# include <va/va_drm.h>
# include <fcntl.h>
# include <unistd.h>
# ifndef DRM_DEVICE_PATH
# define DRM_DEVICE_PATH "/dev/dri/card0"
# endif
#endif
#if USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
#endif
#if USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
#endif
#if USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
#endif

#ifdef HAVE_VA_VA_GLX_H
# include <va/va_glx.h>
#endif

static void
print_value(const GValue *value, const gchar *name)
{
    gchar *value_string;

    value_string = g_strdup_value_contents(value);
    if (!value_string)
        return;
    g_print("  %s: %s\n", name, value_string);
    g_free(value_string);
}

static void
print_profile_caps(GstCaps *caps, const gchar *name)
{
    guint i, n_caps = gst_caps_get_size(caps);
    gint version;
    const gchar *profile;
    gboolean has_version;

    g_print("%u %s caps\n", n_caps, name);

    for (i = 0; i < gst_caps_get_size(caps); i++) {
        GstStructure * const structure = gst_caps_get_structure(caps, i);
        if (!structure)
            g_error("could not get caps structure %d", i);

        has_version = (
            gst_structure_get_int(structure, "version", &version) ||
            gst_structure_get_int(structure, "mpegversion", &version)
        );

        g_print("  %s", gst_structure_get_name(structure));
        if (has_version)
            g_print("%d", version);

        profile = gst_structure_get_string(structure, "profile");
        if (!profile)
            g_error("could not get structure profile");
        g_print(": %s profile\n", profile);
    }
}

static void
print_format_caps(GstCaps *caps, const gchar *name)
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

typedef struct _GstVaapiDisplayProperty GstVaapiDisplayProperty;
struct _GstVaapiDisplayProperty {
    const gchar *name;
    GValue       value;
};

static void
gst_vaapi_display_property_free(GstVaapiDisplayProperty *prop)
{
    if (!prop)
        return;
    g_value_unset(&prop->value);
    g_slice_free(GstVaapiDisplayProperty, prop);
}

static GstVaapiDisplayProperty *
gst_vaapi_display_property_new(const gchar *name)
{
    GstVaapiDisplayProperty *prop;

    prop = g_slice_new0(GstVaapiDisplayProperty);
    if (!prop)
        return NULL;
    prop->name = name;
    return prop;
}

static void
free_property_cb(gpointer data, gpointer user_data)
{
    gst_vaapi_display_property_free(data);
}

static inline GParamSpec *
get_display_property(GstVaapiDisplay *display, const gchar *name)
{
    GObjectClass *klass;

    klass = G_OBJECT_CLASS(GST_VAAPI_DISPLAY_GET_CLASS(display));
    if (!klass)
        return NULL;
    return g_object_class_find_property(klass, name);
}

static void
dump_properties(GstVaapiDisplay *display)
{
    GstVaapiDisplayProperty *prop;
    GPtrArray *properties;
    guint i;

    static const gchar *g_properties[] = {
        GST_VAAPI_DISPLAY_PROP_RENDER_MODE,
        GST_VAAPI_DISPLAY_PROP_ROTATION,
        GST_VAAPI_DISPLAY_PROP_HUE,
        GST_VAAPI_DISPLAY_PROP_SATURATION,
        GST_VAAPI_DISPLAY_PROP_BRIGHTNESS,
        GST_VAAPI_DISPLAY_PROP_CONTRAST,
        NULL
    };

    properties = g_ptr_array_new();
    if (!properties)
        return;

    for (i = 0; g_properties[i] != NULL; i++) {
        GParamSpec *pspec = get_display_property(display, g_properties[i]);

        if (!pspec) {
            GST_ERROR("failed to find GstVaapiDisplay property '%s'",
                      g_properties[i]);
            goto end;
        }

        if (!gst_vaapi_display_has_property(display, pspec->name))
            continue;
            
        prop = gst_vaapi_display_property_new(pspec->name);
        if (!prop) {
            GST_ERROR("failed to allocate GstVaapiDisplayProperty");
            goto end;
        }

        g_value_init(&prop->value, pspec->value_type);
        g_object_get_property(G_OBJECT(display), pspec->name, &prop->value);
        g_ptr_array_add(properties, prop);
    }

    g_print("%u properties\n", properties->len);
    for (i = 0; i < properties->len; i++) {
        prop = g_ptr_array_index(properties, i);
        print_value(&prop->value, prop->name);
    }

end:
    if (properties) {
        g_ptr_array_foreach(properties, free_property_cb, NULL);
        g_ptr_array_free(properties, TRUE);
    }
}

static void
dump_info(GstVaapiDisplay *display)
{
    GstCaps *caps;

    caps = gst_vaapi_display_get_decode_caps(display);
    if (!caps)
        g_error("could not get VA decode caps");

    print_profile_caps(caps, "decoders");
    gst_caps_unref(caps);

    caps = gst_vaapi_display_get_encode_caps(display);
    if (!caps)
        g_error("could not get VA encode caps");

    print_profile_caps(caps, "encoders");
    gst_caps_unref(caps);

    caps = gst_vaapi_display_get_image_caps(display);
    if (!caps)
        g_error("could not get VA image caps");

    print_format_caps(caps, "image");
    gst_caps_unref(caps);

    caps = gst_vaapi_display_get_subpicture_caps(display);
    if (!caps)
        g_error("could not get VA subpicture caps");

    print_format_caps(caps, "subpicture");
    gst_caps_unref(caps);

    dump_properties(display);
}

int
main(int argc, char *argv[])
{
    GstVaapiDisplay *display;
    guint width, height, par_n, par_d;

    gst_init(&argc, &argv);

#if USE_DRM
    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_drm_new()\n");
    g_print("#\n");
    {
        display = gst_vaapi_display_drm_new(NULL);
        if (!display)
            g_error("could not create Gst/VA display");

        dump_info(display);
        g_object_unref(display);
    }
    g_print("\n");

    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_drm_new_with_device()\n");
    g_print("#\n");
    {
        int drm_device;

        drm_device = open(DRM_DEVICE_PATH, O_RDWR|O_CLOEXEC);
        if (drm_device < 0)
            g_error("could not open DRM device");

        display = gst_vaapi_display_drm_new_with_device(drm_device);
        if (!display)
            g_error("could not create Gst/VA display");

        dump_info(display);
        g_object_unref(display);
        close(drm_device);
    }
    g_print("\n");

    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_new_with_display() [vaGetDisplayDRM()]\n");
    g_print("#\n");
    {
        int drm_device;
        VADisplay va_display;

        drm_device = open(DRM_DEVICE_PATH, O_RDWR|O_CLOEXEC);
        if (drm_device < 0)
            g_error("could not open DRM device");

        va_display = vaGetDisplayDRM(drm_device);
        if (!va_display)
            g_error("could not create VA display");

        display = gst_vaapi_display_new_with_display(va_display);
        if (!display)
            g_error("could not create Gst/VA display");

        dump_info(display);
        g_object_unref(display);
        close(drm_device);
    }
    g_print("\n");
#endif

#if USE_X11
    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_x11_new()\n");
    g_print("#\n");
    {
        display = gst_vaapi_display_x11_new(NULL);
        if (!display)
            g_error("could not create Gst/VA display");

        gst_vaapi_display_get_size(display, &width, &height);
        g_print("Display size: %ux%u\n", width, height);

        gst_vaapi_display_get_pixel_aspect_ratio(display, &par_n, &par_d);
        g_print("Pixel aspect ratio: %u/%u\n", par_n, par_d);

        dump_info(display);
        g_object_unref(display);
    }
    g_print("\n");

    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_x11_new_with_display()\n");
    g_print("#\n");
    {
        Display *x11_display;

        x11_display = XOpenDisplay(NULL);
        if (!x11_display)
            g_error("could not create X11 display");

        display = gst_vaapi_display_x11_new_with_display(x11_display);
        if (!display)
            g_error("could not create Gst/VA display");

        dump_info(display);
        g_object_unref(display);
        XCloseDisplay(x11_display);
    }
    g_print("\n");

    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_new_with_display() [vaGetDisplay()]\n");
    g_print("#\n");
    {
        Display *x11_display;
        VADisplay va_display;

        x11_display = XOpenDisplay(NULL);
        if (!x11_display)
            g_error("could not create X11 display");

        va_display = vaGetDisplay(x11_display);
        if (!va_display)
            g_error("could not create VA display");

        display = gst_vaapi_display_new_with_display(va_display);
        if (!display)
            g_error("could not create Gst/VA display");

        dump_info(display);
        g_object_unref(display);
        XCloseDisplay(x11_display);
    }
    g_print("\n");
#endif

#if USE_GLX
    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_glx_new()\n");
    g_print("#\n");
    {
        display = gst_vaapi_display_glx_new(NULL);
        if (!display)
            g_error("could not create Gst/VA display");

        gst_vaapi_display_get_size(display, &width, &height);
        g_print("Display size: %ux%u\n", width, height);

        gst_vaapi_display_get_pixel_aspect_ratio(display, &par_n, &par_d);
        g_print("Pixel aspect ratio: %u/%u\n", par_n, par_d);

        dump_info(display);
        g_object_unref(display);
    }
    g_print("\n");

    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_glx_new_with_display()\n");
    g_print("#\n");
    {
        Display *x11_display;

        x11_display = XOpenDisplay(NULL);
        if (!x11_display)
            g_error("could not create X11 display");

        display = gst_vaapi_display_glx_new_with_display(x11_display);
        if (!display)
            g_error("could not create Gst/VA display");

        dump_info(display);
        g_object_unref(display);
        XCloseDisplay(x11_display);
    }
    g_print("\n");

#ifdef HAVE_VA_VA_GLX_H
    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_new_with_display() [vaGetDisplayGLX()]\n");
    g_print("#\n");
    {
        Display *x11_display;
        VADisplay va_display;

        x11_display = XOpenDisplay(NULL);
        if (!x11_display)
            g_error("could not create X11 display");

        va_display = vaGetDisplayGLX(x11_display);
        if (!va_display)
            g_error("could not create VA display");

        display = gst_vaapi_display_new_with_display(va_display);
        if (!display)
            g_error("could not create Gst/VA display");

        dump_info(display);
        g_object_unref(display);
        XCloseDisplay(x11_display);
    }
    g_print("\n");
#endif
#endif

#if USE_WAYLAND
    g_print("#\n");
    g_print("# Create display with gst_vaapi_display_wayland_new()\n");
    g_print("#\n");
    {
        display = gst_vaapi_display_wayland_new(NULL);
        if (!display)
            g_error("could not create Gst/VA display");

        gst_vaapi_display_get_size(display, &width, &height);
        g_print("Display size: %ux%u\n", width, height);

        gst_vaapi_display_get_pixel_aspect_ratio(display, &par_n, &par_d);
        g_print("Pixel aspect ratio: %u/%u\n", par_n, par_d);

        dump_info(display);
        g_object_unref(display);
    }
    g_print("\n");
#endif

    gst_deinit();
    return 0;
}
