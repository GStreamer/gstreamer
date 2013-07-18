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

#if GST_GL_HAVE_PLATFORM_WGL
#include "gstglwindow_win32_wgl.h"
#endif
#if GST_GL_HAVE_PLATFORM_EGL
#include "gstglwindow_win32_egl.h"
#endif

#define WM_GST_GL_WINDOW_CUSTOM (WM_APP+1)
#define WM_GST_GL_WINDOW_QUIT (WM_APP+2)

void gst_gl_window_set_pixel_format (GstGLWindowWin32 * window);
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
  GstGLAPI gl_api;
  guintptr external_gl_context;
  GError **error;
  gboolean activate;
  gboolean activate_result;
};

#define GST_CAT_DEFAULT gst_gl_window_win32_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_win32_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowWin32, gst_gl_window_win32,
    GST_GL_TYPE_WINDOW, DEBUG_INIT);

HHOOK hHook;

gboolean gst_gl_window_win32_create_context (GstGLWindow * window,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error);
guintptr gst_gl_window_win32_get_gl_context (GstGLWindow * window);
gboolean gst_gl_window_win32_activate (GstGLWindow * window, gboolean activate);
void gst_gl_window_win32_set_window_handle (GstGLWindow * window,
    guintptr handle);
void gst_gl_window_win32_draw_unlocked (GstGLWindow * window, guint width,
    guint height);
void gst_gl_window_win32_draw (GstGLWindow * window, guint width, guint height);
void gst_gl_window_win32_run (GstGLWindow * window);
void gst_gl_window_win32_quit (GstGLWindow * window);
void gst_gl_window_win32_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);

static void
gst_gl_window_win32_class_init (GstGLWindowWin32Class * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowWin32Private));

  window_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_create_context);
  window_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_get_gl_context);
  window_class->activate = GST_DEBUG_FUNCPTR (gst_gl_window_win32_activate);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_set_window_handle);
  window_class->draw_unlocked = GST_DEBUG_FUNCPTR (gst_gl_window_win32_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_win32_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_win32_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_win32_quit);
  window_class->send_message =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_send_message);
}

static void
gst_gl_window_win32_init (GstGLWindowWin32 * window)
{
  window->priv = GST_GL_WINDOW_WIN32_GET_PRIVATE (window);

  gst_gl_window_set_need_lock (GST_GL_WINDOW (window), FALSE);
}

/* Must be called in the gl thread */
GstGLWindowWin32 *
gst_gl_window_win32_new (void)
{
  GstGLWindowWin32 *window = NULL;
  const gchar *user_choice;

  user_choice = g_getenv ("GST_GL_PLATFORM");

#if GST_GL_HAVE_PLATFORM_WGL
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "wgl")))
    window = GST_GL_WINDOW_WIN32 (gst_gl_window_win32_wgl_new ());
#endif
#if GST_GL_HAVE_PLATFORM_EGL
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "egl")))
    window = GST_GL_WINDOW_WIN32 (gst_gl_window_win32_egl_new ());
#endif
  if (!window) {
    GST_WARNING ("Failed to create win32 window, user_choice:%s",
        user_choice ? user_choice : "NULL");
    return NULL;
  }

  return window;
}

gboolean
gst_gl_window_win32_create_context (GstGLWindow * window, GstGLAPI gl_api,
    guintptr external_gl_context, GError ** error)
{
  GstGLWindowWin32 *window_win32 = GST_GL_WINDOW_WIN32 (window);
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
      GST_WARNING ("Failed to register window class %lud\n", GetLastError ());
      goto failure;
    }
  }

  window_win32->internal_win_id = 0;
  window_win32->device = 0;
  window_win32->is_closed = FALSE;
  window_win32->visible = FALSE;

  window_win32->priv->gl_api = gl_api;
  window_win32->priv->external_gl_context = external_gl_context;
  window_win32->priv->error = error;

  window_win32->internal_win_id = CreateWindowEx (0,
      "GSTGL",
      "OpenGL renderer",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
      x, y, 0, 0, (HWND) NULL, (HMENU) NULL, hinstance, window_win32);

  if (!window_win32->internal_win_id) {
    GST_DEBUG ("failed to create gl window\n");
    goto failure;
  }

  GST_DEBUG ("gl window created: %" G_GUINTPTR_FORMAT "\n",
      (guintptr) window_win32->internal_win_id);

  //device is set in the window_proc
  if (!window_win32->device) {
    GST_DEBUG ("failed to create device\n");
    goto failure;
  }

  ShowCursor (TRUE);

  GST_LOG ("Created a win32 window");
  return TRUE;

failure:
  return FALSE;
}

guintptr
gst_gl_window_win32_get_gl_context (GstGLWindow * window)
{
  GstGLWindowWin32Class *window_class;

  window_class = GST_GL_WINDOW_WIN32_GET_CLASS (window);

  return window_class->get_gl_context (GST_GL_WINDOW_WIN32 (window));
}

static void
callback_activate (GstGLWindow * window)
{
  GstGLWindowWin32Class *window_class;
  GstGLWindowWin32Private *priv;
  GstGLWindowWin32 *window_win32;

  window_win32 = GST_GL_WINDOW_WIN32 (window);
  window_class = GST_GL_WINDOW_WIN32_GET_CLASS (window_win32);
  priv = window_win32->priv;

  priv->activate_result = window_class->activate (window_win32, priv->activate);
}

gboolean
gst_gl_window_win32_activate (GstGLWindow * window, gboolean activate)
{
  GstGLWindowWin32 *window_win32;
  GstGLWindowWin32Private *priv;

  window_win32 = GST_GL_WINDOW_WIN32 (window);
  priv = window_win32->priv;
  priv->activate = activate;

  gst_gl_window_win32_send_message (window,
      GST_GL_WINDOW_CB (callback_activate), window_win32);

  return priv->activate_result;
}

void
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

    GST_DEBUG ("release parent %" G_GUINTPTR_FORMAT "\n", (guintptr) parent_id);

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

    GST_DEBUG ("set parent %" G_GUINTPTR_FORMAT "\n", id);

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
void
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

void
gst_gl_window_win32_run (GstGLWindow * window)
{
  gint bRet;
  MSG msg;

  GST_INFO ("begin message loop\n");

  while (TRUE) {
    bRet = GetMessage (&msg, NULL, 0, 0);
    if (bRet == 0)
      break;
    else if (bRet == -1) {
      GST_WARNING ("Failed to get message %lud\n", GetLastError ());
      break;
    } else {
      GST_TRACE ("handle message");
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }
  }

  GST_INFO ("end message loop\n");
}

/* Thread safe */
void
gst_gl_window_win32_quit (GstGLWindow * window)
{
  GstGLWindowWin32 *window_win32;

  window_win32 = GST_GL_WINDOW_WIN32 (window);

  if (window_win32 && window_win32->internal_win_id) {
    LRESULT res =
        PostMessage (window_win32->internal_win_id, WM_GST_GL_WINDOW_QUIT,
        (WPARAM) 0, (LPARAM) 0);
    GST_DEBUG ("end loop requested");
    g_return_if_fail (SUCCEEDED (res));
  }
}

/* Thread safe */
void
gst_gl_window_win32_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowWin32 *window_win32;

  window_win32 = GST_GL_WINDOW_WIN32 (window);

  if (window_win32) {
    LRESULT res =
        SendMessage (window_win32->internal_win_id, WM_GST_GL_WINDOW_CUSTOM,
        (WPARAM) data, (LPARAM) callback);
    g_return_if_fail (SUCCEEDED (res));
  }
}

/* PRIVATE */

LRESULT CALLBACK
window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  GstGLWindowWin32 *window_win32;
  GstGLWindowWin32Class *window_class;
  GstGLWindowWin32Private *priv;

  if (uMsg == WM_CREATE) {
    window_win32 =
        GST_GL_WINDOW_WIN32 (((LPCREATESTRUCT) lParam)->lpCreateParams);
    window_class = GST_GL_WINDOW_WIN32_GET_CLASS (window_win32);

    GST_DEBUG ("WM_CREATE\n");

    {
      priv = window_win32->priv;
      window_win32->device = GetDC (hWnd);

      window_class->choose_format (window_win32);

      window_class->create_context (window_win32, priv->gl_api,
          priv->external_gl_context, priv->error);

/*      priv->gl_context = wglCreateContext (priv->device);
      if (priv->gl_context)
        GST_DEBUG ("gl context created: %" G_GUINTPTR_FORMAT "\n",
            (guintptr) priv->gl_context);
      else
        GST_DEBUG ("failed to create glcontext %" G_GUINTPTR_FORMAT ", %lud\n",
            (guintptr) hWnd, GetLastError ());
      g_assert (priv->gl_context);*/
      ReleaseDC (hWnd, window_win32->device);

      window_class->activate (window_win32, TRUE);

/*      if (!wglMakeCurrent (priv->device, priv->gl_context))
        GST_DEBUG ("failed to make opengl context current %" G_GUINTPTR_FORMAT
            ", %lud\n", (guintptr) hWnd, GetLastError ());
*/
      if (priv->external_gl_context)
        window_class->share_context (window_win32, priv->external_gl_context);
/*
      if (priv->external_gl_context) {
        if (!wglShareLists (priv->external_gl_context, priv->gl_context))
          GST_DEBUG ("failed to share opengl context %" G_GUINTPTR_FORMAT
              " with %" G_GUINTPTR_FORMAT "\n", (guintptr) priv->gl_context,
              (guintptr) priv->external_gl_context);
        else
          GST_DEBUG ("share opengl context succeed %" G_GUINTPTR_FORMAT "\n",
              (guintptr) priv->external_gl_context);
      }*/
    }

    SetProp (hWnd, "gl_window", window_win32);

    return 0;
  } else if (GetProp (hWnd, "gl_window")) {
    GstGLWindow *window;

    window_win32 = GST_GL_WINDOW_WIN32 (GetProp (hWnd, "gl_window"));
    window_class = GST_GL_WINDOW_WIN32_GET_CLASS (window_win32);
    window = GST_GL_WINDOW (window_win32);
    priv = window_win32->priv;

    g_assert (window_win32->internal_win_id == hWnd);

/*    g_assert (!wglGetCurrentContext ()
        || priv->gl_context == wglGetCurrentContext ());
*/
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
          window_class->swap_buffers (window_win32);
//          SwapBuffers (priv->device);
          EndPaint (hWnd, &ps);
        }
        break;
      }

      case WM_CLOSE:
      {
        ShowWindowAsync (window_win32->internal_win_id, SW_HIDE);

        GST_DEBUG ("WM_CLOSE\n");

        if (window->close)
          window->close (window->close_data);
        break;
      }

      case WM_GST_GL_WINDOW_QUIT:
      {
        HWND parent_id = 0;

        GST_DEBUG ("WM_GST_GL_WINDOW_QUIT\n");

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

        window_class->activate (window_win32, FALSE);

        window_class->destroy_context (window_win32);

/*        if (!wglMakeCurrent (NULL, NULL))
          GST_DEBUG ("failed to make current %" G_GUINTPTR_FORMAT ", %lud\n",
              (guintptr) hWnd, GetLastError ());

        if (priv->gl_context) {
          if (!wglDeleteContext (priv->gl_context))
            GST_DEBUG ("failed to destroy context %" G_GUINTPTR_FORMAT ", %lud\n",
                (guintptr) priv->gl_context, GetLastError ());
        }
*/
        if (window_win32->internal_win_id) {
          if (!DestroyWindow (window_win32->internal_win_id))
            GST_DEBUG ("failed to destroy window %" G_GUINTPTR_FORMAT
                ", %lud\n", (guintptr) hWnd, GetLastError ());
        }

        PostQuitMessage (0);
        break;
      }

      case WM_CAPTURECHANGED:
      {
        GST_DEBUG ("WM_CAPTURECHANGED\n");
        if (window->draw)
          window->draw (window->draw_data);
        break;
      }

      case WM_GST_GL_WINDOW_CUSTOM:
      {
        if (!window_win32->is_closed) {
          GstGLWindowCB custom_cb = (GstGLWindowCB) lParam;
          custom_cb ((gpointer) wParam);
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

    return 0;
  } else
    return DefWindowProc (hWnd, uMsg, wParam, lParam);
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
