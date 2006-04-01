/* GStreamer
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
 * SECTION:element-ximagesrc
 * @short_description: a source that captures your X Display
 *
 * <refsect2>
 * <para>
 * This element captures your X Display and creates raw RGB video.  It uses
 * the XDamage extension if available to only capture areas of the screen that
 * have changed since the last frame.  It uses the XFixes extension if
 * available to also cpature your mouse pointer
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * Encode your X display to an Ogg theora video
 * </para>
 * <programlisting>
 * gst-launch -v ximagesrc ! ffmpegcolorspace ! theoraenc ! oggmux ! filesink location=desktop.ogg
 * </programlisting>
 * <para>
 * </refsect2>
 *
 */



#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ximagesrc.h"

#include <string.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

GST_DEBUG_CATEGORY_STATIC (gst_debug_ximagesrc);
#define GST_CAT_DEFAULT gst_debug_ximagesrc

/* elementfactory information */
static GstElementDetails ximagesrc_details =
GST_ELEMENT_DETAILS ("Ximage video source",
    "Source/Video",
    "Creates a screenshot video stream",
    "Lutz Mueller <lutz@users.sourceforge.net>"
    "Jan Schmidt <thaytan@mad.scientist.com>"
    "Zaheer Merali <zaheerabbas at merali dot org>");

static GstStaticPadTemplate t =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ], "
        "pixel-aspect-ratio = (fraction) [ 0, MAX ]"));

enum
{
  PROP_0,
  PROP_DISPLAY_NAME,
  PROP_SCREEN_NUM,
  PROP_SHOW_POINTER
};

GST_BOILERPLATE (GstXImageSrc, gst_ximagesrc, GstPushSrc, GST_TYPE_PUSH_SRC);

static void gst_ximagesrc_fixate (GstPad * pad, GstCaps * caps);
static void gst_ximagesrc_clear_bufpool (GstXImageSrc * ximagesrc);

/* Called when a buffer is returned from the pipeline */
static void
gst_ximagesrc_return_buf (GstXImageSrc * ximagesrc, GstXImageSrcBuffer * ximage)
{
  /* If our geometry changed we can't reuse that image. */
  if ((ximage->width != ximagesrc->width) ||
      (ximage->height != ximagesrc->height)) {
    GST_DEBUG_OBJECT (ximagesrc,
        "destroy image %p as its size changed %dx%d vs current %dx%d",
        ximage, ximage->width, ximage->height,
        ximagesrc->width, ximagesrc->height);
    g_mutex_lock (ximagesrc->x_lock);
    gst_ximageutil_ximage_destroy (ximagesrc->xcontext, ximage);
    g_mutex_unlock (ximagesrc->x_lock);
  } else {
    /* In that case we can reuse the image and add it to our image pool. */
    GST_LOG_OBJECT (ximagesrc, "recycling image %p in pool", ximage);
    /* need to increment the refcount again to recycle */
    gst_buffer_ref (GST_BUFFER (ximage));
    g_mutex_lock (ximagesrc->pool_lock);
    ximagesrc->buffer_pool = g_slist_prepend (ximagesrc->buffer_pool, ximage);
    g_mutex_unlock (ximagesrc->pool_lock);
  }
}

static gboolean
gst_ximagesrc_open_display (GstXImageSrc * s, const gchar * name)
{
  g_return_val_if_fail (GST_IS_XIMAGESRC (s), FALSE);

  if (s->xcontext != NULL)
    return TRUE;

  g_mutex_lock (s->x_lock);
  s->xcontext = ximageutil_xcontext_get (GST_ELEMENT (s), name);
  s->width = s->xcontext->width;
  s->height = s->xcontext->height;

  /* Always capture root window, for now */
  s->xwindow = s->xcontext->root;

#ifdef HAVE_XFIXES
  /* check if xfixes supported */
  {
    int error_base;

    if (XFixesQueryExtension (s->xcontext->disp, &s->fixes_event_base,
            &error_base)) {
      s->have_xfixes = TRUE;
      GST_DEBUG_OBJECT (s, "X Server supports XFixes");
    } else {

      GST_DEBUG_OBJECT (s, "X Server does not support XFixes");
    }
  }

#ifdef HAVE_XDAMAGE
  /* check if xdamage is supported */
  {
    int error_base;
    long evmask = NoEventMask;

    if (XDamageQueryExtension (s->xcontext->disp, &s->damage_event_base,
            &error_base)) {
      s->damage =
          XDamageCreate (s->xcontext->disp, s->xwindow,
          XDamageReportRawRectangles);
      if (s->damage != None) {
        s->damage_region = XFixesCreateRegion (s->xcontext->disp, NULL, 0);
        if (s->damage_region != None) {
          XGCValues values;

          GST_DEBUG_OBJECT (s, "Using XDamage extension");
          values.subwindow_mode = IncludeInferiors;
          s->damage_copy_gc = XCreateGC (s->xcontext->disp,
              s->xwindow, GCSubwindowMode, &values);
          XSelectInput (s->xcontext->disp, s->xwindow, evmask);

          s->have_xdamage = TRUE;
        } else {
          XDamageDestroy (s->xcontext->disp, s->damage);
          s->damage = None;
        }
      } else
        GST_DEBUG_OBJECT (s, "Could not attach to XDamage");
    } else {
      GST_DEBUG_OBJECT (s, "X Server does not have XDamage extension");
    }
  }
#endif
#endif

  g_mutex_unlock (s->x_lock);

  if (s->xcontext == NULL)
    return FALSE;

  return TRUE;
}

static gboolean
gst_ximagesrc_start (GstBaseSrc * basesrc)
{
  GstXImageSrc *s = GST_XIMAGESRC (basesrc);

  s->last_frame_no = -1;

  return gst_ximagesrc_open_display (s, NULL);
}

static gboolean
gst_ximagesrc_stop (GstBaseSrc * basesrc)
{
  GstXImageSrc *src = GST_XIMAGESRC (basesrc);

  gst_ximagesrc_clear_bufpool (src);

  if (src->xcontext) {
    g_mutex_lock (src->x_lock);
    ximageutil_xcontext_clear (src->xcontext);
    src->xcontext = NULL;
    g_mutex_unlock (src->x_lock);
  }

  return TRUE;
}

static gboolean
gst_ximagesrc_unlock (GstBaseSrc * basesrc)
{
  GstXImageSrc *src = GST_XIMAGESRC (basesrc);

  /* Awaken the create() func if it's waiting on the clock */
  GST_OBJECT_LOCK (src);
  if (src->clock_id) {
    GST_DEBUG_OBJECT (src, "Waking up waiting clock");
    gst_clock_id_unschedule (src->clock_id);
  }
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_ximagesrc_recalc (GstXImageSrc * src)
{
  if (!src->xcontext)
    return FALSE;

  /* FIXME: Check the display hasn't changed size or something */
  /* We could use XQueryPointer to get only the current window. */
  return TRUE;
}

/* ifdeff'ed to prevent warnings of not being used when xfixes not there */
#ifdef HAVE_XFIXES
static void
composite_pixel (GstXContext * xcontext, guchar * dest, guchar * src)
{
  guint8 r = src[2];
  guint8 g = src[1];
  guint8 b = src[0];
  guint8 a = src[3];
  guint8 dr, dg, db;
  guint32 color;
  gint r_shift, r_max, r_shift_out;
  gint g_shift, g_max, g_shift_out;
  gint b_shift, b_max, b_shift_out;

  switch (xcontext->bpp) {
    case 8:
      color = *dest;
      break;
    case 16:
      color = GUINT16_FROM_LE (*(guint32 *) (dest));
      break;
    case 32:
      color = GUINT32_FROM_LE (*(guint32 *) (dest));
      break;
    default:
      g_warning ("bpp %d not supported\n", xcontext->bpp);
      color = 0;
  }


  /* FIXME: move the code that finds shift and max in the _link function */
  for (r_shift = 0; !(xcontext->visual->red_mask & (1 << r_shift)); r_shift++);
  for (g_shift = 0; !(xcontext->visual->green_mask & (1 << g_shift));
      g_shift++);
  for (b_shift = 0; !(xcontext->visual->blue_mask & (1 << b_shift)); b_shift++);

  for (r_shift_out = 0; !(xcontext->visual->red_mask & (1 << r_shift_out));
      r_shift_out++);
  for (g_shift_out = 0; !(xcontext->visual->green_mask & (1 << g_shift_out));
      g_shift_out++);
  for (b_shift_out = 0; !(xcontext->visual->blue_mask & (1 << b_shift_out));
      b_shift_out++);


  r_max = (xcontext->visual->red_mask >> r_shift);
  b_max = (xcontext->visual->blue_mask >> b_shift);
  g_max = (xcontext->visual->green_mask >> g_shift);

#define RGBXXX_R(x)  (((x)>>r_shift) & (r_max))
#define RGBXXX_G(x)  (((x)>>g_shift) & (g_max))
#define RGBXXX_B(x)  (((x)>>b_shift) & (b_max))

  dr = (RGBXXX_R (color) * 255) / r_max;
  dg = (RGBXXX_G (color) * 255) / g_max;
  db = (RGBXXX_B (color) * 255) / b_max;

  dr = (r * a + (0xff - a) * dr) / 0xff;
  dg = (g * a + (0xff - a) * dg) / 0xff;
  db = (b * a + (0xff - a) * db) / 0xff;

  color = (((dr * r_max) / 255) << r_shift_out) +
      (((dg * g_max) / 255) << g_shift_out) +
      (((db * b_max) / 255) << b_shift_out);

  switch (xcontext->bpp) {
    case 8:
      *dest = color;
      break;
    case 16:
      *(guint16 *) (dest) = color;
      break;
    case 32:
      *(guint32 *) (dest) = color;
      break;
    default:
      g_warning ("bpp %d not supported\n", xcontext->bpp);
  }
}
#endif

/* Retrieve an XImageSrcBuffer, preferably from our
 * pool of existing images and populate it from the window */
static GstXImageSrcBuffer *
gst_ximagesrc_ximage_get (GstXImageSrc * ximagesrc)
{
  GstXImageSrcBuffer *ximage = NULL;
  GstCaps *caps = NULL;

  g_mutex_lock (ximagesrc->pool_lock);
  while (ximagesrc->buffer_pool != NULL) {
    ximage = ximagesrc->buffer_pool->data;

    if ((ximage->width != ximagesrc->width) ||
        (ximage->height != ximagesrc->height)) {
      gst_ximage_buffer_free (ximage);
    }

    ximagesrc->buffer_pool = g_slist_delete_link (ximagesrc->buffer_pool,
        ximagesrc->buffer_pool);
  }
  g_mutex_unlock (ximagesrc->pool_lock);

  if (ximage == NULL) {
    GstXContext *xcontext;

    GST_DEBUG_OBJECT (ximagesrc, "creating image (%dx%d)",
        ximagesrc->width, ximagesrc->height);

    g_mutex_lock (ximagesrc->x_lock);
    ximage = gst_ximageutil_ximage_new (ximagesrc->xcontext,
        GST_ELEMENT (ximagesrc), ximagesrc->width, ximagesrc->height,
        (BufferReturnFunc) (gst_ximagesrc_return_buf));
    if (ximage == NULL) {
      GST_ELEMENT_ERROR (ximagesrc, RESOURCE, WRITE, (NULL),
          ("could not create a %dx%d ximage", ximage->width, ximage->height));
      g_mutex_unlock (ximagesrc->x_lock);
      return NULL;
    }

    xcontext = ximagesrc->xcontext;

    caps = gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, xcontext->bpp,
        "depth", G_TYPE_INT, xcontext->depth,
        "endianness", G_TYPE_INT, xcontext->endianness,
        "red_mask", G_TYPE_INT, xcontext->r_mask_output,
        "green_mask", G_TYPE_INT, xcontext->g_mask_output,
        "blue_mask", G_TYPE_INT, xcontext->b_mask_output,
        "width", G_TYPE_INT, xcontext->width,
        "height", G_TYPE_INT, xcontext->height,
        "framerate", GST_TYPE_FRACTION, ximagesrc->fps_n, ximagesrc->fps_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION,
        gst_value_get_fraction_numerator (xcontext->par),
        gst_value_get_fraction_denominator (xcontext->par), NULL);

    gst_buffer_set_caps (GST_BUFFER (ximage), caps);
    g_mutex_unlock (ximagesrc->x_lock);
  }

  g_return_val_if_fail (GST_IS_XIMAGESRC (ximagesrc), NULL);

#ifdef HAVE_XDAMAGE
  if (ximagesrc->have_xdamage) {
    XEvent ev;

    GST_DEBUG_OBJECT (ximagesrc, "Retrieving screen using XDamage");

    do {
      XNextEvent (ximagesrc->xcontext->disp, &ev);
      if (ev.type == ximagesrc->damage_event_base + XDamageNotify) {
        XDamageNotifyEvent *dev = (XDamageNotifyEvent *) & ev;

#ifdef HAVE_XSHM
        if (ximagesrc->xcontext->use_xshm &&
            dev->area.width == ximagesrc->width &&
            dev->area.height == ximagesrc->height) {
          GST_DEBUG_OBJECT (ximagesrc, "Entire screen was damaged");
          XShmGetImage (ximagesrc->xcontext->disp, ximagesrc->xwindow,
              ximage->ximage, 0, 0, AllPlanes);
          /* No need to collect more events */
          while (XPending (ximagesrc->xcontext->disp)) {
            XNextEvent (ximagesrc->xcontext->disp, &ev);
          }
          break;
        } else
#endif
        {
          GST_LOG_OBJECT (ximagesrc,
              "Retrieving damaged sub-region @ %d,%d size %dx%d",
              dev->area.x, dev->area.y, dev->area.width, dev->area.height);

          XGetSubImage (ximagesrc->xcontext->disp, ximagesrc->xwindow,
              dev->area.x, dev->area.y,
              dev->area.width, dev->area.height,
              AllPlanes, ZPixmap, ximage->ximage, dev->area.x, dev->area.y);
        }
      }
    } while (XPending (ximagesrc->xcontext->disp));
    XDamageSubtract (ximagesrc->xcontext->disp, ximagesrc->damage, None, None);
#ifdef HAVE_XFIXES
    /* re-get area where last mouse pointer was */
    if (ximagesrc->cursor_image) {
      gint x, y, width, height;

      x = ximagesrc->cursor_image->x - ximagesrc->cursor_image->xhot;
      y = ximagesrc->cursor_image->y - ximagesrc->cursor_image->yhot;
      width = ximagesrc->cursor_image->width;
      height = ximagesrc->cursor_image->height;

      /* bounds checking */
      if (x < 0)
        x = 0;
      if (y < 0)
        y = 0;
      if (x + width > ximagesrc->width)
        width = ximagesrc->width - x;
      if (y + height > ximagesrc->height)
        height = ximagesrc->height - y;
      g_assert (x >= 0);
      g_assert (y >= 0);

      GST_DEBUG_OBJECT (ximagesrc, "Removing cursor from %d,%d", x, y);
      XGetSubImage (ximagesrc->xcontext->disp, ximagesrc->xwindow,
          x, y, width, height, AllPlanes, ZPixmap, ximage->ximage, x, y);
    }
#endif


  } else {
#endif

#ifdef HAVE_XSHM
    if (ximagesrc->xcontext->use_xshm) {
      GST_DEBUG_OBJECT (ximagesrc, "Retrieving screen using XShm");
      XShmGetImage (ximagesrc->xcontext->disp, ximagesrc->xwindow,
          ximage->ximage, 0, 0, AllPlanes);

    } else
#endif /* HAVE_XSHM */
    {
      GST_DEBUG_OBJECT (ximagesrc, "Retrieving screen using XGetImage");
      ximage->ximage = XGetImage (ximagesrc->xcontext->disp, ximagesrc->xwindow,
          0, 0, ximagesrc->width, ximagesrc->height, AllPlanes, ZPixmap);
    }
#ifdef HAVE_XDAMAGE
  }
#endif

#ifdef HAVE_XFIXES
  if (ximagesrc->show_pointer && ximagesrc->have_xfixes) {

    GST_DEBUG_OBJECT (ximagesrc, "Using XFixes to draw cursor");
    /* get cursor */
    ximagesrc->cursor_image = XFixesGetCursorImage (ximagesrc->xcontext->disp);
    if (ximagesrc->cursor_image != NULL) {
      int cx, cy, i, j, count;

      cx = ximagesrc->cursor_image->x - ximagesrc->cursor_image->xhot;
      cy = ximagesrc->cursor_image->y - ximagesrc->cursor_image->yhot;
      //count = image->width * image->height;
      count = ximagesrc->cursor_image->width * ximagesrc->cursor_image->height;
      for (i = 0; i < count; i++)
        ximagesrc->cursor_image->pixels[i] =
            GUINT_TO_LE (ximagesrc->cursor_image->pixels[i]);

      /* copy those pixels across */
      for (j = cy;
          j < cy + ximagesrc->cursor_image->height && j < ximagesrc->height;
          j++) {
        for (i = cx;
            i < cx + ximagesrc->cursor_image->width && i < ximagesrc->width;
            i++) {
          guint8 *src, *dest;

          src =
              (guint8 *) & (ximagesrc->cursor_image->pixels[((j -
                          cy) * ximagesrc->cursor_image->width + (i - cx))]);
          dest =
              (guint8 *) & (ximage->ximage->data[(j * ximagesrc->width +
                      i) * (ximagesrc->xcontext->bpp / 8)]);

          composite_pixel (ximagesrc->xcontext, (guint8 *) dest,
              (guint8 *) src);
        }
      }
    }
  }
#endif

  return ximage;
}

static GstFlowReturn
gst_ximagesrc_create (GstPushSrc * bs, GstBuffer ** buf)
{
  GstXImageSrc *s = GST_XIMAGESRC (bs);
  GstXImageSrcBuffer *image;
  GstClockTime base_time;
  GstClockTime next_capture_ts;
  GstClockTime dur;
  gint64 next_frame_no;

  if (!gst_ximagesrc_recalc (s)) {
    /* FIXME: Post error on the bus */
    return GST_FLOW_ERROR;
  }

  if (s->fps_n <= 0 || s->fps_d <= 0)
    return GST_FLOW_NOT_NEGOTIATED;     /* FPS must be > 0 */

  /* Now, we might need to wait for the next multiple of the fps 
   * before capturing */

  GST_OBJECT_LOCK (s);
  base_time = GST_ELEMENT_CAST (s)->base_time;
  next_capture_ts = gst_clock_get_time (GST_ELEMENT_CLOCK (s));
  next_capture_ts -= base_time;

  /* Figure out which 'frame number' position we're at, based on the cur time
   * and frame rate */
  next_frame_no = gst_util_uint64_scale (next_capture_ts,
      s->fps_n, GST_SECOND * s->fps_d);
  if (next_frame_no == s->last_frame_no) {
    GstClockID id;
    GstClockReturn ret;

    /* Need to wait for the next frame */
    next_frame_no += 1;

    /* Figure out what the next frame time is */
    next_capture_ts = gst_util_uint64_scale (next_frame_no,
        s->fps_d * GST_SECOND, s->fps_n);

    id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (s),
        next_capture_ts + base_time);
    s->clock_id = id;

    /* release the object lock while waiting */
    GST_OBJECT_UNLOCK (s);

    GST_DEBUG_OBJECT (s, "Waiting for next frame time %" G_GUINT64_FORMAT,
        next_capture_ts);
    ret = gst_clock_id_wait (id, NULL);
    GST_OBJECT_LOCK (s);

    gst_clock_id_unref (id);
    s->clock_id = NULL;
    if (ret == GST_CLOCK_UNSCHEDULED) {
      /* Got woken up by the unlock function */
      GST_OBJECT_UNLOCK (s);
      return GST_FLOW_WRONG_STATE;
    }
    /* Duration is a complete 1/fps frame duration */
    dur = gst_util_uint64_scale_int (GST_SECOND, s->fps_d, s->fps_n);
  } else {
    GstClockTime next_frame_ts;

    GST_DEBUG_OBJECT (s, "No need to wait for next frame time %"
        G_GUINT64_FORMAT " next frame = %" G_GINT64_FORMAT " prev = %"
        G_GINT64_FORMAT, next_capture_ts, next_frame_no, s->last_frame_no);
    next_frame_ts = gst_util_uint64_scale (next_frame_no + 1,
        s->fps_d * GST_SECOND, s->fps_n);
    /* Frame duration is from now until the next expected capture time */
    dur = next_frame_ts - next_capture_ts;
  }
  s->last_frame_no = next_frame_no;
  GST_OBJECT_UNLOCK (s);

  image = gst_ximagesrc_ximage_get (s);
  if (!image)
    return GST_FLOW_ERROR;

  *buf = GST_BUFFER (image);
  GST_BUFFER_TIMESTAMP (*buf) = next_capture_ts;
  GST_BUFFER_DURATION (*buf) = dur;

  return GST_FLOW_OK;
}

static void
gst_ximagesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstXImageSrc *src = GST_XIMAGESRC (object);

  switch (prop_id) {
    case PROP_DISPLAY_NAME:

      g_free (src->display_name);
      src->display_name = g_strdup (g_value_get_string (value));

      // src->screen_num = MIN (src->screen_num, ScreenCount (src->display) - 1);
      break;
    case PROP_SCREEN_NUM:
      src->screen_num = g_value_get_uint (value);
      // src->screen_num = MIN (src->screen_num, ScreenCount (src->display) - 1);
      break;
    case PROP_SHOW_POINTER:
      src->show_pointer = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_ximagesrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstXImageSrc *src = GST_XIMAGESRC (object);

  switch (prop_id) {
    case PROP_DISPLAY_NAME:
      if (src->xcontext)
        g_value_set_string (value, DisplayString (src->xcontext->disp));
      else
        g_value_set_string (value, src->display_name);

      break;
    case PROP_SCREEN_NUM:
      g_value_set_uint (value, src->screen_num);
      break;
    case PROP_SHOW_POINTER:
      g_value_set_boolean (value, src->show_pointer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ximagesrc_clear_bufpool (GstXImageSrc * ximagesrc)
{
  g_mutex_lock (ximagesrc->pool_lock);
  while (ximagesrc->buffer_pool != NULL) {
    GstXImageSrcBuffer *ximage = ximagesrc->buffer_pool->data;

    gst_ximage_buffer_free (ximage);

    ximagesrc->buffer_pool = g_slist_delete_link (ximagesrc->buffer_pool,
        ximagesrc->buffer_pool);
  }
  g_mutex_unlock (ximagesrc->pool_lock);
}

static void
gst_ximagesrc_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (ec, &ximagesrc_details);
  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&t));
}

static void
gst_ximagesrc_dispose (GObject * object)
{
  /* Drop references in the buffer_pool */
  gst_ximagesrc_clear_bufpool (GST_XIMAGESRC (object));
}

static void
gst_ximagesrc_finalize (GObject * object)
{
  GstXImageSrc *src = GST_XIMAGESRC (object);

  if (src->xcontext)
    ximageutil_xcontext_clear (src->xcontext);

  g_mutex_free (src->pool_lock);
  g_mutex_free (src->x_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_ximagesrc_get_caps (GstBaseSrc * bs)
{
  GstXImageSrc *s = GST_XIMAGESRC (bs);
  GstXContext *xcontext;

  if ((!s->xcontext) && (!gst_ximagesrc_open_display (s, NULL)))
    return gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC (s)->
            srcpad));

  if (!gst_ximagesrc_recalc (s))
    return gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC (s)->
            srcpad));

  xcontext = s->xcontext;

  return gst_caps_new_simple ("video/x-raw-rgb",
      "bpp", G_TYPE_INT, xcontext->bpp,
      "depth", G_TYPE_INT, xcontext->depth,
      "endianness", G_TYPE_INT, xcontext->endianness,
      "red_mask", G_TYPE_INT, xcontext->r_mask_output,
      "green_mask", G_TYPE_INT, xcontext->g_mask_output,
      "blue_mask", G_TYPE_INT, xcontext->b_mask_output,
      "width", G_TYPE_INT, xcontext->width,
      "height", G_TYPE_INT, xcontext->height,
      "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
      "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
      NULL);
}

static gboolean
gst_ximagesrc_set_caps (GstBaseSrc * bs, GstCaps * caps)
{
  GstXImageSrc *s = GST_XIMAGESRC (bs);
  GstStructure *structure;
  const GValue *new_fps;

  /* If not yet opened, disallow setcaps until later */
  if (!s->xcontext)
    return FALSE;

  /* The only thing that can change is the framerate downstream wants */
  structure = gst_caps_get_structure (caps, 0);
  new_fps = gst_structure_get_value (structure, "framerate");
  if (!new_fps)
    return FALSE;

  /* Store this FPS for use when generating buffers */
  s->fps_n = gst_value_get_fraction_numerator (new_fps);
  s->fps_d = gst_value_get_fraction_denominator (new_fps);

  GST_DEBUG_OBJECT (s, "peer wants %d/%d fps", s->fps_n, s->fps_d);

  return TRUE;
}

static void
gst_ximagesrc_fixate (GstPad * pad, GstCaps * caps)
{
  gint i;
  GstStructure *structure;

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 25, 1);
  }
}

static void
gst_ximagesrc_class_init (GstXImageSrcClass * klass)
{
  GObjectClass *gc = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *bc = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *push_class = GST_PUSH_SRC_CLASS (klass);

  gc->set_property = gst_ximagesrc_set_property;
  gc->get_property = gst_ximagesrc_get_property;
  gc->dispose = gst_ximagesrc_dispose;
  gc->finalize = gst_ximagesrc_finalize;

  g_object_class_install_property (gc, PROP_DISPLAY_NAME,
      g_param_spec_string ("display_name", "Display", "X Display name", NULL,
          G_PARAM_READWRITE));
  g_object_class_install_property (gc, PROP_SCREEN_NUM,
      g_param_spec_uint ("screen_num", "Screen number", "X Screen number",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gc, PROP_SHOW_POINTER,
      g_param_spec_boolean ("show_pointer", "Show Mouse Pointer",
          "Show mouse pointer if XFixes extension enabled", TRUE,
          G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  push_class->create = gst_ximagesrc_create;
  bc->get_caps = gst_ximagesrc_get_caps;
  bc->set_caps = gst_ximagesrc_set_caps;
  bc->start = gst_ximagesrc_start;
  bc->stop = gst_ximagesrc_stop;
  bc->unlock = gst_ximagesrc_unlock;
}

static void
gst_ximagesrc_init (GstXImageSrc * ximagesrc, GstXImageSrcClass * klass)
{
  gst_base_src_set_live (GST_BASE_SRC (ximagesrc), TRUE);
  gst_pad_set_fixatecaps_function (GST_BASE_SRC_PAD (ximagesrc),
      gst_ximagesrc_fixate);

  ximagesrc->pool_lock = g_mutex_new ();
  ximagesrc->x_lock = g_mutex_new ();
  ximagesrc->show_pointer = TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  GST_DEBUG_CATEGORY_INIT (gst_debug_ximagesrc, "ximagesrc", 0,
      "ximagesrc element debug");

  ret = gst_element_register (plugin, "ximagesrc", GST_RANK_NONE,
      GST_TYPE_XIMAGESRC);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ximagesrc",
    "XFree86 video input plugin based on standard Xlib calls",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
