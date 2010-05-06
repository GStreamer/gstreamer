/* GStreamer
 * Copyright (C) <2005> Julien Moutte <julien@moutte.net>
 *               <2009>,<2010> Stefan Kost <stefan.kost@nokia.com> 
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
 * SECTION:element-xvimagesink
 *
 * XvImageSink renders video frames to a drawable (XWindow) on a local display
 * using the XVideo extension. Rendering to a remote display is theorically
 * possible but i doubt that the XVideo extension is actually available when
 * connecting to a remote display. This element can receive a Window ID from the
 * application through the XOverlay interface and will then render video frames
 * in this drawable. If no Window ID was provided by the application, the
 * element will create its own internal window and render into it.
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * The XVideo extension, when it's available, handles hardware accelerated
 * scaling of video frames. This means that the element will just accept
 * incoming video frames no matter their geometry and will then put them to the
 * drawable scaling them on the fly. Using the #GstXvImageSink:force-aspect-ratio
 * property it is possible to enforce scaling with a constant aspect ratio,
 * which means drawing black borders around the video frame.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Events</title>
 * <para>
 * XvImageSink creates a thread to handle events coming from the drawable. There
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
 * When changing state to GST_STATE_READY, XvImageSink will open a connection to
 * the display specified in the #GstXvImageSink:display property or the
 * default display if nothing specified. Once this connection is open it will
 * inspect the display configuration including the physical display geometry and
 * then calculate the pixel aspect ratio. When receiving video frames with a
 * different pixel aspect ratio, XvImageSink will use hardware scaling to
 * display the video frames correctly on display's pixel aspect ratio.
 * Sometimes the calculated pixel aspect ratio can be wrong, it is
 * then possible to enforce a specific pixel aspect ratio using the
 * #GstXvImageSink:pixel-aspect-ratio property.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! xvimagesink
 * ]| A pipeline to test hardware scaling.
 * When the test video signal appears you can resize the window and see that
 * video frames are scaled through hardware (no extra CPU cost).
 * |[
 * gst-launch -v videotestsrc ! xvimagesink force-aspect-ratio=true
 * ]| Same pipeline with #GstXvImageSink:force-aspect-ratio property set to true
 * You can observe the borders drawn around the scaled image respecting aspect
 * ratio.
 * |[
 * gst-launch -v videotestsrc ! navigationtest ! xvimagesink
 * ]| A pipeline to test navigation events.
 * While moving the mouse pointer over the test signal you will see a black box
 * following the mouse pointer. If you press the mouse button somewhere on the
 * video and release it somewhere else a green box will appear where you pressed
 * the button and a red one where you released it. (The navigationtest element
 * is part of gst-plugins-good.) You can observe here that even if the images
 * are scaled through hardware the pointer coordinates are converted back to the
 * original video frame geometry so that the box can be drawn to the correct
 * position. This also handles borders correctly, limiting coordinates to the
 * image area
 * |[
 * gst-launch -v videotestsrc ! video/x-raw-yuv, pixel-aspect-ratio=(fraction)4/3 ! xvimagesink
 * ]| This is faking a 4/3 pixel aspect ratio caps on video frames produced by
 * videotestsrc, in most cases the pixel aspect ratio of the display will be
 * 1/1. This means that XvImageSink will have to do the scaling to convert
 * incoming frames to a size that will match the display pixel aspect ratio
 * (from 320x240 to 320x180 in this case). Note that you might have to escape
 * some characters for your shell like '\(fraction\)'.
 * |[
 * gst-launch -v videotestsrc ! xvimagesink hue=100 saturation=-100 brightness=100
 * ]| Demonstrates how to use the colorbalance interface.
 * </refsect2>
 */

/* for developers: there are two useful tools : xvinfo and xvattr */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Our interfaces */
#include <gst/interfaces/navigation.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/interfaces/colorbalance.h>
#include <gst/interfaces/propertyprobe.h>
/* Helper functions */
#include <gst/video/video.h>

/* Object header */
#include "xvimagesink.h"

/* Debugging category */
#include <gst/gstinfo.h>
GST_DEBUG_CATEGORY_STATIC (gst_debug_xvimagesink);
#define GST_CAT_DEFAULT gst_debug_xvimagesink
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

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

static void gst_xvimagesink_reset (GstXvImageSink * xvimagesink);

static GstBufferClass *xvimage_buffer_parent_class = NULL;
static void gst_xvimage_buffer_finalize (GstXvImageBuffer * xvimage);

static void gst_xvimagesink_xwindow_update_geometry (GstXvImageSink *
    xvimagesink);
static gint gst_xvimagesink_get_format_from_caps (GstXvImageSink * xvimagesink,
    GstCaps * caps);
static void gst_xvimagesink_expose (GstXOverlay * overlay);

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
static GstStaticPadTemplate gst_xvimagesink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]; "
        "video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

enum
{
  ARG_0,
  ARG_CONTRAST,
  ARG_BRIGHTNESS,
  ARG_HUE,
  ARG_SATURATION,
  ARG_DISPLAY,
  ARG_SYNCHRONOUS,
  ARG_PIXEL_ASPECT_RATIO,
  ARG_FORCE_ASPECT_RATIO,
  ARG_HANDLE_EVENTS,
  ARG_DEVICE,
  ARG_DEVICE_NAME,
  ARG_HANDLE_EXPOSE,
  ARG_DOUBLE_BUFFER,
  ARG_AUTOPAINT_COLORKEY,
  ARG_COLORKEY,
  ARG_DRAW_BORDERS
};

static GstVideoSinkClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* xvimage buffers */

#define GST_TYPE_XVIMAGE_BUFFER (gst_xvimage_buffer_get_type())

#define GST_IS_XVIMAGE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_XVIMAGE_BUFFER))
#define GST_XVIMAGE_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_XVIMAGE_BUFFER, GstXvImageBuffer))
#define GST_XVIMAGE_BUFFER_CAST(obj) ((GstXvImageBuffer *)(obj))

/* This function destroys a GstXvImage handling XShm availability */
static void
gst_xvimage_buffer_destroy (GstXvImageBuffer * xvimage)
{
  GstXvImageSink *xvimagesink;

  GST_DEBUG_OBJECT (xvimage, "Destroying buffer");

  xvimagesink = xvimage->xvimagesink;
  if (G_UNLIKELY (xvimagesink == NULL))
    goto no_sink;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  GST_OBJECT_LOCK (xvimagesink);

  /* If the destroyed image is the current one we destroy our reference too */
  if (xvimagesink->cur_image == xvimage)
    xvimagesink->cur_image = NULL;

  /* We might have some buffers destroyed after changing state to NULL */
  if (xvimagesink->xcontext == NULL) {
    GST_DEBUG_OBJECT (xvimagesink, "Destroying XvImage after Xcontext");
#ifdef HAVE_XSHM
    /* Need to free the shared memory segment even if the x context
     * was already cleaned up */
    if (xvimage->SHMInfo.shmaddr != ((void *) -1)) {
      shmdt (xvimage->SHMInfo.shmaddr);
    }
#endif
    goto beach;
  }

  g_mutex_lock (xvimagesink->x_lock);

#ifdef HAVE_XSHM
  if (xvimagesink->xcontext->use_xshm) {
    if (xvimage->SHMInfo.shmaddr != ((void *) -1)) {
      GST_DEBUG_OBJECT (xvimagesink, "XServer ShmDetaching from 0x%x id 0x%lx",
          xvimage->SHMInfo.shmid, xvimage->SHMInfo.shmseg);
      XShmDetach (xvimagesink->xcontext->disp, &xvimage->SHMInfo);
      XSync (xvimagesink->xcontext->disp, FALSE);

      shmdt (xvimage->SHMInfo.shmaddr);
    }
    if (xvimage->xvimage)
      XFree (xvimage->xvimage);
  } else
#endif /* HAVE_XSHM */
  {
    if (xvimage->xvimage) {
      if (xvimage->xvimage->data) {
        g_free (xvimage->xvimage->data);
      }
      XFree (xvimage->xvimage);
    }
  }

  XSync (xvimagesink->xcontext->disp, FALSE);

  g_mutex_unlock (xvimagesink->x_lock);

beach:
  GST_OBJECT_UNLOCK (xvimagesink);
  xvimage->xvimagesink = NULL;
  gst_object_unref (xvimagesink);

  GST_MINI_OBJECT_CLASS (xvimage_buffer_parent_class)->finalize (GST_MINI_OBJECT
      (xvimage));

  return;

no_sink:
  {
    GST_WARNING ("no sink found");
    return;
  }
}

static void
gst_xvimage_buffer_finalize (GstXvImageBuffer * xvimage)
{
  GstXvImageSink *xvimagesink;
  gboolean running;

  xvimagesink = xvimage->xvimagesink;
  if (G_UNLIKELY (xvimagesink == NULL))
    goto no_sink;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  GST_OBJECT_LOCK (xvimagesink);
  running = xvimagesink->running;
  GST_OBJECT_UNLOCK (xvimagesink);

  /* If our geometry changed we can't reuse that image. */
  if (running == FALSE) {
    GST_LOG_OBJECT (xvimage, "destroy image as sink is shutting down");
    gst_xvimage_buffer_destroy (xvimage);
  } else if ((xvimage->width != xvimagesink->video_width) ||
      (xvimage->height != xvimagesink->video_height)) {
    GST_LOG_OBJECT (xvimage,
        "destroy image as its size changed %dx%d vs current %dx%d",
        xvimage->width, xvimage->height,
        xvimagesink->video_width, xvimagesink->video_height);
    gst_xvimage_buffer_destroy (xvimage);
  } else {
    /* In that case we can reuse the image and add it to our image pool. */
    GST_LOG_OBJECT (xvimage, "recycling image in pool");
    /* need to increment the refcount again to recycle */
    gst_buffer_ref (GST_BUFFER_CAST (xvimage));
    g_mutex_lock (xvimagesink->pool_lock);
    xvimagesink->image_pool = g_slist_prepend (xvimagesink->image_pool,
        xvimage);
    g_mutex_unlock (xvimagesink->pool_lock);
  }
  return;

no_sink:
  {
    GST_WARNING ("no sink found");
    return;
  }
}

static void
gst_xvimage_buffer_free (GstXvImageBuffer * xvimage)
{
  /* make sure it is not recycled */
  xvimage->width = -1;
  xvimage->height = -1;
  gst_buffer_unref (GST_BUFFER (xvimage));
}

static void
gst_xvimage_buffer_init (GstXvImageBuffer * xvimage, gpointer g_class)
{
#ifdef HAVE_XSHM
  xvimage->SHMInfo.shmaddr = ((void *) -1);
  xvimage->SHMInfo.shmid = -1;
#endif
}

static void
gst_xvimage_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  xvimage_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_xvimage_buffer_finalize;
}

static GType
gst_xvimage_buffer_get_type (void)
{
  static GType _gst_xvimage_buffer_type;

  if (G_UNLIKELY (_gst_xvimage_buffer_type == 0)) {
    static const GTypeInfo xvimage_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_xvimage_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstXvImageBuffer),
      0,
      (GInstanceInitFunc) gst_xvimage_buffer_init,
      NULL
    };
    _gst_xvimage_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstXvImageBuffer", &xvimage_buffer_info, 0);
  }
  return _gst_xvimage_buffer_type;
}

/* X11 stuff */

static gboolean error_caught = FALSE;

static int
gst_xvimagesink_handle_xerror (Display * display, XErrorEvent * xevent)
{
  char error_msg[1024];

  XGetErrorText (display, xevent->error_code, error_msg, 1024);
  GST_DEBUG ("xvimagesink triggered an XError. error: %s", error_msg);
  error_caught = TRUE;
  return 0;
}

#ifdef HAVE_XSHM
/* This function checks that it is actually really possible to create an image
   using XShm */
static gboolean
gst_xvimagesink_check_xshm_calls (GstXContext * xcontext)
{
  XvImage *xvimage;
  XShmSegmentInfo SHMInfo;
  gint size;
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
  handler = XSetErrorHandler (gst_xvimagesink_handle_xerror);

  /* Trying to create a 1x1 picture */
  GST_DEBUG ("XvShmCreateImage of 1x1");
  xvimage = XvShmCreateImage (xcontext->disp, xcontext->xv_port_id,
      xcontext->im_format, NULL, 1, 1, &SHMInfo);

  /* Might cause an error, sync to ensure it is noticed */
  XSync (xcontext->disp, FALSE);
  if (!xvimage || error_caught) {
    GST_WARNING ("could not XvShmCreateImage a 1x1 image");
    goto beach;
  }
  size = xvimage->data_size;

  SHMInfo.shmid = shmget (IPC_PRIVATE, size, IPC_CREAT | 0777);
  if (SHMInfo.shmid == -1) {
    GST_WARNING ("could not get shared memory of %d bytes", size);
    goto beach;
  }

  SHMInfo.shmaddr = shmat (SHMInfo.shmid, NULL, 0);
  if (SHMInfo.shmaddr == ((void *) -1)) {
    GST_WARNING ("Failed to shmat: %s", g_strerror (errno));
    /* Clean up the shared memory segment */
    shmctl (SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }

  xvimage->data = SHMInfo.shmaddr;
  SHMInfo.readOnly = FALSE;

  if (XShmAttach (xcontext->disp, &SHMInfo) == 0) {
    GST_WARNING ("Failed to XShmAttach");
    /* Clean up the shared memory segment */
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
    GST_DEBUG ("XServer ShmAttached to 0x%x, id 0x%lx", SHMInfo.shmid,
        SHMInfo.shmseg);

    did_attach = TRUE;
    /* store whether we succeeded in result */
    result = TRUE;
  } else {
    GST_WARNING ("MIT-SHM extension check failed at XShmAttach. "
        "Not using shared memory.");
  }

beach:
  /* Sync to ensure we swallow any errors we caused and reset error_caught */
  XSync (xcontext->disp, FALSE);

  error_caught = FALSE;
  XSetErrorHandler (handler);

  if (did_attach) {
    GST_DEBUG ("XServer ShmDetaching from 0x%x id 0x%lx",
        SHMInfo.shmid, SHMInfo.shmseg);
    XShmDetach (xcontext->disp, &SHMInfo);
    XSync (xcontext->disp, FALSE);
  }
  if (SHMInfo.shmaddr != ((void *) -1))
    shmdt (SHMInfo.shmaddr);
  if (xvimage)
    XFree (xvimage);
  return result;
}
#endif /* HAVE_XSHM */

/* This function handles GstXvImage creation depending on XShm availability */
static GstXvImageBuffer *
gst_xvimagesink_xvimage_new (GstXvImageSink * xvimagesink, GstCaps * caps)
{
  GstXvImageBuffer *xvimage = NULL;
  GstStructure *structure = NULL;
  gboolean succeeded = FALSE;
  int (*handler) (Display *, XErrorEvent *);

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);

  if (caps == NULL)
    return NULL;

  xvimage = (GstXvImageBuffer *) gst_mini_object_new (GST_TYPE_XVIMAGE_BUFFER);
  GST_DEBUG_OBJECT (xvimage, "Creating new XvImageBuffer");

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &xvimage->width) ||
      !gst_structure_get_int (structure, "height", &xvimage->height)) {
    GST_WARNING ("failed getting geometry from caps %" GST_PTR_FORMAT, caps);
  }

  GST_LOG_OBJECT (xvimagesink, "creating %dx%d", xvimage->width,
      xvimage->height);

  xvimage->im_format = gst_xvimagesink_get_format_from_caps (xvimagesink, caps);
  if (xvimage->im_format == -1) {
    GST_WARNING_OBJECT (xvimagesink, "failed to get format from caps %"
        GST_PTR_FORMAT, caps);
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            xvimage->width, xvimage->height), ("Invalid input caps"));
    goto beach_unlocked;
  }
  xvimage->xvimagesink = gst_object_ref (xvimagesink);

  g_mutex_lock (xvimagesink->x_lock);

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_xvimagesink_handle_xerror);

#ifdef HAVE_XSHM
  if (xvimagesink->xcontext->use_xshm) {
    int expected_size;

    xvimage->xvimage = XvShmCreateImage (xvimagesink->xcontext->disp,
        xvimagesink->xcontext->xv_port_id,
        xvimage->im_format, NULL,
        xvimage->width, xvimage->height, &xvimage->SHMInfo);
    if (!xvimage->xvimage || error_caught) {
      g_mutex_unlock (xvimagesink->x_lock);
      /* Reset error handler */
      error_caught = FALSE;
      XSetErrorHandler (handler);
      /* Push an error */
      GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              xvimage->width, xvimage->height),
          ("could not XvShmCreateImage a %dx%d image",
              xvimage->width, xvimage->height));
      goto beach_unlocked;
    }

    /* we have to use the returned data_size for our shm size */
    xvimage->size = xvimage->xvimage->data_size;
    GST_LOG_OBJECT (xvimagesink, "XShm image size is %" G_GSIZE_FORMAT,
        xvimage->size);

    /* calculate the expected size.  This is only for sanity checking the
     * number we get from X. */
    switch (xvimage->im_format) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      {
        gint pitches[3];
        gint offsets[3];
        guint plane;

        offsets[0] = 0;
        pitches[0] = GST_ROUND_UP_4 (xvimage->width);
        offsets[1] = offsets[0] + pitches[0] * GST_ROUND_UP_2 (xvimage->height);
        pitches[1] = GST_ROUND_UP_8 (xvimage->width) / 2;
        offsets[2] =
            offsets[1] + pitches[1] * GST_ROUND_UP_2 (xvimage->height) / 2;
        pitches[2] = GST_ROUND_UP_8 (pitches[0]) / 2;

        expected_size =
            offsets[2] + pitches[2] * GST_ROUND_UP_2 (xvimage->height) / 2;

        for (plane = 0; plane < xvimage->xvimage->num_planes; plane++) {
          GST_DEBUG_OBJECT (xvimagesink,
              "Plane %u has a expected pitch of %d bytes, " "offset of %d",
              plane, pitches[plane], offsets[plane]);
        }
        break;
      }
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        expected_size = xvimage->height * GST_ROUND_UP_4 (xvimage->width * 2);
        break;
      default:
        expected_size = 0;
        break;
    }
    if (expected_size != 0 && xvimage->size != expected_size) {
      GST_WARNING_OBJECT (xvimagesink,
          "unexpected XShm image size (got %" G_GSIZE_FORMAT ", expected %d)",
          xvimage->size, expected_size);
    }

    /* Be verbose about our XvImage stride */
    {
      guint plane;

      for (plane = 0; plane < xvimage->xvimage->num_planes; plane++) {
        GST_DEBUG_OBJECT (xvimagesink, "Plane %u has a pitch of %d bytes, "
            "offset of %d", plane, xvimage->xvimage->pitches[plane],
            xvimage->xvimage->offsets[plane]);
      }
    }

    xvimage->SHMInfo.shmid = shmget (IPC_PRIVATE, xvimage->size,
        IPC_CREAT | 0777);
    if (xvimage->SHMInfo.shmid == -1) {
      g_mutex_unlock (xvimagesink->x_lock);
      GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              xvimage->width, xvimage->height),
          ("could not get shared memory of %" G_GSIZE_FORMAT " bytes",
              xvimage->size));
      goto beach_unlocked;
    }

    xvimage->SHMInfo.shmaddr = shmat (xvimage->SHMInfo.shmid, NULL, 0);
    if (xvimage->SHMInfo.shmaddr == ((void *) -1)) {
      g_mutex_unlock (xvimagesink->x_lock);
      GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              xvimage->width, xvimage->height),
          ("Failed to shmat: %s", g_strerror (errno)));
      /* Clean up the shared memory segment */
      shmctl (xvimage->SHMInfo.shmid, IPC_RMID, NULL);
      goto beach_unlocked;
    }

    xvimage->xvimage->data = xvimage->SHMInfo.shmaddr;
    xvimage->SHMInfo.readOnly = FALSE;

    if (XShmAttach (xvimagesink->xcontext->disp, &xvimage->SHMInfo) == 0) {
      /* Clean up the shared memory segment */
      shmctl (xvimage->SHMInfo.shmid, IPC_RMID, NULL);

      g_mutex_unlock (xvimagesink->x_lock);
      GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              xvimage->width, xvimage->height), ("Failed to XShmAttach"));
      goto beach_unlocked;
    }

    XSync (xvimagesink->xcontext->disp, FALSE);

    /* Delete the shared memory segment as soon as we everyone is attached.
     * This way, it will be deleted as soon as we detach later, and not
     * leaked if we crash. */
    shmctl (xvimage->SHMInfo.shmid, IPC_RMID, NULL);

    GST_DEBUG_OBJECT (xvimagesink, "XServer ShmAttached to 0x%x, id 0x%lx",
        xvimage->SHMInfo.shmid, xvimage->SHMInfo.shmseg);
  } else
#endif /* HAVE_XSHM */
  {
    xvimage->xvimage = XvCreateImage (xvimagesink->xcontext->disp,
        xvimagesink->xcontext->xv_port_id,
        xvimage->im_format, NULL, xvimage->width, xvimage->height);
    if (!xvimage->xvimage || error_caught) {
      g_mutex_unlock (xvimagesink->x_lock);
      /* Reset error handler */
      error_caught = FALSE;
      XSetErrorHandler (handler);
      /* Push an error */
      GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
          ("Failed to create outputimage buffer of %dx%d pixels",
              xvimage->width, xvimage->height),
          ("could not XvCreateImage a %dx%d image",
              xvimage->width, xvimage->height));
      goto beach_unlocked;
    }

    /* we have to use the returned data_size for our image size */
    xvimage->size = xvimage->xvimage->data_size;
    xvimage->xvimage->data = g_malloc (xvimage->size);

    XSync (xvimagesink->xcontext->disp, FALSE);
  }

  /* Reset error handler */
  error_caught = FALSE;
  XSetErrorHandler (handler);

  succeeded = TRUE;

  GST_BUFFER_DATA (xvimage) = (guchar *) xvimage->xvimage->data;
  GST_BUFFER_SIZE (xvimage) = xvimage->size;

  g_mutex_unlock (xvimagesink->x_lock);

beach_unlocked:
  if (!succeeded) {
    gst_xvimage_buffer_free (xvimage);
    xvimage = NULL;
  }

  return xvimage;
}

/* We are called with the x_lock taken */
static void
gst_xvimagesink_xwindow_draw_borders (GstXvImageSink * xvimagesink,
    GstXWindow * xwindow, GstVideoRectangle rect)
{
  gint t1, t2;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  g_return_if_fail (xwindow != NULL);

  XSetForeground (xvimagesink->xcontext->disp, xwindow->gc,
      xvimagesink->xcontext->black);

  /* Left border */
  if (rect.x > xvimagesink->render_rect.x) {
    XFillRectangle (xvimagesink->xcontext->disp, xwindow->win, xwindow->gc,
        xvimagesink->render_rect.x, xvimagesink->render_rect.y,
        rect.x - xvimagesink->render_rect.x, xvimagesink->render_rect.h);
  }

  /* Right border */
  t1 = rect.x + rect.w;
  t2 = xvimagesink->render_rect.x + xvimagesink->render_rect.w;
  if (t1 < t2) {
    XFillRectangle (xvimagesink->xcontext->disp, xwindow->win, xwindow->gc,
        t1, xvimagesink->render_rect.y, t2 - t1, xvimagesink->render_rect.h);
  }

  /* Top border */
  if (rect.y > xvimagesink->render_rect.y) {
    XFillRectangle (xvimagesink->xcontext->disp, xwindow->win, xwindow->gc,
        xvimagesink->render_rect.x, xvimagesink->render_rect.y,
        xvimagesink->render_rect.w, rect.y - xvimagesink->render_rect.y);
  }

  /* Bottom border */
  t1 = rect.y + rect.h;
  t2 = xvimagesink->render_rect.y + xvimagesink->render_rect.h;
  if (t1 < t2) {
    XFillRectangle (xvimagesink->xcontext->disp, xwindow->win, xwindow->gc,
        xvimagesink->render_rect.x, t1, xvimagesink->render_rect.w, t2 - t1);
  }
}

/* This function puts a GstXvImage on a GstXvImageSink's window. Returns FALSE
 * if no window was available  */
static gboolean
gst_xvimagesink_xvimage_put (GstXvImageSink * xvimagesink,
    GstXvImageBuffer * xvimage)
{
  GstVideoRectangle result;
  gboolean draw_border = FALSE;

  /* We take the flow_lock. If expose is in there we don't want to run
     concurrently from the data flow thread */
  g_mutex_lock (xvimagesink->flow_lock);

  if (G_UNLIKELY (xvimagesink->xwindow == NULL)) {
    g_mutex_unlock (xvimagesink->flow_lock);
    return FALSE;
  }

  /* Draw borders when displaying the first frame. After this
     draw borders only on expose event or after a size change. */
  if (!xvimagesink->cur_image || xvimagesink->redraw_border) {
    draw_border = TRUE;
  }

  /* Store a reference to the last image we put, lose the previous one */
  if (xvimage && xvimagesink->cur_image != xvimage) {
    if (xvimagesink->cur_image) {
      GST_LOG_OBJECT (xvimagesink, "unreffing %p", xvimagesink->cur_image);
      gst_buffer_unref (GST_BUFFER_CAST (xvimagesink->cur_image));
    }
    GST_LOG_OBJECT (xvimagesink, "reffing %p as our current image", xvimage);
    xvimagesink->cur_image =
        GST_XVIMAGE_BUFFER_CAST (gst_buffer_ref (GST_BUFFER_CAST (xvimage)));
  }

  /* Expose sends a NULL image, we take the latest frame */
  if (!xvimage) {
    if (xvimagesink->cur_image) {
      draw_border = TRUE;
      xvimage = xvimagesink->cur_image;
    } else {
      g_mutex_unlock (xvimagesink->flow_lock);
      return TRUE;
    }
  }

  if (xvimagesink->keep_aspect) {
    GstVideoRectangle src, dst;

    /* We use the calculated geometry from _setcaps as a source to respect
       source and screen pixel aspect ratios. */
    src.w = GST_VIDEO_SINK_WIDTH (xvimagesink);
    src.h = GST_VIDEO_SINK_HEIGHT (xvimagesink);
    dst.w = xvimagesink->render_rect.w;
    dst.h = xvimagesink->render_rect.h;

    gst_video_sink_center_rect (src, dst, &result, TRUE);
    result.x += xvimagesink->render_rect.x;
    result.y += xvimagesink->render_rect.y;
  } else {
    memcpy (&result, &xvimagesink->render_rect, sizeof (GstVideoRectangle));
  }

  g_mutex_lock (xvimagesink->x_lock);

  if (draw_border && xvimagesink->draw_borders) {
    gst_xvimagesink_xwindow_draw_borders (xvimagesink, xvimagesink->xwindow,
        result);
    xvimagesink->redraw_border = FALSE;
  }

  /* We scale to the window's geometry */
#ifdef HAVE_XSHM
  if (xvimagesink->xcontext->use_xshm) {
    GST_LOG_OBJECT (xvimagesink,
        "XvShmPutImage with image %dx%d and window %dx%d, from xvimage %"
        GST_PTR_FORMAT,
        xvimage->width, xvimage->height,
        xvimagesink->render_rect.w, xvimagesink->render_rect.h, xvimage);

    XvShmPutImage (xvimagesink->xcontext->disp,
        xvimagesink->xcontext->xv_port_id,
        xvimagesink->xwindow->win,
        xvimagesink->xwindow->gc, xvimage->xvimage,
        xvimagesink->disp_x, xvimagesink->disp_y,
        xvimagesink->disp_width, xvimagesink->disp_height,
        result.x, result.y, result.w, result.h, FALSE);
  } else
#endif /* HAVE_XSHM */
  {
    XvPutImage (xvimagesink->xcontext->disp,
        xvimagesink->xcontext->xv_port_id,
        xvimagesink->xwindow->win,
        xvimagesink->xwindow->gc, xvimage->xvimage,
        xvimagesink->disp_x, xvimagesink->disp_y,
        xvimagesink->disp_width, xvimagesink->disp_height,
        result.x, result.y, result.w, result.h);
  }

  XSync (xvimagesink->xcontext->disp, FALSE);

  g_mutex_unlock (xvimagesink->x_lock);

  g_mutex_unlock (xvimagesink->flow_lock);

  return TRUE;
}

static gboolean
gst_xvimagesink_xwindow_decorate (GstXvImageSink * xvimagesink,
    GstXWindow * window)
{
  Atom hints_atom = None;
  MotifWmHints *hints;

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), FALSE);
  g_return_val_if_fail (window != NULL, FALSE);

  g_mutex_lock (xvimagesink->x_lock);

  hints_atom = XInternAtom (xvimagesink->xcontext->disp, "_MOTIF_WM_HINTS",
      True);
  if (hints_atom == None) {
    g_mutex_unlock (xvimagesink->x_lock);
    return FALSE;
  }

  hints = g_malloc0 (sizeof (MotifWmHints));

  hints->flags |= MWM_HINTS_DECORATIONS;
  hints->decorations = 1 << 0;

  XChangeProperty (xvimagesink->xcontext->disp, window->win,
      hints_atom, hints_atom, 32, PropModeReplace,
      (guchar *) hints, sizeof (MotifWmHints) / sizeof (long));

  XSync (xvimagesink->xcontext->disp, FALSE);

  g_mutex_unlock (xvimagesink->x_lock);

  g_free (hints);

  return TRUE;
}

static void
gst_xvimagesink_xwindow_set_title (GstXvImageSink * xvimagesink,
    GstXWindow * xwindow, const gchar * media_title)
{
  if (media_title) {
    g_free (xvimagesink->media_title);
    xvimagesink->media_title = g_strdup (media_title);
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

      if (app_name && xvimagesink->media_title) {
        title = title_mem = g_strconcat (xvimagesink->media_title, " : ",
            app_name, NULL);
      } else if (app_name) {
        title = app_name;
      } else if (xvimagesink->media_title) {
        title = xvimagesink->media_title;
      }

      if (title) {
        if ((XStringListToTextProperty (((char **) &title), 1,
                    &xproperty)) != 0) {
          XSetWMName (xvimagesink->xcontext->disp, xwindow->win, &xproperty);
          XFree (xproperty.value);
        }

        g_free (title_mem);
      }
    }
  }
}

/* This function handles a GstXWindow creation
 * The width and height are the actual pixel size on the display */
static GstXWindow *
gst_xvimagesink_xwindow_new (GstXvImageSink * xvimagesink,
    gint width, gint height)
{
  GstXWindow *xwindow = NULL;
  XGCValues values;

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);

  xwindow = g_new0 (GstXWindow, 1);

  xvimagesink->render_rect.x = xvimagesink->render_rect.y = 0;
  xvimagesink->render_rect.w = width;
  xvimagesink->render_rect.h = height;

  xwindow->width = width;
  xwindow->height = height;
  xwindow->internal = TRUE;

  g_mutex_lock (xvimagesink->x_lock);

  xwindow->win = XCreateSimpleWindow (xvimagesink->xcontext->disp,
      xvimagesink->xcontext->root,
      0, 0, width, height, 0, 0, xvimagesink->xcontext->black);

  /* We have to do that to prevent X from redrawing the background on
   * ConfigureNotify. This takes away flickering of video when resizing. */
  XSetWindowBackgroundPixmap (xvimagesink->xcontext->disp, xwindow->win, None);

  /* set application name as a title */
  gst_xvimagesink_xwindow_set_title (xvimagesink, xwindow, NULL);

  if (xvimagesink->handle_events) {
    Atom wm_delete;

    XSelectInput (xvimagesink->xcontext->disp, xwindow->win, ExposureMask |
        StructureNotifyMask | PointerMotionMask | KeyPressMask |
        KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);

    /* Tell the window manager we'd like delete client messages instead of
     * being killed */
    wm_delete = XInternAtom (xvimagesink->xcontext->disp,
        "WM_DELETE_WINDOW", True);
    if (wm_delete != None) {
      (void) XSetWMProtocols (xvimagesink->xcontext->disp, xwindow->win,
          &wm_delete, 1);
    }
  }

  xwindow->gc = XCreateGC (xvimagesink->xcontext->disp,
      xwindow->win, 0, &values);

  XMapRaised (xvimagesink->xcontext->disp, xwindow->win);

  XSync (xvimagesink->xcontext->disp, FALSE);

  g_mutex_unlock (xvimagesink->x_lock);

  gst_xvimagesink_xwindow_decorate (xvimagesink, xwindow);

  gst_x_overlay_got_xwindow_id (GST_X_OVERLAY (xvimagesink), xwindow->win);

  return xwindow;
}

/* This function destroys a GstXWindow */
static void
gst_xvimagesink_xwindow_destroy (GstXvImageSink * xvimagesink,
    GstXWindow * xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  g_mutex_lock (xvimagesink->x_lock);

  /* If we did not create that window we just free the GC and let it live */
  if (xwindow->internal)
    XDestroyWindow (xvimagesink->xcontext->disp, xwindow->win);
  else
    XSelectInput (xvimagesink->xcontext->disp, xwindow->win, 0);

  XFreeGC (xvimagesink->xcontext->disp, xwindow->gc);

  XSync (xvimagesink->xcontext->disp, FALSE);

  g_mutex_unlock (xvimagesink->x_lock);

  g_free (xwindow);
}

static void
gst_xvimagesink_xwindow_update_geometry (GstXvImageSink * xvimagesink)
{
  XWindowAttributes attr;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  /* Update the window geometry */
  g_mutex_lock (xvimagesink->x_lock);
  if (G_UNLIKELY (xvimagesink->xwindow == NULL)) {
    g_mutex_unlock (xvimagesink->x_lock);
    return;
  }

  XGetWindowAttributes (xvimagesink->xcontext->disp,
      xvimagesink->xwindow->win, &attr);

  xvimagesink->xwindow->width = attr.width;
  xvimagesink->xwindow->height = attr.height;

  if (!xvimagesink->have_render_rect) {
    xvimagesink->render_rect.x = xvimagesink->render_rect.y = 0;
    xvimagesink->render_rect.w = attr.width;
    xvimagesink->render_rect.h = attr.height;
  }

  g_mutex_unlock (xvimagesink->x_lock);
}

static void
gst_xvimagesink_xwindow_clear (GstXvImageSink * xvimagesink,
    GstXWindow * xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  g_mutex_lock (xvimagesink->x_lock);

  XvStopVideo (xvimagesink->xcontext->disp, xvimagesink->xcontext->xv_port_id,
      xwindow->win);

  XSetForeground (xvimagesink->xcontext->disp, xwindow->gc,
      xvimagesink->xcontext->black);

  XFillRectangle (xvimagesink->xcontext->disp, xwindow->win, xwindow->gc,
      xvimagesink->render_rect.x, xvimagesink->render_rect.y,
      xvimagesink->render_rect.w, xvimagesink->render_rect.h);

  XSync (xvimagesink->xcontext->disp, FALSE);

  g_mutex_unlock (xvimagesink->x_lock);
}

/* This function commits our internal colorbalance settings to our grabbed Xv
   port. If the xcontext is not initialized yet it simply returns */
static void
gst_xvimagesink_update_colorbalance (GstXvImageSink * xvimagesink)
{
  GList *channels = NULL;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  /* If we haven't initialized the X context we can't update anything */
  if (xvimagesink->xcontext == NULL)
    return;

  /* Don't set the attributes if they haven't been changed, to avoid
   * rounding errors changing the values */
  if (!xvimagesink->cb_changed)
    return;

  /* For each channel of the colorbalance we calculate the correct value
     doing range conversion and then set the Xv port attribute to match our
     values. */
  channels = xvimagesink->xcontext->channels_list;

  while (channels) {
    if (channels->data && GST_IS_COLOR_BALANCE_CHANNEL (channels->data)) {
      GstColorBalanceChannel *channel = NULL;
      Atom prop_atom;
      gint value = 0;
      gdouble convert_coef;

      channel = GST_COLOR_BALANCE_CHANNEL (channels->data);
      g_object_ref (channel);

      /* Our range conversion coef */
      convert_coef = (channel->max_value - channel->min_value) / 2000.0;

      if (g_ascii_strcasecmp (channel->label, "XV_HUE") == 0) {
        value = xvimagesink->hue;
      } else if (g_ascii_strcasecmp (channel->label, "XV_SATURATION") == 0) {
        value = xvimagesink->saturation;
      } else if (g_ascii_strcasecmp (channel->label, "XV_CONTRAST") == 0) {
        value = xvimagesink->contrast;
      } else if (g_ascii_strcasecmp (channel->label, "XV_BRIGHTNESS") == 0) {
        value = xvimagesink->brightness;
      } else {
        g_warning ("got an unknown channel %s", channel->label);
        g_object_unref (channel);
        return;
      }

      /* Committing to Xv port */
      g_mutex_lock (xvimagesink->x_lock);
      prop_atom =
          XInternAtom (xvimagesink->xcontext->disp, channel->label, True);
      if (prop_atom != None) {
        int xv_value;
        xv_value =
            floor (0.5 + (value + 1000) * convert_coef + channel->min_value);
        XvSetPortAttribute (xvimagesink->xcontext->disp,
            xvimagesink->xcontext->xv_port_id, prop_atom, xv_value);
      }
      g_mutex_unlock (xvimagesink->x_lock);

      g_object_unref (channel);
    }
    channels = g_list_next (channels);
  }
}

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_xvimagesink_handle_xevents (GstXvImageSink * xvimagesink)
{
  XEvent e;
  guint pointer_x = 0, pointer_y = 0;
  gboolean pointer_moved = FALSE;
  gboolean exposed = FALSE, configured = FALSE;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  /* Handle Interaction, produces navigation events */

  /* We get all pointer motion events, only the last position is
     interesting. */
  g_mutex_lock (xvimagesink->flow_lock);
  g_mutex_lock (xvimagesink->x_lock);
  while (XCheckWindowEvent (xvimagesink->xcontext->disp,
          xvimagesink->xwindow->win, PointerMotionMask, &e)) {
    g_mutex_unlock (xvimagesink->x_lock);
    g_mutex_unlock (xvimagesink->flow_lock);

    switch (e.type) {
      case MotionNotify:
        pointer_x = e.xmotion.x;
        pointer_y = e.xmotion.y;
        pointer_moved = TRUE;
        break;
      default:
        break;
    }
    g_mutex_lock (xvimagesink->flow_lock);
    g_mutex_lock (xvimagesink->x_lock);
  }
  if (pointer_moved) {
    g_mutex_unlock (xvimagesink->x_lock);
    g_mutex_unlock (xvimagesink->flow_lock);

    GST_DEBUG ("xvimagesink pointer moved over window at %d,%d",
        pointer_x, pointer_y);
    gst_navigation_send_mouse_event (GST_NAVIGATION (xvimagesink),
        "mouse-move", 0, e.xbutton.x, e.xbutton.y);

    g_mutex_lock (xvimagesink->flow_lock);
    g_mutex_lock (xvimagesink->x_lock);
  }

  /* We get all events on our window to throw them upstream */
  while (XCheckWindowEvent (xvimagesink->xcontext->disp,
          xvimagesink->xwindow->win,
          KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask,
          &e)) {
    KeySym keysym;

    /* We lock only for the X function call */
    g_mutex_unlock (xvimagesink->x_lock);
    g_mutex_unlock (xvimagesink->flow_lock);

    switch (e.type) {
      case ButtonPress:
        /* Mouse button pressed over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("xvimagesink button %d pressed over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.y);
        gst_navigation_send_mouse_event (GST_NAVIGATION (xvimagesink),
            "mouse-button-press", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case ButtonRelease:
        /* Mouse button released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("xvimagesink button %d released over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.y);
        gst_navigation_send_mouse_event (GST_NAVIGATION (xvimagesink),
            "mouse-button-release", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case KeyPress:
      case KeyRelease:
        /* Key pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("xvimagesink key %d pressed over window at %d,%d",
            e.xkey.keycode, e.xkey.x, e.xkey.y);
        g_mutex_lock (xvimagesink->x_lock);
        keysym = XKeycodeToKeysym (xvimagesink->xcontext->disp,
            e.xkey.keycode, 0);
        g_mutex_unlock (xvimagesink->x_lock);
        if (keysym != NoSymbol) {
          char *key_str = NULL;

          g_mutex_lock (xvimagesink->x_lock);
          key_str = XKeysymToString (keysym);
          g_mutex_unlock (xvimagesink->x_lock);
          gst_navigation_send_key_event (GST_NAVIGATION (xvimagesink),
              e.type == KeyPress ? "key-press" : "key-release", key_str);
        } else {
          gst_navigation_send_key_event (GST_NAVIGATION (xvimagesink),
              e.type == KeyPress ? "key-press" : "key-release", "unknown");
        }
        break;
      default:
        GST_DEBUG ("xvimagesink unhandled X event (%d)", e.type);
    }
    g_mutex_lock (xvimagesink->flow_lock);
    g_mutex_lock (xvimagesink->x_lock);
  }

  /* Handle Expose */
  while (XCheckWindowEvent (xvimagesink->xcontext->disp,
          xvimagesink->xwindow->win, ExposureMask | StructureNotifyMask, &e)) {
    switch (e.type) {
      case Expose:
        exposed = TRUE;
        break;
      case ConfigureNotify:
        g_mutex_unlock (xvimagesink->x_lock);
        gst_xvimagesink_xwindow_update_geometry (xvimagesink);
        g_mutex_lock (xvimagesink->x_lock);
        configured = TRUE;
        break;
      default:
        break;
    }
  }

  if (xvimagesink->handle_expose && (exposed || configured)) {
    g_mutex_unlock (xvimagesink->x_lock);
    g_mutex_unlock (xvimagesink->flow_lock);

    gst_xvimagesink_expose (GST_X_OVERLAY (xvimagesink));

    g_mutex_lock (xvimagesink->flow_lock);
    g_mutex_lock (xvimagesink->x_lock);
  }

  /* Handle Display events */
  while (XPending (xvimagesink->xcontext->disp)) {
    XNextEvent (xvimagesink->xcontext->disp, &e);

    switch (e.type) {
      case ClientMessage:{
        Atom wm_delete;

        wm_delete = XInternAtom (xvimagesink->xcontext->disp,
            "WM_DELETE_WINDOW", True);
        if (wm_delete != None && wm_delete == (Atom) e.xclient.data.l[0]) {
          /* Handle window deletion by posting an error on the bus */
          GST_ELEMENT_ERROR (xvimagesink, RESOURCE, NOT_FOUND,
              ("Output window was closed"), (NULL));

          g_mutex_unlock (xvimagesink->x_lock);
          gst_xvimagesink_xwindow_destroy (xvimagesink, xvimagesink->xwindow);
          xvimagesink->xwindow = NULL;
          g_mutex_lock (xvimagesink->x_lock);
        }
        break;
      }
      default:
        break;
    }
  }

  g_mutex_unlock (xvimagesink->x_lock);
  g_mutex_unlock (xvimagesink->flow_lock);
}

static void
gst_lookup_xv_port_from_adaptor (GstXContext * xcontext,
    XvAdaptorInfo * adaptors, int adaptor_no)
{
  gint j;
  gint res;

  /* Do we support XvImageMask ? */
  if (!(adaptors[adaptor_no].type & XvImageMask)) {
    GST_DEBUG ("XV Adaptor %s has no support for XvImageMask",
        adaptors[adaptor_no].name);
    return;
  }

  /* We found such an adaptor, looking for an available port */
  for (j = 0; j < adaptors[adaptor_no].num_ports && !xcontext->xv_port_id; j++) {
    /* We try to grab the port */
    res = XvGrabPort (xcontext->disp, adaptors[adaptor_no].base_id + j, 0);
    if (Success == res) {
      xcontext->xv_port_id = adaptors[adaptor_no].base_id + j;
      GST_DEBUG ("XV Adaptor %s with %ld ports", adaptors[adaptor_no].name,
          adaptors[adaptor_no].num_ports);
    } else {
      GST_DEBUG ("GrabPort %d for XV Adaptor %s failed: %d", j,
          adaptors[adaptor_no].name, res);
    }
  }
}

/* This function generates a caps with all supported format by the first
   Xv grabable port we find. We store each one of the supported formats in a
   format list and append the format to a newly created caps that we return
   If this function does not return NULL because of an error, it also grabs
   the port via XvGrabPort */
static GstCaps *
gst_xvimagesink_get_xv_support (GstXvImageSink * xvimagesink,
    GstXContext * xcontext)
{
  gint i;
  XvAdaptorInfo *adaptors;
  gint nb_formats;
  XvImageFormatValues *formats = NULL;
  guint nb_encodings;
  XvEncodingInfo *encodings = NULL;
  gulong max_w = G_MAXINT, max_h = G_MAXINT;
  GstCaps *caps = NULL;
  GstCaps *rgb_caps = NULL;

  g_return_val_if_fail (xcontext != NULL, NULL);

  /* First let's check that XVideo extension is available */
  if (!XQueryExtension (xcontext->disp, "XVideo", &i, &i, &i)) {
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, SETTINGS,
        ("Could not initialise Xv output"),
        ("XVideo extension is not available"));
    return NULL;
  }

  /* Then we get adaptors list */
  if (Success != XvQueryAdaptors (xcontext->disp, xcontext->root,
          &xcontext->nb_adaptors, &adaptors)) {
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, SETTINGS,
        ("Could not initialise Xv output"),
        ("Failed getting XV adaptors list"));
    return NULL;
  }

  xcontext->xv_port_id = 0;

  GST_DEBUG ("Found %u XV adaptor(s)", xcontext->nb_adaptors);

  xcontext->adaptors =
      (gchar **) g_malloc0 (xcontext->nb_adaptors * sizeof (gchar *));

  /* Now fill up our adaptor name array */
  for (i = 0; i < xcontext->nb_adaptors; i++) {
    xcontext->adaptors[i] = g_strdup (adaptors[i].name);
  }

  if (xvimagesink->adaptor_no >= 0 &&
      xvimagesink->adaptor_no < xcontext->nb_adaptors) {
    /* Find xv port from user defined adaptor */
    gst_lookup_xv_port_from_adaptor (xcontext, adaptors,
        xvimagesink->adaptor_no);
  }

  if (!xcontext->xv_port_id) {
    /* Now search for an adaptor that supports XvImageMask */
    for (i = 0; i < xcontext->nb_adaptors && !xcontext->xv_port_id; i++) {
      gst_lookup_xv_port_from_adaptor (xcontext, adaptors, i);
      xvimagesink->adaptor_no = i;
    }
  }

  XvFreeAdaptorInfo (adaptors);

  if (!xcontext->xv_port_id) {
    xvimagesink->adaptor_no = -1;
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, BUSY,
        ("Could not initialise Xv output"), ("No port available"));
    return NULL;
  }

  /* Set XV_AUTOPAINT_COLORKEY and XV_DOUBLE_BUFFER and XV_COLORKEY */
  {
    int count, todo = 3;
    XvAttribute *const attr = XvQueryPortAttributes (xcontext->disp,
        xcontext->xv_port_id, &count);
    static const char autopaint[] = "XV_AUTOPAINT_COLORKEY";
    static const char dbl_buffer[] = "XV_DOUBLE_BUFFER";
    static const char colorkey[] = "XV_COLORKEY";

    GST_DEBUG_OBJECT (xvimagesink, "Checking %d Xv port attributes", count);

    xvimagesink->have_autopaint_colorkey = FALSE;
    xvimagesink->have_double_buffer = FALSE;
    xvimagesink->have_colorkey = FALSE;

    for (i = 0; ((i < count) && todo); i++)
      if (!strcmp (attr[i].name, autopaint)) {
        const Atom atom = XInternAtom (xcontext->disp, autopaint, False);

        /* turn on autopaint colorkey */
        XvSetPortAttribute (xcontext->disp, xcontext->xv_port_id, atom,
            (xvimagesink->autopaint_colorkey ? 1 : 0));
        todo--;
        xvimagesink->have_autopaint_colorkey = TRUE;
      } else if (!strcmp (attr[i].name, dbl_buffer)) {
        const Atom atom = XInternAtom (xcontext->disp, dbl_buffer, False);

        XvSetPortAttribute (xcontext->disp, xcontext->xv_port_id, atom,
            (xvimagesink->double_buffer ? 1 : 0));
        todo--;
        xvimagesink->have_double_buffer = TRUE;
      } else if (!strcmp (attr[i].name, colorkey)) {
        /* Set the colorkey, default is something that is dark but hopefully
         * won't randomly appear on the screen elsewhere (ie not black or greys)
         * can be overridden by setting "colorkey" property
         */
        const Atom atom = XInternAtom (xcontext->disp, colorkey, False);
        guint32 ckey = 0;
        gboolean set_attr = TRUE;
        guint cr, cg, cb;

        /* set a colorkey in the right format RGB565/RGB888
         * We only handle these 2 cases, because they're the only types of
         * devices we've encountered. If we don't recognise it, leave it alone
         */
        cr = (xvimagesink->colorkey >> 16);
        cg = (xvimagesink->colorkey >> 8) & 0xFF;
        cb = (xvimagesink->colorkey) & 0xFF;
        switch (xcontext->depth) {
          case 16:             /* RGB 565 */
            cr >>= 3;
            cg >>= 2;
            cb >>= 3;
            ckey = (cr << 11) | (cg << 5) | cb;
            break;
          case 24:
          case 32:             /* RGB 888 / ARGB 8888 */
            ckey = (cr << 16) | (cg << 8) | cb;
            break;
          default:
            GST_DEBUG_OBJECT (xvimagesink,
                "Unknown bit depth %d for Xv Colorkey - not adjusting",
                xcontext->depth);
            set_attr = FALSE;
            break;
        }

        if (set_attr) {
          ckey = CLAMP (ckey, (guint32) attr[i].min_value,
              (guint32) attr[i].max_value);
          GST_LOG_OBJECT (xvimagesink,
              "Setting color key for display depth %d to 0x%x",
              xcontext->depth, ckey);

          XvSetPortAttribute (xcontext->disp, xcontext->xv_port_id, atom,
              (gint) ckey);
        }
        todo--;
        xvimagesink->have_colorkey = TRUE;
      }

    XFree (attr);
  }

  /* Get the list of encodings supported by the adapter and look for the
   * XV_IMAGE encoding so we can determine the maximum width and height
   * supported */
  XvQueryEncodings (xcontext->disp, xcontext->xv_port_id, &nb_encodings,
      &encodings);

  for (i = 0; i < nb_encodings; i++) {
    GST_LOG_OBJECT (xvimagesink,
        "Encoding %d, name %s, max wxh %lux%lu rate %d/%d",
        i, encodings[i].name, encodings[i].width, encodings[i].height,
        encodings[i].rate.numerator, encodings[i].rate.denominator);
    if (strcmp (encodings[i].name, "XV_IMAGE") == 0) {
      max_w = encodings[i].width;
      max_h = encodings[i].height;
    }
  }

  XvFreeEncodingInfo (encodings);

  /* We get all image formats supported by our port */
  formats = XvListImageFormats (xcontext->disp,
      xcontext->xv_port_id, &nb_formats);
  caps = gst_caps_new_empty ();
  for (i = 0; i < nb_formats; i++) {
    GstCaps *format_caps = NULL;
    gboolean is_rgb_format = FALSE;

    /* We set the image format of the xcontext to an existing one. This
       is just some valid image format for making our xshm calls check before
       caps negotiation really happens. */
    xcontext->im_format = formats[i].id;

    switch (formats[i].type) {
      case XvRGB:
      {
        XvImageFormatValues *fmt = &(formats[i]);
        gint endianness = G_BIG_ENDIAN;

        if (fmt->byte_order == LSBFirst) {
          /* our caps system handles 24/32bpp RGB as big-endian. */
          if (fmt->bits_per_pixel == 24 || fmt->bits_per_pixel == 32) {
            fmt->red_mask = GUINT32_TO_BE (fmt->red_mask);
            fmt->green_mask = GUINT32_TO_BE (fmt->green_mask);
            fmt->blue_mask = GUINT32_TO_BE (fmt->blue_mask);

            if (fmt->bits_per_pixel == 24) {
              fmt->red_mask >>= 8;
              fmt->green_mask >>= 8;
              fmt->blue_mask >>= 8;
            }
          } else
            endianness = G_LITTLE_ENDIAN;
        }

        format_caps = gst_caps_new_simple ("video/x-raw-rgb",
            "endianness", G_TYPE_INT, endianness,
            "depth", G_TYPE_INT, fmt->depth,
            "bpp", G_TYPE_INT, fmt->bits_per_pixel,
            "red_mask", G_TYPE_INT, fmt->red_mask,
            "green_mask", G_TYPE_INT, fmt->green_mask,
            "blue_mask", G_TYPE_INT, fmt->blue_mask,
            "width", GST_TYPE_INT_RANGE, 1, max_w,
            "height", GST_TYPE_INT_RANGE, 1, max_h,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

        is_rgb_format = TRUE;
        break;
      }
      case XvYUV:
        format_caps = gst_caps_new_simple ("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, formats[i].id,
            "width", GST_TYPE_INT_RANGE, 1, max_w,
            "height", GST_TYPE_INT_RANGE, 1, max_h,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    if (format_caps) {
      GstXvImageFormat *format = NULL;

      format = g_new0 (GstXvImageFormat, 1);
      if (format) {
        format->format = formats[i].id;
        format->caps = gst_caps_copy (format_caps);
        xcontext->formats_list = g_list_append (xcontext->formats_list, format);
      }

      if (is_rgb_format) {
        if (rgb_caps == NULL)
          rgb_caps = format_caps;
        else
          gst_caps_append (rgb_caps, format_caps);
      } else
        gst_caps_append (caps, format_caps);
    }
  }

  /* Collected all caps into either the caps or rgb_caps structures.
   * Append rgb_caps on the end of YUV, so that YUV is always preferred */
  if (rgb_caps)
    gst_caps_append (caps, rgb_caps);

  if (formats)
    XFree (formats);

  GST_DEBUG ("Generated the following caps: %" GST_PTR_FORMAT, caps);

  if (gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    XvUngrabPort (xcontext->disp, xcontext->xv_port_id, 0);
    GST_ELEMENT_ERROR (xvimagesink, STREAM, WRONG_TYPE, (NULL),
        ("No supported format found"));
    return NULL;
  }

  return caps;
}

static gpointer
gst_xvimagesink_event_thread (GstXvImageSink * xvimagesink)
{
  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);

  GST_OBJECT_LOCK (xvimagesink);
  while (xvimagesink->running) {
    GST_OBJECT_UNLOCK (xvimagesink);

    if (xvimagesink->xwindow) {
      gst_xvimagesink_handle_xevents (xvimagesink);
    }
    /* FIXME: do we want to align this with the framerate or anything else? */
    g_usleep (G_USEC_PER_SEC / 20);

    GST_OBJECT_LOCK (xvimagesink);
  }
  GST_OBJECT_UNLOCK (xvimagesink);

  return NULL;
}

static void
gst_xvimagesink_manage_event_thread (GstXvImageSink * xvimagesink)
{
  GThread *thread = NULL;

  /* don't start the thread too early */
  if (xvimagesink->xcontext == NULL) {
    return;
  }

  GST_OBJECT_LOCK (xvimagesink);
  if (xvimagesink->handle_expose || xvimagesink->handle_events) {
    if (!xvimagesink->event_thread) {
      /* Setup our event listening thread */
      GST_DEBUG_OBJECT (xvimagesink, "run xevent thread, expose %d, events %d",
          xvimagesink->handle_expose, xvimagesink->handle_events);
      xvimagesink->running = TRUE;
      xvimagesink->event_thread = g_thread_create (
          (GThreadFunc) gst_xvimagesink_event_thread, xvimagesink, TRUE, NULL);
    }
  } else {
    if (xvimagesink->event_thread) {
      GST_DEBUG_OBJECT (xvimagesink, "stop xevent thread, expose %d, events %d",
          xvimagesink->handle_expose, xvimagesink->handle_events);
      xvimagesink->running = FALSE;
      /* grab thread and mark it as NULL */
      thread = xvimagesink->event_thread;
      xvimagesink->event_thread = NULL;
    }
  }
  GST_OBJECT_UNLOCK (xvimagesink);

  /* Wait for our event thread to finish */
  if (thread)
    g_thread_join (thread);

}


/* This function calculates the pixel aspect ratio based on the properties
 * in the xcontext structure and stores it there. */
static void
gst_xvimagesink_calculate_pixel_aspect_ratio (GstXContext * xcontext)
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
gst_xvimagesink_xcontext_get (GstXvImageSink * xvimagesink)
{
  GstXContext *xcontext = NULL;
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i, j, N_attr;
  XvAttribute *xv_attr;
  Atom prop_atom;
  const char *channels[4] = { "XV_HUE", "XV_SATURATION",
    "XV_BRIGHTNESS", "XV_CONTRAST"
  };

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);

  xcontext = g_new0 (GstXContext, 1);
  xcontext->im_format = 0;

  g_mutex_lock (xvimagesink->x_lock);

  xcontext->disp = XOpenDisplay (xvimagesink->display_name);

  if (!xcontext->disp) {
    g_mutex_unlock (xvimagesink->x_lock);
    g_free (xcontext);
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
        ("Could not initialise Xv output"), ("Could not open display"));
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

  GST_DEBUG_OBJECT (xvimagesink, "X reports %dx%d pixels and %d mm x %d mm",
      xcontext->width, xcontext->height, xcontext->widthmm, xcontext->heightmm);

  gst_xvimagesink_calculate_pixel_aspect_ratio (xcontext);
  /* We get supported pixmap formats at supported depth */
  px_formats = XListPixmapFormats (xcontext->disp, &nb_formats);

  if (!px_formats) {
    XCloseDisplay (xcontext->disp);
    g_mutex_unlock (xvimagesink->x_lock);
    g_free (xcontext->par);
    g_free (xcontext);
    GST_ELEMENT_ERROR (xvimagesink, RESOURCE, SETTINGS,
        ("Could not initialise Xv output"), ("Could not get pixel formats"));
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

  xcontext->caps = gst_xvimagesink_get_xv_support (xvimagesink, xcontext);

  if (!xcontext->caps) {
    XCloseDisplay (xcontext->disp);
    g_mutex_unlock (xvimagesink->x_lock);
    g_free (xcontext->par);
    g_free (xcontext);
    /* GST_ELEMENT_ERROR is thrown by gst_xvimagesink_get_xv_support */
    return NULL;
  }
#ifdef HAVE_XSHM
  /* Search for XShm extension support */
  if (XShmQueryExtension (xcontext->disp) &&
      gst_xvimagesink_check_xshm_calls (xcontext)) {
    xcontext->use_xshm = TRUE;
    GST_DEBUG ("xvimagesink is using XShm extension");
  } else
#endif /* HAVE_XSHM */
  {
    xcontext->use_xshm = FALSE;
    GST_DEBUG ("xvimagesink is not using XShm extension");
  }

  xv_attr = XvQueryPortAttributes (xcontext->disp,
      xcontext->xv_port_id, &N_attr);


  /* Generate the channels list */
  for (i = 0; i < (sizeof (channels) / sizeof (char *)); i++) {
    XvAttribute *matching_attr = NULL;

    /* Retrieve the property atom if it exists. If it doesn't exist, 
     * the attribute itself must not either, so we can skip */
    prop_atom = XInternAtom (xcontext->disp, channels[i], True);
    if (prop_atom == None)
      continue;

    if (xv_attr != NULL) {
      for (j = 0; j < N_attr && matching_attr == NULL; ++j)
        if (!g_ascii_strcasecmp (channels[i], xv_attr[j].name))
          matching_attr = xv_attr + j;
    }

    if (matching_attr) {
      GstColorBalanceChannel *channel;

      channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
      channel->label = g_strdup (channels[i]);
      channel->min_value = matching_attr ? matching_attr->min_value : -1000;
      channel->max_value = matching_attr ? matching_attr->max_value : 1000;

      xcontext->channels_list = g_list_append (xcontext->channels_list,
          channel);

      /* If the colorbalance settings have not been touched we get Xv values
         as defaults and update our internal variables */
      if (!xvimagesink->cb_changed) {
        gint val;

        XvGetPortAttribute (xcontext->disp, xcontext->xv_port_id,
            prop_atom, &val);
        /* Normalize val to [-1000, 1000] */
        val = floor (0.5 + -1000 + 2000 * (val - channel->min_value) /
            (double) (channel->max_value - channel->min_value));

        if (!g_ascii_strcasecmp (channels[i], "XV_HUE"))
          xvimagesink->hue = val;
        else if (!g_ascii_strcasecmp (channels[i], "XV_SATURATION"))
          xvimagesink->saturation = val;
        else if (!g_ascii_strcasecmp (channels[i], "XV_BRIGHTNESS"))
          xvimagesink->brightness = val;
        else if (!g_ascii_strcasecmp (channels[i], "XV_CONTRAST"))
          xvimagesink->contrast = val;
      }
    }
  }

  if (xv_attr)
    XFree (xv_attr);

  g_mutex_unlock (xvimagesink->x_lock);

  return xcontext;
}

/* This function cleans the X context. Closing the Display, releasing the XV
   port and unrefing the caps for supported formats. */
static void
gst_xvimagesink_xcontext_clear (GstXvImageSink * xvimagesink)
{
  GList *formats_list, *channels_list;
  GstXContext *xcontext;
  gint i = 0;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  GST_OBJECT_LOCK (xvimagesink);
  if (xvimagesink->xcontext == NULL) {
    GST_OBJECT_UNLOCK (xvimagesink);
    return;
  }

  /* Take the XContext from the sink and clean it up */
  xcontext = xvimagesink->xcontext;
  xvimagesink->xcontext = NULL;

  GST_OBJECT_UNLOCK (xvimagesink);


  formats_list = xcontext->formats_list;

  while (formats_list) {
    GstXvImageFormat *format = formats_list->data;

    gst_caps_unref (format->caps);
    g_free (format);
    formats_list = g_list_next (formats_list);
  }

  if (xcontext->formats_list)
    g_list_free (xcontext->formats_list);

  channels_list = xcontext->channels_list;

  while (channels_list) {
    GstColorBalanceChannel *channel = channels_list->data;

    g_object_unref (channel);
    channels_list = g_list_next (channels_list);
  }

  if (xcontext->channels_list)
    g_list_free (xcontext->channels_list);

  gst_caps_unref (xcontext->caps);
  if (xcontext->last_caps)
    gst_caps_replace (&xcontext->last_caps, NULL);

  for (i = 0; i < xcontext->nb_adaptors; i++) {
    g_free (xcontext->adaptors[i]);
  }

  g_free (xcontext->adaptors);

  g_free (xcontext->par);

  g_mutex_lock (xvimagesink->x_lock);

  GST_DEBUG_OBJECT (xvimagesink, "Closing display and freeing X Context");

  XvUngrabPort (xcontext->disp, xcontext->xv_port_id, 0);

  XCloseDisplay (xcontext->disp);

  g_mutex_unlock (xvimagesink->x_lock);

  g_free (xcontext);
}

static void
gst_xvimagesink_imagepool_clear (GstXvImageSink * xvimagesink)
{
  g_mutex_lock (xvimagesink->pool_lock);

  while (xvimagesink->image_pool) {
    GstXvImageBuffer *xvimage = xvimagesink->image_pool->data;

    xvimagesink->image_pool = g_slist_delete_link (xvimagesink->image_pool,
        xvimagesink->image_pool);
    gst_xvimage_buffer_free (xvimage);
  }

  g_mutex_unlock (xvimagesink->pool_lock);
}

/* Element stuff */

/* This function tries to get a format matching with a given caps in the
   supported list of formats we generated in gst_xvimagesink_get_xv_support */
static gint
gst_xvimagesink_get_format_from_caps (GstXvImageSink * xvimagesink,
    GstCaps * caps)
{
  GList *list = NULL;

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), 0);

  list = xvimagesink->xcontext->formats_list;

  while (list) {
    GstXvImageFormat *format = list->data;

    if (format) {
      if (gst_caps_can_intersect (caps, format->caps)) {
        return format->format;
      }
    }
    list = g_list_next (list);
  }

  return -1;
}

static GstCaps *
gst_xvimagesink_getcaps (GstBaseSink * bsink)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (bsink);

  if (xvimagesink->xcontext)
    return gst_caps_ref (xvimagesink->xcontext->caps);

  return
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD
          (xvimagesink)));
}

static gboolean
gst_xvimagesink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstXvImageSink *xvimagesink;
  GstStructure *structure;
  guint32 im_format = 0;
  gboolean ret;
  gint video_width, video_height;
  gint disp_x, disp_y;
  gint disp_width, disp_height;
  gint video_par_n, video_par_d;        /* video's PAR */
  gint display_par_n, display_par_d;    /* display's PAR */
  const GValue *caps_par;
  const GValue *caps_disp_reg;
  const GValue *fps;
  guint num, den;

  xvimagesink = GST_XVIMAGESINK (bsink);

  GST_DEBUG_OBJECT (xvimagesink,
      "In setcaps. Possible caps %" GST_PTR_FORMAT ", setting caps %"
      GST_PTR_FORMAT, xvimagesink->xcontext->caps, caps);

  if (!gst_caps_intersect (xvimagesink->xcontext->caps, caps))
    goto incompatible_caps;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &video_width);
  ret &= gst_structure_get_int (structure, "height", &video_height);
  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);

  if (!ret)
    goto incomplete_caps;

  xvimagesink->fps_n = gst_value_get_fraction_numerator (fps);
  xvimagesink->fps_d = gst_value_get_fraction_denominator (fps);

  xvimagesink->video_width = video_width;
  xvimagesink->video_height = video_height;

  im_format = gst_xvimagesink_get_format_from_caps (xvimagesink, caps);
  if (im_format == -1)
    goto invalid_format;

  /* get aspect ratio from caps if it's present, and
   * convert video width and height to a display width and height
   * using wd / hd = wv / hv * PARv / PARd */

  /* get video's PAR */
  caps_par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (caps_par) {
    video_par_n = gst_value_get_fraction_numerator (caps_par);
    video_par_d = gst_value_get_fraction_denominator (caps_par);
  } else {
    video_par_n = 1;
    video_par_d = 1;
  }
  /* get display's PAR */
  if (xvimagesink->par) {
    display_par_n = gst_value_get_fraction_numerator (xvimagesink->par);
    display_par_d = gst_value_get_fraction_denominator (xvimagesink->par);
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }

  /* get the display region */
  caps_disp_reg = gst_structure_get_value (structure, "display-region");
  if (caps_disp_reg) {
    disp_x = g_value_get_int (gst_value_array_get_value (caps_disp_reg, 0));
    disp_y = g_value_get_int (gst_value_array_get_value (caps_disp_reg, 1));
    disp_width = g_value_get_int (gst_value_array_get_value (caps_disp_reg, 2));
    disp_height =
        g_value_get_int (gst_value_array_get_value (caps_disp_reg, 3));
  } else {
    disp_x = disp_y = 0;
    disp_width = video_width;
    disp_height = video_height;
  }

  if (!gst_video_calculate_display_ratio (&num, &den, video_width,
          video_height, video_par_n, video_par_d, display_par_n, display_par_d))
    goto no_disp_ratio;

  xvimagesink->disp_x = disp_x;
  xvimagesink->disp_y = disp_y;
  xvimagesink->disp_width = disp_width;
  xvimagesink->disp_height = disp_height;

  GST_DEBUG_OBJECT (xvimagesink,
      "video width/height: %dx%d, calculated display ratio: %d/%d",
      video_width, video_height, num, den);

  /* now find a width x height that respects this display ratio.
   * prefer those that have one of w/h the same as the incoming video
   * using wd / hd = num / den */

  /* start with same height, because of interlaced video */
  /* check hd / den is an integer scale factor, and scale wd with the PAR */
  if (video_height % den == 0) {
    GST_DEBUG_OBJECT (xvimagesink, "keeping video height");
    GST_VIDEO_SINK_WIDTH (xvimagesink) = (guint)
        gst_util_uint64_scale_int (video_height, num, den);
    GST_VIDEO_SINK_HEIGHT (xvimagesink) = video_height;
  } else if (video_width % num == 0) {
    GST_DEBUG_OBJECT (xvimagesink, "keeping video width");
    GST_VIDEO_SINK_WIDTH (xvimagesink) = video_width;
    GST_VIDEO_SINK_HEIGHT (xvimagesink) = (guint)
        gst_util_uint64_scale_int (video_width, den, num);
  } else {
    GST_DEBUG_OBJECT (xvimagesink, "approximating while keeping video height");
    GST_VIDEO_SINK_WIDTH (xvimagesink) = (guint)
        gst_util_uint64_scale_int (video_height, num, den);
    GST_VIDEO_SINK_HEIGHT (xvimagesink) = video_height;
  }
  GST_DEBUG_OBJECT (xvimagesink, "scaling to %dx%d",
      GST_VIDEO_SINK_WIDTH (xvimagesink), GST_VIDEO_SINK_HEIGHT (xvimagesink));

  /* Notify application to set xwindow id now */
  g_mutex_lock (xvimagesink->flow_lock);
  if (!xvimagesink->xwindow) {
    g_mutex_unlock (xvimagesink->flow_lock);
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (xvimagesink));
  } else {
    g_mutex_unlock (xvimagesink->flow_lock);
  }

  /* Creating our window and our image with the display size in pixels */
  if (GST_VIDEO_SINK_WIDTH (xvimagesink) <= 0 ||
      GST_VIDEO_SINK_HEIGHT (xvimagesink) <= 0)
    goto no_display_size;

  g_mutex_lock (xvimagesink->flow_lock);
  if (!xvimagesink->xwindow) {
    xvimagesink->xwindow = gst_xvimagesink_xwindow_new (xvimagesink,
        GST_VIDEO_SINK_WIDTH (xvimagesink),
        GST_VIDEO_SINK_HEIGHT (xvimagesink));
  }

  /* After a resize, we want to redraw the borders in case the new frame size 
   * doesn't cover the same area */
  xvimagesink->redraw_border = TRUE;

  /* We renew our xvimage only if size or format changed;
   * the xvimage is the same size as the video pixel size */
  if ((xvimagesink->xvimage) &&
      ((im_format != xvimagesink->xvimage->im_format) ||
          (video_width != xvimagesink->xvimage->width) ||
          (video_height != xvimagesink->xvimage->height))) {
    GST_DEBUG_OBJECT (xvimagesink,
        "old format %" GST_FOURCC_FORMAT ", new format %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (xvimagesink->xvimage->im_format),
        GST_FOURCC_ARGS (im_format));
    GST_DEBUG_OBJECT (xvimagesink, "renewing xvimage");
    gst_buffer_unref (GST_BUFFER (xvimagesink->xvimage));
    xvimagesink->xvimage = NULL;
  }

  g_mutex_unlock (xvimagesink->flow_lock);

  return TRUE;

  /* ERRORS */
incompatible_caps:
  {
    GST_ERROR_OBJECT (xvimagesink, "caps incompatible");
    return FALSE;
  }
incomplete_caps:
  {
    GST_DEBUG_OBJECT (xvimagesink, "Failed to retrieve either width, "
        "height or framerate from intersected caps");
    return FALSE;
  }
invalid_format:
  {
    GST_DEBUG_OBJECT (xvimagesink,
        "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
no_disp_ratio:
  {
    GST_ELEMENT_ERROR (xvimagesink, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }
no_display_size:
  {
    GST_ELEMENT_ERROR (xvimagesink, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output display ratio of the video."));
    return FALSE;
  }
}

static GstStateChangeReturn
gst_xvimagesink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstXvImageSink *xvimagesink;
  GstXContext *xcontext = NULL;

  xvimagesink = GST_XVIMAGESINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* Initializing the XContext */
      if (xvimagesink->xcontext == NULL) {
        xcontext = gst_xvimagesink_xcontext_get (xvimagesink);
        if (xcontext == NULL)
          return GST_STATE_CHANGE_FAILURE;
        GST_OBJECT_LOCK (xvimagesink);
        if (xcontext)
          xvimagesink->xcontext = xcontext;
        GST_OBJECT_UNLOCK (xvimagesink);
      }

      /* update object's par with calculated one if not set yet */
      if (!xvimagesink->par) {
        xvimagesink->par = g_new0 (GValue, 1);
        gst_value_init_and_copy (xvimagesink->par, xvimagesink->xcontext->par);
        GST_DEBUG_OBJECT (xvimagesink, "set calculated PAR on object's PAR");
      }
      /* call XSynchronize with the current value of synchronous */
      GST_DEBUG_OBJECT (xvimagesink, "XSynchronize called with %s",
          xvimagesink->synchronous ? "TRUE" : "FALSE");
      XSynchronize (xvimagesink->xcontext->disp, xvimagesink->synchronous);
      gst_xvimagesink_update_colorbalance (xvimagesink);
      gst_xvimagesink_manage_event_thread (xvimagesink);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (xvimagesink->pool_lock);
      xvimagesink->pool_invalid = FALSE;
      g_mutex_unlock (xvimagesink->pool_lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (xvimagesink->pool_lock);
      xvimagesink->pool_invalid = TRUE;
      g_mutex_unlock (xvimagesink->pool_lock);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      xvimagesink->fps_n = 0;
      xvimagesink->fps_d = 1;
      GST_VIDEO_SINK_WIDTH (xvimagesink) = 0;
      GST_VIDEO_SINK_HEIGHT (xvimagesink) = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_xvimagesink_reset (xvimagesink);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_xvimagesink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (xvimagesink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, xvimagesink->fps_d,
            xvimagesink->fps_n);
      }
    }
  }
}

static GstFlowReturn
gst_xvimagesink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (vsink);

  /* If this buffer has been allocated using our buffer management we simply
     put the ximage which is in the PRIVATE pointer */
  if (GST_IS_XVIMAGE_BUFFER (buf)) {
    GST_LOG_OBJECT (xvimagesink, "fast put of bufferpool buffer %p", buf);
    if (!gst_xvimagesink_xvimage_put (xvimagesink,
            GST_XVIMAGE_BUFFER_CAST (buf)))
      goto no_window;
  } else {
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, xvimagesink,
        "slow copy into bufferpool buffer %p", buf);
    /* Else we have to copy the data into our private image, */
    /* if we have one... */
    if (!xvimagesink->xvimage) {
      GST_DEBUG_OBJECT (xvimagesink, "creating our xvimage");

      xvimagesink->xvimage = gst_xvimagesink_xvimage_new (xvimagesink,
          GST_BUFFER_CAPS (buf));

      if (!xvimagesink->xvimage)
        /* The create method should have posted an informative error */
        goto no_image;

      if (xvimagesink->xvimage->size < GST_BUFFER_SIZE (buf)) {
        GST_ELEMENT_ERROR (xvimagesink, RESOURCE, WRITE,
            ("Failed to create output image buffer of %dx%d pixels",
                xvimagesink->xvimage->width, xvimagesink->xvimage->height),
            ("XServer allocated buffer size did not match input buffer"));

        gst_xvimage_buffer_destroy (xvimagesink->xvimage);
        xvimagesink->xvimage = NULL;
        goto no_image;
      }
    }

    memcpy (xvimagesink->xvimage->xvimage->data,
        GST_BUFFER_DATA (buf),
        MIN (GST_BUFFER_SIZE (buf), xvimagesink->xvimage->size));

    if (!gst_xvimagesink_xvimage_put (xvimagesink, xvimagesink->xvimage))
      goto no_window;
  }

  return GST_FLOW_OK;

  /* ERRORS */
no_image:
  {
    /* No image available. That's very bad ! */
    GST_WARNING_OBJECT (xvimagesink, "could not create image");
    return GST_FLOW_ERROR;
  }
no_window:
  {
    /* No Window available to put our image into */
    GST_WARNING_OBJECT (xvimagesink, "could not output image - no window");
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_xvimagesink_event (GstBaseSink * sink, GstEvent * event)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *l;
      gchar *title = NULL;

      gst_event_parse_tag (event, &l);
      gst_tag_list_get_string (l, GST_TAG_TITLE, &title);

      if (title) {
        GST_DEBUG_OBJECT (xvimagesink, "got tags, title='%s'", title);
        gst_xvimagesink_xwindow_set_title (xvimagesink, xvimagesink->xwindow,
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

/* Buffer management */

static GstFlowReturn
gst_xvimagesink_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstXvImageSink *xvimagesink;
  GstXvImageBuffer *xvimage = NULL;
  GstCaps *intersection = NULL;
  GstStructure *structure = NULL;
  gint width, height, image_format;
  GstCaps *new_caps;

  xvimagesink = GST_XVIMAGESINK (bsink);

  g_mutex_lock (xvimagesink->pool_lock);
  if (G_UNLIKELY (xvimagesink->pool_invalid))
    goto invalid;

  if (G_LIKELY (xvimagesink->xcontext->last_caps &&
          gst_caps_is_equal (caps, xvimagesink->xcontext->last_caps))) {
    GST_LOG_OBJECT (xvimagesink,
        "buffer alloc for same last_caps, reusing caps");
    intersection = gst_caps_ref (caps);
    image_format = xvimagesink->xcontext->last_format;
    width = xvimagesink->xcontext->last_width;
    height = xvimagesink->xcontext->last_height;

    goto reuse_last_caps;
  }

  GST_DEBUG_OBJECT (xvimagesink, "buffer alloc requested size %d with caps %"
      GST_PTR_FORMAT ", intersecting with our caps %" GST_PTR_FORMAT, size,
      caps, xvimagesink->xcontext->caps);

  /* Check the caps against our xcontext */
  intersection = gst_caps_intersect (xvimagesink->xcontext->caps, caps);

  /* Ensure the returned caps are fixed */
  gst_caps_truncate (intersection);

  GST_DEBUG_OBJECT (xvimagesink, "intersection in buffer alloc returned %"
      GST_PTR_FORMAT, intersection);

  if (gst_caps_is_empty (intersection)) {
    /* So we don't support this kind of buffer, let's define one we'd like */
    new_caps = gst_caps_copy (caps);

    structure = gst_caps_get_structure (new_caps, 0);

    /* Try with YUV first */
    gst_structure_set_name (structure, "video/x-raw-yuv");
    gst_structure_remove_field (structure, "format");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "bpp");
    gst_structure_remove_field (structure, "red_mask");
    gst_structure_remove_field (structure, "green_mask");
    gst_structure_remove_field (structure, "blue_mask");
    gst_structure_remove_field (structure, "alpha_mask");

    /* Reuse intersection with Xcontext */
    gst_caps_unref (intersection);
    intersection = gst_caps_intersect (xvimagesink->xcontext->caps, new_caps);

    if (gst_caps_is_empty (intersection)) {
      /* Now try with RGB */
      gst_structure_set_name (structure, "video/x-raw-rgb");
      /* And interset again */
      gst_caps_unref (intersection);
      intersection = gst_caps_intersect (xvimagesink->xcontext->caps, new_caps);

      if (gst_caps_is_empty (intersection))
        goto incompatible;
    }

    /* Clean this copy */
    gst_caps_unref (new_caps);
    /* We want fixed caps */
    gst_caps_truncate (intersection);

    GST_DEBUG_OBJECT (xvimagesink, "allocating a buffer with caps %"
        GST_PTR_FORMAT, intersection);
  } else if (gst_caps_is_equal (intersection, caps)) {
    /* Things work better if we return a buffer with the same caps ptr
     * as was asked for when we can */
    gst_caps_replace (&intersection, caps);
  }

  /* Get image format from caps */
  image_format = gst_xvimagesink_get_format_from_caps (xvimagesink,
      intersection);

  /* Get geometry from caps */
  structure = gst_caps_get_structure (intersection, 0);
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height) ||
      image_format == -1)
    goto invalid_caps;

  /* Store our caps and format as the last_caps to avoid expensive
   * caps intersection next time */
  gst_caps_replace (&xvimagesink->xcontext->last_caps, intersection);
  xvimagesink->xcontext->last_format = image_format;
  xvimagesink->xcontext->last_width = width;
  xvimagesink->xcontext->last_height = height;

reuse_last_caps:

  /* Walking through the pool cleaning unusable images and searching for a
     suitable one */
  while (xvimagesink->image_pool) {
    xvimage = xvimagesink->image_pool->data;

    if (xvimage) {
      /* Removing from the pool */
      xvimagesink->image_pool = g_slist_delete_link (xvimagesink->image_pool,
          xvimagesink->image_pool);

      /* We check for geometry or image format changes */
      if ((xvimage->width != width) ||
          (xvimage->height != height) || (xvimage->im_format != image_format)) {
        /* This image is unusable. Destroying... */
        gst_xvimage_buffer_free (xvimage);
        xvimage = NULL;
      } else {
        /* We found a suitable image */
        GST_LOG_OBJECT (xvimagesink, "found usable image in pool");
        break;
      }
    }
  }

  if (!xvimage) {
    /* We found no suitable image in the pool. Creating... */
    GST_DEBUG_OBJECT (xvimagesink, "no usable image in pool, creating xvimage");
    xvimage = gst_xvimagesink_xvimage_new (xvimagesink, intersection);
    if (xvimage && xvimage->size < size) {
      /* This image is unusable. Destroying... */
      GST_LOG_OBJECT (xvimagesink, "Discarding allocated buffer as unsuitable. "
          "Falling back to normal buffer");
      gst_xvimage_buffer_free (xvimage);
      xvimage = NULL;
    }
  }
  g_mutex_unlock (xvimagesink->pool_lock);

  if (xvimage) {
    /* Make sure the buffer is cleared of any previously used flags */
    GST_MINI_OBJECT_CAST (xvimage)->flags = 0;
    gst_buffer_set_caps (GST_BUFFER_CAST (xvimage), intersection);
  }

  *buf = GST_BUFFER_CAST (xvimage);

beach:
  if (intersection) {
    gst_caps_unref (intersection);
  }

  return ret;

  /* ERRORS */
invalid:
  {
    GST_DEBUG_OBJECT (xvimagesink, "the pool is flushing");
    ret = GST_FLOW_WRONG_STATE;
    g_mutex_unlock (xvimagesink->pool_lock);
    goto beach;
  }
incompatible:
  {
    GST_WARNING_OBJECT (xvimagesink, "we were requested a buffer with "
        "caps %" GST_PTR_FORMAT ", but our xcontext caps %" GST_PTR_FORMAT
        " are completely incompatible with those caps", new_caps,
        xvimagesink->xcontext->caps);
    gst_caps_unref (new_caps);
    ret = GST_FLOW_NOT_NEGOTIATED;
    g_mutex_unlock (xvimagesink->pool_lock);
    goto beach;
  }
invalid_caps:
  {
    GST_WARNING_OBJECT (xvimagesink, "invalid caps for buffer allocation %"
        GST_PTR_FORMAT, intersection);
    ret = GST_FLOW_NOT_NEGOTIATED;
    g_mutex_unlock (xvimagesink->pool_lock);
    goto beach;
  }
}

/* Interfaces stuff */

static gboolean
gst_xvimagesink_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION || type == GST_TYPE_X_OVERLAY ||
      type == GST_TYPE_COLOR_BALANCE || type == GST_TYPE_PROPERTY_PROBE);
  return TRUE;
}

static void
gst_xvimagesink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_xvimagesink_interface_supported;
}

static void
gst_xvimagesink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (navigation);
  GstPad *peer;

  if ((peer = gst_pad_get_peer (GST_VIDEO_SINK_PAD (xvimagesink)))) {
    GstEvent *event;
    GstVideoRectangle src, dst, result;
    gdouble x, y, xscale = 1.0, yscale = 1.0;

    event = gst_event_new_navigation (structure);

    /* We take the flow_lock while we look at the window */
    g_mutex_lock (xvimagesink->flow_lock);

    if (!xvimagesink->xwindow) {
      g_mutex_unlock (xvimagesink->flow_lock);
      return;
    }

    if (xvimagesink->keep_aspect) {
      /* We get the frame position using the calculated geometry from _setcaps
         that respect pixel aspect ratios */
      src.w = GST_VIDEO_SINK_WIDTH (xvimagesink);
      src.h = GST_VIDEO_SINK_HEIGHT (xvimagesink);
      dst.w = xvimagesink->render_rect.w;
      dst.h = xvimagesink->render_rect.h;

      gst_video_sink_center_rect (src, dst, &result, TRUE);
      result.x += xvimagesink->render_rect.x;
      result.y += xvimagesink->render_rect.y;
    } else {
      memcpy (&result, &xvimagesink->render_rect, sizeof (GstVideoRectangle));
    }

    g_mutex_unlock (xvimagesink->flow_lock);

    /* We calculate scaling using the original video frames geometry to include
       pixel aspect ratio scaling. */
    xscale = (gdouble) xvimagesink->video_width / result.w;
    yscale = (gdouble) xvimagesink->video_height / result.h;

    /* Converting pointer coordinates to the non scaled geometry */
    if (gst_structure_get_double (structure, "pointer_x", &x)) {
      x = MIN (x, result.x + result.w);
      x = MAX (x - result.x, 0);
      gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
          (gdouble) x * xscale, NULL);
    }
    if (gst_structure_get_double (structure, "pointer_y", &y)) {
      y = MIN (y, result.y + result.h);
      y = MAX (y - result.y, 0);
      gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
          (gdouble) y * yscale, NULL);
    }

    gst_pad_send_event (peer, event);
    gst_object_unref (peer);
  }
}

static void
gst_xvimagesink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_xvimagesink_navigation_send_event;
}

static void
gst_xvimagesink_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (overlay);
  GstXWindow *xwindow = NULL;

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));

  g_mutex_lock (xvimagesink->flow_lock);

  /* If we already use that window return */
  if (xvimagesink->xwindow && (xwindow_id == xvimagesink->xwindow->win)) {
    g_mutex_unlock (xvimagesink->flow_lock);
    return;
  }

  /* If the element has not initialized the X11 context try to do so */
  if (!xvimagesink->xcontext &&
      !(xvimagesink->xcontext = gst_xvimagesink_xcontext_get (xvimagesink))) {
    g_mutex_unlock (xvimagesink->flow_lock);
    /* we have thrown a GST_ELEMENT_ERROR now */
    return;
  }

  gst_xvimagesink_update_colorbalance (xvimagesink);

  /* Clear image pool as the images are unusable anyway */
  gst_xvimagesink_imagepool_clear (xvimagesink);

  /* Clear the xvimage */
  if (xvimagesink->xvimage) {
    gst_xvimage_buffer_free (xvimagesink->xvimage);
    xvimagesink->xvimage = NULL;
  }

  /* If a window is there already we destroy it */
  if (xvimagesink->xwindow) {
    gst_xvimagesink_xwindow_destroy (xvimagesink, xvimagesink->xwindow);
    xvimagesink->xwindow = NULL;
  }

  /* If the xid is 0 we go back to an internal window */
  if (xwindow_id == 0) {
    /* If no width/height caps nego did not happen window will be created
       during caps nego then */
    if (GST_VIDEO_SINK_WIDTH (xvimagesink)
        && GST_VIDEO_SINK_HEIGHT (xvimagesink)) {
      xwindow =
          gst_xvimagesink_xwindow_new (xvimagesink,
          GST_VIDEO_SINK_WIDTH (xvimagesink),
          GST_VIDEO_SINK_HEIGHT (xvimagesink));
    }
  } else {
    XWindowAttributes attr;

    xwindow = g_new0 (GstXWindow, 1);
    xwindow->win = xwindow_id;

    /* Set the event we want to receive and create a GC */
    g_mutex_lock (xvimagesink->x_lock);

    XGetWindowAttributes (xvimagesink->xcontext->disp, xwindow->win, &attr);

    xwindow->width = attr.width;
    xwindow->height = attr.height;
    xwindow->internal = FALSE;
    if (!xvimagesink->have_render_rect) {
      xvimagesink->render_rect.x = xvimagesink->render_rect.y = 0;
      xvimagesink->render_rect.w = attr.width;
      xvimagesink->render_rect.h = attr.height;
    }
    if (xvimagesink->handle_events) {
      XSelectInput (xvimagesink->xcontext->disp, xwindow->win, ExposureMask |
          StructureNotifyMask | PointerMotionMask | KeyPressMask |
          KeyReleaseMask);
    }

    xwindow->gc = XCreateGC (xvimagesink->xcontext->disp,
        xwindow->win, 0, NULL);
    g_mutex_unlock (xvimagesink->x_lock);
  }

  if (xwindow)
    xvimagesink->xwindow = xwindow;

  g_mutex_unlock (xvimagesink->flow_lock);
}

static void
gst_xvimagesink_expose (GstXOverlay * overlay)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (overlay);

  gst_xvimagesink_xwindow_update_geometry (xvimagesink);
  gst_xvimagesink_xvimage_put (xvimagesink, NULL);
}

static void
gst_xvimagesink_set_event_handling (GstXOverlay * overlay,
    gboolean handle_events)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (overlay);

  xvimagesink->handle_events = handle_events;

  g_mutex_lock (xvimagesink->flow_lock);

  if (G_UNLIKELY (!xvimagesink->xwindow)) {
    g_mutex_unlock (xvimagesink->flow_lock);
    return;
  }

  g_mutex_lock (xvimagesink->x_lock);

  if (handle_events) {
    if (xvimagesink->xwindow->internal) {
      XSelectInput (xvimagesink->xcontext->disp, xvimagesink->xwindow->win,
          ExposureMask | StructureNotifyMask | PointerMotionMask |
          KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
    } else {
      XSelectInput (xvimagesink->xcontext->disp, xvimagesink->xwindow->win,
          ExposureMask | StructureNotifyMask | PointerMotionMask |
          KeyPressMask | KeyReleaseMask);
    }
  } else {
    XSelectInput (xvimagesink->xcontext->disp, xvimagesink->xwindow->win, 0);
  }

  g_mutex_unlock (xvimagesink->x_lock);

  g_mutex_unlock (xvimagesink->flow_lock);
}

static void
gst_xvimagesink_set_render_rectangle (GstXOverlay * overlay, gint x, gint y,
    gint width, gint height)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (overlay);

  /* FIXME: how about some locking? */
  if (x >= 0 && y >= 0 && width >= 0 && height >= 0) {
    xvimagesink->render_rect.x = x;
    xvimagesink->render_rect.y = y;
    xvimagesink->render_rect.w = width;
    xvimagesink->render_rect.h = height;
    xvimagesink->have_render_rect = TRUE;
  } else {
    xvimagesink->render_rect.x = 0;
    xvimagesink->render_rect.y = 0;
    xvimagesink->render_rect.w = xvimagesink->xwindow->width;
    xvimagesink->render_rect.h = xvimagesink->xwindow->height;
    xvimagesink->have_render_rect = FALSE;
  }
}

static void
gst_xvimagesink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_xvimagesink_set_xwindow_id;
  iface->expose = gst_xvimagesink_expose;
  iface->handle_events = gst_xvimagesink_set_event_handling;
  iface->set_render_rectangle = gst_xvimagesink_set_render_rectangle;
}

static const GList *
gst_xvimagesink_colorbalance_list_channels (GstColorBalance * balance)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (balance);

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), NULL);

  if (xvimagesink->xcontext)
    return xvimagesink->xcontext->channels_list;
  else
    return NULL;
}

static void
gst_xvimagesink_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (balance);

  g_return_if_fail (GST_IS_XVIMAGESINK (xvimagesink));
  g_return_if_fail (channel->label != NULL);

  xvimagesink->cb_changed = TRUE;

  /* Normalize val to [-1000, 1000] */
  value = floor (0.5 + -1000 + 2000 * (value - channel->min_value) /
      (double) (channel->max_value - channel->min_value));

  if (g_ascii_strcasecmp (channel->label, "XV_HUE") == 0) {
    xvimagesink->hue = value;
  } else if (g_ascii_strcasecmp (channel->label, "XV_SATURATION") == 0) {
    xvimagesink->saturation = value;
  } else if (g_ascii_strcasecmp (channel->label, "XV_CONTRAST") == 0) {
    xvimagesink->contrast = value;
  } else if (g_ascii_strcasecmp (channel->label, "XV_BRIGHTNESS") == 0) {
    xvimagesink->brightness = value;
  } else {
    g_warning ("got an unknown channel %s", channel->label);
    return;
  }

  gst_xvimagesink_update_colorbalance (xvimagesink);
}

static gint
gst_xvimagesink_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (balance);
  gint value = 0;

  g_return_val_if_fail (GST_IS_XVIMAGESINK (xvimagesink), 0);
  g_return_val_if_fail (channel->label != NULL, 0);

  if (g_ascii_strcasecmp (channel->label, "XV_HUE") == 0) {
    value = xvimagesink->hue;
  } else if (g_ascii_strcasecmp (channel->label, "XV_SATURATION") == 0) {
    value = xvimagesink->saturation;
  } else if (g_ascii_strcasecmp (channel->label, "XV_CONTRAST") == 0) {
    value = xvimagesink->contrast;
  } else if (g_ascii_strcasecmp (channel->label, "XV_BRIGHTNESS") == 0) {
    value = xvimagesink->brightness;
  } else {
    g_warning ("got an unknown channel %s", channel->label);
  }

  /* Normalize val to [channel->min_value, channel->max_value] */
  value = channel->min_value + (channel->max_value - channel->min_value) *
      (value + 1000) / 2000;

  return value;
}

static void
gst_xvimagesink_colorbalance_init (GstColorBalanceClass * iface)
{
  GST_COLOR_BALANCE_TYPE (iface) = GST_COLOR_BALANCE_HARDWARE;
  iface->list_channels = gst_xvimagesink_colorbalance_list_channels;
  iface->set_value = gst_xvimagesink_colorbalance_set_value;
  iface->get_value = gst_xvimagesink_colorbalance_get_value;
}

static const GList *
gst_xvimagesink_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *list = NULL;

  if (!list) {
    list = g_list_append (NULL, g_object_class_find_property (klass, "device"));
    list =
        g_list_append (list, g_object_class_find_property (klass,
            "autopaint-colorkey"));
    list =
        g_list_append (list, g_object_class_find_property (klass,
            "double-buffer"));
    list =
        g_list_append (list, g_object_class_find_property (klass, "colorkey"));
  }

  return list;
}

static void
gst_xvimagesink_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (probe);

  switch (prop_id) {
    case ARG_DEVICE:
    case ARG_AUTOPAINT_COLORKEY:
    case ARG_DOUBLE_BUFFER:
    case ARG_COLORKEY:
      GST_DEBUG_OBJECT (xvimagesink,
          "probing device list and get capabilities");
      if (!xvimagesink->xcontext) {
        GST_DEBUG_OBJECT (xvimagesink, "generating xcontext");
        xvimagesink->xcontext = gst_xvimagesink_xcontext_get (xvimagesink);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static gboolean
gst_xvimagesink_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (probe);
  gboolean ret = FALSE;

  switch (prop_id) {
    case ARG_DEVICE:
    case ARG_AUTOPAINT_COLORKEY:
    case ARG_DOUBLE_BUFFER:
    case ARG_COLORKEY:
      if (xvimagesink->xcontext != NULL) {
        ret = FALSE;
      } else {
        ret = TRUE;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return ret;
}

static GValueArray *
gst_xvimagesink_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstXvImageSink *xvimagesink = GST_XVIMAGESINK (probe);
  GValueArray *array = NULL;

  if (G_UNLIKELY (!xvimagesink->xcontext)) {
    GST_WARNING_OBJECT (xvimagesink, "we don't have any xcontext, can't "
        "get values");
    goto beach;
  }

  switch (prop_id) {
    case ARG_DEVICE:
    {
      guint i;
      GValue value = { 0 };

      array = g_value_array_new (xvimagesink->xcontext->nb_adaptors);
      g_value_init (&value, G_TYPE_STRING);

      for (i = 0; i < xvimagesink->xcontext->nb_adaptors; i++) {
        gchar *adaptor_id_s = g_strdup_printf ("%u", i);

        g_value_set_string (&value, adaptor_id_s);
        g_value_array_append (array, &value);
        g_free (adaptor_id_s);
      }
      g_value_unset (&value);
      break;
    }
    case ARG_AUTOPAINT_COLORKEY:
      if (xvimagesink->have_autopaint_colorkey) {
        GValue value = { 0 };

        array = g_value_array_new (2);
        g_value_init (&value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&value, FALSE);
        g_value_array_append (array, &value);
        g_value_set_boolean (&value, TRUE);
        g_value_array_append (array, &value);
        g_value_unset (&value);
      }
      break;
    case ARG_DOUBLE_BUFFER:
      if (xvimagesink->have_double_buffer) {
        GValue value = { 0 };

        array = g_value_array_new (2);
        g_value_init (&value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&value, FALSE);
        g_value_array_append (array, &value);
        g_value_set_boolean (&value, TRUE);
        g_value_array_append (array, &value);
        g_value_unset (&value);
      }
      break;
    case ARG_COLORKEY:
      if (xvimagesink->have_colorkey) {
        GValue value = { 0 };

        array = g_value_array_new (1);
        g_value_init (&value, GST_TYPE_INT_RANGE);
        gst_value_set_int_range (&value, 0, 0xffffff);
        g_value_array_append (array, &value);
        g_value_unset (&value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

beach:
  return array;
}

static void
gst_xvimagesink_property_probe_interface_init (GstPropertyProbeInterface *
    iface)
{
  iface->get_properties = gst_xvimagesink_probe_get_properties;
  iface->probe_property = gst_xvimagesink_probe_probe_property;
  iface->needs_probe = gst_xvimagesink_probe_needs_probe;
  iface->get_values = gst_xvimagesink_probe_get_values;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_xvimagesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstXvImageSink *xvimagesink;

  g_return_if_fail (GST_IS_XVIMAGESINK (object));

  xvimagesink = GST_XVIMAGESINK (object);

  switch (prop_id) {
    case ARG_HUE:
      xvimagesink->hue = g_value_get_int (value);
      xvimagesink->cb_changed = TRUE;
      gst_xvimagesink_update_colorbalance (xvimagesink);
      break;
    case ARG_CONTRAST:
      xvimagesink->contrast = g_value_get_int (value);
      xvimagesink->cb_changed = TRUE;
      gst_xvimagesink_update_colorbalance (xvimagesink);
      break;
    case ARG_BRIGHTNESS:
      xvimagesink->brightness = g_value_get_int (value);
      xvimagesink->cb_changed = TRUE;
      gst_xvimagesink_update_colorbalance (xvimagesink);
      break;
    case ARG_SATURATION:
      xvimagesink->saturation = g_value_get_int (value);
      xvimagesink->cb_changed = TRUE;
      gst_xvimagesink_update_colorbalance (xvimagesink);
      break;
    case ARG_DISPLAY:
      xvimagesink->display_name = g_strdup (g_value_get_string (value));
      break;
    case ARG_SYNCHRONOUS:
      xvimagesink->synchronous = g_value_get_boolean (value);
      if (xvimagesink->xcontext) {
        XSynchronize (xvimagesink->xcontext->disp, xvimagesink->synchronous);
        GST_DEBUG_OBJECT (xvimagesink, "XSynchronize called with %s",
            xvimagesink->synchronous ? "TRUE" : "FALSE");
      }
      break;
    case ARG_PIXEL_ASPECT_RATIO:
      g_free (xvimagesink->par);
      xvimagesink->par = g_new0 (GValue, 1);
      g_value_init (xvimagesink->par, GST_TYPE_FRACTION);
      if (!g_value_transform (value, xvimagesink->par)) {
        g_warning ("Could not transform string to aspect ratio");
        gst_value_set_fraction (xvimagesink->par, 1, 1);
      }
      GST_DEBUG_OBJECT (xvimagesink, "set PAR to %d/%d",
          gst_value_get_fraction_numerator (xvimagesink->par),
          gst_value_get_fraction_denominator (xvimagesink->par));
      break;
    case ARG_FORCE_ASPECT_RATIO:
      xvimagesink->keep_aspect = g_value_get_boolean (value);
      break;
    case ARG_HANDLE_EVENTS:
      gst_xvimagesink_set_event_handling (GST_X_OVERLAY (xvimagesink),
          g_value_get_boolean (value));
      gst_xvimagesink_manage_event_thread (xvimagesink);
      break;
    case ARG_DEVICE:
      xvimagesink->adaptor_no = atoi (g_value_get_string (value));
      break;
    case ARG_HANDLE_EXPOSE:
      xvimagesink->handle_expose = g_value_get_boolean (value);
      gst_xvimagesink_manage_event_thread (xvimagesink);
      break;
    case ARG_DOUBLE_BUFFER:
      xvimagesink->double_buffer = g_value_get_boolean (value);
      break;
    case ARG_AUTOPAINT_COLORKEY:
      xvimagesink->autopaint_colorkey = g_value_get_boolean (value);
      break;
    case ARG_COLORKEY:
      xvimagesink->colorkey = g_value_get_int (value);
      break;
    case ARG_DRAW_BORDERS:
      xvimagesink->draw_borders = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_xvimagesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstXvImageSink *xvimagesink;

  g_return_if_fail (GST_IS_XVIMAGESINK (object));

  xvimagesink = GST_XVIMAGESINK (object);

  switch (prop_id) {
    case ARG_HUE:
      g_value_set_int (value, xvimagesink->hue);
      break;
    case ARG_CONTRAST:
      g_value_set_int (value, xvimagesink->contrast);
      break;
    case ARG_BRIGHTNESS:
      g_value_set_int (value, xvimagesink->brightness);
      break;
    case ARG_SATURATION:
      g_value_set_int (value, xvimagesink->saturation);
      break;
    case ARG_DISPLAY:
      g_value_set_string (value, xvimagesink->display_name);
      break;
    case ARG_SYNCHRONOUS:
      g_value_set_boolean (value, xvimagesink->synchronous);
      break;
    case ARG_PIXEL_ASPECT_RATIO:
      if (xvimagesink->par)
        g_value_transform (xvimagesink->par, value);
      break;
    case ARG_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, xvimagesink->keep_aspect);
      break;
    case ARG_HANDLE_EVENTS:
      g_value_set_boolean (value, xvimagesink->handle_events);
      break;
    case ARG_DEVICE:
    {
      char *adaptor_no_s = g_strdup_printf ("%u", xvimagesink->adaptor_no);

      g_value_set_string (value, adaptor_no_s);
      g_free (adaptor_no_s);
      break;
    }
    case ARG_DEVICE_NAME:
      if (xvimagesink->xcontext && xvimagesink->xcontext->adaptors) {
        g_value_set_string (value,
            xvimagesink->xcontext->adaptors[xvimagesink->adaptor_no]);
      } else {
        g_value_set_string (value, NULL);
      }
      break;
    case ARG_HANDLE_EXPOSE:
      g_value_set_boolean (value, xvimagesink->handle_expose);
      break;
    case ARG_DOUBLE_BUFFER:
      g_value_set_boolean (value, xvimagesink->double_buffer);
      break;
    case ARG_AUTOPAINT_COLORKEY:
      g_value_set_boolean (value, xvimagesink->autopaint_colorkey);
      break;
    case ARG_COLORKEY:
      g_value_set_int (value, xvimagesink->colorkey);
      break;
    case ARG_DRAW_BORDERS:
      g_value_set_boolean (value, xvimagesink->draw_borders);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_xvimagesink_reset (GstXvImageSink * xvimagesink)
{
  GThread *thread;

  GST_OBJECT_LOCK (xvimagesink);
  xvimagesink->running = FALSE;
  /* grab thread and mark it as NULL */
  thread = xvimagesink->event_thread;
  xvimagesink->event_thread = NULL;
  GST_OBJECT_UNLOCK (xvimagesink);

  /* invalidate the pool, current allocations continue, new buffer_alloc fails
   * with wrong_state */
  g_mutex_lock (xvimagesink->pool_lock);
  xvimagesink->pool_invalid = TRUE;
  g_mutex_unlock (xvimagesink->pool_lock);

  /* Wait for our event thread to finish before we clean up our stuff. */
  if (thread)
    g_thread_join (thread);

  if (xvimagesink->cur_image) {
    gst_buffer_unref (GST_BUFFER_CAST (xvimagesink->cur_image));
    xvimagesink->cur_image = NULL;
  }
  if (xvimagesink->xvimage) {
    gst_buffer_unref (GST_BUFFER_CAST (xvimagesink->xvimage));
    xvimagesink->xvimage = NULL;
  }

  gst_xvimagesink_imagepool_clear (xvimagesink);

  if (xvimagesink->xwindow) {
    gst_xvimagesink_xwindow_clear (xvimagesink, xvimagesink->xwindow);
    gst_xvimagesink_xwindow_destroy (xvimagesink, xvimagesink->xwindow);
    xvimagesink->xwindow = NULL;
  }

  xvimagesink->render_rect.x = xvimagesink->render_rect.y =
      xvimagesink->render_rect.w = xvimagesink->render_rect.h = 0;
  xvimagesink->have_render_rect = FALSE;

  gst_xvimagesink_xcontext_clear (xvimagesink);
}

/* Finalize is called only once, dispose can be called multiple times.
 * We use mutexes and don't reset stuff to NULL here so let's register
 * as a finalize. */
static void
gst_xvimagesink_finalize (GObject * object)
{
  GstXvImageSink *xvimagesink;

  xvimagesink = GST_XVIMAGESINK (object);

  gst_xvimagesink_reset (xvimagesink);

  if (xvimagesink->display_name) {
    g_free (xvimagesink->display_name);
    xvimagesink->display_name = NULL;
  }

  if (xvimagesink->par) {
    g_free (xvimagesink->par);
    xvimagesink->par = NULL;
  }
  if (xvimagesink->x_lock) {
    g_mutex_free (xvimagesink->x_lock);
    xvimagesink->x_lock = NULL;
  }
  if (xvimagesink->flow_lock) {
    g_mutex_free (xvimagesink->flow_lock);
    xvimagesink->flow_lock = NULL;
  }
  if (xvimagesink->pool_lock) {
    g_mutex_free (xvimagesink->pool_lock);
    xvimagesink->pool_lock = NULL;
  }

  g_free (xvimagesink->media_title);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_xvimagesink_init (GstXvImageSink * xvimagesink)
{
  xvimagesink->display_name = NULL;
  xvimagesink->adaptor_no = 0;
  xvimagesink->xcontext = NULL;
  xvimagesink->xwindow = NULL;
  xvimagesink->xvimage = NULL;
  xvimagesink->cur_image = NULL;

  xvimagesink->hue = xvimagesink->saturation = 0;
  xvimagesink->contrast = xvimagesink->brightness = 0;
  xvimagesink->cb_changed = FALSE;

  xvimagesink->fps_n = 0;
  xvimagesink->fps_d = 0;
  xvimagesink->video_width = 0;
  xvimagesink->video_height = 0;

  xvimagesink->x_lock = g_mutex_new ();
  xvimagesink->flow_lock = g_mutex_new ();

  xvimagesink->image_pool = NULL;
  xvimagesink->pool_lock = g_mutex_new ();

  xvimagesink->synchronous = FALSE;
  xvimagesink->double_buffer = TRUE;
  xvimagesink->running = FALSE;
  xvimagesink->keep_aspect = FALSE;
  xvimagesink->handle_events = TRUE;
  xvimagesink->par = NULL;
  xvimagesink->handle_expose = TRUE;
  xvimagesink->autopaint_colorkey = TRUE;

  /* on 16bit displays this becomes r,g,b = 1,2,3
   * on 24bit displays this becomes r,g,b = 8,8,16
   * as a port atom value
   */
  xvimagesink->colorkey = (8 << 16) | (8 << 8) | 16;
  xvimagesink->draw_borders = TRUE;
}

static void
gst_xvimagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Video sink", "Sink/Video",
      "A Xv based videosink", "Julien Moutte <julien@moutte.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_xvimagesink_sink_template_factory));
}

static void
gst_xvimagesink_class_init (GstXvImageSinkClass * klass)
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

  gobject_class->set_property = gst_xvimagesink_set_property;
  gobject_class->get_property = gst_xvimagesink_get_property;

  g_object_class_install_property (gobject_class, ARG_CONTRAST,
      g_param_spec_int ("contrast", "Contrast", "The contrast of the video",
          -1000, 1000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_BRIGHTNESS,
      g_param_spec_int ("brightness", "Brightness",
          "The brightness of the video", -1000, 1000, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_HUE,
      g_param_spec_int ("hue", "Hue", "The hue of the video", -1000, 1000, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_SATURATION,
      g_param_spec_int ("saturation", "Saturation",
          "The saturation of the video", -1000, 1000, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_SYNCHRONOUS,
      g_param_spec_boolean ("synchronous", "Synchronous",
          "When enabled, runs "
          "the X display in synchronous mode. (used only for debugging)", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_PIXEL_ASPECT_RATIO,
      g_param_spec_string ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", "1/1",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_HANDLE_EVENTS,
      g_param_spec_boolean ("handle-events", "Handle XEvents",
          "When enabled, XEvents will be selected and handled", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Adaptor number",
          "The number of the video adaptor", "0",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_DEVICE_NAME,
      g_param_spec_string ("device-name", "Adaptor name",
          "The name of the video adaptor", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstXvImageSink:handle-expose
   *
   * When enabled, the current frame will always be drawn in response to X
   * Expose.
   *
   * Since: 0.10.14
   */
  g_object_class_install_property (gobject_class, ARG_HANDLE_EXPOSE,
      g_param_spec_boolean ("handle-expose", "Handle expose",
          "When enabled, "
          "the current frame will always be drawn in response to X Expose "
          "events", TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstXvImageSink:double-buffer
   *
   * Whether to double-buffer the output.
   *
   * Since: 0.10.14
   */
  g_object_class_install_property (gobject_class, ARG_DOUBLE_BUFFER,
      g_param_spec_boolean ("double-buffer", "Double-buffer",
          "Whether to double-buffer the output", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstXvImageSink:autopaint-colorkey
   *
   * Whether to autofill overlay with colorkey
   *
   * Since: 0.10.21
   */
  g_object_class_install_property (gobject_class, ARG_AUTOPAINT_COLORKEY,
      g_param_spec_boolean ("autopaint-colorkey", "Autofill with colorkey",
          "Whether to autofill overlay with colorkey", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstXvImageSink:colorkey
   *
   * Color to use for the overlay mask.
   *
   * Since: 0.10.21
   */
  g_object_class_install_property (gobject_class, ARG_COLORKEY,
      g_param_spec_int ("colorkey", "Colorkey",
          "Color to use for the overlay mask", G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstXvImageSink:draw-borders
   *
   * Draw black borders when using GstXvImageSink:force-aspect-ratio to fill
   * unused parts of the video area.
   *
   * Since: 0.10.21
   */
  g_object_class_install_property (gobject_class, ARG_DRAW_BORDERS,
      g_param_spec_boolean ("draw-borders", "Colorkey",
          "Draw black borders to fill unused area in force-aspect-ratio mode",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_xvimagesink_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_xvimagesink_change_state);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_xvimagesink_getcaps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_xvimagesink_setcaps);
  gstbasesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_xvimagesink_buffer_alloc);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_xvimagesink_get_times);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_xvimagesink_event);

  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_xvimagesink_show_frame);
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
gst_xvimagesink_get_type (void)
{
  static GType xvimagesink_type = 0;

  if (!xvimagesink_type) {
    static const GTypeInfo xvimagesink_info = {
      sizeof (GstXvImageSinkClass),
      gst_xvimagesink_base_init,
      NULL,
      (GClassInitFunc) gst_xvimagesink_class_init,
      NULL,
      NULL,
      sizeof (GstXvImageSink),
      0,
      (GInstanceInitFunc) gst_xvimagesink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_xvimagesink_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_xvimagesink_navigation_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_xvimagesink_xoverlay_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo colorbalance_info = {
      (GInterfaceInitFunc) gst_xvimagesink_colorbalance_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo propertyprobe_info = {
      (GInterfaceInitFunc) gst_xvimagesink_property_probe_interface_init,
      NULL,
      NULL,
    };
    xvimagesink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstXvImageSink", &xvimagesink_info, 0);

    g_type_add_interface_static (xvimagesink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (xvimagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
    g_type_add_interface_static (xvimagesink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
    g_type_add_interface_static (xvimagesink_type, GST_TYPE_COLOR_BALANCE,
        &colorbalance_info);
    g_type_add_interface_static (xvimagesink_type, GST_TYPE_PROPERTY_PROBE,
        &propertyprobe_info);


    /* register type and create class in a more safe place instead of at
     * runtime since the type registration and class creation is not
     * threadsafe. */
    g_type_class_ref (gst_xvimage_buffer_get_type ());
  }

  return xvimagesink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "xvimagesink",
          GST_RANK_PRIMARY, GST_TYPE_XVIMAGESINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_xvimagesink, "xvimagesink", 0,
      "xvimagesink element");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "xvimagesink",
    "XFree86 video output plugin using Xv extension",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
