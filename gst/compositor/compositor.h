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

typedef struct _GstCompositor GstCompositor;
typedef struct _GstCompositorClass GstCompositorClass;

/**
 * GstcompositorBackground:
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
}
GstCompositorBackground;

/**
 * GstCompositor:
 *
 * The opaque #GstCompositor structure.
 */
struct _GstCompositor
{
  GstVideoAggregator videoaggregator;
  GstCompositorBackground background;

  BlendFunction blend, overlay;
  FillCheckerFunction fill_checker;
  FillColorFunction fill_color;
};

struct _GstCompositorClass
{
  GstVideoAggregatorClass parent_class;
};

GType gst_compositor_get_type (void);

G_END_DECLS
#endif /* __GST_COMPOSITOR_H__ */
