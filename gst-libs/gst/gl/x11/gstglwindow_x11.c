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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <locale.h>

#include "gstglwindow_x11.h"

#if GST_GL_HAVE_PLATFORM_GLX
# include "gstglwindow_x11_glx.h"
#endif
#if GST_GL_HAVE_PLATFORM_EGL
# include "gstglwindow_x11_egl.h"
#endif

#define GST_GL_WINDOW_X11_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_X11, GstGLWindowX11Private))

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_x11_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstGLWindowX11, gst_gl_window_x11, GST_GL_TYPE_WINDOW);

/* X error trap */
static int TrappedErrorCode = 0;
static int (*old_error_handler) (Display *, XErrorEvent *);

enum
{
  ARG_0,
  ARG_DISPLAY
};

struct _GstGLWindowX11Private
{
  gboolean activate;
  gboolean activate_result;
};

guintptr gst_gl_window_x11_get_gl_context (GstGLWindow * window);
gboolean gst_gl_window_x11_activate (GstGLWindow * window, gboolean activate);
void gst_gl_window_x11_set_window_handle (GstGLWindow * window,
    guintptr handle);
void gst_gl_window_x11_draw_unlocked (GstGLWindow * window, guint width,
    guint height);
void gst_gl_window_x11_draw (GstGLWindow * window, guint width, guint height);
void gst_gl_window_x11_run (GstGLWindow * window);
void gst_gl_window_x11_quit (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data);
void gst_gl_window_x11_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);
gboolean gst_gl_window_x11_create_context (GstGLWindow * window,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error);

static gboolean gst_gl_window_x11_create_window (GstGLWindowX11 * window_x11);

/* Must be called in the gl thread */
static void
gst_gl_window_x11_finalize (GObject * object)
{
  GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (object);
  GstGLWindowX11Class *window_class = GST_GL_WINDOW_X11_GET_CLASS (window_x11);
  XEvent event;
  Bool ret = TRUE;

  GST_GL_WINDOW_LOCK (window_x11);

  window_x11->parent_win = 0;
  if (window_x11->device) {
    if (window_x11->internal_win_id)
      XUnmapWindow (window_x11->device, window_x11->internal_win_id);

    ret = window_class->activate (window_x11, FALSE);
    if (!ret)
      GST_DEBUG ("failed to release opengl context");
    window_class->destroy_context (window_x11);

    XFree (window_x11->visual_info);

    if (window_x11->internal_win_id) {
      XReparentWindow (window_x11->device, window_x11->internal_win_id,
          window_x11->root, 0, 0);
      XDestroyWindow (window_x11->device, window_x11->internal_win_id);
    }
    XSync (window_x11->device, FALSE);

    while (XPending (window_x11->device))
      XNextEvent (window_x11->device, &event);

    XSetCloseDownMode (window_x11->device, DestroyAll);

    /*XAddToSaveSet (display, w)
       Display *display;
       Window w; */

    //FIXME: it seems it causes destroy all created windows, even by other display connection:
    //This is case in: gst-launch-0.10 videotestsrc ! tee name=t t. ! queue ! glimagesink t. ! queue ! glimagesink
    //When the first window is closed and so its display is closed by the following line, then the other Window managed by the
    //other glimagesink, is not useable and so each opengl call causes a segmentation fault.
    //Maybe the solution is to use: XAddToSaveSet
    //The following line is commented to avoid the disagreement explained before.
    //XCloseDisplay (window_x11->device);

    GST_DEBUG ("display receiver closed");
    XCloseDisplay (window_x11->disp_send);
    GST_DEBUG ("display sender closed");
  }

  g_cond_clear (&window_x11->cond_send_message);

  GST_GL_WINDOW_UNLOCK (window_x11);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_window_x11_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLWindowX11 *window_x11;

  g_return_if_fail (GST_GL_IS_WINDOW_X11 (object));

  window_x11 = GST_GL_WINDOW_X11 (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      window_x11->display_name = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_window_x11_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLWindowX11 *window_x11;

  g_return_if_fail (GST_GL_IS_WINDOW_X11 (object));

  window_x11 = GST_GL_WINDOW_X11 (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      g_value_set_string (value, window_x11->display_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_window_x11_class_init (GstGLWindowX11Class * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowX11Private));

  obj_class->finalize = gst_gl_window_x11_finalize;
  obj_class->set_property = gst_gl_window_x11_set_property;
  obj_class->get_property = gst_gl_window_x11_get_property;

  g_object_class_install_property (obj_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  window_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_create_context);
  window_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_get_gl_context);
  window_class->activate = GST_DEBUG_FUNCPTR (gst_gl_window_x11_activate);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_set_window_handle);
  window_class->draw_unlocked =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_draw_unlocked);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_x11_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_x11_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_x11_quit);
  window_class->send_message =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_send_message);
}

static void
gst_gl_window_x11_init (GstGLWindowX11 * window)
{
  window->priv = GST_GL_WINDOW_X11_GET_PRIVATE (window);
}

/* Must be called in the gl thread */
GstGLWindowX11 *
gst_gl_window_x11_new (void)
{
  GstGLWindowX11 *window = NULL;
  const gchar *user_choice;

  user_choice = g_getenv ("GST_GL_PLATFORM");

  GST_INFO ("Attempting to create x11 window, user platform choice:%s",
      user_choice ? user_choice : "(null)");

#if GST_GL_HAVE_PLATFORM_GLX
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "glx") != NULL))
    window = GST_GL_WINDOW_X11 (gst_gl_window_x11_glx_new ());
#endif /* GST_GL_HAVE_PLATFORM_GLX */
#ifdef GST_GL_HAVE_PLATFORM_EGL
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "egl") != NULL))
    window = GST_GL_WINDOW_X11 (gst_gl_window_x11_egl_new ());
#endif /* GST_GL_HAVE_PLATFORM_EGL */
  if (!window) {
    GST_WARNING ("Failed to create x11 window, user_choice:%s",
        user_choice ? user_choice : "NULL");
    return NULL;
  }

  return window;
}

gboolean
gst_gl_window_x11_create_context (GstGLWindow * window,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (window);
  GstGLWindowX11Class *window_class = GST_GL_WINDOW_X11_GET_CLASS (window_x11);

  setlocale (LC_NUMERIC, "C");

  gst_gl_window_set_need_lock (GST_GL_WINDOW (window_x11), TRUE);

  g_cond_init (&window_x11->cond_send_message);
  window_x11->running = TRUE;
  window_x11->visible = FALSE;
  window_x11->parent_win = 0;
  window_x11->allow_extra_expose_events = TRUE;

  window_x11->device = XOpenDisplay (window_x11->display_name);
  if (window_x11->device == NULL) {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to connect to X display server");
    goto failure;
  }

  XSynchronize (window_x11->device, FALSE);

  GST_LOG ("gl device id: %ld", (gulong) window_x11->device);

  window_x11->disp_send = XOpenDisplay (window_x11->display_name);

  XSynchronize (window_x11->disp_send, FALSE);

  GST_LOG ("gl display sender: %ld", (gulong) window_x11->disp_send);

  window_x11->screen = DefaultScreenOfDisplay (window_x11->device);
  window_x11->screen_num = DefaultScreen (window_x11->device);
  window_x11->visual =
      DefaultVisual (window_x11->device, window_x11->screen_num);
  window_x11->root = DefaultRootWindow (window_x11->device);
  window_x11->white = XWhitePixel (window_x11->device, window_x11->screen_num);
  window_x11->black = XBlackPixel (window_x11->device, window_x11->screen_num);
  window_x11->depth = DefaultDepthOfScreen (window_x11->screen);

  GST_LOG ("gl root id: %lud", (gulong) window_x11->root);

  window_x11->device_width =
      DisplayWidth (window_x11->device, window_x11->screen_num);
  window_x11->device_height =
      DisplayHeight (window_x11->device, window_x11->screen_num);

  window_x11->connection = ConnectionNumber (window_x11->device);

  if (!window_class->choose_format (window_x11, error)) {
    goto failure;
  }

  gst_gl_window_x11_create_window (window_x11);

  if (!window_class->create_context (window_x11, gl_api, external_gl_context,
          error)) {
    goto failure;
  }

  if (!window_class->activate (window_x11, TRUE)) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_CREATE_CONTEXT,
        "Failed to make context current");
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

static gboolean
gst_gl_window_x11_create_window (GstGLWindowX11 * window_x11)
{
  XSetWindowAttributes win_attr;
  XTextProperty text_property;
  XWMHints wm_hints;
  unsigned long mask;
  const gchar *title = "OpenGL renderer";
  Atom wm_atoms[3];

  static gint x = 0;
  static gint y = 0;

  if (window_x11->visual_info->visual != window_x11->visual)
    GST_LOG ("selected visual is different from the default");

  GST_LOG ("visual XID:%d, screen:%d, visualid:%d, depth:%d, class:%d, "
      "red_mask:%ld, green_mask:%ld, blue_mask:%ld bpp:%d",
      (gint) XVisualIDFromVisual (window_x11->visual_info->visual),
      window_x11->visual_info->screen, (gint) window_x11->visual_info->visualid,
      window_x11->visual_info->depth, window_x11->visual_info->class,
      window_x11->visual_info->red_mask, window_x11->visual_info->green_mask,
      window_x11->visual_info->blue_mask,
      window_x11->visual_info->bits_per_rgb);

  win_attr.event_mask =
      StructureNotifyMask | ExposureMask | VisibilityChangeMask;
  win_attr.do_not_propagate_mask = NoEventMask;

  win_attr.background_pixmap = None;
  win_attr.background_pixel = 0;
  win_attr.border_pixel = 0;

  win_attr.colormap =
      XCreateColormap (window_x11->device, window_x11->root,
      window_x11->visual_info->visual, AllocNone);

  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  x += 20;
  y += 20;

  window_x11->internal_win_id =
      XCreateWindow (window_x11->device, window_x11->root, x, y, 1, 1, 0,
      window_x11->visual_info->depth, InputOutput,
      window_x11->visual_info->visual, mask, &win_attr);

  XSync (window_x11->device, FALSE);

  XSetWindowBackgroundPixmap (window_x11->device, window_x11->internal_win_id,
      None);

  GST_LOG ("gl window id: %lud", (gulong) window_x11->internal_win_id);
  GST_LOG ("gl window props: x:%d y:%d", x, y);

  wm_atoms[0] = XInternAtom (window_x11->device, "WM_DELETE_WINDOW", True);
  if (wm_atoms[0] == None)
    GST_DEBUG ("Cannot create WM_DELETE_WINDOW");

  wm_atoms[1] = XInternAtom (window_x11->device, "WM_GL_WINDOW", False);
  if (wm_atoms[1] == None)
    GST_DEBUG ("Cannot create WM_GL_WINDOW");

  wm_atoms[2] = XInternAtom (window_x11->device, "WM_QUIT_LOOP", False);
  if (wm_atoms[2] == None)
    GST_DEBUG ("Cannot create WM_QUIT_LOOP");

  XSetWMProtocols (window_x11->device, window_x11->internal_win_id, wm_atoms,
      2);

  wm_hints.flags = StateHint;
  wm_hints.initial_state = NormalState;
  wm_hints.input = False;

  XStringListToTextProperty ((char **) &title, 1, &text_property);

  XSetWMProperties (window_x11->device, window_x11->internal_win_id,
      &text_property, &text_property, 0, 0, NULL, &wm_hints, NULL);

  XFree (text_property.value);

  return TRUE;
}

guintptr
gst_gl_window_x11_get_gl_context (GstGLWindow * window)
{
  GstGLWindowX11Class *window_class;

  window_class = GST_GL_WINDOW_X11_GET_CLASS (window);

  return window_class->get_gl_context (GST_GL_WINDOW_X11 (window));
}

static void
callback_activate (GstGLWindow * window)
{
  GstGLWindowX11Class *window_class;
  GstGLWindowX11Private *priv;
  GstGLWindowX11 *window_x11;

  window_x11 = GST_GL_WINDOW_X11 (window);
  window_class = GST_GL_WINDOW_X11_GET_CLASS (window_x11);
  priv = window_x11->priv;

  priv->activate_result = window_class->activate (window_x11, priv->activate);
}

gboolean
gst_gl_window_x11_activate (GstGLWindow * window, gboolean activate)
{
  GstGLWindowX11 *window_x11;
  GstGLWindowX11Private *priv;

  window_x11 = GST_GL_WINDOW_X11 (window);
  priv = window_x11->priv;
  priv->activate = activate;

  gst_gl_window_x11_send_message (window, GST_GL_WINDOW_CB (callback_activate),
      window_x11);

  return priv->activate_result;
}

/* Not called by the gl thread */
void
gst_gl_window_x11_set_window_handle (GstGLWindow * window, guintptr id)
{
  GstGLWindowX11 *window_x11;
  XWindowAttributes attr;

  window_x11 = GST_GL_WINDOW_X11 (window);

  window_x11->parent_win = (Window) id;

  GST_LOG ("set parent window id: %lud", id);

  XGetWindowAttributes (window_x11->disp_send, window_x11->parent_win, &attr);

  XResizeWindow (window_x11->disp_send, window_x11->internal_win_id, attr.width,
      attr.height);

  XReparentWindow (window_x11->disp_send, window_x11->internal_win_id,
      window_x11->parent_win, 0, 0);

  XSync (window_x11->disp_send, FALSE);
}

/* Called in the gl thread */
void
gst_gl_window_x11_draw_unlocked (GstGLWindow * window, guint width,
    guint height)
{
  GstGLWindowX11 *window_x11;

  window_x11 = GST_GL_WINDOW_X11 (window);

  if (window_x11->running && window_x11->allow_extra_expose_events) {
    XEvent event;
    XWindowAttributes attr;

    XGetWindowAttributes (window_x11->device, window_x11->internal_win_id,
        &attr);

    event.xexpose.type = Expose;
    event.xexpose.send_event = TRUE;
    event.xexpose.display = window_x11->device;
    event.xexpose.window = window_x11->internal_win_id;
    event.xexpose.x = attr.x;
    event.xexpose.y = attr.y;
    event.xexpose.width = attr.width;
    event.xexpose.height = attr.height;
    event.xexpose.count = 0;

    XSendEvent (window_x11->device, window_x11->internal_win_id, FALSE,
        ExposureMask, &event);
    XSync (window_x11->disp_send, FALSE);
  }
}

/* Not called by the gl thread */
void
gst_gl_window_x11_draw (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowX11 *window_x11;

  window_x11 = GST_GL_WINDOW_X11 (window);

  if (window_x11->running) {
    XEvent event;
    XWindowAttributes attr;

    XGetWindowAttributes (window_x11->disp_send, window_x11->internal_win_id,
        &attr);

    if (!window_x11->visible) {

      if (!window_x11->parent_win) {
        attr.width = width;
        attr.height = height;
        XResizeWindow (window_x11->disp_send, window_x11->internal_win_id,
            attr.width, attr.height);
        XSync (window_x11->disp_send, FALSE);
      }

      XMapWindow (window_x11->disp_send, window_x11->internal_win_id);
      window_x11->visible = TRUE;
    }

    if (window_x11->parent_win) {
      XWindowAttributes attr_parent;
      XGetWindowAttributes (window_x11->disp_send, window_x11->parent_win,
          &attr_parent);

      if (attr.width != attr_parent.width || attr.height != attr_parent.height) {
        XMoveResizeWindow (window_x11->disp_send, window_x11->internal_win_id,
            0, 0, attr_parent.width, attr_parent.height);
        XSync (window_x11->disp_send, FALSE);

        attr.width = attr_parent.width;
        attr.height = attr_parent.height;

        GST_LOG ("parent resize:  %d, %d",
            attr_parent.width, attr_parent.height);
      }
    }

    event.xexpose.type = Expose;
    event.xexpose.send_event = TRUE;
    event.xexpose.display = window_x11->disp_send;
    event.xexpose.window = window_x11->internal_win_id;
    event.xexpose.x = attr.x;
    event.xexpose.y = attr.y;
    event.xexpose.width = attr.width;
    event.xexpose.height = attr.height;
    event.xexpose.count = 0;

    XSendEvent (window_x11->disp_send, window_x11->internal_win_id, FALSE,
        ExposureMask, &event);
    XSync (window_x11->disp_send, FALSE);
  }
}

/* Called in the gl thread */
void
gst_gl_window_x11_run (GstGLWindow * window)
{
  GstGLWindowX11 *window_x11;
  GstGLWindowX11Class *window_class;

  window_x11 = GST_GL_WINDOW_X11 (window);
  window_class = GST_GL_WINDOW_X11_GET_CLASS (window_x11);

  GST_DEBUG ("begin loop");

  while (window_x11->running) {
    XEvent event;
    XEvent pending_event;

    GST_GL_WINDOW_UNLOCK (window);

    /* XSendEvent (which are called in other threads) are done from another display structure */
    XNextEvent (window_x11->device, &event);

    GST_GL_WINDOW_LOCK (window);

    // use in generic/cube and other related uses
    window_x11->allow_extra_expose_events = XPending (window_x11->device) <= 2;

    switch (event.type) {
      case ClientMessage:
      {

        Atom wm_delete =
            XInternAtom (window_x11->device, "WM_DELETE_WINDOW", True);
        Atom wm_gl = XInternAtom (window_x11->device, "WM_GL_WINDOW", True);
        Atom wm_quit_loop =
            XInternAtom (window_x11->device, "WM_QUIT_LOOP", True);

        if (wm_delete == None)
          GST_DEBUG ("Cannot create WM_DELETE_WINDOW");
        if (wm_gl == None)
          GST_DEBUG ("Cannot create WM_GL_WINDOW");
        if (wm_quit_loop == None)
          GST_DEBUG ("Cannot create WM_QUIT_LOOP");

        /* Message sent with gst_gl_window_send_message */
        if (wm_gl != None && event.xclient.message_type == wm_gl) {
          if (window_x11->running) {
#if SIZEOF_VOID_P == 8
            GstGLWindowCB custom_cb =
                (GstGLWindowCB) (((event.xclient.
                        data.l[0] & 0xffffffff) << 32) | (event.xclient.
                    data.l[1] & 0xffffffff));
            gpointer custom_data =
                (gpointer) (((event.xclient.
                        data.l[2] & 0xffffffff) << 32) | (event.xclient.
                    data.l[3] & 0xffffffff));
#else
            GstGLWindowCB custom_cb = (GstGLWindowCB) event.xclient.data.l[0];
            gpointer custom_data = (gpointer) event.xclient.data.l[1];
#endif

            if (!custom_cb || !custom_data)
              GST_DEBUG ("custom cb not initialized");

            custom_cb (custom_data);
          }

          g_cond_signal (&window_x11->cond_send_message);
        }

        /* User clicked on the cross */
        else if (wm_delete != None
            && (Atom) event.xclient.data.l[0] == wm_delete) {
          GST_DEBUG ("Close %lud", (gulong) window_x11->internal_win_id);

          if (window->close)
            window->close (window->close_data);
        }

        /* message sent with gst_gl_window_quit_loop */
        else if (wm_quit_loop != None
            && event.xclient.message_type == wm_quit_loop) {
#if SIZEOF_VOID_P == 8
          GstGLWindowCB destroy_cb =
              (GstGLWindowCB) (((event.xclient.
                      data.l[0] & 0xffffffff) << 32) | (event.xclient.
                  data.l[1] & 0xffffffff));
          gpointer destroy_data =
              (gpointer) (((event.xclient.
                      data.l[2] & 0xffffffff) << 32) | (event.xclient.
                  data.l[3] & 0xffffffff));
#else
          GstGLWindowCB destroy_cb = (GstGLWindowCB) event.xclient.data.l[0];
          gpointer destroy_data = (gpointer) event.xclient.data.l[1];
#endif

          GST_DEBUG ("Quit loop message %lud",
              (gulong) window_x11->internal_win_id);

          /* exit loop */
          window_x11->running = FALSE;

          /* make sure last pendings send message calls are executed */
          XFlush (window_x11->device);
          while (XCheckTypedEvent (window_x11->device, ClientMessage,
                  &pending_event)) {
#if SIZEOF_VOID_P == 8
            GstGLWindowCB custom_cb =
                (GstGLWindowCB) (((event.xclient.
                        data.l[0] & 0xffffffff) << 32) | (event.xclient.
                    data.l[1] & 0xffffffff));
            gpointer custom_data =
                (gpointer) (((event.xclient.
                        data.l[2] & 0xffffffff) << 32) | (event.xclient.
                    data.l[3] & 0xffffffff));
#else
            GstGLWindowCB custom_cb = (GstGLWindowCB) event.xclient.data.l[0];
            gpointer custom_data = (gpointer) event.xclient.data.l[1];
#endif
            GST_DEBUG ("execute last pending custom x events");

            if (!custom_cb || !custom_data)
              GST_DEBUG ("custom cb not initialized");

            custom_cb (custom_data);

            g_cond_signal (&window_x11->cond_send_message);
          }

          /* Finally we can destroy opengl ressources (texture/shaders/fbo) */
          if (!destroy_cb || !destroy_data)
            GST_FIXME ("destroy cb not correctly set");

          destroy_cb (destroy_data);

        } else
          GST_DEBUG ("client message not recognized");
        break;
      }

      case CreateNotify:
      case ConfigureNotify:
      {
        if (window->resize)
          window->resize (window->resize_data, event.xconfigure.width,
              event.xconfigure.height);
        break;
      }

      case DestroyNotify:
        break;

      case Expose:
        if (window->draw) {
          window->draw (window->draw_data);
          window_class->swap_buffers (window_x11);
        }
        break;

      case VisibilityNotify:
      {
        switch (event.xvisibility.state) {
          case VisibilityUnobscured:
            if (window->draw)
              window->draw (window->draw_data);
            break;

          case VisibilityPartiallyObscured:
            if (window->draw)
              window->draw (window->draw_data);
            break;

          case VisibilityFullyObscured:
            break;

          default:
            GST_DEBUG ("unknown xvisibility event: %d",
                event.xvisibility.state);
            break;
        }
        break;
      }

      default:
        GST_DEBUG ("unknown XEvent type: %ud", event.type);
        break;

    }                           // switch
  }                             // while running

  GST_DEBUG ("end loop");
}

/* Not called by the gl thread */
void
gst_gl_window_x11_quit (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowX11 *window_x11;

  window_x11 = GST_GL_WINDOW_X11 (window);

  GST_DEBUG ("sending quit, running:%i", window_x11->running);

  if (window_x11->running) {
    XEvent event;

    event.xclient.type = ClientMessage;
    event.xclient.send_event = TRUE;
    event.xclient.display = window_x11->disp_send;
    event.xclient.window = window_x11->internal_win_id;
    event.xclient.message_type =
        XInternAtom (window_x11->disp_send, "WM_QUIT_LOOP", True);
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

    XSendEvent (window_x11->disp_send, window_x11->internal_win_id, FALSE,
        NoEventMask, &event);
    XSync (window_x11->disp_send, FALSE);
  }
}

/* Not called by the gl thread */
void
gst_gl_window_x11_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowX11 *window_x11;

  window_x11 = GST_GL_WINDOW_X11 (window);

  if (window_x11->running) {
    XEvent event;

    event.xclient.type = ClientMessage;
    event.xclient.send_event = TRUE;
    event.xclient.display = window_x11->disp_send;
    event.xclient.window = window_x11->internal_win_id;
    event.xclient.message_type =
        XInternAtom (window_x11->disp_send, "WM_GL_WINDOW", True);
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

    XSendEvent (window_x11->disp_send, window_x11->internal_win_id, FALSE,
        NoEventMask, &event);
    XSync (window_x11->disp_send, FALSE);

    /* block until opengl calls have been executed in the gl thread */
    g_cond_wait (&window_x11->cond_send_message,
        GST_GL_WINDOW_GET_LOCK (window));
  }
}

static int
error_handler (Display * xdpy, XErrorEvent * error)
{
  TrappedErrorCode = error->error_code;
  return 0;
}

/**
 * gst_gl_window_x11_trap_x_errors:
 *
 * Traps every X error until gst_gl_window_x11_untrap_x_errors() is called.
 */
void
gst_gl_window_x11_trap_x_errors (void)
{
  TrappedErrorCode = 0;
  old_error_handler = XSetErrorHandler (error_handler);
}

/**
 * gst_gl_window_x11_untrap_x_errors:
 *
 * Removes the X error trap and returns the current status.
 *
 * Return value: the trapped error code, or 0 for success
 */
gint
gst_gl_window_x11_untrap_x_errors (void)
{
  XSetErrorHandler (old_error_handler);

  return TrappedErrorCode;
}
