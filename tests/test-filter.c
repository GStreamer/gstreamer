/*
 *  test-filter.c - Test GstVaapiFilter
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012-2013 Intel Corporation
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

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapifilter.h>
#include <gst/vaapi/gstvaapiwindow.h>
#include "image.h"
#include "output.h"

static gchar *g_src_format_str;
static gchar *g_crop_rect_str;

static GOptionEntry g_options[] = {
    { "src-format", 's',
      0,
      G_OPTION_ARG_STRING, &g_src_format_str,
      "source surface format", NULL },
    { "crop-rect", 'c',
      0,
      G_OPTION_ARG_STRING, &g_crop_rect_str,
      "cropping rectangle", NULL },
    { NULL, }
};

#define APP_ERROR app_error_quark()
static GQuark
app_error_quark(void)
{
    static gsize g_quark;

    if (g_once_init_enter(&g_quark)) {
        gsize quark = (gsize)g_quark_from_static_string("AppError");
        g_once_init_leave(&g_quark, quark);
    }
    return g_quark;
}

typedef enum {
    APP_ERROR_NONE,
    APP_ERROR_CREATE_TEST_SURFACE,
} AppError;

static inline void
pause(void)
{
    g_print("Press any key to continue...\n");
    getchar();
}

static GstVaapiSurface *
create_test_surface(GstVaapiDisplay *display, guint width, guint height,
    GError **error_ptr)
{
    GstVideoFormat format = GST_VIDEO_FORMAT_I420;
    GstVaapiSurface *surface = NULL;
    GstVaapiImage *image = NULL;
    GError *error = NULL;

    if (g_src_format_str) {
        format = gst_vaapi_video_format_from_string(g_src_format_str);
        if (format == GST_VIDEO_FORMAT_UNKNOWN)
            goto error_invalid_format;
    }

    surface = gst_vaapi_surface_new_with_format(display, format, width, height);
    if (!surface)
        goto error_create_surface;

    image = image_generate(display, format, width, height);
    if (!image)
        goto error_create_image;

    if (!image_upload(image, surface))
        goto error_upload_image;

    gst_vaapi_object_unref(image);
    return surface;

    /* ERRORS */
error_invalid_format:
    error = g_error_new(APP_ERROR, APP_ERROR_CREATE_TEST_SURFACE,
        "unknown format %s", g_src_format_str);
    goto error_cleanup;
error_create_surface:
    error = g_error_new(APP_ERROR, APP_ERROR_CREATE_TEST_SURFACE,
        "unsupported format %s", gst_vaapi_video_format_to_string(format));
    goto error_cleanup;
error_create_image:
    error = g_error_new(APP_ERROR, APP_ERROR_CREATE_TEST_SURFACE,
        "unsupported %s image", gst_vaapi_video_format_to_string(format));
    goto error_cleanup;
error_upload_image:
    error = g_error_new(APP_ERROR, APP_ERROR_CREATE_TEST_SURFACE,
        "failed to upload %s image", gst_vaapi_video_format_to_string(format));
    goto error_cleanup;
error_cleanup:
    if (image)
        gst_vaapi_object_unref(image);
    if (surface)
        gst_vaapi_object_unref(surface);
    if (error_ptr)
        *error_ptr = error;
    else
        g_error_free(error);
    return NULL;
}

static void
dump_operation(GstVaapiFilterOpInfo *op_info)
{
    GParamSpec * const pspec = op_info->pspec;
    GValue value = G_VALUE_INIT;
    gchar *value_str;

    if (!op_info)
        return;

    g_print("  %s: ", g_param_spec_get_name(pspec));
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(pspec));
    g_param_value_set_default(pspec, &value);
    value_str = g_strdup_value_contents(&value);
    g_print("%s (default: %s)\n", G_VALUE_TYPE_NAME(&value),
        value_str ? value_str : "<unknown>");
    g_free(value_str);
}

static void
dump_operations(GstVaapiFilter *filter)
{
    GPtrArray * const ops = gst_vaapi_filter_get_operations(filter);
    guint i;

    if (!ops)
        return;

    g_print("%u operations\n", ops->len);
    for (i = 0; i < ops->len; i++)
        dump_operation(g_ptr_array_index(ops, i));
    g_ptr_array_unref(ops);
}

static void
dump_formats(GstVaapiFilter *filter)
{
    GArray * const formats = gst_vaapi_filter_get_formats(filter);
    guint i;

    if (!formats)
        return;

    g_print("%u formats\n", formats->len);
    for (i = 0; i < formats->len; i++) {
        GstVideoFormat format = g_array_index(formats, GstVideoFormat, i);
        g_print("  %s\n", gst_vaapi_video_format_to_string(format));
    }
    g_array_unref(formats);
}

static gboolean
parse_crop_rect(const gchar *str, GstVaapiRectangle *crop_rect)
{
    if (str) {
        // Format: <WIDTH> 'x' <HEIGHT>
        if (sscanf(str, "%ux%u", &crop_rect->width, &crop_rect->height) == 2) {
            crop_rect->x = 0;
            crop_rect->y = 0;
            return TRUE;
        }

        // Format: '('? <X> ',' <Y> ')'? <WIDTH> 'x' <HEIGHT>
        if (sscanf(str, "(%d,%d):%ux%u", &crop_rect->x, &crop_rect->y,
                   &crop_rect->width, &crop_rect->height) == 4 ||
            sscanf(str, "%d,%d:%ux%u", &crop_rect->x, &crop_rect->y,
                   &crop_rect->width, &crop_rect->height) == 4)
            return TRUE;
    }
    return FALSE;
}

int
main(int argc, char *argv[])
{
    GstVaapiDisplay *display;
    GstVaapiWindow *window;
    GstVaapiSurface *src_surface, *dst_surface;
    GstVaapiFilter *filter = NULL;
    GstVaapiFilterStatus status;
    guint filter_flags = 0;
    GError *error = NULL;

    static const guint src_width        = 320;
    static const guint src_height       = 240;
    static const guint dst_width        = 480;
    static const guint dst_height       = 360;
    static const guint win_width        = 640;
    static const guint win_height       = 480;

    if (!video_output_init(&argc, argv, g_options))
        g_error("failed to initialize video output subsystem");

    display = video_output_create_display(NULL);
    if (!display)
        g_error("failed to create VA display");

    window = video_output_create_window(display, win_width, win_height);
    if (!window)
        g_error("failed to create window");

    src_surface = create_test_surface(display, src_width, src_height, &error);
    if (!src_surface)
        g_error("failed to create source VA surface: %s", error->message);

    dst_surface = gst_vaapi_surface_new(display, GST_VAAPI_CHROMA_TYPE_YUV420,
        dst_width, dst_height);
    if (!dst_surface)
        g_error("failed to create target VA surface");

    filter = gst_vaapi_filter_new(display);
    if (!filter)
        g_error("failed to create video processing pipeline");

    dump_operations(filter);
    dump_formats(filter);

    if (g_crop_rect_str) {
        GstVaapiRectangle crop_rect;

        if (!parse_crop_rect(g_crop_rect_str, &crop_rect))
            g_error("failed to parse cropping rectangle");

        printf("Frame cropping: (%d,%d), size %ux%u\n",
               crop_rect.x, crop_rect.y, crop_rect.width, crop_rect.height);

        if (!gst_vaapi_filter_set_cropping_rectangle(filter, &crop_rect))
            g_error("failed to set cropping rectangle");
    }

    status = gst_vaapi_filter_process(filter, src_surface, dst_surface,
        filter_flags);
    if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
        g_error("failed to process video filters");

    gst_vaapi_window_show(window);

    if (!gst_vaapi_window_put_surface(window, dst_surface, NULL, NULL,
            GST_VAAPI_PICTURE_STRUCTURE_FRAME))
        g_error("failed to render target surface");

    pause();

    gst_vaapi_filter_unref(filter);
    gst_vaapi_object_unref(dst_surface);
    gst_vaapi_object_unref(src_surface);
    gst_vaapi_window_unref(window);
    gst_vaapi_display_unref(display);
    video_output_exit();
    g_free(g_src_format_str);
    g_free(g_crop_rect_str);
    return 0;
}
