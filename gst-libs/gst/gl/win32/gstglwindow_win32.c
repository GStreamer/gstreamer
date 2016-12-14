/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#include "gstglwindow_win32.h"
#include "win32_message_source.h"

LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);
LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);

#define GST_GL_WINDOW_WIN32_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_WINDOW_WIN32, GstGLWindowWin32Private))

enum
{
  PROP_0
};

struct _GstGLWindowWin32Private
{
  gint preferred_width;
  gint preferred_height;
};

#define GST_CAT_DEFAULT gst_gl_window_win32_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_win32_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowWin32, gst_gl_window_win32,
    GST_TYPE_GL_WINDOW, DEBUG_INIT);

static void gst_gl_window_win32_set_window_handle (GstGLWindow * window,
    guintptr handle);
static guintptr gst_gl_window_win32_get_display (GstGLWindow * window);
static void gst_gl_window_win32_set_preferred_size (GstGLWindow * window,
    gint width, gint height);
static void gst_gl_window_win32_show (GstGLWindow * window);
static void gst_gl_window_win32_draw (GstGLWindow * window);
gboolean gst_gl_window_win32_open (GstGLWindow * window, GError ** error);
void gst_gl_window_win32_close (GstGLWindow * window);
static void release_parent_win_id (GstGLWindowWin32 * window_win32);

static void
gst_gl_window_win32_class_init (GstGLWindowWin32Class * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowWin32Private));

  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_set_window_handle);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_win32_draw);
  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_get_display);
  window_class->set_preferred_size =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_set_preferred_size);
  window_class->show = GST_DEBUG_FUNCPTR (gst_gl_window_win32_show);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_win32_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_win32_close);
}

static void
gst_gl_window_win32_init (GstGLWindowWin32 * window)
{
  window->priv = GST_GL_WINDOW_WIN32_GET_PRIVATE (window);
}

GstGLWindowWin32 *
gst_gl_window_win32_new (GstGLDisplay * display)
{
  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_WIN32) ==
      0)
    /* we require an win32 display to create win32 windows */
    return NULL;

  return g_object_new (GST_TYPE_GL_WINDOW_WIN32, NULL);
}

static void
msg_cb (GstGLWindowWin32 * window_win32, MSG * msg, gpointer user_data)
{
  GST_TRACE ("handle message");
  TranslateMessage (msg);
  DispatchMessage (msg);
}

gboolean
gst_gl_window_win32_open (GstGLWindow * window, GError ** error)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);

  if (!GST_GL_WINDOW_CLASS (parent_class)->open (window, error))
    return FALSE;

  window_win32->msg_source = win32_message_source_new (window_win32);
  g_source_set_callback (window_win32->msg_source, (GSourceFunc) msg_cb,
      NULL, NULL);
  g_source_attach (window_win32->msg_source, window->main_context);

  return TRUE;
}

void
gst_gl_window_win32_close (GstGLWindow * window)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);

  release_parent_win_id (window_win32);

  if (window_win32->internal_win_id) {
    RemoveProp (window_win32->internal_win_id, "gl_window");
    ShowWindow (window_win32->internal_win_id, SW_HIDE);
    SetParent (window_win32->internal_win_id, NULL);
    if (!DestroyWindow (window_win32->internal_win_id))
      GST_WARNING ("failed to destroy window %" G_GUINTPTR_FORMAT
          ", 0x%x", (guintptr) window_win32->internal_win_id,
          (unsigned int) GetLastError ());
  }

  g_source_destroy (window_win32->msg_source);
  g_source_unref (window_win32->msg_source);
  window_win32->msg_source = NULL;

  GST_GL_WINDOW_CLASS (parent_class)->close (window);
}

static void
set_parent_win_id (GstGLWindowWin32 * window_win32)
{
  WNDPROC window_parent_proc;
  RECT rect;

  if (!window_win32->parent_win_id) {
    /* no parent so the internal window needs borders and system menu */
    SetWindowLongPtr (window_win32->internal_win_id, GWL_STYLE,
        WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW);
    SetParent (window_win32->internal_win_id, NULL);
    return;
  }

  window_parent_proc =
      (WNDPROC) GetWindowLongPtr (window_win32->parent_win_id, GWLP_WNDPROC);

  GST_DEBUG ("set parent %" G_GUINTPTR_FORMAT,
      (guintptr) window_win32->parent_win_id);

  SetProp (window_win32->parent_win_id, "gl_window_id",
      window_win32->internal_win_id);
  SetProp (window_win32->parent_win_id, "gl_window_parent_proc",
      (WNDPROC) window_parent_proc);
  SetWindowLongPtr (window_win32->parent_win_id, GWLP_WNDPROC,
      (LONG_PTR) sub_class_proc);

  SetWindowLongPtr (window_win32->internal_win_id, GWL_STYLE,
      WS_CHILD | WS_MAXIMIZE);
  SetParent (window_win32->internal_win_id, window_win32->parent_win_id);

  /* take changes into account: SWP_FRAMECHANGED */
  GetClientRect (window_win32->parent_win_id, &rect);
  SetWindowPos (window_win32->internal_win_id, HWND_TOP, rect.left, rect.top,
      rect.right, rect.bottom,
      SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
      SWP_FRAMECHANGED | SWP_NOACTIVATE);
  MoveWindow (window_win32->internal_win_id, rect.left, rect.top, rect.right,
      rect.bottom, FALSE);
}

static void
release_parent_win_id (GstGLWindowWin32 * window_win32)
{
  WNDPROC parent_proc;

  if (!window_win32->parent_win_id)
    return;

  parent_proc = GetProp (window_win32->parent_win_id, "gl_window_parent_proc");
  if (!parent_proc)
    return;

  GST_DEBUG ("release parent %" G_GUINTPTR_FORMAT,
      (guintptr) window_win32->parent_win_id);

  SetWindowLongPtr (window_win32->parent_win_id, GWLP_WNDPROC,
      (LONG_PTR) parent_proc);

  RemoveProp (window_win32->parent_win_id, "gl_window_parent_proc");
}

gboolean
gst_gl_window_win32_create_window (GstGLWindowWin32 * window_win32,
    GError ** error)
{
//  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);
  WNDCLASSEX wc;
  ATOM atom = 0;
  HINSTANCE hinstance = GetModuleHandle (NULL);

  static gint x = 0;
  static gint y = 0;

  GST_LOG ("Attempting to create a win32 window");

  x += 20;
  y += 20;

  atom = GetClassInfoEx (hinstance, "GSTGL", &wc);

  if (atom == 0) {
    ZeroMemory (&wc, sizeof (WNDCLASSEX));

    wc.cbSize = sizeof (WNDCLASSEX);
    wc.lpfnWndProc = window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hinstance;
    wc.hIcon = LoadIcon (NULL, IDI_WINLOGO);
    wc.hIconSm = NULL;
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "GSTGL";

    atom = RegisterClassEx (&wc);

    if (atom == 0) {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
          "Failed to register window class 0x%x\n",
          (unsigned int) GetLastError ());
      goto failure;
    }
  }

  window_win32->internal_win_id = 0;
  window_win32->device = 0;
  window_win32->visible = FALSE;

  window_win32->internal_win_id = CreateWindowEx (0,
      "GSTGL",
      "OpenGL renderer",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
      x, y, 0, 0, (HWND) NULL, (HMENU) NULL, hinstance, window_win32);

  if (!window_win32->internal_win_id) {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
        "failed to create gl window");
    goto failure;
  }

  GST_DEBUG ("gl window created: %" G_GUINTPTR_FORMAT,
      (guintptr) window_win32->internal_win_id);

  //device is set in the window_proc
  if (!window_win32->device) {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
        "failed to create device");
    goto failure;
  }

  ShowCursor (TRUE);

  GST_LOG ("Created a win32 window");

  /* The window has been created as if it had no parent, so there is nothing
   * else to do in that case. Even if user has already set a window,
   * parent_win_id could still be 0 at this point, and in that case calling
   * set_parent_win_id() here would steal focus from the parent window. */
  if (window_win32->parent_win_id)
    set_parent_win_id (window_win32);

  return TRUE;

failure:
  return FALSE;
}

static guintptr
gst_gl_window_win32_get_display (GstGLWindow * window)
{
  GstGLWindowWin32 *window_win32;

  window_win32 = GST_GL_WINDOW_WIN32 (window);

  return (guintptr) window_win32->device;
}

static void
gst_gl_window_win32_set_window_handle (GstGLWindow * window, guintptr id)
{
  GstGLWindowWin32 *window_win32;

  window_win32 = GST_GL_WINDOW_WIN32 (window);

  if (!window_win32->internal_win_id) {
    window_win32->parent_win_id = (HWND) id;
    return;
  }

  if (window_win32->visible) {
    ShowWindow (window_win32->internal_win_id, SW_HIDE);
    window_win32->visible = FALSE;
  }

  release_parent_win_id (window_win32);
  window_win32->parent_win_id = (HWND) id;
  set_parent_win_id (window_win32);
}

static void
gst_gl_window_win32_show (GstGLWindow * window)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);
  gint width = window_win32->priv->preferred_width;
  gint height = window_win32->priv->preferred_height;

  if (!window_win32->visible) {
    HWND parent_id = window_win32->parent_win_id;

    /* if no parent the real size has to be set now because this has not been done
     * when at window creation */
    if (!parent_id) {
      RECT rect;
      GetClientRect (window_win32->internal_win_id, &rect);
      width += 2 * GetSystemMetrics (SM_CXSIZEFRAME);
      height +=
          2 * GetSystemMetrics (SM_CYSIZEFRAME) +
          GetSystemMetrics (SM_CYCAPTION);
      MoveWindow (window_win32->internal_win_id, rect.left, rect.top, width,
          height, FALSE);
    }

    ShowWindowAsync (window_win32->internal_win_id, SW_SHOW);
    window_win32->visible = TRUE;
  }
}

static void
gst_gl_window_win32_set_preferred_size (GstGLWindow * window, gint width,
    gint height)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);

  window_win32->priv->preferred_width = width;
  window_win32->priv->preferred_height = height;
}

/* Thread safe */
static void
gst_gl_window_win32_draw (GstGLWindow * window)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);

  RedrawWindow (window_win32->internal_win_id, NULL, NULL,
      RDW_NOERASE | RDW_INTERNALPAINT | RDW_INVALIDATE);
}

/* PRIVATE */

LRESULT CALLBACK
window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  GstGLWindowWin32 *window_win32;
  LRESULT ret = 0;

  if (uMsg == WM_CREATE) {
    window_win32 =
        GST_GL_WINDOW_WIN32 (((LPCREATESTRUCT) lParam)->lpCreateParams);

    GST_TRACE ("WM_CREATE");

    window_win32->device = GetDC (hWnd);
    /* Do this, otherwise we hang on exit. We can still use it (due to the
     * CS_OWNDC flag in the WindowClass) after we have Released.
     */
    ReleaseDC (hWnd, window_win32->device);

    SetProp (hWnd, "gl_window", window_win32);
  } else if (GetProp (hWnd, "gl_window")) {
    GstGLWindow *window;
    GstGLContext *context;
    GstGLContextClass *context_class;

    window_win32 = GST_GL_WINDOW_WIN32 (GetProp (hWnd, "gl_window"));
    window = GST_GL_WINDOW (window_win32);
    context = gst_gl_window_get_context (window);
    context_class = GST_GL_CONTEXT_GET_CLASS (context);

    g_assert (window_win32->internal_win_id == hWnd);

    switch (uMsg) {
      case WM_SIZE:
        gst_gl_window_resize (window, LOWORD (lParam), HIWORD (lParam));
        break;
      case WM_PAINT:
      {
        if (window->queue_resize) {
          guint width, height;

          gst_gl_window_get_surface_dimensions (window, &width, &height);
          gst_gl_window_resize (window, width, height);
        }
        if (window->draw) {
          PAINTSTRUCT ps;
          BeginPaint (hWnd, &ps);
          window->draw (window->draw_data);
          context_class->swap_buffers (context);
          EndPaint (hWnd, &ps);
        }
        break;
      }
      case WM_CLOSE:
      {
        ShowWindowAsync (window_win32->internal_win_id, SW_HIDE);

        GST_TRACE ("WM_CLOSE");

        if (window->close)
          window->close (window->close_data);
        break;
      }
      case WM_CAPTURECHANGED:
      {
        GST_DEBUG ("WM_CAPTURECHANGED");
        if (window->queue_resize) {
          guint width, height;

          gst_gl_window_get_surface_dimensions (window, &width, &height);
          gst_gl_window_resize (window, width, height);
        }
        if (window->draw)
          window->draw (window->draw_data);
        break;
      }
      case WM_ERASEBKGND:
      {
        ret = TRUE;
        break;
      }
      default:
      {
        /* transmit messages to the parrent (ex: mouse/keyboard input) */
        HWND parent_id = window_win32->parent_win_id;
        if (parent_id)
          PostMessage (parent_id, uMsg, wParam, lParam);
        ret = DefWindowProc (hWnd, uMsg, wParam, lParam);
      }
    }

    gst_object_unref (context);
  } else {
    ret = DefWindowProc (hWnd, uMsg, wParam, lParam);
  }

  return ret;
}

LRESULT FAR PASCAL
sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC window_parent_proc = GetProp (hWnd, "gl_window_parent_proc");

  if (uMsg == WM_SIZE) {
    HWND gl_window_id = GetProp (hWnd, "gl_window_id");
    MoveWindow (gl_window_id, 0, 0, LOWORD (lParam), HIWORD (lParam), FALSE);
  }

  return CallWindowProc (window_parent_proc, hWnd, uMsg, wParam, lParam);
}
