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
#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#include <gst/gl/egl/gsteglimage.h>
#include <gst/gl/egl/gstgldisplay_egl.h>
#include <wpe/fdo.h>
#include <wpe/fdo-egl.h>
#include <wpe/webkit.h>
#include "gstwpesrc.h"

GST_DEBUG_CATEGORY_EXTERN(wpe_src_debug);

class WPEThreadedView {
public:
    WPEThreadedView();
    ~WPEThreadedView();

    void initialize(GstWpeSrc*, GstGLContext*, GstGLDisplay*, int width, int height);

    void resize(int width, int height);
    void loadUri(const gchar*);
    void setDrawBackground(gboolean);

    GstEGLImage* image();

    struct wpe_view_backend* backend() const;

private:
    void frameComplete();
    void releaseImage(EGLImageKHR);
    void loadUriUnlocked(const gchar*);

    static void s_loadEvent(WebKitWebView*, WebKitLoadEvent, gpointer);

    static gpointer s_viewThread(gpointer);
    struct {
        GMutex mutex;
        GCond cond;
        GMutex ready_mutex;
        GCond ready_cond;
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

    static struct wpe_view_backend_exportable_fdo_egl_client s_exportableClient;
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

    struct {
        GMutex mutex;
        GstEGLImage* pending { nullptr };
        GstEGLImage* committed { nullptr };
    } images;
};
