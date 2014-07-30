/*
 *  gstvaapisink.c - VA-API video sink
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#include <gst/vaapi/gstvaapivalue.h>
#if USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
#endif
#if USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
# include <gst/vaapi/gstvaapiwindow_x11.h>
#endif
#if USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
# include <gst/vaapi/gstvaapiwindow_wayland.h>
#endif

/* Supported interfaces */
#if GST_CHECK_VERSION(1,0,0)
# include <gst/video/videooverlay.h>
#else
# include <gst/interfaces/xoverlay.h>

# define GST_TYPE_VIDEO_OVERLAY         GST_TYPE_X_OVERLAY
# define GST_VIDEO_OVERLAY              GST_X_OVERLAY
# define GstVideoOverlay                GstXOverlay
# define GstVideoOverlayInterface       GstXOverlayClass

# define gst_video_overlay_prepare_window_handle(sink) \
    gst_x_overlay_prepare_xwindow_id(sink)
# define gst_video_overlay_got_window_handle(sink, window_handle) \
    gst_x_overlay_got_window_handle(sink, window_handle)
#endif

#include "gstvaapisink.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"
#endif

#define GST_PLUGIN_NAME "vaapisink"
#define GST_PLUGIN_DESC "A VA-API based videosink"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapisink);
#define GST_CAT_DEFAULT gst_debug_vaapisink

/* Default template */
static const char gst_vaapisink_sink_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, "{ ENCODED, NV12, I420, YV12 }") ";"
    GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL);
#else
#if GST_CHECK_VERSION(1,0,0)
    GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL) "; "
#else
    "video/x-raw-yuv, "
    "width  = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; "
#endif
    GST_VAAPI_SURFACE_CAPS;
#endif

static GstStaticPadTemplate gst_vaapisink_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapisink_sink_caps_str));

static gboolean
gst_vaapisink_has_interface(GstVaapiPluginBase *plugin, GType type)
{
    return type == GST_TYPE_VIDEO_OVERLAY;
}

static void
gst_vaapisink_video_overlay_iface_init(GstVideoOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(
    GstVaapiSink,
    gst_vaapisink,
    GST_TYPE_VIDEO_SINK,
    GST_VAAPI_PLUGIN_BASE_INIT_INTERFACES
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_OVERLAY,
                          gst_vaapisink_video_overlay_iface_init))

enum {
    PROP_0,

    PROP_DISPLAY_TYPE,
    PROP_DISPLAY_NAME,
    PROP_FULLSCREEN,
    PROP_SYNCHRONOUS,
    PROP_ROTATION,
    PROP_FORCE_ASPECT_RATIO,
    PROP_VIEW_ID,
};

#define DEFAULT_DISPLAY_TYPE            GST_VAAPI_DISPLAY_TYPE_ANY
#define DEFAULT_ROTATION                GST_VAAPI_ROTATION_0

static inline gboolean
gst_vaapisink_ensure_display(GstVaapiSink *sink);

/* GstVideoOverlay interface */

static void
gst_vaapisink_video_overlay_expose(GstVideoOverlay *overlay);

#if USE_X11
static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id);
#endif

static void
gst_vaapisink_set_event_handling(GstVideoOverlay *overlay, gboolean handle_events);

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *buffer);

static gboolean
gst_vaapisink_ensure_render_rect(GstVaapiSink *sink, guint width, guint height);

static void
gst_vaapisink_video_overlay_set_window_handle(GstVideoOverlay *overlay,
    guintptr window)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);
    GstVaapiDisplayType display_type;

    if (!gst_vaapisink_ensure_display(sink))
        return;
    display_type = GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink);

    /* Disable GLX rendering when vaapisink is using a foreign X
       window. It's pretty much useless */
    if (display_type == GST_VAAPI_DISPLAY_TYPE_GLX) {
        display_type = GST_VAAPI_DISPLAY_TYPE_X11;
        gst_vaapi_plugin_base_set_display_type(GST_VAAPI_PLUGIN_BASE(sink),
            display_type);
    }

    sink->foreign_window = TRUE;

    switch (display_type) {
#if USE_X11
    case GST_VAAPI_DISPLAY_TYPE_X11:
        gst_vaapisink_ensure_window_xid(sink, window);
        gst_vaapisink_set_event_handling(GST_VIDEO_OVERLAY(sink), sink->handle_events);
        break;
#endif
    default:
        break;
    }
}

static void
gst_vaapisink_video_overlay_set_render_rectangle(
    GstVideoOverlay *overlay,
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

static gboolean
gst_vaapisink_reconfigure_window(GstVaapiSink * sink)
{
    guint win_width, win_height;

    gst_vaapi_window_reconfigure(sink->window);
    gst_vaapi_window_get_size(sink->window, &win_width, &win_height);
    if (win_width != sink->window_width || win_height != sink->window_height) {
        if (!gst_vaapisink_ensure_render_rect(sink, win_width, win_height))
            return FALSE;
        GST_INFO("window was resized from %ux%u to %ux%u",
            sink->window_width, sink->window_height, win_width, win_height);
        sink->window_width = win_width;
        sink->window_height = win_height;
        return TRUE;
    }
    return FALSE;
}

#if USE_X11
static void
gst_vaapisink_event_thread_loop_x11(GstVaapiSink *sink)
{
    GstVaapiDisplay * const display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);
    gboolean has_events, do_expose = FALSE;
    XEvent e;

    if (sink->window) {
        Display * const x11_dpy =
            gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(display));
        Window x11_win =
            gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window));

        /* Handle Expose + ConfigureNotify */
        /* Need to lock whole loop or we corrupt the XEvent queue: */
        for (;;) {
            gst_vaapi_display_lock(display);
            has_events = XCheckWindowEvent(x11_dpy, x11_win,
                StructureNotifyMask | ExposureMask, &e);
            gst_vaapi_display_unlock(display);
            if (!has_events)
                break;

            switch (e.type) {
            case Expose:
                do_expose = TRUE;
                break;
            case ConfigureNotify:
                if (gst_vaapisink_reconfigure_window(sink))
                    do_expose = TRUE;
                break;
            default:
                break;
            }
        }
        if (do_expose)
            gst_vaapisink_video_overlay_expose(GST_VIDEO_OVERLAY(sink));
        /* FIXME: handle mouse and key events */
    }
}
#endif

static void
gst_vaapisink_event_thread_loop_default(GstVaapiSink *sink)
{
}

static gpointer
gst_vaapisink_event_thread (GstVaapiSink *sink)
{
    void (*thread_loop)(GstVaapiSink *sink);

    switch (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink)) {
#if USE_X11
    case GST_VAAPI_DISPLAY_TYPE_X11:
    case GST_VAAPI_DISPLAY_TYPE_GLX:
        thread_loop = gst_vaapisink_event_thread_loop_x11;
        break;
#endif
    default:
        thread_loop = gst_vaapisink_event_thread_loop_default;
        break;
    }

    GST_OBJECT_LOCK(sink);
    while (!sink->event_thread_cancel) {
        GST_OBJECT_UNLOCK(sink);
        thread_loop(sink);
        g_usleep(G_USEC_PER_SEC / 20);
        GST_OBJECT_LOCK(sink);
    }
    GST_OBJECT_UNLOCK(sink);

  return NULL;
}

static void
gst_vaapisink_video_overlay_expose(GstVideoOverlay *overlay)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);

    if (sink->video_buffer) {
        gst_vaapisink_reconfigure_window(sink);
        gst_vaapisink_show_frame(GST_BASE_SINK_CAST(sink), sink->video_buffer);
    }
}

static void
gst_vaapisink_set_event_handling(GstVideoOverlay *overlay,
    gboolean handle_events)
{
    GThread *thread = NULL;
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);
#if USE_X11
    GstVaapiDisplayX11 * const display =
        GST_VAAPI_DISPLAY_X11(GST_VAAPI_PLUGIN_BASE_DISPLAY(sink));
#endif

    GST_OBJECT_LOCK(sink);
    sink->handle_events = handle_events;
    if (handle_events && !sink->event_thread) {
        /* Setup our event listening thread */
        GST_DEBUG("starting xevent thread");
        switch (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink)) {
#if USE_X11
        case GST_VAAPI_DISPLAY_TYPE_X11:
        case GST_VAAPI_DISPLAY_TYPE_GLX:
            XSelectInput(gst_vaapi_display_x11_get_display(display),
                gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window)),
                StructureNotifyMask | ExposureMask);
            break;
#endif
        default:
            break;
        }

        sink->event_thread_cancel = FALSE;
        sink->event_thread = g_thread_try_new("vaapisink-events",
                (GThreadFunc) gst_vaapisink_event_thread, sink, NULL);
    }
    else if (!handle_events && sink->event_thread) {
        GST_DEBUG("stopping xevent thread");
        if (sink->window) {
            switch (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink)) {
#if USE_X11
            case GST_VAAPI_DISPLAY_TYPE_X11:
            case GST_VAAPI_DISPLAY_TYPE_GLX:
                XSelectInput(gst_vaapi_display_x11_get_display(display),
                    gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window)),
                    0);
                break;
#endif
            default:
                break;
            }
        }

        /* grab thread and mark it as NULL */
        thread = sink->event_thread;
        sink->event_thread = NULL;
        sink->event_thread_cancel = TRUE;
    }
    GST_OBJECT_UNLOCK(sink);

    /* Wait for our event thread to finish */
    if (thread) {
        g_thread_join(thread);
        GST_DEBUG("xevent thread stopped");
    }
}

static void
gst_vaapisink_video_overlay_iface_init(GstVideoOverlayInterface *iface)
{
    iface->set_window_handle    = gst_vaapisink_video_overlay_set_window_handle;
    iface->set_render_rectangle = gst_vaapisink_video_overlay_set_render_rectangle;
    iface->expose               = gst_vaapisink_video_overlay_expose;
    iface->handle_events        = gst_vaapisink_set_event_handling;
}

static void
gst_vaapisink_destroy(GstVaapiSink *sink)
{
    gst_vaapisink_set_event_handling(GST_VIDEO_OVERLAY(sink), FALSE);

    gst_buffer_replace(&sink->video_buffer, NULL);
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
    GstVaapiDisplayX11 * const display =
        GST_VAAPI_DISPLAY_X11(GST_VAAPI_PLUGIN_BASE_DISPLAY(sink));
    ConfigureNotifyEventPendingArgs args;
    XEvent xev;

    args.window = window;
    args.width  = width;
    args.height = height;
    args.match  = FALSE;

    /* XXX: don't use XPeekIfEvent() because it might block */
    XCheckIfEvent(
        gst_vaapi_display_x11_get_display(display),
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
    return gst_vaapi_plugin_base_ensure_display(GST_VAAPI_PLUGIN_BASE(sink));
}

static void
gst_vaapisink_display_changed(GstVaapiPluginBase *plugin)
{
    GstVaapiSink * const sink = GST_VAAPISINK(plugin);
    GstVaapiRenderMode render_mode;

    GST_INFO("created %s %p", get_display_type_name(plugin->display_type),
             plugin->display);

    sink->use_overlay =
        gst_vaapi_display_get_render_mode(plugin->display, &render_mode) &&
        render_mode == GST_VAAPI_RENDER_MODE_OVERLAY;
    GST_DEBUG("use %s rendering mode",
              sink->use_overlay ? "overlay" : "texture");

    sink->use_rotation = gst_vaapi_display_has_property(plugin->display,
        GST_VAAPI_DISPLAY_PROP_ROTATION);
}

static gboolean
gst_vaapisink_ensure_uploader(GstVaapiSink *sink)
{
    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;
    if (!gst_vaapi_plugin_base_ensure_uploader(GST_VAAPI_PLUGIN_BASE(sink)))
        return FALSE;
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

    if (!sink->keep_aspect) {
        display_rect->width = width;
        display_rect->height = height;
        display_rect->x = 0;
        display_rect->y = 0;

        GST_DEBUG("force-aspect-ratio is false; distorting while scaling video");
        GST_DEBUG("render rect (%d,%d):%ux%u",
                  display_rect->x, display_rect->y,
                  display_rect->width, display_rect->height);
        return TRUE;
    }

    GST_DEBUG("ensure render rect within %ux%u bounds", width, height);

    gst_vaapi_display_get_pixel_aspect_ratio(
        GST_VAAPI_PLUGIN_BASE_DISPLAY(sink),
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
    GstVaapiDisplay * const display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);
    GstVideoRectangle src_rect, dst_rect, out_rect;
    guint num, den, display_width, display_height, display_par_n, display_par_d;
    gboolean success, scale;

    if (sink->foreign_window) {
        *pwidth  = sink->window_width;
        *pheight = sink->window_height;
        return;
    }

    gst_vaapi_display_get_size(display, &display_width, &display_height);
    if (sink->fullscreen) {
        *pwidth  = display_width;
        *pheight = display_height;
        return;
    }

    gst_vaapi_display_get_pixel_aspect_ratio(
        display,
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
    GstVaapiDisplay * const display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);

    if (!sink->window) {
        const GstVaapiDisplayType display_type =
            GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink);
        switch (display_type) {
#if USE_X11
        case GST_VAAPI_DISPLAY_TYPE_GLX:
        case GST_VAAPI_DISPLAY_TYPE_X11:
            sink->window = gst_vaapi_window_x11_new(display, width, height);
            if (!sink->window)
                break;
            gst_video_overlay_got_window_handle(
                GST_VIDEO_OVERLAY(sink),
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
            GST_ERROR("unsupported display type %d", display_type);
            return FALSE;
        }
    }
    return sink->window != NULL;
}

#if USE_X11
static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id)
{
    GstVaapiDisplay *display;
    Window rootwin;
    unsigned int width, height, border_width, depth;
    int x, y;
    XID xid = window_id;

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;
    display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);

    gst_vaapi_display_lock(display);
    XGetGeometry(
        gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(display)),
        xid,
        &rootwin,
        &x, &y, &width, &height, &border_width, &depth
    );
    gst_vaapi_display_unlock(display);

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

    gst_vaapi_window_replace(&sink->window, NULL);

    sink->window = gst_vaapi_window_x11_new_with_xid(display, xid);
    return sink->window != NULL;
}
#endif

static gboolean
gst_vaapisink_ensure_rotation(GstVaapiSink *sink, gboolean recalc_display_rect)
{
    GstVaapiDisplay * const display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);
    gboolean success = FALSE;

    g_return_val_if_fail(display, FALSE);

    if (sink->rotation == sink->rotation_req)
        return TRUE;

    if (!sink->use_rotation) {
        GST_WARNING("VA display does not support rotation");
        goto end;
    }

    gst_vaapi_display_lock(display);
    success = gst_vaapi_display_set_rotation(display, sink->rotation_req);
    gst_vaapi_display_unlock(display);
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

    if (!gst_vaapi_plugin_base_open(GST_VAAPI_PLUGIN_BASE(sink)))
        return FALSE;
    return gst_vaapisink_ensure_uploader(sink);
}

static gboolean
gst_vaapisink_stop(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    gst_buffer_replace(&sink->video_buffer, NULL);
#if GST_CHECK_VERSION(1,0,0)
    g_clear_object(&sink->video_buffer_pool);
#endif
    gst_vaapi_window_replace(&sink->window, NULL);

    gst_vaapi_plugin_base_close(GST_VAAPI_PLUGIN_BASE(sink));
    return TRUE;
}

static GstCaps *
gst_vaapisink_get_caps_impl(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstCaps *out_caps, *yuv_caps;

#if GST_CHECK_VERSION(1,1,0)
    out_caps = gst_static_pad_template_get_caps(&gst_vaapisink_sink_factory);
#else
    out_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS);
#endif
    if (!out_caps)
        return NULL;

    if (gst_vaapisink_ensure_uploader(sink)) {
        yuv_caps = GST_VAAPI_PLUGIN_BASE_UPLOADER_CAPS(sink);
        if (yuv_caps) {
            out_caps = gst_caps_make_writable(out_caps);
            gst_caps_append(out_caps, gst_caps_copy(yuv_caps));
	}
    }
    return out_caps;
}

#if GST_CHECK_VERSION(1,0,0)
static inline GstCaps *
gst_vaapisink_get_caps(GstBaseSink *base_sink, GstCaps *filter)
{
    GstCaps *caps, *out_caps;

    caps = gst_vaapisink_get_caps_impl(base_sink);
    if (caps && filter) {
        out_caps = gst_caps_intersect_full(caps, filter,
            GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(caps);
    }
    else
        out_caps = caps;
    return out_caps;
}
#else
#define gst_vaapisink_get_caps gst_vaapisink_get_caps_impl
#endif

static void
update_colorimetry(GstVaapiSink *sink, GstVideoColorimetry *cinfo)
{
#if GST_CHECK_VERSION(1,0,0)
    if (gst_video_colorimetry_matches(cinfo,
            GST_VIDEO_COLORIMETRY_BT601))
        sink->color_standard = GST_VAAPI_COLOR_STANDARD_ITUR_BT_601;
    else if (gst_video_colorimetry_matches(cinfo,
            GST_VIDEO_COLORIMETRY_BT709))
        sink->color_standard = GST_VAAPI_COLOR_STANDARD_ITUR_BT_709;
    else if (gst_video_colorimetry_matches(cinfo,
            GST_VIDEO_COLORIMETRY_SMPTE240M))
        sink->color_standard = GST_VAAPI_COLOR_STANDARD_SMPTE_240M;
    else
        sink->color_standard = 0;

    GST_DEBUG("colorimetry %s", gst_video_colorimetry_to_string(cinfo));
#endif
}

static gboolean
gst_vaapisink_set_caps(GstBaseSink *base_sink, GstCaps *caps)
{
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(base_sink);
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstVideoInfo * const vip = GST_VAAPI_PLUGIN_BASE_SINK_PAD_INFO(sink);
    GstVaapiDisplay *display;
    guint win_width, win_height;

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;
    display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);

#if USE_DRM
    if (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink) == GST_VAAPI_DISPLAY_TYPE_DRM)
        return TRUE;
#endif

    if (!gst_vaapi_plugin_base_set_caps(plugin, caps, NULL))
        return FALSE;

    sink->video_width   = GST_VIDEO_INFO_WIDTH(vip);
    sink->video_height  = GST_VIDEO_INFO_HEIGHT(vip);
    sink->video_par_n   = GST_VIDEO_INFO_PAR_N(vip);
    sink->video_par_d   = GST_VIDEO_INFO_PAR_D(vip);
    GST_DEBUG("video pixel-aspect-ratio %d/%d",
              sink->video_par_n, sink->video_par_d);

    update_colorimetry(sink, &vip->colorimetry);
    gst_caps_replace(&sink->caps, caps);

    gst_vaapisink_ensure_rotation(sink, FALSE);

    gst_vaapisink_ensure_window_size(sink, &win_width, &win_height);
    if (sink->window) {
        if (!sink->foreign_window || sink->fullscreen)
            gst_vaapi_window_set_size(sink->window, win_width, win_height);
    }
    else {
        gst_vaapi_display_lock(display);
        gst_video_overlay_prepare_window_handle(GST_VIDEO_OVERLAY(sink));
        gst_vaapi_display_unlock(display);
        if (sink->window)
            return TRUE;
        if (!gst_vaapisink_ensure_window(sink, win_width, win_height))
            return FALSE;
        gst_vaapi_window_set_fullscreen(sink->window, sink->fullscreen);
        gst_vaapi_window_show(sink->window);
        gst_vaapi_window_get_size(sink->window, &win_width, &win_height);
        gst_vaapisink_set_event_handling(GST_VIDEO_OVERLAY(sink), sink->handle_events);
    }
    sink->window_width  = win_width;
    sink->window_height = win_height;
    GST_DEBUG("window size %ux%u", win_width, win_height);

    return gst_vaapisink_ensure_render_rect(sink, win_width, win_height);
}

static inline gboolean
gst_vaapisink_put_surface(
    GstVaapiSink               *sink,
    GstVaapiSurface            *surface,
    const GstVaapiRectangle    *surface_rect,
    guint                       flags
)
{
    if (!sink->window)
        return FALSE;

    if (!gst_vaapi_window_put_surface(sink->window, surface,
                surface_rect, &sink->display_rect, flags)) {
        GST_DEBUG("could not render VA surface");
        return FALSE;
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapisink_show_frame_unlocked(GstVaapiSink *sink, GstBuffer *src_buffer)
{
    GstVaapiVideoMeta *meta;
    GstVaapiSurfaceProxy *proxy;
    GstVaapiSurface *surface;
    GstBuffer *buffer;
    guint flags;
    gboolean success;
    GstVaapiRectangle *surface_rect = NULL;
#if GST_CHECK_VERSION(1,0,0)
    GstVaapiRectangle tmp_rect;
#endif
    GstFlowReturn ret;
    gint32 view_id;

#if GST_CHECK_VERSION(1,0,0)
    GstVideoCropMeta * const crop_meta =
        gst_buffer_get_video_crop_meta(src_buffer);
    if (crop_meta) {
        surface_rect = &tmp_rect;
        surface_rect->x = crop_meta->x;
        surface_rect->y = crop_meta->y;
        surface_rect->width = crop_meta->width;
        surface_rect->height = crop_meta->height;
    }
#endif

    ret = gst_vaapi_plugin_base_get_input_buffer(GST_VAAPI_PLUGIN_BASE(sink),
        src_buffer, &buffer);
    if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_SUPPORTED)
        return ret;

    meta = gst_buffer_get_vaapi_video_meta(buffer);
    GST_VAAPI_PLUGIN_BASE_DISPLAY_REPLACE(sink,
        gst_vaapi_video_meta_get_display(meta));

    gst_vaapisink_ensure_rotation(sink, TRUE);

    proxy = gst_vaapi_video_meta_get_surface_proxy(meta);
    if (!proxy)
        goto error;

    /* Valide view component to display */
    view_id = GST_VAAPI_SURFACE_PROXY_VIEW_ID(proxy);
    if (G_UNLIKELY(sink->view_id == -1))
        sink->view_id = view_id;
    else if (sink->view_id != view_id) {
        gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }

    surface = gst_vaapi_video_meta_get_surface(meta);
    if (!surface)
        goto error;

    GST_DEBUG("render surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(gst_vaapi_surface_get_id(surface)));

    if (!surface_rect)
        surface_rect = (GstVaapiRectangle *)
            gst_vaapi_video_meta_get_render_rect(meta);

    if (surface_rect)
        GST_DEBUG("render rect (%d,%d), size %ux%u",
                  surface_rect->x, surface_rect->y,
                  surface_rect->width, surface_rect->height);

    flags = gst_vaapi_video_meta_get_render_flags(meta);

    /* Append default color standard obtained from caps if none was
       available on a per-buffer basis */
    if (!(flags & GST_VAAPI_COLOR_STANDARD_MASK))
        flags |= sink->color_standard;

    if (!gst_vaapi_apply_composition(surface, src_buffer))
        GST_WARNING("could not update subtitles");

    switch (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink)) {
#if USE_DRM
    case GST_VAAPI_DISPLAY_TYPE_DRM:
        success = TRUE;
        break;
#endif
#if USE_X11
    case GST_VAAPI_DISPLAY_TYPE_GLX:
    case GST_VAAPI_DISPLAY_TYPE_X11:
        success = gst_vaapisink_put_surface(sink, surface, surface_rect, flags);
        break;
#endif
#if USE_WAYLAND
    case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
        success = gst_vaapisink_put_surface(sink, surface, surface_rect, flags);
        break;
#endif
    default:
        GST_ERROR("unsupported display type %d",
                  GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink));
        success = FALSE;
        break;
    }
    if (!success)
        goto error;

    /* Retain VA surface until the next one is displayed */
    gst_buffer_replace(&sink->video_buffer, buffer);
    gst_buffer_unref(buffer);
    return GST_FLOW_OK;

error:
    gst_buffer_unref(buffer);
    return GST_FLOW_EOS;
}

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *src_buffer)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstFlowReturn ret;

   /* At least we need at least to protect the _set_subpictures_()
    * call to prevent a race during subpicture desctruction.
    * FIXME: Could use a less coarse grained lock, though: */
    gst_vaapi_display_lock(GST_VAAPI_PLUGIN_BASE_DISPLAY(sink));
    ret = gst_vaapisink_show_frame_unlocked(sink, src_buffer);
    gst_vaapi_display_unlock(GST_VAAPI_PLUGIN_BASE_DISPLAY(sink));
    return ret;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapisink_propose_allocation(GstBaseSink *base_sink, GstQuery *query)
{
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(base_sink);

    if (!gst_vaapi_plugin_base_propose_allocation(plugin, query))
        return FALSE;

    gst_query_add_allocation_meta(query, GST_VIDEO_CROP_META_API_TYPE, NULL);
    gst_query_add_allocation_meta(query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);
    return TRUE;
}
#else
static GstFlowReturn
gst_vaapisink_buffer_alloc(GstBaseSink *base_sink, guint64 offset, guint size,
    GstCaps *caps, GstBuffer **outbuf_ptr)
{
    return gst_vaapi_plugin_base_allocate_input_buffer(
        GST_VAAPI_PLUGIN_BASE(base_sink), caps, outbuf_ptr);
}
#endif

static gboolean
gst_vaapisink_query(GstBaseSink *base_sink, GstQuery *query)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    GST_INFO_OBJECT(sink, "query type %s", GST_QUERY_TYPE_NAME(query));

    if (gst_vaapi_reply_to_query(query, GST_VAAPI_PLUGIN_BASE_DISPLAY(sink))) {
        GST_DEBUG("sharing display %p", GST_VAAPI_PLUGIN_BASE_DISPLAY(sink));
        return TRUE;
    }

    return GST_BASE_SINK_CLASS(gst_vaapisink_parent_class)->query(base_sink,
        query);
}

static void
gst_vaapisink_finalize(GObject *object)
{
    gst_vaapisink_destroy(GST_VAAPISINK(object));

    gst_vaapi_plugin_base_finalize(GST_VAAPI_PLUGIN_BASE(object));
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
        gst_vaapi_plugin_base_set_display_type(GST_VAAPI_PLUGIN_BASE(sink),
            g_value_get_enum(value));
        break;
    case PROP_DISPLAY_NAME:
        gst_vaapi_plugin_base_set_display_name(GST_VAAPI_PLUGIN_BASE(sink),
            g_value_get_string(value));
        break;
    case PROP_FULLSCREEN:
        sink->fullscreen = g_value_get_boolean(value);
        break;
    case PROP_SYNCHRONOUS:
        sink->synchronous = g_value_get_boolean(value);
        break;
    case PROP_VIEW_ID:
        sink->view_id = g_value_get_int(value);
        break;
    case PROP_ROTATION:
        sink->rotation_req = g_value_get_enum(value);
        break;
    case PROP_FORCE_ASPECT_RATIO:
        sink->keep_aspect = g_value_get_boolean(value);
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
        g_value_set_enum(value, GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink));
        break;
    case PROP_DISPLAY_NAME:
        g_value_set_string(value, GST_VAAPI_PLUGIN_BASE_DISPLAY_NAME(sink));
        break;
    case PROP_FULLSCREEN:
        g_value_set_boolean(value, sink->fullscreen);
        break;
    case PROP_SYNCHRONOUS:
        g_value_set_boolean(value, sink->synchronous);
        break;
    case PROP_VIEW_ID:
        g_value_set_int(value, sink->view_id);
        break;
    case PROP_ROTATION:
        g_value_set_enum(value, sink->rotation);
        break;
    case PROP_FORCE_ASPECT_RATIO:
        g_value_set_boolean(value, sink->keep_aspect);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapisink_set_bus(GstElement *element, GstBus *bus)
{
    /* Make sure to allocate a VA display in the sink element first,
       so that upstream elements could query a display that was
       allocated here, and that exactly matches what the user
       requested through the "display" property */
    if (!GST_ELEMENT_BUS(element) && bus)
        gst_vaapisink_ensure_display(GST_VAAPISINK(element));

    GST_ELEMENT_CLASS(gst_vaapisink_parent_class)->set_bus(element, bus);
}

static void
gst_vaapisink_class_init(GstVaapiSinkClass *klass)
{
    GObjectClass * const     object_class   = G_OBJECT_CLASS(klass);
    GstElementClass * const  element_class  = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass * const basesink_class = GST_BASE_SINK_CLASS(klass);
    GstVaapiPluginBaseClass * const base_plugin_class =
        GST_VAAPI_PLUGIN_BASE_CLASS(klass);
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapisink,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    gst_vaapi_plugin_base_class_init(base_plugin_class);
    base_plugin_class->has_interface    = gst_vaapisink_has_interface;
    base_plugin_class->display_changed  = gst_vaapisink_display_changed;

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
#if GST_CHECK_VERSION(1,0,0)
    basesink_class->propose_allocation = gst_vaapisink_propose_allocation;
#else
    basesink_class->buffer_alloc = gst_vaapisink_buffer_alloc;
#endif

    element_class->set_bus = gst_vaapisink_set_bus;
    gst_element_class_set_static_metadata(element_class,
        "VA-API sink",
        "Sink/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    pad_template = gst_static_pad_template_get(&gst_vaapisink_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_TYPE,
         g_param_spec_enum("display",
                           "display type",
                           "display type to use",
                           GST_VAAPI_TYPE_DISPLAY_TYPE,
                           GST_VAAPI_DISPLAY_TYPE_ANY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_NAME,
         g_param_spec_string("display-name",
                             "display name",
                             "display name to use",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class,
         PROP_FULLSCREEN,
         g_param_spec_boolean("fullscreen",
                              "Fullscreen",
                              "Requests window in fullscreen state",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

    /**
     * GstVaapiSink:force-aspect-ratio:
     *
     * When enabled, scaling respects video aspect ratio; when disabled, the
     * video is distorted to fit the window.
     */
    g_object_class_install_property
        (object_class,
         PROP_FORCE_ASPECT_RATIO,
         g_param_spec_boolean("force-aspect-ratio",
                              "Force aspect ratio",
                              "When enabled, scaling will respect original aspect ratio",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * GstVaapiSink:view-id:
     *
     * When not set to -1, the displayed frame will always be the one
     * that matches the view-id of the very first displayed frame. Any
     * other number will indicate the desire to display the supplied
     * view-id only.
     */
    g_object_class_install_property
        (object_class,
         PROP_VIEW_ID,
         g_param_spec_int("view-id",
                          "View ID",
                          "ID of the view component of interest to display",
                          -1, G_MAXINT32, -1,
                          G_PARAM_READWRITE));
}

static void
gst_vaapisink_init(GstVaapiSink *sink)
{
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(sink);

    gst_vaapi_plugin_base_init(plugin, GST_CAT_DEFAULT);
    gst_vaapi_plugin_base_set_display_type(plugin, DEFAULT_DISPLAY_TYPE);

    sink->caps           = NULL;
    sink->window         = NULL;
    sink->window_width   = 0;
    sink->window_height  = 0;
    sink->video_buffer   = NULL;
    sink->video_width    = 0;
    sink->video_height   = 0;
    sink->video_par_n    = 1;
    sink->video_par_d    = 1;
    sink->view_id        = -1;
    sink->handle_events  = TRUE;
    sink->foreign_window = FALSE;
    sink->fullscreen     = FALSE;
    sink->synchronous    = FALSE;
    sink->rotation       = DEFAULT_ROTATION;
    sink->rotation_req   = DEFAULT_ROTATION;
    sink->use_overlay    = FALSE;
    sink->use_rotation   = FALSE;
    sink->keep_aspect    = TRUE;
    gst_video_info_init(&sink->video_info);
}
