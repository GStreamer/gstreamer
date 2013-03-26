/*
 *  gstvaapisink.c - VA-API video sink
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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

/**
 * SECTION:gstvaapisink
 * @short_description: A VA-API based videosink
 *
 * vaapisink renders video frames to a drawable (X #Window) on a local
 * display using the Video Acceleration (VA) API. The element will
 * create its own internal window and render into it.
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videocontext.h>
#include <gst/vaapi/gstvaapivalue.h>
#if USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
#endif
#if USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
# include <gst/vaapi/gstvaapiwindow_x11.h>
#endif
#if USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
# include <gst/vaapi/gstvaapiwindow_glx.h>
#endif
#if USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
# include <gst/vaapi/gstvaapiwindow_wayland.h>
#endif

/* Supported interfaces */
#include <gst/interfaces/xoverlay.h>

#include "gstvaapisink.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"

#define GST_PLUGIN_NAME "vaapisink"
#define GST_PLUGIN_DESC "A VA-API based videosink"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapisink);
#define GST_CAT_DEFAULT gst_debug_vaapisink

/* Default template */
static const char gst_vaapisink_sink_caps_str[] =
    "video/x-raw-yuv, "
    "width  = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; "
    GST_VAAPI_SURFACE_CAPS;

static GstStaticPadTemplate gst_vaapisink_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapisink_sink_caps_str));

/* GstImplementsInterface interface */
static gboolean
gst_vaapisink_implements_interface_supported(
    GstImplementsInterface *iface,
    GType                   type
)
{
    return (type == GST_TYPE_VIDEO_CONTEXT ||
            type == GST_TYPE_X_OVERLAY);
}

static void
gst_vaapisink_implements_iface_init(GstImplementsInterfaceClass *iface)
{
    iface->supported = gst_vaapisink_implements_interface_supported;
}

/* GstVideoContext interface */
static void
gst_vaapisink_set_video_context(GstVideoContext *context, const gchar *type,
    const GValue *value)
{
  GstVaapiSink *sink = GST_VAAPISINK (context);
  gst_vaapi_set_display (type, value, &sink->display);
}

static void
gst_vaapisink_video_context_iface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapisink_set_video_context;
}

static void
gst_vaapisink_xoverlay_iface_init(GstXOverlayClass *iface);

G_DEFINE_TYPE_WITH_CODE(
    GstVaapiSink,
    gst_vaapisink,
    GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapisink_implements_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_vaapisink_video_context_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_X_OVERLAY,
                          gst_vaapisink_xoverlay_iface_init))

enum {
    PROP_0,

    PROP_DISPLAY_TYPE,
    PROP_FULLSCREEN,
    PROP_SYNCHRONOUS,
    PROP_USE_REFLECTION,
    PROP_ROTATION,
};

#define DEFAULT_DISPLAY_TYPE            GST_VAAPI_DISPLAY_TYPE_ANY
#define DEFAULT_ROTATION                GST_VAAPI_ROTATION_0

/* GstXOverlay interface */

#if USE_X11
static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id);
#endif

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *buffer);

static void
gst_vaapisink_xoverlay_set_window_handle(GstXOverlay *overlay, guintptr window)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);

    /* Disable GLX rendering when vaapisink is using a foreign X
       window. It's pretty much useless */
    if (sink->display_type == GST_VAAPI_DISPLAY_TYPE_GLX)
        sink->display_type = GST_VAAPI_DISPLAY_TYPE_X11;

    sink->foreign_window = TRUE;

    switch (sink->display_type) {
#if USE_X11
    case GST_VAAPI_DISPLAY_TYPE_X11:
        gst_vaapisink_ensure_window_xid(sink, window);
        break;
#endif
    default:
        break;
    }
}

static void
gst_vaapisink_xoverlay_set_render_rectangle(
    GstXOverlay *overlay,
    gint         x,
    gint         y,
    gint         width,
    gint         height
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);
    GstVaapiRectangle * const display_rect = &sink->display_rect;

    display_rect->x      = x;
    display_rect->y      = y;
    display_rect->width  = width;
    display_rect->height = height;
    
    GST_DEBUG("render rect (%d,%d):%ux%u",
              display_rect->x, display_rect->y,
              display_rect->width, display_rect->height);
}

static void
gst_vaapisink_xoverlay_expose(GstXOverlay *overlay)
{
    GstBaseSink * const base_sink = GST_BASE_SINK(overlay);
    GstBuffer *buffer;

    buffer = gst_base_sink_get_last_buffer(base_sink);
    if (buffer) {
        gst_vaapisink_show_frame(base_sink, buffer);
        gst_buffer_unref(buffer);
    }
}

static void
gst_vaapisink_xoverlay_iface_init(GstXOverlayClass *iface)
{
    iface->set_window_handle    = gst_vaapisink_xoverlay_set_window_handle;
    iface->set_render_rectangle = gst_vaapisink_xoverlay_set_render_rectangle;
    iface->expose               = gst_vaapisink_xoverlay_expose;
}

static void
gst_vaapisink_destroy(GstVaapiSink *sink)
{
    gst_buffer_replace(&sink->video_buffer, NULL);
    g_clear_object(&sink->texture);
    g_clear_object(&sink->display);
    g_clear_object(&sink->uploader);

    gst_caps_replace(&sink->caps, NULL);
}

#if USE_X11
/* Checks whether a ConfigureNotify event is in the queue */
typedef struct _ConfigureNotifyEventPendingArgs ConfigureNotifyEventPendingArgs;
struct _ConfigureNotifyEventPendingArgs {
    Window      window;
    guint       width;
    guint       height;
    gboolean    match;
};

static Bool
configure_notify_event_pending_cb(Display *dpy, XEvent *xev, XPointer arg)
{
    ConfigureNotifyEventPendingArgs * const args =
        (ConfigureNotifyEventPendingArgs *)arg;

    if (xev->type == ConfigureNotify &&
        xev->xconfigure.window == args->window &&
        xev->xconfigure.width  == args->width  &&
        xev->xconfigure.height == args->height)
        args->match = TRUE;

    /* XXX: this is a hack to traverse the whole queue because we
       can't use XPeekIfEvent() since it could block */
    return False;
}

static gboolean
configure_notify_event_pending(
    GstVaapiSink *sink,
    Window        window,
    guint         width,
    guint         height
)
{
    ConfigureNotifyEventPendingArgs args;
    XEvent xev;

    args.window = window;
    args.width  = width;
    args.height = height;
    args.match  = FALSE;

    /* XXX: don't use XPeekIfEvent() because it might block */
    XCheckIfEvent(
        gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(sink->display)),
        &xev,
        configure_notify_event_pending_cb, (XPointer)&args
    );
    return args.match;
}
#endif

static const gchar *
get_display_type_name(GstVaapiDisplayType display_type)
{
    gpointer const klass = g_type_class_peek(GST_VAAPI_TYPE_DISPLAY_TYPE);
    GEnumValue * const e = g_enum_get_value(klass, display_type);

    if (e)
        return e->value_name;
    return "<unknown-type>";
}

static inline gboolean
gst_vaapisink_ensure_display(GstVaapiSink *sink)
{
    GstVaapiDisplayType display_type;
    GstVaapiRenderMode render_mode;

    if (!gst_vaapi_ensure_display(sink, sink->display_type, &sink->display))
        return FALSE;

    display_type = gst_vaapi_display_get_display_type(sink->display);
    if (display_type != sink->display_type) {
        GST_INFO("created %s %p", get_display_type_name(display_type),
            sink->display);
        sink->display_type = display_type;

        sink->use_overlay =
            gst_vaapi_display_get_render_mode(sink->display, &render_mode) &&
            render_mode == GST_VAAPI_RENDER_MODE_OVERLAY;
        GST_DEBUG("use %s rendering mode", sink->use_overlay ? "overlay" : "texture");

        sink->use_rotation = gst_vaapi_display_has_property(
            sink->display, GST_VAAPI_DISPLAY_PROP_ROTATION);
    }
    return TRUE;
}

static gboolean
gst_vaapisink_ensure_uploader(GstVaapiSink *sink)
{
    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;

    if (!sink->uploader) {
        sink->uploader = gst_vaapi_uploader_new(sink->display);
        if (!sink->uploader)
            return FALSE;
    }
    return TRUE;
}

static gboolean
gst_vaapisink_ensure_render_rect(GstVaapiSink *sink, guint width, guint height)
{
    GstVaapiRectangle * const display_rect = &sink->display_rect;
    guint num, den, display_par_n, display_par_d;
    gboolean success;

    /* Return success if caps are not set yet */
    if (!sink->caps)
        return TRUE;

    GST_DEBUG("ensure render rect within %ux%u bounds", width, height);

    gst_vaapi_display_get_pixel_aspect_ratio(
        sink->display,
        &display_par_n, &display_par_d
    );
    GST_DEBUG("display pixel-aspect-ratio %d/%d",
              display_par_n, display_par_d);

    success = gst_video_calculate_display_ratio(
        &num, &den,
        sink->video_width, sink->video_height,
        sink->video_par_n, sink->video_par_d,
        display_par_n, display_par_d
    );
    if (!success)
        return FALSE;
    GST_DEBUG("video size %dx%d, calculated ratio %d/%d",
              sink->video_width, sink->video_height, num, den);

    display_rect->width = gst_util_uint64_scale_int(height, num, den);
    if (display_rect->width <= width) {
        GST_DEBUG("keeping window height");
        display_rect->height = height;
    }
    else {
        GST_DEBUG("keeping window width");
        display_rect->width  = width;
        display_rect->height =
            gst_util_uint64_scale_int(width, den, num);
    }
    GST_DEBUG("scaling video to %ux%u", display_rect->width, display_rect->height);

    g_assert(display_rect->width  <= width);
    g_assert(display_rect->height <= height);

    display_rect->x = (width  - display_rect->width)  / 2;
    display_rect->y = (height - display_rect->height) / 2;

    GST_DEBUG("render rect (%d,%d):%ux%u",
              display_rect->x, display_rect->y,
              display_rect->width, display_rect->height);
    return TRUE;
}

static void
gst_vaapisink_ensure_window_size(GstVaapiSink *sink, guint *pwidth, guint *pheight)
{
    GstVideoRectangle src_rect, dst_rect, out_rect;
    guint num, den, display_width, display_height, display_par_n, display_par_d;
    gboolean success, scale;

    if (sink->foreign_window) {
        *pwidth  = sink->window_width;
        *pheight = sink->window_height;
        return;
    }

    gst_vaapi_display_get_size(sink->display, &display_width, &display_height);
    if (sink->fullscreen) {
        *pwidth  = display_width;
        *pheight = display_height;
        return;
    }

    gst_vaapi_display_get_pixel_aspect_ratio(
        sink->display,
        &display_par_n, &display_par_d
    );

    success = gst_video_calculate_display_ratio(
        &num, &den,
        sink->video_width, sink->video_height,
        sink->video_par_n, sink->video_par_d,
        display_par_n, display_par_d
    );
    if (!success) {
        num = sink->video_par_n;
        den = sink->video_par_d;
    }

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.w = gst_util_uint64_scale_int(sink->video_height, num, den);
    src_rect.h = sink->video_height;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = display_width;
    dst_rect.h = display_height;
    scale      = (src_rect.w > dst_rect.w || src_rect.h > dst_rect.h);
    gst_video_sink_center_rect(src_rect, dst_rect, &out_rect, scale);
    *pwidth    = out_rect.w;
    *pheight   = out_rect.h;
}

static inline gboolean
gst_vaapisink_ensure_window(GstVaapiSink *sink, guint width, guint height)
{
    GstVaapiDisplay * const display = sink->display;

    if (!sink->window) {
        switch (sink->display_type) {
#if USE_GLX
        case GST_VAAPI_DISPLAY_TYPE_GLX:
            sink->window = gst_vaapi_window_glx_new(display, width, height);
            goto notify_xoverlay_interface;
#endif
#if USE_X11
        case GST_VAAPI_DISPLAY_TYPE_X11:
            sink->window = gst_vaapi_window_x11_new(display, width, height);
        notify_xoverlay_interface:
            if (!sink->window)
                break;
            gst_x_overlay_got_window_handle(
                GST_X_OVERLAY(sink),
                gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window))
            );
            break;
#endif
#if USE_WAYLAND
        case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
            sink->window = gst_vaapi_window_wayland_new(display, width, height);
            break;
#endif
        default:
            GST_ERROR("unsupported display type %d", sink->display_type);
            return FALSE;
        }
    }
    return sink->window != NULL;
}

#if USE_X11
static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id)
{
    Window rootwin;
    unsigned int width, height, border_width, depth;
    int x, y;
    XID xid = window_id;

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;

    gst_vaapi_display_lock(sink->display);
    XGetGeometry(
        gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(sink->display)),
        xid,
        &rootwin,
        &x, &y, &width, &height, &border_width, &depth
    );
    gst_vaapi_display_unlock(sink->display);

    if ((width != sink->window_width || height != sink->window_height) &&
        !configure_notify_event_pending(sink, xid, width, height)) {
        if (!gst_vaapisink_ensure_render_rect(sink, width, height))
            return FALSE;
        sink->window_width  = width;
        sink->window_height = height;
    }

    if (sink->window &&
        gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window)) == xid)
        return TRUE;

    g_clear_object(&sink->window);

    switch (sink->display_type) {
#if USE_GLX
    case GST_VAAPI_DISPLAY_TYPE_GLX:
        sink->window = gst_vaapi_window_glx_new_with_xid(sink->display, xid);
        break;
#endif
    case GST_VAAPI_DISPLAY_TYPE_X11:
        sink->window = gst_vaapi_window_x11_new_with_xid(sink->display, xid);
        break;
    default:
        GST_ERROR("unsupported display type %d", sink->display_type);
        return FALSE;
    }
    return sink->window != NULL;
}
#endif

static gboolean
gst_vaapisink_ensure_rotation(GstVaapiSink *sink, gboolean recalc_display_rect)
{
    gboolean success = FALSE;

    g_return_val_if_fail(sink->display, FALSE);

    if (sink->rotation == sink->rotation_req)
        return TRUE;

    if (!sink->use_rotation) {
        GST_WARNING("VA display does not support rotation");
        goto end;
    }

    gst_vaapi_display_lock(sink->display);
    success = gst_vaapi_display_set_rotation(sink->display, sink->rotation_req);
    gst_vaapi_display_unlock(sink->display);
    if (!success) {
        GST_ERROR("failed to change VA display rotation mode");
        goto end;
    }

    if (((sink->rotation + sink->rotation_req) % 180) == 90) {
        /* Orientation changed */
        G_PRIMITIVE_SWAP(guint, sink->video_width, sink->video_height);
        G_PRIMITIVE_SWAP(gint, sink->video_par_n, sink->video_par_d);
    }

    if (recalc_display_rect && !sink->foreign_window)
        gst_vaapisink_ensure_render_rect(sink, sink->window_width,
            sink->window_height);
    success = TRUE;

end:
    sink->rotation = sink->rotation_req;
    return success;
}

static gboolean
gst_vaapisink_start(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;

    sink->uploader = gst_vaapi_uploader_new(sink->display);
    if (!sink->uploader)
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapisink_stop(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    gst_buffer_replace(&sink->video_buffer, NULL);
    g_clear_object(&sink->window);
    g_clear_object(&sink->display);
    g_clear_object(&sink->uploader);

    return TRUE;
}

static GstCaps *
gst_vaapisink_get_caps(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstCaps *out_caps, *yuv_caps;

    out_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS);
    if (!out_caps)
        return NULL;

    if (gst_vaapisink_ensure_uploader(sink)) {
        yuv_caps = gst_vaapi_uploader_get_caps(sink->uploader);
        if (yuv_caps)
            gst_caps_append(out_caps, gst_caps_copy(yuv_caps));
    }
    return out_caps;
}

static gboolean
gst_vaapisink_set_caps(GstBaseSink *base_sink, GstCaps *caps)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVideoInfo vi;
    guint win_width, win_height;

#if USE_DRM
    if (sink->display_type == GST_VAAPI_DISPLAY_TYPE_DRM)
        return TRUE;
#endif

    if (!gst_video_info_from_caps(&vi, caps))
        return FALSE;
    sink->video_width  = GST_VIDEO_INFO_WIDTH(&vi);
    sink->video_height = GST_VIDEO_INFO_HEIGHT(&vi);
    sink->video_par_n  = GST_VIDEO_INFO_PAR_N(&vi);
    sink->video_par_d  = GST_VIDEO_INFO_PAR_D(&vi);
    GST_DEBUG("video pixel-aspect-ratio %d/%d",
              sink->video_par_n, sink->video_par_d);

    if (gst_structure_has_name(structure, "video/x-raw-yuv"))
        sink->use_video_raw = TRUE;

    gst_caps_replace(&sink->caps, caps);

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;

    gst_vaapisink_ensure_rotation(sink, FALSE);

    gst_vaapisink_ensure_window_size(sink, &win_width, &win_height);
    if (sink->window) {
        if (!sink->foreign_window || sink->fullscreen)
            gst_vaapi_window_set_size(sink->window, win_width, win_height);
    }
    else {
        gst_vaapi_display_lock(sink->display);
        gst_x_overlay_prepare_xwindow_id(GST_X_OVERLAY(sink));
        gst_vaapi_display_unlock(sink->display);
        if (sink->window)
            return TRUE;
        if (!gst_vaapisink_ensure_window(sink, win_width, win_height))
            return FALSE;
        gst_vaapi_window_set_fullscreen(sink->window, sink->fullscreen);
        gst_vaapi_window_show(sink->window);
        gst_vaapi_window_get_size(sink->window, &win_width, &win_height);
    }
    sink->window_width  = win_width;
    sink->window_height = win_height;
    GST_DEBUG("window size %ux%u", win_width, win_height);

    return gst_vaapisink_ensure_render_rect(sink, win_width, win_height);
}

#if USE_GLX
static void
render_background(GstVaapiSink *sink)
{
    /* Original code from Mirco Muller (MacSlow):
       <http://cgit.freedesktop.org/~macslow/gl-gst-player/> */
    GLfloat fStartX = 0.0f;
    GLfloat fStartY = 0.0f;
    GLfloat fWidth  = (GLfloat)sink->window_width;
    GLfloat fHeight = (GLfloat)sink->window_height;

    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    {
        /* top third, darker grey to white */
        glColor3f(0.85f, 0.85f, 0.85f);
        glVertex3f(fStartX, fStartY, 0.0f);
        glColor3f(0.85f, 0.85f, 0.85f);
        glVertex3f(fStartX + fWidth, fStartY, 0.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX + fWidth, fStartY + fHeight / 3.0f, 0.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX, fStartY + fHeight / 3.0f, 0.0f);

        /* middle third, just plain white */
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX, fStartY + fHeight / 3.0f, 0.0f);
        glVertex3f(fStartX + fWidth, fStartY + fHeight / 3.0f, 0.0f);
        glVertex3f(fStartX + fWidth, fStartY + 2.0f * fHeight / 3.0f, 0.0f);
        glVertex3f(fStartX, fStartY + 2.0f * fHeight / 3.0f, 0.0f);

        /* bottom third, white to lighter grey */
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX, fStartY + 2.0f * fHeight / 3.0f, 0.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX + fWidth, fStartY + 2.0f * fHeight / 3.0f, 0.0f);
        glColor3f(0.62f, 0.66f, 0.69f);
        glVertex3f(fStartX + fWidth, fStartY + fHeight, 0.0f);
        glColor3f(0.62f, 0.66f, 0.69f);
        glVertex3f(fStartX, fStartY + fHeight, 0.0f);
    }
    glEnd();
}

static void
render_frame(GstVaapiSink *sink)
{
    const guint x1 = sink->display_rect.x;
    const guint x2 = sink->display_rect.x + sink->display_rect.width;
    const guint y1 = sink->display_rect.y;
    const guint y2 = sink->display_rect.y + sink->display_rect.height;

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    {
        glTexCoord2f(0.0f, 0.0f); glVertex2i(x1, y1);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(x1, y2);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(x2, y2);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(x2, y1);
    }
    glEnd();
}

static void
render_reflection(GstVaapiSink *sink)
{
    const guint x1 = sink->display_rect.x;
    const guint x2 = sink->display_rect.x + sink->display_rect.width;
    const guint y1 = sink->display_rect.y;
    const guint rh = sink->display_rect.height / 5;
    GLfloat     ry = 1.0f - (GLfloat)rh / (GLfloat)sink->display_rect.height;

    glBegin(GL_QUADS);
    {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(x1, y1);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(x2, y1);

        glColor4f(1.0f, 1.0f, 1.0f, 0.0f);
        glTexCoord2f(1.0f, ry); glVertex2i(x2, y1 + rh);
        glTexCoord2f(0.0f, ry); glVertex2i(x1, y1 + rh);
    }
    glEnd();
}

static gboolean
gst_vaapisink_show_frame_glx(
    GstVaapiSink    *sink,
    GstVaapiSurface *surface,
    guint            flags
)
{
    GstVaapiWindowGLX * const window = GST_VAAPI_WINDOW_GLX(sink->window);
    GLenum target;
    GLuint texture;

    gst_vaapi_window_glx_make_current(window);
    if (!sink->texture) {
        sink->texture = gst_vaapi_texture_new(
            sink->display,
            GL_TEXTURE_2D,
            GL_BGRA,
            sink->video_width,
            sink->video_height
        );
        if (!sink->texture)
            goto error_create_texture;
    }
    if (!gst_vaapi_texture_put_surface(sink->texture, surface, flags))
        goto error_transfer_surface;

    target  = gst_vaapi_texture_get_target(sink->texture);
    texture = gst_vaapi_texture_get_id(sink->texture);
    if (target != GL_TEXTURE_2D || !texture)
        return FALSE;

    if (sink->use_reflection)
        render_background(sink);

    glEnable(target);
    glBindTexture(target, texture);
    {
        if (sink->use_reflection) {
            glPushMatrix();
            glRotatef(20.0f, 0.0f, 1.0f, 0.0f);
            glTranslatef(50.0f, 0.0f, 0.0f);
        }
        render_frame(sink);
        if (sink->use_reflection) {
            glPushMatrix();
            glTranslatef(0.0, (GLfloat)sink->display_rect.height + 5.0f, 0.0f);
            render_reflection(sink);
            glPopMatrix();
            glPopMatrix();
        }
    }
    glBindTexture(target, 0);
    glDisable(target);
    gst_vaapi_window_glx_swap_buffers(window);
    return TRUE;

    /* ERRORS */
error_create_texture:
    {
        GST_DEBUG("could not create VA/GLX texture");
        return FALSE;
    }
error_transfer_surface:
    {
        GST_DEBUG("could not transfer VA surface to texture");
        return FALSE;
    }
}
#endif

static inline gboolean
gst_vaapisink_put_surface(
    GstVaapiSink    *sink,
    GstVaapiSurface *surface,
    guint            flags
)
{
    if (!gst_vaapi_window_put_surface(sink->window, surface,
                NULL, &sink->display_rect, flags)) {
        GST_DEBUG("could not render VA surface");
        return FALSE;
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *src_buffer)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstVaapiVideoMeta *meta;
    GstVaapiSurface *surface;
    GstBuffer *buffer;
    guint flags;
    gboolean success;

    meta = gst_buffer_get_vaapi_video_meta(src_buffer);
    if (meta)
        buffer = gst_buffer_ref(src_buffer);
    else if (sink->use_video_raw) {
        buffer = gst_vaapi_uploader_get_buffer(sink->uploader);
        if (!buffer)
            return GST_FLOW_UNEXPECTED;
        meta = gst_buffer_get_vaapi_video_meta(buffer);
        if (!meta)
            goto error;
    }
    else
        return GST_FLOW_UNEXPECTED;

    if (sink->use_video_raw &&
        !gst_vaapi_uploader_process(sink->uploader, src_buffer, buffer)) {
        GST_WARNING("failed to process raw YUV buffer");
        goto error;
    }

    if (sink->display != gst_vaapi_video_meta_get_display(meta)) {
        g_clear_object(&sink->display);
        sink->display = g_object_ref(gst_vaapi_video_meta_get_display(meta));
    }

    if (!sink->window)
        goto error;

    gst_vaapisink_ensure_rotation(sink, TRUE);

    surface = gst_vaapi_video_meta_get_surface(meta);
    if (!surface)
        goto error;

    GST_DEBUG("render surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(gst_vaapi_surface_get_id(surface)));

    flags = gst_vaapi_video_meta_get_render_flags(meta);

    if (!gst_vaapi_apply_composition(surface, src_buffer))
        GST_WARNING("could not update subtitles");

    switch (sink->display_type) {
#if USE_GLX
    case GST_VAAPI_DISPLAY_TYPE_GLX:
        success = gst_vaapisink_show_frame_glx(sink, surface, flags);
        break;
#endif
#if USE_DRM
    case GST_VAAPI_DISPLAY_TYPE_DRM:
        success = TRUE;
        break;
#endif
#if USE_X11
    case GST_VAAPI_DISPLAY_TYPE_X11:
        success = gst_vaapisink_put_surface(sink, surface, flags);
        break;
#endif
#if USE_WAYLAND
    case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
        success = gst_vaapisink_put_surface(sink, surface, flags);
        break;
#endif
    default:
        GST_ERROR("unsupported display type %d", sink->display_type);
        success = FALSE;
        break;
    }
    if (!success)
        goto error;

    /* Retain VA surface until the next one is displayed */
    if (sink->use_overlay)
        gst_buffer_replace(&sink->video_buffer, buffer);
    gst_buffer_unref(buffer);
    return GST_FLOW_OK;

error:
    gst_buffer_unref(buffer);
    return GST_FLOW_UNEXPECTED;
}

static GstFlowReturn
gst_vaapisink_buffer_alloc(
    GstBaseSink        *base_sink,
    guint64             offset,
    guint               size,
    GstCaps            *caps,
    GstBuffer         **pbuf
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstStructure *structure;
    GstBuffer *buf;

    *pbuf = NULL;

    structure = gst_caps_get_structure(caps, 0);
    if (!gst_structure_has_name(structure, "video/x-raw-yuv"))
        return GST_FLOW_OK;

    if (!gst_vaapi_uploader_ensure_display(sink->uploader, sink->display))
        return GST_FLOW_NOT_SUPPORTED;
    if (!gst_vaapi_uploader_ensure_caps(sink->uploader, caps, NULL))
        return GST_FLOW_NOT_SUPPORTED;

    buf = gst_vaapi_uploader_get_buffer(sink->uploader);
    if (!buf) {
        GST_WARNING("failed to allocate resources for raw YUV buffer");
        return GST_FLOW_NOT_SUPPORTED;
    }

    *pbuf = buf;
    return GST_FLOW_OK;
}

static gboolean
gst_vaapisink_query(GstBaseSink *base_sink, GstQuery *query)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    if (gst_vaapi_reply_to_query(query, sink->display)) {
        GST_DEBUG("sharing display %p", sink->display);
        return TRUE;
    }
    return GST_BASE_SINK_CLASS(gst_vaapisink_parent_class)->query(base_sink,
        query);
}

static void
gst_vaapisink_finalize(GObject *object)
{
    gst_vaapisink_destroy(GST_VAAPISINK(object));

    G_OBJECT_CLASS(gst_vaapisink_parent_class)->finalize(object);
}

static void
gst_vaapisink_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(object);

    switch (prop_id) {
    case PROP_DISPLAY_TYPE:
        sink->display_type = g_value_get_enum(value);
        break;
    case PROP_FULLSCREEN:
        sink->fullscreen = g_value_get_boolean(value);
        break;
    case PROP_SYNCHRONOUS:
        sink->synchronous = g_value_get_boolean(value);
        break;
    case PROP_USE_REFLECTION:
        sink->use_reflection = g_value_get_boolean(value);
        break;
    case PROP_ROTATION:
        sink->rotation_req = g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapisink_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(object);

    switch (prop_id) {
    case PROP_DISPLAY_TYPE:
        g_value_set_enum(value, sink->display_type);
        break;
    case PROP_FULLSCREEN:
        g_value_set_boolean(value, sink->fullscreen);
        break;
    case PROP_SYNCHRONOUS:
        g_value_set_boolean(value, sink->synchronous);
        break;
    case PROP_USE_REFLECTION:
        g_value_set_boolean(value, sink->use_reflection);
        break;
    case PROP_ROTATION:
        g_value_set_enum(value, sink->rotation);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapisink_class_init(GstVaapiSinkClass *klass)
{
    GObjectClass * const     object_class   = G_OBJECT_CLASS(klass);
    GstElementClass * const  element_class  = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass * const basesink_class = GST_BASE_SINK_CLASS(klass);
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapisink,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize       = gst_vaapisink_finalize;
    object_class->set_property   = gst_vaapisink_set_property;
    object_class->get_property   = gst_vaapisink_get_property;

    basesink_class->start        = gst_vaapisink_start;
    basesink_class->stop         = gst_vaapisink_stop;
    basesink_class->get_caps     = gst_vaapisink_get_caps;
    basesink_class->set_caps     = gst_vaapisink_set_caps;
    basesink_class->preroll      = gst_vaapisink_show_frame;
    basesink_class->render       = gst_vaapisink_show_frame;
    basesink_class->query        = gst_vaapisink_query;
    basesink_class->buffer_alloc = gst_vaapisink_buffer_alloc;

    gst_element_class_set_static_metadata(element_class,
        "VA-API sink",
        "Sink/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    pad_template = gst_static_pad_template_get(&gst_vaapisink_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_TYPE,
         g_param_spec_enum("display",
                           "display type",
                           "display type to use",
                           GST_VAAPI_TYPE_DISPLAY_TYPE,
                           GST_VAAPI_DISPLAY_TYPE_ANY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#if USE_GLX
    g_object_class_install_property
        (object_class,
         PROP_USE_REFLECTION,
         g_param_spec_boolean("use-reflection",
                              "Reflection effect",
                              "Enables OpenGL reflection effect",
                              FALSE,
                              G_PARAM_READWRITE));
#endif

    g_object_class_install_property
        (object_class,
         PROP_FULLSCREEN,
         g_param_spec_boolean("fullscreen",
                              "Fullscreen",
                              "Requests window in fullscreen state",
                              FALSE,
                              G_PARAM_READWRITE));

    /**
     * GstVaapiSink:synchronous:
     *
     * When enabled, runs the X display in synchronous mode. Note that
     * this is used only for debugging.
     */
    g_object_class_install_property
        (object_class,
         PROP_SYNCHRONOUS,
         g_param_spec_boolean("synchronous",
                              "Synchronous mode",
                              "Toggles X display synchronous mode",
                              FALSE,
                              G_PARAM_READWRITE));

    /**
     * GstVaapiSink:rotation:
     *
     * The VA display rotation mode, expressed as a #GstVaapiRotation.
     */
    g_object_class_install_property
        (object_class,
         PROP_ROTATION,
         g_param_spec_enum(GST_VAAPI_DISPLAY_PROP_ROTATION,
                           "rotation",
                           "The display rotation mode",
                           GST_VAAPI_TYPE_ROTATION,
                           DEFAULT_ROTATION,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_vaapisink_init(GstVaapiSink *sink)
{
    sink->caps           = NULL;
    sink->display        = NULL;
    sink->window         = NULL;
    sink->window_width   = 0;
    sink->window_height  = 0;
    sink->texture        = NULL;
    sink->video_buffer   = NULL;
    sink->video_width    = 0;
    sink->video_height   = 0;
    sink->video_par_n    = 1;
    sink->video_par_d    = 1;
    sink->foreign_window = FALSE;
    sink->fullscreen     = FALSE;
    sink->synchronous    = FALSE;
    sink->display_type   = DEFAULT_DISPLAY_TYPE;
    sink->rotation       = DEFAULT_ROTATION;
    sink->rotation_req   = DEFAULT_ROTATION;
    sink->use_reflection = FALSE;
    sink->use_overlay    = FALSE;
    sink->use_rotation   = FALSE;
}
