/* Copyright (C) <2018> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018> Žan Doberšek <zdobersek@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "WPEThreadedView.h"

#include <cstdio>
#include <mutex>

#define GST_CAT_DEFAULT wpe_src_debug

// -70 is the GLib priority we use internally in WebKit, for WPE.
#define WPE_GLIB_SOURCE_PRIORITY -70

class GMutexHolder {
public:
    GMutexHolder(GMutex& mutex)
        : m(mutex)
    {
        g_mutex_lock(&m);
    }
    ~GMutexHolder()
    {
        g_mutex_unlock(&m);
    }

private:
    GMutex& m;
};

WPEThreadedView::WPEThreadedView()
{
    g_mutex_init(&threading.mutex);
    g_cond_init(&threading.cond);
    g_mutex_init(&threading.ready_mutex);
    g_cond_init(&threading.ready_cond);

    g_mutex_init(&images.mutex);

    {
        GMutexHolder lock(threading.mutex);
        threading.thread = g_thread_new("WPEThreadedView",
            s_viewThread, this);
        g_cond_wait(&threading.cond, &threading.mutex);
        GST_DEBUG("thread spawned");
    }
}

WPEThreadedView::~WPEThreadedView()
{
    {
        GMutexHolder lock(images.mutex);

        if (images.pending) {
            gst_egl_image_unref(images.pending);
            images.pending = nullptr;
        }
        if (images.committed) {
            gst_egl_image_unref(images.committed);
            images.committed = nullptr;
        }
    }

    {
        GMutexHolder lock(threading.mutex);
        wpe_view_backend_exportable_fdo_destroy(wpe.exportable);
    }

    if (gst.display) {
        gst_object_unref(gst.display);
        gst.display = nullptr;
    }

    if (gst.context) {
        gst_object_unref(gst.context);
        gst.context = nullptr;
    }

    if (threading.thread) {
        g_thread_unref(threading.thread);
        threading.thread = nullptr;
    }

    g_mutex_clear(&threading.mutex);
    g_cond_clear(&threading.cond);
    g_mutex_clear(&threading.ready_mutex);
    g_cond_clear(&threading.ready_cond);
    g_mutex_clear(&images.mutex);
}

gpointer WPEThreadedView::s_viewThread(gpointer data)
{
    auto& view = *static_cast<WPEThreadedView*>(data);

    view.glib.context = g_main_context_new();
    view.glib.loop = g_main_loop_new(view.glib.context, FALSE);

    g_main_context_push_thread_default(view.glib.context);

    {
        GSource* source = g_idle_source_new();
        g_source_set_callback(source,
            [](gpointer data) -> gboolean {
                auto& view = *static_cast<WPEThreadedView*>(data);
                GMutexHolder lock(view.threading.mutex);
                g_cond_signal(&view.threading.cond);
                return G_SOURCE_REMOVE;
            },
            &view, nullptr);
        g_source_attach(source, view.glib.context);
        g_source_unref(source);
    }

    g_main_loop_run(view.glib.loop);

    g_main_loop_unref(view.glib.loop);
    view.glib.loop = nullptr;

    if (view.webkit.view) {
        g_object_unref(view.webkit.view);
        view.webkit.view = nullptr;
    }
    if (view.webkit.uri) {
        g_free(view.webkit.uri);
        view.webkit.uri = nullptr;
    }

    g_main_context_pop_thread_default(view.glib.context);
    g_main_context_unref(view.glib.context);
    view.glib.context = nullptr;
    return nullptr;
}

struct wpe_view_backend* WPEThreadedView::backend() const
{
    return wpe.exportable ? wpe_view_backend_exportable_fdo_get_view_backend(wpe.exportable) : nullptr;
}

void WPEThreadedView::s_loadEvent(WebKitWebView*, WebKitLoadEvent event, gpointer data)
{
    if (event == WEBKIT_LOAD_COMMITTED) {
        auto& view = *static_cast<WPEThreadedView*>(data);
        GMutexHolder lock(view.threading.ready_mutex);
        g_cond_signal(&view.threading.ready_cond);
    }
}

void WPEThreadedView::initialize(GstWpeSrc* src, GstGLContext* context, GstGLDisplay* display, int width, int height)
{
    GST_DEBUG("context %p display %p, size (%d,%d)", context, display, width, height);

    static std::once_flag s_loaderFlag;
    std::call_once(s_loaderFlag,
        [] {
#if defined(WPE_BACKEND_CHECK_VERSION) && WPE_BACKEND_CHECK_VERSION(0, 2, 0)
            wpe_loader_init("libWPEBackend-fdo-0.1.so");
#endif
        });

    struct InitializeContext {
        GstWpeSrc* src;
        WPEThreadedView& view;
        GstGLContext* context;
        GstGLDisplay* display;
        int width;
        int height;
    } initializeContext{ src, *this, context, display, width, height };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            GST_DEBUG("on view thread");
            auto& initializeContext = *static_cast<InitializeContext*>(data);
            auto& view = initializeContext.view;

            GMutexHolder lock(view.threading.mutex);

            view.gst.context = GST_GL_CONTEXT(gst_object_ref(initializeContext.context));
            view.gst.display = GST_GL_DISPLAY(gst_object_ref(initializeContext.display));

            view.wpe.width = initializeContext.width;
            view.wpe.height = initializeContext.height;

            EGLDisplay eglDisplay = gst_gl_display_egl_get_from_native(
                GST_GL_DISPLAY_TYPE_WAYLAND,
                gst_gl_display_get_handle(initializeContext.display));
            GST_DEBUG("eglDisplay %p", eglDisplay);
            wpe_fdo_initialize_for_egl_display(eglDisplay);

            view.wpe.exportable = wpe_view_backend_exportable_fdo_egl_create(&s_exportableClient,
                &view, view.wpe.width, view.wpe.height);
            auto* viewBackend = webkit_web_view_backend_new(
                wpe_view_backend_exportable_fdo_get_view_backend(view.wpe.exportable), nullptr, nullptr);

            view.webkit.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                "backend", viewBackend, nullptr));

            gst_wpe_src_configure_web_view(initializeContext.src, view.webkit.view);

            g_signal_connect(view.webkit.view, "load-changed", G_CALLBACK(s_loadEvent), &view);

            const gchar* location;
            gboolean drawBackground = TRUE;
            g_object_get(initializeContext.src, "location", &location, "draw-background", &drawBackground, nullptr);
            if (!location)
                g_warning("Invalid location");
            else {
                view.setDrawBackground(drawBackground);
                view.loadUriUnlocked(location);
            }
            g_cond_signal(&view.threading.cond);
            return G_SOURCE_REMOVE;
        },
        &initializeContext, nullptr);
    g_source_set_priority(source, WPE_GLIB_SOURCE_PRIORITY);

    {
        GMutexHolder lock(threading.mutex);
        g_source_attach(source, glib.context);
        g_cond_wait(&threading.cond, &threading.mutex);
    }

    g_source_unref(source);

    {
        GST_DEBUG("waiting load to finish");
        GMutexHolder lock(threading.ready_mutex);
        g_cond_wait(&threading.ready_cond, &threading.ready_mutex);
        GST_DEBUG("done");
    }
}

GstEGLImage* WPEThreadedView::image()
{
    GstEGLImage* ret = nullptr;
    GMutexHolder lock(images.mutex);

    GST_TRACE("pending %" GST_PTR_FORMAT " committed %" GST_PTR_FORMAT, images.pending, images.committed);

    if (images.pending) {
        auto* previousImage = images.committed;
        images.committed = images.pending;
        images.pending = nullptr;

        frameComplete();

        if (previousImage)
            gst_egl_image_unref(previousImage);
    }

    if (images.committed)
        ret = images.committed;

    return ret;
}

void WPEThreadedView::resize(int width, int height)
{
    GST_DEBUG("resize");

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            auto& view = *static_cast<WPEThreadedView*>(data);
            GMutexHolder lock(view.threading.mutex);

            GST_DEBUG("dispatching");
            if (view.wpe.exportable && wpe_view_backend_exportable_fdo_get_view_backend(view.wpe.exportable))
                wpe_view_backend_dispatch_set_size(wpe_view_backend_exportable_fdo_get_view_backend(view.wpe.exportable), view.wpe.width, view.wpe.height);

            g_cond_signal(&view.threading.cond);
            return G_SOURCE_REMOVE;
        },
        this, nullptr);
    g_source_set_priority(source, WPE_GLIB_SOURCE_PRIORITY);

    {
        GMutexHolder lock(threading.mutex);
        g_source_attach(source, glib.context);
        g_cond_wait(&threading.cond, &threading.mutex);
    }

    g_source_unref(source);
}

void WPEThreadedView::frameComplete()
{
    GST_DEBUG("frame complete");

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            auto& view = *static_cast<WPEThreadedView*>(data);
            GMutexHolder lock(view.threading.mutex);

            GST_DEBUG("dispatching");
            wpe_view_backend_exportable_fdo_dispatch_frame_complete(view.wpe.exportable);

            g_cond_signal(&view.threading.cond);
            return G_SOURCE_REMOVE;
        },
        this, nullptr);
    g_source_set_priority(source, WPE_GLIB_SOURCE_PRIORITY);

    {
        GMutexHolder lock(threading.mutex);
        g_source_attach(source, glib.context);
        g_cond_wait(&threading.cond, &threading.mutex);
    }

    g_source_unref(source);
}

void WPEThreadedView::loadUriUnlocked(const gchar* uri)
{
    if (webkit.uri)
        g_free(webkit.uri);
    webkit.uri = g_strdup(uri);
    webkit_web_view_load_uri(webkit.view, webkit.uri);
}

void WPEThreadedView::loadUri(const gchar* uri)
{
    GST_DEBUG("loading %s", uri);

    struct UriContext {
        WPEThreadedView& view;
        const gchar* uri;
    } uriContext{ *this, uri };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            GST_DEBUG("on view thread");
            auto& uriContext = *static_cast<UriContext*>(data);
            auto& view = uriContext.view;
            GMutexHolder lock(view.threading.mutex);

            view.loadUriUnlocked(uriContext.uri);

            g_cond_signal(&view.threading.cond);
            return G_SOURCE_REMOVE;
        },
        &uriContext, nullptr);
    g_source_set_priority(source, WPE_GLIB_SOURCE_PRIORITY);

    {
        GMutexHolder lock(threading.mutex);
        g_source_attach(source, glib.context);
        g_cond_wait(&threading.cond, &threading.mutex);
        GST_DEBUG("done");
    }

    g_source_unref(source);
}

void WPEThreadedView::setDrawBackground(gboolean drawsBackground)
{
#if 1
    // See https://bugs.webkit.org/show_bug.cgi?id=192305
    GST_FIXME("set_draws_background API not upstream yet");
#else
    webkit_web_view_set_draws_background(webkit.view, drawsBackground);
#endif
}

void WPEThreadedView::releaseImage(EGLImageKHR image)
{
    struct ReleaseImageContext {
        WPEThreadedView& view;
        EGLImageKHR image;
    } releaseImageContext{ *this, image };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            auto& releaseImageContext = *static_cast<ReleaseImageContext*>(data);
            auto& view = releaseImageContext.view;
            GMutexHolder lock(view.threading.mutex);

            wpe_view_backend_exportable_fdo_egl_dispatch_release_image(
                releaseImageContext.view.wpe.exportable, releaseImageContext.image);

            g_cond_signal(&view.threading.cond);
            return G_SOURCE_REMOVE;
        },
        &releaseImageContext, nullptr);
    g_source_set_priority(source, WPE_GLIB_SOURCE_PRIORITY);

    {
        GMutexHolder lock(threading.mutex);
        g_source_attach(source, glib.context);
        g_cond_wait(&threading.cond, &threading.mutex);
    }

    g_source_unref(source);
}

struct wpe_view_backend_exportable_fdo_egl_client WPEThreadedView::s_exportableClient = {
    // export_buffer_resource
    [](void* data, EGLImageKHR image) {
        auto& view = *static_cast<WPEThreadedView*>(data);
        auto* gstImage = gst_egl_image_new_wrapped(view.gst.context, image,
            GST_GL_RGBA, &view, s_releaseImage);
        GMutexHolder lock(view.images.mutex);

        view.images.pending = gstImage;
    },
    // padding
    nullptr, nullptr, nullptr, nullptr
};

void WPEThreadedView::s_releaseImage(GstEGLImage* image, gpointer data)
{
    auto& view = *static_cast<WPEThreadedView*>(data);
    GST_DEBUG("view %p image %" GST_PTR_FORMAT, &view, image);
    view.releaseImage(gst_egl_image_get_image(image));
}
