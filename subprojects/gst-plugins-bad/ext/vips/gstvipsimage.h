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

#ifndef __GST_VIPS_IMAGE_H__
#define __GST_VIPS_IMAGE_H__

#include <gst/video/video.h>
#include <vips/vips.h>

G_BEGIN_DECLS

/**
 * GST_VIPS_FORMATS:
 *
 * The video formats supported by the vips plugin. Only formats that map
 * natively to a libvips memory image are supported: the packed memory
 * layout is used as-is, without any pixel format conversion.
 */
#define GST_VIPS_FORMATS "RGB, RGBA, GRAY8"

/**
 * GST_VIPS_VIDEO_CAPS:
 *
 * The caps of the vips plugin.
 */
#define GST_VIPS_VIDEO_CAPS \
  "video/x-raw, format = (string) {" GST_VIPS_FORMATS "}, " \
  "width = " GST_VIDEO_SIZE_RANGE ", " \
  "height = " GST_VIDEO_SIZE_RANGE

/**
 * GstVipsError:
 * @GST_VIPS_ERROR_UNKNOWN: Unknown error.
 * @GST_VIPS_ERROR_UNSUPPORTED_FORMAT: The video format is not supported.
 * @GST_VIPS_ERROR_VIPS: libvips returned an error.
 *
 * Errors returned by the vips plugin.
 */
typedef enum
{
  GST_VIPS_ERROR_UNKNOWN,
  GST_VIPS_ERROR_UNSUPPORTED_FORMAT,
  GST_VIPS_ERROR_VIPS,
} GstVipsError;

GQuark gst_vips_error_quark (void);

/**
 * gst_vips_image_new_from_frame:
 * @frame: a video frame
 * @image: (out): return location for the new image
 * @error: return location for an error
 *
 * Creates a VipsImage from a video frame. The image references the frame
 * data directly unless the frame has pitch padding, in which case a packed
 * copy is made.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 */
gboolean gst_vips_image_new_from_frame (GstVideoFrame * frame,
    VipsImage ** image, GError ** error);

/**
 * gst_vips_image_copy_to_frame:
 * @image: a VipsImage
 * @frame: a video frame
 * @error: return location for an error
 *
 * Copies the pixels of a VipsImage to a video frame.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 */
gboolean gst_vips_image_copy_to_frame (VipsImage * image,
    GstVideoFrame * frame, GError ** error);

G_END_DECLS

#endif /* __GST_VIPS_IMAGE_H__ */
