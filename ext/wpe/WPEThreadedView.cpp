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

WPEThreadedView::WPEThreadedView()
{
    g_mutex_init(&threading.mutex);
    g_cond_init(&threading.cond);
    g_mutex_init(&threading.ready_mutex);
    g_cond_init(&threading.ready_cond);

    g_mutex_init(&images_mutex);

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
        GMutexHolder lock(images_mutex);

        if (egl.pending) {
            gst_egl_image_unref(egl.pending);
            egl.pending = nullptr;
        }
        if (egl.committed) {
            gst_egl_image_unref(egl.committed);
            egl.committed = nullptr;
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
    g_mutex_clear(&images_mutex);
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

struct InitializeContext {
    GstWpeSrc* src;
    WPEThreadedView& view;
    GstGLContext* context;
    GstGLDisplay* display;
    EGLDisplay eglDisplay;
    int width;
    int height;
    bool result;
    gulong loadFailedHandler;
};

void WPEThreadedView::s_loadFailed(WebKitWebView*, WebKitLoadEvent event, gchar *failing_uri, GError *error, gpointer data)
{
    InitializeContext *ctx = (InitializeContext*) data;
    GMutexHolder lock(ctx->view.threading.ready_mutex);


    GST_ERROR_OBJECT (ctx->src, "Failed to load %s (%s)", failing_uri, error->message);
    ctx->result = false;

    ctx->view.threading.ready = true;
    g_cond_signal(&ctx->view.threading.ready_cond);
}

bool WPEThreadedView::initialize(GstWpeSrc* src, GstGLContext* context, GstGLDisplay* display, int width, int height)
{
    GST_DEBUG("context %p display %p, size (%d,%d)", context, display, width, height);
    threading.ready = FALSE;

    static std::once_flag s_loaderFlag;
    std::call_once(s_loaderFlag,
        [] {
#if defined(WPE_BACKEND_CHECK_VERSION) && WPE_BACKEND_CHECK_VERSION(1, 2, 0)
            wpe_loader_init("libWPEBackend-fdo-1.0.so");
#endif
        });

    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    if (context && display)
      eglDisplay = gst_gl_display_egl_get_from_native(GST_GL_DISPLAY_TYPE_WAYLAND,
                                                      gst_gl_display_get_handle(display));
    GST_DEBUG("eglDisplay %p", eglDisplay);

    struct InitializeContext initializeContext { src, *this, context, display, eglDisplay, width, height, FALSE, 0 };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            GST_DEBUG("on view thread");
            auto& initializeContext = *static_cast<InitializeContext*>(data);
            auto& view = initializeContext.view;

            GMutexHolder lock(view.threading.mutex);

            if (initializeContext.context)
              view.gst.context = GST_GL_CONTEXT(gst_object_ref(initializeContext.context));
            if (initializeContext.display)
              view.gst.display = GST_GL_DISPLAY(gst_object_ref(initializeContext.display));

            view.wpe.width = initializeContext.width;
            view.wpe.height = initializeContext.height;

            if (initializeContext.eglDisplay) {
              initializeContext.result = wpe_fdo_initialize_for_egl_display(initializeContext.eglDisplay);
              GST_DEBUG("FDO EGL display initialisation result: %d", initializeContext.result);
            } else {
#if ENABLE_SHM_BUFFER_SUPPORT
              initializeContext.result = wpe_fdo_initialize_shm();
              GST_DEBUG("FDO SHM initialisation result: %d", initializeContext.result);
#else
              GST_WARNING("FDO SHM support is available only in WPEBackend-FDO 1.7.0");
#endif
            }
            if (!initializeContext.result) {
              g_cond_signal(&view.threading.cond);
              return G_SOURCE_REMOVE;
            }

            if (initializeContext.eglDisplay) {
              view.wpe.exportable = wpe_view_backend_exportable_fdo_egl_create(&s_exportableEGLClient,
                  &view, view.wpe.width, view.wpe.height);
            } else {
#if ENABLE_SHM_BUFFER_SUPPORT
              view.wpe.exportable = wpe_view_backend_exportable_fdo_create(&s_exportableClient,
                  &view, view.wpe.width, view.wpe.height);
#endif
            }
            auto* wpeViewBackend = wpe_view_backend_exportable_fdo_get_view_backend(view.wpe.exportable);
            auto* viewBackend = webkit_web_view_backend_new(wpeViewBackend, nullptr, nullptr);
#if defined(WPE_BACKEND_CHECK_VERSION) && WPE_BACKEND_CHECK_VERSION(1, 1, 0)
            wpe_view_backend_add_activity_state(wpeViewBackend, wpe_view_activity_state_visible | wpe_view_activity_state_focused | wpe_view_activity_state_in_window);
#endif

            view.webkit.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                "backend", viewBackend, nullptr));

            gst_wpe_src_configure_web_view(initializeContext.src, view.webkit.view);

            initializeContext.loadFailedHandler = g_signal_connect(view.webkit.view,
                "load-failed", G_CALLBACK(s_loadFailed), &initializeContext);

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
        while (!threading.ready)
          g_cond_wait(&threading.ready_cond, &threading.ready_mutex);
        GST_DEBUG("done");
    }

    if (initializeContext.loadFailedHandler)
      g_signal_handler_disconnect (webkit.view, initializeContext.loadFailedHandler);

    return initializeContext.result;
}

GstEGLImage* WPEThreadedView::image()
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

GstBuffer* WPEThreadedView::buffer()
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
      GMutexHolder lock(images_mutex);

      GST_TRACE("EGLImage %p wrapped in GstEGLImage %" GST_PTR_FORMAT, eglImage, gstImage);
      egl.pending = gstImage;

      {
        GMutexHolder lock(threading.ready_mutex);
        if (!threading.ready) {
          threading.ready = TRUE;
          g_cond_signal(&threading.ready_cond);
        }
      }
    }
}

#if ENABLE_SHM_BUFFER_SUPPORT
struct SHMBufferContext {
  WPEThreadedView* view;
  struct wpe_fdo_shm_exported_buffer* buffer;
};

void WPEThreadedView::releaseSHMBuffer(gpointer data)
{
    SHMBufferContext* context = static_cast<SHMBufferContext*>(data);
    struct ReleaseBufferContext {
        WPEThreadedView& view;
        SHMBufferContext* context;
    } releaseImageContext{ *this, context };

    GSource* source = g_idle_source_new();
    g_source_set_callback(source,
        [](gpointer data) -> gboolean {
            auto& releaseBufferContext = *static_cast<ReleaseBufferContext*>(data);
            auto& view = releaseBufferContext.view;
            GMutexHolder lock(view.threading.mutex);

            struct wpe_fdo_shm_exported_buffer* buffer = static_cast<struct wpe_fdo_shm_exported_buffer*>(releaseBufferContext.context->buffer);
            GST_TRACE("Dispatch release exported buffer %p", buffer);
            wpe_view_backend_exportable_fdo_dispatch_release_shm_exported_buffer(view.wpe.exportable, buffer);
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

void WPEThreadedView::s_releaseSHMBuffer(gpointer data)
{
    SHMBufferContext* context = static_cast<SHMBufferContext*>(data);
    context->view->releaseSHMBuffer(data);
    g_slice_free(SHMBufferContext, context);
}

void WPEThreadedView::handleExportedBuffer(struct wpe_fdo_shm_exported_buffer* buffer)
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
      {
        GMutexHolder lock(threading.ready_mutex);
        if (!threading.ready) {
          threading.ready = TRUE;
          g_cond_signal(&threading.ready_cond);
        }
      }
    }
}
#endif

struct wpe_view_backend_exportable_fdo_egl_client WPEThreadedView::s_exportableEGLClient = {
#if USE_DEPRECATED_FDO_EGL_IMAGE
    // export_egl_image
    [](void* data, EGLImageKHR image) {
        auto& view = *static_cast<WPEThreadedView*>(data);
        view.handleExportedImage(static_cast<gpointer>(image));
    },
    nullptr, nullptr,
#else
    // export_egl_image
    nullptr,
    [](void* data, struct wpe_fdo_egl_exported_image* image) {
        auto& view = *static_cast<WPEThreadedView*>(data);
        view.handleExportedImage(static_cast<gpointer>(image));
    },
    nullptr,
#endif // USE_DEPRECATED_FDO_EGL_IMAGE
    // padding
    nullptr, nullptr
};

#if ENABLE_SHM_BUFFER_SUPPORT
struct wpe_view_backend_exportable_fdo_client WPEThreadedView::s_exportableClient = {
    nullptr,
    nullptr,
    // export_shm_buffer
    [](void* data, struct wpe_fdo_shm_exported_buffer* buffer) {
        auto& view = *static_cast<WPEThreadedView*>(data);
        view.handleExportedBuffer(buffer);
    },
    nullptr,
    nullptr,
};
#endif

void WPEThreadedView::s_releaseImage(GstEGLImage* image, gpointer data)
{
    ImageContext* context = static_cast<ImageContext*>(data);
    context->view->releaseImage(context->image);
    g_slice_free(ImageContext, context);
}
