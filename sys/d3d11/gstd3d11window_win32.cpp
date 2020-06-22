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

#include "gstd3d11device.h"
#include "gstd3d11memory.h"
#include "gstd3d11utils.h"
#include "gstd3d11window_win32.h"

extern "C" {
GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_window_debug);
#define GST_CAT_DEFAULT gst_d3d11_window_debug
}

G_LOCK_DEFINE_STATIC (create_lock);

#define EXTERNAL_PROC_PROP_NAME "d3d11_window_external_proc"
#define D3D11_WINDOW_PROP_NAME "gst_d3d11_window_win32_object"

#define WM_GST_D3D11_FULLSCREEN (WM_USER + 1)
#define WM_GST_D3D11_CONSTRUCT_INTERNAL_WINDOW (WM_USER + 2)

static LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);
static LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);

typedef enum
{
  GST_D3D11_WINDOW_WIN32_OVERLAY_STATE_NONE = 0,
  GST_D3D11_WINDOW_WIN32_OVERLAY_STATE_OPENED,
  GST_D3D11_WINDOW_WIN32_OVERLAY_STATE_CLOSED,
} GstD3D11WindowWin32OverlayState;

struct _GstD3D11WindowWin32
{
  GstD3D11Window parent;

  GMutex lock;
  GCond cond;

  GMainContext *main_context;
  GMainLoop *loop;

  gboolean visible;

  GSource *msg_source;
  GIOChannel *msg_io_channel;

  GThread *thread;

  HWND internal_hwnd;
  HWND external_hwnd;
  GstD3D11WindowWin32OverlayState overlay_state;

  HDC device_handle;
  gboolean first_present;
  gboolean have_swapchain1;

  /* atomic */
  volatile gint pending_fullscreen_count;

  /* fullscreen related */
  RECT restore_rect;
  LONG restore_style;
};

#define gst_d3d11_window_win32_parent_class parent_class
G_DEFINE_TYPE (GstD3D11WindowWin32, gst_d3d11_window_win32,
    GST_TYPE_D3D11_WINDOW);

static void gst_d3d11_window_win32_constructed (GObject * object);
static void gst_d3d11_window_win32_dispose (GObject * object);
static void gst_d3d11_window_win32_finalize (GObject * object);

static void gst_d3d11_window_win32_show (GstD3D11Window * window);
static void gst_d3d11_window_win32_update_swap_chain (GstD3D11Window * window);
static void
gst_d3d11_window_win32_change_fullscreen_mode (GstD3D11Window * window);
static gboolean
gst_d3d11_window_win32_create_swap_chain (GstD3D11Window * window,
    DXGI_FORMAT format, guint width, guint height,
    guint swapchain_flags, IDXGISwapChain ** swap_chain);
static GstFlowReturn gst_d3d11_window_win32_present (GstD3D11Window * window,
    guint present_flags);

static gpointer gst_d3d11_window_win32_thread_func (gpointer data);
static gboolean
gst_d3d11_window_win32_create_internal_window (GstD3D11WindowWin32 * self);
static void gst_d3d11_window_win32_close_internal_window (GstD3D11WindowWin32 *
    self);
static void gst_d3d11_window_win32_release_external_handle (GstD3D11WindowWin32
    * self);
static void
gst_d3d11_window_win32_set_window_handle (GstD3D11WindowWin32 * self,
    guintptr handle);
static void
gst_d3d11_window_win32_on_resize (GstD3D11Window * window,
    guint width, guint height);
static void gst_d3d11_window_win32_unprepare (GstD3D11Window * window);

static void
gst_d3d11_window_win32_class_init (GstD3D11WindowWin32Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstD3D11WindowClass *window_class = GST_D3D11_WINDOW_CLASS (klass);

  gobject_class->constructed = gst_d3d11_window_win32_constructed;
  gobject_class->dispose = gst_d3d11_window_win32_dispose;
  gobject_class->finalize = gst_d3d11_window_win32_finalize;

  window_class->show = GST_DEBUG_FUNCPTR (gst_d3d11_window_win32_show);
  window_class->update_swap_chain =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_win32_update_swap_chain);
  window_class->change_fullscreen_mode =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_win32_change_fullscreen_mode);
  window_class->create_swap_chain =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_win32_create_swap_chain);
  window_class->present = GST_DEBUG_FUNCPTR (gst_d3d11_window_win32_present);
  window_class->on_resize =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_win32_on_resize);
  window_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_win32_unprepare);
}

static void
gst_d3d11_window_win32_init (GstD3D11WindowWin32 * self)
{
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  self->main_context = g_main_context_new ();
}

static void
gst_d3d11_window_win32_constructed (GObject * object)
{
  GstD3D11Window *window = GST_D3D11_WINDOW (object);
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (object);

  if (window->external_handle) {
    gst_d3d11_window_win32_set_window_handle (self, window->external_handle);
    goto done;
  }

  g_mutex_lock (&self->lock);
  self->loop = g_main_loop_new (self->main_context, FALSE);
  self->thread = g_thread_new ("GstD3D11WindowWin32Win32",
      (GThreadFunc) gst_d3d11_window_win32_thread_func, self);
  while (!g_main_loop_is_running (self->loop))
    g_cond_wait (&self->cond, &self->lock);
  g_mutex_unlock (&self->lock);

done:
  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_d3d11_window_win32_dispose (GObject * object)
{
  gst_d3d11_window_win32_unprepare (GST_D3D11_WINDOW (object));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_window_win32_unprepare (GstD3D11Window * window)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (window);

  gst_d3d11_window_win32_release_external_handle (self);

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

  gst_d3d11_window_win32_close_internal_window (self);
}

static void
gst_d3d11_window_win32_finalize (GObject * object)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (object);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
running_cb (gpointer user_data)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (user_data);

  GST_TRACE_OBJECT (self, "Main loop running now");

  g_mutex_lock (&self->lock);
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
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

static gpointer
gst_d3d11_window_win32_thread_func (gpointer data)
{
  GstD3D11Window *window = GST_D3D11_WINDOW (data);
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (data);
  GSource *source;

  GST_DEBUG_OBJECT (self, "Enter loop");
  g_main_context_push_thread_default (self->main_context);

  window->initialized = gst_d3d11_window_win32_create_internal_window (self);

  self->msg_io_channel =
      g_io_channel_win32_new_messages ((guintptr) self->internal_hwnd);
  self->msg_source = g_io_create_watch (self->msg_io_channel, G_IO_IN);
  g_source_set_callback (self->msg_source, (GSourceFunc) msg_cb, self, NULL);
  g_source_attach (self->msg_source, self->main_context);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) running_cb, self, NULL);
  g_source_attach (source, self->main_context);
  g_source_unref (source);

  g_main_loop_run (self->loop);

  gst_d3d11_window_win32_close_internal_window (self);

  g_main_context_pop_thread_default (self->main_context);

  GST_DEBUG_OBJECT (self, "Exit loop");

  return NULL;
}

static void
gst_d3d11_window_win32_close_internal_window (GstD3D11WindowWin32 * self)
{
  if (self->internal_hwnd) {
    RemoveProp (self->internal_hwnd, D3D11_WINDOW_PROP_NAME);
    ShowWindow (self->internal_hwnd, SW_HIDE);
    SetParent (self->internal_hwnd, NULL);
    if (!DestroyWindow (self->internal_hwnd))
      GST_WARNING ("failed to destroy window %" G_GUINTPTR_FORMAT
          ", 0x%x", (guintptr) self->internal_hwnd, (guint) GetLastError ());
    self->internal_hwnd = NULL;
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
gst_d3d11_window_win32_set_external_handle (GstD3D11WindowWin32 * self)
{
  WNDPROC external_window_proc;

  external_window_proc =
      (WNDPROC) GetWindowLongPtr (self->external_hwnd, GWLP_WNDPROC);

  GST_DEBUG_OBJECT (self, "set external window %" G_GUINTPTR_FORMAT
      ", original window procedure %p", (guintptr) self->external_hwnd,
      external_window_proc);

  SetProp (self->external_hwnd, EXTERNAL_PROC_PROP_NAME,
      (HANDLE) external_window_proc);
  SetProp (self->external_hwnd, D3D11_WINDOW_PROP_NAME, self);
  SetWindowLongPtr (self->external_hwnd, GWLP_WNDPROC,
      (LONG_PTR) sub_class_proc);

  /* Will create our internal window on parent window's thread */
  SendMessage (self->external_hwnd, WM_GST_D3D11_CONSTRUCT_INTERNAL_WINDOW,
      0, 0);
}

static void
gst_d3d11_window_win32_release_external_handle (GstD3D11WindowWin32 * self)
{
  WNDPROC external_proc;

  if (!self->external_hwnd)
    return;

  external_proc =
      (WNDPROC) GetProp (self->external_hwnd, EXTERNAL_PROC_PROP_NAME);
  if (!external_proc)
    return;

  GST_DEBUG_OBJECT (self, "release external window %" G_GUINTPTR_FORMAT
      ", original window procedure %p", (guintptr) self->external_hwnd,
      external_proc);

  if (!SetWindowLongPtr (self->external_hwnd,
      GWLP_WNDPROC, (LONG_PTR) external_proc)) {
    GST_WARNING_OBJECT (self, "Couldn't restore original window procedure");
  }

  RemoveProp (self->external_hwnd, EXTERNAL_PROC_PROP_NAME);
  RemoveProp (self->external_hwnd, D3D11_WINDOW_PROP_NAME);
  self->external_hwnd = NULL;
}

static gboolean
gst_d3d11_window_win32_create_internal_window (GstD3D11WindowWin32 * self)
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
  self->internal_hwnd = 0;
  self->visible = FALSE;

  self->internal_hwnd = CreateWindowEx (0,
      "GSTD3D11",
      "Direct3D11 renderer",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT,
      0, 0, (HWND) NULL, (HMENU) NULL, hinstance, self);

  G_UNLOCK (create_lock);

  if (!self->internal_hwnd) {
    GST_ERROR_OBJECT (self, "Failed to create d3d11 window");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "d3d11 window created: %" G_GUINTPTR_FORMAT,
      (guintptr) self->internal_hwnd);

  /* device_handle is set in the window_proc */
  if (!self->device_handle) {
    GST_ERROR_OBJECT (self, "device handle is not available");
    return FALSE;
  }

  GST_LOG_OBJECT (self,
      "Created a internal d3d11 window %p", self->internal_hwnd);

  return TRUE;
}

/* always called from window thread */
static void
gst_d3d11_window_win32_change_fullscreen_mode_internal (GstD3D11WindowWin32 *
    self)
{
  GstD3D11Window *window = GST_D3D11_WINDOW (self);
  HWND hwnd = self->external_hwnd ? self->external_hwnd : self->internal_hwnd;

  if (!window->swap_chain)
    return;

  if (window->requested_fullscreen == window->fullscreen)
    return;

  GST_DEBUG_OBJECT (self, "Change mode to %s",
      window->requested_fullscreen ? "fullscreen" : "windowed");

  window->fullscreen = !window->fullscreen;

  if (!window->fullscreen) {
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
    IDXGISwapChain *swap_chain = window->swap_chain;

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

    swap_chain->GetContainingOutput (&output);
    output->GetDesc (&output_desc);
    output->Release ();

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
gst_d3d11_window_win32_on_key_event (GstD3D11WindowWin32 * self,
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  GstD3D11Window *window = GST_D3D11_WINDOW (self);
  gunichar2 wcrep[128];
  const gchar *event;

  if (!window->enable_navigation_events)
    return;

  if (GetKeyNameTextW (lParam, (LPWSTR) wcrep, 128)) {
    gchar *utfrep = g_utf16_to_utf8 (wcrep, 128, NULL, NULL, NULL);
    if (utfrep) {
      if (uMsg == WM_KEYDOWN)
        event = "key-press";
      else
        event = "key-release";

      gst_d3d11_window_on_key_event (window, event, utfrep);
      g_free (utfrep);
    }
  }
}

static void
gst_d3d11_window_win32_on_mouse_event (GstD3D11WindowWin32 * self,
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  GstD3D11Window *window = GST_D3D11_WINDOW (self);
  gint button;
  const gchar *event = NULL;

  if (!window->enable_navigation_events)
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
    gst_d3d11_window_on_mouse_event (window,
        event, button, (gdouble) LOWORD (lParam), (gdouble) HIWORD (lParam));
}

static void
gst_d3d11_window_win32_handle_window_proc (GstD3D11WindowWin32 * self,
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  GstD3D11Window *window = GST_D3D11_WINDOW (self);

  switch (uMsg) {
    case WM_SIZE:
      gst_d3d11_window_win32_on_resize (window, 0, 0);
      break;
    case WM_CLOSE:
      if (self->internal_hwnd) {
        ShowWindow (self->internal_hwnd, SW_HIDE);
        gst_d3d11_window_win32_close_internal_window (self);
      }
      break;
    case WM_KEYDOWN:
    case WM_KEYUP:
      gst_d3d11_window_win32_on_key_event (self, hWnd, uMsg, wParam, lParam);
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
      /* To handle mouse event only once, do this only for internal window */
      if (self->internal_hwnd && self->internal_hwnd == hWnd)
        gst_d3d11_window_win32_on_mouse_event (self, hWnd, uMsg, wParam, lParam);

      /* DefWindowProc will not chain up mouse event to parent window */
      if (self->external_hwnd && self->external_hwnd != hWnd)
        SendMessage (self->external_hwnd, uMsg, wParam, lParam);
      break;
    case WM_SYSKEYDOWN:
      if ((window->fullscreen_toggle_mode &
              GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER)
          == GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_ALT_ENTER) {
        WORD state = GetKeyState (VK_RETURN);
        BYTE high = HIBYTE (state);

        if (high & 0x1) {
          window->requested_fullscreen = !window->fullscreen;
          gst_d3d11_window_win32_change_fullscreen_mode_internal (self);
        }
      }
      break;
    case WM_GST_D3D11_FULLSCREEN:
      if (g_atomic_int_get (&self->pending_fullscreen_count)) {
        g_atomic_int_dec_and_test (&self->pending_fullscreen_count);
        if ((window->fullscreen_toggle_mode &
                GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY)
            == GST_D3D11_WINDOW_FULLSCREEN_TOGGLE_MODE_PROPERTY)
          gst_d3d11_window_win32_change_fullscreen_mode_internal (self);
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
  GstD3D11WindowWin32 *self;

  if (uMsg == WM_CREATE) {
    self = GST_D3D11_WINDOW_WIN32 (((LPCREATESTRUCT) lParam)->lpCreateParams);

    GST_LOG_OBJECT (self, "WM_CREATE");

    self->device_handle = GetDC (hWnd);
    /* Do this, otherwise we hang on exit. We can still use it (due to the
     * CS_OWNDC flag in the WindowClass) after we have Released.
     */
    ReleaseDC (hWnd, self->device_handle);

    SetProp (hWnd, D3D11_WINDOW_PROP_NAME, self);
  } else if (GetProp (hWnd, D3D11_WINDOW_PROP_NAME)) {
    HANDLE handle = GetProp (hWnd, D3D11_WINDOW_PROP_NAME);

    if (!GST_IS_D3D11_WINDOW_WIN32 (handle)) {
      GST_WARNING ("%p is not d3d11window object", handle);
      goto done;
    }

    self = GST_D3D11_WINDOW_WIN32 (handle);

    g_assert (self->internal_hwnd == hWnd);

    gst_d3d11_window_win32_handle_window_proc (self, hWnd, uMsg, wParam,
        lParam);
  }

  if (uMsg == WM_SIZE)
    return 0;

done:
  return DefWindowProc (hWnd, uMsg, wParam, lParam);
}

static LRESULT FAR PASCAL
sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC external_window_proc =
      (WNDPROC) GetProp (hWnd, EXTERNAL_PROC_PROP_NAME);
  GstD3D11WindowWin32 *self =
      (GstD3D11WindowWin32 *) GetProp (hWnd, D3D11_WINDOW_PROP_NAME);

  if (uMsg == WM_GST_D3D11_CONSTRUCT_INTERNAL_WINDOW) {
    GstD3D11Window *window = GST_D3D11_WINDOW (self);
    RECT rect;

    GST_DEBUG_OBJECT (self, "Create internal window");

    window->initialized = gst_d3d11_window_win32_create_internal_window (self);

    SetWindowLongPtr (self->internal_hwnd, GWL_STYLE, WS_CHILD | WS_MAXIMIZE);
    SetParent (self->internal_hwnd, self->external_hwnd);

    /* take changes into account: SWP_FRAMECHANGED */
    GetClientRect (self->external_hwnd, &rect);
    SetWindowPos (self->internal_hwnd, HWND_TOP, rect.left, rect.top,
        rect.right, rect.bottom,
        SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
        SWP_FRAMECHANGED | SWP_NOACTIVATE);
    MoveWindow (self->internal_hwnd, rect.left, rect.top, rect.right,
        rect.bottom, FALSE);

    /* don't need to be chained up to parent window procedure,
     * as this is our custom message */
    return 0;
  } else if (uMsg == WM_SIZE) {
    MoveWindow (self->internal_hwnd, 0, 0, LOWORD (lParam), HIWORD (lParam),
        FALSE);
  } else if (uMsg == WM_CLOSE || uMsg == WM_DESTROY) {
    g_mutex_lock (&self->lock);
    GST_WARNING_OBJECT (self, "external window is closing");
    gst_d3d11_window_win32_release_external_handle (self);
    self->external_hwnd = NULL;
    self->overlay_state = GST_D3D11_WINDOW_WIN32_OVERLAY_STATE_CLOSED;
    g_mutex_unlock (&self->lock);
  } else {
    gst_d3d11_window_win32_handle_window_proc (self, hWnd, uMsg, wParam,
        lParam);
  }

  return CallWindowProc (external_window_proc, hWnd, uMsg, wParam, lParam);
}

static void
gst_d3d11_window_win32_disable_alt_enter (GstD3D11WindowWin32 * self,
    GstD3D11Device * device, IDXGISwapChain * swap_chain, HWND hwnd)
{
  IDXGIFactory1 *factory = NULL;
  HRESULT hr;

  hr = swap_chain->GetParent (IID_IDXGIFactory1, (void **) &factory);
  if (!gst_d3d11_result (hr, device) || !factory) {
    GST_WARNING_OBJECT (self,
        "Cannot get parent dxgi factory for swapchain %p, hr: 0x%x",
        swap_chain, (guint) hr);
    return;
  }

  hr = factory->MakeWindowAssociation (hwnd, DXGI_MWA_NO_ALT_ENTER);
  if (!gst_d3d11_result (hr, device)) {
    GST_WARNING_OBJECT (self,
        "MakeWindowAssociation failure, hr: 0x%x", (guint) hr);
  }

  factory->Release ();
}

static gboolean
gst_d3d11_window_win32_create_swap_chain (GstD3D11Window * window,
    DXGI_FORMAT format, guint width, guint height,
    guint swapchain_flags, IDXGISwapChain ** swap_chain)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (window);
  DXGI_SWAP_CHAIN_DESC desc = { 0, };
  IDXGISwapChain *new_swapchain = NULL;
  GstD3D11Device *device = window->device;

  self->have_swapchain1 = FALSE;

#if (DXGI_HEADER_VERSION >= 2)
  {
    DXGI_SWAP_CHAIN_DESC1 desc1 = { 0, };
    desc1.Width = 0;
    desc1.Height = 0;
    desc1.Format = format;
    /* FIXME: add support stereo */
    desc1.Stereo = FALSE;
    desc1.SampleDesc.Count = 1;
    desc1.SampleDesc.Quality = 0;
    desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc1.BufferCount = 2;
    desc1.Scaling = DXGI_SCALING_STRETCH;

    /* scaling-stretch would break aspect-ratio so we prefer to use scaling-none,
     * but Windows7 does not support this method */
    if (gst_d3d11_is_windows_8_or_greater ())
      desc1.Scaling = DXGI_SCALING_NONE;
    desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc1.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc1.Flags = swapchain_flags;

    new_swapchain = (IDXGISwapChain *)
        gst_d3d11_device_create_swap_chain_for_hwnd (device,
        self->internal_hwnd, &desc1, NULL, NULL);

    if (!new_swapchain) {
      GST_WARNING_OBJECT (self, "Failed to create swapchain1");
    } else {
      self->have_swapchain1 = TRUE;
    }
  }
#endif

  if (!new_swapchain) {
    DXGI_SWAP_EFFECT swap_effect = DXGI_SWAP_EFFECT_DISCARD;

    if (gst_d3d11_is_windows_8_or_greater ())
      swap_effect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    /* we will get client area at on_resize */
    desc.BufferDesc.Width = 0;
    desc.BufferDesc.Height = 0;
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
    desc.SwapEffect = swap_effect;
    desc.OutputWindow = self->internal_hwnd;
    desc.Windowed = TRUE;
    desc.Flags = swapchain_flags;

    new_swapchain = gst_d3d11_device_create_swap_chain (device, &desc);
  }

  if (!new_swapchain) {
    GST_ERROR_OBJECT (self, "Cannot create swapchain");
    return FALSE;
  }

  /* disable alt+enter here. It should be manually handled */
  gst_d3d11_device_lock (device);
  gst_d3d11_window_win32_disable_alt_enter (self,
      device, new_swapchain, desc.OutputWindow);
  gst_d3d11_device_unlock (device);

  *swap_chain = new_swapchain;

  return TRUE;
}

static void
gst_d3d11_window_win32_set_window_handle (GstD3D11WindowWin32 * self,
    guintptr handle)
{
  self->overlay_state = GST_D3D11_WINDOW_WIN32_OVERLAY_STATE_NONE;

  self->external_hwnd = (HWND) handle;
  gst_d3d11_window_win32_set_external_handle (self);

  self->overlay_state = GST_D3D11_WINDOW_WIN32_OVERLAY_STATE_OPENED;
}

static void
gst_d3d11_window_win32_show (GstD3D11Window * window)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (window);
  gint width, height;

  width = GST_VIDEO_INFO_WIDTH (&window->render_info);
  height = GST_VIDEO_INFO_HEIGHT (&window->render_info);

  if (!self->visible) {
    /* if no parent the real size has to be set now because this has not been done
     * when at window creation */
    if (!self->external_hwnd) {
      RECT rect;
      GetClientRect (self->internal_hwnd, &rect);
      width += 2 * GetSystemMetrics (SM_CXSIZEFRAME);
      height +=
          2 * GetSystemMetrics (SM_CYSIZEFRAME) +
          GetSystemMetrics (SM_CYCAPTION);
      MoveWindow (self->internal_hwnd, rect.left, rect.top, width,
          height, FALSE);
    }

    ShowWindow (self->internal_hwnd, SW_SHOW);
    self->visible = TRUE;
  }
}

static GstFlowReturn
gst_d3d11_window_win32_present (GstD3D11Window * window, guint present_flags)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (window);
  HRESULT hr;

  if ((!self->external_hwnd &&
          self->overlay_state == GST_D3D11_WINDOW_WIN32_OVERLAY_STATE_CLOSED)
      || !self->internal_hwnd) {
    GST_ERROR_OBJECT (self, "Output window was closed");

    return GST_D3D11_WINDOW_FLOW_CLOSED;
  }

#if (DXGI_HEADER_VERSION >= 2)
  if (self->have_swapchain1) {
    IDXGISwapChain1 *swap_chain1 = (IDXGISwapChain1 *) window->swap_chain;
    DXGI_PRESENT_PARAMETERS present_params = { 0, };

    /* the first present should not specify dirty-rect */
    if (!window->first_present) {
      present_params.DirtyRectsCount = 1;
      present_params.pDirtyRects = &window->render_rect;
    }

    hr = swap_chain1->Present1 (0, present_flags, &present_params);
  } else
#endif
  {
    hr = window->swap_chain->Present (0, present_flags);
  }

  if (!gst_d3d11_result (hr, window->device)) {
    GST_WARNING_OBJECT (self, "Direct3D cannot present texture, hr: 0x%x",
        (guint) hr);
  }

  return GST_FLOW_OK;
}

static void
gst_d3d11_window_win32_on_resize (GstD3D11Window * window,
    guint width, guint height)
{
  /* Set zero width and height here. dxgi will decide client area by itself */
  GST_D3D11_WINDOW_CLASS (parent_class)->on_resize (window, 0, 0);
}

static void
gst_d3d11_window_win32_update_swap_chain (GstD3D11Window * window)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (window);

  if (self->internal_hwnd)
    PostMessage (self->internal_hwnd, WM_SIZE, 0, 0);

  return;
}

static void
gst_d3d11_window_win32_change_fullscreen_mode (GstD3D11Window * window)
{
  GstD3D11WindowWin32 *self = GST_D3D11_WINDOW_WIN32 (window);

  if (self->internal_hwnd) {
    g_atomic_int_add (&self->pending_fullscreen_count, 1);
    PostMessage (self->internal_hwnd, WM_GST_D3D11_FULLSCREEN, 0, 0);
  }
}

GstD3D11Window *
gst_d3d11_window_win32_new (GstD3D11Device * device, guintptr handle)
{
  GstD3D11Window *window;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  window = (GstD3D11Window *) g_object_new (GST_TYPE_D3D11_WINDOW_WIN32,
      "d3d11device", device, "window-handle", handle, NULL);
  if (!window->initialized) {
    gst_object_unref (window);
    return NULL;
  }

  g_object_ref_sink (window);

  return window;
}