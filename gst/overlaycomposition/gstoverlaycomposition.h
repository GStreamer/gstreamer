/* GStreamer
 * Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
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

#include <gst/gst.h>
#include <gst/video/video.h>

#ifndef __GST_OVERLAY_COMPOSITION_H__
#define __GST_OVERLAY_COMPOSITION_H__

G_BEGIN_DECLS

#define GST_TYPE_OVERLAY_COMPOSITION \
  (gst_overlay_composition_get_type())
#define GST_OVERLAY_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OVERLAY_COMPOSITION,GstOverlayComposition))
#define GST_OVERLAY_COMPOSITION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OVERLAY_COMPOSITION,GstOverlayCompositionClass))
#define GST_IS_OVERLAY_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OVERLAY_COMPOSITION))
#define GST_IS_OVERLAY_COMPOSITION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OVERLAY_COMPOSITION))

typedef struct _GstOverlayComposition GstOverlayComposition;
typedef struct _GstOverlayCompositionClass GstOverlayCompositionClass;

struct _GstOverlayComposition {
  GstElement parent;

  GstPad *sinkpad, *srcpad;

  /* state */
  GstSample *sample;
  GstSegment segment;
  GstCaps *caps;
  GstVideoInfo info;
  guint window_width, window_height;
  gboolean attach_compo_to_buffer;
};

struct _GstOverlayCompositionClass {
  GstElementClass parent_class;
};

GType gst_overlay_composition_get_type (void);

G_END_DECLS

#endif /* __GST_OVERLAY_COMPOSITION_H__ */
