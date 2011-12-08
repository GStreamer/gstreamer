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

#ifndef __GST_VIDEO_OVERLAY_COMPOSITION_H__
#define __GST_VIDEO_OVERLAY_COMPOSITION_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GstVideoOverlayRectangle:
 *
 * An opaque video overlay rectangle object. A rectangle contains a single
 * overlay rectangle which can be added to a composition.
 *
 * Since: 0.10.36
 */
#define GST_TYPE_VIDEO_OVERLAY_RECTANGLE			\
  (gst_video_overlay_rectangle_get_type ())
#define GST_VIDEO_OVERLAY_RECTANGLE(obj)			\
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_OVERLAY_RECTANGLE, GstVideoOverlayRectangle))
#define GST_IS_VIDEO_OVERLAY_RECTANGLE(obj)			\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_OVERLAY_RECTANGLE))

typedef struct _GstVideoOverlayRectangle      GstVideoOverlayRectangle;
typedef struct _GstVideoOverlayRectangleClass GstVideoOverlayRectangleClass;

/**
 * gst_video_overlay_rectangle_ref:
 * @comp: a a #GstVideoOverlayRectangle.
 *
 * Increases the refcount of the given rectangle by one.
 *
 * Note that the refcount affects the writeability
 * of @comp, use gst_video_overlay_rectangle_copy() to ensure a rectangle can
 * be modified (there is no gst_video_overlay_rectangle_make_writable() because
 * it is unlikely that someone will hold the single reference to the rectangle
 * and not know that that's the case).
 *
 * Returns: (transfer full): @comp
 *
 * Since: 0.10.36
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstVideoOverlayRectangle *
gst_video_overlay_rectangle_ref (GstVideoOverlayRectangle * comp);
#endif

static inline GstVideoOverlayRectangle *
gst_video_overlay_rectangle_ref (GstVideoOverlayRectangle * comp)
{
  return (GstVideoOverlayRectangle *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (comp));
}

/**
 * gst_video_overlay_rectangle_unref:
 * @comp: (transfer full): a #GstVideoOverlayRectangle.
 *
 * Decreases the refcount of the rectangle. If the refcount reaches 0, the
 * rectangle will be freed.
 *
 * Since: 0.10.36
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC void
gst_video_overlay_rectangle_unref (GstVideoOverlayRectangle * comp);
#endif

static inline void
gst_video_overlay_rectangle_unref (GstVideoOverlayRectangle * comp)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (comp));
}

/**
 * GstVideoOverlayFormatFlags:
 * @GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE: no flags
 *
 * Overlay format flags.
 *
 * Since: 0.10.36
 */
typedef enum {
  GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE = 0
} GstVideoOverlayFormatFlags;

GType                        gst_video_overlay_rectangle_get_type (void);

GstVideoOverlayRectangle *   gst_video_overlay_rectangle_new_argb (GstBuffer * pixels,
                                                                   guint width, guint height, guint stride,
                                                                   gint  render_x, gint render_y,
                                                                   guint render_width, guint render_height,
                                                                   GstVideoOverlayFormatFlags flags);

GstVideoOverlayRectangle *   gst_video_overlay_rectangle_copy     (GstVideoOverlayRectangle * rectangle);

guint                        gst_video_overlay_rectangle_get_seqnum (GstVideoOverlayRectangle  * rectangle);

void                         gst_video_overlay_rectangle_set_render_rectangle     (GstVideoOverlayRectangle  * rectangle,
                                                                                   gint                        render_x,
                                                                                   gint                        render_y,
                                                                                   guint                       render_width,
                                                                                   guint                       render_height);

gboolean                     gst_video_overlay_rectangle_get_render_rectangle     (GstVideoOverlayRectangle  * rectangle,
                                                                                   gint                      * render_x,
                                                                                   gint                      * render_y,
                                                                                   guint                     * render_width,
                                                                                   guint                     * render_height);

GstBuffer *                  gst_video_overlay_rectangle_get_pixels_argb          (GstVideoOverlayRectangle  * rectangle,
                                                                                   guint                     * stride,
                                                                                   GstVideoOverlayFormatFlags  flags);

GstBuffer *                  gst_video_overlay_rectangle_get_pixels_unscaled_argb (GstVideoOverlayRectangle  * rectangle,
                                                                                   guint                     * width,
                                                                                   guint                     * height,
                                                                                   guint                     * stride,
                                                                                   GstVideoOverlayFormatFlags  flags);

/**
 * GstVideoOverlayComposition:
 *
 * An opaque video overlay composition object. A composition contains
 * multiple overlay rectangles.
 *
 * Since: 0.10.36
 */
#define GST_TYPE_VIDEO_OVERLAY_COMPOSITION			\
  (gst_video_overlay_composition_get_type ())
#define GST_VIDEO_OVERLAY_COMPOSITION(obj)			\
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_OVERLAY_COMPOSITION, GstVideoOverlayComposition))
#define GST_IS_VIDEO_OVERLAY_COMPOSITION(obj)			\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_OVERLAY_COMPOSITION))

typedef struct _GstVideoOverlayComposition      GstVideoOverlayComposition;
typedef struct _GstVideoOverlayCompositionClass GstVideoOverlayCompositionClass;

/**
 * gst_video_overlay_composition_ref:
 * @comp: a a #GstVideoOverlayComposition.
 *
 * Increases the refcount of the given composition by one.
 *
 * Note that the refcount affects the writeability
 * of @comp, use gst_video_overlay_composition_make_writable() to ensure
 * a composition and its rectangles can be modified.
 *
 * Returns: (transfer full): @comp
 *
 * Since: 0.10.36
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstVideoOverlayComposition *
gst_video_overlay_composition_ref (GstVideoOverlayComposition * comp);
#endif

static inline GstVideoOverlayComposition *
gst_video_overlay_composition_ref (GstVideoOverlayComposition * comp)
{
  return (GstVideoOverlayComposition *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (comp));
}

/**
 * gst_video_overlay_composition_unref:
 * @comp: (transfer full): a #GstVideoOverlayComposition.
 *
 * Decreases the refcount of the composition. If the refcount reaches 0, the
 * composition will be freed.
 *
 * Since: 0.10.36
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC void
gst_video_overlay_composition_unref (GstVideoOverlayComposition * comp);
#endif

static inline void
gst_video_overlay_composition_unref (GstVideoOverlayComposition * comp)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (comp));
}

GType                        gst_video_overlay_composition_get_type (void);

GstVideoOverlayComposition * gst_video_overlay_composition_copy          (GstVideoOverlayComposition * comp);

GstVideoOverlayComposition * gst_video_overlay_composition_make_writable (GstVideoOverlayComposition * comp);

GstVideoOverlayComposition * gst_video_overlay_composition_new           (GstVideoOverlayRectangle * rectangle);

void                         gst_video_overlay_composition_add_rectangle (GstVideoOverlayComposition * comp,
                                                                          GstVideoOverlayRectangle   * rectangle);

guint                        gst_video_overlay_composition_n_rectangles  (GstVideoOverlayComposition * comp);

GstVideoOverlayRectangle *   gst_video_overlay_composition_get_rectangle (GstVideoOverlayComposition * comp, guint n);

guint                        gst_video_overlay_composition_get_seqnum    (GstVideoOverlayComposition * comp);

/* blend composition onto raw video buffer */

gboolean                     gst_video_overlay_composition_blend         (GstVideoOverlayComposition * comp,
                                                                          GstBuffer                  * video_buf);

/* attach/retrieve composition from buffers */

void                         gst_video_buffer_set_overlay_composition (GstBuffer                  * buf,
                                                                       GstVideoOverlayComposition * comp);

GstVideoOverlayComposition * gst_video_buffer_get_overlay_composition (GstBuffer * buf);

G_END_DECLS

#endif /* __GST_VIDEO_OVERLAY_COMPOSITION_H__ */
