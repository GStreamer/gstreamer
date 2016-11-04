/* GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
 * Copyright (C) 2005 Julien Moutte <julien@moutte.net>
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

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Our interfaces */
#include <gst/video/navigation.h>
#include <gst/video/videooverlay.h>

#include <X11/XKBlib.h>

/* Debugging category */
#include <gst/gstinfo.h>

#include "gstvdpoutputbuffer.h"
#include "gstvdpoutputbufferpool.h"

/* Object header */
#include "gstvdpsink.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_sink_debug);
#define GST_CAT_DEFAULT gst_vdp_sink_debug

typedef struct
{
  unsigned long flags;
  unsigned long functions;
  unsigned long decorations;
  long input_mode;
  unsigned long status;
}
MotifWmHints, MwmHints;

#define MWM_HINTS_DECORATIONS   (1L << 1)

static void gst_vdp_sink_expose (GstXOverlay * overlay);

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_SYNCHRONOUS,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_HANDLE_EVENTS,
  PROP_HANDLE_EXPOSE
};

static GstVideoSinkClass *parent_class = NULL;

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VDP_OUTPUT_CAPS));

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_sink_debug, "vdpausink", 0, "VDPAU video sink");

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* X11 stuff */

static gboolean
gst_vdp_sink_window_decorate (VdpSink * vdp_sink, GstVdpWindow * window)
{
  Atom hints_atom = None;
  MotifWmHints *hints;

  g_return_val_if_fail (GST_IS_VDP_SINK (vdp_sink), FALSE);
  g_return_val_if_fail (window != NULL, FALSE);

  g_mutex_lock (vdp_sink->x_lock);

  hints_atom = XInternAtom (vdp_sink->device->display, "_MOTIF_WM_HINTS", 1);
  if (hints_atom == None) {
    g_mutex_unlock (vdp_sink->x_lock);
    return FALSE;
  }

  hints = g_malloc0 (sizeof (MotifWmHints));

  hints->flags |= MWM_HINTS_DECORATIONS;
  hints->decorations = 1 << 0;

  XChangeProperty (vdp_sink->device->display, window->win,
      hints_atom, hints_atom, 32, PropModeReplace,
      (guchar *) hints, sizeof (MotifWmHints) / sizeof (long));

  XSync (vdp_sink->device->display, FALSE);

  g_mutex_unlock (vdp_sink->x_lock);

  g_free (hints);

  return TRUE;
}

static void
gst_vdp_sink_window_set_title (VdpSink * vdp_sink,
    GstVdpWindow * window, const gchar * media_title)
{
  if (media_title) {
    g_free (vdp_sink->media_title);
    vdp_sink->media_title = g_strdup (media_title);
  }
  if (window) {
    /* we have a window */
    if (window->internal) {
      XTextProperty xproperty;
      const gchar *app_name;
      const gchar *title = NULL;
      gchar *title_mem = NULL;

      /* set application name as a title */
      app_name = g_get_application_name ();

      if (app_name && vdp_sink->media_title) {
        title = title_mem = g_strconcat (vdp_sink->media_title, " : ",
            app_name, NULL);
      } else if (app_name) {
        title = app_name;
      } else if (vdp_sink->media_title) {
        title = vdp_sink->media_title;
      }

      if (title) {
        if ((XStringListToTextProperty (((char **) &title), 1,
                    &xproperty)) != 0) {
          XSetWMName (vdp_sink->device->display, window->win, &xproperty);
          XFree (xproperty.value);
	}

        g_free (title_mem);
      }
    }
  }
}

static void
gst_vdp_sink_window_setup_vdpau (VdpSink * vdp_sink, GstVdpWindow * window)
{
  GstVdpDevice *device = vdp_sink->device;
  VdpStatus status;
  VdpColor color = { 0, };

  status = device->vdp_presentation_queue_target_create_x11 (device->device,
      window->win, &window->target);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (vdp_sink, RESOURCE, READ,
        ("Could not create presentation target"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
  }

  status =
      device->vdp_presentation_queue_create (device->device, window->target,
      &window->queue);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (vdp_sink, RESOURCE, READ,
        ("Could not create presentation queue"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
  }

  status =
      device->vdp_presentation_queue_set_background_color (window->queue,
      &color);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (vdp_sink, RESOURCE, READ,
        ("Could not set background color"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
  }
}

/* This function handles a GstVdpWindow creation */
static GstVdpWindow *
gst_vdp_sink_window_new (VdpSink * vdp_sink, gint width, gint height)
{
  GstVdpDevice *device = vdp_sink->device;
  GstVdpWindow *window = NULL;

  Window root;
  gint screen_num;
  gulong black;

  g_return_val_if_fail (GST_IS_VDP_SINK (vdp_sink), NULL);

  window = g_new0 (GstVdpWindow, 1);

  window->width = width;
  window->height = height;
  window->internal = TRUE;

  g_mutex_lock (vdp_sink->x_lock);

  screen_num = DefaultScreen (device->display);
  root = DefaultRootWindow (device->display);
  black = XBlackPixel (device->display, screen_num);

  window->win = XCreateSimpleWindow (vdp_sink->device->display,
      root, 0, 0, window->width, window->height, 0, 0, black);

  /* We have to do that to prevent X from redrawing the background on 
     ConfigureNotify. This takes away flickering of video when resizing. */
  XSetWindowBackgroundPixmap (vdp_sink->device->display, window->win, None);

  /* set application name as a title */
  gst_vdp_sink_window_set_title (vdp_sink, window, NULL);

  if (vdp_sink->handle_events) {
    Atom wm_delete;

    XSelectInput (vdp_sink->device->display, window->win, ExposureMask |
        StructureNotifyMask | PointerMotionMask | KeyPressMask |
        KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);

    /* Tell the window manager we'd like delete client messages instead of
     * being killed */
    wm_delete =
        XInternAtom (vdp_sink->device->display, "WM_DELETE_WINDOW", False);
    (void) XSetWMProtocols (vdp_sink->device->display, window->win, &wm_delete,
        1);
  }

  XMapRaised (vdp_sink->device->display, window->win);

  XSync (vdp_sink->device->display, FALSE);

  g_mutex_unlock (vdp_sink->x_lock);

  gst_vdp_sink_window_decorate (vdp_sink, window);
  gst_vdp_sink_window_setup_vdpau (vdp_sink, window);

  gst_x_overlay_got_window_handle (GST_X_OVERLAY (vdp_sink),
      (guintptr) window->win);

  return window;
}

/* This function destroys a GstVdpWindow */
static void
gst_vdp_sink_window_destroy (VdpSink * vdp_sink, GstVdpWindow * window)
{
  g_return_if_fail (window != NULL);
  g_return_if_fail (GST_IS_VDP_SINK (vdp_sink));

  g_mutex_lock (vdp_sink->x_lock);

  /* If we did not create that window we just free the GC and let it live */
  if (window->internal)
    XDestroyWindow (vdp_sink->device->display, window->win);
  else
    XSelectInput (vdp_sink->device->display, window->win, 0);

  XSync (vdp_sink->device->display, FALSE);

  g_mutex_unlock (vdp_sink->x_lock);

  g_free (window);
}

static void
gst_vdp_sink_window_update_geometry (VdpSink * vdp_sink, GstVdpWindow * window)
{
  XWindowAttributes attr;

  g_return_if_fail (window != NULL);
  g_return_if_fail (GST_IS_VDP_SINK (vdp_sink));

  /* Update the window geometry */
  g_mutex_lock (vdp_sink->x_lock);

  XGetWindowAttributes (vdp_sink->device->display, window->win, &attr);

  window->width = attr.width;
  window->height = attr.height;

  g_mutex_unlock (vdp_sink->x_lock);
}

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation.*/
static void
gst_vdp_sink_handle_xevents (VdpSink * vdp_sink)
{
  XEvent e;
  guint pointer_x = 0, pointer_y = 0;
  gboolean pointer_moved = FALSE;
  gboolean exposed = FALSE, configured = FALSE;

  g_return_if_fail (GST_IS_VDP_SINK (vdp_sink));

  /* Then we get all pointer motion events, only the last position is
     interesting. */
  g_mutex_lock (vdp_sink->flow_lock);
  g_mutex_lock (vdp_sink->x_lock);
  while (XCheckWindowEvent (vdp_sink->device->display,
          vdp_sink->window->win, PointerMotionMask, &e)) {
    g_mutex_unlock (vdp_sink->x_lock);
    g_mutex_unlock (vdp_sink->flow_lock);

    switch (e.type) {
      case MotionNotify:
        pointer_x = e.xmotion.x;
        pointer_y = e.xmotion.y;
        pointer_moved = TRUE;
        break;
      default:
        break;
    }
    g_mutex_lock (vdp_sink->flow_lock);
    g_mutex_lock (vdp_sink->x_lock);
  }

  if (pointer_moved) {
    g_mutex_unlock (vdp_sink->x_lock);
    g_mutex_unlock (vdp_sink->flow_lock);

    GST_DEBUG ("vdp_sink pointer moved over window at %d,%d",
        pointer_x, pointer_y);
    gst_navigation_send_mouse_event (GST_NAVIGATION (vdp_sink),
        "mouse-move", 0, pointer_x, pointer_y);

    g_mutex_lock (vdp_sink->flow_lock);
    g_mutex_lock (vdp_sink->x_lock);
  }

  /* We get all remaining events on our window to throw them upstream */
  while (XCheckWindowEvent (vdp_sink->device->display,
          vdp_sink->window->win,
          KeyPressMask | KeyReleaseMask |
          ButtonPressMask | ButtonReleaseMask, &e)) {
    KeySym keysym;

    /* We lock only for the X function call */
    g_mutex_unlock (vdp_sink->x_lock);
    g_mutex_unlock (vdp_sink->flow_lock);

    switch (e.type) {
      case ButtonPress:
        /* Mouse button pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("vdp_sink button %d pressed over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        gst_navigation_send_mouse_event (GST_NAVIGATION (vdp_sink),
            "mouse-button-press", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case ButtonRelease:
        GST_DEBUG ("vdp_sink button %d release over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        gst_navigation_send_mouse_event (GST_NAVIGATION (vdp_sink),
            "mouse-button-release", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case KeyPress:
      case KeyRelease:
        /* Key pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("vdp_sink key %d pressed over window at %d,%d",
            e.xkey.keycode, e.xkey.x, e.xkey.x);
        g_mutex_lock (vdp_sink->x_lock);
        keysym =
            XkbKeycodeToKeysym (vdp_sink->device->display, e.xkey.keycode, 0,
            0);
        g_mutex_unlock (vdp_sink->x_lock);
        if (keysym != NoSymbol) {
          char *key_str = NULL;

          g_mutex_lock (vdp_sink->x_lock);
          key_str = XKeysymToString (keysym);
          g_mutex_unlock (vdp_sink->x_lock);
          gst_navigation_send_key_event (GST_NAVIGATION (vdp_sink),
              e.type == KeyPress ? "key-press" : "key-release", key_str);

        } else {
          gst_navigation_send_key_event (GST_NAVIGATION (vdp_sink),
              e.type == KeyPress ? "key-press" : "key-release", "unknown");
        }
        break;
      default:
        GST_DEBUG_OBJECT (vdp_sink, "vdp_sink unhandled X event (%d)", e.type);
    }
    g_mutex_lock (vdp_sink->flow_lock);
    g_mutex_lock (vdp_sink->x_lock);
  }

  while (XCheckWindowEvent (vdp_sink->device->display,
          vdp_sink->window->win, ExposureMask | StructureNotifyMask, &e)) {
    switch (e.type) {
      case Expose:
        exposed = TRUE;
        break;
      case ConfigureNotify:
        configured = TRUE;
        break;
      default:
        break;
    }
  }

  if (vdp_sink->handle_expose && (exposed || configured)) {
    g_mutex_unlock (vdp_sink->x_lock);
    g_mutex_unlock (vdp_sink->flow_lock);

    gst_vdp_sink_expose (GST_X_OVERLAY (vdp_sink));

    g_mutex_lock (vdp_sink->flow_lock);
    g_mutex_lock (vdp_sink->x_lock);
  }

  /* Handle Display events */
  while (XPending (vdp_sink->device->display)) {
    XNextEvent (vdp_sink->device->display, &e);

    switch (e.type) {
      case ClientMessage:{
        Atom wm_delete;

        wm_delete = XInternAtom (vdp_sink->device->display,
            "WM_DELETE_WINDOW", False);
        if (wm_delete == (Atom) e.xclient.data.l[0]) {
          /* Handle window deletion by posting an error on the bus */
          GST_ELEMENT_ERROR (vdp_sink, RESOURCE, NOT_FOUND,
              ("Output window was closed"), (NULL));

          g_mutex_unlock (vdp_sink->x_lock);
          gst_vdp_sink_window_destroy (vdp_sink, vdp_sink->window);
          vdp_sink->window = NULL;
          g_mutex_lock (vdp_sink->x_lock);
        }
        break;
      }
      default:
        break;
    }
  }

  g_mutex_unlock (vdp_sink->x_lock);
  g_mutex_unlock (vdp_sink->flow_lock);
}

static gpointer
gst_vdp_sink_event_thread (VdpSink * vdp_sink)
{
  g_return_val_if_fail (GST_IS_VDP_SINK (vdp_sink), NULL);

  GST_OBJECT_LOCK (vdp_sink);
  while (vdp_sink->running) {
    GST_OBJECT_UNLOCK (vdp_sink);

    if (vdp_sink->window) {
      gst_vdp_sink_handle_xevents (vdp_sink);
    }
    g_usleep (100000);

    GST_OBJECT_LOCK (vdp_sink);
  }
  GST_OBJECT_UNLOCK (vdp_sink);

  return NULL;
}

/* This function calculates the pixel aspect ratio */
static GValue *
gst_vdp_sink_calculate_par (Display * display)
{
  static const gint par[][2] = {
    {1, 1},                     /* regular screen */
    {16, 15},                   /* PAL TV */
    {11, 10},                   /* 525 line Rec.601 video */
    {54, 59},                   /* 625 line Rec.601 video */
    {64, 45},                   /* 1280x1024 on 16:9 display */
    {5, 3},                     /* 1280x1024 on 4:3 display */
    {4, 3}                      /*  800x600 on 16:9 display */
  };
  gint screen_num;
  gint width, height;
  gint widthmm, heightmm;
  gint i;
  gint index;
  gdouble ratio;
  gdouble delta;
  GValue *par_value;

#define DELTA(idx) (ABS (ratio - ((gdouble) par[idx][0] / par[idx][1])))

  screen_num = DefaultScreen (display);
  width = DisplayWidth (display, screen_num);
  height = DisplayHeight (display, screen_num);
  widthmm = DisplayWidthMM (display, screen_num);
  heightmm = DisplayHeightMM (display, screen_num);

  /* first calculate the "real" ratio based on the X values;
   * which is the "physical" w/h divided by the w/h in pixels of the display */
  ratio = (gdouble) (widthmm * height)
      / (heightmm * width);

  /* DirectFB's X in 720x576 reports the physical dimensions wrong, so
   * override here */
  if (width == 720 && height == 576) {
    ratio = 4.0 * 576 / (3.0 * 720);
  }
  GST_DEBUG ("calculated pixel aspect ratio: %f", ratio);

  /* now find the one from par[][2] with the lowest delta to the real one */
  delta = DELTA (0);
  index = 0;

  for (i = 1; i < sizeof (par) / (sizeof (gint) * 2); ++i) {
    gdouble this_delta = DELTA (i);

    if (this_delta < delta) {
      index = i;
      delta = this_delta;
    }
  }

  GST_DEBUG ("Decided on index %d (%d/%d)", index,
      par[index][0], par[index][1]);

  par_value = g_new0 (GValue, 1);
  g_value_init (par_value, GST_TYPE_FRACTION);
  gst_value_set_fraction (par_value, par[index][0], par[index][1]);
  GST_DEBUG ("set X11 PAR to %d/%d",
      gst_value_get_fraction_numerator (par_value),
      gst_value_get_fraction_denominator (par_value));

  return par_value;
}

static GstCaps *
gst_vdp_sink_get_allowed_caps (GstVdpDevice * device, GValue * par)
{
  GstCaps *templ_caps, *allowed_caps, *caps;
  gint i;

  allowed_caps = gst_vdp_output_buffer_get_allowed_caps (device);
  templ_caps = gst_static_pad_template_get_caps (&sink_template);
  caps = gst_caps_intersect (allowed_caps, templ_caps);

  gst_caps_unref (allowed_caps);
  gst_caps_unref (templ_caps);

  if (!par)
    par = gst_vdp_sink_calculate_par (device->display);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure;

    structure = gst_caps_get_structure (caps, i);
    gst_structure_set_value (structure, "pixel-aspect-ratio", par);
  }

  return caps;
}

static void
gst_vdp_sink_post_error (VdpSink * vdp_sink, GError * error)
{
  GstMessage *message;

  message = gst_message_new_error (GST_OBJECT (vdp_sink), error, NULL);
  gst_element_post_message (GST_ELEMENT (vdp_sink), message);
  g_error_free (error);
}

static gboolean
gst_vdp_sink_open_device (VdpSink * vdp_sink)
{
  gboolean res;
  GstVdpDevice *device;
  GError *err;

  g_mutex_lock (vdp_sink->device_lock);
  if (vdp_sink->device) {
    res = TRUE;
    goto done;
  }

  err = NULL;
  vdp_sink->device = device = gst_vdp_get_device (vdp_sink->display_name, &err);
  if (!device)
    goto device_error;

  vdp_sink->bpool = gst_vdp_output_buffer_pool_new (device);

  vdp_sink->caps = gst_vdp_sink_get_allowed_caps (device, vdp_sink->par);
  GST_DEBUG ("runtime calculated caps: %" GST_PTR_FORMAT, vdp_sink->caps);

  /* call XSynchronize with the current value of synchronous */
  GST_DEBUG_OBJECT (vdp_sink, "XSynchronize called with %s",
      vdp_sink->synchronous ? "TRUE" : "FALSE");
  XSynchronize (device->display, vdp_sink->synchronous);

  /* Setup our event listening thread */
  vdp_sink->running = TRUE;
  vdp_sink->event_thread = g_thread_create (
      (GThreadFunc) gst_vdp_sink_event_thread, vdp_sink, TRUE, NULL);

  res = TRUE;

done:
  g_mutex_unlock (vdp_sink->device_lock);
  return res;

device_error:
  gst_vdp_sink_post_error (vdp_sink, err);
  res = FALSE;
  goto done;
}

static gboolean
gst_vdp_sink_start (GstBaseSink * bsink)
{
  VdpSink *vdp_sink = GST_VDP_SINK (bsink);
  gboolean res = TRUE;

  vdp_sink->window = NULL;
  vdp_sink->cur_image = NULL;

  vdp_sink->event_thread = NULL;

  vdp_sink->fps_n = 0;
  vdp_sink->fps_d = 1;

  res = gst_vdp_sink_open_device (vdp_sink);

  return res;
}

static void
gst_vdp_device_clear (VdpSink * vdp_sink)
{
  g_return_if_fail (GST_IS_VDP_SINK (vdp_sink));

  GST_OBJECT_LOCK (vdp_sink);
  if (vdp_sink->device == NULL) {
    GST_OBJECT_UNLOCK (vdp_sink);
    return;
  }
  GST_OBJECT_UNLOCK (vdp_sink);

  g_mutex_lock (vdp_sink->x_lock);

  g_object_unref (vdp_sink->bpool);
  g_object_unref (vdp_sink->device);
  vdp_sink->device = NULL;

  g_mutex_unlock (vdp_sink->x_lock);
}

static gboolean
gst_vdp_sink_stop (GstBaseSink * bsink)
{
  VdpSink *vdp_sink = GST_VDP_SINK (bsink);

  vdp_sink->running = FALSE;
  /* Wait for our event thread to finish before we clean up our stuff. */
  if (vdp_sink->event_thread)
    g_thread_join (vdp_sink->event_thread);

  if (vdp_sink->cur_image) {
    gst_buffer_unref (GST_BUFFER_CAST (vdp_sink->cur_image));
    vdp_sink->cur_image = NULL;
  }

  g_mutex_lock (vdp_sink->flow_lock);
  if (vdp_sink->window) {
    gst_vdp_sink_window_destroy (vdp_sink, vdp_sink->window);
    vdp_sink->window = NULL;
  }
  g_mutex_unlock (vdp_sink->flow_lock);

  gst_vdp_device_clear (vdp_sink);

  return TRUE;
}

/* Element stuff */

static GstCaps *
gst_vdp_sink_getcaps (GstBaseSink * bsink)
{
  VdpSink *vdp_sink;
  GstCaps *caps;

  vdp_sink = GST_VDP_SINK (bsink);

  if (vdp_sink->caps)
    caps = gst_caps_copy (vdp_sink->caps);
  else
    caps = gst_static_pad_template_get_caps (&sink_template);

  return caps;
}

static gboolean
gst_vdp_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  VdpSink *vdp_sink;
  GstCaps *allowed_caps;
  gboolean ret = TRUE;
  GstStructure *structure;
  GstCaps *intersection;
  gint new_width, new_height;
  const GValue *fps;

  vdp_sink = GST_VDP_SINK (bsink);

  GST_OBJECT_LOCK (vdp_sink);
  if (!vdp_sink->device)
    return FALSE;
  GST_OBJECT_UNLOCK (vdp_sink);

  allowed_caps = gst_pad_get_caps (GST_BASE_SINK_PAD (bsink));
  GST_DEBUG_OBJECT (vdp_sink,
      "sinkconnect possible caps %" GST_PTR_FORMAT " with given caps %"
      GST_PTR_FORMAT, allowed_caps, caps);

  /* We intersect those caps with our template to make sure they are correct */
  intersection = gst_caps_intersect (allowed_caps, caps);
  gst_caps_unref (allowed_caps);

  GST_DEBUG_OBJECT (vdp_sink, "intersection returned %" GST_PTR_FORMAT,
      intersection);
  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    return FALSE;
  }

  gst_caps_unref (intersection);

  structure = gst_caps_get_structure (caps, 0);

  ret &= gst_structure_get_int (structure, "width", &new_width);
  ret &= gst_structure_get_int (structure, "height", &new_height);
  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);
  if (!ret)
    return FALSE;

  GST_VIDEO_SINK_WIDTH (vdp_sink) = new_width;
  GST_VIDEO_SINK_HEIGHT (vdp_sink) = new_height;
  vdp_sink->fps_n = gst_value_get_fraction_numerator (fps);
  vdp_sink->fps_d = gst_value_get_fraction_denominator (fps);

  gst_vdp_buffer_pool_set_caps (vdp_sink->bpool, caps);

  /* Notify application to set xwindow id now */
  g_mutex_lock (vdp_sink->flow_lock);
  if (!vdp_sink->window) {
    g_mutex_unlock (vdp_sink->flow_lock);
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (vdp_sink));
  } else {
    g_mutex_unlock (vdp_sink->flow_lock);
  }

  /* Creating our window and our image */
  if (GST_VIDEO_SINK_WIDTH (vdp_sink) <= 0
      || GST_VIDEO_SINK_HEIGHT (vdp_sink) <= 0) {
    GST_ELEMENT_ERROR (vdp_sink, CORE, NEGOTIATION, (NULL),
        ("Invalid image size."));
    return FALSE;
  }

  g_mutex_lock (vdp_sink->flow_lock);
  if (!vdp_sink->window) {
    vdp_sink->window = gst_vdp_sink_window_new (vdp_sink,
        GST_VIDEO_SINK_WIDTH (vdp_sink), GST_VIDEO_SINK_HEIGHT (vdp_sink));
  }
  g_mutex_unlock (vdp_sink->flow_lock);

  return TRUE;
}

static void
gst_vdp_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  VdpSink *vdp_sink;

  vdp_sink = GST_VDP_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (vdp_sink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, vdp_sink->fps_d,
            vdp_sink->fps_n);
      }
    }
  }
}

static GstFlowReturn
gst_vdp_sink_show_frame (GstBaseSink * bsink, GstBuffer * outbuf)
{
  VdpSink *vdp_sink = GST_VDP_SINK (bsink);
  VdpStatus status;
  GstVdpDevice *device;

  g_return_val_if_fail (GST_IS_VDP_SINK (vdp_sink), FALSE);

  /* We take the flow_lock. If expose is in there we don't want to run
     concurrently from the data flow thread */
  g_mutex_lock (vdp_sink->flow_lock);

  if (G_UNLIKELY (vdp_sink->window == NULL)) {
    g_mutex_unlock (vdp_sink->flow_lock);
    return GST_FLOW_ERROR;
  }

  device = vdp_sink->device;

  if (vdp_sink->cur_image) {
    VdpOutputSurface surface =
        GST_VDP_OUTPUT_BUFFER (vdp_sink->cur_image)->surface;
    VdpPresentationQueueStatus queue_status;
    VdpTime pres_time;

    g_mutex_lock (vdp_sink->x_lock);
    status =
        device->vdp_presentation_queue_query_surface_status (vdp_sink->
        window->queue, surface, &queue_status, &pres_time);
    g_mutex_unlock (vdp_sink->x_lock);

    if (queue_status == VDP_PRESENTATION_QUEUE_STATUS_QUEUED) {
      g_mutex_unlock (vdp_sink->flow_lock);
      return GST_FLOW_OK;
    }
  }

  /* Expose sends a NULL image, we take the latest frame */
  if (!outbuf) {
    if (vdp_sink->cur_image) {
      outbuf = vdp_sink->cur_image;
    } else {
      g_mutex_unlock (vdp_sink->flow_lock);
      return GST_FLOW_OK;
    }
  }

  gst_vdp_sink_window_update_geometry (vdp_sink, vdp_sink->window);

  g_mutex_lock (vdp_sink->x_lock);

  status = device->vdp_presentation_queue_display (vdp_sink->window->queue,
      GST_VDP_OUTPUT_BUFFER (outbuf)->surface, 0, 0, 0);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (vdp_sink, RESOURCE, READ,
        ("Could not display frame"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));

    g_mutex_unlock (vdp_sink->x_lock);
    g_mutex_unlock (vdp_sink->flow_lock);
    return GST_FLOW_ERROR;
  }


  if (!vdp_sink->cur_image)
    vdp_sink->cur_image = gst_buffer_ref (outbuf);

  else if (vdp_sink->cur_image != outbuf) {
    gst_buffer_unref (vdp_sink->cur_image);
    vdp_sink->cur_image = gst_buffer_ref (outbuf);
  }

  XSync (vdp_sink->device->display, FALSE);

  g_mutex_unlock (vdp_sink->x_lock);
  g_mutex_unlock (vdp_sink->flow_lock);

  return GST_FLOW_OK;
}


static gboolean
gst_vdp_sink_event (GstBaseSink * sink, GstEvent * event)
{
  VdpSink *vdp_sink = GST_VDP_SINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *l;
      gchar *title = NULL;

      gst_event_parse_tag (event, &l);
      gst_tag_list_get_string (l, GST_TAG_TITLE, &title);

      if (title) {
        GST_DEBUG_OBJECT (vdp_sink, "got tags, title='%s'", title);
        gst_vdp_sink_window_set_title (vdp_sink, vdp_sink->window, title);

        g_free (title);
      }
      break;
    }
    default:
      break;
  }
  if (GST_BASE_SINK_CLASS (parent_class)->event)
    return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
  else
    return TRUE;
}

/* Buffer management
 *
 * The buffer_alloc function must either return a buffer with given size and
 * caps or create a buffer with different caps attached to the buffer. This
 * last option is called reverse negotiation, ie, where the sink suggests a
 * different format from the upstream peer. 
 *
 * We try to do reverse negotiation when our geometry changes and we like a
 * resized buffer.
 */
static GstFlowReturn
gst_vdp_sink_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  VdpSink *vdp_sink;
  GstStructure *structure = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gint width, height;
  GstCaps *alloc_caps;
  gint w_width, w_height;
  GError *err;

  vdp_sink = GST_VDP_SINK (bsink);

  GST_LOG_OBJECT (vdp_sink,
      "a buffer of %d bytes was requested with caps %" GST_PTR_FORMAT
      " and offset %" G_GUINT64_FORMAT, size, caps, offset);

  /* get struct to see what is requested */
  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    GST_WARNING_OBJECT (vdp_sink, "invalid caps for buffer allocation %"
        GST_PTR_FORMAT, caps);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  alloc_caps = gst_caps_ref (caps);

  /* We take the flow_lock because the window might go away */
  g_mutex_lock (vdp_sink->flow_lock);
  if (!vdp_sink->window) {
    g_mutex_unlock (vdp_sink->flow_lock);
    goto alloc;
  }

  /* What is our geometry */
  gst_vdp_sink_window_update_geometry (vdp_sink, vdp_sink->window);
  w_width = vdp_sink->window->width;
  w_height = vdp_sink->window->height;

  g_mutex_unlock (vdp_sink->flow_lock);

  /* We would like another geometry */
  if (width != w_width || height != w_height) {
    GstCaps *new_caps, *allowed_caps, *desired_caps;
    GstStructure *desired_struct;

    /* make a copy of the incomming caps to create the new
     * suggestion. We can't use make_writable because we might
     * then destroy the original caps which we still need when the
     * peer does not accept the suggestion. */
    new_caps = gst_caps_copy (caps);
    desired_struct = gst_caps_get_structure (new_caps, 0);

    GST_DEBUG ("we would love to receive a %dx%d video", w_width, w_height);
    gst_structure_set (desired_struct, "width", G_TYPE_INT, w_width, NULL);
    gst_structure_set (desired_struct, "height", G_TYPE_INT, w_height, NULL);

    allowed_caps = gst_pad_get_caps (GST_BASE_SINK_PAD (vdp_sink));
    desired_caps = gst_caps_intersect (new_caps, allowed_caps);

    gst_caps_unref (new_caps);
    gst_caps_unref (allowed_caps);

    /* see if peer accepts our new suggestion, if there is no peer, this 
     * function returns true. */
    if (gst_pad_peer_accept_caps (GST_VIDEO_SINK_PAD (vdp_sink), desired_caps)) {
      /* we will not alloc a buffer of the new suggested caps. Make sure
       * we also unref this new caps after we set it on the buffer. */
      GST_DEBUG ("peer pad accepts our desired caps %" GST_PTR_FORMAT,
          desired_caps);
      gst_caps_unref (alloc_caps);
      alloc_caps = desired_caps;
    } else {
      GST_DEBUG ("peer pad does not accept our desired caps %" GST_PTR_FORMAT,
          desired_caps);
      /* we alloc a buffer with the original incomming caps already in the
       * width and height variables */
      gst_caps_unref (desired_caps);
    }
  }

alloc:
  gst_vdp_buffer_pool_set_caps (vdp_sink->bpool, alloc_caps);
  gst_caps_unref (alloc_caps);

  err = NULL;
  *buf =
      GST_BUFFER_CAST (gst_vdp_buffer_pool_get_buffer (vdp_sink->bpool, &err));
  if (!*buf) {
    gst_vdp_sink_post_error (vdp_sink, err);
    return GST_FLOW_ERROR;
  }

beach:
  return ret;
}

/* Interfaces stuff */

static gboolean
gst_vdp_sink_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION || type == GST_TYPE_X_OVERLAY);
  return TRUE;
}

static void
gst_vdp_sink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_vdp_sink_interface_supported;
}

static void
gst_vdp_sink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  VdpSink *vdp_sink = GST_VDP_SINK (navigation);
  GstEvent *event;
  gint x_offset, y_offset;
  gdouble x, y;
  GstPad *pad = NULL;

  event = gst_event_new_navigation (structure);

  /* We are not converting the pointer coordinates as there's no hardware
     scaling done here. The only possible scaling is done by videoscale and
     videoscale will have to catch those events and tranform the coordinates
     to match the applied scaling. So here we just add the offset if the image
     is centered in the window.  */

  /* We take the flow_lock while we look at the window */
  g_mutex_lock (vdp_sink->flow_lock);

  if (!vdp_sink->window) {
    g_mutex_unlock (vdp_sink->flow_lock);
    return;
  }

  x_offset = vdp_sink->window->width - GST_VIDEO_SINK_WIDTH (vdp_sink);
  y_offset = vdp_sink->window->height - GST_VIDEO_SINK_HEIGHT (vdp_sink);

  g_mutex_unlock (vdp_sink->flow_lock);

  if (x_offset > 0 && gst_structure_get_double (structure, "pointer_x", &x)) {
    x -= x_offset / 2;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (y_offset > 0 && gst_structure_get_double (structure, "pointer_y", &y)) {
    y -= y_offset / 2;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (vdp_sink));

  if (GST_IS_PAD (pad) && GST_IS_EVENT (event)) {
    gst_pad_send_event (pad, event);

    gst_object_unref (pad);
  }
}

static void
gst_vdp_sink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_vdp_sink_navigation_send_event;
}

static void
gst_vdp_sink_set_window_handle (GstXOverlay * overlay, guintptr window_handle)
{
  VdpSink *vdp_sink = GST_VDP_SINK (overlay);
  GstVdpWindow *window = NULL;
  XWindowAttributes attr;
  Window xwindow_id = (XID) window_handle;

  /* We acquire the stream lock while setting this window in the element.
     We are basically cleaning tons of stuff replacing the old window, putting
     images while we do that would surely crash */
  g_mutex_lock (vdp_sink->flow_lock);

  /* If we already use that window return */
  if (vdp_sink->window && (xwindow_id == vdp_sink->window->win)) {
    g_mutex_unlock (vdp_sink->flow_lock);
    return;
  }

  /* If the element has not initialized the X11 context try to do so */
  if (!gst_vdp_sink_open_device (vdp_sink)) {
    g_mutex_unlock (vdp_sink->flow_lock);
    /* we have thrown a GST_ELEMENT_ERROR now */
    return;
  }

  /* If a window is there already we destroy it */
  if (vdp_sink->window) {
    gst_vdp_sink_window_destroy (vdp_sink, vdp_sink->window);
    vdp_sink->window = NULL;
  }

  /* If the xid is 0 we go back to an internal window */
  if (xwindow_id == 0) {
    /* If no width/height caps nego did not happen window will be created
       during caps nego then */
    if (GST_VIDEO_SINK_WIDTH (vdp_sink) && GST_VIDEO_SINK_HEIGHT (vdp_sink)) {
      window = gst_vdp_sink_window_new (vdp_sink,
          GST_VIDEO_SINK_WIDTH (vdp_sink), GST_VIDEO_SINK_HEIGHT (vdp_sink));
    }
  } else {
    window = g_new0 (GstVdpWindow, 1);

    window->win = xwindow_id;

    /* We get window geometry, set the event we want to receive,
       and create a GC */
    g_mutex_lock (vdp_sink->x_lock);
    XGetWindowAttributes (vdp_sink->device->display, window->win, &attr);
    window->width = attr.width;
    window->height = attr.height;
    window->internal = FALSE;
    if (vdp_sink->handle_events) {
      XSelectInput (vdp_sink->device->display, window->win, ExposureMask |
          StructureNotifyMask | PointerMotionMask | KeyPressMask |
          KeyReleaseMask);
    }
    g_mutex_unlock (vdp_sink->x_lock);

    gst_vdp_sink_window_setup_vdpau (vdp_sink, window);
  }

  if (window)
    vdp_sink->window = window;

  g_mutex_unlock (vdp_sink->flow_lock);
}

static void
gst_vdp_sink_expose (GstXOverlay * overlay)
{
  gst_vdp_sink_show_frame (GST_BASE_SINK (overlay), NULL);
}

static void
gst_vdp_sink_set_event_handling (GstXOverlay * overlay, gboolean handle_events)
{
  VdpSink *vdp_sink = GST_VDP_SINK (overlay);

  vdp_sink->handle_events = handle_events;

  g_mutex_lock (vdp_sink->flow_lock);

  if (G_UNLIKELY (!vdp_sink->window)) {
    g_mutex_unlock (vdp_sink->flow_lock);
    return;
  }

  g_mutex_lock (vdp_sink->x_lock);

  if (handle_events) {
    if (vdp_sink->window->internal) {
      XSelectInput (vdp_sink->device->display, vdp_sink->window->win,
          ExposureMask | StructureNotifyMask | PointerMotionMask |
          KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
    } else {
      XSelectInput (vdp_sink->device->display, vdp_sink->window->win,
          ExposureMask | StructureNotifyMask | PointerMotionMask |
          KeyPressMask | KeyReleaseMask);
    }
  } else {
    XSelectInput (vdp_sink->device->display, vdp_sink->window->win, 0);
  }

  g_mutex_unlock (vdp_sink->x_lock);

  g_mutex_unlock (vdp_sink->flow_lock);
}

static void
gst_vdp_sink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_window_handle = gst_vdp_sink_set_window_handle;
  iface->expose = gst_vdp_sink_expose;
  iface->handle_events = gst_vdp_sink_set_event_handling;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_vdp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  VdpSink *vdp_sink;

  g_return_if_fail (GST_IS_VDP_SINK (object));

  vdp_sink = GST_VDP_SINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      vdp_sink->display_name = g_strdup (g_value_get_string (value));
      break;
    case PROP_SYNCHRONOUS:
      vdp_sink->synchronous = g_value_get_boolean (value);
      if (vdp_sink->device) {
        GST_DEBUG_OBJECT (vdp_sink, "XSynchronize called with %s",
            vdp_sink->synchronous ? "TRUE" : "FALSE");
        g_mutex_lock (vdp_sink->x_lock);
        XSynchronize (vdp_sink->device->display, vdp_sink->synchronous);
        g_mutex_unlock (vdp_sink->x_lock);
      }
      break;
    case PROP_PIXEL_ASPECT_RATIO:
    {
      GValue *tmp;

      tmp = g_new0 (GValue, 1);
      g_value_init (tmp, GST_TYPE_FRACTION);

      if (!g_value_transform (value, tmp)) {
        GST_WARNING_OBJECT (vdp_sink,
            "Could not transform string to aspect ratio");
        g_free (tmp);
      } else {
        GST_DEBUG_OBJECT (vdp_sink, "set PAR to %d/%d",
            gst_value_get_fraction_numerator (tmp),
            gst_value_get_fraction_denominator (tmp));
        g_free (vdp_sink->par);
        vdp_sink->par = tmp;
      }
    }
      break;
    case PROP_HANDLE_EVENTS:
      gst_vdp_sink_set_event_handling (GST_X_OVERLAY (vdp_sink),
          g_value_get_boolean (value));
      break;
    case PROP_HANDLE_EXPOSE:
      vdp_sink->handle_expose = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  VdpSink *vdp_sink;

  g_return_if_fail (GST_IS_VDP_SINK (object));

  vdp_sink = GST_VDP_SINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, vdp_sink->display_name);
      break;
    case PROP_SYNCHRONOUS:
      g_value_set_boolean (value, vdp_sink->synchronous);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      if (vdp_sink->par)
        g_value_transform (vdp_sink->par, value);
      break;
    case PROP_HANDLE_EVENTS:
      g_value_set_boolean (value, vdp_sink->handle_events);
      break;
    case PROP_HANDLE_EXPOSE:
      g_value_set_boolean (value, vdp_sink->handle_expose);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_sink_finalize (GObject * object)
{
  VdpSink *vdp_sink;

  vdp_sink = GST_VDP_SINK (object);

  if (vdp_sink->display_name) {
    g_free (vdp_sink->display_name);
    vdp_sink->display_name = NULL;
  }
  if (vdp_sink->par) {
    g_free (vdp_sink->par);
    vdp_sink->par = NULL;
  }
  if (vdp_sink->device_lock) {
    g_mutex_free (vdp_sink->device_lock);
    vdp_sink->device_lock = NULL;
  }
  if (vdp_sink->x_lock) {
    g_mutex_free (vdp_sink->x_lock);
    vdp_sink->x_lock = NULL;
  }
  if (vdp_sink->flow_lock) {
    g_mutex_free (vdp_sink->flow_lock);
    vdp_sink->flow_lock = NULL;
  }

  g_free (vdp_sink->media_title);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vdp_sink_init (VdpSink * vdp_sink)
{
  vdp_sink->device = NULL;

  vdp_sink->display_name = NULL;
  vdp_sink->par = NULL;

  vdp_sink->device_lock = g_mutex_new ();
  vdp_sink->x_lock = g_mutex_new ();
  vdp_sink->flow_lock = g_mutex_new ();

  vdp_sink->synchronous = FALSE;
  vdp_sink->handle_events = TRUE;
  vdp_sink->handle_expose = TRUE;
}

static void
gst_vdp_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class,
      "VDPAU Sink",
      "Sink/Video",
      "VDPAU Sink", "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
}

static void
gst_vdp_sink_class_init (VdpSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_vdp_sink_finalize;
  gobject_class->set_property = gst_vdp_sink_set_property;
  gobject_class->get_property = gst_vdp_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SYNCHRONOUS,
      g_param_spec_boolean ("synchronous", "Synchronous", "When enabled, runs "
          "the X display in synchronous mode. (used only for debugging)", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      g_param_spec_string ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", "1/1",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HANDLE_EVENTS,
      g_param_spec_boolean ("handle-events", "Handle XEvents",
          "When enabled, XEvents will be selected and handled", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HANDLE_EXPOSE,
      g_param_spec_boolean ("handle-expose", "Handle expose",
          "When enabled, "
          "the current frame will always be drawn in response to X Expose "
          "events", TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_vdp_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_vdp_sink_stop);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_vdp_sink_getcaps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_vdp_sink_setcaps);
  gstbasesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_vdp_sink_buffer_alloc);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_vdp_sink_get_times);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_vdp_sink_show_frame);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_vdp_sink_show_frame);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_vdp_sink_event);
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */

/* =========================================== */
/*                                             */
/*          Object typing & Creation           */
/*                                             */
/* =========================================== */

GType
gst_vdp_sink_get_type (void)
{
  static GType vdp_sink_type = 0;

  if (!vdp_sink_type) {
    static const GTypeInfo vdp_sink_info = {
      sizeof (VdpSinkClass),
      gst_vdp_sink_base_init,
      NULL,
      (GClassInitFunc) gst_vdp_sink_class_init,
      NULL,
      NULL,
      sizeof (VdpSink),
      0,
      (GInstanceInitFunc) gst_vdp_sink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_vdp_sink_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_vdp_sink_navigation_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_vdp_sink_xoverlay_init,
      NULL,
      NULL,
    };

    vdp_sink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "VdpSink", &vdp_sink_info, 0);

    g_type_add_interface_static (vdp_sink_type, GST_TYPE_IMPLEMENTS_INTERFACE,
        &iface_info);
    g_type_add_interface_static (vdp_sink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
    g_type_add_interface_static (vdp_sink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
  }

  DEBUG_INIT ();

  return vdp_sink_type;
}
