/*
 * GStreamer
 * Copyright (C) 2026 Vivia Nikolaidou <vivia@ahiru.eu>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvipsimage.h"

GQuark
gst_vips_error_quark (void)
{
  return g_quark_from_static_string ("org-gstreamer-vips-error");
}

/* The number of bands of the video format, or 0 if unsupported. */
static guint
gst_vips_image_get_bands (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
      return 3;
    case GST_VIDEO_FORMAT_RGBA:
      return 4;
    case GST_VIDEO_FORMAT_GRAY8:
      return 1;
    default:
      return 0;
  }
}

gboolean
gst_vips_image_new_from_frame (GstVideoFrame * frame, VipsImage ** image,
    GError ** error)
{
  const guint bands = gst_vips_image_get_bands (GST_VIDEO_FRAME_FORMAT (frame));
  const guint width = GST_VIDEO_FRAME_WIDTH (frame);
  const guint height = GST_VIDEO_FRAME_HEIGHT (frame);
  const gsize stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  const gsize packed_line = (gsize) width * bands;
  guint8 *data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  guint y;
  VipsImage *vips_image;

  if (G_UNLIKELY (bands == 0)) {
    g_set_error (error, gst_vips_error_quark (),
        GST_VIPS_ERROR_UNSUPPORTED_FORMAT, "unsupported video format %s",
        gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));
    return FALSE;
  }

  /* libvips memory images are packed: compact the frame if it has stride
   * padding. */
  if (G_UNLIKELY (stride != packed_line)) {
    guint8 *packed = g_malloc (packed_line * height);
    for (y = 0; y < height; y++)
      memcpy (packed + y * packed_line, data + y * stride, packed_line);
    data = packed;
  }

  vips_image = vips_image_new_from_memory (data, packed_line * height,
      width, height, bands, VIPS_FORMAT_UCHAR);
  if (G_UNLIKELY (vips_image == NULL)) {
    g_set_error (error, gst_vips_error_quark (), GST_VIPS_ERROR_VIPS,
        "failed to create vips image: %s", vips_error_buffer ());
    vips_error_clear ();
    if (data != (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0))
      g_free (data);
    return FALSE;
  }

  /* The packed copy is not owned by the VipsImage: free it on disposal. */
  if (data != (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0))
    g_object_set_data_full (G_OBJECT (vips_image), "gst-vips-owned",
        (GDestroyNotify) g_free, (gpointer) data);

  *image = vips_image;

  return TRUE;
}

gboolean
gst_vips_image_copy_to_frame (VipsImage * image, GstVideoFrame * frame,
    GError ** error)
{
  const int width = vips_image_get_width (image);
  const int height = vips_image_get_height (image);
  const int bands = vips_image_get_bands (image);
  const guint8 *data;
  guint8 *out_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  const gsize out_stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  const gsize packed_line = (gsize) width * bands;
  guint y;

  if (G_UNLIKELY (vips_image_get_format (image) != VIPS_FORMAT_UCHAR)) {
    g_set_error (error, gst_vips_error_quark (), GST_VIPS_ERROR_VIPS,
        "unexpected vips image format");
    return FALSE;
  }

  if (G_UNLIKELY (width != GST_VIDEO_FRAME_WIDTH (frame) ||
          height != GST_VIDEO_FRAME_HEIGHT (frame))) {
    g_set_error (error, gst_vips_error_quark (), GST_VIPS_ERROR_UNKNOWN,
        "vips image size %dx%d does not match frame size %ux%u",
        width, height,
        (guint) GST_VIDEO_FRAME_WIDTH (frame),
        (guint) GST_VIDEO_FRAME_HEIGHT (frame));
    return FALSE;
  }

  if (G_UNLIKELY (bands > 4)) {
    g_set_error (error, gst_vips_error_quark (), GST_VIPS_ERROR_UNKNOWN,
        "unexpected vips image band count %d", bands);
    return FALSE;
  }

  /* Generating the pixels can be expensive and may fail, so only do it
   * once the image is known to be usable. */
  data = vips_image_get_data (image);
  if (G_UNLIKELY (data == NULL)) {
    g_set_error (error, gst_vips_error_quark (), GST_VIPS_ERROR_VIPS,
        "failed to read vips image data: %s", vips_error_buffer ());
    vips_error_clear ();
    return FALSE;
  }

  for (y = 0; y < (guint) height; y++)
    memcpy (out_data + y * out_stride, data + y * packed_line, packed_line);

  return TRUE;
}
