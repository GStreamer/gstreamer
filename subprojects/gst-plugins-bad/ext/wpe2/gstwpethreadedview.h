/* Copyright (C) <2018, 2025> Philippe Normand <philn@igalia.com>
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
#include <gst/gl/egl/gstgldisplay_egl.h>
#include <wpe/webkit.h>
#include "gstwpevideosrc.h"
#include <optional>
#include <utility>

typedef struct _GstGLContext GstGLContext;
typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstEGLImage GstEGLImage;

class GstWPEThreadedView {
public:
    GstWPEThreadedView(WebKitWebContext *, GstWpeVideoSrc2 *, GstGLContext *,
                       GstGLDisplay *, WPEDisplay *, int width, int height);
    ~GstWPEThreadedView();

    /* Used by gstwpeview */
    gboolean setPendingBuffer(WPEBuffer*, GError**);

    /*  Used by wpevideosrc */
    void resize(int width, int height);
    void loadUri(const gchar*);
    void loadData(GBytes*);
    void runJavascript(const gchar*);
    void setDrawBackground(gboolean);
    void clearBuffers();

    GstEGLImage* image();
    GstBuffer* buffer();

    gboolean dispatchKeyboardEvent(GstEvent*);
    gboolean dispatchPointerEvent(GstEvent*);
    gboolean dispatchPointerMoveEvent(GstEvent*);
    gboolean dispatchAxisEvent(GstEvent*);
    gboolean dispatchTouchEvent(GstEvent*);

    /*  Used by GstWPEContextThread */
    bool hasUri() const { return webkit.uri; }
    void disconnectLoadFailedSignal();
    void waitLoadCompletion();

    GstWpeVideoSrc2 *src() const { return m_src; }

    void notifyLoadFinished();

private:
    void frameComplete();

    void dispatchEvent(WPEEvent*);
    void loadUriUnlocked(const gchar*);

    static void s_releaseBuffer(gpointer);

    struct {
        GstGLContext* context;
        GstGLDisplay* display;
        GstGLDisplayEGL* display_egl;
    } gst { nullptr, nullptr, nullptr };

    struct {
        WPEView *view;
        int width;
        int height;
    } wpe { nullptr, 0, 0 };

    struct {
        gchar* uri;
        WebKitWebView* view;
    } webkit = { nullptr, nullptr };

    struct {
        GMutex ready_mutex;
        GCond ready_cond;
        gboolean ready;
    } threading;

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

    struct {
        gulong init_ext_sigid;
        gulong extension_msg_sigid;
    } audio {0, 0};

    GstWpeVideoSrc2 *m_src { nullptr };

  WPEBuffer *m_pending_buffer { nullptr };
  WPEBuffer *m_committed_buffer { nullptr };

  std::optional<std::pair<gdouble, gdouble>> m_last_pointer_position;
};

class GstWPEContextThread {
public:
    static GstWPEContextThread& singleton();

    GstWPEContextThread();
    ~GstWPEContextThread();

    GstWPEThreadedView* createWPEView(GstWpeVideoSrc2*, GstGLContext*, GstGLDisplay*, WPEDisplay*, int width, int height);

    template<typename Function>
    void dispatch(Function);

private:
    static gpointer s_viewThread(gpointer);
    struct {
        GMutex mutex;
        GCond cond;
        gboolean ready;
        GThread* thread { nullptr };
    } threading;

    struct {
        GMainContext* context;
        GMainLoop* loop;
        WebKitWebContext* web_context;
    } glib { nullptr, nullptr, nullptr };
};
