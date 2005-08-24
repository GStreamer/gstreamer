/* GStreamer
 * Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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

/* Our interfaces */
#include <gst/interfaces/navigation.h>
#include <gst/interfaces/xoverlay.h>

/* Object header */
#include "ximagesink.h"

/* Debugging category */
#include <gst/gstinfo.h>

GST_DEBUG_CATEGORY_EXTERN (gst_debug_ximagesink);
#define GST_CAT_DEFAULT gst_debug_ximagesink

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

//static void gst_ximagesink_buffer_free (GstBuffer * buffer);
static void gst_ximagesink_ximage_destroy (GstXImageSink * ximagesink,
    GstXImageBuffer * ximage);
#if 0
static void gst_ximagesink_send_pending_navigation (GstXImageSink * ximagesink);
#endif

/* ElementFactory information */
static GstElementDetails gst_ximagesink_details =
GST_ELEMENT_DETAILS ("Video sink",
    "Sink/Video",
    "A standard X based videosink",
    "Julien Moutte <julien@moutte.net>");

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
static GstStaticPadTemplate gst_ximagesink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (double) [ 1.0, 100.0 ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_SYNCHRONOUS,
  PROP_PIXEL_ASPECT_RATIO
      /* FILL ME */
};

static GstVideoSinkClass *parent_class = NULL;
static gboolean error_caught = FALSE;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* ximage buffers */

#define GST_TYPE_XIMAGE_BUFFER (gst_ximage_buffer_get_type())

#define GST_IS_XIMAGE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_XIMAGE_BUFFER))
#define GST_XIMAGE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_XIMAGE_BUFFER, GstXImageBuffer))


static void
gst_ximage_buffer_finalize (GstXImageBuffer * ximage_buffer)
{
  GstXImageSink *ximagesink;

  g_return_if_fail (ximage_buffer != NULL);

  if (ximage_buffer->ximagesink == NULL) {
    return;
  }
  ximagesink = ximage_buffer->ximagesink;

  /* If the destroyed image is the current one we destroy our reference too */
  if (ximagesink->cur_image == ximage_buffer)
    ximagesink->cur_image = NULL;

  g_mutex_lock (ximagesink->x_lock);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    if (ximage_buffer->SHMInfo.shmaddr != ((void *) -1)) {
      XShmDetach (ximagesink->xcontext->disp, &ximage_buffer->SHMInfo);
      XSync (ximagesink->xcontext->disp, 0);
      shmdt (ximage_buffer->SHMInfo.shmaddr);
    }
    if (ximage_buffer->SHMInfo.shmid > 0)
      shmctl (ximage_buffer->SHMInfo.shmid, IPC_RMID, 0);
    if (ximage_buffer->ximage)
      XDestroyImage (ximage_buffer->ximage);

  } else
#endif /* HAVE_XSHM */
  {
    if (ximage_buffer->ximage) {
      XDestroyImage (ximage_buffer->ximage);
    }
  }

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);
}

static void
gst_ximage_buffer_init (GTypeInstance * instance, gpointer g_class)
{

}

static void
gst_ximage_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_ximage_buffer_finalize;
}

GType
gst_ximage_buffer_get_type (void)
{
  static GType _gst_ximage_buffer_type;

  if (G_UNLIKELY (_gst_ximage_buffer_type == 0)) {
    static const GTypeInfo ximage_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_ximage_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstXImageBuffer),
      0,
      gst_ximage_buffer_init,
      NULL
    };
    _gst_ximage_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstXImageBuffer", &ximage_buffer_info, 0);
  }
  return _gst_ximage_buffer_type;
}

/* X11 stuff */

static int
gst_ximagesink_handle_xerror (Display * display, XErrorEvent * xevent)
{
  char error_msg[1024];

  XGetErrorText (display, xevent->error_code, error_msg, 1024);
  GST_DEBUG ("ximagesink failed to use XShm calls. error: %s", error_msg);
  error_caught = TRUE;
  return 0;
}

/* This function checks that it is actually really possible to create an image
   using XShm */
static gboolean
gst_ximagesink_check_xshm_calls (GstXContext * xcontext)
{
#ifndef HAVE_XSHM
  return FALSE;
#else
  GstXImageBuffer *ximage = NULL;
  int (*handler) (Display *, XErrorEvent *);
  gboolean result = FALSE;

  g_return_val_if_fail (xcontext != NULL, FALSE);

  ximage = (GstXImageBuffer *) gst_mini_object_new (GST_TYPE_XIMAGE_BUFFER);
  g_return_val_if_fail (ximage != NULL, FALSE);

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_ximagesink_handle_xerror);

  /* Trying to create a 1x1 picture */
  GST_DEBUG ("XShmCreateImage of 1x1");

  ximage->ximage = XShmCreateImage (xcontext->disp, xcontext->visual,
      xcontext->depth, ZPixmap, NULL, &ximage->SHMInfo, 1, 1);
  if (!ximage->ximage) {
    GST_WARNING ("could not XShmCreateImage a 1x1 image");
    goto beach;
  }
  ximage->size = ximage->ximage->height * ximage->ximage->bytes_per_line;

  ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size, IPC_CREAT | 0777);
  if (ximage->SHMInfo.shmid == -1) {
    GST_WARNING ("could not get shared memory of %d bytes", ximage->size);
    goto beach;
  }

  ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, 0, 0);
  if (ximage->SHMInfo.shmaddr == ((void *) -1)) {
    GST_WARNING ("Failed to shmat: %s", g_strerror (errno));
    goto beach;
  }

  ximage->ximage->data = ximage->SHMInfo.shmaddr;
  ximage->SHMInfo.readOnly = FALSE;

  if (XShmAttach (xcontext->disp, &ximage->SHMInfo) == 0) {
    GST_WARNING ("Failed to XShmAttach");
    goto beach;
  }

  XSync (xcontext->disp, 0);

  XShmDetach (xcontext->disp, &ximage->SHMInfo);
  XSync (xcontext->disp, FALSE);

  shmdt (ximage->SHMInfo.shmaddr);
  shmctl (ximage->SHMInfo.shmid, IPC_RMID, 0);

  /* To be sure, reset the SHMInfo entry */
  ximage->SHMInfo.shmaddr = ((void *) -1);

  /* store whether we succeeded in result and reset error_caught */
  result = !error_caught;
  error_caught = FALSE;

beach:
  XSetErrorHandler (handler);

  gst_buffer_unref (GST_BUFFER (ximage));

  XSync (xcontext->disp, FALSE);
  return result;
#endif /* HAVE_XSHM */
}

/* This function handles GstXImageBuffer creation depending on XShm availability */
static GstXImageBuffer *
gst_ximagesink_ximage_new (GstXImageSink * ximagesink, gint width, gint height)
{
  GstXImageBuffer *ximage = NULL;
  gboolean succeeded = FALSE;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  GST_DEBUG_OBJECT (ximagesink, "creating %dx%d", width, height);

  ximage = (GstXImageBuffer *) gst_mini_object_new (GST_TYPE_XIMAGE_BUFFER);

  ximage->width = width;
  ximage->height = height;
  ximage->ximagesink = ximagesink;

  g_mutex_lock (ximagesink->x_lock);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    ximage->ximage = XShmCreateImage (ximagesink->xcontext->disp,
        ximagesink->xcontext->visual,
        ximagesink->xcontext->depth,
        ZPixmap, NULL, &ximage->SHMInfo, ximage->width, ximage->height);
    if (!ximage->ximage) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("could not XShmCreateImage a %dx%d image"));
      goto beach;
    }

    /* we have to use the returned bytes_per_line for our shm size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;
    GST_DEBUG_OBJECT (ximagesink, "XShm image size is %d, width %d, stride %d",
        ximage->size, ximage->width, ximage->ximage->bytes_per_line);

    ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size,
        IPC_CREAT | 0777);
    if (ximage->SHMInfo.shmid == -1) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("could not get shared memory of %d bytes", ximage->size));
      goto beach;
    }

    ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, 0, 0);
    if (ximage->SHMInfo.shmaddr == ((void *) -1)) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("Failed to shmat: %s", g_strerror (errno)));
      goto beach;
    }

    ximage->ximage->data = ximage->SHMInfo.shmaddr;
    ximage->SHMInfo.readOnly = FALSE;

    if (XShmAttach (ximagesink->xcontext->disp, &ximage->SHMInfo) == 0) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("Failed to XShmAttach"));
      goto beach;
    }

    XSync (ximagesink->xcontext->disp, FALSE);
  } else
#endif /* HAVE_XSHM */
  {
    ximage->ximage = XCreateImage (ximagesink->xcontext->disp,
        ximagesink->xcontext->visual,
        ximagesink->xcontext->depth,
        ZPixmap, 0, NULL,
        ximage->width, ximage->height, ximagesink->xcontext->bpp, 0);
    if (!ximage->ximage) {
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
          ("could not XCreateImage a %dx%d image"));
      goto beach;
    }

    /* we have to use the returned bytes_per_line for our image size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;
    ximage->ximage->data = g_malloc (ximage->size);

    XSync (ximagesink->xcontext->disp, FALSE);
  }
  succeeded = TRUE;

  GST_BUFFER_DATA (ximage) = (guchar *) ximage->ximage->data;
  GST_BUFFER_SIZE (ximage) = ximage->size;

  g_mutex_unlock (ximagesink->x_lock);

beach:
  if (!succeeded) {
    gst_buffer_unref (GST_BUFFER (ximage));
    ximage = NULL;
  }

  return ximage;
}

/* This function destroys a GstXImageBuffer handling XShm availability */
static void
gst_ximagesink_ximage_destroy (GstXImageSink * ximagesink,
    GstXImageBuffer * ximage)
{
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* If the destroyed image is the current one we destroy our reference too */
  if (ximagesink->cur_image == ximage)
    ximagesink->cur_image = NULL;

  g_mutex_lock (ximagesink->x_lock);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    if (ximage->SHMInfo.shmaddr != ((void *) -1)) {
      XShmDetach (ximagesink->xcontext->disp, &ximage->SHMInfo);
      XSync (ximagesink->xcontext->disp, 0);
      shmdt (ximage->SHMInfo.shmaddr);
    }
    if (ximage->SHMInfo.shmid > 0)
      shmctl (ximage->SHMInfo.shmid, IPC_RMID, 0);
    if (ximage->ximage)
      XDestroyImage (ximage->ximage);

  } else
#endif /* HAVE_XSHM */
  {
    if (ximage->ximage) {
      XDestroyImage (ximage->ximage);
    }
  }

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (ximage);
}

/* This function puts a GstXImageBuffer on a GstXImageSink's window */
static void
gst_ximagesink_ximage_put (GstXImageSink * ximagesink, GstXImageBuffer * ximage)
{
  gint x, y;
  gint w, h;

  g_return_if_fail (ximage != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* Store a reference to the last image we put */
  if (ximagesink->cur_image != ximage)
    ximagesink->cur_image = ximage;

  /* We center the image in the window; so calculate top left corner location */
  x = MAX (0, (ximagesink->xwindow->width - ximage->width) / 2);
  y = MAX (0, (ximagesink->xwindow->height - ximage->height) / 2);

  w = ximage->width;
  h = ximage->height;

  g_mutex_lock (ximagesink->x_lock);
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    GST_LOG_OBJECT (ximagesink,
        "XShmPutImage, src: %d, %d - dest: %d, %d, dim: %dx%d, win %dx%d",
        0, 0, x, y, w, h, ximagesink->xwindow->width,
        ximagesink->xwindow->height);
    XShmPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win,
        ximagesink->xwindow->gc, ximage->ximage, 0, 0, x, y, w, h, FALSE);
  } else
#endif /* HAVE_XSHM */
  {
    XPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win,
        ximagesink->xwindow->gc, ximage->ximage, 0, 0, x, y, w, h);
  }

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);
}

static gboolean
gst_ximagesink_xwindow_decorate (GstXImageSink * ximagesink,
    GstXWindow * window)
{
  Atom hints_atom = None;
  MotifWmHints *hints;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), FALSE);
  g_return_val_if_fail (window != NULL, FALSE);

  g_mutex_lock (ximagesink->x_lock);

  hints_atom = XInternAtom (ximagesink->xcontext->disp, "_MOTIF_WM_HINTS", 1);
  if (hints_atom == None) {
    g_mutex_unlock (ximagesink->x_lock);
    return FALSE;
  }

  hints = g_malloc0 (sizeof (MotifWmHints));

  hints->flags |= MWM_HINTS_DECORATIONS;
  hints->decorations = 1 << 0;

  XChangeProperty (ximagesink->xcontext->disp, window->win,
      hints_atom, hints_atom, 32, PropModeReplace,
      (guchar *) hints, sizeof (MotifWmHints) / sizeof (long));

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (hints);

  return TRUE;
}

/* This function handles a GstXWindow creation */
static GstXWindow *
gst_ximagesink_xwindow_new (GstXImageSink * ximagesink, gint width, gint height)
{
  GstXWindow *xwindow = NULL;
  XGCValues values;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);

  xwindow = g_new0 (GstXWindow, 1);

  xwindow->width = width;
  xwindow->height = height;
  xwindow->internal = TRUE;

  g_mutex_lock (ximagesink->x_lock);

  xwindow->win = XCreateSimpleWindow (ximagesink->xcontext->disp,
      ximagesink->xcontext->root,
      0, 0, xwindow->width, xwindow->height, 0, 0, ximagesink->xcontext->black);

  XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
      StructureNotifyMask | PointerMotionMask | KeyPressMask |
      KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);

  xwindow->gc = XCreateGC (ximagesink->xcontext->disp, xwindow->win,
      0, &values);

  XMapRaised (ximagesink->xcontext->disp, xwindow->win);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  gst_ximagesink_xwindow_decorate (ximagesink, xwindow);

  gst_x_overlay_got_xwindow_id (GST_X_OVERLAY (ximagesink), xwindow->win);

  return xwindow;
}

/* This function destroys a GstXWindow */
static void
gst_ximagesink_xwindow_destroy (GstXImageSink * ximagesink,
    GstXWindow * xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  g_mutex_lock (ximagesink->x_lock);

  /* If we did not create that window we just free the GC and let it live */
  if (xwindow->internal)
    XDestroyWindow (ximagesink->xcontext->disp, xwindow->win);
  else
    XSelectInput (ximagesink->xcontext->disp, xwindow->win, 0);

  XFreeGC (ximagesink->xcontext->disp, xwindow->gc);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (xwindow);
}

/* This function resizes a GstXWindow */
static void
gst_ximagesink_xwindow_resize (GstXImageSink * ximagesink, GstXWindow * xwindow,
    guint width, guint height)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  g_mutex_lock (ximagesink->x_lock);

  xwindow->width = width;
  xwindow->height = height;

  XResizeWindow (ximagesink->xcontext->disp, xwindow->win,
      xwindow->width, xwindow->height);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);
}

static void
gst_ximagesink_xwindow_clear (GstXImageSink * ximagesink, GstXWindow * xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  g_mutex_lock (ximagesink->x_lock);

  XSetForeground (ximagesink->xcontext->disp, xwindow->gc,
      ximagesink->xcontext->black);

  XFillRectangle (ximagesink->xcontext->disp, xwindow->win, xwindow->gc,
      0, 0, xwindow->width, xwindow->height);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);
}

static void
gst_ximagesink_xwindow_update_geometry (GstXImageSink * ximagesink,
    GstXWindow * xwindow)
{
  XWindowAttributes attr;

  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* Update the window geometry */
  g_mutex_lock (ximagesink->x_lock);
  XGetWindowAttributes (ximagesink->xcontext->disp,
      ximagesink->xwindow->win, &attr);
  g_mutex_unlock (ximagesink->x_lock);

  ximagesink->xwindow->width = attr.width;
  ximagesink->xwindow->height = attr.height;
}

static void
gst_ximagesink_renegotiate_size (GstXImageSink * ximagesink)
{
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  if (!ximagesink->xwindow)
    return;

  gst_ximagesink_xwindow_update_geometry (ximagesink, ximagesink->xwindow);

  if (ximagesink->sw_scaling_failed)
    return;

  if (ximagesink->xwindow->width <= 1 || ximagesink->xwindow->height <= 1)
    return;

  /* Window got resized or moved. We do caps negotiation again to get video
     scaler to fit that new size only if size of the window differs from our
     size. */

  if (GST_VIDEO_SINK_WIDTH (ximagesink) != ximagesink->xwindow->width ||
      GST_VIDEO_SINK_HEIGHT (ximagesink) != ximagesink->xwindow->height) {
    GstCaps *caps;

    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, ximagesink->xcontext->bpp,
        "depth", G_TYPE_INT, ximagesink->xcontext->depth,
        "endianness", G_TYPE_INT, ximagesink->xcontext->endianness,
        "red_mask", G_TYPE_INT, ximagesink->xcontext->visual->red_mask,
        "green_mask", G_TYPE_INT, ximagesink->xcontext->visual->green_mask,
        "blue_mask", G_TYPE_INT, ximagesink->xcontext->visual->blue_mask,
        "width", G_TYPE_INT, ximagesink->xwindow->width,
        "height", G_TYPE_INT, ximagesink->xwindow->height,
        "framerate", G_TYPE_DOUBLE, ximagesink->framerate, NULL);

    if (ximagesink->par) {
      int nom, den;

      nom = gst_value_get_fraction_numerator (ximagesink->par);
      den = gst_value_get_fraction_denominator (ximagesink->par);
      gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          nom, den, NULL);
    }

    if (gst_pad_peer_accept_caps (GST_VIDEO_SINK_PAD (ximagesink), caps)) {
      g_mutex_lock (ximagesink->pool_lock);
      gst_caps_replace (&ximagesink->desired_caps, caps);
      g_mutex_unlock (ximagesink->pool_lock);
    } else {
      ximagesink->sw_scaling_failed = TRUE;
      gst_caps_unref (caps);
    }
  }
}

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_ximagesink_handle_xevents (GstXImageSink * ximagesink)
{
  XEvent e;
  guint pointer_x = 0, pointer_y = 0;
  gboolean pointer_moved = FALSE;

  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  gst_ximagesink_renegotiate_size (ximagesink);

  /* Then we get all pointer motion events, only the last position is
     interesting. */
  g_mutex_lock (ximagesink->x_lock);
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
          ximagesink->xwindow->win, PointerMotionMask, &e)) {
    g_mutex_unlock (ximagesink->x_lock);

    switch (e.type) {
      case MotionNotify:
        pointer_x = e.xmotion.x;
        pointer_y = e.xmotion.y;
        pointer_moved = TRUE;
        break;
      default:
        break;
    }

    g_mutex_lock (ximagesink->x_lock);
  }
  g_mutex_unlock (ximagesink->x_lock);

  if (pointer_moved) {
    GST_DEBUG ("ximagesink pointer moved over window at %d,%d",
        pointer_x, pointer_y);
    gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
        "mouse-move", 0, pointer_x, pointer_y);
  }

  /* We get all remaining events on our window to throw them upstream */
  g_mutex_lock (ximagesink->x_lock);
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
          ximagesink->xwindow->win,
          KeyPressMask | KeyReleaseMask |
          ButtonPressMask | ButtonReleaseMask, &e)) {
    KeySym keysym;

    /* We lock only for the X function call */
    g_mutex_unlock (ximagesink->x_lock);

    switch (e.type) {
      case ButtonPress:
        /* Mouse button pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("ximagesink button %d pressed over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
            "mouse-button-press", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case ButtonRelease:
        GST_DEBUG ("ximagesink button %d release over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
            "mouse-button-release", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case KeyPress:
      case KeyRelease:
        /* Key pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("ximagesink key %d pressed over window at %d,%d",
            e.xkey.keycode, e.xkey.x, e.xkey.x);
        keysym = XKeycodeToKeysym (ximagesink->xcontext->disp,
            e.xkey.keycode, 0);
        if (keysym != NoSymbol) {
          gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
              e.type == KeyPress ?
              "key-press" : "key-release", XKeysymToString (keysym));
        } else {
          gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
              e.type == KeyPress ? "key-press" : "key-release", "unknown");
        }
        break;
      default:
        GST_DEBUG ("ximagesink unhandled X event (%d)", e.type);
    }
    g_mutex_lock (ximagesink->x_lock);
  }
  g_mutex_unlock (ximagesink->x_lock);
}

/* This function calculates the pixel aspect ratio based on the properties
 * in the xcontext structure and stores it there. */
static void
gst_ximagesink_calculate_pixel_aspect_ratio (GstXContext * xcontext)
{
  gint par[][2] = {
    {1, 1},                     /* regular screen */
    {16, 15},                   /* PAL TV */
    {11, 10},                   /* 525 line Rec.601 video */
    {54, 59}                    /* 625 line Rec.601 video */
  };
  gint i;
  gint index;
  gdouble ratio;
  gdouble delta;

#define DELTA(idx) (ABS (ratio - ((gdouble) par[idx][0] / par[idx][1])))

  /* first calculate the "real" ratio based on the X values;
   * which is the "physical" w/h divided by the w/h in pixels of the display */
  ratio = (gdouble) (xcontext->widthmm * xcontext->height)
      / (xcontext->heightmm * xcontext->width);

  /* DirectFB's X in 720x576 reports the physical dimensions wrong, so
   * override here */
  if (xcontext->width == 720 && xcontext->height == 576) {
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

  g_free (xcontext->par);
  xcontext->par = g_new0 (GValue, 1);
  g_value_init (xcontext->par, GST_TYPE_FRACTION);
  gst_value_set_fraction (xcontext->par, par[index][0], par[index][1]);
  GST_DEBUG ("set xcontext PAR to %d/%d",
      gst_value_get_fraction_numerator (xcontext->par),
      gst_value_get_fraction_denominator (xcontext->par));
}

/* This function gets the X Display and global info about it. Everything is
   stored in our object and will be cleaned when the object is disposed. Note
   here that caps for supported format are generated without any window or
   image creation */
static GstXContext *
gst_ximagesink_xcontext_get (GstXImageSink * ximagesink)
{
  GstXContext *xcontext = NULL;
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);

  xcontext = g_new0 (GstXContext, 1);

  g_mutex_lock (ximagesink->x_lock);

  xcontext->disp = XOpenDisplay (ximagesink->display_name);

  if (!xcontext->disp) {
    g_mutex_unlock (ximagesink->x_lock);
    g_free (xcontext);
    GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE, (NULL),
        ("Could not open display"));
    return NULL;
  }

  xcontext->screen = DefaultScreenOfDisplay (xcontext->disp);
  xcontext->screen_num = DefaultScreen (xcontext->disp);
  xcontext->visual = DefaultVisual (xcontext->disp, xcontext->screen_num);
  xcontext->root = DefaultRootWindow (xcontext->disp);
  xcontext->white = XWhitePixel (xcontext->disp, xcontext->screen_num);
  xcontext->black = XBlackPixel (xcontext->disp, xcontext->screen_num);
  xcontext->depth = DefaultDepthOfScreen (xcontext->screen);

  xcontext->width = DisplayWidth (xcontext->disp, xcontext->screen_num);
  xcontext->height = DisplayHeight (xcontext->disp, xcontext->screen_num);
  xcontext->widthmm = DisplayWidthMM (xcontext->disp, xcontext->screen_num);
  xcontext->heightmm = DisplayHeightMM (xcontext->disp, xcontext->screen_num);

  GST_DEBUG_OBJECT (ximagesink, "X reports %dx%d pixels and %d mm x %d mm",
      xcontext->width, xcontext->height, xcontext->widthmm, xcontext->heightmm);

  gst_ximagesink_calculate_pixel_aspect_ratio (xcontext);

  /* We get supported pixmap formats at supported depth */
  px_formats = XListPixmapFormats (xcontext->disp, &nb_formats);

  if (!px_formats) {
    XCloseDisplay (xcontext->disp);
    g_mutex_unlock (ximagesink->x_lock);
    g_free (xcontext);
    return NULL;
  }

  /* We get bpp value corresponding to our running depth */
  for (i = 0; i < nb_formats; i++) {
    if (px_formats[i].depth == xcontext->depth)
      xcontext->bpp = px_formats[i].bits_per_pixel;
  }

  XFree (px_formats);

  xcontext->endianness =
      (ImageByteOrder (xcontext->disp) ==
      LSBFirst) ? G_LITTLE_ENDIAN : G_BIG_ENDIAN;

#ifdef HAVE_XSHM
  /* Search for XShm extension support */
  if (XShmQueryExtension (xcontext->disp) &&
      gst_ximagesink_check_xshm_calls (xcontext)) {
    xcontext->use_xshm = TRUE;
    GST_DEBUG ("ximagesink is using XShm extension");
  } else {
    xcontext->use_xshm = FALSE;
    GST_DEBUG ("ximagesink is not using XShm extension");
  }
#endif /* HAVE_XSHM */

  /* our caps system handles 24/32bpp RGB as big-endian. */
  if ((xcontext->bpp == 24 || xcontext->bpp == 32) &&
      xcontext->endianness == G_LITTLE_ENDIAN) {
    xcontext->endianness = G_BIG_ENDIAN;
    xcontext->visual->red_mask = GUINT32_TO_BE (xcontext->visual->red_mask);
    xcontext->visual->green_mask = GUINT32_TO_BE (xcontext->visual->green_mask);
    xcontext->visual->blue_mask = GUINT32_TO_BE (xcontext->visual->blue_mask);
    if (xcontext->bpp == 24) {
      xcontext->visual->red_mask >>= 8;
      xcontext->visual->green_mask >>= 8;
      xcontext->visual->blue_mask >>= 8;
    }
  }

  /* update object's par with calculated one if not set yet */
  if (!ximagesink->par) {
    ximagesink->par = g_new0 (GValue, 1);
    gst_value_init_and_copy (ximagesink->par, xcontext->par);
    GST_DEBUG_OBJECT (ximagesink, "set calculated PAR on object's PAR");
  }
  xcontext->caps = gst_caps_new_simple ("video/x-raw-rgb",
      "bpp", G_TYPE_INT, xcontext->bpp,
      "depth", G_TYPE_INT, xcontext->depth,
      "endianness", G_TYPE_INT, xcontext->endianness,
      "red_mask", G_TYPE_INT, xcontext->visual->red_mask,
      "green_mask", G_TYPE_INT, xcontext->visual->green_mask,
      "blue_mask", G_TYPE_INT, xcontext->visual->blue_mask,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);
  if (ximagesink->par) {
    int nom, den;

    nom = gst_value_get_fraction_numerator (ximagesink->par);
    den = gst_value_get_fraction_denominator (ximagesink->par);
    gst_caps_set_simple (xcontext->caps, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, nom, den, NULL);
  }

  g_mutex_unlock (ximagesink->x_lock);

  return xcontext;
}

/* This function cleans the X context. Closing the Display and unrefing the
   caps for supported formats. */
static void
gst_ximagesink_xcontext_clear (GstXImageSink * ximagesink)
{
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  g_return_if_fail (ximagesink->xcontext != NULL);

  gst_caps_unref (ximagesink->xcontext->caps);
  g_free (ximagesink->xcontext->par);
  g_free (ximagesink->par);
  ximagesink->par = NULL;

  g_mutex_lock (ximagesink->x_lock);

  XCloseDisplay (ximagesink->xcontext->disp);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (ximagesink->xcontext);
  ximagesink->xcontext = NULL;
}

static void
gst_ximagesink_imagepool_clear (GstXImageSink * ximagesink)
{
  g_mutex_lock (ximagesink->pool_lock);

  while (ximagesink->image_pool) {
    GstXImageBuffer *ximage = ximagesink->image_pool->data;

    ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
        ximagesink->image_pool);
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  }

  g_mutex_unlock (ximagesink->pool_lock);
}

/* Element stuff */

#if 0
static GstCaps *
gst_ximagesink_fixate (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *newcaps;

  if (gst_caps_get_size (caps) > 1)
    return NULL;

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  /* if par is set and either w or h is set, we can set the other */

  if (gst_caps_structure_fixate_field_nearest_int (structure, "width", 320)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_int (structure, "height", 240)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
          30.0)) {
    return newcaps;
  }

  gst_caps_unref (newcaps);
  return NULL;
}
#endif

static GstCaps *
gst_ximagesink_getcaps (GstBaseSink * bsink)
{
  GstXImageSink *ximagesink;
  GstCaps *caps;
  int i;

  ximagesink = GST_XIMAGESINK (bsink);

  if (ximagesink->xcontext)
    return gst_caps_ref (ximagesink->xcontext->caps);

  /* get a template copy and add the pixel aspect ratio */
  caps =
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK (ximagesink)->
          sinkpad));
  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    if (ximagesink->par) {
      int nom, den;

      nom = gst_value_get_fraction_numerator (ximagesink->par);
      den = gst_value_get_fraction_denominator (ximagesink->par);
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, nom, den, NULL);
    }
  }
  return caps;
}

static gboolean
gst_ximagesink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstXImageSink *ximagesink;
  gboolean ret = TRUE;
  GstStructure *structure;
  const GValue *par;

  ximagesink = GST_XIMAGESINK (bsink);

  if (!ximagesink->xcontext)
    return FALSE;

  GST_DEBUG_OBJECT (ximagesink,
      "sinkconnect possible caps %" GST_PTR_FORMAT " with given caps %"
      GST_PTR_FORMAT, ximagesink->xcontext->caps, caps);

  structure = gst_caps_get_structure (caps, 0);
  if (GST_VIDEO_SINK_WIDTH (ximagesink) == 0) {
    ret &= gst_structure_get_int (structure, "width",
        &(GST_VIDEO_SINK_WIDTH (ximagesink)));
    ret &= gst_structure_get_int (structure, "height",
        &(GST_VIDEO_SINK_HEIGHT (ximagesink)));
  }
  ret &= gst_structure_get_double (structure,
      "framerate", &ximagesink->framerate);
  if (!ret)
    return FALSE;

  g_mutex_lock (ximagesink->stream_lock);

  /* if the caps contain pixel-aspect-ratio, they have to match ours,
   * otherwise linking should fail */
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par && gst_value_compare (par, ximagesink->par) != GST_VALUE_EQUAL)
    goto wrong_aspect;

  /* Creating our window and our image */
  g_assert (GST_VIDEO_SINK_WIDTH (ximagesink) > 0);
  g_assert (GST_VIDEO_SINK_HEIGHT (ximagesink) > 0);
  if (!ximagesink->xwindow) {
    ximagesink->xwindow = gst_ximagesink_xwindow_new (ximagesink,
        GST_VIDEO_SINK_WIDTH (ximagesink), GST_VIDEO_SINK_HEIGHT (ximagesink));
  } else {
    if (ximagesink->xwindow->internal) {
      gst_ximagesink_xwindow_resize (ximagesink, ximagesink->xwindow,
          GST_VIDEO_SINK_WIDTH (ximagesink),
          GST_VIDEO_SINK_HEIGHT (ximagesink));
    }
  }

  /* If our ximage has changed we destroy it, next chain iteration will create
     a new one */
  if ((ximagesink->ximage) &&
      ((GST_VIDEO_SINK_WIDTH (ximagesink) != ximagesink->ximage->width) ||
          (GST_VIDEO_SINK_HEIGHT (ximagesink) != ximagesink->ximage->height))) {
    gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
    ximagesink->ximage = NULL;
  }

  g_mutex_unlock (ximagesink->stream_lock);

  gst_x_overlay_got_desired_size (GST_X_OVERLAY (ximagesink),
      GST_VIDEO_SINK_WIDTH (ximagesink), GST_VIDEO_SINK_HEIGHT (ximagesink));

  return TRUE;

  /* ERRORS */
wrong_aspect:
  {
    g_mutex_unlock (ximagesink->stream_lock);
    GST_INFO_OBJECT (ximagesink, "pixel aspect ratio does not match");
    return FALSE;
  }
}

static GstElementStateReturn
gst_ximagesink_change_state (GstElement * element)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      /* Initializing the XContext */
      if (!ximagesink->xcontext)
        ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink);
      if (!ximagesink->xcontext)
        return GST_STATE_FAILURE;
      /* call XSynchronize with the current value of synchronous */
      GST_DEBUG_OBJECT (ximagesink, "XSynchronize called with %s",
          ximagesink->synchronous ? "TRUE" : "FALSE");
      g_mutex_lock (ximagesink->x_lock);
      XSynchronize (ximagesink->xcontext->disp, ximagesink->synchronous);
      g_mutex_unlock (ximagesink->x_lock);
      break;
    case GST_STATE_READY_TO_PAUSED:
      ximagesink->time = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      g_mutex_lock (ximagesink->stream_lock);
      if (ximagesink->xwindow)
        gst_ximagesink_xwindow_clear (ximagesink, ximagesink->xwindow);
      ximagesink->framerate = 0;
      ximagesink->sw_scaling_failed = FALSE;
      GST_VIDEO_SINK_WIDTH (ximagesink) = 0;
      GST_VIDEO_SINK_HEIGHT (ximagesink) = 0;
      g_mutex_unlock (ximagesink->stream_lock);
      break;
    case GST_STATE_READY_TO_NULL:
      /* We are cleaning our resources here, yes i know chain is not running
         but the interface can be called to set a window from a different thread
         and that would crash */
      g_mutex_lock (ximagesink->stream_lock);
      if (ximagesink->ximage) {
        gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
        ximagesink->ximage = NULL;
      }

      if (ximagesink->image_pool)
        gst_ximagesink_imagepool_clear (ximagesink);

      if (ximagesink->xwindow) {
        gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
        ximagesink->xwindow = NULL;
      }

      if (ximagesink->xcontext) {
        gst_ximagesink_xcontext_clear (ximagesink);
        ximagesink->xcontext = NULL;
      }
      g_mutex_unlock (ximagesink->stream_lock);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ximagesink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (ximagesink->framerate > 0) {
        *end = *start + GST_SECOND / ximagesink->framerate;
      }
    }
  }
}

static GstFlowReturn
gst_ximagesink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstXImageSink *ximagesink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  ximagesink = GST_XIMAGESINK (bsink);

  g_mutex_lock (ximagesink->stream_lock);

  /* If this buffer has been allocated using our buffer management we simply
     put the ximage which is in the PRIVATE pointer */
  if (GST_IS_XIMAGE_BUFFER (buf)) {
    GST_LOG_OBJECT (ximagesink, "buffer from our pool, writing directly");
    gst_ximagesink_ximage_put (ximagesink, GST_XIMAGE_BUFFER (buf));
  } else {
    /* Else we have to copy the data into our private image, */
    /* if we have one... */
    GST_LOG_OBJECT (ximagesink, "normal buffer, copying from it");
    if (!ximagesink->ximage) {
      GST_DEBUG_OBJECT (ximagesink, "creating our ximage");
      ximagesink->ximage = gst_ximagesink_ximage_new (ximagesink,
          GST_VIDEO_SINK_WIDTH (ximagesink),
          GST_VIDEO_SINK_HEIGHT (ximagesink));
      if (!ximagesink->ximage)
        goto no_ximage;
    }
    memcpy (ximagesink->ximage->ximage->data,
        GST_BUFFER_DATA (buf),
        MIN (GST_BUFFER_SIZE (buf), ximagesink->ximage->size));
    gst_ximagesink_ximage_put (ximagesink, ximagesink->ximage);
  }

  gst_ximagesink_handle_xevents (ximagesink);
#if 0
  gst_ximagesink_send_pending_navigation (ximagesink);
#endif

  g_mutex_unlock (ximagesink->stream_lock);

  return GST_FLOW_OK;

  /* ERRORS */
no_ximage:
  {
    /* No image available. That's very bad ! */
    g_mutex_unlock (ximagesink->stream_lock);
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (ximagesink, CORE, NEGOTIATION, (NULL),
        ("Failed creating an XImage in ximagesink chain function."));
    return GST_FLOW_ERROR;
  }
}

/* Buffer management */

#if 0
static void
gst_ximagesink_buffer_free (GstBuffer * buffer)
{
  GstXImageSink *ximagesink;
  GstXImageBuffer *ximage;

  ximage = GST_BUFFER_PRIVATE (buffer);

  g_assert (GST_IS_XIMAGESINK (ximage->ximagesink));
  ximagesink = ximage->ximagesink;

  /* If our geometry changed we can't reuse that image. */
  if ((ximage->width != GST_VIDEO_SINK_WIDTH (ximagesink)) ||
      (ximage->height != GST_VIDEO_SINK_HEIGHT (ximagesink)))
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  else {
    /* In that case we can reuse the image and add it to our image pool. */
    g_mutex_lock (ximagesink->pool_lock);
    ximagesink->image_pool = g_slist_prepend (ximagesink->image_pool, ximage);
    g_mutex_unlock (ximagesink->pool_lock);
  }
}
#endif

static GstFlowReturn
gst_ximagesink_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstXImageSink *ximagesink;
  GstXImageBuffer *ximage = NULL;
  gboolean not_found = TRUE;

  ximagesink = GST_XIMAGESINK (bsink);

  /* FIXME, we should just parse the caps, and provide a buffer in this format,
   * we should not just reconfigure ourselves yet */
  if (caps && !GST_PAD_CAPS (GST_VIDEO_SINK_PAD (ximagesink))) {
    if (!gst_ximagesink_setcaps (bsink, caps)) {
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  g_mutex_lock (ximagesink->pool_lock);

  /* Walking through the pool cleaning unsuable images and searching for a
     suitable one */
  while (not_found && ximagesink->image_pool) {
    ximage = ximagesink->image_pool->data;

    if (ximage) {
      /* Removing from the pool */
      ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
          ximagesink->image_pool);

      if ((ximage->width != GST_VIDEO_SINK_WIDTH (ximagesink)) ||
          (ximage->height != GST_VIDEO_SINK_HEIGHT (ximagesink))) {
        /* This image is unusable. Destroying... */
        gst_ximagesink_ximage_destroy (ximagesink, ximage);
        ximage = NULL;
      } else {
        /* We found a suitable image */
        break;
      }
    }
  }

  if (!ximage) {
    /* We found no suitable image in the pool. Creating... */
    gint height, width;

    if (ximagesink->desired_caps) {
      GstStructure *s = gst_caps_get_structure (ximagesink->desired_caps, 0);

      gst_structure_get_int (s, "width", &width);
      gst_structure_get_int (s, "height", &height);
    } else {
      width = GST_VIDEO_SINK_WIDTH (ximagesink);
      height = GST_VIDEO_SINK_HEIGHT (ximagesink);
    }

    GST_DEBUG_OBJECT (ximagesink, "no usable image in pool, creating ximage");
    ximage = gst_ximagesink_ximage_new (ximagesink, width, height);

    if (ximagesink->desired_caps)
      gst_buffer_set_caps (GST_BUFFER (ximage), ximagesink->desired_caps);
    else
      /* fixme we have no guarantee that the ximage is actually of these caps,
         do we? */
      gst_buffer_set_caps (GST_BUFFER (ximage), caps);
  }

  g_mutex_unlock (ximagesink->pool_lock);

  *buf = GST_BUFFER (ximage);

  return GST_FLOW_OK;
}

/* Interfaces stuff */

static gboolean
gst_ximagesink_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION || type == GST_TYPE_X_OVERLAY);
  return TRUE;
}

static void
gst_ximagesink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_ximagesink_interface_supported;
}

#if 0
/*
 * This function is called with the stream-lock held
 */
static void
gst_ximagesink_send_pending_navigation (GstXImageSink * ximagesink)
{
  GSList *cur;
  GSList *pend_events;

  g_mutex_lock (ximagesink->nav_lock);
  pend_events = ximagesink->pend_nav_events;
  ximagesink->pend_nav_events = NULL;
  g_mutex_unlock (ximagesink->nav_lock);

  cur = pend_events;
  while (cur) {
    GstEvent *event = cur->data;
    GstStructure *structure;
    double x, y;
    gint x_offset, y_offset;

    if (event) {
      structure = event->event_data.structure.structure;

      if (!GST_PAD_PEER (GST_VIDEO_SINK_PAD (ximagesink))) {
        gst_event_unref (event);
        cur = g_slist_next (cur);
        continue;
      }

      /* We are not converting the pointer coordinates as there's no hardware
         scaling done here. The only possible scaling is done by videoscale and
         videoscale will have to catch those events and tranform the coordinates
         to match the applied scaling. So here we just add the offset if the image
         is centered in the window.  */

      x_offset = ximagesink->xwindow->width - GST_VIDEO_SINK_WIDTH (ximagesink);
      y_offset =
          ximagesink->xwindow->height - GST_VIDEO_SINK_HEIGHT (ximagesink);

      if (gst_structure_get_double (structure, "pointer_x", &x)) {
        x -= x_offset / 2;
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &y)) {
        y -= y_offset / 2;
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
      }

      gst_pad_send_event (gst_pad_get_peer (GST_VIDEO_SINK_PAD (ximagesink)),
          event);
    }
    cur = g_slist_next (cur);
  }

  g_slist_free (pend_events);
}
#endif

static void
gst_ximagesink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (navigation);
  GstEvent *event;

  event = gst_event_new_custom (GST_EVENT_NAVIGATION, structure);

  g_mutex_lock (ximagesink->nav_lock);
  ximagesink->pend_nav_events =
      g_slist_prepend (ximagesink->pend_nav_events, event);
  g_mutex_unlock (ximagesink->nav_lock);
}

static void
gst_ximagesink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_ximagesink_navigation_send_event;
}

static void
gst_ximagesink_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);
  GstXWindow *xwindow = NULL;
  XWindowAttributes attr;

  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* If we already use that window return */
  if (ximagesink->xwindow && (xwindow_id == ximagesink->xwindow->win))
    return;

  /* If the element has not initialized the X11 context try to do so */
  if (!ximagesink->xcontext)
    ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink);

  if (!ximagesink->xcontext) {
    g_warning ("ximagesink was unable to obtain the X11 context.");
    return;
  }

  /* We acquire the stream lock while setting this window in the element.
     We are basically cleaning tons of stuff replacing the old window, putting
     images while we do that would surely crash */
  g_mutex_lock (ximagesink->stream_lock);

  /* Clear image pool as the images are unusable anyway */
  gst_ximagesink_imagepool_clear (ximagesink);

  /* Clear the ximage */
  if (ximagesink->ximage) {
    gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
    ximagesink->ximage = NULL;
  }

  /* If a window is there already we destroy it */
  if (ximagesink->xwindow) {
    gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
    ximagesink->xwindow = NULL;
  }

  /* If the xid is 0 we go back to an internal window */
  if (xwindow_id == 0) {
    /* If no width/height caps nego did not happen window will be created
       during caps nego then */
    if (GST_VIDEO_SINK_WIDTH (ximagesink) && GST_VIDEO_SINK_HEIGHT (ximagesink)) {
      xwindow = gst_ximagesink_xwindow_new (ximagesink,
          GST_VIDEO_SINK_WIDTH (ximagesink),
          GST_VIDEO_SINK_HEIGHT (ximagesink));
    }
  } else {
    xwindow = g_new0 (GstXWindow, 1);

    xwindow->win = xwindow_id;

    /* We get window geometry, set the event we want to receive,
       and create a GC */
    g_mutex_lock (ximagesink->x_lock);
    XGetWindowAttributes (ximagesink->xcontext->disp, xwindow->win, &attr);
    xwindow->width = attr.width;
    xwindow->height = attr.height;
    xwindow->internal = FALSE;
    XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
        StructureNotifyMask | PointerMotionMask | KeyPressMask |
        KeyReleaseMask);

    xwindow->gc = XCreateGC (ximagesink->xcontext->disp, xwindow->win, 0, NULL);
    g_mutex_unlock (ximagesink->x_lock);

#if 0
    /* If that new window geometry differs from our one we try to
       renegotiate caps */
    if (gst_pad_is_negotiated (GST_VIDEO_SINK_PAD (ximagesink)) &&
        (xwindow->width != GST_VIDEO_SINK_WIDTH (ximagesink) ||
            xwindow->height != GST_VIDEO_SINK_HEIGHT (ximagesink))) {
      GstPadLinkReturn r;
      GstCaps *caps;

      caps = gst_caps_new_simple ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, ximagesink->xcontext->bpp,
          "depth", G_TYPE_INT, ximagesink->xcontext->depth,
          "endianness", G_TYPE_INT, ximagesink->xcontext->endianness,
          "red_mask", G_TYPE_INT, ximagesink->xcontext->visual->red_mask,
          "green_mask", G_TYPE_INT,
          ximagesink->xcontext->visual->green_mask,
          "blue_mask", G_TYPE_INT,
          ximagesink->xcontext->visual->blue_mask,
          "width", G_TYPE_INT, xwindow->width,
          "height", G_TYPE_INT, xwindow->height,
          "framerate", G_TYPE_DOUBLE, ximagesink->framerate, NULL);

      if (ximagesink->par) {
        int nom, den;

        nom = gst_value_get_fraction_numerator (ximagesink->par);
        den = gst_value_get_fraction_denominator (ximagesink->par);
        gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            nom, den, NULL);
      }
      r = gst_pad_try_set_caps (GST_VIDEO_SINK_PAD (ximagesink), caps);

      /* If caps nego succeded updating our size */
      if ((r == GST_PAD_LINK_OK) || (r == GST_PAD_LINK_DONE)) {
        GST_VIDEO_SINK_WIDTH (ximagesink) = xwindow->width;
        GST_VIDEO_SINK_HEIGHT (ximagesink) = xwindow->height;
      }
    }
#endif
  }

  if (xwindow)
    ximagesink->xwindow = xwindow;

  g_mutex_unlock (ximagesink->stream_lock);
}

static void
gst_ximagesink_get_desired_size (GstXOverlay * overlay,
    guint * width, guint * height)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);

  *width = GST_VIDEO_SINK_WIDTH (ximagesink);
  *height = GST_VIDEO_SINK_HEIGHT (ximagesink);
}

static void
gst_ximagesink_expose (GstXOverlay * overlay)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);

  if (!ximagesink->xwindow)
    return;

  /* We don't want chain to iterate while we do that. We might act on random
     cur_image and different geometry */
  g_mutex_lock (ximagesink->stream_lock);

  gst_ximagesink_xwindow_update_geometry (ximagesink, ximagesink->xwindow);

  /* We don't act on internal window from outside that could cause some thread
     race with the video sink own thread checking for configure event */
  if (ximagesink->xwindow->internal)
    return;

  gst_ximagesink_xwindow_clear (ximagesink, ximagesink->xwindow);

  if (ximagesink->cur_image)
    gst_ximagesink_ximage_put (ximagesink, ximagesink->cur_image);

  g_mutex_unlock (ximagesink->stream_lock);
}

static void
gst_ximagesink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_ximagesink_set_xwindow_id;
  iface->get_desired_size = gst_ximagesink_get_desired_size;
  iface->expose = gst_ximagesink_expose;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_ximagesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstXImageSink *ximagesink;

  g_return_if_fail (GST_IS_XIMAGESINK (object));

  ximagesink = GST_XIMAGESINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      ximagesink->display_name = g_strdup (g_value_get_string (value));
      break;
    case PROP_SYNCHRONOUS:
      ximagesink->synchronous = g_value_get_boolean (value);
      if (ximagesink->xcontext) {
        GST_DEBUG_OBJECT (ximagesink, "XSynchronize called with %s",
            ximagesink->synchronous ? "TRUE" : "FALSE");
        g_mutex_lock (ximagesink->x_lock);
        XSynchronize (ximagesink->xcontext->disp, ximagesink->synchronous);
        g_mutex_unlock (ximagesink->x_lock);
      }
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      g_free (ximagesink->par);
      ximagesink->par = g_new0 (GValue, 1);
      g_value_init (ximagesink->par, GST_TYPE_FRACTION);
      if (!g_value_transform (value, ximagesink->par)) {
        g_warning ("Could not transform string to aspect ratio");
        gst_value_set_fraction (ximagesink->par, 1, 1);
      }
      GST_DEBUG_OBJECT (ximagesink, "set PAR to %d/%d",
          gst_value_get_fraction_numerator (ximagesink->par),
          gst_value_get_fraction_denominator (ximagesink->par));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ximagesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstXImageSink *ximagesink;

  g_return_if_fail (GST_IS_XIMAGESINK (object));

  ximagesink = GST_XIMAGESINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, g_strdup (ximagesink->display_name));
      break;
    case PROP_SYNCHRONOUS:
      g_value_set_boolean (value, ximagesink->synchronous);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      if (ximagesink->par)
        g_value_transform (ximagesink->par, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ximagesink_finalize (GObject * object)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (object);

  if (ximagesink->display_name) {
    g_free (ximagesink->display_name);
    ximagesink->display_name = NULL;
  }

  if (ximagesink->par) {
    g_free (ximagesink->par);
    ximagesink->par = NULL;
  }
  if (ximagesink->x_lock) {
    g_mutex_free (ximagesink->x_lock);
    ximagesink->x_lock = NULL;
  }
  if (ximagesink->stream_lock) {
    g_mutex_free (ximagesink->stream_lock);
    ximagesink->stream_lock = NULL;
  }
  if (ximagesink->pool_lock) {
    g_mutex_free (ximagesink->pool_lock);
    ximagesink->pool_lock = NULL;
  }
  if (ximagesink->nav_lock) {
    g_mutex_free (ximagesink->nav_lock);
    ximagesink->nav_lock = NULL;
  }

  while (ximagesink->pend_nav_events) {
    GstEvent *event = ximagesink->pend_nav_events->data;

    ximagesink->pend_nav_events =
        g_slist_delete_link (ximagesink->pend_nav_events,
        ximagesink->pend_nav_events);
    gst_event_unref (event);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ximagesink_init (GstXImageSink * ximagesink)
{
  ximagesink->display_name = NULL;
  ximagesink->xcontext = NULL;
  ximagesink->xwindow = NULL;
  ximagesink->ximage = NULL;
  ximagesink->cur_image = NULL;

  ximagesink->framerate = 0;

  ximagesink->x_lock = g_mutex_new ();
  ximagesink->stream_lock = g_mutex_new ();

  ximagesink->image_pool = NULL;
  ximagesink->pool_lock = g_mutex_new ();

  ximagesink->sw_scaling_failed = FALSE;
  ximagesink->synchronous = FALSE;

  ximagesink->par = NULL;

  ximagesink->nav_lock = g_mutex_new ();
  ximagesink->pend_nav_events = NULL;

#if 0
  GST_FLAG_SET (ximagesink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (ximagesink, GST_ELEMENT_EVENT_AWARE);
#endif
}

static void
gst_ximagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_ximagesink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ximagesink_sink_template_factory));
}

static void
gst_ximagesink_class_init (GstXImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_VIDEO_SINK);

  gobject_class->finalize = gst_ximagesink_finalize;
  gobject_class->set_property = gst_ximagesink_set_property;
  gobject_class->get_property = gst_ximagesink_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SYNCHRONOUS,
      g_param_spec_boolean ("synchronous", "Synchronous", "When enabled, runs "
          "the X display in synchronous mode. (used only for debugging)", FALSE,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      g_param_spec_string ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", "1/1", G_PARAM_READWRITE));

  gstelement_class->change_state = gst_ximagesink_change_state;

  gstbasesink_class->get_caps = gst_ximagesink_getcaps;
  gstbasesink_class->set_caps = gst_ximagesink_setcaps;
  gstbasesink_class->buffer_alloc = gst_ximagesink_buffer_alloc;
  gstbasesink_class->get_times = gst_ximagesink_get_times;
  gstbasesink_class->preroll = gst_ximagesink_show_frame;
  gstbasesink_class->render = gst_ximagesink_show_frame;
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
gst_ximagesink_get_type (void)
{
  static GType ximagesink_type = 0;

  if (!ximagesink_type) {
    static const GTypeInfo ximagesink_info = {
      sizeof (GstXImageSinkClass),
      gst_ximagesink_base_init,
      NULL,
      (GClassInitFunc) gst_ximagesink_class_init,
      NULL,
      NULL,
      sizeof (GstXImageSink),
      0,
      (GInstanceInitFunc) gst_ximagesink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_ximagesink_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_ximagesink_navigation_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_ximagesink_xoverlay_init,
      NULL,
      NULL,
    };

    ximagesink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstXImageSink", &ximagesink_info, 0);

    g_type_add_interface_static (ximagesink_type, GST_TYPE_IMPLEMENTS_INTERFACE,
        &iface_info);
    g_type_add_interface_static (ximagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
    g_type_add_interface_static (ximagesink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
  }

  return ximagesink_type;
}
