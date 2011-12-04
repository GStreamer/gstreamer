/* GStreamer
 * Copyright (C) <2005> Julien Moutte <julien@moutte.net>
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

/**
 * SECTION:element-ximagesink
 *
 * XImageSink renders video frames to a drawable (XWindow) on a local or remote
 * display. This element can receive a Window ID from the application through
 * the XOverlay interface and will then render video frames in this drawable.
 * If no Window ID was provided by the application, the element will create its
 * own internal window and render into it.
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * As standard XImage rendering to a drawable is not scaled, XImageSink will use
 * reverse caps negotiation to try to get scaled video frames for the drawable.
 * This is accomplished by asking the peer pad if it accepts some different caps
 * which in most cases implies that there is a scaling element in the pipeline,
 * or that an element generating the video frames can generate them with a 
 * different geometry. This mechanism is handled during buffer allocations, for
 * each allocation request the video sink will check the drawable geometry, look
 * at the #GstXImageSink:force-aspect-ratio property, calculate the geometry of
 * desired video frames and then check that the peer pad accept those new caps.
 * If it does it will then allocate a buffer in video memory with this new
 * geometry and return it with the new caps.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Events</title>
 * <para>
 * XImageSink creates a thread to handle events coming from the drawable. There
 * are several kind of events that can be grouped in 2 big categories: input 
 * events and window state related events. Input events will be translated to
 * navigation events and pushed upstream for other elements to react on them.
 * This includes events such as pointer moves, key press/release, clicks etc...
 * Other events are used to handle the drawable appearance even when the data
 * is not flowing (GST_STATE_PAUSED). That means that even when the element is
 * paused, it will receive expose events from the drawable and draw the latest
 * frame with correct borders/aspect-ratio.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Pixel aspect ratio</title>
 * <para>
 * When changing state to GST_STATE_READY, XImageSink will open a connection to
 * the display specified in the #GstXImageSink:display property or the default
 * display if nothing specified. Once this connection is open it will inspect 
 * the display configuration including the physical display geometry and 
 * then calculate the pixel aspect ratio. When caps negotiation will occur, the
 * video sink will set the calculated pixel aspect ratio on the caps to make 
 * sure that incoming video frames will have the correct pixel aspect ratio for
 * this display. Sometimes the calculated pixel aspect ratio can be wrong, it is
 * then possible to enforce a specific pixel aspect ratio using the
 * #GstXImageSink:pixel-aspect-ratio property.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! queue ! ximagesink
 * ]| A pipeline to test reverse negotiation. When the test video signal appears
 * you can resize the window and see that scaled buffers of the desired size are
 * going to arrive with a short delay. This illustrates how buffers of desired
 * size are allocated along the way. If you take away the queue, scaling will
 * happen almost immediately.
 * |[
 * gst-launch -v videotestsrc ! navigationtest ! ffmpegcolorspace ! ximagesink
 * ]| A pipeline to test navigation events.
 * While moving the mouse pointer over the test signal you will see a black box
 * following the mouse pointer. If you press the mouse button somewhere on the 
 * video and release it somewhere else a green box will appear where you pressed
 * the button and a red one where you released it. (The navigationtest element
 * is part of gst-plugins-good.)
 * |[
 * gst-launch -v videotestsrc ! video/x-raw-rgb, pixel-aspect-ratio=(fraction)4/3 ! videoscale ! ximagesink
 * ]| This is faking a 4/3 pixel aspect ratio caps on video frames produced by
 * videotestsrc, in most cases the pixel aspect ratio of the display will be
 * 1/1. This means that videoscale will have to do the scaling to convert 
 * incoming frames to a size that will match the display pixel aspect ratio
 * (from 320x240 to 320x180 in this case). Note that you might have to escape 
 * some characters for your shell like '\(fraction\)'.
 * </refsect2>
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

#include "gst/glib-compat-private.h"

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

static void gst_ximagesink_reset (GstXImageSink * ximagesink);
static void gst_ximagesink_ximage_destroy (GstXImageSink * ximagesink,
    GstXImageBuffer * ximage);
static void gst_ximagesink_xwindow_update_geometry (GstXImageSink * ximagesink);
static void gst_ximagesink_expose (GstXOverlay * overlay);

static GstStaticPadTemplate gst_ximagesink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_SYNCHRONOUS,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_FORCE_ASPECT_RATIO,
  PROP_HANDLE_EVENTS,
  PROP_HANDLE_EXPOSE,
  PROP_WINDOW_WIDTH,
  PROP_WINDOW_HEIGHT
};

static GstVideoSinkClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* ximage buffers */

static GstBufferClass *ximage_buffer_parent_class = NULL;

#define GST_TYPE_XIMAGE_BUFFER (gst_ximage_buffer_get_type())

#define GST_IS_XIMAGE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_XIMAGE_BUFFER))
#define GST_XIMAGE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_XIMAGE_BUFFER, GstXImageBuffer))
#define GST_XIMAGE_BUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_XIMAGE_BUFFER, GstXImageBufferClass))

/* So some words about GstMiniObject, this is pretty messy...
   GstMiniObject does not use the standard finalizing of GObjects, you are 
   supposed to call gst_buffer_unref that's going to call gst_mini_objec_unref
   which will handle its own refcount system and call gst_mini_object_free.
   gst_mini_object_free will call the class finalize method which is not the 
   one from GObject, after calling this finalize method it will free the object
   instance for you if the refcount is still 0 so you should not chain up */
static void
gst_ximage_buffer_finalize (GstXImageBuffer * ximage)
{
  GstXImageSink *ximagesink = NULL;
  gboolean recycled = FALSE;
  gboolean running;

  g_return_if_fail (ximage != NULL);

  ximagesink = ximage->ximagesink;
  if (G_UNLIKELY (ximagesink == NULL)) {
    GST_WARNING_OBJECT (ximagesink, "no sink found");
    goto beach;
  }

  GST_OBJECT_LOCK (ximagesink);
  running = ximagesink->running;
  GST_OBJECT_UNLOCK (ximagesink);

  if (running == FALSE) {
    /* If the sink is shutting down, need to clear the image */
    GST_DEBUG_OBJECT (ximagesink,
        "destroy image %p because the sink is shutting down", ximage);
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  } else if ((ximage->width != GST_VIDEO_SINK_WIDTH (ximagesink)) ||
      (ximage->height != GST_VIDEO_SINK_HEIGHT (ximagesink))) {
    /* If our geometry changed we can't reuse that image. */
    GST_DEBUG_OBJECT (ximagesink,
        "destroy image %p as its size changed %dx%d vs current %dx%d",
        ximage, ximage->width, ximage->height,
        GST_VIDEO_SINK_WIDTH (ximagesink), GST_VIDEO_SINK_HEIGHT (ximagesink));
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  } else {
    /* In that case we can reuse the image and add it to our image pool. */
    GST_LOG_OBJECT (ximagesink, "recycling image %p in pool", ximage);
    /* need to increment the refcount again to recycle */
    gst_buffer_ref (GST_BUFFER_CAST (ximage));
    g_mutex_lock (ximagesink->pool_lock);
    ximagesink->buffer_pool = g_slist_prepend (ximagesink->buffer_pool, ximage);
    g_mutex_unlock (ximagesink->pool_lock);
    recycled = TRUE;
  }

  if (!recycled)
    GST_MINI_OBJECT_CLASS (ximage_buffer_parent_class)->finalize
        (GST_MINI_OBJECT (ximage));

beach:
  return;
}

static void
gst_ximage_buffer_free (GstXImageBuffer * ximage)
{
  /* make sure it is not recycled */
  ximage->width = -1;
  ximage->height = -1;
  gst_buffer_unref (GST_BUFFER_CAST (ximage));
}

static void
gst_ximage_buffer_init (GstXImageBuffer * ximage_buffer, gpointer g_class)
{
#ifdef HAVE_XSHM
  ximage_buffer->SHMInfo.shmaddr = ((void *) -1);
  ximage_buffer->SHMInfo.shmid = -1;
#endif
}

static void
gst_ximage_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  ximage_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_ximage_buffer_finalize;
}

static GType
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
      (GInstanceInitFunc) gst_ximage_buffer_init,
      NULL
    };
    _gst_ximage_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstXImageBuffer", &ximage_buffer_info, 0);
  }
  return _gst_ximage_buffer_type;
}

/* X11 stuff */

static gboolean error_caught = FALSE;

static int
gst_ximagesink_handle_xerror (Display * display, XErrorEvent * xevent)
{
  char error_msg[1024];

  XGetErrorText (display, xevent->error_code, error_msg, 1024);
  GST_DEBUG ("ximagesink triggered an XError. error: %s", error_msg);
  error_caught = TRUE;
  return 0;
}

#ifdef HAVE_XSHM                /* Check that XShm calls actually work */

static gboolean
gst_ximagesink_check_xshm_calls (GstXImageSink * ximagesink,
    GstXContext * xcontext)
{
  XImage *ximage;
  XShmSegmentInfo SHMInfo;
  size_t size;
  int (*handler) (Display *, XErrorEvent *);
  gboolean result = FALSE;
  gboolean did_attach = FALSE;

  g_return_val_if_fail (xcontext != NULL, FALSE);

  /* Sync to ensure any older errors are already processed */
  XSync (xcontext->disp, FALSE);

  /* Set defaults so we don't free these later unnecessarily */
  SHMInfo.shmaddr = ((void *) -1);
  SHMInfo.shmid = -1;

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_ximagesink_handle_xerror);

  /* Trying to create a 1x1 ximage */
  GST_DEBUG ("XShmCreateImage of 1x1");

  ximage = XShmCreateImage (xcontext->disp, xcontext->visual,
      xcontext->depth, ZPixmap, NULL, &SHMInfo, 1, 1);

  /* Might cause an error, sync to ensure it is noticed */
  XSync (xcontext->disp, FALSE);
  if (!ximage || error_caught) {
    GST_WARNING ("could not XShmCreateImage a 1x1 image");
    goto beach;
  }
  size = ximage->height * ximage->bytes_per_line;

  SHMInfo.shmid = shmget (IPC_PRIVATE, size, IPC_CREAT | 0777);
  if (SHMInfo.shmid == -1) {
    GST_WARNING ("could not get shared memory of %" G_GSIZE_FORMAT " bytes",
        size);
    goto beach;
  }

  SHMInfo.shmaddr = shmat (SHMInfo.shmid, NULL, 0);
  if (SHMInfo.shmaddr == ((void *) -1)) {
    GST_WARNING ("Failed to shmat: %s", g_strerror (errno));
    /* Clean up shm seg */
    shmctl (SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }

  ximage->data = SHMInfo.shmaddr;
  SHMInfo.readOnly = FALSE;

  if (XShmAttach (xcontext->disp, &SHMInfo) == 0) {
    GST_WARNING ("Failed to XShmAttach");
    /* Clean up shm seg */
    shmctl (SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }

  /* Sync to ensure we see any errors we caused */
  XSync (xcontext->disp, FALSE);

  /* Delete the shared memory segment as soon as everyone is attached. 
   * This way, it will be deleted as soon as we detach later, and not
   * leaked if we crash. */
  shmctl (SHMInfo.shmid, IPC_RMID, NULL);

  if (!error_caught) {
    did_attach = TRUE;
    /* store whether we succeeded in result */
    result = TRUE;
  }

beach:
  /* Sync to ensure we swallow any errors we caused and reset error_caught */
  XSync (xcontext->disp, FALSE);
  error_caught = FALSE;
  XSetErrorHandler (handler);

  if (did_attach) {
    XShmDetach (xcontext->disp, &SHMInfo);
    XSync (xcontext->disp, FALSE);
  }
  if (SHMInfo.shmaddr != ((void *) -1))
    shmdt (SHMInfo.shmaddr);
  if (ximage)
    XDestroyImage (ximage);
  return result;
}
#endif /* HAVE_XSHM */

/* This function handles GstXImageBuffer creation depending on XShm availability */
static GstXImageBuffer *
gst_ximagesink_ximage_new (GstXImageSink * ximagesink, GstCaps * caps)
{
  GstXImageBuffer *ximage = NULL;
  GstStructure *structure = NULL;
  gboolean succeeded = FALSE;
  int (*handler) (Display *, XErrorEvent *);

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);

  ximage = (GstXImageBuffer *) gst_mini_object_new (GST_TYPE_XIMAGE_BUFFER);

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &ximage->width) ||
      !gst_structure_get_int (structure, "height", &ximage->height)) {
    GST_WARNING ("failed getting geometry from caps %" GST_PTR_FORMAT, caps);
  }

  GST_DEBUG_OBJECT (ximagesink, "creating image %p (%dx%d)", ximage,
      ximage->width, ximage->height);

  g_mutex_lock (ximagesink->x_lock);

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_ximagesink_handle_xerror);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    ximage->ximage = XShmCreateImage (ximagesink->xcontext->disp,
        ximagesink->xcontext->visual,
        ximagesink->xcontext->depth,
        ZPixmap, NULL, &ximage->SHMInfo, ximage->width, ximage->height);
    if (!ximage->ximage || error_caught) {
      g_mutex_unlock (ximagesink->x_lock);

      /* Reset error flag */
      error_caught = FALSE;

      /* Push a warning */
      GST_ELEMENT_WARNING (ximagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              ximage->width, ximage->height),
          ("could not XShmCreateImage a %dx%d image",
              ximage->width, ximage->height));

      /* Retry without XShm */
      ximagesink->xcontext->use_xshm = FALSE;

      /* Hold X mutex again to try without XShm */
      g_mutex_lock (ximagesink->x_lock);
      goto no_xshm;
    }

    /* we have to use the returned bytes_per_line for our shm size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;
    GST_LOG_OBJECT (ximagesink,
        "XShm image size is %" G_GSIZE_FORMAT ", width %d, stride %d",
        ximage->size, ximage->width, ximage->ximage->bytes_per_line);

    ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size,
        IPC_CREAT | 0777);
    if (ximage->SHMInfo.shmid == -1) {
      g_mutex_unlock (ximagesink->x_lock);
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              ximage->width, ximage->height),
          ("could not get shared memory of %" G_GSIZE_FORMAT " bytes",
              ximage->size));
      goto beach;
    }

    ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, NULL, 0);
    if (ximage->SHMInfo.shmaddr == ((void *) -1)) {
      g_mutex_unlock (ximagesink->x_lock);
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              ximage->width, ximage->height),
          ("Failed to shmat: %s", g_strerror (errno)));
      /* Clean up the shared memory segment */
      shmctl (ximage->SHMInfo.shmid, IPC_RMID, NULL);
      goto beach;
    }

    ximage->ximage->data = ximage->SHMInfo.shmaddr;
    ximage->SHMInfo.readOnly = FALSE;

    if (XShmAttach (ximagesink->xcontext->disp, &ximage->SHMInfo) == 0) {
      /* Clean up shm seg */
      shmctl (ximage->SHMInfo.shmid, IPC_RMID, NULL);

      g_mutex_unlock (ximagesink->x_lock);
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              ximage->width, ximage->height), ("Failed to XShmAttach"));
      goto beach;
    }

    XSync (ximagesink->xcontext->disp, FALSE);

    /* Now that everyone has attached, we can delete the shared memory segment.
     * This way, it will be deleted as soon as we detach later, and not
     * leaked if we crash. */
    shmctl (ximage->SHMInfo.shmid, IPC_RMID, NULL);

  } else
  no_xshm:
#endif /* HAVE_XSHM */
  {
    guint allocsize;

    ximage->ximage = XCreateImage (ximagesink->xcontext->disp,
        ximagesink->xcontext->visual,
        ximagesink->xcontext->depth,
        ZPixmap, 0, NULL,
        ximage->width, ximage->height, ximagesink->xcontext->bpp, 0);
    if (!ximage->ximage || error_caught) {
      g_mutex_unlock (ximagesink->x_lock);
      /* Reset error handler */
      error_caught = FALSE;
      XSetErrorHandler (handler);
      /* Push an error */
      GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              ximage->width, ximage->height),
          ("could not XCreateImage a %dx%d image",
              ximage->width, ximage->height));
      goto beach;
    }

    /* upstream will assume that rowstrides are multiples of 4, but this
     * doesn't always seem to be the case with XCreateImage() */
    if ((ximage->ximage->bytes_per_line % 4) != 0) {
      GST_WARNING_OBJECT (ximagesink, "returned stride not a multiple of 4 as "
          "usually assumed");
    }

    /* we have to use the returned bytes_per_line for our image size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;

    /* alloc a bit more for unexpected strides to avoid crashes upstream.
     * FIXME: if we get an unrounded stride, the image will be displayed
     * distorted, since all upstream elements assume a rounded stride */
    allocsize =
        GST_ROUND_UP_4 (ximage->ximage->bytes_per_line) *
        ximage->ximage->height;
    ximage->ximage->data = g_malloc (allocsize);
    GST_LOG_OBJECT (ximagesink,
        "non-XShm image size is %" G_GSIZE_FORMAT " (alloced: %u), width %d, "
        "stride %d", ximage->size, allocsize, ximage->width,
        ximage->ximage->bytes_per_line);

    XSync (ximagesink->xcontext->disp, FALSE);
  }

  /* Reset error handler */
  error_caught = FALSE;
  XSetErrorHandler (handler);

  succeeded = TRUE;

  GST_BUFFER_DATA (ximage) = (guchar *) ximage->ximage->data;
  GST_BUFFER_SIZE (ximage) = ximage->size;

  /* Keep a ref to our sink */
  ximage->ximagesink = gst_object_ref (ximagesink);

  g_mutex_unlock (ximagesink->x_lock);
beach:
  if (!succeeded) {
    gst_ximage_buffer_free (ximage);
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

  /* Hold the object lock to ensure the XContext doesn't disappear */
  GST_OBJECT_LOCK (ximagesink);

  /* If the destroyed image is the current one we destroy our reference too */
  if (ximagesink->cur_image == ximage) {
    ximagesink->cur_image = NULL;
  }

  /* We might have some buffers destroyed after changing state to NULL */
  if (!ximagesink->xcontext) {
    GST_DEBUG_OBJECT (ximagesink, "Destroying XImage after XContext");
#ifdef HAVE_XSHM
    if (ximage->SHMInfo.shmaddr != ((void *) -1)) {
      shmdt (ximage->SHMInfo.shmaddr);
    }
#endif
    goto beach;
  }

  g_mutex_lock (ximagesink->x_lock);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    if (ximage->SHMInfo.shmaddr != ((void *) -1)) {
      XShmDetach (ximagesink->xcontext->disp, &ximage->SHMInfo);
      XSync (ximagesink->xcontext->disp, 0);
      shmdt (ximage->SHMInfo.shmaddr);
    }
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

beach:
  GST_OBJECT_UNLOCK (ximagesink);

  if (ximage->ximagesink) {
    /* Release the ref to our sink */
    ximage->ximagesink = NULL;
    gst_object_unref (ximagesink);
  }

  return;
}

/* We are called with the x_lock taken */
static void
gst_ximagesink_xwindow_draw_borders (GstXImageSink * ximagesink,
    GstXWindow * xwindow, GstVideoRectangle rect)
{
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  g_return_if_fail (xwindow != NULL);

  XSetForeground (ximagesink->xcontext->disp, xwindow->gc,
      ximagesink->xcontext->black);

  /* Left border */
  if (rect.x > 0) {
    XFillRectangle (ximagesink->xcontext->disp, xwindow->win, xwindow->gc,
        0, 0, rect.x, xwindow->height);
  }

  /* Right border */
  if ((rect.x + rect.w) < xwindow->width) {
    XFillRectangle (ximagesink->xcontext->disp, xwindow->win, xwindow->gc,
        rect.x + rect.w, 0, xwindow->width, xwindow->height);
  }

  /* Top border */
  if (rect.y > 0) {
    XFillRectangle (ximagesink->xcontext->disp, xwindow->win, xwindow->gc,
        0, 0, xwindow->width, rect.y);
  }

  /* Bottom border */
  if ((rect.y + rect.h) < xwindow->height) {
    XFillRectangle (ximagesink->xcontext->disp, xwindow->win, xwindow->gc,
        0, rect.y + rect.h, xwindow->width, xwindow->height);
  }
}

/* This function puts a GstXImageBuffer on a GstXImageSink's window */
static gboolean
gst_ximagesink_ximage_put (GstXImageSink * ximagesink, GstXImageBuffer * ximage)
{
  GstVideoRectangle src, dst, result;
  gboolean draw_border = FALSE;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), FALSE);

  /* We take the flow_lock. If expose is in there we don't want to run
     concurrently from the data flow thread */
  g_mutex_lock (ximagesink->flow_lock);

  if (G_UNLIKELY (ximagesink->xwindow == NULL)) {
    g_mutex_unlock (ximagesink->flow_lock);
    return FALSE;
  }

  /* Draw borders when displaying the first frame. After this
     draw borders only on expose event or caps change (ximagesink->draw_border = TRUE). */
  if (!ximagesink->cur_image || ximagesink->draw_border) {
    draw_border = TRUE;
  }

  /* Store a reference to the last image we put, lose the previous one */
  if (ximage && ximagesink->cur_image != ximage) {
    if (ximagesink->cur_image) {
      GST_LOG_OBJECT (ximagesink, "unreffing %p", ximagesink->cur_image);
      gst_buffer_unref (GST_BUFFER_CAST (ximagesink->cur_image));
    }
    GST_LOG_OBJECT (ximagesink, "reffing %p as our current image", ximage);
    ximagesink->cur_image =
        GST_XIMAGE_BUFFER (gst_buffer_ref (GST_BUFFER_CAST (ximage)));
  }

  /* Expose sends a NULL image, we take the latest frame */
  if (!ximage) {
    draw_border = TRUE;
    if (ximagesink->cur_image) {
      ximage = ximagesink->cur_image;
    } else {
      g_mutex_unlock (ximagesink->flow_lock);
      return TRUE;
    }
  }

  src.w = ximage->width;
  src.h = ximage->height;
  dst.w = ximagesink->xwindow->width;
  dst.h = ximagesink->xwindow->height;

  gst_video_sink_center_rect (src, dst, &result, FALSE);

  g_mutex_lock (ximagesink->x_lock);

  if (draw_border) {
    gst_ximagesink_xwindow_draw_borders (ximagesink, ximagesink->xwindow,
        result);
    ximagesink->draw_border = FALSE;
  }
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    GST_LOG_OBJECT (ximagesink,
        "XShmPutImage on %p, src: %d, %d - dest: %d, %d, dim: %dx%d, win %dx%d",
        ximage, 0, 0, result.x, result.y, result.w, result.h,
        ximagesink->xwindow->width, ximagesink->xwindow->height);
    XShmPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win,
        ximagesink->xwindow->gc, ximage->ximage, 0, 0, result.x, result.y,
        result.w, result.h, FALSE);
  } else
#endif /* HAVE_XSHM */
  {
    GST_LOG_OBJECT (ximagesink,
        "XPutImage on %p, src: %d, %d - dest: %d, %d, dim: %dx%d, win %dx%d",
        ximage, 0, 0, result.x, result.y, result.w, result.h,
        ximagesink->xwindow->width, ximagesink->xwindow->height);
    XPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win,
        ximagesink->xwindow->gc, ximage->ximage, 0, 0, result.x, result.y,
        result.w, result.h);
  }

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  g_mutex_unlock (ximagesink->flow_lock);

  return TRUE;
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

static void
gst_ximagesink_xwindow_set_title (GstXImageSink * ximagesink,
    GstXWindow * xwindow, const gchar * media_title)
{
  if (media_title) {
    g_free (ximagesink->media_title);
    ximagesink->media_title = g_strdup (media_title);
  }
  if (xwindow) {
    /* we have a window */
    if (xwindow->internal) {
      XTextProperty xproperty;
      const gchar *app_name;
      const gchar *title = NULL;
      gchar *title_mem = NULL;

      /* set application name as a title */
      app_name = g_get_application_name ();

      if (app_name && ximagesink->media_title) {
        title = title_mem = g_strconcat (ximagesink->media_title, " : ",
            app_name, NULL);
      } else if (app_name) {
        title = app_name;
      } else if (ximagesink->media_title) {
        title = ximagesink->media_title;
      }

      if (title) {
        if ((XStringListToTextProperty (((char **) &title), 1,
                    &xproperty)) != 0) {
          XSetWMName (ximagesink->xcontext->disp, xwindow->win, &xproperty);
          XFree (xproperty.value);
        }

        g_free (title_mem);
      }
    }
  }
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

  /* We have to do that to prevent X from redrawing the background on 
     ConfigureNotify. This takes away flickering of video when resizing. */
  XSetWindowBackgroundPixmap (ximagesink->xcontext->disp, xwindow->win, None);

  /* set application name as a title */
  gst_ximagesink_xwindow_set_title (ximagesink, xwindow, NULL);

  if (ximagesink->handle_events) {
    Atom wm_delete;

    XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
        StructureNotifyMask | PointerMotionMask | KeyPressMask |
        KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);

    /* Tell the window manager we'd like delete client messages instead of
     * being killed */
    wm_delete = XInternAtom (ximagesink->xcontext->disp,
        "WM_DELETE_WINDOW", False);
    (void) XSetWMProtocols (ximagesink->xcontext->disp, xwindow->win,
        &wm_delete, 1);
  }

  xwindow->gc = XCreateGC (ximagesink->xcontext->disp, xwindow->win,
      0, &values);

  XMapRaised (ximagesink->xcontext->disp, xwindow->win);

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (ximagesink->x_lock);

  gst_ximagesink_xwindow_decorate (ximagesink, xwindow);

  gst_x_overlay_got_window_handle (GST_X_OVERLAY (ximagesink), xwindow->win);

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

static void
gst_ximagesink_xwindow_update_geometry (GstXImageSink * ximagesink)
{
  XWindowAttributes attr;

  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* Update the window geometry */
  g_mutex_lock (ximagesink->x_lock);
  if (G_UNLIKELY (ximagesink->xwindow == NULL)) {
    g_mutex_unlock (ximagesink->x_lock);
    return;
  }

  XGetWindowAttributes (ximagesink->xcontext->disp,
      ximagesink->xwindow->win, &attr);

  ximagesink->xwindow->width = attr.width;
  ximagesink->xwindow->height = attr.height;

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

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation.*/
static void
gst_ximagesink_handle_xevents (GstXImageSink * ximagesink)
{
  XEvent e;
  guint pointer_x = 0, pointer_y = 0;
  gboolean pointer_moved = FALSE;
  gboolean exposed = FALSE, configured = FALSE;

  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  /* Then we get all pointer motion events, only the last position is
     interesting. */
  g_mutex_lock (ximagesink->flow_lock);
  g_mutex_lock (ximagesink->x_lock);
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
          ximagesink->xwindow->win, PointerMotionMask, &e)) {
    g_mutex_unlock (ximagesink->x_lock);
    g_mutex_unlock (ximagesink->flow_lock);

    switch (e.type) {
      case MotionNotify:
        pointer_x = e.xmotion.x;
        pointer_y = e.xmotion.y;
        pointer_moved = TRUE;
        break;
      default:
        break;
    }
    g_mutex_lock (ximagesink->flow_lock);
    g_mutex_lock (ximagesink->x_lock);
  }

  if (pointer_moved) {
    g_mutex_unlock (ximagesink->x_lock);
    g_mutex_unlock (ximagesink->flow_lock);

    GST_DEBUG ("ximagesink pointer moved over window at %d,%d",
        pointer_x, pointer_y);
    gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
        "mouse-move", 0, pointer_x, pointer_y);

    g_mutex_lock (ximagesink->flow_lock);
    g_mutex_lock (ximagesink->x_lock);
  }

  /* We get all remaining events on our window to throw them upstream */
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
          ximagesink->xwindow->win,
          KeyPressMask | KeyReleaseMask |
          ButtonPressMask | ButtonReleaseMask, &e)) {
    KeySym keysym;

    /* We lock only for the X function call */
    g_mutex_unlock (ximagesink->x_lock);
    g_mutex_unlock (ximagesink->flow_lock);

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
        g_mutex_lock (ximagesink->x_lock);
        keysym = XKeycodeToKeysym (ximagesink->xcontext->disp,
            e.xkey.keycode, 0);
        g_mutex_unlock (ximagesink->x_lock);
        if (keysym != NoSymbol) {
          char *key_str = NULL;

          g_mutex_lock (ximagesink->x_lock);
          key_str = XKeysymToString (keysym);
          g_mutex_unlock (ximagesink->x_lock);
          gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
              e.type == KeyPress ? "key-press" : "key-release", key_str);

        } else {
          gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
              e.type == KeyPress ? "key-press" : "key-release", "unknown");
        }
        break;
      default:
        GST_DEBUG_OBJECT (ximagesink, "ximagesink unhandled X event (%d)",
            e.type);
    }
    g_mutex_lock (ximagesink->flow_lock);
    g_mutex_lock (ximagesink->x_lock);
  }

  while (XCheckWindowEvent (ximagesink->xcontext->disp,
          ximagesink->xwindow->win, ExposureMask | StructureNotifyMask, &e)) {
    switch (e.type) {
      case Expose:
        exposed = TRUE;
        break;
      case ConfigureNotify:
        g_mutex_unlock (ximagesink->x_lock);
        gst_ximagesink_xwindow_update_geometry (ximagesink);
        g_mutex_lock (ximagesink->x_lock);
        configured = TRUE;
        break;
      default:
        break;
    }
  }

  if (ximagesink->handle_expose && (exposed || configured)) {
    g_mutex_unlock (ximagesink->x_lock);
    g_mutex_unlock (ximagesink->flow_lock);

    gst_ximagesink_expose (GST_X_OVERLAY (ximagesink));

    g_mutex_lock (ximagesink->flow_lock);
    g_mutex_lock (ximagesink->x_lock);
  }

  /* Handle Display events */
  while (XPending (ximagesink->xcontext->disp)) {
    XNextEvent (ximagesink->xcontext->disp, &e);

    switch (e.type) {
      case ClientMessage:{
        Atom wm_delete;

        wm_delete = XInternAtom (ximagesink->xcontext->disp,
            "WM_DELETE_WINDOW", False);
        if (wm_delete == (Atom) e.xclient.data.l[0]) {
          /* Handle window deletion by posting an error on the bus */
          GST_ELEMENT_ERROR (ximagesink, RESOURCE, NOT_FOUND,
              ("Output window was closed"), (NULL));

          g_mutex_unlock (ximagesink->x_lock);
          gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
          ximagesink->xwindow = NULL;
          g_mutex_lock (ximagesink->x_lock);
        }
        break;
      }
      default:
        break;
    }
  }

  g_mutex_unlock (ximagesink->x_lock);
  g_mutex_unlock (ximagesink->flow_lock);
}

static gpointer
gst_ximagesink_event_thread (GstXImageSink * ximagesink)
{
  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);

  GST_OBJECT_LOCK (ximagesink);
  while (ximagesink->running) {
    GST_OBJECT_UNLOCK (ximagesink);

    if (ximagesink->xwindow) {
      gst_ximagesink_handle_xevents (ximagesink);
    }
    /* FIXME: do we want to align this with the framerate or anything else? */
    g_usleep (G_USEC_PER_SEC / 20);

    GST_OBJECT_LOCK (ximagesink);
  }
  GST_OBJECT_UNLOCK (ximagesink);

  return NULL;
}

static void
gst_ximagesink_manage_event_thread (GstXImageSink * ximagesink)
{
  GThread *thread = NULL;

  /* don't start the thread too early */
  if (ximagesink->xcontext == NULL) {
    return;
  }

  GST_OBJECT_LOCK (ximagesink);
  if (ximagesink->handle_expose || ximagesink->handle_events) {
    if (!ximagesink->event_thread) {
      /* Setup our event listening thread */
      GST_DEBUG_OBJECT (ximagesink, "run xevent thread, expose %d, events %d",
          ximagesink->handle_expose, ximagesink->handle_events);
      ximagesink->running = TRUE;
#if !GLIB_CHECK_VERSION (2, 31, 0)
      ximagesink->event_thread = g_thread_create (
          (GThreadFunc) gst_ximagesink_event_thread, ximagesink, TRUE, NULL);
#else
      ximagesink->event_thread = g_thread_try_new ("ximagesink-events",
          (GThreadFunc) gst_ximagesink_event_thread, ximagesink, NULL);
#endif
    }
  } else {
    if (ximagesink->event_thread) {
      GST_DEBUG_OBJECT (ximagesink, "stop xevent thread, expose %d, events %d",
          ximagesink->handle_expose, ximagesink->handle_events);
      ximagesink->running = FALSE;
      /* grab thread and mark it as NULL */
      thread = ximagesink->event_thread;
      ximagesink->event_thread = NULL;
    }
  }
  GST_OBJECT_UNLOCK (ximagesink);

  /* Wait for our event thread to finish */
  if (thread)
    g_thread_join (thread);

}


/* This function calculates the pixel aspect ratio based on the properties
 * in the xcontext structure and stores it there. */
static void
gst_ximagesink_calculate_pixel_aspect_ratio (GstXContext * xcontext)
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
    GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
        ("Could not initialise X output"), ("Could not open display"));
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
    g_free (xcontext->par);
    g_free (xcontext);
    GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
        ("Could not get supported pixmap formats"), (NULL));
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

  /* Search for XShm extension support */
#ifdef HAVE_XSHM
  if (XShmQueryExtension (xcontext->disp) &&
      gst_ximagesink_check_xshm_calls (ximagesink, xcontext)) {
    xcontext->use_xshm = TRUE;
    GST_DEBUG ("ximagesink is using XShm extension");
  } else
#endif
  {
    xcontext->use_xshm = FALSE;
    GST_DEBUG ("ximagesink is not using XShm extension");
  }

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
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
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
  GstXContext *xcontext;

  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));

  GST_OBJECT_LOCK (ximagesink);
  if (ximagesink->xcontext == NULL) {
    GST_OBJECT_UNLOCK (ximagesink);
    return;
  }

  /* Take the xcontext reference and NULL it while we
   * clean it up, so that any buffer-alloced buffers 
   * arriving after this will be freed correctly */
  xcontext = ximagesink->xcontext;
  ximagesink->xcontext = NULL;

  GST_OBJECT_UNLOCK (ximagesink);

  gst_caps_unref (xcontext->caps);
  g_free (xcontext->par);
  g_free (ximagesink->par);
  ximagesink->par = NULL;

  if (xcontext->last_caps)
    gst_caps_replace (&xcontext->last_caps, NULL);

  g_mutex_lock (ximagesink->x_lock);

  XCloseDisplay (xcontext->disp);

  g_mutex_unlock (ximagesink->x_lock);

  g_free (xcontext);
}

static void
gst_ximagesink_bufferpool_clear (GstXImageSink * ximagesink)
{

  g_mutex_lock (ximagesink->pool_lock);

  while (ximagesink->buffer_pool) {
    GstXImageBuffer *ximage = ximagesink->buffer_pool->data;

    ximagesink->buffer_pool = g_slist_delete_link (ximagesink->buffer_pool,
        ximagesink->buffer_pool);
    gst_ximage_buffer_free (ximage);
  }

  g_mutex_unlock (ximagesink->pool_lock);
}

/* Element stuff */

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
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK
          (ximagesink)->sinkpad));
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
  gint new_width, new_height;
  const GValue *fps;

  ximagesink = GST_XIMAGESINK (bsink);

  if (!ximagesink->xcontext)
    return FALSE;

  GST_DEBUG_OBJECT (ximagesink,
      "sinkconnect possible caps %" GST_PTR_FORMAT " with given caps %"
      GST_PTR_FORMAT, ximagesink->xcontext->caps, caps);

  /* We intersect those caps with our template to make sure they are correct */
  if (!gst_caps_can_intersect (ximagesink->xcontext->caps, caps))
    goto incompatible_caps;

  structure = gst_caps_get_structure (caps, 0);

  ret &= gst_structure_get_int (structure, "width", &new_width);
  ret &= gst_structure_get_int (structure, "height", &new_height);
  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);
  if (!ret)
    return FALSE;

  /* if the caps contain pixel-aspect-ratio, they have to match ours,
   * otherwise linking should fail */
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par) {
    if (ximagesink->par) {
      if (gst_value_compare (par, ximagesink->par) != GST_VALUE_EQUAL) {
        goto wrong_aspect;
      }
    } else if (ximagesink->xcontext->par) {
      if (gst_value_compare (par, ximagesink->xcontext->par) != GST_VALUE_EQUAL) {
        goto wrong_aspect;
      }
    }
  }

  GST_VIDEO_SINK_WIDTH (ximagesink) = new_width;
  GST_VIDEO_SINK_HEIGHT (ximagesink) = new_height;
  ximagesink->fps_n = gst_value_get_fraction_numerator (fps);
  ximagesink->fps_d = gst_value_get_fraction_denominator (fps);

  /* Notify application to set xwindow id now */
  g_mutex_lock (ximagesink->flow_lock);
  if (!ximagesink->xwindow) {
    g_mutex_unlock (ximagesink->flow_lock);
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (ximagesink));
  } else {
    g_mutex_unlock (ximagesink->flow_lock);
  }

  /* Creating our window and our image */
  if (GST_VIDEO_SINK_WIDTH (ximagesink) <= 0 ||
      GST_VIDEO_SINK_HEIGHT (ximagesink) <= 0) {
    GST_ELEMENT_ERROR (ximagesink, CORE, NEGOTIATION, (NULL),
        ("Invalid image size."));
    return FALSE;
  }

  g_mutex_lock (ximagesink->flow_lock);
  if (!ximagesink->xwindow) {
    ximagesink->xwindow = gst_ximagesink_xwindow_new (ximagesink,
        GST_VIDEO_SINK_WIDTH (ximagesink), GST_VIDEO_SINK_HEIGHT (ximagesink));
  }
  /* Remember to draw borders for next frame */
  ximagesink->draw_border = TRUE;
  g_mutex_unlock (ximagesink->flow_lock);

  /* If our ximage has changed we destroy it, next chain iteration will create
     a new one */
  if ((ximagesink->ximage) &&
      ((GST_VIDEO_SINK_WIDTH (ximagesink) != ximagesink->ximage->width) ||
          (GST_VIDEO_SINK_HEIGHT (ximagesink) != ximagesink->ximage->height))) {
    GST_DEBUG_OBJECT (ximagesink, "our image is not usable anymore, unref %p",
        ximagesink->ximage);
    gst_buffer_unref (GST_BUFFER_CAST (ximagesink->ximage));
    ximagesink->ximage = NULL;
  }

  return TRUE;

  /* ERRORS */
incompatible_caps:
  {
    GST_ERROR_OBJECT (ximagesink, "caps incompatible");
    return FALSE;
  }
wrong_aspect:
  {
    GST_INFO_OBJECT (ximagesink, "pixel aspect ratio does not match");
    return FALSE;
  }
}

static GstStateChangeReturn
gst_ximagesink_change_state (GstElement * element, GstStateChange transition)
{
  GstXImageSink *ximagesink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstXContext *xcontext = NULL;

  ximagesink = GST_XIMAGESINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:

      /* Initializing the XContext */
      if (ximagesink->xcontext == NULL) {
        xcontext = gst_ximagesink_xcontext_get (ximagesink);
        if (xcontext == NULL) {
          ret = GST_STATE_CHANGE_FAILURE;
          goto beach;
        }
        GST_OBJECT_LOCK (ximagesink);
        if (xcontext)
          ximagesink->xcontext = xcontext;
        GST_OBJECT_UNLOCK (ximagesink);
      }

      /* call XSynchronize with the current value of synchronous */
      GST_DEBUG_OBJECT (ximagesink, "XSynchronize called with %s",
          ximagesink->synchronous ? "TRUE" : "FALSE");
      g_mutex_lock (ximagesink->x_lock);
      XSynchronize (ximagesink->xcontext->disp, ximagesink->synchronous);
      g_mutex_unlock (ximagesink->x_lock);
      gst_ximagesink_manage_event_thread (ximagesink);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (ximagesink->flow_lock);
      if (ximagesink->xwindow)
        gst_ximagesink_xwindow_clear (ximagesink, ximagesink->xwindow);
      g_mutex_unlock (ximagesink->flow_lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      ximagesink->fps_n = 0;
      ximagesink->fps_d = 1;
      GST_VIDEO_SINK_WIDTH (ximagesink) = 0;
      GST_VIDEO_SINK_HEIGHT (ximagesink) = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ximagesink_reset (ximagesink);
      break;
    default:
      break;
  }

beach:
  return ret;
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
      if (ximagesink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, ximagesink->fps_d,
            ximagesink->fps_n);
      }
    }
  }
}

static GstFlowReturn
gst_ximagesink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstXImageSink *ximagesink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  ximagesink = GST_XIMAGESINK (vsink);

  /* This shouldn't really happen because state changes will fail
   * if the xcontext can't be allocated */
  if (!ximagesink->xcontext)
    return GST_FLOW_ERROR;

  /* If this buffer has been allocated using our buffer management we simply
     put the ximage which is in the PRIVATE pointer */
  if (GST_IS_XIMAGE_BUFFER (buf)) {
    GST_LOG_OBJECT (ximagesink, "buffer from our pool, writing directly");
    if (!gst_ximagesink_ximage_put (ximagesink, GST_XIMAGE_BUFFER (buf)))
      goto no_window;
  } else {
    /* Else we have to copy the data into our private image, */
    /* if we have one... */
    GST_LOG_OBJECT (ximagesink, "normal buffer, copying from it");
    if (!ximagesink->ximage) {
      GST_DEBUG_OBJECT (ximagesink, "creating our ximage");
      ximagesink->ximage = gst_ximagesink_ximage_new (ximagesink,
          GST_BUFFER_CAPS (buf));
      if (!ximagesink->ximage)
        /* The create method should have posted an informative error */
        goto no_ximage;

      if (ximagesink->ximage->size < GST_BUFFER_SIZE (buf)) {
        GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
            ("Failed to create output image buffer of %dx%d pixels",
                ximagesink->ximage->width, ximagesink->ximage->height),
            ("XServer allocated buffer size did not match input buffer"));

        gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
        ximagesink->ximage = NULL;
        goto no_ximage;
      }
    }
    memcpy (GST_BUFFER_DATA (ximagesink->ximage), GST_BUFFER_DATA (buf),
        MIN (GST_BUFFER_SIZE (buf), ximagesink->ximage->size));
    if (!gst_ximagesink_ximage_put (ximagesink, ximagesink->ximage))
      goto no_window;
  }

  return GST_FLOW_OK;

  /* ERRORS */
no_ximage:
  {
    /* No image available. That's very bad ! */
    GST_WARNING_OBJECT (ximagesink, "could not create image");
    return GST_FLOW_ERROR;
  }
no_window:
  {
    /* No Window available to put our image into */
    GST_WARNING_OBJECT (ximagesink, "could not output image - no window");
    return GST_FLOW_ERROR;
  }
}


static gboolean
gst_ximagesink_event (GstBaseSink * sink, GstEvent * event)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *l;
      gchar *title = NULL;

      gst_event_parse_tag (event, &l);
      gst_tag_list_get_string (l, GST_TAG_TITLE, &title);

      if (title) {
        GST_DEBUG_OBJECT (ximagesink, "got tags, title='%s'", title);
        gst_ximagesink_xwindow_set_title (ximagesink, ximagesink->xwindow,
            title);

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
gst_ximagesink_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstXImageSink *ximagesink;
  GstXImageBuffer *ximage = NULL;
  GstStructure *structure = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *alloc_caps;
  gboolean alloc_unref = FALSE;
  gint width, height;
  GstVideoRectangle dst, src, result;
  gboolean caps_accepted = FALSE;

  ximagesink = GST_XIMAGESINK (bsink);

  if (G_UNLIKELY (!caps)) {
    GST_WARNING_OBJECT (ximagesink, "have no caps, doing fallback allocation");
    *buf = NULL;
    ret = GST_FLOW_OK;
    goto beach;
  }

  /* This shouldn't really happen because state changes will fail
   * if the xcontext can't be allocated */
  if (!ximagesink->xcontext)
    return GST_FLOW_ERROR;

  GST_LOG_OBJECT (ximagesink,
      "a buffer of %d bytes was requested with caps %" GST_PTR_FORMAT
      " and offset %" G_GUINT64_FORMAT, size, caps, offset);

  /* assume we're going to alloc what was requested, keep track of
   * whether we need to unref or not. When we suggest a new format 
   * upstream we will create a new caps that we need to unref. */
  alloc_caps = caps;
  alloc_unref = FALSE;

  /* get struct to see what is requested */
  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    GST_WARNING_OBJECT (ximagesink, "invalid caps for buffer allocation %"
        GST_PTR_FORMAT, caps);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  src.w = width;
  src.h = height;

  /* We take the flow_lock because the window might go away */
  g_mutex_lock (ximagesink->flow_lock);
  if (!ximagesink->xwindow) {
    g_mutex_unlock (ximagesink->flow_lock);
    goto alloc;
  }

  /* What is our geometry */
  dst.w = ximagesink->xwindow->width;
  dst.h = ximagesink->xwindow->height;

  g_mutex_unlock (ximagesink->flow_lock);

  if (ximagesink->keep_aspect) {
    GST_LOG_OBJECT (ximagesink, "enforcing aspect ratio in reverse caps "
        "negotiation");
    gst_video_sink_center_rect (src, dst, &result, TRUE);
  } else {
    GST_LOG_OBJECT (ximagesink, "trying to resize to window geometry "
        "ignoring aspect ratio");
    result.x = result.y = 0;
    result.w = dst.w;
    result.h = dst.h;
  }

  /* We would like another geometry */
  if (width != result.w || height != result.h) {
    int nom, den;
    GstCaps *desired_caps;
    GstStructure *desired_struct;

    /* make a copy of the incomming caps to create the new
     * suggestion. We can't use make_writable because we might
     * then destroy the original caps which we still need when the
     * peer does not accept the suggestion. */
    desired_caps = gst_caps_copy (caps);
    desired_struct = gst_caps_get_structure (desired_caps, 0);

    GST_DEBUG ("we would love to receive a %dx%d video", result.w, result.h);
    gst_structure_set (desired_struct, "width", G_TYPE_INT, result.w, NULL);
    gst_structure_set (desired_struct, "height", G_TYPE_INT, result.h, NULL);

    /* PAR property overrides the X calculated one */
    if (ximagesink->par) {
      nom = gst_value_get_fraction_numerator (ximagesink->par);
      den = gst_value_get_fraction_denominator (ximagesink->par);
      gst_structure_set (desired_struct, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, nom, den, NULL);
    } else if (ximagesink->xcontext->par) {
      nom = gst_value_get_fraction_numerator (ximagesink->xcontext->par);
      den = gst_value_get_fraction_denominator (ximagesink->xcontext->par);
      gst_structure_set (desired_struct, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, nom, den, NULL);
    }


    /* see if peer accepts our new suggestion, if there is no peer, this 
     * function returns true. */
    if (!ximagesink->xcontext->last_caps ||
        !gst_caps_is_equal (desired_caps, ximagesink->xcontext->last_caps)) {
      caps_accepted =
          gst_pad_peer_accept_caps (GST_VIDEO_SINK_PAD (ximagesink),
          desired_caps);

      /* Suggestion failed, prevent future attempts for the same caps
       * to fail as well. */
      if (!caps_accepted)
        gst_caps_replace (&ximagesink->xcontext->last_caps, desired_caps);
    }

    if (caps_accepted) {
      /* we will not alloc a buffer of the new suggested caps. Make sure
       * we also unref this new caps after we set it on the buffer. */
      alloc_caps = desired_caps;
      alloc_unref = TRUE;
      width = result.w;
      height = result.h;
      GST_DEBUG ("peer pad accepts our desired caps %" GST_PTR_FORMAT,
          desired_caps);
    } else {
      GST_DEBUG ("peer pad does not accept our desired caps %" GST_PTR_FORMAT,
          desired_caps);
      /* we alloc a buffer with the original incomming caps already in the
       * width and height variables */
      gst_caps_unref (desired_caps);
    }
  }

alloc:
  /* Inspect our buffer pool */
  g_mutex_lock (ximagesink->pool_lock);
  while (ximagesink->buffer_pool) {
    ximage = (GstXImageBuffer *) ximagesink->buffer_pool->data;

    if (ximage) {
      /* Removing from the pool */
      ximagesink->buffer_pool = g_slist_delete_link (ximagesink->buffer_pool,
          ximagesink->buffer_pool);

      /* If the ximage is invalid for our need, destroy */
      if ((ximage->width != width) || (ximage->height != height)) {
        gst_ximage_buffer_free (ximage);
        ximage = NULL;
      } else {
        /* We found a suitable ximage */
        break;
      }
    }
  }
  g_mutex_unlock (ximagesink->pool_lock);

  /* We haven't found anything, creating a new one */
  if (!ximage) {
    ximage = gst_ximagesink_ximage_new (ximagesink, alloc_caps);
  }
  /* Now we should have a ximage, set appropriate caps on it */
  if (ximage) {
    /* Make sure the buffer is cleared of any previously used flags */
    GST_MINI_OBJECT_CAST (ximage)->flags = 0;
    gst_buffer_set_caps (GST_BUFFER_CAST (ximage), alloc_caps);
  }

  /* could be our new reffed suggestion or the original unreffed caps */
  if (alloc_unref)
    gst_caps_unref (alloc_caps);

  *buf = GST_BUFFER_CAST (ximage);

beach:
  return ret;
}

/* Interfaces stuff */

static gboolean
gst_ximagesink_interface_supported (GstImplementsInterface * iface, GType type)
{
  if (type == GST_TYPE_NAVIGATION || type == GST_TYPE_X_OVERLAY)
    return TRUE;
  else
    return FALSE;
}

static void
gst_ximagesink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_ximagesink_interface_supported;
}

static void
gst_ximagesink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (navigation);
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
  g_mutex_lock (ximagesink->flow_lock);

  if (!ximagesink->xwindow) {
    g_mutex_unlock (ximagesink->flow_lock);
    return;
  }

  x_offset = ximagesink->xwindow->width - GST_VIDEO_SINK_WIDTH (ximagesink);
  y_offset = ximagesink->xwindow->height - GST_VIDEO_SINK_HEIGHT (ximagesink);

  g_mutex_unlock (ximagesink->flow_lock);

  if (x_offset > 0 && gst_structure_get_double (structure, "pointer_x", &x)) {
    x -= x_offset / 2;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (y_offset > 0 && gst_structure_get_double (structure, "pointer_y", &y)) {
    y -= y_offset / 2;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (ximagesink));

  if (GST_IS_PAD (pad) && GST_IS_EVENT (event)) {
    gst_pad_send_event (pad, event);

    gst_object_unref (pad);
  }
}

static void
gst_ximagesink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_ximagesink_navigation_send_event;
}

static void
gst_ximagesink_set_window_handle (GstXOverlay * overlay, guintptr id)
{
  XID xwindow_id = id;
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);
  GstXWindow *xwindow = NULL;
  XWindowAttributes attr;

  /* We acquire the stream lock while setting this window in the element.
     We are basically cleaning tons of stuff replacing the old window, putting
     images while we do that would surely crash */
  g_mutex_lock (ximagesink->flow_lock);

  /* If we already use that window return */
  if (ximagesink->xwindow && (xwindow_id == ximagesink->xwindow->win)) {
    g_mutex_unlock (ximagesink->flow_lock);
    return;
  }

  /* If the element has not initialized the X11 context try to do so */
  if (!ximagesink->xcontext &&
      !(ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink))) {
    g_mutex_unlock (ximagesink->flow_lock);
    /* we have thrown a GST_ELEMENT_ERROR now */
    return;
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
    if (ximagesink->handle_events) {
      XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
          StructureNotifyMask | PointerMotionMask | KeyPressMask |
          KeyReleaseMask);
    }

    xwindow->gc = XCreateGC (ximagesink->xcontext->disp, xwindow->win, 0, NULL);
    g_mutex_unlock (ximagesink->x_lock);
  }

  if (xwindow)
    ximagesink->xwindow = xwindow;

  g_mutex_unlock (ximagesink->flow_lock);
}

static void
gst_ximagesink_expose (GstXOverlay * overlay)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);

  gst_ximagesink_xwindow_update_geometry (ximagesink);
  gst_ximagesink_ximage_put (ximagesink, NULL);
}

static void
gst_ximagesink_set_event_handling (GstXOverlay * overlay,
    gboolean handle_events)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);

  ximagesink->handle_events = handle_events;

  g_mutex_lock (ximagesink->flow_lock);

  if (G_UNLIKELY (!ximagesink->xwindow)) {
    g_mutex_unlock (ximagesink->flow_lock);
    return;
  }

  g_mutex_lock (ximagesink->x_lock);

  if (handle_events) {
    if (ximagesink->xwindow->internal) {
      XSelectInput (ximagesink->xcontext->disp, ximagesink->xwindow->win,
          ExposureMask | StructureNotifyMask | PointerMotionMask |
          KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
    } else {
      XSelectInput (ximagesink->xcontext->disp, ximagesink->xwindow->win,
          ExposureMask | StructureNotifyMask | PointerMotionMask |
          KeyPressMask | KeyReleaseMask);
    }
  } else {
    XSelectInput (ximagesink->xcontext->disp, ximagesink->xwindow->win, 0);
  }

  g_mutex_unlock (ximagesink->x_lock);

  g_mutex_unlock (ximagesink->flow_lock);
}

static void
gst_ximagesink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_window_handle = gst_ximagesink_set_window_handle;
  iface->expose = gst_ximagesink_expose;
  iface->handle_events = gst_ximagesink_set_event_handling;
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
    case PROP_FORCE_ASPECT_RATIO:
      ximagesink->keep_aspect = g_value_get_boolean (value);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
    {
      GValue *tmp;

      tmp = g_new0 (GValue, 1);
      g_value_init (tmp, GST_TYPE_FRACTION);

      if (!g_value_transform (value, tmp)) {
        GST_WARNING_OBJECT (ximagesink,
            "Could not transform string to aspect ratio");
        g_free (tmp);
      } else {
        GST_DEBUG_OBJECT (ximagesink, "set PAR to %d/%d",
            gst_value_get_fraction_numerator (tmp),
            gst_value_get_fraction_denominator (tmp));
        g_free (ximagesink->par);
        ximagesink->par = tmp;
      }
    }
      break;
    case PROP_HANDLE_EVENTS:
      gst_ximagesink_set_event_handling (GST_X_OVERLAY (ximagesink),
          g_value_get_boolean (value));
      gst_ximagesink_manage_event_thread (ximagesink);
      break;
    case PROP_HANDLE_EXPOSE:
      ximagesink->handle_expose = g_value_get_boolean (value);
      gst_ximagesink_manage_event_thread (ximagesink);
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
      g_value_set_string (value, ximagesink->display_name);
      break;
    case PROP_SYNCHRONOUS:
      g_value_set_boolean (value, ximagesink->synchronous);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, ximagesink->keep_aspect);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      if (ximagesink->par)
        g_value_transform (ximagesink->par, value);
      break;
    case PROP_HANDLE_EVENTS:
      g_value_set_boolean (value, ximagesink->handle_events);
      break;
    case PROP_HANDLE_EXPOSE:
      g_value_set_boolean (value, ximagesink->handle_expose);
      break;
    case PROP_WINDOW_WIDTH:
      if (ximagesink->xwindow)
        g_value_set_uint64 (value, ximagesink->xwindow->width);
      else
        g_value_set_uint64 (value, 0);
      break;
    case PROP_WINDOW_HEIGHT:
      if (ximagesink->xwindow)
        g_value_set_uint64 (value, ximagesink->xwindow->height);
      else
        g_value_set_uint64 (value, 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ximagesink_reset (GstXImageSink * ximagesink)
{
  GThread *thread;

  GST_OBJECT_LOCK (ximagesink);
  ximagesink->running = FALSE;
  /* grab thread and mark it as NULL */
  thread = ximagesink->event_thread;
  ximagesink->event_thread = NULL;
  GST_OBJECT_UNLOCK (ximagesink);

  /* Wait for our event thread to finish before we clean up our stuff. */
  if (thread)
    g_thread_join (thread);

  if (ximagesink->ximage) {
    gst_buffer_unref (GST_BUFFER_CAST (ximagesink->ximage));
    ximagesink->ximage = NULL;
  }
  if (ximagesink->cur_image) {
    gst_buffer_unref (GST_BUFFER_CAST (ximagesink->cur_image));
    ximagesink->cur_image = NULL;
  }

  gst_ximagesink_bufferpool_clear (ximagesink);

  g_mutex_lock (ximagesink->flow_lock);
  if (ximagesink->xwindow) {
    gst_ximagesink_xwindow_clear (ximagesink, ximagesink->xwindow);
    gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
    ximagesink->xwindow = NULL;
  }
  g_mutex_unlock (ximagesink->flow_lock);

  gst_ximagesink_xcontext_clear (ximagesink);
}

static void
gst_ximagesink_finalize (GObject * object)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (object);

  gst_ximagesink_reset (ximagesink);

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
  if (ximagesink->flow_lock) {
    g_mutex_free (ximagesink->flow_lock);
    ximagesink->flow_lock = NULL;
  }
  if (ximagesink->pool_lock) {
    g_mutex_free (ximagesink->pool_lock);
    ximagesink->pool_lock = NULL;
  }

  g_free (ximagesink->media_title);

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

  ximagesink->event_thread = NULL;
  ximagesink->running = FALSE;

  ximagesink->fps_n = 0;
  ximagesink->fps_d = 1;

  ximagesink->x_lock = g_mutex_new ();
  ximagesink->flow_lock = g_mutex_new ();

  ximagesink->par = NULL;

  ximagesink->pool_lock = g_mutex_new ();
  ximagesink->buffer_pool = NULL;

  ximagesink->synchronous = FALSE;
  ximagesink->keep_aspect = FALSE;
  ximagesink->handle_events = TRUE;
  ximagesink->handle_expose = TRUE;
}

static void
gst_ximagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Video sink", "Sink/Video",
      "A standard X based videosink", "Julien Moutte <julien@moutte.net>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_ximagesink_sink_template_factory);
}

static void
gst_ximagesink_class_init (GstXImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *videosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  videosink_class = (GstVideoSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_ximagesink_finalize;
  gobject_class->set_property = gst_ximagesink_set_property;
  gobject_class->get_property = gst_ximagesink_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SYNCHRONOUS,
      g_param_spec_boolean ("synchronous", "Synchronous",
          "When enabled, runs the X display in synchronous mode. "
          "(unrelated to A/V sync, used only for debugging)", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, reverse caps negotiation (scaling) will respect "
          "original aspect ratio", FALSE,
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

  /**
   * GstXImageSink:window-width
   *
   * Actual width of the video window.
   *
   * Since: 0.10.32
   */
  g_object_class_install_property (gobject_class, PROP_WINDOW_WIDTH,
      g_param_spec_uint64 ("window-width", "window-width",
          "Width of the window", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstXImageSink:window-height
   *
   * Actual height of the video window.
   *
   * Since: 0.10.32
   */
  g_object_class_install_property (gobject_class, PROP_WINDOW_HEIGHT,
      g_param_spec_uint64 ("window-height", "window-height",
          "Height of the window", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_ximagesink_change_state;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_ximagesink_getcaps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_ximagesink_setcaps);
  gstbasesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_ximagesink_buffer_alloc);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_ximagesink_get_times);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_ximagesink_event);

  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_ximagesink_show_frame);
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
      sizeof (GstXImageSink), 0, (GInstanceInitFunc) gst_ximagesink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_ximagesink_interface_init, NULL, NULL,
    };
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_ximagesink_navigation_init, NULL, NULL,
    };
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_ximagesink_xoverlay_init, NULL, NULL,
    };

    ximagesink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstXImageSink", &ximagesink_info, 0);

    g_type_add_interface_static (ximagesink_type, GST_TYPE_IMPLEMENTS_INTERFACE,
        &iface_info);
    g_type_add_interface_static (ximagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
    g_type_add_interface_static (ximagesink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);

    /* register type and create class in a more safe place instead of at
     * runtime since the type registration and class creation is not
     * threadsafe. */
    g_type_class_ref (gst_ximage_buffer_get_type ());
  }

  return ximagesink_type;
}
