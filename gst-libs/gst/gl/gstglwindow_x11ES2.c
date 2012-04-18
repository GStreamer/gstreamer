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

#include <locale.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define GST_GL_WINDOW_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))


/* A gl window is created and deleted in a thread dedicated to opengl calls
   The name contains "window" because an opengl context is used in cooperation
   with a window */

const gchar *EGLErrorString ();

enum
{
  ARG_0,
  ARG_DISPLAY
};

struct _GstGLWindowPrivate
{
  /* X is not thread safe */
  GMutex *x_lock;
  GCond *cond_send_message;
  gboolean running;
  gboolean visible;
  gboolean allow_extra_expose_events;

  /* X context */
  gchar *display_name;
  Display *device;
  gint screen_num;
  Window root;
  gint depth;
  gint device_width;
  gint device_height;
  gint connection;
  XVisualInfo *visual_info;
  Window parent;
  Window internal_win_id;

  /* We use a specific connection to send events */
  Display *disp_send;

  /* EGL */
  EGLContext gl_context;
  EGLDisplay gl_display;
  EGLSurface gl_surface;

  /* frozen callbacks */
  GstGLWindowCB draw_cb;
  gpointer draw_data;
  GstGLWindowCB2 resize_cb;
  gpointer resize_data;
  GstGLWindowCB close_cb;
  gpointer close_data;
};

G_DEFINE_TYPE (GstGLWindow, gst_gl_window, G_TYPE_OBJECT);

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GstGLWindow"

gboolean _gst_gl_window_debug = FALSE;

void
gst_gl_window_init_platform ()
{
}

/* Must be called in the gl thread */
static void
gst_gl_window_finalize (GObject * object)
{
  GstGLWindow *window = GST_GL_WINDOW (object);
  GstGLWindowPrivate *priv = window->priv;
  XEvent event;
  Bool ret = TRUE;

  g_mutex_lock (priv->x_lock);

  g_debug ("about to finalize gl window\n");

  priv->parent = 0;

  if (priv->device)
    XUnmapWindow (priv->device, priv->internal_win_id);

  if (priv->gl_context) {
    ret =
        eglMakeCurrent (priv->device, EGL_NO_SURFACE, EGL_NO_SURFACE,
        EGL_NO_CONTEXT);
    if (!ret)
      g_debug ("failed to release opengl context\n");

    eglDestroyContext (priv->device, priv->gl_context);
  }

  if (priv->device)
    eglTerminate (priv->device);

  XFree (priv->visual_info);

  XReparentWindow (priv->device, priv->internal_win_id, priv->root, 0, 0);

  XDestroyWindow (priv->device, priv->internal_win_id);

  XSync (priv->device, FALSE);

  while (XPending (priv->device))
    XNextEvent (priv->device, &event);

  XSetCloseDownMode (priv->device, DestroyAll);

  /*XAddToSaveSet (display, w)
     Display *display;
     Window w; */

  //FIXME: it seems it causes destroy all created windows, even by other display connection:
  //This is case in: gst-launch-0.10 videotestsrc ! tee name=t t. ! queue ! glimagesink t. ! queue ! glimagesink
  //When the first window is closed and so its display is closed by the following line, then the other Window managed by the
  //other glimagesink, is not useable and so each opengl call causes a segmentation fault.
  //Maybe the solution is to use: XAddToSaveSet
  //The following line is commented to avoid the disagreement explained before.
  //XCloseDisplay (priv->device);

  g_debug ("display receiver closed\n");

  XCloseDisplay (priv->disp_send);

  g_debug ("display sender closed\n");

  if (priv->cond_send_message) {
    g_cond_free (priv->cond_send_message);
    priv->cond_send_message = NULL;
  }

  g_mutex_unlock (priv->x_lock);

  if (priv->x_lock) {
    g_mutex_free (priv->x_lock);
    priv->x_lock = NULL;
  }

  G_OBJECT_CLASS (gst_gl_window_parent_class)->finalize (object);
}

static void
gst_gl_window_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLWindow *window;
  GstGLWindowPrivate *priv;

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
  GstGLWindowPrivate *priv;

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
gst_gl_window_log_handler (const gchar * domain, GLogLevelFlags flags,
    const gchar * message, gpointer user_data)
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
gst_gl_window_init (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = NULL;
  window->priv = GST_GL_WINDOW_GET_PRIVATE (window);
  priv = window->priv;

  if (g_getenv ("GST_GL_WINDOW_DEBUG") != NULL)
    _gst_gl_window_debug = TRUE;

  g_log_set_handler ("GstGLWindow", G_LOG_LEVEL_DEBUG,
      gst_gl_window_log_handler, NULL);

  priv->x_lock = NULL;
  priv->cond_send_message = NULL;
  priv->running = FALSE;
  priv->visible = FALSE;
  priv->allow_extra_expose_events = FALSE;

  /* X context */
  priv->display_name = NULL;
  priv->device = NULL;
  priv->screen_num = 0;
  priv->root = 0;
  priv->depth = 0;
  priv->device_width = 0;
  priv->device_height = 0;
  priv->connection = 0;
  priv->visual_info = NULL;
  priv->parent = 0;
  priv->internal_win_id = 0;

  /* We use a specific connection to send events */
  priv->disp_send = NULL;

  /* EGL */
  priv->gl_context = EGL_NO_CONTEXT;
  priv->gl_display = 0;
  priv->gl_surface = EGL_NO_SURFACE;

  /* frozen callbacks */
  priv->draw_cb = NULL;
  priv->draw_data = NULL;
  priv->resize_cb = NULL;
  priv->resize_data = NULL;
  priv->close_cb = NULL;
  priv->close_data = NULL;
}

/* Must be called in the gl thread */
GstGLWindow *
gst_gl_window_new (gulong external_gl_context)
{
  GstGLWindow *window = g_object_new (GST_GL_TYPE_WINDOW, NULL);
  GstGLWindowPrivate *priv = window->priv;

  EGLint config_attrib[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_DEPTH_SIZE, 16,
    EGL_NONE
  };

  EGLint context_attrib[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  EGLint majorVersion;
  EGLint minorVersion;
  EGLint numConfigs;
  EGLConfig config;

  XSetWindowAttributes win_attr;
  XTextProperty text_property;
  XWMHints wm_hints;
  unsigned long mask;
  const gchar *title = "OpenGL renderer";
  Atom wm_atoms[3];

  static gint x = 0;
  static gint y = 0;

  setlocale (LC_NUMERIC, "C");

  priv->x_lock = g_mutex_new ();
  priv->cond_send_message = g_cond_new ();
  priv->running = TRUE;
  priv->visible = FALSE;
  priv->parent = 0;
  priv->allow_extra_expose_events = TRUE;

  g_mutex_lock (priv->x_lock);

  priv->device = XOpenDisplay (priv->display_name);

  XSynchronize (priv->device, FALSE);

  g_debug ("gl device id: %ld\n", (gulong) priv->device);

  priv->disp_send = XOpenDisplay (priv->display_name);

  XSynchronize (priv->disp_send, FALSE);

  g_debug ("gl display sender: %ld\n", (gulong) priv->disp_send);

  priv->screen_num = DefaultScreen (priv->device);
  priv->root = RootWindow (priv->device, priv->screen_num);
  priv->depth = DefaultDepth (priv->device, priv->screen_num);

  g_debug ("gl root id: %lud\n", (gulong) priv->root);

  priv->device_width = DisplayWidth (priv->device, priv->screen_num);
  priv->device_height = DisplayHeight (priv->device, priv->screen_num);

  priv->visual_info = g_new0 (XVisualInfo, 1);
  XMatchVisualInfo (priv->device, priv->screen_num, priv->depth, TrueColor,
      priv->visual_info);

  win_attr.event_mask =
      StructureNotifyMask | ExposureMask | VisibilityChangeMask;
  win_attr.do_not_propagate_mask = NoEventMask;

  win_attr.background_pixmap = None;
  win_attr.background_pixel = 0;
  win_attr.border_pixel = 0;

  win_attr.colormap =
      XCreateColormap (priv->device, priv->root, priv->visual_info->visual,
      AllocNone);

  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  x += 20;
  y += 20;

  priv->internal_win_id = XCreateWindow (priv->device, priv->root, x, y,
      1, 1, 0, priv->visual_info->depth, InputOutput,
      priv->visual_info->visual, mask, &win_attr);

  if (!priv->internal_win_id) {
    g_debug ("XCreateWindow failed\n");
    goto failure;
  }

  XSync (priv->device, FALSE);

  XSetWindowBackgroundPixmap (priv->device, priv->internal_win_id, None);

  g_debug ("gl window id: %lud\n", (gulong) priv->internal_win_id);

  g_debug ("gl window props: x:%d y:%d\n", x, y);

  wm_atoms[0] = XInternAtom (priv->device, "WM_DELETE_WINDOW", True);
  if (wm_atoms[0] == None)
    g_debug ("Cannot create WM_DELETE_WINDOW\n");

  wm_atoms[1] = XInternAtom (priv->device, "WM_GL_WINDOW", False);
  if (wm_atoms[1] == None)
    g_debug ("Cannot create WM_GL_WINDOW\n");

  wm_atoms[2] = XInternAtom (priv->device, "WM_QUIT_LOOP", False);
  if (wm_atoms[2] == None)
    g_debug ("Cannot create WM_QUIT_LOOP\n");

  XSetWMProtocols (priv->device, priv->internal_win_id, wm_atoms, 2);

  wm_hints.flags = StateHint;
  wm_hints.initial_state = NormalState;
  wm_hints.input = False;

  XStringListToTextProperty ((char **) &title, 1, &text_property);

  XSetWMProperties (priv->device, priv->internal_win_id, &text_property,
      &text_property, 0, 0, NULL, &wm_hints, NULL);

  XFree (text_property.value);

  priv->gl_display = eglGetDisplay ((EGLNativeDisplayType) priv->device);

  if (eglInitialize (priv->gl_display, &majorVersion, &minorVersion))
    g_debug ("egl initialized: %d.%d\n", majorVersion, minorVersion);
  else {
    g_debug ("failed to initialize egl %ld, %s\n", (gulong) priv->gl_display,
        EGLErrorString ());
    goto failure;
  }

  if (eglChooseConfig (priv->gl_display, config_attrib, &config, 1,
          &numConfigs))
    g_debug ("config set: %ld, %ld\n", (gulong) config, (gulong) numConfigs);
  else {
    g_debug ("failed to set config %ld, %s\n", (gulong) priv->gl_display,
        EGLErrorString ());
    goto failure;
  }

  priv->gl_surface =
      eglCreateWindowSurface (priv->gl_display, config,
      (EGLNativeWindowType) priv->internal_win_id, NULL);
  if (priv->gl_surface != EGL_NO_SURFACE)
    g_debug ("surface created: %ld\n", (gulong) priv->gl_surface);
  else {
    g_debug ("failed to create surface %ld, %ld, %ld, %s\n",
        (gulong) priv->gl_display, (gulong) priv->gl_surface,
        (gulong) priv->gl_display, EGLErrorString ());
    goto failure;
  }

  g_debug ("about to create gl context\n");

  priv->gl_context =
      eglCreateContext (priv->gl_display, config,
      (EGLContext) (guint) external_gl_context, context_attrib);

  if (priv->gl_context != EGL_NO_CONTEXT)
    g_debug ("gl context created: %ld\n", (gulong) priv->gl_context);
  else {
    g_debug ("failed to create glcontext %ld, %ld, %s\n",
        (gulong) priv->gl_context, (gulong) priv->gl_display,
        EGLErrorString ());
    goto failure;
  }

  if (!eglMakeCurrent (priv->gl_display, priv->gl_surface, priv->gl_surface,
          priv->gl_context)) {
    g_debug ("failed to make opengl context current %ld, %s\n",
        (gulong) priv->gl_display, EGLErrorString ());
    goto failure;
  }

  g_mutex_unlock (priv->x_lock);

  return window;

failure:
  g_mutex_unlock (priv->x_lock);
  g_object_unref (G_OBJECT (window));
  return NULL;
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
  if (!eglMakeCurrent (priv->gl_display, priv->gl_surface, priv->gl_surface,
          priv->gl_context))
    g_debug ("failed to activate opengl context %lud\n",
        (gulong) priv->gl_context);
}

void
callback_inactivate_gl_context (GstGLWindowPrivate * priv)
{
  if (!eglMakeCurrent (priv->device, EGL_NO_SURFACE, EGL_NO_SURFACE,
          EGL_NO_CONTEXT))
    g_debug ("failed to inactivate opengl context %lud\n",
        (gulong) priv->gl_context);
}

void
gst_gl_window_activate_gl_context (GstGLWindow * window, gboolean activate)
{
  GstGLWindowPrivate *priv = window->priv;
  if (activate)
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (callback_activate_gl_context), priv);
  else
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (callback_inactivate_gl_context), priv);
}

/* Not called by the gl thread */
void
gst_gl_window_set_external_window_id (GstGLWindow * window, gulong id)
{
  if (window) {
    GstGLWindowPrivate *priv = window->priv;
    XWindowAttributes attr;

    g_mutex_lock (priv->x_lock);

    priv->parent = (Window) id;

    g_debug ("set parent window id: %lud\n", id);

    XGetWindowAttributes (priv->disp_send, priv->parent, &attr);

    XResizeWindow (priv->disp_send, priv->internal_win_id, attr.width,
        attr.height);

    XReparentWindow (priv->disp_send, priv->internal_win_id, priv->parent,
        0, 0);

    XSync (priv->disp_send, FALSE);

    g_mutex_unlock (priv->x_lock);
  }
}

void
gst_gl_window_set_draw_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  g_mutex_lock (priv->x_lock);

  priv->draw_cb = callback;
  priv->draw_data = data;

  g_mutex_unlock (priv->x_lock);
}

void
gst_gl_window_set_resize_callback (GstGLWindow * window,
    GstGLWindowCB2 callback, gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  g_mutex_lock (priv->x_lock);

  priv->resize_cb = callback;
  priv->resize_data = data;

  g_mutex_unlock (priv->x_lock);
}

void
gst_gl_window_set_close_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  g_mutex_lock (priv->x_lock);

  priv->close_cb = callback;
  priv->close_data = data;

  g_mutex_unlock (priv->x_lock);
}

/* Called in the gl thread */
void
gst_gl_window_draw_unlocked (GstGLWindow * window, gint width, gint height)
{
  GstGLWindowPrivate *priv = window->priv;

  if (priv->running && priv->allow_extra_expose_events) {
    XEvent event;
    XWindowAttributes attr;

    XGetWindowAttributes (priv->device, priv->internal_win_id, &attr);

    event.xexpose.type = Expose;
    event.xexpose.send_event = TRUE;
    event.xexpose.display = priv->device;
    event.xexpose.window = priv->internal_win_id;
    event.xexpose.x = attr.x;
    event.xexpose.y = attr.y;
    event.xexpose.width = attr.width;
    event.xexpose.height = attr.height;
    event.xexpose.count = 0;

    XSendEvent (priv->device, priv->internal_win_id, FALSE, ExposureMask,
        &event);
    XSync (priv->disp_send, FALSE);
  }
}

/* Not called by the gl thread */
void
gst_gl_window_draw (GstGLWindow * window, gint width, gint height)
{
  if (window) {
    GstGLWindowPrivate *priv = window->priv;

    g_mutex_lock (priv->x_lock);

    if (priv->running) {
      XEvent event;
      XWindowAttributes attr;

      XGetWindowAttributes (priv->disp_send, priv->internal_win_id, &attr);

      if (!priv->visible) {

        if (!priv->parent) {
          attr.width = width;
          attr.height = height;
          XResizeWindow (priv->disp_send, priv->internal_win_id,
              attr.width, attr.height);
          XSync (priv->disp_send, FALSE);
        }

        XMapWindow (priv->disp_send, priv->internal_win_id);
        priv->visible = TRUE;
      }

      if (priv->parent) {
        XWindowAttributes attr_parent;
        XGetWindowAttributes (priv->disp_send, priv->parent, &attr_parent);

        if (attr.width != attr_parent.width ||
            attr.height != attr_parent.height) {
          XMoveResizeWindow (priv->disp_send, priv->internal_win_id,
              0, 0, attr_parent.width, attr_parent.height);
          XSync (priv->disp_send, FALSE);

          attr.width = attr_parent.width;
          attr.height = attr_parent.height;

          g_debug ("parent resize:  %d, %d\n",
              attr_parent.width, attr_parent.height);
        }
      }

      event.xexpose.type = Expose;
      event.xexpose.send_event = TRUE;
      event.xexpose.display = priv->disp_send;
      event.xexpose.window = priv->internal_win_id;
      event.xexpose.x = attr.x;
      event.xexpose.y = attr.y;
      event.xexpose.width = attr.width;
      event.xexpose.height = attr.height;
      event.xexpose.count = 0;

      XSendEvent (priv->disp_send, priv->internal_win_id, FALSE, ExposureMask,
          &event);
      XSync (priv->disp_send, FALSE);
    }

    g_mutex_unlock (priv->x_lock);
  }
}

/* Called in the gl thread */
void
gst_gl_window_run_loop (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = window->priv;

  g_debug ("begin loop\n");

  g_mutex_lock (priv->x_lock);

  while (priv->running) {
    XEvent event;
    XEvent pending_event;

    g_mutex_unlock (priv->x_lock);

    /* XSendEvent (which are called in other threads) are done from another display structure */
    XNextEvent (priv->device, &event);

    g_mutex_lock (priv->x_lock);

    // use in generic/cube and other related uses
    priv->allow_extra_expose_events = XPending (priv->device) <= 2;

    switch (event.type) {
      case ClientMessage:
      {

        Atom wm_delete = XInternAtom (priv->device, "WM_DELETE_WINDOW", True);
        Atom wm_gl = XInternAtom (priv->device, "WM_GL_WINDOW", True);
        Atom wm_quit_loop = XInternAtom (priv->device, "WM_QUIT_LOOP", True);

        if (wm_delete == None)
          g_debug ("Cannot create WM_DELETE_WINDOW\n");
        if (wm_gl == None)
          g_debug ("Cannot create WM_GL_WINDOW\n");
        if (wm_quit_loop == None)
          g_debug ("Cannot create WM_QUIT_LOOP\n");

        /* Message sent with gst_gl_window_send_message */
        if (wm_gl != None && event.xclient.message_type == wm_gl) {
          if (priv->running) {
#if SIZEOF_VOID_P == 8
            GstGLWindowCB custom_cb =
                (GstGLWindowCB) (((event.xclient.data.
                        l[0] & 0xffffffff) << 32) | (event.xclient.data.
                    l[1] & 0xffffffff));
            gpointer custom_data =
                (gpointer) (((event.xclient.data.
                        l[2] & 0xffffffff) << 32) | (event.xclient.data.
                    l[3] & 0xffffffff));
#else
            GstGLWindowCB custom_cb = (GstGLWindowCB) event.xclient.data.l[0];
            gpointer custom_data = (gpointer) event.xclient.data.l[1];
#endif

            if (!custom_cb || !custom_data)
              g_debug ("custom cb not initialized\n");

            custom_cb (custom_data);
          }

          g_cond_signal (priv->cond_send_message);
        }

        /* User clicked on the cross */
        else if (wm_delete != None
            && (Atom) event.xclient.data.l[0] == wm_delete) {
          g_debug ("Close %lud\n", (gulong) priv->internal_win_id);

          if (priv->close_cb)
            priv->close_cb (priv->close_data);

          priv->draw_cb = NULL;
          priv->draw_data = NULL;
          priv->resize_cb = NULL;
          priv->resize_data = NULL;
          priv->close_cb = NULL;
          priv->close_data = NULL;
        }

        /* message sent with gst_gl_window_quit_loop */
        else if (wm_quit_loop != None
            && event.xclient.message_type == wm_quit_loop) {
#if SIZEOF_VOID_P == 8
          GstGLWindowCB destroy_cb =
              (GstGLWindowCB) (((event.xclient.data.
                      l[0] & 0xffffffff) << 32) | (event.xclient.data.
                  l[1] & 0xffffffff));
          gpointer destroy_data =
              (gpointer) (((event.xclient.data.
                      l[2] & 0xffffffff) << 32) | (event.xclient.data.
                  l[3] & 0xffffffff));
#else
          GstGLWindowCB destroy_cb = (GstGLWindowCB) event.xclient.data.l[0];
          gpointer destroy_data = (gpointer) event.xclient.data.l[1];
#endif

          g_debug ("Quit loop message %lud\n", (gulong) priv->internal_win_id);

          /* exit loop */
          priv->running = FALSE;

          /* make sure last pendings send message calls are executed */
          XFlush (priv->device);
          while (XCheckTypedEvent (priv->device, ClientMessage, &pending_event)) {
#if SIZEOF_VOID_P == 8
            GstGLWindowCB custom_cb =
                (GstGLWindowCB) (((event.xclient.data.
                        l[0] & 0xffffffff) << 32) | (event.xclient.data.
                    l[1] & 0xffffffff));
            gpointer custom_data =
                (gpointer) (((event.xclient.data.
                        l[2] & 0xffffffff) << 32) | (event.xclient.data.
                    l[3] & 0xffffffff));
#else
            GstGLWindowCB custom_cb = (GstGLWindowCB) event.xclient.data.l[0];
            gpointer custom_data = (gpointer) event.xclient.data.l[1];
#endif
            g_debug ("execute last pending custom x events\n");

            if (!custom_cb || !custom_data)
              g_debug ("custom cb not initialized\n");

            custom_cb (custom_data);

            g_cond_signal (priv->cond_send_message);
          }

          /* Finally we can destroy opengl ressources (texture/shaders/fbo) */
          if (!destroy_cb || !destroy_data)
            g_debug ("destroy cb not correclty set\n");

          destroy_cb (destroy_data);

        } else
          g_debug ("client message not reconized \n");
        break;
      }

      case CreateNotify:
      case ConfigureNotify:
      {
        if (priv->resize_cb)
          priv->resize_cb (priv->resize_data, event.xconfigure.width,
              event.xconfigure.height);
        break;
      }

      case DestroyNotify:
        g_debug ("DestroyNotify\n");
        break;

      case Expose:
        if (priv->draw_cb) {
          priv->draw_cb (priv->draw_data);
          glFlush ();
          eglSwapBuffers (priv->gl_display, priv->gl_surface);
        }
        break;

      case VisibilityNotify:
      {
        switch (event.xvisibility.state) {
          case VisibilityUnobscured:
            if (priv->draw_cb)
              priv->draw_cb (priv->draw_data);
            break;

          case VisibilityPartiallyObscured:
            if (priv->draw_cb)
              priv->draw_cb (priv->draw_data);
            break;

          case VisibilityFullyObscured:
            break;

          default:
            g_debug ("unknown xvisibility event: %d\n",
                event.xvisibility.state);
            break;
        }
        break;
      }

      default:
        g_debug ("unknown XEvent type: %ud\n", event.type);
        break;

    }                           // switch

  }                             // while running

  g_mutex_unlock (priv->x_lock);

  g_debug ("end loop\n");
}

/* Not called by the gl thread */
void
gst_gl_window_quit_loop (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  if (window) {
    GstGLWindowPrivate *priv = window->priv;

    g_mutex_lock (priv->x_lock);

    if (priv->running) {
      XEvent event;

      event.xclient.type = ClientMessage;
      event.xclient.send_event = TRUE;
      event.xclient.display = priv->disp_send;
      event.xclient.window = priv->internal_win_id;
      event.xclient.message_type =
          XInternAtom (priv->disp_send, "WM_QUIT_LOOP", True);;
      event.xclient.format = 32;
#if SIZEOF_VOID_P == 8
      event.xclient.data.l[0] = (((long) callback) >> 32) & 0xffffffff;
      event.xclient.data.l[1] = (((long) callback)) & 0xffffffff;
      event.xclient.data.l[2] = (((long) data) >> 32) & 0xffffffff;
      event.xclient.data.l[3] = (((long) data)) & 0xffffffff;
#else
      event.xclient.data.l[0] = (long) callback;
      event.xclient.data.l[1] = (long) data;
#endif

      XSendEvent (priv->disp_send, priv->internal_win_id, FALSE, NoEventMask,
          &event);
      XSync (priv->disp_send, FALSE);
    }

    g_mutex_unlock (priv->x_lock);
  }
}

/* Not called by the gl thread */
void
gst_gl_window_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  if (window) {
    GstGLWindowPrivate *priv = window->priv;

    g_mutex_lock (priv->x_lock);

    if (priv->running) {
      XEvent event;

      event.xclient.type = ClientMessage;
      event.xclient.send_event = TRUE;
      event.xclient.display = priv->disp_send;
      event.xclient.window = priv->internal_win_id;
      event.xclient.message_type =
          XInternAtom (priv->disp_send, "WM_GL_WINDOW", True);
      event.xclient.format = 32;
#if SIZEOF_VOID_P == 8
      event.xclient.data.l[0] = (((long) callback) >> 32) & 0xffffffff;
      event.xclient.data.l[1] = (((long) callback)) & 0xffffffff;
      event.xclient.data.l[2] = (((long) data) >> 32) & 0xffffffff;
      event.xclient.data.l[3] = (((long) data)) & 0xffffffff;
#else
      event.xclient.data.l[0] = (long) callback;
      event.xclient.data.l[1] = (long) data;
#endif

      XSendEvent (priv->disp_send, priv->internal_win_id, FALSE, NoEventMask,
          &event);
      XSync (priv->disp_send, FALSE);

      /* block until opengl calls have been executed in the gl thread */
      g_cond_wait (priv->cond_send_message, priv->x_lock);
    }

    g_mutex_unlock (priv->x_lock);
  }
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
