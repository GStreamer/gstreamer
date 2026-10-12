/* GStreamer vips plugin
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

#ifndef __GST_VIPS_SMARTCROP_H__
#define __GST_VIPS_SMARTCROP_H__

#include <gst/video/video.h>
#include "gstvipsenumtypes.h"

G_BEGIN_DECLS

#define GST_TYPE_VIPS_SMARTCROP (gst_vips_smartcrop_get_type ())
#define GST_VIPS_SMARTCROP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIPS_SMARTCROP,GstVipsSmartcrop))
#define GST_VIPS_SMARTCROP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_CAST((obj) ,GST_TYPE_VIPS_SMARTCROP,GstVipsSmartcropClass))
#define GST_IS_VIPS_SMARTCROP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj) ,GST_TYPE_VIPS_SMARTCROP))
#define GST_IS_VIPS_SMARTCROP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((obj),GST_TYPE_VIPS_SMARTCROP))

typedef struct _GstVipsSmartcrop GstVipsSmartcrop;
typedef struct _GstVipsSmartcropClass GstVipsSmartcropClass;

struct _GstVipsSmartcrop
{
  GstVideoFilter element;

  GstVipsInteresting interesting;

  /* The centre of attention found by the last smart crop, if any. */
  gint attention_x;
  gint attention_y;

  /* Whether the buffer currently being transformed is a zero-copy view of
   * the input buffer (set by prepare_output_buffer). */
  gboolean zero_copy;

  /* Whether the element below the src pad accepts video meta (set by
   * decide_allocation). The zero-copy crop requires it. */
  gboolean downstream_video_meta;
};

struct _GstVipsSmartcropClass
{
  GstVideoFilterClass parent_class;
};

GType gst_vips_smartcrop_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (vips_smartcrop);

G_END_DECLS

#endif /* __GST_VIPS_SMARTCROP_H__ */
