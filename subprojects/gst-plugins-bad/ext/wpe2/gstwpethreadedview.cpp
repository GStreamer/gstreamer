/* Copyright (C) <2018, 2019, 2020, 2025> Philippe Normand <philn@igalia.com>
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

#include "gstwpe.h"
#include "gstwpethreadedview.h"
#include "gstwpedisplay.h"
#include "gstwpeview.h"

#include <gst/gl/gl.h>
#include <gst/gl/egl/gsteglimage.h>
#include <gst/gl/egl/gstgldisplay_egl.h>

#include <cstdio>
#include <mutex>

GST_DEBUG_CATEGORY_EXTERN (wpe_view_debug);
#define GST_CAT_DEFAULT wpe_view_debug

/* *INDENT-OFF* */
class GMutexHolder {
public:
  GMutexHolder (GMutex & mutex)
    :m(mutex)
  {
    g_mutex_lock (&m);
  }
   ~GMutexHolder ()
  {
    g_mutex_unlock (&m);
  }

private:
  GMutex &m;
};
/* *INDENT-ON* */

static GstWPEContextThread *s_view = NULL;

GstWPEContextThread & GstWPEContextThread::singleton ()
{
  /* *INDENT-OFF* */
  static gsize initialized = 0;
  /* *INDENT-ON* */

  if (g_once_init_enter (&initialized)) {
    s_view = new GstWPEContextThread;

    g_once_init_leave (&initialized, 1);
  }

  return *s_view;
}

GstWPEContextThread::GstWPEContextThread ()
{
  g_mutex_init (&threading.mutex);
  g_cond_init (&threading.cond);
  threading.ready = FALSE;

  {
    GMutexHolder lock (threading.mutex);
    threading.thread = g_thread_new ("GstWPEContextThread", s_viewThread, this);
    while (!threading.ready) {
      g_cond_wait (&threading.cond, &threading.mutex);
    }
    GST_DEBUG ("thread spawned");
  }
}

GstWPEContextThread::~GstWPEContextThread ()
{
  if (threading.thread) {
    g_thread_unref (threading.thread);
    threading.thread = nullptr;
  }

  g_mutex_clear (&threading.mutex);
  g_cond_clear (&threading.cond);
}

template < typename Function > void
GstWPEContextThread::dispatch (Function func)
{
  /* *INDENT-OFF* */
  struct Job {
    Job (Function & f)
      :func (f)
    {
      g_mutex_init (&mutex);
      g_cond_init (&cond);
      dispatched = FALSE;
    }
    ~Job ()
    {
      g_mutex_clear (&mutex);
      g_cond_clear (&cond);
    }

    void dispatch ()
    {
      GMutexHolder lock (mutex);
      func ();
      dispatched = TRUE;
      g_cond_signal (&cond);
    }

    void waitCompletion ()
    {
      GMutexHolder lock (mutex);
      while (!dispatched) {
        g_cond_wait (&cond, &mutex);
      }
    }

    Function & func;
    GMutex mutex;
    GCond cond;
    gboolean dispatched;
  };
  /* *INDENT-ON* */

  struct Job job (func);
  GSource *source = g_idle_source_new ();
  /* *INDENT-OFF*  */
  g_source_set_callback (source,[](gpointer data)->gboolean {
      auto job = static_cast<struct Job *>(data);
      job->dispatch ();
      return G_SOURCE_REMOVE;
  }, &job, nullptr);
  /* *INDENT-ON*  */
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_attach (source, glib.context);
  job.waitCompletion ();
  g_source_unref (source);
}

gpointer
GstWPEContextThread::s_viewThread (gpointer data)
{
  /* *INDENT-OFF*  */
  auto &view = *static_cast<GstWPEContextThread *>(data);
  /* *INDENT-ON*  */

  view.glib.context = g_main_context_new ();
  view.glib.loop = g_main_loop_new (view.glib.context, FALSE);

  g_main_context_push_thread_default (view.glib.context);

  {
    GSource *source = g_idle_source_new ();
    /* *INDENT-OFF*  */
    g_source_set_callback(source, [](gpointer data) -> gboolean {
      auto& view = *static_cast<GstWPEContextThread*>(data);
      GMutexHolder lock (view.threading.mutex);
      view.threading.ready = TRUE;
      g_cond_signal(&view.threading.cond);
      return G_SOURCE_REMOVE;
    }, &view, nullptr);
    /* *INDENT-ON*  */
    g_source_attach (source, view.glib.context);
    g_source_unref (source);
  }

  g_main_loop_run (view.glib.loop);

  g_main_loop_unref (view.glib.loop);
  view.glib.loop = nullptr;

  g_main_context_pop_thread_default (view.glib.context);
  g_main_context_unref (view.glib.context);
  view.glib.context = nullptr;
  return nullptr;
}

GstWPEThreadedView *
GstWPEContextThread::createWPEView (GstWpeVideoSrc2 * src,
    GstGLContext * context,
    GstGLDisplay * display, WPEDisplay * wpe_display, int width, int height)
{
  GST_DEBUG ("context %p display %p, size (%d,%d)", context, display, width,
      height);

  GstWPEThreadedView *view = nullptr;
  /* *INDENT-OFF*  */
  dispatch([&]() mutable {
    if (!glib.web_context) {
      glib.web_context =
        WEBKIT_WEB_CONTEXT (g_object_new (WEBKIT_TYPE_WEB_CONTEXT, nullptr));
    }
    view =
      new GstWPEThreadedView (glib.web_context, src, context, display, wpe_display,
                              width, height);
  });
  /* *INDENT-ON*  */

  if (view && view->hasUri ()) {
    GST_DEBUG ("waiting load to finish");
    view->waitLoadCompletion ();
    GST_DEBUG ("done");
  }

  return view;
}

static gboolean
s_loadFailed (WebKitWebView *, WebKitLoadEvent, gchar * failing_uri,
    GError * error, gpointer data)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (data);

  if (g_error_matches (error, WEBKIT_NETWORK_ERROR,
          WEBKIT_NETWORK_ERROR_CANCELLED)) {
    GST_INFO_OBJECT (src, "Loading cancelled.");

    return FALSE;
  }

  GST_ELEMENT_ERROR (GST_ELEMENT_CAST (src), RESOURCE, FAILED, (NULL),
      ("Failed to load %s (%s)", failing_uri, error->message));
  return FALSE;
}

static gboolean
s_loadFailedWithTLSErrors (WebKitWebView *, gchar * failing_uri,
    GTlsCertificate *, GTlsCertificateFlags, gpointer data)
{
  // Defer to load-failed.
  return FALSE;
}

static void
s_loadProgressChanged (GObject * object, GParamSpec *, gpointer data)
{
  GstElement *src = GST_ELEMENT_CAST (data);
  // The src element is locked already so we can't call
  // gst_element_post_message(). Instead retrieve the bus manually and use it
  // directly.
  GstBus *bus = GST_ELEMENT_BUS (src);
  double estimatedProgress;
  g_object_get (object, "estimated-load-progress", &estimatedProgress, nullptr);
  gst_object_ref (bus);
  gst_bus_post (bus, gst_message_new_element (GST_OBJECT_CAST (src),
          gst_structure_new ("wpe-stats", "estimated-load-progress",
              G_TYPE_DOUBLE, estimatedProgress * 100, nullptr)));
  gst_object_unref (bus);
}

static void
s_webProcessCrashed (WebKitWebView *, WebKitWebProcessTerminationReason reason,
    gpointer data)
{
  /* *INDENT-OFF*  */
  auto &view = *static_cast<GstWPEThreadedView *>(data);
  /* *INDENT-ON*  */
  auto *src = view.src ();
  gchar *reason_str =
      g_enum_to_string (WEBKIT_TYPE_WEB_PROCESS_TERMINATION_REASON, reason);

  // In case the crash happened while doing the initial URL loading, unlock
  // the load completion waiting.
  view.notifyLoadFinished ();

  // TODO: Emit a signal here and fallback to error system if signal wasn't handled by application?

  GST_ELEMENT_ERROR (GST_ELEMENT_CAST (src), RESOURCE, FAILED, (NULL), ("%s",
          reason_str));

  g_free (reason_str);
}

/* *INDENT-OFF* */
GstWPEThreadedView::GstWPEThreadedView(
    WebKitWebContext *web_context, GstWpeVideoSrc2 *src, GstGLContext *context,
    GstGLDisplay *display, WPEDisplay *wpe_display, int width, int height)
    : m_src(src) {
  g_mutex_init (&threading.ready_mutex);
  g_cond_init (&threading.ready_cond);
  threading.ready = FALSE;

  g_mutex_init (&images_mutex);
  if (context)
    gst.context = GST_GL_CONTEXT (gst_object_ref (context));
  if (display)
    gst.display = GST_GL_DISPLAY (gst_object_ref (display));

  wpe.width = width;
  wpe.height = height;

  auto *defaultWebsitePolicies = webkit_website_policies_new_with_policies(
      "autoplay", WEBKIT_AUTOPLAY_ALLOW, nullptr);

  webkit.view = WEBKIT_WEB_VIEW(g_object_new(
      WEBKIT_TYPE_WEB_VIEW, "web-context", web_context, "display", wpe_display,
      "website-policies", defaultWebsitePolicies, nullptr));

  g_object_unref (wpe_display);
  g_object_unref(defaultWebsitePolicies);

  wpe.view = webkit_web_view_get_wpe_view (webkit.view);
  wpe_view_gstreamer_set_client (WPE_VIEW_GSTREAMER (wpe.view), this);
  if (auto wpeToplevel = wpe_view_get_toplevel (wpe.view))
    wpe_toplevel_resize (wpeToplevel, width, height);

  // FIXME: unmap when appropriate and implement can_be_mapped if needed.
  wpe_view_map (wpe.view);

  g_signal_connect (webkit.view, "load-failed", G_CALLBACK (s_loadFailed), src);
  g_signal_connect (webkit.view, "load-failed-with-tls-errors",
      G_CALLBACK (s_loadFailedWithTLSErrors), src);
  g_signal_connect (webkit.view, "notify::estimated-load-progress",
      G_CALLBACK (s_loadProgressChanged), src);
  g_signal_connect (webkit.view, "web-process-terminated",
      G_CALLBACK (s_webProcessCrashed), this);

  auto *settings = webkit_web_view_get_settings (webkit.view);
  webkit_settings_set_enable_webaudio (settings, TRUE);

  gst_wpe_video_src_configure_web_view (src, webkit.view);

  gchar *location;
  gboolean drawBackground = TRUE;
  g_object_get (src, "location", &location, "draw-background", &drawBackground, nullptr);
  setDrawBackground (drawBackground);
  if (location) {
    loadUriUnlocked (location);
    g_free (location);
  }
}
/* *INDENT-ON* */

GstWPEThreadedView::~GstWPEThreadedView ()
{
  GstEGLImage *egl_pending = NULL;
  GstEGLImage *egl_committed = NULL;
  GstBuffer *shm_pending = NULL;
  GstBuffer *shm_committed = NULL;
  GST_TRACE ("%p destroying", this);

  g_mutex_clear (&threading.ready_mutex);
  g_cond_clear (&threading.ready_cond);

  {
    GMutexHolder lock (images_mutex);

    if (egl.pending) {
      egl_pending = egl.pending;
      egl.pending = nullptr;
    }
    if (egl.committed) {
      egl_committed = egl.committed;
      egl.committed = nullptr;
    }
    if (shm.pending) {
      GST_TRACE ("%p freeing shm pending %" GST_PTR_FORMAT, this, shm.pending);
      shm_pending = shm.pending;
      shm.pending = nullptr;
    }
    if (shm.committed) {
      GST_TRACE ("%p freeing shm commited %" GST_PTR_FORMAT, this,
          shm.committed);
      shm_committed = shm.committed;
      shm.committed = nullptr;
    }
  }

  if (egl_pending)
    gst_egl_image_unref (egl_pending);
  if (egl_committed)
    gst_egl_image_unref (egl_committed);
  if (shm_pending)
    gst_buffer_unref (shm_pending);
  if (shm_committed)
    gst_buffer_unref (shm_committed);

  /* *INDENT-OFF* */
  GstWPEContextThread::singleton().dispatch([&]() {
    if (webkit.view) {
      g_object_unref (webkit.view);
      webkit.view = nullptr;
    }
  });
  /* *INDENT-ON* */

  if (gst.display_egl) {
    gst_object_unref (gst.display_egl);
    gst.display_egl = nullptr;
  }

  if (gst.display) {
    gst_object_unref (gst.display);
    gst.display = nullptr;
  }

  if (gst.context) {
    gst_object_unref (gst.context);
    gst.context = nullptr;
  }
  if (webkit.uri) {
    g_free (webkit.uri);
    webkit.uri = nullptr;
  }

  g_mutex_clear (&images_mutex);
  GST_TRACE ("%p destroyed", this);
}

void
GstWPEThreadedView::notifyLoadFinished ()
{
  GMutexHolder lock (threading.ready_mutex);
  if (!threading.ready) {
    threading.ready = TRUE;
    g_cond_signal (&threading.ready_cond);
  }
}

void
GstWPEThreadedView::waitLoadCompletion ()
{
  GMutexHolder lock (threading.ready_mutex);
  while (!threading.ready)
    g_cond_wait (&threading.ready_cond, &threading.ready_mutex);
}

GstEGLImage *
GstWPEThreadedView::image ()
{
  GstEGLImage *ret = nullptr;
  bool dispatchFrameComplete = false;
  GstEGLImage *prev_image = NULL;

  {
    GMutexHolder lock (images_mutex);

    GST_TRACE ("pending %" GST_PTR_FORMAT " (%d) committed %" GST_PTR_FORMAT
        " (%d)", egl.pending,
        GST_IS_EGL_IMAGE (egl.pending) ?
        GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (egl.pending)) : 0,
        egl.committed,
        GST_IS_EGL_IMAGE (egl.committed) ?
        GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (egl.committed)) :
        0);

    if (egl.pending) {
      prev_image = egl.committed;
      egl.committed = egl.pending;
      egl.pending = nullptr;

      dispatchFrameComplete = true;
    }

    if (egl.committed)
      ret = egl.committed;
  }

  if (prev_image) {
    gst_egl_image_unref (prev_image);
  }

  if (dispatchFrameComplete) {
    frameComplete ();
  }

  return ret;
}

GstBuffer *
GstWPEThreadedView::buffer ()
{
  GstBuffer *ret = nullptr;
  bool dispatchFrameComplete = false;
  GstBuffer *prev_image = NULL;

  {
    GMutexHolder lock (images_mutex);

    GST_TRACE ("pending %" GST_PTR_FORMAT " (%d) committed %" GST_PTR_FORMAT
        " (%d)", shm.pending,
        GST_IS_BUFFER (shm.pending) ?
        GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (shm.pending)) : 0,
        shm.committed,
        GST_IS_BUFFER (shm.committed) ?
        GST_MINI_OBJECT_REFCOUNT_VALUE (GST_MINI_OBJECT_CAST (shm.committed)) :
        0);

    if (shm.pending) {
      prev_image = shm.committed;
      shm.committed = shm.pending;
      shm.pending = nullptr;

      dispatchFrameComplete = true;
    }

    if (shm.committed)
      ret = shm.committed;
  }

  if (prev_image)
    gst_buffer_unref (prev_image);

  if (dispatchFrameComplete) {
    frameComplete ();
  }

  return ret;
}

void
GstWPEThreadedView::resize (int width, int height)
{
  GST_DEBUG ("resize to %dx%d", width, height);
  wpe.width = width;
  wpe.height = height;
  if (auto wpeToplevel = wpe_view_get_toplevel (wpe.view))
    wpe_toplevel_resize (wpeToplevel, wpe.width, wpe.height);
}

void
GstWPEThreadedView::clearBuffers ()
{
  bool dispatchFrameComplete = false;
  {
    GMutexHolder lock (images_mutex);

    if (shm.pending) {
      auto meta = gst_buffer_get_video_meta (shm.pending);
      if (static_cast < int >(meta->width) != wpe.width ||
          static_cast < int >(meta->height) != wpe.height) {
        gst_clear_buffer (&shm.pending);
        dispatchFrameComplete = true;
      }
    }

    if (shm.committed) {
      auto meta = gst_buffer_get_video_meta (shm.committed);
      if (static_cast < int >(meta->width) != wpe.width ||
          static_cast < int >(meta->height) != wpe.height) {
        gst_clear_buffer (&shm.committed);
        dispatchFrameComplete = true;
      }
    }
  }

  if (dispatchFrameComplete) {
    frameComplete ();
    // Wait until the next SHM buffer has been received.
    threading.ready = false;
    waitLoadCompletion ();
  }
}

void
GstWPEThreadedView::loadUriUnlocked (const gchar * uri)
{
  if (webkit.uri)
    g_free (webkit.uri);

  GST_DEBUG ("loading %s", uri);
  webkit.uri = g_strdup (uri);
  webkit_web_view_load_uri (webkit.view, webkit.uri);
}

void
GstWPEThreadedView::loadUri (const gchar * uri)
{
  s_view->dispatch ([&]() {
      loadUriUnlocked (uri);});
}

static void
s_runJavascriptFinished (GObject * object, GAsyncResult * result,
    gpointer user_data)
{
  GError *error = NULL;
  g_autoptr (JSCValue) js_result =
      webkit_web_view_evaluate_javascript_finish (WEBKIT_WEB_VIEW (object),
      result, &error);

  // TODO: Pass result back to signal call site using a GstPromise?
  (void) js_result;

  if (error) {
    GST_WARNING ("Error running javascript: %s", error->message);
    g_error_free (error);
  }
}

void
GstWPEThreadedView::runJavascript (const char *script)
{
  /* *INDENT-OFF* */
  s_view->dispatch([&]() {
    webkit_web_view_evaluate_javascript(webkit.view, script, -1, nullptr,
                                        nullptr, nullptr,
                                        s_runJavascriptFinished, nullptr);
  });
  /* *INDENT-ON* */
}

void
GstWPEThreadedView::loadData (GBytes * bytes)
{
  /* *INDENT-OFF* */
  s_view->dispatch([this, bytes = g_bytes_ref(bytes)]() {
    webkit_web_view_load_bytes(webkit.view, bytes, nullptr, nullptr, nullptr);
    g_bytes_unref(bytes);
  });
  /* *INDENT-ON* */
}

void
GstWPEThreadedView::setDrawBackground (gboolean drawsBackground)
{
  GST_DEBUG ("%s background rendering",
      drawsBackground ? "Enabling" : "Disabling");
  WebKitColor color;
  webkit_color_parse (&color, drawsBackground ? "white" : "transparent");
  webkit_web_view_set_background_color (webkit.view, &color);
}

struct WPEBufferContext
{
  GstWPEThreadedView *view;
  WPEBuffer *buffer;
};

void
GstWPEThreadedView::s_releaseBuffer (gpointer data)
{
  /* *INDENT-OFF* */
  s_view->dispatch([&]() {
    WPEBufferContext *context = static_cast<WPEBufferContext *>(data);
    wpe_view_buffer_released(WPE_VIEW(context->view->wpe.view),
                             context->buffer);
    g_object_unref(context->buffer);
    g_free(context);
  });
/* *INDENT-ON* */
}

/* *INDENT-OFF* */
gboolean GstWPEThreadedView::setPendingBuffer(WPEBuffer *buffer, GError **error)
{
  WPEBufferContext *bufferContext = g_new (WPEBufferContext, 1);
  bufferContext->view = this;
  bufferContext->buffer = g_object_ref (buffer);

  if (WPE_IS_BUFFER_DMA_BUF (buffer)) {
    auto eglImage = wpe_buffer_import_to_egl_image (buffer, error);
    if (*error)
      return FALSE;

    auto *gstImage =
        gst_egl_image_new_wrapped (gst.context, eglImage, GST_GL_RGBA,
        bufferContext,[](GstEGLImage *, gpointer data) { s_releaseBuffer (data); });
    {
      GMutexHolder lock (images_mutex);

      GST_TRACE ("EGLImage %p wrapped in GstEGLImage %" GST_PTR_FORMAT,
          eglImage, gstImage);
      gst_clear_mini_object ((GstMiniObject **) & egl.pending);
      egl.pending = gstImage;

      m_pending_buffer = g_object_ref (buffer);
      notifyLoadFinished ();
    }
    return TRUE;
  }

  if (!WPE_IS_BUFFER_SHM (buffer)) {
    g_set_error_literal (error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
                         "Unsupported WPEBuffer format");
    return FALSE;
  }

  GBytes *bytes = wpe_buffer_import_to_pixels (buffer, error);
  if (!bytes) {
    return FALSE;
  }

  auto width = wpe_buffer_get_width (buffer);
  auto height = wpe_buffer_get_height (buffer);

  guint stride;
  g_object_get (buffer, "stride", &stride, nullptr);

  gsize size = g_bytes_get_size (bytes);
  auto *gstBuffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      (gpointer) g_bytes_get_data (bytes, nullptr), size, 0, size,
      bufferContext, s_releaseBuffer);
  gsize offsets[1];
  gint strides[1];
  offsets[0] = 0;
  strides[0] = stride;
  gst_buffer_add_video_meta_full (gstBuffer, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_BGRA, width, height, 1, offsets, strides);

  {
    GMutexHolder lock (images_mutex);
    GST_TRACE ("SHM buffer %p wrapped in buffer %" GST_PTR_FORMAT, buffer,
        gstBuffer);
    gst_clear_buffer (&shm.pending);
    shm.pending = gstBuffer;
    m_pending_buffer = g_object_ref (buffer);
    notifyLoadFinished ();
  }
  return TRUE;
}
/* *INDENT-ON* */

static uint32_t
_pointer_modifiers_from_gst_event (GstEvent * ev)
{
  GstNavigationModifierType modifier_state;
  uint32_t modifiers = 0;

  if (gst_navigation_event_parse_modifier_state (ev, &modifier_state)) {
    if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON1_MASK)
      modifiers |= WPE_MODIFIER_POINTER_BUTTON1;
    if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON2_MASK)
      modifiers |= WPE_MODIFIER_POINTER_BUTTON2;
    if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON3_MASK)
      modifiers |= WPE_MODIFIER_POINTER_BUTTON3;
    if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON4_MASK)
      modifiers |= WPE_MODIFIER_POINTER_BUTTON4;
    if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON5_MASK)
      modifiers |= WPE_MODIFIER_POINTER_BUTTON5;
  }

  return modifiers;
}

static uint32_t
_keyboard_modifiers_from_gst_event (GstEvent * ev)
{
  GstNavigationModifierType modifier_state;
  uint32_t modifiers = 0;

  if (gst_navigation_event_parse_modifier_state (ev, &modifier_state)) {
    if (modifier_state & GST_NAVIGATION_MODIFIER_CONTROL_MASK)
      modifiers |= WPE_MODIFIER_KEYBOARD_CONTROL;
    if (modifier_state & GST_NAVIGATION_MODIFIER_SHIFT_MASK)
      modifiers |= WPE_MODIFIER_KEYBOARD_SHIFT;
    if (modifier_state & GST_NAVIGATION_MODIFIER_MOD1_MASK)
      modifiers |= WPE_MODIFIER_KEYBOARD_ALT;
    if (modifier_state & GST_NAVIGATION_MODIFIER_META_MASK)
      modifiers |= WPE_MODIFIER_KEYBOARD_META;
  }

  return modifiers;
}

static WPEModifiers
modifiers_from_gst_event (GstEvent * event)
{
  /* *INDENT-OFF* */
  return static_cast<WPEModifiers>
      (_pointer_modifiers_from_gst_event (event) |
      _keyboard_modifiers_from_gst_event (event));
  /* *INDENT-ON* */
}

void
GstWPEThreadedView::frameComplete ()
{
  GST_TRACE ("frame complete");
  /* *INDENT-OFF* */
  s_view->dispatch([&]() {
    if (m_committed_buffer) {
      wpe_view_buffer_released(WPE_VIEW(wpe.view), m_committed_buffer);
      g_object_unref(m_committed_buffer);
    }
    m_committed_buffer = m_pending_buffer;
    wpe_view_buffer_rendered (WPE_VIEW (wpe.view), m_committed_buffer);
  });
  /* *INDENT-ON* */
}

void
GstWPEThreadedView::dispatchEvent (WPEEvent * wpe_event)
{
  /* *INDENT-OFF* */
  s_view->dispatch([&]() {
    wpe_view_event(WPE_VIEW(wpe.view), wpe_event);
    wpe_event_unref(wpe_event);
  });
  /* *INDENT-ON* */
}

/* *INDENT-OFF* */
gboolean GstWPEThreadedView::dispatchKeyboardEvent(GstEvent *event) {
  const gchar *key;
  if (!gst_navigation_event_parse_key_event (event, &key)) {
    return FALSE;
  }

  auto modifiers = static_cast<WPEModifiers>(_keyboard_modifiers_from_gst_event (event));
  auto timestamp = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));

  /* FIXME: This is wrong... The GstNavigation API should pass
     hardware-level information, not high-level keysym strings */
  gunichar *unichar;
  glong items_written;
  uint32_t keysym;

  unichar = g_utf8_to_ucs4_fast (key, -1, &items_written);
  if (items_written == 1)
    keysym = (uint32_t) xkb_utf32_to_keysym (*unichar);
  else
    keysym = (uint32_t) xkb_keysym_from_name (key, XKB_KEYSYM_NO_FLAGS);

  WPEEventType event_type = WPE_EVENT_NONE;
  if (gst_navigation_event_get_type (event) == GST_NAVIGATION_EVENT_KEY_PRESS)
    event_type = WPE_EVENT_KEYBOARD_KEY_DOWN;
  else
    event_type = WPE_EVENT_KEYBOARD_KEY_UP;

  dispatchEvent (wpe_event_keyboard_new (event_type, WPE_VIEW (wpe.view),
          WPE_INPUT_SOURCE_KEYBOARD, timestamp, modifiers, keysym, keysym));
  return TRUE;
}

gboolean GstWPEThreadedView::dispatchPointerEvent (GstEvent * event)
{
  gdouble x, y;
  gint button;
  if (!gst_navigation_event_parse_mouse_button_event (event, &button, &x, &y)) {
    return FALSE;
  }

  GstNavigationModifierType modifier_state;
  guint wpe_button = 0;
  if (gst_navigation_event_parse_modifier_state (event, &modifier_state)) {
    if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON1_MASK)
      wpe_button = WPE_BUTTON_PRIMARY;
    else if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON2_MASK)
      wpe_button = WPE_BUTTON_MIDDLE;
    else if (modifier_state & GST_NAVIGATION_MODIFIER_BUTTON3_MASK)
      wpe_button = WPE_BUTTON_SECONDARY;
  }

  auto timestamp = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));
  guint press_count = 0;
  WPEEventType type;
  if (gst_navigation_event_get_type (event) ==
      GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS) {
    press_count = wpe_view_compute_press_count (WPE_VIEW (wpe.view), x, y,
        wpe_button, timestamp);
    type = WPE_EVENT_POINTER_DOWN;
  } else {
    type = WPE_EVENT_POINTER_UP;
  }
  dispatchEvent (wpe_event_pointer_button_new (type, WPE_VIEW (wpe.view),
          WPE_INPUT_SOURCE_MOUSE, timestamp, modifiers_from_gst_event (event),
          wpe_button, x, y, press_count));
  return TRUE;
}

gboolean GstWPEThreadedView::dispatchPointerMoveEvent (GstEvent * event)
{
  gdouble x, y;
  if (!gst_navigation_event_parse_mouse_move_event (event, &x, &y)) {
    return FALSE;
  }

  gdouble delta_x = 0;
  gdouble delta_y = 0;
  if (m_last_pointer_position) {
    delta_x = x - m_last_pointer_position->first;
    delta_y = y - m_last_pointer_position->second;
  }
  m_last_pointer_position = { x, y };

  auto timestamp = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));
  dispatchEvent (wpe_event_pointer_move_new (WPE_EVENT_POINTER_MOVE,
          WPE_VIEW (wpe.view), WPE_INPUT_SOURCE_MOUSE, timestamp,
          modifiers_from_gst_event (event), x, y, delta_x, delta_y));
  return TRUE;
}

gboolean GstWPEThreadedView::dispatchAxisEvent (GstEvent * event)
{
  gdouble x, y, delta_x, delta_y;
  if (!gst_navigation_event_parse_mouse_scroll_event (event, &x, &y, &delta_x,
          &delta_y)) {
    return FALSE;
  }

  auto timestamp = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));
  dispatchEvent (wpe_event_scroll_new (WPE_VIEW (wpe.view),
          WPE_INPUT_SOURCE_MOUSE, timestamp, modifiers_from_gst_event (event),
          delta_x, delta_y, TRUE, FALSE, x, y));

  return TRUE;
}

gboolean GstWPEThreadedView::dispatchTouchEvent (GstEvent * event)
{
  guint touch_id;
  gdouble x, y;
  if (!gst_navigation_event_parse_touch_event (event, &touch_id, &x, &y, NULL)) {
    return FALSE;
  }

  WPEEventType event_type = WPE_EVENT_NONE;
  switch (gst_navigation_event_get_type (event)) {
    case GST_NAVIGATION_EVENT_TOUCH_DOWN:
      event_type = WPE_EVENT_TOUCH_DOWN;
      break;
    case GST_NAVIGATION_EVENT_TOUCH_MOTION:
      event_type = WPE_EVENT_TOUCH_MOVE;
      break;
    case GST_NAVIGATION_EVENT_TOUCH_UP:
      event_type = WPE_EVENT_TOUCH_UP;
      break;
    default:
      break;
  }

  auto timestamp = GST_TIME_AS_MSECONDS (GST_EVENT_TIMESTAMP (event));
  auto modifiers = static_cast<WPEModifiers>(_keyboard_modifiers_from_gst_event (event));
  dispatchEvent (wpe_event_touch_new (event_type, WPE_VIEW (wpe.view),
          WPE_INPUT_SOURCE_TOUCHPAD, timestamp, modifiers, touch_id, x, y));
  return TRUE;
}
/* *INDENT-ON* */
