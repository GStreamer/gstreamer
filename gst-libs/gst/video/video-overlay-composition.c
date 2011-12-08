/* GStreamer Video Overlay Composition
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Collabora Ltd.
 * Copyright (C) 2011 Tim-Philipp Müller <tim centricular net>
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
 * SECTION:gstvideooverlaycomposition
 * @short_description: Video Buffer Overlay Compositions (Subtitles, Logos)
 *
 * <refsect2>
 * <para>
 * Functions to create and handle overlay compositions on video buffers.
 * </para>
 * <para>
 * An overlay composition describes one or more overlay rectangles to be
 * blended on top of a video buffer.
 * </para>
 * <para>
 * This API serves two main purposes:
 * <itemizedlist>
 * <listitem>
 * it can be used to attach overlay information (subtitles or logos)
 * to non-raw video buffers such as GL/VAAPI/VDPAU surfaces. The actual
 * blending of the overlay can then be done by e.g. the video sink that
 * processes these non-raw buffers.
 * </listitem>
 * <listitem>
 * it can also be used to blend overlay rectangles on top of raw video
 * buffers, thus consolidating blending functionality for raw video in
 * one place.
 * </listitem>
 * Together, this allows existing overlay elements to easily handle raw
 * and non-raw video as input in without major changes (once the overlays
 * have been put into a #GstOverlayComposition object anyway) - for raw
 * video the overlay can just use the blending function to blend the data
 * on top of the video, and for surface buffers it can just attach them to
 * the buffer and let the sink render the overlays.
 * </itemizedlist>
 * </para>
 * </refsect2>
 *
 * Since: 0.10.36
 */

/* TODO:
 *  - provide accessors for seq_num and other fields (as needed)
 *  - allow overlay to set/get original pango markup string on/from rectangle
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video-overlay-composition.h"
#include "video-blend.h"

struct _GstVideoOverlayComposition
{
  GstMiniObject parent;

  guint num_rectangles;
  GstVideoOverlayRectangle **rectangles;

  /* lowest rectangle sequence number still used by the upstream
   * overlay element. This way a renderer maintaining some kind of
   * rectangles <-> surface cache can know when to free cached
   * surfaces/rectangles. */
  guint min_seq_num_used;

  /* sequence number for the composition (same series as rectangles) */
  guint seq_num;
};

struct _GstVideoOverlayCompositionClass
{
  GstMiniObjectClass parent_class;
};

struct _GstVideoOverlayRectangle
{
  GstMiniObject parent;

  /* Position on video frame and dimension of output rectangle in
   * output frame terms (already adjusted for the PAR of the output
   * frame). x/y can be negative (overlay will be clipped then) */
  gint x, y;
  guint render_width, render_height;

  /* Dimensions of overlay pixels */
  guint width, height, stride;

  /* The format of the data in pixels */
  GstVideoFormat format;

  /* Refcounted blob of memory, no caps or timestamps */
  GstBuffer *pixels;

  /* FIXME: how to express source like text or pango markup?
   *        (just add source type enum + source buffer with data)
   *
   * FOR 0.10: always send pixel blobs, but attach source data in
   * addition (reason: if downstream changes, we can't renegotiate
   * that properly, if we just do a query of supported formats from
   * the start). Sink will just ignore pixels and use pango markup
   * from source data if it supports that.
   *
   * FOR 0.11: overlay should query formats (pango markup, pixels)
   * supported by downstream and then only send that. We can
   * renegotiate via the reconfigure event.
   */

  /* sequence number: useful for backends/renderers/sinks that want
   * to maintain a cache of rectangles <-> surfaces. The value of
   * the min_seq_num_used in the composition tells the renderer which
   * rectangles have expired. */
  guint seq_num;

  /* FIXME: we may also need a (private) way to cache converted/scaled
   * pixel blobs */
#if !GLIB_CHECK_VERSION (2, 31, 0)
  GStaticMutex lock;
#else
  GMutex lock;
#endif

  GList *scaled_rectangles;
};

struct _GstVideoOverlayRectangleClass
{
  GstMiniObjectClass parent_class;
};

#if !GLIB_CHECK_VERSION (2, 31, 0)
#define GST_RECTANGLE_LOCK(rect)   g_static_mutex_lock(&rect->lock)
#define GST_RECTANGLE_UNLOCK(rect) g_static_mutex_unlock(&rect->lock)
#else
#define GST_RECTANGLE_LOCK(rect)   g_mutex_lock(&rect->lock)
#define GST_RECTANGLE_UNLOCK(rect) g_mutex_unlock(&rect->lock)
#endif

static void gst_video_overlay_composition_class_init (GstMiniObjectClass * k);
static void gst_video_overlay_composition_finalize (GstMiniObject * comp);
static void gst_video_overlay_rectangle_class_init (GstMiniObjectClass * klass);
static void gst_video_overlay_rectangle_finalize (GstMiniObject * rect);

/* --------------------------- utility functions --------------------------- */

#ifndef GST_DISABLE_GST_DEBUG

#define GST_CAT_DEFAULT ensure_debug_category()

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("video-composition", 0,
        "video overlay composition");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}

#else

#define ensure_debug_category() /* NOOP */

#endif /* GST_DISABLE_GST_DEBUG */

static guint
gst_video_overlay_get_seqnum (void)
{
  static gint seqnum;           /* 0 */

#if GLIB_CHECK_VERSION(2,29,5)
  return (guint) g_atomic_int_add (&seqnum, 1);
#else
  return (guint) g_atomic_int_exchange_and_add (&seqnum, 1);
#endif
}

#define GST_OVERLAY_COMPOSITION_QUARK gst_overlay_composition_quark_get()
static GQuark
gst_overlay_composition_quark_get (void)
{
  static gsize quark_gonce = 0;

  if (g_once_init_enter (&quark_gonce)) {
    gsize quark;

    quark = (gsize) g_quark_from_static_string ("GstVideoOverlayComposition");

    g_once_init_leave (&quark_gonce, quark);
  }

  return (GQuark) quark_gonce;
}

#define COMPOSITION_QUARK composition_quark_get()
static GQuark
composition_quark_get (void)
{
  static gsize quark_gonce = 0;

  if (g_once_init_enter (&quark_gonce)) {
    gsize quark;

    quark = (gsize) g_quark_from_static_string ("composition");

    g_once_init_leave (&quark_gonce, quark);
  }

  return (GQuark) quark_gonce;
}

/**
 * gst_video_buffer_set_overlay_composition:
 * @buf: a #GstBuffer
 * @comp: (allow-none): a #GstVideoOverlayComposition, or NULL to clear a
 *     previously-set composition
 *
 * Sets an overlay composition on a buffer. The buffer will obtain its own
 * reference to the composition, meaning this function does not take ownership
 * of @comp.
 *
 * Since: 0.10.36
 */
void
gst_video_buffer_set_overlay_composition (GstBuffer * buf,
    GstVideoOverlayComposition * comp)
{
  gst_buffer_set_qdata (buf, GST_OVERLAY_COMPOSITION_QUARK,
      gst_structure_id_new (GST_OVERLAY_COMPOSITION_QUARK,
          COMPOSITION_QUARK, GST_TYPE_VIDEO_OVERLAY_COMPOSITION, comp, NULL));
}

/**
 * gst_video_buffer_get_overlay_composition:
 * @buf: a #GstBuffer
 *
 * Get the overlay composition that has previously been attached to a buffer
 * with gst_video_buffer_get_overlay_composition(), usually by another element
 * upstream.
 *
 * Returns: (transfer none): the #GstVideoOverlayComposition attached to
 *    this buffer, or NULL. Does not return a reference to the composition,
 *    caller must obtain her own ref via gst_video_overlay_composition_ref()
 *    if needed.
 *
 * Since: 0.10.36
 */
GstVideoOverlayComposition *
gst_video_buffer_get_overlay_composition (GstBuffer * buf)
{
  const GstStructure *s;
  const GValue *val;

  s = gst_buffer_get_qdata (buf, GST_OVERLAY_COMPOSITION_QUARK);
  if (s == NULL)
    return NULL;

  val = gst_structure_id_get_value (s, COMPOSITION_QUARK);
  if (val == NULL)
    return NULL;

  return GST_VIDEO_OVERLAY_COMPOSITION (gst_value_get_mini_object (val));
}

/* ------------------------------ composition ------------------------------ */

#define RECTANGLE_ARRAY_STEP 4  /* premature optimization */

GType
gst_video_overlay_composition_get_type (void)
{
  static volatile gsize type_id = 0;

  if (g_once_init_enter (&type_id)) {
    GType new_type_id = g_type_register_static_simple (GST_TYPE_MINI_OBJECT,
        g_intern_static_string ("GstVideoOverlayComposition"),
        sizeof (GstVideoOverlayCompositionClass),
        (GClassInitFunc) gst_video_overlay_composition_class_init,
        sizeof (GstVideoOverlayComposition),
        NULL,
        (GTypeFlags) 0);

    g_once_init_leave (&type_id, new_type_id);
  }

  return type_id;
}

static void
gst_video_overlay_composition_finalize (GstMiniObject * mini_obj)
{
  GstVideoOverlayComposition *comp = (GstVideoOverlayComposition *) mini_obj;
  guint num;

  num = comp->num_rectangles;

  while (num > 0) {
    gst_video_overlay_rectangle_unref (comp->rectangles[num - 1]);
    --num;
  }

  g_free (comp->rectangles);
  comp->rectangles = NULL;
  comp->num_rectangles = 0;

  /* not chaining up to GstMiniObject's finalize for now, we know it's empty */
}

static void
gst_video_overlay_composition_class_init (GstMiniObjectClass * klass)
{
  klass->finalize = gst_video_overlay_composition_finalize;
  klass->copy = (GstMiniObjectCopyFunction) gst_video_overlay_composition_copy;
}

/**
 * gst_video_overlay_composition_new:
 * @rectangle: (transfer none): a #GstVideoOverlayRectangle to add to the
 *     composition
 *
 * Creates a new video overlay composition object to hold one or more
 * overlay rectangles.
 *
 * Returns: (transfer full): a new #GstVideoOverlayComposition. Unref with
 *     gst_video_overlay_composition_unref() when no longer needed.
 *
 * Since: 0.10.36
 */
GstVideoOverlayComposition *
gst_video_overlay_composition_new (GstVideoOverlayRectangle * rectangle)
{
  GstVideoOverlayComposition *comp;


  /* FIXME: should we allow empty compositions? Could also be expressed as
   * buffer without a composition on it. Maybe there are cases where doing
   * an empty new + _add() in a loop is easier? */
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle), NULL);

  comp = (GstVideoOverlayComposition *)
      gst_mini_object_new (GST_TYPE_VIDEO_OVERLAY_COMPOSITION);

  comp->rectangles = g_new0 (GstVideoOverlayRectangle *, RECTANGLE_ARRAY_STEP);
  comp->rectangles[0] = gst_video_overlay_rectangle_ref (rectangle);
  comp->num_rectangles = 1;

  comp->seq_num = gst_video_overlay_get_seqnum ();

  /* since the rectangle was created earlier, its seqnum is smaller than ours */
  comp->min_seq_num_used = rectangle->seq_num;

  GST_LOG ("new composition %p: seq_num %u with rectangle %p", comp,
      comp->seq_num, rectangle);

  return comp;
}

/**
 * gst_video_overlay_composition_add_rectangle:
 * @comp: a #GstVideoOverlayComposition
 * @rectangle: (transfer none): a #GstVideoOverlayRectangle to add to the
 *     composition
 *
 * Adds an overlay rectangle to an existing overlay composition object. This
 * must be done right after creating the overlay composition.
 *
 * Since: 0.10.36
 */
void
gst_video_overlay_composition_add_rectangle (GstVideoOverlayComposition * comp,
    GstVideoOverlayRectangle * rectangle)
{
  g_return_if_fail (GST_IS_VIDEO_OVERLAY_COMPOSITION (comp));
  g_return_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle));
  g_return_if_fail (GST_MINI_OBJECT_REFCOUNT_VALUE (comp) == 1);

  if (comp->num_rectangles % RECTANGLE_ARRAY_STEP == 0) {
    comp->rectangles =
        g_renew (GstVideoOverlayRectangle *, comp->rectangles,
        comp->num_rectangles + RECTANGLE_ARRAY_STEP);
  }

  comp->rectangles[comp->num_rectangles] =
      gst_video_overlay_rectangle_ref (rectangle);
  comp->num_rectangles += 1;

  comp->min_seq_num_used = MIN (comp->min_seq_num_used, rectangle->seq_num);

  GST_LOG ("composition %p: added rectangle %p", comp, rectangle);
}

/**
 * gst_video_overlay_composition_n_rectangles:
 * @comp: a #GstVideoOverlayComposition
 *
 * Returns the number of #GstVideoOverlayRectangle<!-- -->s contained in @comp.
 *
 * Returns: the number of rectangles
 *
 * Since: 0.10.36
 */
guint
gst_video_overlay_composition_n_rectangles (GstVideoOverlayComposition * comp)
{
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_COMPOSITION (comp), 0);

  return comp->num_rectangles;
}

/**
 * gst_video_overlay_composition_get_rectangle:
 * @comp: a #GstVideoOverlayComposition
 * @n: number of the rectangle to get
 *
 * Returns the @n-th #GstVideoOverlayRectangle contained in @comp.
 *
 * Returns: (transfer none): the @n-th rectangle, or NULL if @n is out of
 *     bounds. Will not return a new reference, the caller will need to
 *     obtain her own reference using gst_video_overlay_rectangle_ref()
 *     if needed.
 *
 * Since: 0.10.36
 */
GstVideoOverlayRectangle *
gst_video_overlay_composition_get_rectangle (GstVideoOverlayComposition * comp,
    guint n)
{
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_COMPOSITION (comp), NULL);

  if (n >= comp->num_rectangles)
    return NULL;

  return comp->rectangles[n];
}

static gboolean
gst_video_overlay_rectangle_needs_scaling (GstVideoOverlayRectangle * r)
{
  return (r->width != r->render_width || r->height != r->render_height);
}

/**
 * gst_video_overlay_composition_blend:
 * @comp: a #GstVideoOverlayComposition
 * @video_buf: a #GstBuffer containing raw video data in a supported format
 *
 * Blends the overlay rectangles in @comp on top of the raw video data
 * contained in @video_buf. The data in @video_buf must be writable. If
 * needed, use gst_buffer_make_writable() before calling this function to
 * ensure a buffer is writable. @video_buf must also have valid raw video
 * caps set on it.
 *
 * Since: 0.10.36
 */
gboolean
gst_video_overlay_composition_blend (GstVideoOverlayComposition * comp,
    GstBuffer * video_buf)
{
  GstBlendVideoFormatInfo video_info, rectangle_info;
  GstVideoFormat fmt;
  gboolean ret = TRUE;
  guint n, num;
  int w, h;

  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_COMPOSITION (comp), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (video_buf), FALSE);
  g_return_val_if_fail (gst_buffer_is_writable (video_buf), FALSE);
  g_return_val_if_fail (GST_BUFFER_CAPS (video_buf) != NULL, FALSE);

  if (!gst_video_format_parse_caps (GST_BUFFER_CAPS (video_buf), &fmt, &w, &h)) {
    gchar *str = gst_caps_to_string (GST_BUFFER_CAPS (video_buf));
    g_warning ("%s: could not parse video buffer caps '%s'", GST_FUNCTION, str);
    g_free (str);
    return FALSE;
  }

  video_blend_format_info_init (&video_info, GST_BUFFER_DATA (video_buf),
      h, w, fmt);

  num = comp->num_rectangles;
  GST_LOG ("Blending composition %p with %u rectangles onto video buffer %p "
      "(%ux%u, format %u)", comp, num, video_buf, w, h, fmt);

  for (n = 0; n < num; ++n) {
    GstVideoOverlayRectangle *rect;
    gboolean needs_scaling;

    rect = comp->rectangles[n];

    GST_LOG (" rectangle %u %p: %ux%u, format %u", n, rect, rect->height,
        rect->width, rect->format);

    video_blend_format_info_init (&rectangle_info,
        GST_BUFFER_DATA (rect->pixels), rect->height, rect->width,
        rect->format);

    needs_scaling = gst_video_overlay_rectangle_needs_scaling (rect);
    if (needs_scaling) {
      video_blend_scale_linear_RGBA (&rectangle_info, rect->render_height,
          rect->render_width);
    }

    ret = video_blend (&video_info, &rectangle_info, rect->x, rect->y);
    if (!ret) {
      GST_WARNING ("Could not blend overlay rectangle onto video buffer");
    }

    /* FIXME: should cache scaled pixels in the rectangle struct */
    if (needs_scaling)
      g_free (rectangle_info.pixels);
  }

  return ret;
}

/**
 * gst_video_overlay_composition_copy:
 * @comp: (transfer none): a #GstVideoOverlayComposition to copy
 *
 * Makes a copy of @comp and all contained rectangles, so that it is possible
 * to modify the composition and contained rectangles (e.g. add additional
 * rectangles or change the render co-ordinates or render dimension). The
 * actual overlay pixel data buffers contained in the rectangles are not
 * copied.
 *
 * Returns: (transfer full): a new #GstVideoOverlayComposition equivalent
 *     to @comp.
 *
 * Since: 0.10.36
 */
GstVideoOverlayComposition *
gst_video_overlay_composition_copy (GstVideoOverlayComposition * comp)
{
  GstVideoOverlayComposition *copy;
  GstVideoOverlayRectangle *rect;
  guint n;

  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_COMPOSITION (comp), NULL);

  if (G_LIKELY (comp->num_rectangles == 0))
    return gst_video_overlay_composition_new (NULL);

  rect = gst_video_overlay_rectangle_copy (comp->rectangles[0]);
  copy = gst_video_overlay_composition_new (rect);
  gst_video_overlay_rectangle_unref (rect);

  for (n = 1; n < comp->num_rectangles; ++n) {
    rect = gst_video_overlay_rectangle_copy (comp->rectangles[n]);
    gst_video_overlay_composition_add_rectangle (copy, rect);
    gst_video_overlay_rectangle_unref (rect);
  }

  return copy;
}

/**
 * gst_video_overlay_composition_make_writable:
 * @comp: (transfer full): a #GstVideoOverlayComposition to copy
 *
 * Takes ownership of @comp and returns a version of @comp that is writable
 * (i.e. can be modified). Will either return @comp right away, or create a
 * new writable copy of @comp and unref @comp itself. All the contained
 * rectangles will also be copied, but the actual overlay pixel data buffers
 * contained in the rectangles are not copied.
 *
 * Returns: (transfer full): a writable #GstVideoOverlayComposition
 *     equivalent to @comp.
 *
 * Since: 0.10.36
 */
GstVideoOverlayComposition *
gst_video_overlay_composition_make_writable (GstVideoOverlayComposition * comp)
{
  GstVideoOverlayComposition *writable_comp;

  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_COMPOSITION (comp), NULL);

  if (GST_MINI_OBJECT_REFCOUNT_VALUE (comp) == 1) {
    guint n;

    for (n = 0; n < comp->num_rectangles; ++n) {
      if (GST_MINI_OBJECT_REFCOUNT_VALUE (comp->rectangles[n]) != 1)
        goto copy;
    }
    return comp;
  }

copy:

  writable_comp = gst_video_overlay_composition_copy (comp);
  gst_video_overlay_composition_unref (comp);

  return writable_comp;
}

/**
 * gst_video_overlay_composition_get_seqnum:
 * @comp: a #GstVideoOverlayComposition
 *
 * Returns the sequence number of this composition. Sequence numbers are
 * monotonically increasing and unique for overlay compositions and rectangles
 * (meaning there will never be a rectangle with the same sequence number as
 * a composition).
 *
 * Returns: the sequence number of @comp
 *
 * Since: 0.10.36
 */
guint
gst_video_overlay_composition_get_seqnum (GstVideoOverlayComposition * comp)
{
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_COMPOSITION (comp), 0);

  return comp->seq_num;
}

/* ------------------------------ rectangles ------------------------------ -*/

static void gst_video_overlay_rectangle_instance_init (GstMiniObject * miniobj);

GType
gst_video_overlay_rectangle_get_type (void)
{
  static volatile gsize type_id = 0;

  if (g_once_init_enter (&type_id)) {
    GType new_type_id = g_type_register_static_simple (GST_TYPE_MINI_OBJECT,
        g_intern_static_string ("GstVideoOverlayRectangle"),
        sizeof (GstVideoOverlayRectangleClass),
        (GClassInitFunc) gst_video_overlay_rectangle_class_init,
        sizeof (GstVideoOverlayRectangle),
        (GInstanceInitFunc) gst_video_overlay_rectangle_instance_init,
        (GTypeFlags) 0);

    g_once_init_leave (&type_id, new_type_id);
  }

  return type_id;
}

static void
gst_video_overlay_rectangle_finalize (GstMiniObject * mini_obj)
{
  GstVideoOverlayRectangle *rect = (GstVideoOverlayRectangle *) mini_obj;

  gst_buffer_replace (&rect->pixels, NULL);

  while (rect->scaled_rectangles != NULL) {
    GstVideoOverlayRectangle *scaled_rect = rect->scaled_rectangles->data;

    gst_video_overlay_rectangle_unref (scaled_rect);

    rect->scaled_rectangles =
        g_list_delete_link (rect->scaled_rectangles, rect->scaled_rectangles);
  }
#if !GLIB_CHECK_VERSION (2, 31, 0)
  g_static_mutex_free (&rect->lock);
#else
  g_mutex_clear (&rect->lock);
#endif
  /* not chaining up to GstMiniObject's finalize for now, we know it's empty */
}

static void
gst_video_overlay_rectangle_class_init (GstMiniObjectClass * klass)
{
  klass->finalize = gst_video_overlay_rectangle_finalize;
  klass->copy = (GstMiniObjectCopyFunction) gst_video_overlay_rectangle_copy;
}

static void
gst_video_overlay_rectangle_instance_init (GstMiniObject * mini_obj)
{
  GstVideoOverlayRectangle *rect = (GstVideoOverlayRectangle *) mini_obj;

#if !GLIB_CHECK_VERSION (2, 31, 0)
  g_static_mutex_init (&rect->lock);
#else
  g_mutex_init (&rect->lock);
#endif
}

/**
 * gst_video_overlay_rectangle_new_argb:
 * @pixels: (transfer none): a #GstBuffer pointing to the pixel memory
 * @width: the width of the rectangle in @pixels
 * @height: the height of the rectangle in @pixels
 * @stride: the stride of the rectangle in @pixels in bytes (&gt;= 4*width)
 * @x: the X co-ordinate on the video where the top-left corner of this
 *     overlay rectangle should be rendered to
 * @y: the Y co-ordinate on the video where the top-left corner of this
 *     overlay rectangle should be rendered to
 * @render_width: the render width of this rectangle on the video
 * @render_height: the render height of this rectangle on the video
 * @flags: flags (currently unused)
 *
 * Creates a new video overlay rectangle with ARGB pixel data. The layout
 * of the components in memory is B-G-R-A on little-endian platforms
 * (corresponding to #GST_VIDEO_FORMAT_BGRA) and A-R-G-B on big-endian
 * platforms (corresponding to #GST_VIDEO_FORMAT_ARGB). In other words,
 * pixels are treated as 32-bit words and the lowest 8 bits then contain
 * the blue component value and the highest 8 bits contain the alpha
 * component value. The RGB values are non-premultiplied. This is the
 * format that is used by most hardware, and also many rendering libraries
 * such as Cairo, for example.
 *
 * Returns: (transfer full): a new #GstVideoOverlayRectangle. Unref with
 *     gst_video_overlay_rectangle_unref() when no longer needed.
 *
 * Since: 0.10.36
 */
GstVideoOverlayRectangle *
gst_video_overlay_rectangle_new_argb (GstBuffer * pixels,
    guint width, guint height, guint stride, gint render_x, gint render_y,
    guint render_width, guint render_height, GstVideoOverlayFormatFlags flags)
{
  GstVideoOverlayRectangle *rect;

  g_return_val_if_fail (GST_IS_BUFFER (pixels), NULL);
  /* technically ((height-1)*stride)+width might be okay too */
  g_return_val_if_fail (GST_BUFFER_SIZE (pixels) >= height * stride, NULL);
  g_return_val_if_fail (stride >= (4 * width), NULL);
  g_return_val_if_fail (height > 0 && width > 0, NULL);
  g_return_val_if_fail (render_height > 0 && render_width > 0, NULL);
  g_return_val_if_fail (flags == 0, NULL);

  rect = (GstVideoOverlayRectangle *)
      gst_mini_object_new (GST_TYPE_VIDEO_OVERLAY_RECTANGLE);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  rect->format = GST_VIDEO_FORMAT_BGRA;
#else
  rect->format = GST_VIDEO_FORMAT_ARGB;
#endif
  rect->pixels = gst_buffer_ref (pixels);

  rect->width = width;
  rect->height = height;
  rect->stride = stride;

  rect->x = render_x;
  rect->y = render_y;
  rect->render_width = render_width;
  rect->render_height = render_height;

  rect->seq_num = gst_video_overlay_get_seqnum ();

  GST_LOG ("new rectangle %p: %ux%u => %ux%u @ %u,%u, seq_num %u, format %u, "
      "pixels %p", rect, width, height, render_width, render_height, render_x,
      render_y, rect->seq_num, rect->format, pixels);

  return rect;
}

/**
 * gst_video_overlay_rectangle_get_render_rectangle:
 * @rectangle: a #GstVideoOverlayRectangle
 * @render_x: (out) (allow-none): address where to store the X render offset
 * @render_y: (out) (allow-none): address where to store the Y render offset
 * @render_width: (out) (allow-none): address where to store the render width
 * @render_height: (out) (allow-none): address where to store the render height
 *
 * Retrieves the render position and render dimension of the overlay
 * rectangle on the video.
 *
 * Returns: TRUE if valid render dimensions were retrieved.
 *
 * Since: 0.10.36
 */
gboolean
gst_video_overlay_rectangle_get_render_rectangle (GstVideoOverlayRectangle *
    rectangle, gint * render_x, gint * render_y, guint * render_width,
    guint * render_height)
{
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle), FALSE);

  if (render_x)
    *render_x = rectangle->x;
  if (render_y)
    *render_y = rectangle->y;
  if (render_width)
    *render_width = rectangle->render_width;
  if (render_height)
    *render_height = rectangle->render_height;

  return TRUE;
}

/**
 * gst_video_overlay_rectangle_set_render_rectangle:
 * @rectangle: a #GstVideoOverlayRectangle
 * @render_x: render X position of rectangle on video
 * @render_y: render Y position of rectangle on video
 * @render_width: render width of rectangle
 * @render_height: render height of rectangle
 *
 * Sets the render position and dimensions of the rectangle on the video.
 * This function is mainly for elements that modify the size of the video
 * in some way (e.g. through scaling or cropping) and need to adjust the
 * details of any overlays to match the operation that changed the size.
 *
 * @rectangle must be writable, meaning its refcount must be 1. You can
 * make the rectangles inside a #GstVideoOverlayComposition writable using
 * gst_video_overlay_composition_make_writable() or
 * gst_video_overlay_composition_copy().
 *
 * Since: 0.10.36
 */
void
gst_video_overlay_rectangle_set_render_rectangle (GstVideoOverlayRectangle *
    rectangle, gint render_x, gint render_y, guint render_width,
    guint render_height)
{
  g_return_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle));
  g_return_if_fail (GST_MINI_OBJECT_REFCOUNT_VALUE (rectangle) == 1);

  rectangle->x = render_x;
  rectangle->y = render_y;
  rectangle->render_width = render_width;
  rectangle->render_height = render_height;
}

/**
 * gst_video_overlay_rectangle_get_pixels_argb:
 * @rectangle: a #GstVideoOverlayRectangle
 * @stride: (out) (allow-none): address of guint variable where to store the
 *    row stride of the ARGB pixel data in the buffer
 * @flags: flags (unused)
 *
 * Returns: (transfer none): a #GstBuffer holding the ARGB pixel data with
 *    row stride @stride and width and height of the render dimensions as per
 *    gst_video_overlay_rectangle_get_render_rectangle(). This function does
 *    not return a reference, the caller should obtain a reference of her own
 *    with gst_buffer_ref() if needed.
 *
 * Since: 0.10.36
 */
GstBuffer *
gst_video_overlay_rectangle_get_pixels_argb (GstVideoOverlayRectangle *
    rectangle, guint * stride, GstVideoOverlayFormatFlags flags)
{
  GstVideoOverlayRectangle *scaled_rect = NULL;
  GstBlendVideoFormatInfo info;
  GstBuffer *buf;
  GList *l;

  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle), NULL);
  g_return_val_if_fail (flags == 0, NULL);
  g_return_val_if_fail (stride != NULL, NULL);

  /* This assumes we don't need to adjust the format */
  if (rectangle->render_width == rectangle->width &&
      rectangle->render_height == rectangle->height) {
    *stride = rectangle->stride;
    return rectangle->pixels;
  }

  /* see if we've got one cached already */
  GST_RECTANGLE_LOCK (rectangle);
  for (l = rectangle->scaled_rectangles; l != NULL; l = l->next) {
    GstVideoOverlayRectangle *r = l->data;

    if (r->width == rectangle->render_width &&
        r->height == rectangle->render_height) {
      /* we'll keep these rectangles around until finalize, so it's ok not
       * to take our own ref here */
      scaled_rect = r;
      break;
    }
  }
  GST_RECTANGLE_UNLOCK (rectangle);

  if (scaled_rect != NULL)
    goto done;

  /* not cached yet, do the scaling and put the result into our cache */
  video_blend_format_info_init (&info, GST_BUFFER_DATA (rectangle->pixels),
      rectangle->height, rectangle->width, rectangle->format);

  video_blend_scale_linear_RGBA (&info, rectangle->render_height,
      rectangle->render_width);

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = info.pixels;
  GST_BUFFER_MALLOCDATA (buf) = info.pixels;
  GST_BUFFER_SIZE (buf) = info.size;

  scaled_rect = gst_video_overlay_rectangle_new_argb (buf,
      rectangle->render_width, rectangle->render_height, info.stride[0],
      0, 0, rectangle->render_width, rectangle->render_height, 0);

  gst_buffer_unref (buf);

  GST_RECTANGLE_LOCK (rectangle);
  rectangle->scaled_rectangles =
      g_list_prepend (rectangle->scaled_rectangles, scaled_rect);
  GST_RECTANGLE_UNLOCK (rectangle);

done:

  *stride = scaled_rect->stride;
  return scaled_rect->pixels;
}

/**
 * gst_video_overlay_rectangle_get_pixels_unscaled_argb:
 * @rectangle: a #GstVideoOverlayRectangle
 * @width: (out): address where to store the width of the unscaled
 *    rectangle in pixels
 * @width: (out): address where to store the height of the unscaled
 *    rectangle in pixels
 * @stride: (out): address of guint variable where to store the row
 *    stride of the ARGB pixel data in the buffer
 * @flags: flags for future use (unused)
 *
 * Retrieves the pixel data as it is. This is useful if the caller can
 * do the scaling itself when handling the overlaying. The rectangle will
 * need to be scaled to the render dimensions, which can be retrieved using
 * gst_video_overlay_rectangle_get_render_rectangle().
 *
 * Returns: (transfer none): a #GstBuffer holding the ARGB pixel data with
 *    row stride @stride. This function does not return a reference, the caller
 *    should obtain a reference of her own with gst_buffer_ref() if needed.
 *
 * Since: 0.10.36
 */
GstBuffer *
gst_video_overlay_rectangle_get_pixels_unscaled_argb (GstVideoOverlayRectangle *
    rectangle, guint * width, guint * height, guint * stride,
    GstVideoOverlayFormatFlags flags)
{
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle), NULL);
  g_return_val_if_fail (width != NULL, NULL);
  g_return_val_if_fail (height != NULL, NULL);
  g_return_val_if_fail (stride != NULL, NULL);
  g_return_val_if_fail (flags == 0, NULL);

  *width = rectangle->width;
  *height = rectangle->height;
  *stride = rectangle->stride;

  return rectangle->pixels;
}

/**
 * gst_video_overlay_rectangle_copy:
 * @rectangle: (transfer none): a #GstVideoOverlayRectangle to copy
 *
 * Makes a copy of @rectangle, so that it is possible to modify it
 * (e.g. to change the render co-ordinates or render dimension). The
 * actual overlay pixel data buffers contained in the rectangle are not
 * copied.
 *
 * Returns: (transfer full): a new #GstVideoOverlayRectangle equivalent
 *     to @rectangle.
 *
 * Since: 0.10.36
 */
GstVideoOverlayRectangle *
gst_video_overlay_rectangle_copy (GstVideoOverlayRectangle * rectangle)
{
  GstVideoOverlayRectangle *copy;

  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle), NULL);

  copy = gst_video_overlay_rectangle_new_argb (rectangle->pixels,
      rectangle->width, rectangle->height, rectangle->stride,
      rectangle->x, rectangle->y,
      rectangle->render_width, rectangle->render_height, 0);

  return copy;
}

/**
 * gst_video_overlay_rectangle_get_seqnum:
 * @rectangle: a #GstVideoOverlayRectangle
 *
 * Returns the sequence number of this rectangle. Sequence numbers are
 * monotonically increasing and unique for overlay compositions and rectangles
 * (meaning there will never be a rectangle with the same sequence number as
 * a composition).
 *
 * Returns: the sequence number of @rectangle
 *
 * Since: 0.10.36
 */
guint
gst_video_overlay_rectangle_get_seqnum (GstVideoOverlayRectangle * rectangle)
{
  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rectangle), 0);

  return rectangle->seq_num;
}
