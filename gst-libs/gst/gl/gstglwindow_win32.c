/* 
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#undef UNICODE
#include <windows.h>
#define UNICODE

#include "gstglwindow.h"


#define WM_GSTGLWINDOW (WM_APP+1)

void gst_gl_window_set_pixel_format (GstGLWindow *window);
LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define GST_GL_WINDOW_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))

enum
{
  PROP_0
};

struct _GstGLWindowPrivate
{
  HWND internal_win_id;
  HDC device;
  HGLRC gl_context;
  GstGLWindowCB draw_cb;
  gpointer draw_data;
  GstGLWindowCB2 resize_cb;
  gpointer resize_data;
  GstGLWindowCB close_cb;
  gpointer close_data;
  gboolean is_closed;
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
gst_gl_window_log_handler (const gchar *domain, GLogLevelFlags flags,
                           const gchar *message, gpointer user_data)
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

  g_type_class_add_private (klass, sizeof (GstGLWindowPrivate));

  obj_class->finalize = gst_gl_window_finalize;
}

static void
gst_gl_window_init (GstGLWindow *window)
{
  window->priv = GST_GL_WINDOW_GET_PRIVATE (window);

  if (g_getenv ("GST_GL_WINDOW_DEBUG") != NULL)
    _gst_gl_window_debug = TRUE;

  g_log_set_handler ("GstGLWindow", G_LOG_LEVEL_DEBUG,
    gst_gl_window_log_handler, NULL);
}

/* Must be called in the gl thread */
GstGLWindow *
gst_gl_window_new (gint width, gint height)
{
  GstGLWindow *window = g_object_new (GST_GL_TYPE_WINDOW, NULL);
  GstGLWindowPrivate *priv = window->priv;
  GstGLWindowClass* klass = GST_GL_WINDOW_GET_CLASS (window);

  WNDCLASS wc;
  ATOM atom = 0;
  HINSTANCE hinstance = GetModuleHandle (NULL);

  static gint x = 0;
  static gint y = 0;

  atom = GetClassInfo (hinstance, "GSTGL", &wc);

  if (atom == 0)
  {
    ZeroMemory (&wc, sizeof(WNDCLASS));

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
      g_error ("Failed to register window class %x\r\n", GetLastError());
  }

  x += 20;
  y += 20;

  priv->internal_win_id = 0;
  priv->device = 0;
  priv->gl_context = 0;
  priv->draw_cb = NULL;
  priv->draw_data = NULL;
  priv->resize_cb = NULL;
  priv->resize_data = NULL;
  priv->close_cb = NULL;
  priv->close_data = NULL;
  priv->is_closed = FALSE;

  width += 2 * GetSystemMetrics (SM_CXSIZEFRAME);
  height += 2 * GetSystemMetrics (SM_CYSIZEFRAME) + GetSystemMetrics (SM_CYCAPTION);

  priv->internal_win_id = CreateWindowEx (
    0,
    "GSTGL",
    "OpenGL renderer",
    WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
    x, y, width, height,
    (HWND) NULL,
    (HMENU) NULL,
    hinstance,
    window
  );

  if (!priv->internal_win_id)
  {
    g_debug ("failed to create gl window: %d\n", priv->internal_win_id);
    return NULL;
  }

  g_debug ("gl window created: %d\n", priv->internal_win_id);

  //device is set in the window_proc 
  g_assert (priv->device);

  ShowCursor (TRUE);

  return window;
}

GQuark
gst_gl_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error");
}

void 
gst_gl_window_set_external_window_id (GstGLWindow *window, guint64 id)
{
  GstGLWindowPrivate *priv = window->priv;
  WNDPROC window_parent_proc = (WNDPROC) (guint64) GetWindowLongPtr((HWND)id, GWL_WNDPROC);
  RECT rect;

  SetProp (priv->internal_win_id, "gl_window_parent_id", (HWND)id);
  SetProp ((HWND)id, "gl_window_id", priv->internal_win_id);
  SetProp ((HWND)id, "gl_window_parent_proc", (WNDPROC) window_parent_proc);
  SetWindowLongPtr ((HWND)id, GWL_WNDPROC, (DWORD) (guint64) sub_class_proc);
  
  SetWindowLongPtr (priv->internal_win_id, GWL_STYLE, WS_CHILD | WS_MAXIMIZE);
  SetParent (priv->internal_win_id, (HWND)id);  

  //take changes into account: SWP_FRAMECHANGED
  GetClientRect ((HWND)id, &rect);
  SetWindowPos (priv->internal_win_id, HWND_TOP, rect.left, rect.top, rect.right, rect.bottom,
    SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
  MoveWindow (priv->internal_win_id, rect.left, rect.top, rect.right, rect.bottom, FALSE);
}

void 
gst_gl_window_set_external_gl_context (GstGLWindow *window, guint64 context)
{
  g_warning ("gst_gl_window_set_external_gl_context: not implemented\n");
}

/* Must be called in the gl thread */
void
gst_gl_window_set_draw_callback (GstGLWindow *window, GstGLWindowCB callback, gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;
  
  priv->draw_cb = callback;
  priv->draw_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_resize_callback (GstGLWindow *window, GstGLWindowCB2 callback , gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->resize_cb = callback;
  priv->resize_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_close_callback (GstGLWindow *window, GstGLWindowCB callback, gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->close_cb = callback;
  priv->close_data = data;
}

/* Thread safe */
void
gst_gl_window_visible (GstGLWindow *window, gboolean visible)
{
  GstGLWindowPrivate *priv = window->priv;
  BOOL ret = FALSE; 

  g_debug ("set visible %d\n", priv->internal_win_id);
  
  if (visible)
    ret = ShowWindowAsync (priv->internal_win_id, SW_SHOW);
  else
    ret = ShowWindowAsync (priv->internal_win_id, SW_HIDE);
}

/* Thread safe */
void
gst_gl_window_draw (GstGLWindow *window)
{
  GstGLWindowPrivate *priv = window->priv;
  RedrawWindow (priv->internal_win_id, NULL, NULL,
    RDW_NOERASE | RDW_INTERNALPAINT | RDW_INVALIDATE);
}

void
gst_gl_window_run_loop (GstGLWindow *window)
{
  GstGLWindowPrivate *priv = window->priv;
  gboolean running = TRUE;
  gboolean bRet = FALSE;
  MSG msg;

  g_debug ("begin loop\n");

  while (running && (bRet = GetMessage (&msg, NULL, 0, 0)) != 0)
  { 
      if (bRet == -1)
      {
          g_error ("Failed to get message %x\r\n", GetLastError());
          running = FALSE;
      }
      else
      {
          TranslateMessage (&msg); 
          DispatchMessage (&msg); 
      }
  }

  g_debug ("end loop\n");
}

/* Thread safe */
void
gst_gl_window_quit_loop (GstGLWindow *window)
{
  if (window)
  {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = PostMessage(priv->internal_win_id, WM_CLOSE, 0, 0);
    g_assert (SUCCEEDED (res));
    g_debug ("end loop requested\n");
  }
}

/* Thread safe */
void
gst_gl_window_send_message (GstGLWindow *window, GstGLWindowCB callback, gpointer data)
{
  if (window)
  {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = SendMessage (priv->internal_win_id, WM_GSTGLWINDOW, (WPARAM) data, (LPARAM) callback);
    g_assert (SUCCEEDED (res));
  }
}

/* PRIVATE */

void
gst_gl_window_set_pixel_format (GstGLWindow *window)
{
    GstGLWindowPrivate *priv = window->priv;
    PIXELFORMATDESCRIPTOR pfd;
    gint pixelformat = 0;
    gboolean res = FALSE;

    pfd.nSize           = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion        = 1;
    pfd.dwFlags         = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType      = PFD_TYPE_RGBA;
    pfd.cColorBits      = 24;
    pfd.cRedBits        = 0;
    pfd.cRedShift       = 0;
    pfd.cGreenBits      = 0;
    pfd.cGreenShift     = 0;
    pfd.cBlueBits       = 0;
    pfd.cBlueShift      = 0;
    pfd.cAlphaBits      = 0;
    pfd.cAlphaShift     = 0;
    pfd.cAccumBits      = 0;
    pfd.cAccumRedBits   = 0;
    pfd.cAccumGreenBits = 0;
    pfd.cAccumBlueBits  = 0;
    pfd.cAccumAlphaBits = 0;
    pfd.cDepthBits      = 32;
    pfd.cStencilBits    = 8;
    pfd.cAuxBuffers     = 0;
    pfd.iLayerType      = PFD_MAIN_PLANE;
    pfd.bReserved       = 0;
    pfd.dwLayerMask     = 0;
    pfd.dwVisibleMask   = 0;
    pfd.dwDamageMask    = 0;

    pfd.cColorBits = (BYTE) GetDeviceCaps (priv->device, BITSPIXEL);

    pixelformat = ChoosePixelFormat (priv->device, &pfd );
    
    g_assert (pixelformat);

    res = SetPixelFormat (priv->device, pixelformat, &pfd);

    g_assert (res);
}

LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_CREATE) {

    GstGLWindow *window = (GstGLWindow *) (((LPCREATESTRUCT) lParam)->lpCreateParams);

    g_debug ("WM_CREATE\n");

    g_assert (window);

    {
      GstGLWindowPrivate *priv = window->priv;
      priv->device = GetDC (hWnd);
      gst_gl_window_set_pixel_format (window);
      priv->gl_context = wglCreateContext (priv->device);
      if (priv->gl_context)
        g_debug ("gl context created: %d\n", priv->gl_context);
      else
        g_debug ("failed to create glcontext %d, %x\r\n", hWnd, GetLastError());
      g_assert (priv->gl_context);
      ReleaseDC (hWnd, priv->device);
      if (!wglMakeCurrent (priv->device, priv->gl_context))
        g_debug ("failed to make opengl context current %d, %x\r\n", hWnd, GetLastError());
    }

    SetProp (hWnd, "gl_window", window);

    return 0;
  }
  else if (GetProp(hWnd, "gl_window")) {

    GstGLWindow *window = GetProp(hWnd, "gl_window");
    GstGLWindowPrivate *priv = NULL;

    g_assert (window);

    priv = window->priv;

    g_assert (priv);

    g_assert (priv->internal_win_id == hWnd);

    g_assert (priv->gl_context == wglGetCurrentContext());

    switch ( uMsg ) {

      case WM_SIZE:
      {
        if (priv->resize_cb)
          priv->resize_cb (priv->resize_data, LOWORD(lParam), HIWORD(lParam));
        break;
      }

      case WM_PAINT:
      { 
        if (priv->draw_cb)
        {
          PAINTSTRUCT ps;
          BeginPaint (hWnd, &ps);
          priv->draw_cb (priv->draw_data);
          SwapBuffers (priv->device);
          EndPaint (hWnd, &ps);
        }
        break;
      }

      case WM_CLOSE:
      {
        HWND parent_id = 0;

        g_debug ("WM_CLOSE\n");

        parent_id = GetProp (hWnd, "gl_window_parent_id");
        if (parent_id)
        {
          WNDPROC parent_proc = GetProp (parent_id, "gl_window_parent_proc");
          
          g_assert (parent_proc);

          SetWindowLongPtr (parent_id, GWL_WNDPROC, (LONG) (guint64) parent_proc);
          SetParent (hWnd, NULL);

          RemoveProp (parent_id, "gl_window_parent_proc");
          RemoveProp (hWnd, "gl_window_parent_id");
        }

        priv->is_closed = TRUE;
        RemoveProp (hWnd, "gl_window");

        if (!wglMakeCurrent (NULL, NULL))
          g_debug ("failed to make current %d, %x\r\n", hWnd, GetLastError());

        if (priv->gl_context)
        {
          if (!wglDeleteContext (priv->gl_context))
            g_debug ("failed to destroy context %d, %x\r\n", priv->gl_context, GetLastError());
        }

        if (priv->internal_win_id)
        {
          g_debug ("BEFORE\n");
          if (!DestroyWindow(priv->internal_win_id))
            g_debug ("failed to destroy window %d, %x\r\n", hWnd, GetLastError());
          g_debug ("AFTER\n");
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

      case WM_GSTGLWINDOW:
      {
        if (priv->is_closed && priv->close_cb)
          priv->close_cb (priv->close_data);
        else
        {
          GstGLWindowCB custom_cb = (GstGLWindowCB) lParam;
          custom_cb ((gpointer) wParam);
        }
        break;
      }

      case WM_ERASEBKGND:
        return TRUE;

      default:
        return DefWindowProc( hWnd, uMsg, wParam, lParam );
    }

    return 0;
  }
  else
    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC window_parent_proc = GetProp (hWnd, "gl_window_parent_proc");
  
  if (uMsg == WM_SIZE)
  {
    HWND gl_window_id = GetProp (hWnd, "gl_window_id");
    MoveWindow (gl_window_id, 0, 0, LOWORD(lParam), HIWORD(lParam), FALSE);
  }

  return CallWindowProc (window_parent_proc, hWnd, uMsg, wParam, lParam);
}

