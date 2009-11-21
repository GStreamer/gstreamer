/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglwindow.h"

#define WM_GST_GL_WINDOW_CUSTOM (WM_APP+1)
#define WM_GST_GL_WINDOW_QUIT (WM_APP+2)

LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);
LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam);

const gchar *EGLErrorString ();

#define GST_GL_WINDOW_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))

enum
{
  PROP_0
};

struct _GstGLWindowPrivate
{
  EGLNativeWindowType internal_win_id;
  EGLDisplay display;
  EGLSurface surface;
  EGLContext gl_context;
  EGLContext external_gl_context;
  GstGLWindowCB draw_cb;
  gpointer draw_data;
  GstGLWindowCB2 resize_cb;
  gpointer resize_data;
  GstGLWindowCB close_cb;
  gpointer close_data;
  gboolean is_closed;
  gboolean visible;
};

G_DEFINE_TYPE (GstGLWindow, gst_gl_window, G_TYPE_OBJECT);

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GstGLWindow"

gboolean _gst_gl_window_debug = FALSE;

HHOOK hHook;

/* Must be called in the gl thread */
static void
gst_gl_window_finalize (GObject * object)
{
  GstGLWindow *window = GST_GL_WINDOW (object);
  GstGLWindowPrivate *priv = window->priv;

  G_OBJECT_CLASS (gst_gl_window_parent_class)->finalize (object);
}

static void
gst_gl_window_log_handler (const gchar * domain, GLogLevelFlags flags,
    const gchar * message, gpointer user_data)
{
  if (_gst_gl_window_debug) {
    g_log_default_handler (domain, flags, message, user_data);
  }
}

static void
gst_gl_window_base_init (gpointer g_class)
{
}

static void
gst_gl_window_class_init (GstGLWindowClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  WNDCLASS wc;
  ATOM atom = 0;
  HINSTANCE hinstance = GetModuleHandle (NULL);

  g_type_class_add_private (klass, sizeof (GstGLWindowPrivate));

  obj_class->finalize = gst_gl_window_finalize;

  atom = GetClassInfo (hinstance, "GSTGL", &wc);

  if (atom == 0) {
    ZeroMemory (&wc, sizeof (WNDCLASS));

    wc.lpfnWndProc = window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hinstance;
    wc.hIcon = LoadIcon (NULL, IDI_WINLOGO);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "GSTGL";

    atom = RegisterClass (&wc);

    if (atom == 0)
      g_error ("Failed to register window class %lud\n", GetLastError ());
  }
}

static void
gst_gl_window_init (GstGLWindow * window)
{
  window->priv = GST_GL_WINDOW_GET_PRIVATE (window);

  if (g_getenv ("GST_GL_WINDOW_DEBUG") != NULL)
    _gst_gl_window_debug = TRUE;

  g_log_set_handler ("GstGLWindow", G_LOG_LEVEL_DEBUG,
      gst_gl_window_log_handler, NULL);
}

/* Must be called in the gl thread */
GstGLWindow *
gst_gl_window_new (gulong external_gl_context)
{
  GstGLWindow *window = g_object_new (GST_GL_TYPE_WINDOW, NULL);
  GstGLWindowPrivate *priv = window->priv;
  GstGLWindowClass *klass = GST_GL_WINDOW_GET_CLASS (window);

  HINSTANCE hinstance = GetModuleHandle (NULL);

  static gint x = 0;
  static gint y = 0;

  x += 20;
  y += 20;

  priv->internal_win_id = 0;
  priv->display = 0;
  priv->surface = 0;
  priv->gl_context = 0;
  priv->external_gl_context = (EGLContext) external_gl_context;
  priv->draw_cb = NULL;
  priv->draw_data = NULL;
  priv->resize_cb = NULL;
  priv->resize_data = NULL;
  priv->close_cb = NULL;
  priv->close_data = NULL;
  priv->is_closed = FALSE;
  priv->visible = FALSE;

  priv->internal_win_id = CreateWindow ("GSTGL", "OpenGL renderer", WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,    //WS_POPUP | WS_BORDER | WS_SYSMENU | WS_CAPTION
      x, y, 0, 0, (HWND) NULL, (HMENU) NULL, hinstance, window);

  if (!priv->internal_win_id) {
    g_debug ("failed to create gl window: %d\n", priv->internal_win_id);
    return NULL;
  }

  g_debug ("gl window created: %d\n", priv->internal_win_id);

  //display is set in the window_proc
  if (!priv->display) {
    g_object_unref (G_OBJECT (window));
    return NULL;
  }

  ShowCursor (TRUE);

  return window;
}

GQuark
gst_gl_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error");
}

gulong
gst_gl_window_get_internal_gl_context (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = window->priv;
  return (gulong) priv->gl_context;
}

void
callback_activate_gl_context (GstGLWindowPrivate * priv)
{
  if (!eglMakeCurrent (priv->display, priv->surface, priv->surface,
          priv->gl_context))
    g_debug ("failed to activate opengl context %lud\n", GetLastError ());
}

void
callback_inactivate_gl_context (GstGLWindowPrivate * priv)
{
  if (!eglMakeCurrent (priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
          EGL_NO_CONTEXT))
    g_debug ("failed to inactivate opengl context %lud\n", GetLastError ());
}

void
gst_gl_window_activate_gl_context (GstGLWindow * window, gboolean activate)
{
  GstGLWindowPrivate *priv = window->priv;
  if (activate)
    gst_gl_window_send_message (window, callback_activate_gl_context, priv);
  else
    gst_gl_window_send_message (window, callback_inactivate_gl_context, priv);
}

void
gst_gl_window_set_external_window_id (GstGLWindow * window, gulong id)
{
  GstGLWindowPrivate *priv = window->priv;

  //retrieve parent if previously set
  HWND parent_id = GetProp (priv->internal_win_id, "gl_window_parent_id");

  if (priv->visible) {
    ShowWindow (priv->internal_win_id, SW_HIDE);
    priv->visible = FALSE;
  }

  if (parent_id) {
    WNDPROC parent_proc = GetProp (parent_id, "gl_window_parent_proc");

    g_debug ("release parent %lud\n", (gulong) parent_id);

    g_assert (parent_proc);

    SetWindowLongPtr (parent_id, GWL_WNDPROC, (LONG) parent_proc);
    SetParent (priv->internal_win_id, NULL);

    RemoveProp (parent_id, "gl_window_parent_proc");
    RemoveProp (priv->internal_win_id, "gl_window_parent_id");
  }
  //not 0
  if (id) {
    WNDPROC window_parent_proc =
        (WNDPROC) GetWindowLongPtr ((HWND) id, GWL_WNDPROC);
    RECT rect;

    g_debug ("set parent %lud\n", (gulong) id);

    SetProp (priv->internal_win_id, "gl_window_parent_id", (HWND) id);
    SetProp ((HWND) id, "gl_window_id", priv->internal_win_id);
    SetProp ((HWND) id, "gl_window_parent_proc", (WNDPROC) window_parent_proc);
    SetWindowLongPtr ((HWND) id, GWL_WNDPROC, (LONG_PTR) sub_class_proc);

    SetWindowLongPtr (priv->internal_win_id, GWL_STYLE, WS_CHILD | WS_MAXIMIZE);
    SetParent (priv->internal_win_id, (HWND) id);

    //take changes into account: SWP_FRAMECHANGED
    GetClientRect ((HWND) id, &rect);
    SetWindowPos (priv->internal_win_id, HWND_TOP, rect.left, rect.top,
        rect.right, rect.bottom,
        SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
        SWP_FRAMECHANGED | SWP_NOACTIVATE);
    MoveWindow (priv->internal_win_id, rect.left, rect.top, rect.right,
        rect.bottom, FALSE);
  } else {
    //no parent so the internal window needs borders and system menu
    SetWindowLongPtr (priv->internal_win_id, GWL_STYLE,
        WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW);
  }
}

/* Must be called in the gl thread */
void
gst_gl_window_set_draw_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->draw_cb = callback;
  priv->draw_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_resize_callback (GstGLWindow * window,
    GstGLWindowCB2 callback, gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->resize_cb = callback;
  priv->resize_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_close_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->close_cb = callback;
  priv->close_data = data;
}

void
gst_gl_window_draw_unlocked (GstGLWindow * window, gint width, gint height)
{
  gst_gl_window_draw (window, width, height);
}

/* Thread safe */
void
gst_gl_window_draw (GstGLWindow * window, gint width, gint height)
{
  GstGLWindowPrivate *priv = window->priv;

  if (!priv->visible) {
    HWND parent_id = GetProp (priv->internal_win_id, "gl_window_parent_id");
    /* if no parent the real size has to be set now because this has not been done
     * when at window creation */
    if (!parent_id) {
      RECT rect;
      GetClientRect (priv->internal_win_id, &rect);
      width += 2 * GetSystemMetrics (SM_CXSIZEFRAME);
      height +=
          2 * GetSystemMetrics (SM_CYSIZEFRAME) +
          GetSystemMetrics (SM_CYCAPTION);
      MoveWindow (priv->internal_win_id, rect.left, rect.top, width, height,
          FALSE);
    }
    ShowWindowAsync (priv->internal_win_id, SW_SHOW);
    priv->visible = TRUE;
  }

  RedrawWindow (priv->internal_win_id, NULL, NULL,
      RDW_NOERASE | RDW_INTERNALPAINT | RDW_INVALIDATE);
}

void
gst_gl_window_run_loop (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = window->priv;
  gboolean running = TRUE;
  gboolean bRet = FALSE;
  MSG msg;

  g_debug ("begin loop\n");

  while (running && (bRet = GetMessage (&msg, NULL, 0, 0)) != 0) {
    if (bRet == -1) {
      g_error ("Failed to get message %lud\n", GetLastError ());
      running = FALSE;
    } else {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }
  }

  g_debug ("end loop\n");
}

/* Thread safe */
void
gst_gl_window_quit_loop (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  if (window) {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = PostMessage (priv->internal_win_id, WM_GST_GL_WINDOW_QUIT,
        (WPARAM) data, (LPARAM) callback);
    g_assert (SUCCEEDED (res));
    g_debug ("end loop requested\n");
  }
}

/* Thread safe */
void
gst_gl_window_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  if (window) {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = SendMessage (priv->internal_win_id, WM_GST_GL_WINDOW_CUSTOM,
        (WPARAM) data, (LPARAM) callback);
    g_assert (SUCCEEDED (res));
  }
}

LRESULT CALLBACK
window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_CREATE) {

    GstGLWindow *window =
        (GstGLWindow *) (((LPCREATESTRUCT) lParam)->lpCreateParams);

    g_debug ("WM_CREATE\n");

    g_assert (window);

    {
      EGLint majorVersion;
      EGLint minorVersion;
      EGLint numConfigs;
      EGLConfig config;
      EGLint contextAttribs[] =
          { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE, EGL_NONE };

      EGLint attribList[] = {
        EGL_RED_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_BLUE_SIZE, 5,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 8,
        EGL_STENCIL_SIZE, 8,
        EGL_SAMPLE_BUFFERS, EGL_DONT_CARE,      //1
        EGL_NONE
      };

      GstGLWindowPrivate *priv = window->priv;

      priv->display = eglGetDisplay (GetDC (hWnd));
      if (priv->display != EGL_NO_DISPLAY)
        g_debug ("display retrieved: %d\n", priv->display);
      else
        g_debug ("failed to retrieve display %d, %s\n", hWnd,
            EGLErrorString ());

      if (eglInitialize (priv->display, &majorVersion, &minorVersion))
        g_debug ("egl initialized: %d.%d\n", majorVersion, minorVersion);
      else
        g_debug ("failed to initialize egl %d, %s\n", priv->display,
            EGLErrorString ());

      if (eglGetConfigs (priv->display, NULL, 0, &numConfigs))
        g_debug ("configs retrieved: %d\n", numConfigs);
      else
        g_debug ("failed to retrieve configs %d, %s\n", priv->display,
            EGLErrorString ());

      if (eglChooseConfig (priv->display, attribList, &config, 1, &numConfigs))
        g_debug ("config set: %d, %d\n", config, numConfigs);
      else
        g_debug ("failed to set config %d, %s\n", priv->display,
            EGLErrorString ());

      priv->surface =
          eglCreateWindowSurface (priv->display, config,
          (EGLNativeWindowType) hWnd, NULL);
      if (priv->surface != EGL_NO_SURFACE)
        g_debug ("surface created: %d\n", priv->surface);
      else
        g_debug ("failed to create surface %d, %d, %d, %s\n", priv->display,
            priv->surface, hWnd, EGLErrorString ());

      priv->gl_context =
          eglCreateContext (priv->display, config, priv->external_gl_context,
          contextAttribs);
      if (priv->gl_context != EGL_NO_CONTEXT)
        g_debug ("gl context created: %lud, external: %lud\n",
            (gulong) priv->gl_context, (gulong) priv->external_gl_context);
      else
        g_debug
            ("failed to create glcontext %lud, extenal: %lud, win: %lud, %s\n",
            (gulong) priv->gl_context, (gulong) priv->external_gl_context,
            (gulong) hWnd, EGLErrorString ());

      ReleaseDC (hWnd, priv->display);

      if (!eglMakeCurrent (priv->display, priv->surface, priv->surface,
              priv->gl_context))
        g_debug ("failed to make opengl context current %d, %s\n", hWnd,
            EGLErrorString ());
    }

    SetProp (hWnd, "gl_window", window);

    return 0;
  } else if (GetProp (hWnd, "gl_window")) {

    GstGLWindow *window = GetProp (hWnd, "gl_window");
    GstGLWindowPrivate *priv = NULL;

    g_assert (window);

    priv = window->priv;

    g_assert (priv);

    g_assert (priv->internal_win_id == hWnd);

    g_assert (priv->gl_context == eglGetCurrentContext ());

    switch (uMsg) {

      case WM_SIZE:
      {
        if (priv->resize_cb)
          priv->resize_cb (priv->resize_data, LOWORD (lParam), HIWORD (lParam));
        break;
      }

      case WM_PAINT:
      {
        if (priv->draw_cb) {
          priv->draw_cb (priv->draw_data);
          eglSwapBuffers (priv->display, priv->surface);
          ValidateRect (hWnd, NULL);
        }
        break;
      }

      case WM_CLOSE:
      {
        ShowWindowAsync (priv->internal_win_id, SW_HIDE);

        if (priv->close_cb)
          priv->close_cb (priv->close_data);

        priv->draw_cb = NULL;
        priv->draw_data = NULL;
        priv->resize_cb = NULL;
        priv->resize_data = NULL;
        priv->close_cb = NULL;
        priv->close_data = NULL;
        break;
      }

      case WM_GST_GL_WINDOW_QUIT:
      {
        HWND parent_id = 0;
        GstGLWindowCB destroy_cb = (GstGLWindowCB) lParam;

        g_debug ("WM_CLOSE\n");

        destroy_cb ((gpointer) wParam);

        parent_id = GetProp (hWnd, "gl_window_parent_id");
        if (parent_id) {
          WNDPROC parent_proc = GetProp (parent_id, "gl_window_parent_proc");

          g_assert (parent_proc);

          SetWindowLongPtr (parent_id, GWL_WNDPROC,
              (LONG) (guint64) parent_proc);
          SetParent (hWnd, NULL);

          RemoveProp (parent_id, "gl_window_parent_proc");
          RemoveProp (hWnd, "gl_window_parent_id");
        }

        priv->is_closed = TRUE;
        RemoveProp (hWnd, "gl_window");

        if (!eglMakeCurrent (priv->display, priv->surface, priv->surface,
                EGL_NO_CONTEXT))
          g_debug ("failed to make current null context %d, %s\n",
              priv->display, EGLErrorString ());

        if (priv->gl_context) {
          if (!eglDestroyContext (priv->display, priv->gl_context))
            g_debug ("failed to destroy context %d, %s\n", priv->gl_context,
                EGLErrorString ());
          priv->gl_context = NULL;
        }

        if (priv->surface) {
          if (!eglDestroySurface (priv->display, priv->surface))
            g_debug ("failed to destroy surface %d, %s\n", priv->surface,
                EGLErrorString ());
          priv->surface = NULL;
        }

        if (priv->surface) {
          if (!eglTerminate (priv->display))
            g_debug ("failed to terminate display %d, %s\n", priv->display,
                EGLErrorString ());
          priv->surface = NULL;
        }

        if (priv->internal_win_id) {
          if (!DestroyWindow (priv->internal_win_id))
            g_debug ("failed to destroy window %lud, %lud\n", (gulong) hWnd,
                GetLastError ());
        }

        PostQuitMessage (0);
        break;
      }

      case WM_CAPTURECHANGED:
      {
        g_debug ("WM_CAPTURECHANGED\n");
        if (priv->draw_cb)
          priv->draw_cb (priv->draw_data);
        break;
      }

      case WM_GST_GL_WINDOW_CUSTOM:
      {
        if (!priv->is_closed) {
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
        HWND parent_id = GetProp (hWnd, "gl_window_parent_id");
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

const gchar *
EGLErrorString ()
{
  EGLint nErr = eglGetError ();
  switch (nErr) {
    case EGL_SUCCESS:
      return "EGL_SUCCESS";
    case EGL_BAD_DISPLAY:
      return "EGL_BAD_DISPLAY";
    case EGL_NOT_INITIALIZED:
      return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
      return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
      return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
      return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
      return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
      return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:
      return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_MATCH:
      return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:
      return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
      return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:
      return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:
      return "EGL_BAD_SURFACE";
    default:
      return "unknown";
  }
}
