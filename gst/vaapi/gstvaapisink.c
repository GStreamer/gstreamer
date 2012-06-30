/*
 *  gstvaapisink.c - VA-API video sink
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#include "config.h"
#include <gst/gst.h>
#include <gst/gstutils_version.h>
#include <gst/video/video.h>
#include <gst/video/videocontext.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#include <gst/vaapi/gstvaapivideosink.h>
#include <gst/vaapi/gstvaapidisplay_x11.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>
#if USE_VAAPISINK_GLX
#include <gst/vaapi/gstvaapidisplay_glx.h>
#include <gst/vaapi/gstvaapiwindow_glx.h>
#endif

/* Supported interfaces */
#include <gst/interfaces/xoverlay.h>

#include "gstvaapisink.h"
#include "gstvaapipluginutil.h"

#define HAVE_GST_XOVERLAY_SET_WINDOW_HANDLE \
    GST_PLUGINS_BASE_CHECK_VERSION(0,10,31)

#define HAVE_GST_XOVERLAY_SET_RENDER_RECTANGLE \
    GST_PLUGINS_BASE_CHECK_VERSION(0,10,29)

#define GST_PLUGIN_NAME "vaapisink"
#define GST_PLUGIN_DESC "A VA-API based videosink"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapisink);
#define GST_CAT_DEFAULT gst_debug_vaapisink

/* ElementFactory information */
static const GstElementDetails gst_vaapisink_details =
    GST_ELEMENT_DETAILS(
        "VA-API sink",
        "Sink/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

/* Default template */
static GstStaticPadTemplate gst_vaapisink_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(GST_VAAPI_SURFACE_CAPS));

static void gst_vaapisink_iface_init(GType type);

GST_BOILERPLATE_FULL(
    GstVaapiSink,
    gst_vaapisink,
    GstVideoSink,
    GST_TYPE_VIDEO_SINK,
    gst_vaapisink_iface_init);

enum {
    PROP_0,

    PROP_USE_GLX,
    PROP_FULLSCREEN,
    PROP_SYNCHRONOUS,
    PROP_USE_REFLECTION
};

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

/* GstVaapiVideoSink interface */

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

/* GstXOverlay interface */

static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id);

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *buffer);

static inline void
_gst_vaapisink_xoverlay_set_xid(GstXOverlay *overlay, guintptr window_id)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);

    /* Disable GLX rendering when vaapisink is using a foreign X
       window. It's pretty much useless */
    sink->use_glx = FALSE;

    sink->foreign_window = TRUE;
    gst_vaapisink_ensure_window_xid(sink, window_id);
}

#if HAVE_GST_XOVERLAY_SET_WINDOW_HANDLE
static void
gst_vaapisink_xoverlay_set_window_handle(GstXOverlay *overlay, guintptr window_id)
{
    _gst_vaapisink_xoverlay_set_xid(overlay, window_id);
}
#else
static void
gst_vaapisink_xoverlay_set_xid(GstXOverlay *overlay, XID xid)
{
    _gst_vaapisink_xoverlay_set_xid(overlay, xid);
}
#endif

#if HAVE_GST_XOVERLAY_SET_RENDER_RECTANGLE
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
#endif

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
#if HAVE_GST_XOVERLAY_SET_WINDOW_HANDLE
    iface->set_window_handle    = gst_vaapisink_xoverlay_set_window_handle;
#else
    iface->set_xwindow_id       = gst_vaapisink_xoverlay_set_xid;
#endif
#if HAVE_GST_XOVERLAY_SET_RENDER_RECTANGLE
    iface->set_render_rectangle = gst_vaapisink_xoverlay_set_render_rectangle;
#endif
    iface->expose               = gst_vaapisink_xoverlay_expose;
}

static void
gst_vaapisink_iface_init(GType type)
{
    const GType g_define_type_id = type;

    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapisink_implements_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_vaapisink_video_context_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_X_OVERLAY,
                          gst_vaapisink_xoverlay_iface_init);
}

static void
gst_vaapisink_destroy(GstVaapiSink *sink)
{
    if (sink->texture) {
        g_object_unref(sink->texture);
        sink->texture = NULL;
    }

    if (sink->display) {
        g_object_unref(sink->display);
        sink->display = NULL;
    }

    gst_caps_replace(&sink->caps, NULL);
}

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

static inline gboolean
gst_vaapisink_ensure_window(GstVaapiSink *sink, guint width, guint height)
{
    GstVaapiDisplay * const display = sink->display;

    if (!sink->window) {
#if USE_VAAPISINK_GLX
        if (sink->use_glx)
            sink->window = gst_vaapi_window_glx_new(display, width, height);
        else
#endif
            sink->window = gst_vaapi_window_x11_new(display, width, height);
        if (sink->window)
            gst_x_overlay_got_xwindow_id(
                GST_X_OVERLAY(sink),
                gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window))
            );
    }
    return sink->window != NULL;
}

static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id)
{
    Window rootwin;
    unsigned int width, height, border_width, depth;
    int x, y;
    XID xid = window_id;

    if (!gst_vaapi_ensure_display(sink, &sink->display))
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

    if (sink->window) {
        g_object_unref(sink->window);
        sink->window = NULL;
    }

#if USE_VAAPISINK_GLX
    if (sink->use_glx)
        sink->window = gst_vaapi_window_glx_new_with_xid(sink->display, xid);
    else
#endif
        sink->window = gst_vaapi_window_x11_new_with_xid(sink->display, xid);
    return sink->window != NULL;
}

static gboolean
gst_vaapisink_start(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    return gst_vaapi_ensure_display(sink, &sink->display);
}

static gboolean
gst_vaapisink_stop(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    if (sink->window) {
        g_object_unref(sink->window);
        sink->window = NULL;
    }

    if (sink->display) {
        g_object_unref(sink->display);
        sink->display = NULL;
    }
    return TRUE;
}

static gboolean
gst_vaapisink_set_caps(GstBaseSink *base_sink, GstCaps *caps)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    guint win_width, win_height, display_width, display_height;
    gint video_width, video_height, video_par_n = 1, video_par_d = 1;

    if (!structure)
        return FALSE;
    if (!gst_structure_get_int(structure, "width",  &video_width))
        return FALSE;
    if (!gst_structure_get_int(structure, "height", &video_height))
        return FALSE;
    sink->video_width  = video_width;
    sink->video_height = video_height;

    gst_video_parse_caps_pixel_aspect_ratio(caps, &video_par_n, &video_par_d);
    sink->video_par_n  = video_par_n;
    sink->video_par_d  = video_par_d;
    GST_DEBUG("video pixel-aspect-ratio %d/%d", video_par_n, video_par_d);

    gst_caps_replace(&sink->caps, caps);

    if (!gst_vaapi_ensure_display(sink, &sink->display))
        return FALSE;

    gst_vaapi_display_get_size(sink->display, &display_width, &display_height);
    if (sink->foreign_window) {
        win_width  = sink->window_width;
        win_height = sink->window_height;
    }
    else if (sink->fullscreen ||
             video_width > display_width || video_height > display_height) {
        win_width  = display_width;
        win_height = display_height;
    }
    else {
        win_width  = video_width;
        win_height = video_height;
    }

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

#if USE_VAAPISINK_GLX
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
gst_vaapisink_show_frame_x11(
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
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *buffer)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstVaapiVideoBuffer * const vbuffer = GST_VAAPI_VIDEO_BUFFER(buffer);
    GstVaapiSurface *surface;
    guint flags;
    gboolean success;
    GstVideoOverlayComposition * const composition =
        gst_video_buffer_get_overlay_composition(buffer);

    if (sink->display != gst_vaapi_video_buffer_get_display (vbuffer)) {
      if (sink->display)
        g_object_unref (sink->display);
      sink->display = g_object_ref (gst_vaapi_video_buffer_get_display (vbuffer));
    }

    if (!sink->window)
        return GST_FLOW_UNEXPECTED;

    surface = gst_vaapi_video_buffer_get_surface(vbuffer);
    if (!surface)
        return GST_FLOW_UNEXPECTED;

    GST_DEBUG("render surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(gst_vaapi_surface_get_id(surface)));

    flags = gst_vaapi_video_buffer_get_render_flags(vbuffer);

    if (!gst_vaapi_surface_set_subpictures_from_composition(surface,
             composition, TRUE))
        GST_WARNING("could not update subtitles");

#if USE_VAAPISINK_GLX
    if (sink->use_glx)
        success = gst_vaapisink_show_frame_glx(sink, surface, flags);
    else
#endif
        success = gst_vaapisink_show_frame_x11(sink, surface, flags);
    return success ? GST_FLOW_OK : GST_FLOW_UNEXPECTED;
}

static gboolean
gst_vaapisink_query(GstBaseSink *base_sink, GstQuery *query)
{
    GstVaapiSink *sink = GST_VAAPISINK(base_sink);
    GST_DEBUG ("sharing display %p", sink->display);
    return gst_vaapi_reply_to_query (query, sink->display);
}

static void
gst_vaapisink_finalize(GObject *object)
{
    gst_vaapisink_destroy(GST_VAAPISINK(object));

    G_OBJECT_CLASS(parent_class)->finalize(object);
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
    case PROP_USE_GLX:
        sink->use_glx = g_value_get_boolean(value);
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
    case PROP_USE_GLX:
        g_value_set_boolean(value, sink->use_glx);
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
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapisink_base_init(gpointer klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstPadTemplate *pad_template;

    gst_element_class_set_details_simple(
        element_class,
        gst_vaapisink_details.longname,
        gst_vaapisink_details.klass,
        gst_vaapisink_details.description,
        gst_vaapisink_details.author
    );

    pad_template = gst_static_pad_template_get(&gst_vaapisink_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);
}

static void
gst_vaapisink_class_init(GstVaapiSinkClass *klass)
{
    GObjectClass * const     object_class   = G_OBJECT_CLASS(klass);
    GstBaseSinkClass * const basesink_class = GST_BASE_SINK_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapisink,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize       = gst_vaapisink_finalize;
    object_class->set_property   = gst_vaapisink_set_property;
    object_class->get_property   = gst_vaapisink_get_property;

    basesink_class->start        = gst_vaapisink_start;
    basesink_class->stop         = gst_vaapisink_stop;
    basesink_class->set_caps     = gst_vaapisink_set_caps;
    basesink_class->preroll      = gst_vaapisink_show_frame;
    basesink_class->render       = gst_vaapisink_show_frame;
    basesink_class->query        = gst_vaapisink_query;

#if USE_VAAPISINK_GLX
    g_object_class_install_property
        (object_class,
         PROP_USE_GLX,
         g_param_spec_boolean("use-glx",
                              "GLX rendering",
                              "Enables GLX rendering",
                              TRUE,
                              G_PARAM_READWRITE));

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
}

static void
gst_vaapisink_init(GstVaapiSink *sink, GstVaapiSinkClass *klass)
{
    sink->caps           = NULL;
    sink->display        = NULL;
    sink->window         = NULL;
    sink->window_width   = 0;
    sink->window_height  = 0;
    sink->texture        = NULL;
    sink->video_width    = 0;
    sink->video_height   = 0;
    sink->video_par_n    = 1;
    sink->video_par_d    = 1;
    sink->foreign_window = FALSE;
    sink->fullscreen     = FALSE;
    sink->synchronous    = FALSE;
    sink->use_glx        = USE_VAAPISINK_GLX;
    sink->use_reflection = FALSE;
}
