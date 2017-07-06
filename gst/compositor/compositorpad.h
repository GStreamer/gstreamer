/* Generic compositor plugin pad
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_COMPOSITOR_PAD_H__
#define __GST_COMPOSITOR_PAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_COMPOSITOR_PAD (gst_compositor_pad_get_type())
#define GST_COMPOSITOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COMPOSITOR_PAD, GstCompositorPad))
#define GST_COMPOSITOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COMPOSITOR_PAD, GstCompositorPadClass))
#define GST_IS_COMPOSITOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COMPOSITOR_PAD))
#define GST_IS_COMPOSITOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COMPOSITOR_PAD))

typedef struct _GstCompositorPad GstCompositorPad;
typedef struct _GstCompositorPadClass GstCompositorPadClass;

/**
 * GstCompositorPad:
 *
 * The opaque #GstCompositorPad structure.
 */
struct _GstCompositorPad
{
  GstVideoAggregatorPad parent;

  /* properties */
  gint xpos, ypos;
  gint width, height;
  gdouble alpha;
  gdouble crossfade;

  GstVideoConverter *convert;
  GstVideoInfo conversion_info;
  GstBuffer *converted_buffer;

  gboolean crossfaded;
};

struct _GstCompositorPadClass
{
  GstVideoAggregatorPadClass parent_class;
};

GType gst_compositor_pad_get_type (void);

G_END_DECLS
#endif /* __GST_COMPOSITOR_PAD_H__ */
