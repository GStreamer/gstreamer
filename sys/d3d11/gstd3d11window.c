/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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
#include "config.h"
#endif

#include "gstd3d11window.h"
#include "gstd3d11device.h"

#include <windows.h>

G_LOCK_DEFINE_STATIC (create_lock);

enum
{
  PROP_0,
  PROP_D3D11_DEVICE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS,
};

#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE

enum
{
  SIGNAL_KEY_EVENT,
  SIGNAL_MOUSE_EVENT,
  SIGNAL_GOT_WINDOW_HANDLE,
  SIGNAL_LAST
};

static guint d3d11_window_signals[SIGNAL_LAST] = { 0, };

#define EXTERNAL_PROC_PROP_NAME "d3d11_window_external_proc"
#define D3D11_WINDOW_PROP_NAME "gst_d3d11_window_object"

static LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);
static LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_window_debug);
#define GST_CAT_DEFAULT gst_d3d11_window_debug

#define gst_d3d11_window_parent_class parent_class
G_DEFINE_TYPE (GstD3D11Window, gst_d3d11_window, GST_TYPE_OBJECT);

static void gst_d3d11_window_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_window_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d11_window_dispose (GObject * object);
static void gst_d3d11_window_finalize (GObject * object);
static gpointer gst_d3d11_window_thread_func (gpointer data);
static gboolean _create_window (GstD3D11Window * self, GError ** error);
static void _open_window (GstD3D11Window * self);
static void _close_window (GstD3D11Window * self);
static void release_external_win_id (GstD3D11Window * self);

static void
gst_d3d11_window_class_init (GstD3D11WindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_window_set_property;
  gobject_class->get_property = gst_d3d11_window_get_property;
  gobject_class->dispose = gst_d3d11_window_dispose;
  gobject_class->finalize = gst_d3d11_window_finalize;

  g_object_class_install_property (gobject_class, PROP_D3D11_DEVICE,
      g_param_spec_object ("d3d11device", "D3D11 Device",
          "GstD3D11Device object for creating swapchain",
          GST_TYPE_D3D11_DEVICE,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLE_NAVIGATION_EVENTS,
      g_param_spec_boolean ("enable-navigation-events",
          "Enable navigation events",
          "When enabled, signals for navigation events are emitted",
          DEFAULT_ENABLE_NAVIGATION_EVENTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  d3d11_window_signals[SIGNAL_KEY_EVENT] =
      g_signal_new ("key-event", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  d3d11_window_signals[SIGNAL_MOUSE_EVENT] =
      g_signal_new ("mouse-event", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE);

  d3d11_window_signals[SIGNAL_GOT_WINDOW_HANDLE] =
      g_signal_new ("got-window-handle", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_window_debug, "d3d11window", 0,
      "d3d11 window");
}

static void
gst_d3d11_window_init (GstD3D11Window * self)
{
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  self->main_context = g_main_context_new ();

  self->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  self->enable_navigation_events = DEFAULT_ENABLE_NAVIGATION_EVENTS;

  self->aspect_ratio_n = 1;
  self->aspect_ratio_d = 1;

  GST_TRACE_OBJECT (self, "Initialized");
}

static void
gst_d3d11_window_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_D3D11_DEVICE:
      self->device = g_value_dup_object (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
    {
      gboolean force_aspect_ratio;

      force_aspect_ratio = g_value_get_boolean (value);
      if (force_aspect_ratio != self->force_aspect_ratio)
        self->pending_resize = TRUE;

      self->force_aspect_ratio = force_aspect_ratio;
      break;
    }
    case PROP_ENABLE_NAVIGATION_EVENTS:
      self->enable_navigation_events = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_d3d11_window_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  switch (prop_id) {
    case PROP_ENABLE_NAVIGATION_EVENTS:
      g_value_set_boolean (value, self->enable_navigation_events);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, self->force_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_window_release_resources (GstD3D11Device * device,
    GstD3D11Window * window)
{
  if (window->backbuffer) {
    ID3D11Texture2D_Release (window->backbuffer);
    window->backbuffer = NULL;
  }

  if (window->rtv) {
    ID3D11RenderTargetView_Release (window->rtv);
    window->rtv = NULL;
  }

  if (window->swap_chain) {
    IDXGISwapChain_Release (window->swap_chain);
    window->swap_chain = NULL;
  }
}

static void
gst_d3d11_window_dispose (GObject * object)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  release_external_win_id (self);

  if (self->loop) {
    g_main_loop_quit (self->loop);
  }

  if (self->thread) {
    g_thread_join (self->thread);
    self->thread = NULL;
  }

  if (self->loop) {
    g_main_loop_unref (self->loop);
    self->loop = NULL;
  }

  if (self->main_context) {
    g_main_context_unref (self->main_context);
    self->main_context = NULL;
  }

  if (self->device) {
    gst_d3d11_device_thread_add (self->device,
        (GstD3D11DeviceThreadFunc) gst_d3d11_window_release_resources, self);
  }

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_window_finalize (GObject * object)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
running_cb (gpointer user_data)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (user_data);

  GST_TRACE_OBJECT (self, "Main loop running now");

  g_mutex_lock (&self->lock);
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
}

typedef struct
{
  GstD3D11Window *self;
  GError **error;
} GstD3D11ThreadFuncData;

static gpointer
gst_d3d11_window_thread_func (gpointer data)
{
  GstD3D11ThreadFuncData *func_data = (GstD3D11ThreadFuncData *) data;
  GstD3D11Window *self = func_data->self;
  GSource *source;

  GST_DEBUG_OBJECT (self, "Enter loop");
  g_main_context_push_thread_default (self->main_context);

  self->created = _create_window (self, func_data->error);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) running_cb, self, NULL);
  g_source_attach (source, self->main_context);
  g_source_unref (source);

  if (self->created)
    _open_window (self);

  g_main_loop_run (self->loop);

  if (self->created)
    _close_window (self);

  g_main_context_pop_thread_default (self->main_context);

  GST_DEBUG_OBJECT (self, "Exit loop");

  return NULL;
}

static gboolean
msg_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  MSG msg;

  if (!PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  TranslateMessage (&msg);
  DispatchMessage (&msg);

  return G_SOURCE_CONTINUE;
}

static void
_open_window (GstD3D11Window * self)
{
  self->msg_io_channel = g_io_channel_win32_new_messages (0);
  self->msg_source = g_io_create_watch (self->msg_io_channel, G_IO_IN);
  g_source_set_callback (self->msg_source, (GSourceFunc) msg_cb, self, NULL);
  g_source_attach (self->msg_source, self->main_context);
}

static void
_close_window (GstD3D11Window * self)
{
  if (self->internal_win_id) {
    RemoveProp (self->internal_win_id, D3D11_WINDOW_PROP_NAME);
    ShowWindow (self->internal_win_id, SW_HIDE);
    SetParent (self->internal_win_id, NULL);
    if (!DestroyWindow (self->internal_win_id))
      GST_WARNING ("failed to destroy window %" G_GUINTPTR_FORMAT
          ", 0x%x", (guintptr) self->internal_win_id, (guint) GetLastError ());
    self->internal_win_id = NULL;
  }

  if (self->msg_source) {
    g_source_destroy (self->msg_source);
    g_source_unref (self->msg_source);
    self->msg_source = NULL;
  }

  if (self->msg_io_channel) {
    g_io_channel_unref (self->msg_io_channel);
    self->msg_io_channel = NULL;
  }
}

static void
set_external_win_id (GstD3D11Window * self)
{
  WNDPROC external_window_proc;
  if (!self->external_win_id)
    return;

  external_window_proc =
      (WNDPROC) GetWindowLongPtr (self->external_win_id, GWLP_WNDPROC);

  GST_DEBUG ("set external window %" G_GUINTPTR_FORMAT,
      (guintptr) self->external_win_id);

  SetProp (self->external_win_id, EXTERNAL_PROC_PROP_NAME,
      (WNDPROC) external_window_proc);
  SetProp (self->external_win_id, D3D11_WINDOW_PROP_NAME, self);
  SetWindowLongPtr (self->external_win_id, GWLP_WNDPROC,
      (LONG_PTR) sub_class_proc);
}

static void
release_external_win_id (GstD3D11Window * self)
{
  WNDPROC external_proc;

  if (!self->external_win_id)
    return;

  external_proc = GetProp (self->external_win_id, EXTERNAL_PROC_PROP_NAME);
  if (!external_proc)
    return;

  GST_DEBUG ("release external window %" G_GUINTPTR_FORMAT,
      (guintptr) self->external_win_id);

  SetWindowLongPtr (self->external_win_id,
      GWLP_WNDPROC, (LONG_PTR) external_proc);

  RemoveProp (self->external_win_id, EXTERNAL_PROC_PROP_NAME);
  RemoveProp (self->external_win_id, D3D11_WINDOW_PROP_NAME);
  self->external_win_id = NULL;
}

static gboolean
_create_window (GstD3D11Window * self, GError ** error)
{
  WNDCLASSEX wc;
  ATOM atom = 0;
  HINSTANCE hinstance = GetModuleHandle (NULL);

  GST_LOG_OBJECT (self, "Attempting to create a win32 window");

  G_LOCK (create_lock);
  atom = GetClassInfoEx (hinstance, "GSTD3D11", &wc);
  if (atom == 0) {
    GST_LOG_OBJECT (self, "Register internal window class");
    ZeroMemory (&wc, sizeof (WNDCLASSEX));

    wc.cbSize = sizeof (WNDCLASSEX);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hinstance;
    wc.hIcon = LoadIcon (NULL, IDI_WINLOGO);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
    wc.lpszClassName = "GSTD3D11";

    atom = RegisterClassEx (&wc);

    if (atom == 0) {
      G_UNLOCK (create_lock);
      GST_ERROR_OBJECT (self, "Failed to register window class 0x%x",
          (unsigned int) GetLastError ());
      g_set_error (error, GST_RESOURCE_ERROR,
          GST_RESOURCE_ERROR_FAILED, "Failed to register window class 0x%x",
          (unsigned int) GetLastError ());
      return FALSE;
    }
  } else {
    GST_LOG_OBJECT (self, "window class was already registered");
  }

  self->device_handle = 0;
  self->internal_win_id = 0;
  self->visible = FALSE;

  self->internal_win_id = CreateWindowEx (0,
      "GSTD3D11",
      "Direct3D11 renderer",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT,
      0, 0, (HWND) NULL, (HMENU) NULL, hinstance, self);

  G_UNLOCK (create_lock);

  if (!self->internal_win_id) {
    GST_ERROR_OBJECT (self, "Failed to create d3d11 window");
    g_set_error (error, GST_RESOURCE_ERROR,
        GST_RESOURCE_ERROR_FAILED, "Failed to create d3d11 window");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "d3d11 window created: %" G_GUINTPTR_FORMAT,
      (guintptr) self->internal_win_id);

  g_signal_emit (self,
      d3d11_window_signals[SIGNAL_GOT_WINDOW_HANDLE], 0, self->internal_win_id);

  /* device_handle is set in the window_proc */
  if (!self->device_handle) {
    GST_ERROR_OBJECT (self, "device handle is not available");
    g_set_error (error, GST_RESOURCE_ERROR,
        GST_RESOURCE_ERROR_FAILED, "device handle is not available");
    return FALSE;
  }

  GST_LOG_OBJECT (self,
      "Created a internal d3d11 window %p", self->internal_win_id);

  return TRUE;
}

static void
gst_d3d11_window_on_resize (GstD3D11Device * device, GstD3D11Window * window)
{
  HRESULT hr;
  ID3D11Device *d3d11_dev;
  ID3D11DeviceContext *d3d11_context;
  guint width, height;

  if (!window->swap_chain)
    return;

  d3d11_dev = gst_d3d11_device_get_device (device);
  d3d11_context = gst_d3d11_device_get_device_context (device);

  if (window->backbuffer) {
    ID3D11Texture2D_Release (window->backbuffer);
    window->backbuffer = NULL;
  }

  if (window->rtv) {
    ID3D11RenderTargetView_Release (window->rtv);
    window->rtv = NULL;
  }

  /* NOTE: there can be various way to resize texture, but
   * we just copy incoming texture toward resized swap chain buffer in order to
   * avoid shader coding.
   * To keep aspect ratio, required vertical or horizontal padding area
   * will be calculated in here.
   */
  width = window->width;
  height = window->height;

  if (width != window->surface_width || height != window->surface_height) {
    GstVideoRectangle src_rect, dst_rect;
    gdouble src_ratio, dst_ratio;
    gdouble aspect_ratio =
        (gdouble) window->aspect_ratio_n / (gdouble) window->aspect_ratio_d;

    src_ratio = (gdouble) width / height;
    dst_ratio =
        (gdouble) window->surface_width / window->surface_height / aspect_ratio;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.w = width;
    src_rect.h = height;

    dst_rect.x = 0;
    dst_rect.y = 0;

    if (window->force_aspect_ratio) {
      if (src_ratio > dst_ratio) {
        /* padding top and bottom */
        dst_rect.w = width;
        dst_rect.h = width / dst_ratio;
      } else {
        /* padding left and right */
        dst_rect.w = height * dst_ratio;
        dst_rect.h = height;
      }
    } else {
      dst_rect.w = width;
      dst_rect.h = height;
    }

    gst_video_sink_center_rect (src_rect, dst_rect, &window->render_rect, TRUE);

    width = dst_rect.w;
    height = dst_rect.h;
  }

  hr = IDXGISwapChain_ResizeBuffers (window->swap_chain,
      0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (window, "Couldn't resize buffers, hr: 0x%x", (guint) hr);
    return;
  }

  hr = IDXGISwapChain_GetBuffer (window->swap_chain,
      0, &IID_ID3D11Texture2D, (void **) &window->backbuffer);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (window,
        "Cannot get backbuffer from swapchain, hr: 0x%x", (guint) hr);
    return;
  }

  hr = ID3D11Device_CreateRenderTargetView (d3d11_dev,
      (ID3D11Resource *) window->backbuffer, NULL, &window->rtv);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (window, "Cannot create render target view, hr: 0x%x",
        (guint) hr);
    return;
  }

  ID3D11DeviceContext_OMSetRenderTargets (d3d11_context, 1, &window->rtv, NULL);
}

static void
gst_d3d11_window_on_size (GstD3D11Window * self,
    HWND hWnd, WPARAM wParam, LPARAM lParam)
{
  RECT clientRect = { 0, };

  GetClientRect (hWnd, &clientRect);

  self->surface_width = clientRect.right - clientRect.left;
  self->surface_height = clientRect.bottom - clientRect.top;

  GST_LOG_OBJECT (self, "WM_PAINT, surface %ux%u",
      self->surface_width, self->surface_height);

  gst_d3d11_device_thread_add (self->device,
      (GstD3D11DeviceThreadFunc) gst_d3d11_window_on_resize, self);
}

static void
gst_d3d11_window_on_keyboard_event (GstD3D11Window * self,
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  gunichar2 wcrep[128];
  const gchar *event;

  if (!self->enable_navigation_events)
    return;

  if (GetKeyNameTextW (lParam, (LPWSTR) wcrep, 128)) {
    gchar *utfrep = g_utf16_to_utf8 (wcrep, 128, NULL, NULL, NULL);
    if (utfrep) {
      if (uMsg == WM_KEYDOWN)
        event = "key-press";
      else
        event = "key-release";

      g_signal_emit (self, d3d11_window_signals[SIGNAL_KEY_EVENT], 0,
          event, utfrep);
      g_free (utfrep);
    }
  }
}

static void
gst_d3d11_window_on_mouse_event (GstD3D11Window * self,
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  gint button;
  const gchar *event = NULL;

  if (!self->enable_navigation_events)
    return;

  /* FIXME: convert to render coordinate */
  switch (uMsg) {
    case WM_MOUSEMOVE:
      button = 0;
      event = "mouse-move";
      break;
    case WM_LBUTTONDOWN:
      button = 1;
      event = "mouse-button-press";
      break;
    case WM_LBUTTONUP:
      button = 1;
      event = "mouse-button-release";
      break;
    case WM_RBUTTONDOWN:
      button = 2;
      event = "mouse-button-press";
      break;
    case WM_RBUTTONUP:
      button = 2;
      event = "mouse-button-release";
      break;
    case WM_MBUTTONDOWN:
      button = 3;
      event = "mouse-button-press";
      break;
    case WM_MBUTTONUP:
      button = 3;
      event = "mouse-button-release";
      break;
    default:
      break;
  }

  if (event)
    g_signal_emit (self, d3d11_window_signals[SIGNAL_MOUSE_EVENT], 0,
        event, button, (gdouble) LOWORD (lParam), (gdouble) HIWORD (lParam));
}

static void
gst_d3d11_window_handle_window_proc (GstD3D11Window * self,
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {
    case WM_SIZE:
      gst_d3d11_window_on_size (self, hWnd, wParam, lParam);
      break;
    case WM_CLOSE:
      if (self->internal_win_id) {
        ShowWindow (self->internal_win_id, SW_HIDE);
        _close_window (self);
      }
      break;
    case WM_KEYDOWN:
    case WM_KEYUP:
      gst_d3d11_window_on_keyboard_event (self, hWnd, uMsg, wParam, lParam);
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
      gst_d3d11_window_on_mouse_event (self, hWnd, uMsg, wParam, lParam);
      break;
    default:
      break;
  }

  return;
}

static LRESULT CALLBACK
window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  GstD3D11Window *self;

  if (uMsg == WM_CREATE) {
    self = GST_D3D11_WINDOW (((LPCREATESTRUCT) lParam)->lpCreateParams);

    GST_LOG_OBJECT (self, "WM_CREATE");

    self->device_handle = GetDC (hWnd);
    /* Do this, otherwise we hang on exit. We can still use it (due to the
     * CS_OWNDC flag in the WindowClass) after we have Released.
     */
    ReleaseDC (hWnd, self->device_handle);

    SetProp (hWnd, D3D11_WINDOW_PROP_NAME, self);
  } else if (GetProp (hWnd, D3D11_WINDOW_PROP_NAME)) {
    self = GST_D3D11_WINDOW (GetProp (hWnd, D3D11_WINDOW_PROP_NAME));

    g_assert (self->internal_win_id == hWnd);

    gst_d3d11_window_handle_window_proc (self, hWnd, uMsg, wParam, lParam);
  }

  return DefWindowProc (hWnd, uMsg, wParam, lParam);
}

static LRESULT FAR PASCAL
sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC external_window_proc = GetProp (hWnd, EXTERNAL_PROC_PROP_NAME);
  GstD3D11Window *self =
      (GstD3D11Window *) GetProp (hWnd, D3D11_WINDOW_PROP_NAME);
  LRESULT ret = 0;

  gst_d3d11_window_handle_window_proc (self, hWnd, uMsg, wParam, lParam);

  ret = CallWindowProc (external_window_proc, hWnd, uMsg, wParam, lParam);

  if (uMsg == WM_CLOSE)
    release_external_win_id (self);

  return ret;
}

GstD3D11Window *
gst_d3d11_window_new (GstD3D11Device * device)
{
  GstD3D11Window *window;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  window = g_object_new (GST_TYPE_D3D11_WINDOW, "d3d11device", device, NULL);
  g_object_ref_sink (window);

  return window;
}

#ifdef HAVE_DXGI_1_5_H
static inline UINT16
fraction_to_uint (guint num, guint den, guint scale)
{
  gdouble val;
  gst_util_fraction_to_double (num, den, &val);

  return (UINT16) val *scale;
}

static void
mastering_display_gst_to_dxgi (GstVideoMasteringDisplayInfo * m,
    GstVideoContentLightLevel * c, DXGI_HDR_METADATA_HDR10 * meta)
{
  meta->RedPrimary[0] = fraction_to_uint (m->Rx_n, m->Rx_d, 50000);
  meta->RedPrimary[1] = fraction_to_uint (m->Ry_n, m->Ry_d, 50000);
  meta->GreenPrimary[0] = fraction_to_uint (m->Gx_n, m->Gx_d, 50000);
  meta->GreenPrimary[1] = fraction_to_uint (m->Gy_n, m->Gy_d, 50000);
  meta->BluePrimary[0] = fraction_to_uint (m->Bx_n, m->Bx_d, 50000);
  meta->BluePrimary[1] = fraction_to_uint (m->By_n, m->By_d, 50000);
  meta->WhitePoint[0] = fraction_to_uint (m->Wx_n, m->Wx_d, 50000);
  meta->WhitePoint[1] = fraction_to_uint (m->Wy_n, m->Wy_d, 50000);
  meta->MaxMasteringLuminance =
      fraction_to_uint (m->max_luma_n, m->max_luma_d, 1);
  meta->MinMasteringLuminance =
      fraction_to_uint (m->min_luma_n, m->min_luma_d, 1);
  meta->MaxContentLightLevel = fraction_to_uint (c->maxCLL_n, c->maxCLL_d, 1);
  meta->MaxFrameAverageLightLevel =
      fraction_to_uint (c->maxFALL_n, c->maxFALL_d, 1);
}
#endif

gboolean
gst_d3d11_window_prepare (GstD3D11Window * window, guint width, guint height,
    guint aspect_ratio_n, guint aspect_ratio_d, DXGI_FORMAT format,
    GstCaps * caps, GError ** error)
{
  DXGI_SWAP_CHAIN_DESC desc = { 0, };
  gboolean have_cll = FALSE;
  gboolean have_mastering = FALSE;
  gboolean hdr_api_available = FALSE;
  GstD3D11ThreadFuncData data;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);
  g_return_val_if_fail (aspect_ratio_n > 0, FALSE);
  g_return_val_if_fail (aspect_ratio_d > 0, FALSE);

  GST_DEBUG_OBJECT (window, "Prepare window with %dx%d format %d",
      width, height, format);

  data.self = window;
  data.error = error;

  g_mutex_lock (&window->lock);
  if (!window->external_win_id && !window->created) {
    window->loop = g_main_loop_new (window->main_context, FALSE);
    window->thread = g_thread_new ("GstD3D11Window",
        (GThreadFunc) gst_d3d11_window_thread_func, &data);
    while (!g_main_loop_is_running (window->loop))
      g_cond_wait (&window->cond, &window->lock);
  }
  g_mutex_unlock (&window->lock);

  if (!window->external_win_id && !window->created) {
    g_main_loop_quit (window->loop);
    g_thread_join (window->thread);
    g_main_loop_unref (window->loop);
    window->loop = NULL;
    window->thread = NULL;
  }

  gst_video_info_from_caps (&window->info, caps);
  if (!gst_video_content_light_level_from_caps (&window->content_light_level,
          caps)) {
    gst_video_content_light_level_init (&window->content_light_level);
  } else {
    have_cll = TRUE;
  }

  if (!gst_video_mastering_display_info_from_caps
      (&window->mastering_display_info, caps)) {
    gst_video_mastering_display_info_init (&window->mastering_display_info);
  } else {
    have_mastering = TRUE;
  }

#ifdef HAVE_DXGI_1_5_H
  if (gst_d3d11_device_get_chosen_dxgi_factory_version (window->device) >=
      GST_D3D11_DXGI_FACTORY_5) {
    GST_DEBUG_OBJECT (window, "DXGI 1.5 interface is available");
    hdr_api_available = TRUE;
  }
#endif

  window->aspect_ratio_n = aspect_ratio_n;
  window->aspect_ratio_d = aspect_ratio_d;

  window->render_rect.x = 0;
  window->render_rect.y = 0;
  window->render_rect.w = width;
  window->render_rect.h = height;

  desc.BufferDesc.Width = window->width = window->surface_width = width;
  desc.BufferDesc.Height = window->height = window->surface_height = height;
  /* don't care refresh rate */
  desc.BufferDesc.RefreshRate.Numerator = 0;
  desc.BufferDesc.RefreshRate.Denominator = 1;
  desc.BufferDesc.Format = format;
  desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
#ifdef HAVE_DXGI_1_5_H
  /* For non-DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 color space support,
   * DXGI_SWAP_EFFECT_FLIP_DISCARD instead of DXGI_SWAP_EFFECT_DISCARD */
  if (hdr_api_available)
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
#endif
  desc.OutputWindow =
      window->external_win_id ? window->external_win_id : window->
      internal_win_id;
  desc.Windowed = TRUE;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  window->swap_chain =
      gst_d3d11_device_create_swap_chain (window->device, &desc);

  if (!window->swap_chain) {
    GST_ERROR_OBJECT (window, "Cannot create swapchain");
    return FALSE;
  }
#ifdef HAVE_DXGI_1_5_H
  if (hdr_api_available && format == DXGI_FORMAT_R10G10B10A2_UNORM &&
      have_cll && have_mastering) {
    UINT can_support = 0;
    HRESULT hr;

    hr = IDXGISwapChain4_CheckColorSpaceSupport ((IDXGISwapChain4 *)
        window->swap_chain, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
        &can_support);

    if (SUCCEEDED (hr) &&
        (can_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ==
        DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) {
      DXGI_HDR_METADATA_HDR10 metadata = { 0, };

      GST_DEBUG_OBJECT (window,
          "Swapchain support BT2084 color space, set HDR metadata");

      mastering_display_gst_to_dxgi (&window->mastering_display_info,
          &window->content_light_level, &metadata);

      hr = IDXGISwapChain4_SetColorSpace1 ((IDXGISwapChain4 *)
          window->swap_chain, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);

      if (SUCCEEDED (hr)) {
        hr = IDXGISwapChain4_SetHDRMetaData ((IDXGISwapChain4 *)
            window->swap_chain, DXGI_HDR_METADATA_TYPE_HDR10,
            sizeof (DXGI_HDR_METADATA_HDR10), &metadata);
        if (FAILED (hr)) {
          GST_WARNING_OBJECT (window, "Couldn't set HDR metadata, hr 0x%x",
              (guint) hr);
        }
      } else {
        GST_WARNING_OBJECT (window, "Couldn't set colorspace, hr 0x%x",
            (guint) hr);
      }
    } else {
      GST_DEBUG_OBJECT (window,
          "Swapchain couldn't support BT2084 color space, hr 0x%x", (guint) hr);
    }
  }
#endif

  gst_d3d11_device_thread_add (window->device,
      (GstD3D11DeviceThreadFunc) gst_d3d11_window_on_resize, window);

  if (!window->rtv) {
    gst_d3d11_device_thread_add (window->device,
        (GstD3D11DeviceThreadFunc) gst_d3d11_window_release_resources, window);
    return FALSE;
  }

  GST_DEBUG_OBJECT (window, "New swap chain 0x%p created", window->swap_chain);

  return TRUE;
}

void
gst_d3d11_window_set_window_handle (GstD3D11Window * window, guintptr id)
{
  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  if (window->visible) {
    ShowWindow (window->internal_win_id, SW_HIDE);
    window->visible = FALSE;
  }

  release_external_win_id (window);
  window->external_win_id = (HWND) id;
  set_external_win_id (window);
}

void
gst_d3d11_window_show (GstD3D11Window * window)
{
  gint width, height;

  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  width = window->width;
  height = window->height;

  if (!window->visible) {
    /* if no parent the real size has to be set now because this has not been done
     * when at window creation */
    if (!window->external_win_id) {
      RECT rect;
      GetClientRect (window->internal_win_id, &rect);
      width += 2 * GetSystemMetrics (SM_CXSIZEFRAME);
      height +=
          2 * GetSystemMetrics (SM_CYSIZEFRAME) +
          GetSystemMetrics (SM_CYCAPTION);
      MoveWindow (window->internal_win_id, rect.left, rect.top, width,
          height, FALSE);
    }

    ShowWindow (window->internal_win_id, SW_SHOW);
    window->visible = TRUE;
  }
}

void
gst_d3d11_window_set_render_rectangle (GstD3D11Window * window, gint x, gint y,
    gint width, gint height)
{
  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  if (x < 0 || y < 0) {
    x = y = 0;
    width = window->surface_width;
    height = window->surface_height;
  }

  if (x < 0 || y < 0 || width <= 0 || height <= 0)
    return;

  /* TODO: resize window and view */
}

void
gst_d3d11_window_get_surface_dimensions (GstD3D11Window * window,
    guint * width, guint * height)
{
  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  if (width)
    *width = window->surface_width;
  if (height)
    *height = window->surface_height;
}

typedef struct
{
  GstD3D11Window *window;
  ID3D11Resource *resource;
  GstVideoRectangle *rect;

  GstFlowReturn ret;
} FramePresentData;

static void
_present_on_device_thread (GstD3D11Device * device, FramePresentData * data)
{
  GstD3D11Window *self = data->window;
  ID3D11DeviceContext *device_context;
  HRESULT hr;
  float black[] = { 0.0f, 0.0f, 0.0f, 0.0f };
  D3D11_BOX src_box;

  src_box.left = data->rect->x;
  src_box.right = data->rect->x + data->rect->w;
  src_box.top = data->rect->y;
  src_box.bottom = data->rect->y + data->rect->h;
  src_box.front = 0;
  src_box.back = 1;

  device_context = gst_d3d11_device_get_device_context (device);

  if (data->resource) {
    ID3D11DeviceContext_ClearRenderTargetView (device_context, self->rtv,
        black);
    ID3D11DeviceContext_CopySubresourceRegion (device_context,
        (ID3D11Resource *) self->backbuffer, 0, self->render_rect.x,
        self->render_rect.y, 0, data->resource, 0, &src_box);
  }

  hr = IDXGISwapChain_Present (self->swap_chain, 0, DXGI_PRESENT_DO_NOT_WAIT);

  if (FAILED (hr)) {
    GST_WARNING_OBJECT (self, "Direct3D cannot present texture, hr: 0x%x",
        (guint) hr);
  }

  data->ret = GST_FLOW_OK;
}

GstFlowReturn
gst_d3d11_window_render (GstD3D11Window * window, ID3D11Texture2D * texture,
    GstVideoRectangle * rect)
{
  FramePresentData data;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), GST_FLOW_ERROR);
  g_return_val_if_fail (rect != NULL, GST_FLOW_ERROR);

  if (!window->external_win_id && !window->internal_win_id) {
    GST_ERROR_OBJECT (window, "Output window was closed");
    return GST_D3D11_WINDOW_FLOW_CLOSED;
  }

  GST_OBJECT_LOCK (window);
  if (rect->w != window->width || rect->h != window->height ||
      window->pending_resize) {
    window->width = rect->w;
    window->height = rect->h;

    gst_d3d11_device_thread_add (window->device,
        (GstD3D11DeviceThreadFunc) gst_d3d11_window_on_resize, window);
  }
  GST_OBJECT_UNLOCK (window);

  data.window = window;
  data.resource = (ID3D11Resource *) texture;
  data.rect = rect;
  data.ret = GST_FLOW_OK;

  gst_d3d11_device_thread_add (window->device,
      (GstD3D11DeviceThreadFunc) _present_on_device_thread, &data);

  return data.ret;
}
