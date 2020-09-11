/* Copyright (C) <2018, 2019, 2020> Philippe Normand <philn@igalia.com>
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
#include <wayland-server.h>

#include <cstdio>
#include <mutex>

#if ENABLE_SHM_BUFFER_SUPPORT
#include <wpe/unstable/fdo-shm.h>
#endif

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

static WPEContextThread *s_view = NULL;

WPEContextThread& WPEContextThread::singleton()
{
    if (!s_view)
        s_view = new WPEContextThread;

    return *s_view;
}

WPEContextThread::WPEContextThread()
{
    g_mutex_init(&threading.mutex);
    g_cond_init(&threading.cond);

    {
        GMutexHolder lock(threading.mutex);
        threading.thread = g_thread_new("WPEContextThread", s_viewThread, this);
        g_cond_wait(&threading.cond, &threading.mutex);
        GST_DEBUG("thread spawned");
    }
}

WPEContextThread::~WPEContextThread()
{
    if (threading.thread) {
        g_thread_unref(threading.thread);
        threading.thread = nullptr;
    }

    g_mutex_clear(&threading.mutex);
    g_cond_clear(&threading.cond);
}

template<typename Function>
void WPEContextThread::dispatch(Function func)
{
    struct Payload {
        Function& func;
    };
    struct Payload payload { func };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source, [](gpointer data) -> gboolean {
        auto& view = WPEContextThread::singleton();
        GMutexHolder lock(view.threading.mutex);

        auto* payload = static_cast<struct Payload*>(data);
        payload->func();

        g_cond_signal(&view.threading.cond);
        return G_SOURCE_REMOVE;
    }, &payload, nullptr);
    g_source_set_priority(source, WPE_GLIB_SOURCE_PRIORITY);

    {
        GMutexHolder lock(threading.mutex);
        g_source_attach(source, glib.context);
        g_cond_wait(&threading.cond, &threading.mutex);
    }

    g_source_unref(source);
}

gpointer WPEContextThread::s_viewThread(gpointer data)
{
    auto& view = *static_cast<WPEContextThread*>(data);

    view.glib.context = g_main_context_new();
    view.glib.loop = g_main_loop_new(view.glib.context, FALSE);

    g_main_context_push_thread_default(view.glib.context);

    {
        GSource* source = g_idle_source_new();
        g_source_set_callback(source,
            [](gpointer data) -> gboolean {
                auto& view = *static_cast<WPEContextThread*>(data);
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

    g_main_context_pop_thread_default(view.glib.context);
    g_main_context_unref(view.glib.context);
    view.glib.context = nullptr;
    return nullptr;
}

WPEView* WPEContextThread::createWPEView(GstWpeSrc* src, GstGLContext* context, GstGLDisplay* display, int width, int height)
{
    GST_DEBUG("context %p display %p, size (%d,%d)", context, display, width, height);

    static std::once_flag s_loaderFlag;
    std::call_once(s_loaderFlag,
        [] {
#if defined(WPE_BACKEND_CHECK_VERSION) && WPE_BACKEND_CHECK_VERSION(1, 2, 0)
            wpe_loader_init("libWPEBackend-fdo-1.0.so");
#endif
        });

    WPEView* view = nullptr;
    dispatch([&]() mutable {
        if (!glib.web_context) {
            auto* manager = webkit_website_data_manager_new_ephemeral();
            glib.web_context = webkit_web_context_new_with_website_data_manager(manager);
            g_object_unref(manager);
        }

        view = new WPEView(glib.web_context, src, context, display, width, height);
    });

    if (view && view->hasUri()) {
        GST_DEBUG("waiting load to finish");
        view->waitLoadCompletion();
        GST_DEBUG("done");
    }

    return view;
}

static gboolean s_loadFailed(WebKitWebView*, WebKitLoadEvent, gchar* failing_uri, GError* error, gpointer data)
{
    GstWpeSrc* src = GST_WPE_SRC(data);
    GST_ELEMENT_ERROR (GST_ELEMENT_CAST(src), RESOURCE, FAILED, (NULL), ("Failed to load %s (%s)", failing_uri, error->message));
    return FALSE;
}

static gboolean s_loadFailedWithTLSErrors(WebKitWebView*,  gchar* failing_uri, GTlsCertificate*, GTlsCertificateFlags, gpointer data)
{
    // Defer to load-failed.
    return FALSE;
}

WPEView::WPEView(WebKitWebContext* web_context, GstWpeSrc* src, GstGLContext* context, GstGLDisplay* display, int width, int height)
{
    g_mutex_init(&threading.ready_mutex);
    g_cond_init(&threading.ready_cond);
    threading.ready = FALSE;

    g_mutex_init(&images_mutex);
    if (context)
        gst.context = GST_GL_CONTEXT(gst_object_ref(context));
    if (display)
        gst.display = GST_GL_DISPLAY(gst_object_ref(display));

    wpe.width = width;
    wpe.height = height;

    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    if (context && display)
        eglDisplay = gst_gl_display_egl_get_from_native(GST_GL_DISPLAY_TYPE_WAYLAND, gst_gl_display_get_handle(display));
    GST_DEBUG("eglDisplay %p", eglDisplay);

    if (eglDisplay) {
        m_isValid = wpe_fdo_initialize_for_egl_display(eglDisplay);
        GST_DEBUG("FDO EGL display initialisation result: %d", m_isValid);
    } else {
#if ENABLE_SHM_BUFFER_SUPPORT
        m_isValid = wpe_fdo_initialize_shm();
        GST_DEBUG("FDO SHM initialisation result: %d", m_isValid);
#else
        GST_WARNING("FDO SHM support is available only in WPEBackend-FDO 1.7.0");
#endif
    }
    if (!m_isValid)
        return;

    if (eglDisplay) {
        wpe.exportable = wpe_view_backend_exportable_fdo_egl_create(&s_exportableEGLClient, this, wpe.width, wpe.height);
    } else {
#if ENABLE_SHM_BUFFER_SUPPORT
        wpe.exportable = wpe_view_backend_exportable_fdo_create(&s_exportableClient, this, wpe.width, wpe.height);
#endif
    }

    auto* wpeViewBackend = wpe_view_backend_exportable_fdo_get_view_backend(wpe.exportable);
    auto* viewBackend = webkit_web_view_backend_new(wpeViewBackend, (GDestroyNotify) wpe_view_backend_exportable_fdo_destroy, wpe.exportable);
#if defined(WPE_BACKEND_CHECK_VERSION) && WPE_BACKEND_CHECK_VERSION(1, 1, 0)
    wpe_view_backend_add_activity_state(wpeViewBackend, wpe_view_activity_state_visible | wpe_view_activity_state_focused | wpe_view_activity_state_in_window);
#endif

    webkit.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", web_context, "backend", viewBackend, nullptr));

    g_signal_connect(webkit.view, "load-failed", G_CALLBACK(s_loadFailed), src);
    g_signal_connect(webkit.view, "load-failed-with-tls-errors", G_CALLBACK(s_loadFailedWithTLSErrors), src);

    gst_wpe_src_configure_web_view(src, webkit.view);

    const gchar* location;
    gboolean drawBackground = TRUE;
    g_object_get(src, "location", &location, "draw-background", &drawBackground, nullptr);
    setDrawBackground(drawBackground);
    if (location)
        loadUriUnlocked(location);
}

WPEView::~WPEView()
{
    g_mutex_clear(&threading.ready_mutex);
    g_cond_clear(&threading.ready_cond);

    {
        GMutexHolder lock(images_mutex);

        if (egl.pending) {
            gst_egl_image_unref(egl.pending);
            egl.pending = nullptr;
        }
        if (egl.committed) {
            gst_egl_image_unref(egl.committed);
            egl.committed = nullptr;
        }
        if (shm.pending) {
            gst_buffer_unref(shm.pending);
            shm.pending = nullptr;
        }
        if (shm.committed) {
            gst_buffer_unref(shm.committed);
            shm.committed = nullptr;
        }
    }

    WPEContextThread::singleton().dispatch([&]() {
        if (webkit.view) {
            g_object_unref(webkit.view);
            webkit.view = nullptr;
        }
    });

    if (gst.display) {
        gst_object_unref(gst.display);
        gst.display = nullptr;
    }

    if (gst.context) {
        gst_object_unref(gst.context);
        gst.context = nullptr;
    }
    if (webkit.uri) {
        g_free(webkit.uri);
        webkit.uri = nullptr;
    }

    g_mutex_clear(&images_mutex);
}

void WPEView::notifyLoadFinished()
{
    GMutexHolder lock(threading.ready_mutex);
    if (!threading.ready) {
        threading.ready = TRUE;
        g_cond_signal(&threading.ready_cond);
    }
}

void WPEView::waitLoadCompletion()
{
    GMutexHolder lock(threading.ready_mutex);
    while (!threading.ready)
        g_cond_wait(&threading.ready_cond, &threading.ready_mutex);
}

GstEGLImage* WPEView::image()
{
    GstEGLImage* ret = nullptr;
    bool dispatchFrameComplete = false;

    {
        GMutexHolder lock(images_mutex);

        GST_TRACE("pending %" GST_PTR_FORMAT " (%d) committed %" GST_PTR_FORMAT " (%d)", egl.pending,
                  GST_IS_EGL_IMAGE(egl.pending) ? GST_MINI_OBJECT_REFCOUNT_VALUE(GST_MINI_OBJECT_CAST(egl.pending)) : 0,
                  egl.committed,
                  GST_IS_EGL_IMAGE(egl.committed) ? GST_MINI_OBJECT_REFCOUNT_VALUE(GST_MINI_OBJECT_CAST(egl.committed)) : 0);

        if (egl.pending) {
            auto* previousImage = egl.committed;
            egl.committed = egl.pending;
            egl.pending = nullptr;

            if (previousImage)
                gst_egl_image_unref(previousImage);
            dispatchFrameComplete = true;
        }

        if (egl.committed)
            ret = egl.committed;
    }

    if (dispatchFrameComplete)
        frameComplete();

    return ret;
}

GstBuffer* WPEView::buffer()
{
    GstBuffer* ret = nullptr;
    bool dispatchFrameComplete = false;

    {
        GMutexHolder lock(images_mutex);

        GST_TRACE("pending %" GST_PTR_FORMAT " (%d) committed %" GST_PTR_FORMAT " (%d)", shm.pending,
                  GST_IS_BUFFER(shm.pending) ? GST_MINI_OBJECT_REFCOUNT_VALUE(GST_MINI_OBJECT_CAST(shm.pending)) : 0,
                  shm.committed,
                  GST_IS_BUFFER(shm.committed) ? GST_MINI_OBJECT_REFCOUNT_VALUE(GST_MINI_OBJECT_CAST(shm.committed)) : 0);

        if (shm.pending) {
            auto* previousImage = shm.committed;
            shm.committed = shm.pending;
            shm.pending = nullptr;

            if (previousImage)
                gst_buffer_unref(previousImage);
            dispatchFrameComplete = true;
        }

        if (shm.committed)
            ret = shm.committed;
    }

    if (dispatchFrameComplete)
        frameComplete();

    return ret;
}

void WPEView::resize(int width, int height)
{
    GST_DEBUG("resize to %dx%d", width, height);
    wpe.width = width;
    wpe.height = height;

    s_view->dispatch([&]() {
        if (wpe.exportable && wpe_view_backend_exportable_fdo_get_view_backend(wpe.exportable))
          wpe_view_backend_dispatch_set_size(wpe_view_backend_exportable_fdo_get_view_backend(wpe.exportable), wpe.width, wpe.height);
    });
}

void WPEView::frameComplete()
{
    GST_TRACE("frame complete");
    s_view->dispatch([&]() {
        GST_TRACE("dispatching");
        wpe_view_backend_exportable_fdo_dispatch_frame_complete(wpe.exportable);
    });
}

void WPEView::loadUriUnlocked(const gchar* uri)
{
    if (webkit.uri)
        g_free(webkit.uri);

    GST_DEBUG("loading %s", uri);
    webkit.uri = g_strdup(uri);
    webkit_web_view_load_uri(webkit.view, webkit.uri);
}

void WPEView::loadUri(const gchar* uri)
{
    s_view->dispatch([&]() {
        loadUriUnlocked(uri);
    });
}

void WPEView::loadData(GBytes* bytes)
{
    s_view->dispatch([this, bytes = g_bytes_ref(bytes)]() {
        webkit_web_view_load_bytes(webkit.view, bytes, nullptr, nullptr, nullptr);
        g_bytes_unref(bytes);
    });
}

void WPEView::setDrawBackground(gboolean drawsBackground)
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

void WPEView::releaseImage(gpointer imagePointer)
{
    s_view->dispatch([&]() {
        GST_TRACE("Dispatch release exported image %p", imagePointer);
#if USE_DEPRECATED_FDO_EGL_IMAGE
        wpe_view_backend_exportable_fdo_egl_dispatch_release_image(wpe.exportable,
                                                                   static_cast<EGLImageKHR>(imagePointer));
#else
        wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(wpe.exportable,
                                                                            static_cast<struct wpe_fdo_egl_exported_image*>(imagePointer));
#endif
    });
}

struct ImageContext {
    WPEView* view;
    gpointer image;
};

void WPEView::handleExportedImage(gpointer image)
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
      GMutexHolder lock(images_mutex);

      GST_TRACE("EGLImage %p wrapped in GstEGLImage %" GST_PTR_FORMAT, eglImage, gstImage);
      egl.pending = gstImage;

      notifyLoadFinished();
    }
}

#if ENABLE_SHM_BUFFER_SUPPORT
struct SHMBufferContext {
    WPEView* view;
    struct wpe_fdo_shm_exported_buffer* buffer;
};

void WPEView::releaseSHMBuffer(gpointer data)
{
    SHMBufferContext* context = static_cast<SHMBufferContext*>(data);
    s_view->dispatch([&]() {
        auto* buffer = static_cast<struct wpe_fdo_shm_exported_buffer*>(context->buffer);
        GST_TRACE("Dispatch release exported buffer %p", buffer);
        wpe_view_backend_exportable_fdo_dispatch_release_shm_exported_buffer(wpe.exportable, buffer);
    });
}

void WPEView::s_releaseSHMBuffer(gpointer data)
{
    SHMBufferContext* context = static_cast<SHMBufferContext*>(data);
    context->view->releaseSHMBuffer(data);
    g_slice_free(SHMBufferContext, context);
}

void WPEView::handleExportedBuffer(struct wpe_fdo_shm_exported_buffer* buffer)
{
    struct wl_shm_buffer* shmBuffer = wpe_fdo_shm_exported_buffer_get_shm_buffer(buffer);
    auto format = wl_shm_buffer_get_format(shmBuffer);
    if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888) {
        GST_ERROR("Unsupported pixel format: %d", format);
        return;
    }

    int32_t width = wl_shm_buffer_get_width(shmBuffer);
    int32_t height = wl_shm_buffer_get_height(shmBuffer);
    gint stride = wl_shm_buffer_get_stride(shmBuffer);
    gsize size = width * height * 4;
    auto* data = static_cast<uint8_t*>(wl_shm_buffer_get_data(shmBuffer));

    SHMBufferContext* bufferContext = g_slice_new(SHMBufferContext);
    bufferContext->view = this;
    bufferContext->buffer = buffer;

    auto* gstBuffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, data, size, 0, size, bufferContext, s_releaseSHMBuffer);
    gsize offsets[1];
    gint strides[1];
    offsets[0] = 0;
    strides[0] = stride;
    gst_buffer_add_video_meta_full(gstBuffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_BGRA, width, height, 1, offsets, strides);

    {
        GMutexHolder lock(images_mutex);
        GST_TRACE("SHM buffer %p wrapped in buffer %" GST_PTR_FORMAT, buffer, gstBuffer);
        shm.pending = gstBuffer;
        notifyLoadFinished();
    }
}
#endif

struct wpe_view_backend_exportable_fdo_egl_client WPEView::s_exportableEGLClient = {
#if USE_DEPRECATED_FDO_EGL_IMAGE
    // export_egl_image
    [](void* data, EGLImageKHR image) {
        auto& view = *static_cast<WPEView*>(data);
        view.handleExportedImage(static_cast<gpointer>(image));
    },
    nullptr, nullptr,
#else
    // export_egl_image
    nullptr,
    [](void* data, struct wpe_fdo_egl_exported_image* image) {
        auto& view = *static_cast<WPEView*>(data);
        view.handleExportedImage(static_cast<gpointer>(image));
    },
    nullptr,
#endif // USE_DEPRECATED_FDO_EGL_IMAGE
    // padding
    nullptr, nullptr
};

#if ENABLE_SHM_BUFFER_SUPPORT
struct wpe_view_backend_exportable_fdo_client WPEView::s_exportableClient = {
    nullptr,
    nullptr,
    // export_shm_buffer
    [](void* data, struct wpe_fdo_shm_exported_buffer* buffer) {
        auto& view = *static_cast<WPEView*>(data);
        view.handleExportedBuffer(buffer);
    },
    nullptr,
    nullptr,
};
#endif

void WPEView::s_releaseImage(GstEGLImage* image, gpointer data)
{
    ImageContext* context = static_cast<ImageContext*>(data);
    context->view->releaseImage(context->image);
    g_slice_free(ImageContext, context);
}

struct wpe_view_backend* WPEView::backend() const
{
    return wpe.exportable ? wpe_view_backend_exportable_fdo_get_view_backend(wpe.exportable) : nullptr;
}

void WPEView::dispatchKeyboardEvent(struct wpe_input_keyboard_event& wpe_event)
{
    s_view->dispatch([&]() {
        wpe_view_backend_dispatch_keyboard_event(backend(), &wpe_event);
    });
}

void WPEView::dispatchPointerEvent(struct wpe_input_pointer_event& wpe_event)
{
    s_view->dispatch([&]() {
        wpe_view_backend_dispatch_pointer_event(backend(), &wpe_event);
    });
}

void WPEView::dispatchAxisEvent(struct wpe_input_axis_event& wpe_event)
{
    s_view->dispatch([&]() {
        wpe_view_backend_dispatch_axis_event(backend(), &wpe_event);
    });
}
