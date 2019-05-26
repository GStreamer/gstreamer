/* Generic video compositor plugin
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

#ifndef __GST_COMPOSITOR_H__
#define __GST_COMPOSITOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>

#include "blend.h"

G_BEGIN_DECLS

#define GST_TYPE_COMPOSITOR (gst_compositor_get_type())
#define GST_COMPOSITOR(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COMPOSITOR, GstCompositor))
#define GST_COMPOSITOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COMPOSITOR, GstCompositorClass))
#define GST_IS_COMPOSITOR(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COMPOSITOR))
#define GST_IS_COMPOSITOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COMPOSITOR))

#define GST_TYPE_COMPOSITOR_PAD (gst_compositor_pad_get_type())
#define GST_COMPOSITOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COMPOSITOR_PAD, GstCompositorPad))
#define GST_COMPOSITOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COMPOSITOR_PAD, GstCompositorPadClass))
#define GST_IS_COMPOSITOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COMPOSITOR_PAD))
#define GST_IS_COMPOSITOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COMPOSITOR_PAD))

typedef struct _GstCompositor GstCompositor;
typedef struct _GstCompositorClass GstCompositorClass;

typedef struct _GstCompositorPad GstCompositorPad;
typedef struct _GstCompositorPadClass GstCompositorPadClass;

/**
 * GstCompositorBackground:
 * @COMPOSITOR_BACKGROUND_CHECKER: checker pattern background
 * @COMPOSITOR_BACKGROUND_BLACK: solid color black background
 * @COMPOSITOR_BACKGROUND_WHITE: solid color white background
 * @COMPOSITOR_BACKGROUND_TRANSPARENT: background is left transparent and layers are composited using "A OVER B" composition rules. This is only applicable to AYUV and ARGB (and variants) as it preserves the alpha channel and allows for further mixing.
 *
 * The different backgrounds compositor can blend over.
 */
typedef enum
{
  COMPOSITOR_BACKGROUND_CHECKER,
  COMPOSITOR_BACKGROUND_BLACK,
  COMPOSITOR_BACKGROUND_WHITE,
  COMPOSITOR_BACKGROUND_TRANSPARENT,
} GstCompositorBackground;

/**
 * GstCompositorOperator:
 * @COMPOSITOR_OPERATOR_SOURCE: Copy the source over the destination,
 *                              without the destination pixels.
 * @COMPOSITOR_OPERATOR_OVER: Blend the source over the destination.
 * @COMPOSITOR_OPERATOR_ADD: Similar to over but add the source and
 *                           destination alpha. Requires output with alpha
 *                           channel.
 *
 * The different blending operators that can be used by compositor.
 *
 * See https://www.cairographics.org/operators/ for some explanation and
 * visualizations.
 *
 */
typedef enum
{
  COMPOSITOR_OPERATOR_SOURCE,
  COMPOSITOR_OPERATOR_OVER,
  COMPOSITOR_OPERATOR_ADD,
} GstCompositorOperator;

/**
 * GstCompositor:
 *
 * The opaque #GstCompositor structure.
 */
struct _GstCompositor
{
  GstVideoAggregator videoaggregator;
  GstCompositorBackground background;

  /* The 'blend' compositing function does not preserve the alpha value of the
   * background, while 'overlay' does; i.e., COMPOSITOR_OPERATOR_ADD is the
   * same as COMPOSITOR_OPERATOR_OVER when using the 'blend' BlendFunction. */
  BlendFunction blend, overlay;
  FillCheckerFunction fill_checker;
  FillColorFunction fill_color;
};

struct _GstCompositorClass
{
  GstVideoAggregatorClass parent_class;
};

/**
 * GstCompositorPad:
 *
 * The opaque #GstCompositorPad structure.
 */
struct _GstCompositorPad
{
  GstVideoAggregatorConvertPad parent;

  /* properties */
  gint xpos, ypos;
  gint width, height;
  gdouble alpha;

  GstCompositorOperator op;
};

struct _GstCompositorPadClass
{
  GstVideoAggregatorConvertPadClass parent_class;
};

GType gst_compositor_get_type (void);
GType gst_compositor_pad_get_type (void);

G_END_DECLS
#endif /* __GST_COMPOSITOR_H__ */
