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

#include "x11_event_source.h"
#include "gstglwindow_x11.h"
#include "gstgldisplay_x11.h"
/* for XkbKeycodeToKeysym */
#include <X11/XKBlib.h>

#define GST_GL_WINDOW_X11_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_X11, GstGLWindowX11Private))

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_x11_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowX11, gst_gl_window_x11, GST_GL_TYPE_WINDOW);

/* X error trap */
static int TrappedErrorCode = 0;
static int (*old_error_handler) (Display *, XErrorEvent *);

gboolean gst_gl_window_x11_handle_event (GstGLWindowX11 * window_x11);

enum
{
  ARG_0,
  ARG_DISPLAY
};

struct _GstGLWindowX11Private
{
  gboolean activate;
  gboolean activate_result;

  gint preferred_width;
  gint preferred_height;

  gboolean handle_events;
};

guintptr gst_gl_window_x11_get_display (GstGLWindow * window);
guintptr gst_gl_window_x11_get_gl_context (GstGLWindow * window);
gboolean gst_gl_window_x11_activate (GstGLWindow * window, gboolean activate);
void gst_gl_window_x11_set_window_handle (GstGLWindow * window,
    guintptr handle);
guintptr gst_gl_window_x11_get_window_handle (GstGLWindow * window);
static void gst_gl_window_x11_set_preferred_size (GstGLWindow * window,
    gint width, gint height);
void gst_gl_window_x11_show (GstGLWindow * window);
void gst_gl_window_x11_draw (GstGLWindow * window);
gboolean gst_gl_window_x11_create_context (GstGLWindow * window,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error);
gboolean gst_gl_window_x11_open (GstGLWindow * window, GError ** error);
void gst_gl_window_x11_close (GstGLWindow * window);
void gst_gl_window_x11_handle_events (GstGLWindow * window,
    gboolean handle_events);

static void
gst_gl_window_x11_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_window_x11_class_init (GstGLWindowX11Class * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowX11Private));

  obj_class->finalize = gst_gl_window_x11_finalize;

  window_class->get_display = GST_DEBUG_FUNCPTR (gst_gl_window_x11_get_display);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_set_window_handle);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_get_window_handle);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_x11_draw);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_x11_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_x11_close);
  window_class->handle_events =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_handle_events);
  window_class->set_preferred_size =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_set_preferred_size);
  window_class->show = GST_DEBUG_FUNCPTR (gst_gl_window_x11_show);
}

static void
gst_gl_window_x11_init (GstGLWindowX11 * window)
{
  window->priv = GST_GL_WINDOW_X11_GET_PRIVATE (window);
}

/* Must be called in the gl thread */
GstGLWindowX11 *
gst_gl_window_x11_new (GstGLDisplay * display)
{
  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_X11)
      == GST_GL_DISPLAY_TYPE_NONE) {
    GST_INFO ("Wrong display type %u for this window type %u", display->type,
        GST_GL_DISPLAY_TYPE_X11);
    return NULL;
  }

  return g_object_new (GST_GL_TYPE_WINDOW_X11, NULL);
}

gboolean
gst_gl_window_x11_open (GstGLWindow * window, GError ** error)
{
  GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (window);
  GstGLDisplayX11 *display_x11 = (GstGLDisplayX11 *) window->display;

  window_x11->device = XOpenDisplay (display_x11->name);
//  window_x11->device = display_x11->display;
  if (window_x11->device == NULL) {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to connect to X display server");
    goto failure;
  }

  XSynchronize (window_x11->device, FALSE);

  GST_LOG ("gl device id: %ld", (gulong) window_x11->device);

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

  if (!GST_GL_WINDOW_CLASS (parent_class)->open (window, error))
    return FALSE;

  if (!display_x11->foreign_display) {
    window_x11->x11_source = x11_event_source_new (window_x11);
    g_source_attach (window_x11->x11_source, window->main_context);
  }

  window_x11->allow_extra_expose_events = TRUE;

  return TRUE;

failure:
  return FALSE;
}

gboolean
gst_gl_window_x11_create_window (GstGLWindowX11 * window_x11)
{
  XSetWindowAttributes win_attr;
  XTextProperty text_property;
  XWMHints wm_hints;
  unsigned long mask;
  const gchar *title = "OpenGL renderer";
  Atom wm_atoms[1];
  gint x = 0, y = 0, width = 1, height = 1;

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

  window_x11->internal_win_id =
      XCreateWindow (window_x11->device,
      window_x11->parent_win ? window_x11->parent_win : window_x11->root,
      x, y, width, height, 0,
      window_x11->visual_info->depth, InputOutput,
      window_x11->visual_info->visual, mask, &win_attr);

  gst_gl_window_x11_handle_events (GST_GL_WINDOW (window_x11),
      window_x11->priv->handle_events);

  XSync (window_x11->device, FALSE);

  XSetWindowBackgroundPixmap (window_x11->device,
      window_x11->internal_win_id, None);

  GST_LOG ("gl window id: %lud", (gulong) window_x11->internal_win_id);
  GST_LOG ("gl window props: x:%d y:%d", x, y);

  wm_atoms[0] = XInternAtom (window_x11->device, "WM_DELETE_WINDOW", True);
  if (wm_atoms[0] == None)
    GST_DEBUG ("Cannot create WM_DELETE_WINDOW");

  XSetWMProtocols (window_x11->device, window_x11->internal_win_id,
      wm_atoms, 1);

  wm_hints.flags = StateHint;
  wm_hints.initial_state = NormalState;
  wm_hints.input = False;

  XStringListToTextProperty ((char **) &title, 1, &text_property);

  XSetWMProperties (window_x11->device, window_x11->internal_win_id,
      &text_property, &text_property, 0, 0, NULL, &wm_hints, NULL);

  XFree (text_property.value);

  return TRUE;
}

void
gst_gl_window_x11_close (GstGLWindow * window)
{
  GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (window);
  GstGLDisplay *display = window->display;
  XEvent event;

  if (window_x11->device) {
    /* Avoid BadDrawable Errors... */
    if (gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_X11)
      XSync (GST_GL_DISPLAY_X11 (display)->display, FALSE);

    if (window_x11->internal_win_id)
      XUnmapWindow (window_x11->device, window_x11->internal_win_id);

    XFree (window_x11->visual_info);

    if (window_x11->internal_win_id) {
      XReparentWindow (window_x11->device, window_x11->internal_win_id,
          window_x11->root, 0, 0);
      XDestroyWindow (window_x11->device, window_x11->internal_win_id);
    }
    XSync (window_x11->device, FALSE);

    while (XPending (window_x11->device))
      XNextEvent (window_x11->device, &event);

    XCloseDisplay (window_x11->device);
    GST_DEBUG ("display receiver closed");
  }

  if (window_x11->x11_source) {
    g_source_destroy (window_x11->x11_source);
    g_source_unref (window_x11->x11_source);
    window_x11->x11_source = NULL;
  }

  window_x11->running = FALSE;

  GST_GL_WINDOW_CLASS (parent_class)->close (window);
}

/* called by the gl thread */
void
gst_gl_window_x11_set_window_handle (GstGLWindow * window, guintptr id)
{
  GstGLWindowX11 *window_x11;
  XWindowAttributes attr;

  window_x11 = GST_GL_WINDOW_X11 (window);

  window_x11->parent_win = (Window) id;

  /* XXX: seems to be needed for the difference between gtk videooverlay and
   * the embedding gl into gtk directly */
  if (id && !window_x11->x11_source) {
    window_x11->x11_source = x11_event_source_new (window_x11);
    g_source_attach (window_x11->x11_source,
        g_main_context_get_thread_default ());
  }

  XGetWindowAttributes (window_x11->device, window_x11->parent_win, &attr);

  XResizeWindow (window_x11->device, window_x11->internal_win_id,
      attr.width, attr.height);

  XReparentWindow (window_x11->device, window_x11->internal_win_id,
      window_x11->parent_win, 0, 0);

  XSync (window_x11->device, FALSE);
}

guintptr
gst_gl_window_x11_get_window_handle (GstGLWindow * window)
{
  GstGLWindowX11 *window_x11;

  window_x11 = GST_GL_WINDOW_X11 (window);

  return window_x11->internal_win_id;
}

static void
gst_gl_window_x11_set_preferred_size (GstGLWindow * window, gint width,
    gint height)
{
  GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (window);

  window_x11->priv->preferred_width = width;
  window_x11->priv->preferred_height = height;
}

static void
_show_window (GstGLWindow * window)
{
  GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (window);
  guint width = window_x11->priv->preferred_width;
  guint height = window_x11->priv->preferred_height;
  XWindowAttributes attr;

  XGetWindowAttributes (window_x11->device, window_x11->internal_win_id, &attr);

  if (!window_x11->visible) {

    if (!window_x11->parent_win) {
      attr.width = width;
      attr.height = height;
      XResizeWindow (window_x11->device, window_x11->internal_win_id,
          attr.width, attr.height);
      XSync (window_x11->device, FALSE);
    }

    XMapWindow (window_x11->device, window_x11->internal_win_id);
    window_x11->visible = TRUE;
  }
}

void
gst_gl_window_x11_show (GstGLWindow * window)
{
  gst_gl_window_send_message (window, (GstGLWindowCB) _show_window, window);
}

static void
draw_cb (gpointer data)
{
  GstGLWindowX11 *window_x11 = data;
  GstGLWindow *window = GST_GL_WINDOW (window_x11);

  if (gst_gl_window_is_running (window)) {
    guint width, height;
    XWindowAttributes attr;

    XGetWindowAttributes (window_x11->device, window_x11->internal_win_id,
        &attr);

    if (window_x11->parent_win) {
      XWindowAttributes attr_parent;
      XGetWindowAttributes (window_x11->device, window_x11->parent_win,
          &attr_parent);

      if (attr.width != attr_parent.width || attr.height != attr_parent.height) {
        XMoveResizeWindow (window_x11->device, window_x11->internal_win_id,
            0, 0, attr_parent.width, attr_parent.height);
        XSync (window_x11->device, FALSE);

        attr.width = attr_parent.width;
        attr.height = attr_parent.height;

        GST_LOG ("parent resize:  %d, %d",
            attr_parent.width, attr_parent.height);
      }
    }

    gst_gl_window_get_surface_dimensions (window, &width, &height);
    if (attr.width != width || attr.height != height) {
      width = attr.width;
      height = attr.height;
      gst_gl_window_queue_resize (window);
    }

    if (window_x11->allow_extra_expose_events) {
      if (window->queue_resize)
        gst_gl_window_resize (window, width, height);

      if (window->draw) {
        GstGLContext *context = gst_gl_window_get_context (window);
        GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

        window->draw (window->draw_data);
        context_class->swap_buffers (context);

        gst_object_unref (context);
      }
    }
  }
}

/* Not called by the gl thread */
void
gst_gl_window_x11_draw (GstGLWindow * window)
{
  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, window);
}

static inline const gchar *
event_type_to_string (guint type)
{
  switch (type) {
    case CreateNotify:
      return "CreateNotify";
    case ConfigureNotify:
      return "ConfigureNotify";
    case DestroyNotify:
      return "DestroyNotify";
    case MapNotify:
      return "MapNotify";
    case UnmapNotify:
      return "UnmapNotify";
    case Expose:
      return "Expose";
    case VisibilityNotify:
      return "VisibilityNotify";
    case PropertyNotify:
      return "PropertyNotify";
    case SelectionClear:
      return "SelectionClear";
    case SelectionNotify:
      return "SelectionNotify";
    case SelectionRequest:
      return "SelectionRequest";
    case ClientMessage:
      return "ClientMessage";
    case KeyPress:
      return "KeyPress";
    case KeyRelease:
      return "KeyRelease";
    case ButtonPress:
      return "ButtonPress";
    case ButtonRelease:
      return "ButtonRelease";
    case MotionNotify:
      return "MotionNotify";
    default:
      return "unknown";
  }
}

void
gst_gl_window_x11_handle_events (GstGLWindow * window, gboolean handle_events)
{
  GstGLWindowX11 *window_x11;

  g_return_if_fail (window != NULL);

  window_x11 = GST_GL_WINDOW_X11 (window);

  window_x11->priv->handle_events = handle_events;

  if (window_x11->internal_win_id) {
    if (handle_events) {
      XSelectInput (window_x11->device, window_x11->internal_win_id,
          StructureNotifyMask | ExposureMask | VisibilityChangeMask |
          PointerMotionMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
          ButtonReleaseMask);
    } else {
      XSelectInput (window_x11->device, window_x11->internal_win_id,
          StructureNotifyMask | ExposureMask | VisibilityChangeMask);
    }
  }
}

gboolean
gst_gl_window_x11_handle_event (GstGLWindowX11 * window_x11)
{
  GstGLContext *context;
  GstGLContextClass *context_class;
  GstGLWindow *window;
  gboolean ret = TRUE;

  window = GST_GL_WINDOW (window_x11);

  if (gst_gl_window_is_running (window)
      && XPending (window_x11->device)) {
    XEvent event;

    /* XSendEvent (which are called in other threads) are done from another display structure */
    XNextEvent (window_x11->device, &event);

    window_x11->allow_extra_expose_events = XPending (window_x11->device) <= 2;

    GST_LOG ("got event %s", event_type_to_string (event.type));

    switch (event.type) {
      case ClientMessage:
      {
        Atom wm_delete =
            XInternAtom (window_x11->device, "WM_DELETE_WINDOW", True);

        if (wm_delete == None)
          GST_DEBUG ("Cannot create WM_DELETE_WINDOW");

        /* User clicked on the cross */
        if (wm_delete != None && (Atom) event.xclient.data.l[0] == wm_delete) {
          GST_DEBUG ("Close %lud", (gulong) window_x11->internal_win_id);

          if (window->close)
            window->close (window->close_data);

          ret = FALSE;
        }
        break;
      }

      case CreateNotify:
      case ConfigureNotify:
      {
        gst_gl_window_resize (window, event.xconfigure.width,
            event.xconfigure.height);
        break;
      }

      case DestroyNotify:
        break;

      case Expose:
        /* non-zero means that other Expose follows
         * so just wait for the last one
         * in theory we should not receive non-zero because
         * we have no sub areas here but just in case */
        if (event.xexpose.count != 0) {
          break;
        }

        /* We need to redraw on expose */
        if (window->draw) {
          context = gst_gl_window_get_context (window);
          context_class = GST_GL_CONTEXT_GET_CLASS (context);

          window->draw (window->draw_data);
          context_class->swap_buffers (context);

          gst_object_unref (context);
        }
        break;

      case VisibilityNotify:
        /* actually nothing to do here */
        break;
      case KeyPress:
      case KeyRelease:
      {
        const char *key_str = NULL, *key_type = NULL;
        KeySym keysym;

        keysym = XkbKeycodeToKeysym (window_x11->device,
            event.xkey.keycode, 0, 0);
        key_str = XKeysymToString (keysym);
        key_type = event.type == KeyPress ? "key-press" : "key-release";
        GST_DEBUG ("input event key %d %s over window at %d,%d (%s)",
            event.xkey.keycode, key_type, event.xkey.x, event.xkey.y, key_str);
        gst_gl_window_send_key_event_async (window, key_type, key_str);
        break;
      }
      case ButtonPress:
      case ButtonRelease:{
        const char *mouse_type = NULL;

        mouse_type = event.type ==
            ButtonPress ? "mouse-button-press" : "mouse-button-release";

        GST_DEBUG ("input event mouse button %d %s over window at %d,%d",
            event.xbutton.button, mouse_type, event.xbutton.x, event.xbutton.y);

        gst_gl_window_send_mouse_event_async (window, mouse_type,
            event.xbutton.button, event.xbutton.x, event.xbutton.y);
        break;
      }
      case MotionNotify:
        GST_DEBUG ("input event pointer moved over window at %d,%d",
            event.xmotion.x, event.xmotion.y);

        gst_gl_window_send_mouse_event_async (window, "mouse-move", 0,
            event.xbutton.x, event.xbutton.y);
        break;
      default:
        GST_DEBUG ("unknown XEvent type: %u", event.type);
        break;
    }                           // switch
  }                             // while running

  return ret;
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

guintptr
gst_gl_window_x11_get_display (GstGLWindow * window)
{
  GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (window);

  return (guintptr) window_x11->device;
}
