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

#ifndef __GST_VIPS_ENUMTYPES_H__
#define __GST_VIPS_ENUMTYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_VIPS_INTERESTING (gst_vips_interesting_get_type ())

/**
 * GstVipsInteresting:
 * @GST_VIPS_INTERESTING_NONE: Crop from the top left.
 * @GST_VIPS_INTERESTING_CENTRE: Crop from the centre.
 * @GST_VIPS_INTERESTING_ENTROPY: Crop from the highest-entropy area. Requires
 * two memcpy's per buffer because of the libvips API.
 * @GST_VIPS_INTERESTING_ATTENTION: Crop from the area most likely to attract
 * human attention (edges, skin colours, saturated areas).
 * @GST_VIPS_INTERESTING_LOW: Crop from the top left.
 * @GST_VIPS_INTERESTING_HIGH: Crop from the bottom right.
 *
 * How to measure interestingness when smart-cropping an image.
 */
typedef enum
{
  GST_VIPS_INTERESTING_NONE,
  GST_VIPS_INTERESTING_CENTRE,
  GST_VIPS_INTERESTING_ENTROPY,
  GST_VIPS_INTERESTING_ATTENTION,
  GST_VIPS_INTERESTING_LOW,
  GST_VIPS_INTERESTING_HIGH,
} GstVipsInteresting;

GType gst_vips_interesting_get_type (void);

G_END_DECLS

#endif /* __GST_VIPS_ENUMTYPES_H__ */
