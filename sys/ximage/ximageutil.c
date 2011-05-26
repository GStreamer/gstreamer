/* GStreamer
 * Copyright (C) <2005> Luca Ognibene <luogni@tin.it>
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

#include "ximageutil.h"

#ifdef HAVE_XSHM
static gboolean error_caught = FALSE;

static int
ximageutil_handle_xerror (Display * display, XErrorEvent * xevent)
{
  char error_msg[1024];

  XGetErrorText (display, xevent->error_code, error_msg, 1024);
  GST_DEBUG ("ximageutil failed to use XShm calls. error: %s", error_msg);
  error_caught = TRUE;
  return 0;
}

/* This function checks that it is actually really possible to create an image
   using XShm */
gboolean
ximageutil_check_xshm_calls (GstXContext * xcontext)
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
  handler = XSetErrorHandler (ximageutil_handle_xerror);

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

  SHMInfo.shmaddr = shmat (SHMInfo.shmid, 0, 0);
  if (SHMInfo.shmaddr == ((void *) -1)) {
    GST_WARNING ("Failed to shmat: %s", g_strerror (errno));
    goto beach;
  }

  /* Delete the SHM segment. It will actually go away automatically
   * when we detach now */
  shmctl (SHMInfo.shmid, IPC_RMID, 0);

  ximage->data = SHMInfo.shmaddr;
  SHMInfo.readOnly = FALSE;

  if (XShmAttach (xcontext->disp, &SHMInfo) == 0) {
    GST_WARNING ("Failed to XShmAttach");
    goto beach;
  }

  /* Sync to ensure we see any errors we caused */
  XSync (xcontext->disp, FALSE);

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

/* This function gets the X Display and global info about it. Everything is
   stored in our object and will be cleaned when the object is disposed. Note
   here that caps for supported format are generated without any window or
   image creation */
GstXContext *
ximageutil_xcontext_get (GstElement * parent, const gchar * display_name)
{
  GstXContext *xcontext = NULL;
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i;

  xcontext = g_new0 (GstXContext, 1);

  xcontext->disp = XOpenDisplay (display_name);
  GST_DEBUG_OBJECT (parent, "opened display %p", xcontext->disp);
  if (!xcontext->disp) {
    g_free (xcontext);
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

  xcontext->caps = NULL;

  GST_DEBUG_OBJECT (parent, "X reports %dx%d pixels and %d mm x %d mm",
      xcontext->width, xcontext->height, xcontext->widthmm, xcontext->heightmm);
  ximageutil_calculate_pixel_aspect_ratio (xcontext);

  /* We get supported pixmap formats at supported depth */
  px_formats = XListPixmapFormats (xcontext->disp, &nb_formats);

  if (!px_formats) {
    XCloseDisplay (xcontext->disp);
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
      ximageutil_check_xshm_calls (xcontext)) {
    xcontext->use_xshm = TRUE;
    GST_DEBUG ("ximageutil is using XShm extension");
  } else {
    xcontext->use_xshm = FALSE;
    GST_DEBUG ("ximageutil is not using XShm extension");
  }
#endif /* HAVE_XSHM */

  /* our caps system handles 24/32bpp RGB as big-endian. */
  if ((xcontext->bpp == 24 || xcontext->bpp == 32) &&
      xcontext->endianness == G_LITTLE_ENDIAN) {
    xcontext->endianness = G_BIG_ENDIAN;
    xcontext->r_mask_output = GUINT32_TO_BE (xcontext->visual->red_mask);
    xcontext->g_mask_output = GUINT32_TO_BE (xcontext->visual->green_mask);
    xcontext->b_mask_output = GUINT32_TO_BE (xcontext->visual->blue_mask);
    if (xcontext->bpp == 24) {
      xcontext->r_mask_output >>= 8;
      xcontext->g_mask_output >>= 8;
      xcontext->b_mask_output >>= 8;
    }
  } else {
    xcontext->r_mask_output = xcontext->visual->red_mask;
    xcontext->g_mask_output = xcontext->visual->green_mask;
    xcontext->b_mask_output = xcontext->visual->blue_mask;
  }

  return xcontext;
}

/* This function cleans the X context. Closing the Display and unrefing the
   caps for supported formats. */
void
ximageutil_xcontext_clear (GstXContext * xcontext)
{
  g_return_if_fail (xcontext != NULL);

  if (xcontext->caps != NULL)
    gst_caps_unref (xcontext->caps);

  if (xcontext->par) {
    g_value_unset (xcontext->par);
    g_free (xcontext->par);
  }

  XCloseDisplay (xcontext->disp);

  g_free (xcontext);
}

/* This function calculates the pixel aspect ratio based on the properties
 * in the xcontext structure and stores it there. */
void
ximageutil_calculate_pixel_aspect_ratio (GstXContext * xcontext)
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

  if (xcontext->par)
    g_free (xcontext->par);
  xcontext->par = g_new0 (GValue, 1);
  g_value_init (xcontext->par, GST_TYPE_FRACTION);
  gst_value_set_fraction (xcontext->par, par[index][0], par[index][1]);
  GST_DEBUG ("set xcontext PAR to %d/%d\n",
      gst_value_get_fraction_numerator (xcontext->par),
      gst_value_get_fraction_denominator (xcontext->par));
}

static GstBufferClass *ximagesrc_buffer_parent_class = NULL;

static void
gst_ximagesrc_buffer_finalize (GstXImageSrcBuffer * ximage)
{
  GstElement *parent;

  g_return_if_fail (ximage != NULL);

  parent = ximage->parent;
  if (parent == NULL) {
    g_warning ("XImageSrcBuffer->ximagesrc == NULL");
    goto beach;
  }

  if (ximage->return_func)
    ximage->return_func (parent, ximage);

beach:

  GST_MINI_OBJECT_CLASS (ximagesrc_buffer_parent_class)->finalize
      (GST_MINI_OBJECT (ximage));

  return;
}

void
gst_ximage_buffer_free (GstXImageSrcBuffer * ximage)
{
  /* make sure it is not recycled */
  ximage->width = -1;
  ximage->height = -1;
  gst_buffer_unref (GST_BUFFER (ximage));
}

static void
gst_ximagesrc_buffer_init (GstXImageSrcBuffer * ximage_buffer, gpointer g_class)
{
#ifdef HAVE_XSHM
  ximage_buffer->SHMInfo.shmaddr = ((void *) -1);
  ximage_buffer->SHMInfo.shmid = -1;
#endif
}

static void
gst_ximagesrc_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  ximagesrc_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_ximagesrc_buffer_finalize;
}

static GType
gst_ximagesrc_buffer_get_type (void)
{
  static GType _gst_ximagesrc_buffer_type;

  if (G_UNLIKELY (_gst_ximagesrc_buffer_type == 0)) {
    static const GTypeInfo ximagesrc_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_ximagesrc_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstXImageSrcBuffer),
      0,
      (GInstanceInitFunc) gst_ximagesrc_buffer_init,
      NULL
    };
    _gst_ximagesrc_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstXImageSrcBuffer", &ximagesrc_buffer_info, 0);
  }
  return _gst_ximagesrc_buffer_type;
}

/* This function handles GstXImageSrcBuffer creation depending on XShm availability */
GstXImageSrcBuffer *
gst_ximageutil_ximage_new (GstXContext * xcontext,
    GstElement * parent, int width, int height, BufferReturnFunc return_func)
{
  GstXImageSrcBuffer *ximage = NULL;
  gboolean succeeded = FALSE;

  ximage =
      (GstXImageSrcBuffer *) gst_mini_object_new (GST_TYPE_XIMAGESRC_BUFFER);

  ximage->width = width;
  ximage->height = height;

#ifdef HAVE_XSHM
  if (xcontext->use_xshm) {
    ximage->ximage = XShmCreateImage (xcontext->disp,
        xcontext->visual, xcontext->depth,
        ZPixmap, NULL, &ximage->SHMInfo, ximage->width, ximage->height);
    if (!ximage->ximage) {
      GST_WARNING_OBJECT (parent,
          "could not XShmCreateImage a %dx%d image",
          ximage->width, ximage->height);

      /* Retry without XShm */
      xcontext->use_xshm = FALSE;
      goto no_xshm;
    }

    /* we have to use the returned bytes_per_line for our shm size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;
    ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size,
        IPC_CREAT | 0777);
    if (ximage->SHMInfo.shmid == -1)
      goto beach;

    ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, 0, 0);
    if (ximage->SHMInfo.shmaddr == ((void *) -1))
      goto beach;

    /* Delete the SHM segment. It will actually go away automatically
     * when we detach now */
    shmctl (ximage->SHMInfo.shmid, IPC_RMID, 0);

    ximage->ximage->data = ximage->SHMInfo.shmaddr;
    ximage->SHMInfo.readOnly = FALSE;

    if (XShmAttach (xcontext->disp, &ximage->SHMInfo) == 0)
      goto beach;

    XSync (xcontext->disp, FALSE);
  } else
  no_xshm:
#endif /* HAVE_XSHM */
  {
    ximage->ximage = XCreateImage (xcontext->disp,
        xcontext->visual,
        xcontext->depth,
        ZPixmap, 0, NULL, ximage->width, ximage->height, xcontext->bpp, 0);
    if (!ximage->ximage)
      goto beach;

    /* we have to use the returned bytes_per_line for our image size */
    ximage->size = ximage->ximage->bytes_per_line * ximage->ximage->height;
    ximage->ximage->data = g_malloc (ximage->size);

    XSync (xcontext->disp, FALSE);
  }
  succeeded = TRUE;

  GST_BUFFER_DATA (ximage) = (guchar *) ximage->ximage->data;
  GST_BUFFER_SIZE (ximage) = ximage->size;

  /* Keep a ref to our src */
  ximage->parent = gst_object_ref (parent);
  ximage->return_func = return_func;
beach:
  if (!succeeded) {
    gst_ximage_buffer_free (ximage);
    ximage = NULL;
  }

  return ximage;
}

/* This function destroys a GstXImageBuffer handling XShm availability */
void
gst_ximageutil_ximage_destroy (GstXContext * xcontext,
    GstXImageSrcBuffer * ximage)
{
  /* We might have some buffers destroyed after changing state to NULL */
  if (!xcontext)
    goto beach;

  g_return_if_fail (ximage != NULL);

#ifdef HAVE_XSHM
  if (xcontext->use_xshm) {
    if (ximage->SHMInfo.shmaddr != ((void *) -1)) {
      XShmDetach (xcontext->disp, &ximage->SHMInfo);
      XSync (xcontext->disp, 0);
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

  XSync (xcontext->disp, FALSE);
beach:
  if (ximage->parent) {
    /* Release the ref to our parent */
    gst_object_unref (ximage->parent);
    ximage->parent = NULL;
  }

  return;
}
