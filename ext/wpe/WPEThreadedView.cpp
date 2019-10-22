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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "WPEThreadedView.h"

#include <gst/gl/gl.h>
#include <gst/gl/egl/gsteglimage.h>
#include <gst/gl/egl/gstgldisplay_egl.h>

#include <cstdio>
#include <mutex>

GST_DEBUG_CATEGORY_EXTERN (wpe_src_debug);
#define GST_CAT_DEFAULT wpe_src_debug

#if defined(WPE_FDO_CHECK_VERSION) && WPE_FDO_CHECK_VERSION(1, 3, 0)
#define USE_DEPRECATED_FDO_EGL_IMAGE 0
#define WPE_GLIB_SOURCE_PRIORITY G_PRIORITY_DEFAULT
#else
#define USE_DEPRECATED_FDO_EGL_IMAGE 1
#define WPE_GLIB_SOURCE_PRIORITY -70
#endif

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

bool WPEThreadedView::initialize(GstWpeSrc* src, GstGLContext* context, GstGLDisplay* display, int width, int height)
{
    GST_DEBUG("context %p display %p, size (%d,%d)", context, display, width, height);

    static std::once_flag s_loaderFlag;
    std::call_once(s_loaderFlag,
        [] {
#if defined(WPE_BACKEND_CHECK_VERSION) && WPE_BACKEND_CHECK_VERSION(1, 2, 0)
            wpe_loader_init("libWPEBackend-fdo-1.0.so");
#endif
        });

    EGLDisplay eglDisplay = gst_gl_display_egl_get_from_native(GST_GL_DISPLAY_TYPE_WAYLAND,
        gst_gl_display_get_handle(display));
    GST_DEBUG("eglDisplay %p", eglDisplay);

    struct InitializeContext {
        GstWpeSrc* src;
        WPEThreadedView& view;
        GstGLContext* context;
        GstGLDisplay* display;
        EGLDisplay eglDisplay;
        int width;
        int height;
      bool result;
    } initializeContext { src, *this, context, display, eglDisplay, width, height, FALSE };

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

            initializeContext.result = wpe_fdo_initialize_for_egl_display(initializeContext.eglDisplay);
            GST_DEBUG("FDO EGL display initialisation result: %d", initializeContext.result);
            if (!initializeContext.result) {
              g_cond_signal(&view.threading.cond);
              return G_SOURCE_REMOVE;
            }

            view.wpe.exportable = wpe_view_backend_exportable_fdo_egl_create(&s_exportableClient,
                &view, view.wpe.width, view.wpe.height);
            auto* wpeViewBackend = wpe_view_backend_exportable_fdo_get_view_backend(view.wpe.exportable);
            auto* viewBackend = webkit_web_view_backend_new(wpeViewBackend, nullptr, nullptr);
#if defined(WPE_BACKEND_CHECK_VERSION) && WPE_BACKEND_CHECK_VERSION(1, 1, 0)
            wpe_view_backend_add_activity_state(wpeViewBackend, wpe_view_activity_state_visible | wpe_view_activity_state_focused | wpe_view_activity_state_in_window);
#endif

            view.webkit.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                "backend", viewBackend, nullptr));

            gst_wpe_src_configure_web_view(initializeContext.src, view.webkit.view);

            g_signal_connect(view.webkit.view, "load-changed", G_CALLBACK(s_loadEvent), &view);

            const gchar* location;
            gboolean drawBackground = TRUE;
            g_object_get(initializeContext.src, "location", &location, "draw-background", &drawBackground, nullptr);
            view.setDrawBackground(drawBackground);
            if (location)
                view.loadUriUnlocked(location);
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

    if (initializeContext.result && webkit.uri) {
        GST_DEBUG("waiting load to finish");
        GMutexHolder lock(threading.ready_mutex);
        g_cond_wait(&threading.ready_cond, &threading.ready_mutex);
        GST_DEBUG("done");
    }
    return initializeContext.result;
}

GstEGLImage* WPEThreadedView::image()
{
    GstEGLImage* ret = nullptr;
    bool dispatchFrameComplete = false;

    {
        GMutexHolder lock(images.mutex);

        GST_TRACE("pending %" GST_PTR_FORMAT " (%d) committed %" GST_PTR_FORMAT " (%d)", images.pending,
                  GST_IS_EGL_IMAGE(images.pending) ? GST_MINI_OBJECT_REFCOUNT_VALUE(GST_MINI_OBJECT_CAST(images.pending)) : 0,
                  images.committed,
                  GST_IS_EGL_IMAGE(images.committed) ? GST_MINI_OBJECT_REFCOUNT_VALUE(GST_MINI_OBJECT_CAST(images.committed)) : 0);

        if (images.pending) {
            auto* previousImage = images.committed;
            images.committed = images.pending;
            images.pending = nullptr;

            if (previousImage)
                gst_egl_image_unref(previousImage);
            dispatchFrameComplete = true;
        }

        if (images.committed)
            ret = images.committed;
    }

    if (dispatchFrameComplete)
        frameComplete();

    return ret;
}

void WPEThreadedView::resize(int width, int height)
{
    GST_DEBUG("resize to %dx%d", width, height);
    wpe.width = width;
    wpe.height = height;

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
    GST_TRACE("frame complete");

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            auto& view = *static_cast<WPEThreadedView*>(data);
            GMutexHolder lock(view.threading.mutex);

            GST_TRACE("dispatching");
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

    GST_DEBUG("loading %s", uri);
    webkit.uri = g_strdup(uri);
    webkit_web_view_load_uri(webkit.view, webkit.uri);
}

void WPEThreadedView::loadUri(const gchar* uri)
{
    struct UriContext {
        WPEThreadedView& view;
        const gchar* uri;
    } uriContext { *this, uri };

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

void WPEThreadedView::loadData(GBytes* bytes)
{
    struct DataContext {
        WPEThreadedView& view;
        GBytes* bytes;
    } dataContext { *this, g_bytes_ref(bytes) };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            GST_DEBUG("on view thread");
            auto& dataContext = *static_cast<DataContext*>(data);
            auto& view = dataContext.view;
            GMutexHolder lock(view.threading.mutex);

            webkit_web_view_load_bytes(view.webkit.view, dataContext.bytes, nullptr, nullptr, nullptr);
            g_bytes_unref(dataContext.bytes);

            g_cond_signal(&view.threading.cond);
            return G_SOURCE_REMOVE;
        },
        &dataContext, nullptr);
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
#if WEBKIT_CHECK_VERSION(2, 24, 0)
    GST_DEBUG("%s background rendering", drawsBackground ? "Enabling" : "Disabling");
    WebKitColor color;
    webkit_color_parse(&color, drawsBackground ? "white" : "transparent");
    webkit_web_view_set_background_color(webkit.view, &color);
#else
    GST_FIXME("webkit_web_view_set_background_color is not implemented in WPE %u.%u. Please upgrade to 2.24", webkit_get_major_version(), webkit_get_minor_version());
#endif
}

void WPEThreadedView::releaseImage(gpointer imagePointer)
{
    struct ReleaseImageContext {
        WPEThreadedView& view;
        gpointer imagePointer;
    } releaseImageContext{ *this, imagePointer };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            auto& releaseImageContext = *static_cast<ReleaseImageContext*>(data);
            auto& view = releaseImageContext.view;
            GMutexHolder lock(view.threading.mutex);

            GST_TRACE("Dispatch release exported image %p", releaseImageContext.imagePointer);
#if USE_DEPRECATED_FDO_EGL_IMAGE
            wpe_view_backend_exportable_fdo_egl_dispatch_release_image(releaseImageContext.view.wpe.exportable,
                static_cast<EGLImageKHR>(releaseImageContext.imagePointer));
#else
            wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(releaseImageContext.view.wpe.exportable,
                static_cast<struct wpe_fdo_egl_exported_image*>(releaseImageContext.imagePointer));
#endif
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

struct ImageContext {
    WPEThreadedView* view;
    gpointer image;
};

void WPEThreadedView::handleExportedImage(gpointer image)
{
    ImageContext* imageContext = g_slice_new(ImageContext);
    imageContext->view = this;
    imageContext->image = static_cast<gpointer>(image);
    EGLImageKHR eglImage;
#if USE_DEPRECATED_FDO_EGL_IMAGE
    eglImage = static_cast<EGLImageKHR>(image);
#else
    eglImage = wpe_fdo_egl_exported_image_get_egl_image(static_cast<struct wpe_fdo_egl_exported_image*>(image));
#endif

    auto* gstImage = gst_egl_image_new_wrapped(gst.context, eglImage, GST_GL_RGBA, imageContext, s_releaseImage);
    {
      GMutexHolder lock(images.mutex);

      GST_TRACE("EGLImage %p wrapped in GstEGLImage %" GST_PTR_FORMAT, eglImage, gstImage);
      images.pending = gstImage;
    }
}

struct wpe_view_backend_exportable_fdo_egl_client WPEThreadedView::s_exportableClient = {
#if USE_DEPRECATED_FDO_EGL_IMAGE
    // export_egl_image
    [](void* data, EGLImageKHR image) {
        auto& view = *static_cast<WPEThreadedView*>(data);
        view.handleExportedImage(static_cast<gpointer>(image));
    },
    nullptr,
#else
    // export_egl_image
    nullptr,
    [](void* data, struct wpe_fdo_egl_exported_image* image) {
        auto& view = *static_cast<WPEThreadedView*>(data);
        view.handleExportedImage(static_cast<gpointer>(image));
    },
#endif
    // padding
    nullptr, nullptr, nullptr
};

void WPEThreadedView::s_releaseImage(GstEGLImage* image, gpointer data)
{
    ImageContext* context = static_cast<ImageContext*>(data);
    context->view->releaseImage(context->image);
    g_slice_free(ImageContext, context);
}
