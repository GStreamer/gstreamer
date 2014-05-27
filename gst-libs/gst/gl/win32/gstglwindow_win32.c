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

#define WM_GST_GL_WINDOW_CUSTOM (WM_APP+1)
#define WM_GST_GL_WINDOW_QUIT (WM_APP+2)

LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);
LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);

#define GST_GL_WINDOW_WIN32_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_WIN32, GstGLWindowWin32Private))

enum
{
  PROP_0
};

struct _GstGLWindowWin32Private
{
  GThread *thread;
};

#define GST_CAT_DEFAULT gst_gl_window_win32_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_win32_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowWin32, gst_gl_window_win32,
    GST_GL_TYPE_WINDOW, DEBUG_INIT);

static void gst_gl_window_win32_set_window_handle (GstGLWindow * window,
    guintptr handle);
static guintptr gst_gl_window_win32_get_display (GstGLWindow * window);
static void gst_gl_window_win32_draw (GstGLWindow * window, guint width,
    guint height);
static void gst_gl_window_win32_run (GstGLWindow * window);
static void gst_gl_window_win32_quit (GstGLWindow * window);
static void gst_gl_window_win32_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);

static void
gst_gl_window_win32_class_init (GstGLWindowWin32Class * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowWin32Private));

  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_set_window_handle);
  window_class->draw_unlocked = GST_DEBUG_FUNCPTR (gst_gl_window_win32_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_win32_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_win32_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_win32_quit);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_send_message_async);
  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_get_display);
}

static void
gst_gl_window_win32_init (GstGLWindowWin32 * window)
{
  window->priv = GST_GL_WINDOW_WIN32_GET_PRIVATE (window);
  window->priv->thread = NULL;
}

/* Must be called in the gl thread */
GstGLWindowWin32 *
gst_gl_window_win32_new (void)
{
  GstGLWindowWin32 *window = g_object_new (GST_GL_TYPE_WINDOW_WIN32, NULL);

  return window;
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
  window_win32->is_closed = FALSE;
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
  HWND parent_id;

  window_win32 = GST_GL_WINDOW_WIN32 (window);

  if (!window_win32->internal_win_id) {
    window_win32->parent_win_id = (HWND) id;
    return;
  }

  /* retrieve parent if previously set */
  parent_id = window_win32->parent_win_id;

  if (window_win32->visible) {
    ShowWindow (window_win32->internal_win_id, SW_HIDE);
    window_win32->visible = FALSE;
  }

  if (parent_id) {
    WNDPROC parent_proc = GetProp (parent_id, "gl_window_parent_proc");

    GST_DEBUG ("release parent %" G_GUINTPTR_FORMAT, (guintptr) parent_id);

    g_return_if_fail (parent_proc);

    SetWindowLongPtr (parent_id, GWLP_WNDPROC, (LONG_PTR) parent_proc);
    SetParent (window_win32->internal_win_id, NULL);

    RemoveProp (parent_id, "gl_window_parent_proc");
  }
  //not 0
  if (id) {
    WNDPROC window_parent_proc =
        (WNDPROC) GetWindowLongPtr ((HWND) id, GWLP_WNDPROC);
    RECT rect;

    GST_DEBUG ("set parent %" G_GUINTPTR_FORMAT, id);

    SetProp ((HWND) id, "gl_window_id", window_win32->internal_win_id);
    SetProp ((HWND) id, "gl_window_parent_proc", (WNDPROC) window_parent_proc);
    SetWindowLongPtr ((HWND) id, GWLP_WNDPROC, (LONG_PTR) sub_class_proc);

    SetWindowLongPtr (window_win32->internal_win_id, GWL_STYLE,
        WS_CHILD | WS_MAXIMIZE);
    SetParent (window_win32->internal_win_id, (HWND) id);

    /* take changes into account: SWP_FRAMECHANGED */
    GetClientRect ((HWND) id, &rect);
    SetWindowPos (window_win32->internal_win_id, HWND_TOP, rect.left, rect.top,
        rect.right, rect.bottom,
        SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
        SWP_FRAMECHANGED | SWP_NOACTIVATE);
    MoveWindow (window_win32->internal_win_id, rect.left, rect.top, rect.right,
        rect.bottom, FALSE);
  } else {
    /* no parent so the internal window needs borders and system menu */
    SetWindowLongPtr (window_win32->internal_win_id, GWL_STYLE,
        WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW);
  }
  window_win32->parent_win_id = (HWND) id;
}

/* Thread safe */
static void
gst_gl_window_win32_draw (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);

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

  RedrawWindow (window_win32->internal_win_id, NULL, NULL,
      RDW_NOERASE | RDW_INTERNALPAINT | RDW_INVALIDATE);
}

static void
gst_gl_window_win32_run (GstGLWindow * window)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);
  gint bRet;
  MSG msg;

  GST_INFO ("begin message loop");

  window_win32->priv->thread = g_thread_self ();

  while (TRUE) {
    bRet = GetMessage (&msg, NULL, 0, 0);
    if (bRet == 0)
      break;
    else if (bRet == -1) {
      GST_WARNING ("Failed to get message 0x%x",
          (unsigned int) GetLastError ());
      break;
    } else {
      GST_TRACE ("handle message");
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }
  }

  GST_INFO ("end message loop");
}

/* Thread safe */
static void
gst_gl_window_win32_quit (GstGLWindow * window)
{
  GstGLWindowWin32 *window_win32;

  window_win32 = GST_GL_WINDOW_WIN32 (window);
  window_win32->priv->thread = NULL;

  if (window_win32 && window_win32->internal_win_id) {
    LRESULT res =
        PostMessage (window_win32->internal_win_id, WM_GST_GL_WINDOW_QUIT,
        (WPARAM) 0, (LPARAM) 0);
    GST_DEBUG ("end loop requested");
    g_return_if_fail (SUCCEEDED (res));
  }
}

typedef struct _GstGLMessage
{
  GstGLWindowCB callback;
  gpointer data;
  GDestroyNotify destroy;
} GstGLMessage;

static gboolean
_run_message (GstGLMessage * message)
{
  if (message->callback)
    message->callback (message->data);

  if (message->destroy)
    message->destroy (message->data);

  g_slice_free (GstGLMessage, message);

  return FALSE;
}

/* Thread safe */
static void
gst_gl_window_win32_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowWin32 *window_win32;
  GstGLMessage *message;

  window_win32 = GST_GL_WINDOW_WIN32 (window);

  if (window_win32->priv->thread == g_thread_self ()) {
    /* re-entracy... */
    if (callback)
      callback (data);
    if (destroy)
      destroy (data);
    return;
  }

  message = g_slice_new (GstGLMessage);

  if (window_win32) {
    LRESULT res;

    message->callback = callback;
    message->data = data;
    message->destroy = destroy;

    res = PostMessage (window_win32->internal_win_id, WM_GST_GL_WINDOW_CUSTOM,
        (WPARAM) message, (LPARAM) NULL);
    g_return_if_fail (SUCCEEDED (res));
  }
}

/* PRIVATE */

LRESULT CALLBACK
window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  GstGLWindowWin32 *window_win32;
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
    return 0;
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
      {
        if (window->resize) {
          window->resize (window->resize_data, LOWORD (lParam),
              HIWORD (lParam));
        }
        break;
      }
      case WM_PAINT:
      {
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
      case WM_GST_GL_WINDOW_QUIT:
      {
        HWND parent_id = 0;

        GST_TRACE ("WM_GST_GL_WINDOW_QUIT");

        parent_id = window_win32->parent_win_id;
        if (parent_id) {
          WNDPROC parent_proc = GetProp (parent_id, "gl_window_parent_proc");

          g_assert (parent_proc);

          SetWindowLongPtr (parent_id, GWLP_WNDPROC, (LONG_PTR) parent_proc);
          SetParent (hWnd, NULL);

          RemoveProp (parent_id, "gl_window_parent_proc");
        }

        window_win32->is_closed = TRUE;
        RemoveProp (hWnd, "gl_window");

        if (window_win32->internal_win_id) {
          if (!DestroyWindow (window_win32->internal_win_id))
            GST_WARNING ("failed to destroy window %" G_GUINTPTR_FORMAT
                ", 0x%x", (guintptr) hWnd, (unsigned int) GetLastError ());
        }

        PostQuitMessage (0);
        break;
      }
      case WM_CAPTURECHANGED:
      {
        GST_DEBUG ("WM_CAPTURECHANGED");
        if (window->draw)
          window->draw (window->draw_data);
        break;
      }
      case WM_GST_GL_WINDOW_CUSTOM:
      {
        if (!window_win32->is_closed) {
          GstGLMessage *message = (GstGLMessage *) wParam;
          _run_message (message);
        }
        break;
      }
      case WM_ERASEBKGND:
        return TRUE;
      default:
      {
        /* transmit messages to the parrent (ex: mouse/keyboard input) */
        HWND parent_id = window_win32->parent_win_id;
        if (parent_id)
          PostMessage (parent_id, uMsg, wParam, lParam);
        return DefWindowProc (hWnd, uMsg, wParam, lParam);
      }
    }

    gst_object_unref (context);

    return 0;
  } else {
    return DefWindowProc (hWnd, uMsg, wParam, lParam);
  }
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
