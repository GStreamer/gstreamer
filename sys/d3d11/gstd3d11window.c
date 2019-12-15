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

#include "d3d11config.h"

#include "gstd3d11window.h"
#include "gstd3d11device.h"
#include "gstd3d11memory.h"
#include "gstd3d11utils.h"

#include <windows.h>

G_LOCK_DEFINE_STATIC (create_lock);

enum
{
  PROP_0,
  PROP_D3D11_DEVICE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_ENABLE_NAVIGATION_EVENTS,
  PROP_FULLSCREEN_TOGGLE_MODE,
  PROP_FULLSCREEN,
};

#define DEFAULT_ENABLE_NAVIGATION_EVENTS  TRUE
#define DEFAULT_FORCE_ASPECT_RATIO        TRUE
#define DEFAULT_FULLSCREEN_TOGGLE_MODE    GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE
#define DEFAULT_FULLSCREEN                FALSE

enum
{
  SIGNAL_KEY_EVENT,
  SIGNAL_MOUSE_EVENT,
  SIGNAL_GOT_WINDOW_HANDLE,
  SIGNAL_LAST
};

static guint d3d11_window_signals[SIGNAL_LAST] = { 0, };

GType
gst_d3d11_window_fullscreen_toggle_mode_type (void)
{
  static volatile gsize mode_type = 0;

  if (g_once_init_enter (&mode_type)) {
    static const GFlagsValue mode_types[] = {
      {GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE,
          "GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE", "none"},
      {GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER,
          "GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER", "alt-enter"},
      {GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY,
          "GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY", "property"},
      {0, NULL, NULL},
    };
    GType tmp = g_flags_register_static ("GstD3D11WindowFullscreenToggleMode",
        mode_types);
    g_once_init_leave (&mode_type, tmp);
  }

  return (GType) mode_type;
}

#define EXTERNAL_PROC_PROP_NAME "d3d11_window_external_proc"
#define D3D11_WINDOW_PROP_NAME "gst_d3d11_window_object"

#define WM_GST_D3D11_FULLSCREEN (WM_USER + 1)

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
static void gst_d3d11_window_constructed (GObject * object);
static void gst_d3d11_window_dispose (GObject * object);
static void gst_d3d11_window_finalize (GObject * object);
static gpointer gst_d3d11_window_thread_func (gpointer data);
static gboolean gst_d3d11_window_create_internal_window (GstD3D11Window * self);
static void gst_d3d11_window_close_internal_window (GstD3D11Window * self);
static void release_external_win_id (GstD3D11Window * self);
static GstFlowReturn gst_d3d111_window_present (GstD3D11Window * self,
    GstBuffer * buffer);
static void gst_d3d11_window_change_fullscreen_mode (GstD3D11Window * self);

static void
gst_d3d11_window_class_init (GstD3D11WindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_window_set_property;
  gobject_class->get_property = gst_d3d11_window_get_property;
  gobject_class->constructed = gst_d3d11_window_constructed;
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

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN_TOGGLE_MODE,
      g_param_spec_flags ("fullscreen-toggle-mode",
          "Full screen toggle mode",
          "Full screen toggle mode used to trigger fullscreen mode change",
          GST_D3D11_WINDOW_TOGGLE_MODE_GET_TYPE, DEFAULT_FULLSCREEN_TOGGLE_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen",
          "fullscreen",
          "Ignored when \"fullscreen-toggle-mode\" does not include \"property\"",
          DEFAULT_FULLSCREEN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
  self->fullscreen_toggle_mode = GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_NONE;
  self->fullscreen = DEFAULT_FULLSCREEN;

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
    case PROP_FULLSCREEN_TOGGLE_MODE:
      self->fullscreen_toggle_mode = g_value_get_flags (value);
      break;
    case PROP_FULLSCREEN:
    {
      self->requested_fullscreen = g_value_get_boolean (value);
      if (self->swap_chain) {
        g_atomic_int_add (&self->pending_fullscreen_count, 1);
        PostMessage (self->internal_win_id, WM_GST_D3D11_FULLSCREEN, 0, 0);
      }
      break;
    }
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
    case PROP_FULLSCREEN_TOGGLE_MODE:
      g_value_set_flags (value, self->fullscreen_toggle_mode);
      break;
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, self->fullscreen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_window_constructed (GObject * object)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (object);

  g_mutex_lock (&self->lock);
  self->loop = g_main_loop_new (self->main_context, FALSE);
  self->thread = g_thread_new ("GstD3D11Window",
      (GThreadFunc) gst_d3d11_window_thread_func, self);
  while (!g_main_loop_is_running (self->loop))
    g_cond_wait (&self->cond, &self->lock);
  g_mutex_unlock (&self->lock);
}

static void
gst_d3d11_window_release_resources (GstD3D11Device * device,
    GstD3D11Window * window)
{
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
    gst_d3d11_window_release_resources (self->device, self);
  }

  if (self->converter) {
    gst_d3d11_color_converter_free (self->converter);
    self->converter = NULL;
  }

  gst_clear_buffer (&self->cached_buffer);
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

static gpointer
gst_d3d11_window_thread_func (gpointer data)
{
  GstD3D11Window *self = GST_D3D11_WINDOW (data);
  GSource *source;

  GST_DEBUG_OBJECT (self, "Enter loop");
  g_main_context_push_thread_default (self->main_context);

  self->created = gst_d3d11_window_create_internal_window (self);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) running_cb, self, NULL);
  g_source_attach (source, self->main_context);
  g_source_unref (source);

  g_main_loop_run (self->loop);

  gst_d3d11_window_close_internal_window (self);

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
gst_d3d11_window_close_internal_window (GstD3D11Window * self)
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
  RECT rect;

  if (!self->external_win_id) {
    /* no parent so the internal window needs borders and system menu */
    SetWindowLongPtr (self->internal_win_id, GWL_STYLE,
        WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW);
    SetParent (self->internal_win_id, NULL);

    return;
  }

  external_window_proc =
      (WNDPROC) GetWindowLongPtr (self->external_win_id, GWLP_WNDPROC);

  GST_DEBUG ("set external window %" G_GUINTPTR_FORMAT,
      (guintptr) self->external_win_id);

  SetProp (self->external_win_id, EXTERNAL_PROC_PROP_NAME,
      (WNDPROC) external_window_proc);
  SetProp (self->external_win_id, D3D11_WINDOW_PROP_NAME, self);
  SetWindowLongPtr (self->external_win_id, GWLP_WNDPROC,
      (LONG_PTR) sub_class_proc);

  SetWindowLongPtr (self->internal_win_id, GWL_STYLE, WS_CHILD | WS_MAXIMIZE);
  SetParent (self->internal_win_id, self->external_win_id);

  /* take changes into account: SWP_FRAMECHANGED */
  GetClientRect (self->external_win_id, &rect);
  SetWindowPos (self->internal_win_id, HWND_TOP, rect.left, rect.top,
      rect.right, rect.bottom,
      SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
      SWP_FRAMECHANGED | SWP_NOACTIVATE);
  MoveWindow (self->internal_win_id, rect.left, rect.top, rect.right,
      rect.bottom, FALSE);
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
gst_d3d11_window_create_internal_window (GstD3D11Window * self)
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
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "d3d11 window created: %" G_GUINTPTR_FORMAT,
      (guintptr) self->internal_win_id);

  g_signal_emit (self,
      d3d11_window_signals[SIGNAL_GOT_WINDOW_HANDLE], 0, self->internal_win_id);

  /* device_handle is set in the window_proc */
  if (!self->device_handle) {
    GST_ERROR_OBJECT (self, "device handle is not available");
    return FALSE;
  }

  GST_LOG_OBJECT (self,
      "Created a internal d3d11 window %p", self->internal_win_id);

  self->msg_io_channel =
      g_io_channel_win32_new_messages ((guintptr) self->internal_win_id);
  self->msg_source = g_io_create_watch (self->msg_io_channel, G_IO_IN);
  g_source_set_callback (self->msg_source, (GSourceFunc) msg_cb, self, NULL);
  g_source_attach (self->msg_source, self->main_context);

  return TRUE;
}

static void
gst_d3d11_window_on_resize (GstD3D11Window * window, gboolean redraw)
{
  HRESULT hr;
  ID3D11Device *d3d11_dev;
  guint width, height;
  D3D11_TEXTURE2D_DESC desc;
  DXGI_SWAP_CHAIN_DESC swap_desc;
  ID3D11Texture2D *backbuffer = NULL;

  gst_d3d11_device_lock (window->device);
  if (!window->swap_chain)
    goto done;

  d3d11_dev = gst_d3d11_device_get_device_handle (window->device);

  if (window->rtv) {
    ID3D11RenderTargetView_Release (window->rtv);
    window->rtv = NULL;
  }

  window->pending_resize = FALSE;

  /* Set zero width and height here. dxgi will decide client area by itself */
  IDXGISwapChain_GetDesc (window->swap_chain, &swap_desc);
  hr = IDXGISwapChain_ResizeBuffers (window->swap_chain,
      0, 0, 0, DXGI_FORMAT_UNKNOWN, swap_desc.Flags);
  if (!gst_d3d11_result (hr)) {
    GST_ERROR_OBJECT (window, "Couldn't resize buffers, hr: 0x%x", (guint) hr);
    goto done;
  }

  hr = IDXGISwapChain_GetBuffer (window->swap_chain,
      0, &IID_ID3D11Texture2D, (void **) &backbuffer);
  if (!gst_d3d11_result (hr)) {
    GST_ERROR_OBJECT (window,
        "Cannot get backbuffer from swapchain, hr: 0x%x", (guint) hr);
    goto done;
  }

  ID3D11Texture2D_GetDesc (backbuffer, &desc);
  window->surface_width = desc.Width;
  window->surface_height = desc.Height;

  width = window->width;
  height = window->height;

  {
    GstVideoRectangle src_rect, dst_rect;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.w = width * window->aspect_ratio_n;
    src_rect.h = height * window->aspect_ratio_d;

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = window->surface_width;
    dst_rect.h = window->surface_height;

    if (window->force_aspect_ratio) {
      src_rect.w = width * window->aspect_ratio_n;
      src_rect.h = height * window->aspect_ratio_d;

      gst_video_sink_center_rect (src_rect, dst_rect, &window->render_rect,
          TRUE);
    } else {
      window->render_rect = dst_rect;
    }
  }

  GST_LOG_OBJECT (window,
      "New client area %dx%d, render rect x: %d, y: %d, %dx%d",
      desc.Width, desc.Height, window->render_rect.x, window->render_rect.y,
      window->render_rect.w, window->render_rect.h);

  hr = ID3D11Device_CreateRenderTargetView (d3d11_dev,
      (ID3D11Resource *) backbuffer, NULL, &window->rtv);
  if (!gst_d3d11_result (hr)) {
    GST_ERROR_OBJECT (window, "Cannot create render target view, hr: 0x%x",
        (guint) hr);

    goto done;
  }

  if (redraw)
    gst_d3d111_window_present (window, NULL);

done:
  if (backbuffer)
    ID3D11Texture2D_Release (backbuffer);

  gst_d3d11_device_unlock (window->device);
}

/* always called from window thread */
static void
gst_d3d11_window_change_fullscreen_mode (GstD3D11Window * self)
{
  HWND hwnd = self->external_win_id ? self->external_win_id :
      self->internal_win_id;

  if (!self->swap_chain)
    return;

  if (self->requested_fullscreen == self->fullscreen)
    return;

  GST_DEBUG_OBJECT (self, "Change mode to %s",
      self->requested_fullscreen ? "fullscreen" : "windowed");

  self->fullscreen = !self->fullscreen;

  if (!self->fullscreen) {
    /* Restore the window's attributes and size */
    SetWindowLong (hwnd, GWL_STYLE, self->restore_style);

    SetWindowPos (hwnd, HWND_NOTOPMOST,
        self->restore_rect.left,
        self->restore_rect.top,
        self->restore_rect.right - self->restore_rect.left,
        self->restore_rect.bottom - self->restore_rect.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE);

    ShowWindow (hwnd, SW_NORMAL);
  } else {
    IDXGIOutput *output;
    DXGI_OUTPUT_DESC output_desc;

    /* show window before change style */
    ShowWindow (hwnd, SW_SHOW);

    /* Save the old window rect so we can restore it when exiting
     * fullscreen mode */
    GetWindowRect (hwnd, &self->restore_rect);
    self->restore_style = GetWindowLong (hwnd, GWL_STYLE);

    /* Make the window borderless so that the client area can fill the screen */
    SetWindowLong (hwnd, GWL_STYLE,
        self->restore_style &
        ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU |
            WS_THICKFRAME));

    IDXGISwapChain_GetContainingOutput (self->swap_chain, &output);
    IDXGIOutput_GetDesc (output, &output_desc);
    IDXGIOutput_Release (output);

    SetWindowPos (hwnd, HWND_TOPMOST,
        output_desc.DesktopCoordinates.left,
        output_desc.DesktopCoordinates.top,
        output_desc.DesktopCoordinates.right,
        output_desc.DesktopCoordinates.bottom,
        SWP_FRAMECHANGED | SWP_NOACTIVATE);

    ShowWindow (hwnd, SW_MAXIMIZE);
  }

  GST_DEBUG_OBJECT (self, "Fullscreen mode change done");
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
      gst_d3d11_window_on_resize (self, TRUE);
      break;
    case WM_CLOSE:
      if (self->internal_win_id) {
        ShowWindow (self->internal_win_id, SW_HIDE);
        gst_d3d11_window_close_internal_window (self);
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
    case WM_SYSKEYDOWN:
      if ((self->fullscreen_toggle_mode &
              GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER)
          == GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER) {
        WORD state = GetKeyState (VK_RETURN);
        BYTE high = HIBYTE (state);

        if (high & 0x1) {
          self->requested_fullscreen = !self->fullscreen;
          gst_d3d11_window_change_fullscreen_mode (self);
        }
      }
      break;
    case WM_GST_D3D11_FULLSCREEN:
      if (g_atomic_int_get (&self->pending_fullscreen_count)) {
        g_atomic_int_dec_and_test (&self->pending_fullscreen_count);
        if ((self->fullscreen_toggle_mode &
                GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY)
            == GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY)
          gst_d3d11_window_change_fullscreen_mode (self);
      }
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

  if (uMsg == WM_SIZE)
    return 0;

  return DefWindowProc (hWnd, uMsg, wParam, lParam);
}

static LRESULT FAR PASCAL
sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC external_window_proc = GetProp (hWnd, EXTERNAL_PROC_PROP_NAME);
  GstD3D11Window *self =
      (GstD3D11Window *) GetProp (hWnd, D3D11_WINDOW_PROP_NAME);

  if (uMsg == WM_SIZE) {
    MoveWindow (self->internal_win_id, 0, 0, LOWORD (lParam), HIWORD (lParam),
        FALSE);
  } else if (uMsg == WM_CLOSE || uMsg == WM_DESTROY) {
    g_mutex_lock (&self->lock);
    GST_WARNING_OBJECT (self, "external window is closing");
    release_external_win_id (self);
    self->external_win_id = NULL;
    self->overlay_state = GST_D3D11_WINDOW_OVERLAY_STATE_CLOSED;
    g_mutex_unlock (&self->lock);
  } else {
    gst_d3d11_window_handle_window_proc (self, hWnd, uMsg, wParam, lParam);
  }

  return CallWindowProc (external_window_proc, hWnd, uMsg, wParam, lParam);
}

GstD3D11Window *
gst_d3d11_window_new (GstD3D11Device * device)
{
  GstD3D11Window *window;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  window = g_object_new (GST_TYPE_D3D11_WINDOW, "d3d11device", device, NULL);
  g_object_ref_sink (window);

  if (!window->created)
    gst_clear_object (&window);

  return window;
}

#if (DXGI_HEADER_VERSION >= 5)
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

/* missing in mingw header... */
typedef enum
{
  GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 = 0,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 = 1,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709 = 2,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020 = 3,
  GST_DXGI_COLOR_SPACE_RESERVED = 4,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601 = 5,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601 = 6,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 = 7,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 = 8,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 = 9,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020 = 10,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 = 11,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020 = 13,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 = 14,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020 = 15,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020 = 16,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 = 17,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020 = 18,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 = 19,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709 = 20,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020 = 21,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709 = 22,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020 = 23,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020 = 24,
  GST_DXGI_COLOR_SPACE_CUSTOM = 0xFFFFFFFF
} GST_DXGI_COLOR_SPACE_TYPE;

typedef struct
{
  GST_DXGI_COLOR_SPACE_TYPE type;
  GstVideoColorRange range;
  GstVideoTransferFunction transfer;
  GstVideoColorPrimaries primaries;
} DxgiColorSpaceMap;

/* https://docs.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type */
static const DxgiColorSpaceMap colorspace_map[] = {
  /* RGB, bt709 */
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_BT709, GST_VIDEO_COLOR_PRIMARIES_BT709},
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_GAMMA10, GST_VIDEO_COLOR_PRIMARIES_BT709},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_BT709, GST_VIDEO_COLOR_PRIMARIES_BT709},
  /* RGB, bt2020 */
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_BT2020_10, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_BT2020_10, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  /* RGB, bt2084 */
  {GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, GST_VIDEO_COLOR_RANGE_0_255,
      GST_VIDEO_TRANSFER_SMPTE2084, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020,
        GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_SMPTE2084, GST_VIDEO_COLOR_PRIMARIES_BT2020},
  /* RGB, SRGB */
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_SRGB, GST_VIDEO_COLOR_PRIMARIES_BT709},
  {GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020, GST_VIDEO_COLOR_RANGE_16_235,
      GST_VIDEO_TRANSFER_SRGB, GST_VIDEO_COLOR_PRIMARIES_BT2020},
};

static gboolean
gst_d3d11_window_color_space_from_video_info (GstD3D11Window * self,
    GstVideoInfo * info, IDXGISwapChain4 * swapchain,
    GST_DXGI_COLOR_SPACE_TYPE * dxgi_colorspace)
{
  gint i;
  gint best_idx = -1;
  gint best_score = 0;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (dxgi_colorspace != NULL, FALSE);

  /* We render only RGB for now */
  if (!GST_VIDEO_FORMAT_INFO_IS_RGB (info->finfo))
    return FALSE;

  /* find the best matching colorspace */
  for (i = 0; i < G_N_ELEMENTS (colorspace_map); i++) {
    GstVideoColorimetry *cinfo = &info->colorimetry;
    UINT can_support = 0;
    HRESULT hr;
    gint score = 0;
    GstVideoTransferFunction transfer = cinfo->transfer;

    if (transfer == GST_VIDEO_TRANSFER_BT2020_12)
      transfer = GST_VIDEO_TRANSFER_BT2020_10;

    hr = IDXGISwapChain4_CheckColorSpaceSupport (swapchain,
        colorspace_map[i].type, &can_support);

    if (SUCCEEDED (hr) &&
        (can_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ==
        DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) {
      if (cinfo->range == colorspace_map[i].range)
        score++;

      if (transfer == colorspace_map[i].transfer)
        score++;

      if (cinfo->primaries == colorspace_map[i].primaries)
        score++;

      GST_DEBUG_OBJECT (self,
          "colorspace %d supported, score %d", colorspace_map[i].type, score);

      if (score > best_score) {
        best_score = score;
        best_idx = i;
      }
    } else {
      GST_DEBUG_OBJECT (self,
          "colorspace %d not supported", colorspace_map[i].type);
    }
  }

  if (best_idx < 0)
    return FALSE;

  *dxgi_colorspace = colorspace_map[best_idx].type;

  return TRUE;
}
#endif

static void
gst_d3d11_window_disable_alt_enter (GstD3D11Window * window,
    IDXGISwapChain * swap_chain, HWND hwnd)
{
  IDXGIFactory1 *factory = NULL;
  HRESULT hr;

  hr = IDXGISwapChain_GetParent (swap_chain, &IID_IDXGIFactory1,
      (void **) &factory);
  if (!gst_d3d11_result (hr) || !factory) {
    GST_WARNING_OBJECT (window,
        "Cannot get parent dxgi factory for swapchain %p, hr: 0x%x",
        swap_chain, (guint) hr);
    return;
  }

  hr = IDXGIFactory1_MakeWindowAssociation (factory,
      hwnd, DXGI_MWA_NO_ALT_ENTER);
  if (!gst_d3d11_result (hr)) {
    GST_WARNING_OBJECT (window,
        "MakeWindowAssociation failure, hr: 0x%x", (guint) hr);
  }

  IDXGIFactory1_Release (factory);
}

gboolean
gst_d3d11_window_prepare (GstD3D11Window * window, guint width, guint height,
    guint aspect_ratio_n, guint aspect_ratio_d, GstCaps * caps, GError ** error)
{
  DXGI_SWAP_CHAIN_DESC desc = { 0, };
  GstCaps *render_caps;
  UINT swapchain_flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  DXGI_SWAP_EFFECT swap_effect = DXGI_SWAP_EFFECT_DISCARD;
#if (DXGI_HEADER_VERSION >= 5)
  gboolean have_cll = FALSE;
  gboolean have_mastering = FALSE;
  gboolean swapchain4_available = FALSE;
#endif

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);
  g_return_val_if_fail (aspect_ratio_n > 0, FALSE);
  g_return_val_if_fail (aspect_ratio_d > 0, FALSE);

  if (gst_d3d11_is_windows_8_or_greater ())
    swap_effect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

  GST_DEBUG_OBJECT (window, "Prepare window with %dx%d caps %" GST_PTR_FORMAT,
      width, height, caps);

  render_caps = gst_d3d11_device_get_supported_caps (window->device,
      D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DISPLAY);

  GST_DEBUG_OBJECT (window, "rendering caps %" GST_PTR_FORMAT, render_caps);
  render_caps = gst_d3d11_caps_fixate_format (caps, render_caps);

  if (!render_caps || gst_caps_is_empty (render_caps)) {
    GST_ERROR_OBJECT (window, "Couldn't define render caps");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Couldn't define render caps");
    gst_clear_caps (&render_caps);

    return FALSE;
  }

  render_caps = gst_caps_fixate (render_caps);
  gst_video_info_from_caps (&window->render_info, render_caps);
  gst_clear_caps (&render_caps);

  window->render_format =
      gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (&window->render_info));
  if (!window->render_format ||
      window->render_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (window, "Unknown dxgi render format");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Unknown dxgi render format");

    return FALSE;
  }

  gst_video_info_from_caps (&window->info, caps);

  if (window->converter)
    gst_d3d11_color_converter_free (window->converter);
  window->converter = NULL;

  /* preserve upstream colorimetry */
  window->render_info.colorimetry.primaries =
      window->info.colorimetry.primaries;
  window->render_info.colorimetry.transfer = window->info.colorimetry.transfer;

  window->converter =
      gst_d3d11_color_converter_new (window->device, &window->info,
      &window->render_info);

  if (!window->converter) {
    GST_ERROR_OBJECT (window, "Cannot create converter");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create converter");

    return FALSE;
  }

  window->allow_tearing = FALSE;
#if (DXGI_HEADER_VERSION >= 5)
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

  if (gst_d3d11_device_get_chosen_dxgi_factory_version (window->device) >=
      GST_D3D11_DXGI_FACTORY_5) {
    gboolean allow_tearing = FALSE;

    GST_DEBUG_OBJECT (window, "DXGI 1.5 interface is available");
    swapchain4_available = TRUE;

    /* For non-DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 color space support,
     * DXGI_SWAP_EFFECT_FLIP_DISCARD instead of DXGI_SWAP_EFFECT_DISCARD */
    swap_effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    g_object_get (window->device, "allow-tearing", &allow_tearing, NULL);
    if (allow_tearing) {
      GST_DEBUG_OBJECT (window, "device support tearning");
      swapchain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
      window->allow_tearing = TRUE;
    }
  }
#endif

  if (window->swap_chain) {
    gst_d3d11_device_lock (window->device);
    gst_d3d11_window_release_resources (window->device, window);
    gst_d3d11_device_unlock (window->device);
  }

  window->aspect_ratio_n = aspect_ratio_n;
  window->aspect_ratio_d = aspect_ratio_d;

  window->render_rect.x = 0;
  window->render_rect.y = 0;
  window->render_rect.w = width;
  window->render_rect.h = height;

  if (window->external_win_id) {
    RECT client_rect = { 0, };
    GetClientRect (window->external_win_id, &client_rect);

    window->surface_width = client_rect.right - client_rect.left;
    window->surface_height = client_rect.bottom - client_rect.top;
  } else {
    window->surface_width = width;
    window->surface_height = height;
  }

  window->width = width;
  window->height = height;

#if (DXGI_HEADER_VERSION >= 2)
  if (!window->swap_chain) {
    DXGI_SWAP_CHAIN_DESC1 desc1 = { 0, };
    desc1.Width = 0;
    desc1.Height = 0;
    desc1.Format = window->render_format->dxgi_format;
    /* FIXME: add support stereo */
    desc1.Stereo = FALSE;
    desc1.SampleDesc.Count = 1;
    desc1.SampleDesc.Quality = 0;
    desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc1.BufferCount = 2;
    /* NOTE: for UWP app, this should be DXGI_SCALING_ASPECT_RATIO_STRETCH
     * with CreateSwapChainForComposition or CreateSwapChainForCoreWindow */
    desc1.Scaling = DXGI_SCALING_STRETCH;

    /* scaling-stretch would break aspect-ratio so we prefer to use scaling-none,
     * but Windows7 does not support this method */
    if (gst_d3d11_is_windows_8_or_greater ())
      desc1.Scaling = DXGI_SCALING_NONE;
    desc1.SwapEffect = swap_effect;
    /* FIXME: might need to define for ovelay composition */
    desc1.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc1.Flags = swapchain_flags;

    window->swap_chain = (IDXGISwapChain *)
        gst_d3d11_device_create_swap_chain_for_hwnd (window->device,
        window->internal_win_id, &desc1, NULL, NULL);

    if (!window->swap_chain) {
      GST_WARNING_OBJECT (window, "Failed to create swapchain1");
    }
  }
#endif

  if (!window->swap_chain) {
    /* we will get client area at on_resize */
    desc.BufferDesc.Width = 0;
    desc.BufferDesc.Height = 0;
    /* don't care refresh rate */
    desc.BufferDesc.RefreshRate.Numerator = 0;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferDesc.Format = window->render_format->dxgi_format;
    desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = swap_effect;
    desc.OutputWindow = window->internal_win_id;
    desc.Windowed = TRUE;
    desc.Flags = swapchain_flags;

    window->swap_chain =
        gst_d3d11_device_create_swap_chain (window->device, &desc);
  }

  if (!window->swap_chain) {
    GST_ERROR_OBJECT (window, "Cannot create swapchain");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create swapchain");

    return FALSE;
  }

  /* disable alt+enter here. It should be manually handled */
  gst_d3d11_device_lock (window->device);
  gst_d3d11_window_disable_alt_enter (window,
      window->swap_chain, desc.OutputWindow);
  gst_d3d11_device_unlock (window->device);

#if (DXGI_HEADER_VERSION >= 5)
  if (swapchain4_available) {
    HRESULT hr;
    GST_DXGI_COLOR_SPACE_TYPE ctype;

    if (gst_d3d11_window_color_space_from_video_info (window,
            &window->render_info, (IDXGISwapChain4 *) window->swap_chain,
            &ctype)) {
      hr = IDXGISwapChain4_SetColorSpace1 ((IDXGISwapChain4 *)
          window->swap_chain, (DXGI_COLOR_SPACE_TYPE) ctype);

      if (!gst_d3d11_result (hr)) {
        GST_WARNING_OBJECT (window, "Failed to set colorspace %d, hr: 0x%x",
            ctype, (guint) hr);
      } else {
        GST_DEBUG_OBJECT (window, "Set colorspace %d", ctype);
      }

      if (have_cll && have_mastering) {
        DXGI_HDR_METADATA_HDR10 metadata = { 0, };

        GST_DEBUG_OBJECT (window, "Have HDR metadata, set to DXGI swapchain");

        mastering_display_gst_to_dxgi (&window->mastering_display_info,
            &window->content_light_level, &metadata);

        hr = IDXGISwapChain4_SetHDRMetaData ((IDXGISwapChain4 *)
            window->swap_chain, DXGI_HDR_METADATA_TYPE_HDR10,
            sizeof (DXGI_HDR_METADATA_HDR10), &metadata);
        if (!gst_d3d11_result (hr)) {
          GST_WARNING_OBJECT (window, "Couldn't set HDR metadata, hr 0x%x",
              (guint) hr);
        }
      }
    } else {
      GST_DEBUG_OBJECT (window,
          "Could not get color space from %" GST_PTR_FORMAT, caps);
    }
  }
#endif

  gst_d3d11_window_on_resize (window, FALSE);

  if (!window->rtv) {
    gst_d3d11_window_release_resources (window->device, window);
    GST_ERROR_OBJECT (window, "Failed to setup internal resources");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Failed to setup internal resources");

    return FALSE;
  }

  if (window->requested_fullscreen != window->fullscreen) {
    g_atomic_int_add (&window->pending_fullscreen_count, 1);
    PostMessage (window->internal_win_id, WM_GST_D3D11_FULLSCREEN, 0, 0);
  }

  GST_DEBUG_OBJECT (window, "New swap chain 0x%p created", window->swap_chain);

  return TRUE;
}

void
gst_d3d11_window_set_window_handle (GstD3D11Window * window, guintptr id)
{
  g_return_if_fail (GST_IS_D3D11_WINDOW (window));

  window->overlay_state = GST_D3D11_WINDOW_OVERLAY_STATE_NONE;

  if (window->visible) {
    ShowWindow (window->internal_win_id, SW_HIDE);
    window->visible = FALSE;
  }

  release_external_win_id (window);
  window->external_win_id = (HWND) id;
  set_external_win_id (window);

  if (window->external_win_id)
    window->overlay_state = GST_D3D11_WINDOW_OVERLAY_STATE_OPENED;
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

static GstFlowReturn
gst_d3d111_window_present (GstD3D11Window * self, GstBuffer * buffer)
{
  HRESULT hr;
  UINT present_flags = 0;

  if (buffer) {
    gst_buffer_replace (&self->cached_buffer, buffer);
  }

  if (self->cached_buffer) {
    ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES];
    gint i, j, k;
    RECT rect;

    for (i = 0, j = 0; i < gst_buffer_n_memory (self->cached_buffer); i++) {
      GstD3D11Memory *mem =
          (GstD3D11Memory *) gst_buffer_peek_memory (self->cached_buffer, i);
      for (k = 0; k < mem->num_shader_resource_views; k++) {
        srv[j] = mem->shader_resource_view[k];
        j++;
      }
    }

    rect.left = self->render_rect.x;
    rect.right = self->render_rect.x + self->render_rect.w;
    rect.top = self->render_rect.y;
    rect.bottom = self->render_rect.y + self->render_rect.h;

    gst_d3d11_color_converter_update_rect (self->converter, &rect);
    gst_d3d11_color_converter_convert (self->converter, srv, &self->rtv);

#if (DXGI_HEADER_VERSION >= 5)
    if (self->allow_tearing) {
      present_flags |= DXGI_PRESENT_ALLOW_TEARING;
    }
#endif

    hr = IDXGISwapChain_Present (self->swap_chain, 0, present_flags);

    if (!gst_d3d11_result (hr)) {
      GST_WARNING_OBJECT (self, "Direct3D cannot present texture, hr: 0x%x",
          (guint) hr);
    }
  }

  return GST_FLOW_OK;
}

GstFlowReturn
gst_d3d11_window_render (GstD3D11Window * window, GstBuffer * buffer,
    GstVideoRectangle * rect)
{
  GstMemory *mem;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), GST_FLOW_ERROR);
  g_return_val_if_fail (rect != NULL, GST_FLOW_ERROR);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_ERROR_OBJECT (window, "Invalid buffer");

    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&window->lock);
  if ((!window->external_win_id &&
          window->overlay_state == GST_D3D11_WINDOW_OVERLAY_STATE_CLOSED)
      || !window->internal_win_id) {
    GST_ERROR_OBJECT (window, "Output window was closed");
    g_mutex_unlock (&window->lock);

    return GST_D3D11_WINDOW_FLOW_CLOSED;
  }
  g_mutex_unlock (&window->lock);

  GST_OBJECT_LOCK (window);
  if (window->pending_resize) {
    gst_d3d11_window_on_resize (window, FALSE);
  }
  GST_OBJECT_UNLOCK (window);

  gst_d3d11_device_lock (window->device);
  ret = gst_d3d111_window_present (window, buffer);
  gst_d3d11_device_unlock (window->device);

  return ret;
}

gboolean
gst_d3d11_window_flush (GstD3D11Window * window)
{
  g_return_val_if_fail (GST_IS_D3D11_WINDOW (window), FALSE);

  gst_clear_buffer (&window->cached_buffer);

  return TRUE;
}
