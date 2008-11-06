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

#include "gstglwindow.h"

#include <GL/glx.h>


//#define WM_GSTGLWINDOW (WM_APP+1)

//LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
//LRESULT FAR PASCAL sub_class_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define GST_GL_WINDOW_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))

enum
{
  ARG_0,
  ARG_DISPLAY
};

struct _GstGLWindowPrivate
{
  gchar *display_name;
  Display *device;
  gint screen;
  Window root;
  gint device_width;
  gint device_height;
  gint connection;
  Atom atom_delete_window;
  XVisualInfo *visual_info;

  Window internal_win_id;
  GLXContext gl_context;

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

/* Must be called in the gl thread */
static void
gst_gl_window_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_gl_window_parent_class)->finalize (object);
}

static void
gst_gl_window_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLWindow *window;
  GstGLWindowPrivate *priv = window->priv;

  g_return_if_fail (GST_GL_IS_WINDOW (object));

  window = GST_GL_WINDOW (object);

  priv = window->priv;

  switch (prop_id) {
    case ARG_DISPLAY:
      priv->display_name = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_window_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLWindow *window;
  GstGLWindowPrivate *priv = window->priv;

  g_return_if_fail (GST_GL_IS_WINDOW (object));

  window = GST_GL_WINDOW (object);

  priv = window->priv;

  switch (prop_id) {
    case ARG_DISPLAY:
      g_value_set_string (value, priv->display_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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
gst_gl_window_class_init (GstGLWindowClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstGLWindowPrivate));

  obj_class->finalize = gst_gl_window_finalize;
  obj_class->set_property = gst_gl_window_set_property;
  obj_class->get_property = gst_gl_window_get_property;

  g_object_class_install_property (obj_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

  gint attrib[] = {
    GLX_RGBA,
    GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1,
    GLX_DOUBLEBUFFER,
    GLX_DEPTH_SIZE, 1,
    None
  };

  XSetWindowAttributes win_attr;
  XTextProperty text_property;
  XSizeHints size_hints;
  XWMHints wm_hints;
  unsigned long mask;
  const gchar *title = "OpenGL renderer";

  static gint x = 0;
  static gint y = 0;

  x += 20;
  y += 20;

  priv->device = XOpenDisplay (priv->display_name);
  priv->screen = DefaultScreen (priv->device);
  priv->root = RootWindow (priv->device, priv->screen);

  priv->device_width = DisplayWidth (priv->device, priv->screen);
  priv->device_height = DisplayHeight (priv->device, priv->screen);

  priv->connection = ConnectionNumber (priv->device);
  priv->atom_delete_window = XInternAtom (priv->device, "WM_DELETE_WINDOW", FALSE);

  priv->visual_info = glXChooseVisual (priv->device, priv->screen, attrib);

  win_attr.event_mask =
    StructureNotifyMask | SubstructureNotifyMask | ExposureMask |
    ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask |
    VisibilityChangeMask | EnterWindowMask | LeaveWindowMask |
    PointerMotionMask | ButtonMotionMask;

  win_attr.background_pixmap = None;
  win_attr.background_pixel = 0;
  win_attr.border_pixel = 0;

  win_attr.colormap = XCreateColormap(priv->device, priv->root, priv->visual_info->visual, AllocNone);

  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  priv->internal_win_id = XCreateWindow (priv->device, priv->root, x, y,
    width, height, 0, priv->visual_info->depth, InputOutput,
    priv->visual_info->visual, mask, &win_attr);

  priv->gl_context = glXCreateContext (priv->device, priv->visual_info, NULL, TRUE);

  if (!glXIsDirect(priv->device, priv->gl_context))
    g_debug ("direct rendering failed\n");

  size_hints.flags = USPosition | USSize;
  size_hints.x = x;
  size_hints.y = y;
  size_hints.width = width;
  size_hints.height= height;

  wm_hints.flags = StateHint;
  wm_hints.initial_state = NormalState;

  XStringListToTextProperty ((char**)&title, 1, &text_property);

  XSetWMProperties (priv->device, priv->internal_win_id, &text_property, &text_property, 0, 0,
    &size_hints, &wm_hints, NULL);

  XFree (text_property.value);

  XSetWMProtocols (priv->device, priv->internal_win_id, &priv->atom_delete_window, 1);

  glXMakeCurrent (priv->device, priv->internal_win_id, priv->gl_context);

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
  /*stGLWindowPrivate *priv = window->priv;
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
  MoveWindow (priv->internal_win_id, rect.left, rect.top, rect.right, rect.bottom, FALSE);*/
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

  g_debug ("set visible %lld\n", (guint64) priv->internal_win_id);

  if (visible)
    XMapWindow (priv->device, priv->internal_win_id);
  else
    XUnmapWindow (priv->device, priv->internal_win_id);

  XSync(priv->device, FALSE);
}

/* Thread safe */
void
gst_gl_window_draw (GstGLWindow *window)
{
  /*GstGLWindowPrivate *priv = window->priv;
  RedrawWindow (priv->internal_win_id, NULL, NULL,
    RDW_NOERASE | RDW_INTERNALPAINT | RDW_INVALIDATE);*/
}

void
gst_gl_window_run_loop (GstGLWindow *window)
{
  GstGLWindowPrivate *priv = window->priv;
  gboolean running = TRUE;
  gboolean bRet = FALSE;
  XEvent event;

  g_debug ("begin loop\n");

  while (running && (bRet = XNextEvent(priv->device, &event)) != 0)
  {
      if (bRet == -1)
      {
          //g_error ("Failed to get message %x\r\n", GetLastError());  -> check status
          running = FALSE;
      }
      else
      {

        switch (event.type)
        {

          case ClientMessage:
          {

              if( (Atom) event.xclient.data.l[ 0 ] == priv->atom_delete_window)
              {
                //priv->is_closed = TRUE;
              }
              break;
          }

          case CreateNotify:
          case ConfigureNotify:
          {
            //int width = event.xconfigure.width;
            //int height = event.xconfigure.height;
            //call resize cb
            break;
          }

          case DestroyNotify:
            break;

          case Expose:
            //call draw cb
            break;

          case VisibilityNotify:
          {
            switch (event.xvisibility.state)
            {
              case VisibilityUnobscured:
                //call draw cb
                break;

              case VisibilityPartiallyObscured:
                //call draw cb
                break;

              case VisibilityFullyObscured:
                break;

              default:
                g_debug("unknown xvisibility event: %d\n", event.xvisibility.state);
                break;
            }
            break;
          }

          default:
            break;

        }

      }
  }

  g_debug ("end loop\n");
}

/* Thread safe */
void
gst_gl_window_quit_loop (GstGLWindow *window)
{
  /*if (window)
  {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = PostMessage(priv->internal_win_id, WM_CLOSE, 0, 0);
    g_assert (SUCCEEDED (res));
    g_debug ("end loop requested\n");
  }*/
}

/* Thread safe */
void
gst_gl_window_send_message (GstGLWindow *window, GstGLWindowCB callback, gpointer data)
{
  /*if (window)
  {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = SendMessage (priv->internal_win_id, WM_GSTGLWINDOW, (WPARAM) data, (LPARAM) callback);
    g_assert (SUCCEEDED (res));
  }*/
}

/* PRIVATE */

/*
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
*/
