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

#pragma once

#include <EGL/egl.h>
#include <glib.h>
#include <gst/gl/gstglfuncs.h>
#include <wpe/fdo.h>
#include <wpe/fdo-egl.h>
#include <wpe/webkit.h>
#include "gstwpesrc.h"

typedef struct _GstGLContext GstGLContext;
typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstEGLImage GstEGLImage;

#if defined(WPE_FDO_CHECK_VERSION) && WPE_FDO_CHECK_VERSION(1, 7, 0)
#define ENABLE_SHM_BUFFER_SUPPORT 1
#else
#define ENABLE_SHM_BUFFER_SUPPORT 0
#endif

class WPEThreadedView {
public:
    WPEThreadedView();
    ~WPEThreadedView();

    bool initialize(GstWpeSrc*, GstGLContext*, GstGLDisplay*, int width, int height);

    void resize(int width, int height);
    void loadUri(const gchar*);
    void loadData(GBytes*);
    void setDrawBackground(gboolean);

    GstEGLImage* image();
    GstBuffer* buffer();

    struct wpe_view_backend* backend() const;

protected:
    void handleExportedImage(gpointer);
#if ENABLE_SHM_BUFFER_SUPPORT
    void handleExportedBuffer(struct wpe_fdo_shm_exported_buffer*);
#endif

private:
    void frameComplete();
    void loadUriUnlocked(const gchar*);

    void releaseImage(gpointer);
#if ENABLE_SHM_BUFFER_SUPPORT
    void releaseSHMBuffer(gpointer);
    static void s_releaseSHMBuffer(gpointer);
#endif

    static void s_loadFailed(WebKitWebView*, WebKitLoadEvent, gchar*, GError*, gpointer);

    static gpointer s_viewThread(gpointer);
    struct {
        GMutex mutex;
        GCond cond;
        GMutex ready_mutex;
        GCond ready_cond;
        gboolean ready;
        GThread* thread { nullptr };
    } threading;

    struct {
        GMainContext* context;
        GMainLoop* loop;
    } glib { nullptr, nullptr };

    struct {
        GstGLContext* context;
        GstGLDisplay* display;
    } gst { nullptr, nullptr };

    static struct wpe_view_backend_exportable_fdo_egl_client s_exportableEGLClient;
#if ENABLE_SHM_BUFFER_SUPPORT
    static struct wpe_view_backend_exportable_fdo_client s_exportableClient;
#endif

    static void s_releaseImage(GstEGLImage*, gpointer);
    struct {
        struct wpe_view_backend_exportable_fdo* exportable;
        int width;
        int height;
    } wpe { nullptr, 0, 0 };

    struct {
        gchar* uri;
        WebKitWebView* view;
    } webkit = { nullptr, nullptr };

    // This mutex guards access to either egl or shm resources declared below,
    // depending on the runtime behavior.
    GMutex images_mutex;

    struct {
        GstEGLImage* pending;
        GstEGLImage* committed;
    } egl { nullptr, nullptr };

    struct {
        GstBuffer* pending;
        GstBuffer* committed;
    } shm { nullptr, nullptr };
};
