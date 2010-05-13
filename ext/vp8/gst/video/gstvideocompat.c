/*
 * Copyright 2007 David A. Schleef
 *
 * This comes solely from commit 1c3e012fc3e4b9aba19d44855bf8c8bf6ec44cd5
 * to gst-plugins-base.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>
#include "gstvideocompat.h"

#if 0
/**
 * gst_video_format_parse_caps:
 * @caps: the #GstCaps to parse
 * @format: the #GstVideoFormat of the video represented by @caps (output)
 * @width: the width of the video represented by @caps (output)
 * @height: the height of the video represented by @caps (output)
 *
 * Determines the #GstVideoFormat of @caps and places it in the location
 * pointed to by @format.  Extracts the size of the video and places it
 * in the location pointed to by @width and @height.  If @caps does not
 * reprsent one of the raw video formats listed in #GstVideoFormat, the
 * function will fail and return FALSE.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @caps was parsed correctly.
 */
gboolean
gst_video_format_parse_caps (GstCaps * caps, GstVideoFormat * format,
    int *width, int *height)
{
  GstStructure *structure;
  gboolean ok = TRUE;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (format) {
    if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
      guint32 fourcc;

      ok &= gst_structure_get_fourcc (structure, "format", &fourcc);

      *format = gst_video_format_from_fourcc (fourcc);
      if (*format == GST_VIDEO_FORMAT_UNKNOWN) {
        ok = FALSE;
      }
#if 0
    } else if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
      int depth;
      int bpp;
      int endianness;
      int red_mask;
      int green_mask;
      int blue_mask;

      ok &= gst_structure_get_int (structure, "depth", &depth);
      ok &= gst_structure_get_int (structure, "bpp", &bpp);
      ok &= gst_structure_get_int (structure, "endianness", &endianness);
      ok &= gst_structure_get_int (structure, "red_mask", &red_mask);
      ok &= gst_structure_get_int (structure, "green_mask", &green_mask);
      ok &= gst_structure_get_int (structure, "blue_mask", &blue_mask);

      if (depth != 24 || bpp != 32 || endianness != G_BIG_ENDIAN) {
        ok = FALSE;
      } else {
        *format = gst_video_format_from_rgb32_masks (red_mask, green_mask,
            blue_mask);
        if (*format == GST_VIDEO_FORMAT_UNKNOWN) {
          ok = FALSE;
        }
      }
#endif
    } else {
      ok = FALSE;
    }
  }

  if (width) {
    ok &= gst_structure_get_int (structure, "width", width);
  }

  if (height) {
    ok &= gst_structure_get_int (structure, "height", height);
  }

  return ok;
}

/**
 * gst_video_parse_caps_framerate:
 * @caps:
 * @fps_n: pointer to numerator of frame rate (output)
 * @fps_d: pointer to denominator of frame rate (output)
 *
 * Extracts the frame rate from @caps and places the values in the locations
 * pointed to by @fps_n and @fps_d.  Returns TRUE if the values could be
 * parsed correctly, FALSE if not.
 *
 * This function can be used with #GstCaps that have any media type; it
 * is not limited to formats handled by #GstVideoFormat.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @caps was parsed correctly.
 */
gboolean
gst_video_parse_caps_framerate (GstCaps * caps, int *fps_n, int *fps_d)
{
  GstStructure *structure;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  return gst_structure_get_fraction (structure, "framerate", fps_n, fps_d);
}

/**
 * gst_video_parse_caps_pixel_aspect_ratio:
 * @caps:
 * @par_n: pointer to numerator of pixel aspect ratio (output)
 * @par_d: pointer to denominator of pixel aspect ratio (output)
 *
 * Extracts the pixel aspect ratio from @caps and places the values in
 * the locations pointed to by @par_n and @par_d.  Returns TRUE if the
 * values could be parsed correctly, FALSE if not.
 *
 * This function can be used with #GstCaps that have any media type; it
 * is not limited to formats handled by #GstVideoFormat.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @caps was parsed correctly.
 */
gboolean
gst_video_parse_caps_pixel_aspect_ratio (GstCaps * caps, int *par_n, int *par_d)
{
  GstStructure *structure;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          par_n, par_d)) {
    *par_n = 1;
    *par_d = 1;
  }
  return TRUE;
}

/**
 * gst_video_format_new_caps:
 * @format: the #GstVideoFormat describing the raw video format
 * @width: width of video
 * @height: height of video
 * @framerate_n: numerator of frame rate
 * @framerate_d: denominator of frame rate
 * @par_n: numerator of pixel aspect ratio
 * @par_d: denominator of pixel aspect ratio
 *
 * Creates a new #GstCaps object based on the parameters provided.
 *
 * Since: 0.10.16
 *
 * Returns: a new #GstCaps object, or NULL if there was an error
 */
GstCaps *
gst_video_format_new_caps (GstVideoFormat format, int width, int height,
    int framerate_n, int framerate_d, int par_n, int par_d)
{
  if (gst_video_format_is_yuv (format)) {
    return gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, gst_video_format_to_fourcc (format),
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
  }
  if (gst_video_format_is_rgb (format)) {
    int red_mask;
    int blue_mask;
    int green_mask;

    red_mask =
        0xff000000U >> gst_video_format_get_component_offset (format, 0, width,
        height);
    green_mask =
        0xff000000U >> gst_video_format_get_component_offset (format, 1, width,
        height);
    blue_mask =
        0xff000000U >> gst_video_format_get_component_offset (format, 2, width,
        height);

    return gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, 32,
        "depth", G_TYPE_INT, 24,
        "endianness", G_TYPE_INT, G_BIG_ENDIAN,
        "red_mask", G_TYPE_INT, red_mask,
        "green_mask", G_TYPE_INT, green_mask,
        "blue_mask", G_TYPE_INT, blue_mask,
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
  }
  return NULL;
}

/**
 * gst_video_format_from_fourcc:
 * @fourcc: a FOURCC value representing raw YUV video
 *
 * Converts a FOURCC value into the corresponding #GstVideoFormat.
 * If the FOURCC cannot be represented by #GstVideoFormat,
 * #GST_VIDEO_FORMAT_UNKNOWN is returned.
 *
 * Since: 0.10.16
 *
 * Returns: the #GstVideoFormat describing the FOURCC value
 */
GstVideoFormat
gst_video_format_from_fourcc (guint32 fourcc)
{
  switch (fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      return GST_VIDEO_FORMAT_I420;
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      return GST_VIDEO_FORMAT_YV12;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      return GST_VIDEO_FORMAT_YUY2;
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      return GST_VIDEO_FORMAT_UYVY;
    case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
      return GST_VIDEO_FORMAT_AYUV;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

/**
 * gst_video_format_to_fourcc:
 * @format: a #GstVideoFormat video format
 *
 * Converts a #GstVideoFormat value into the corresponding FOURCC.  Only
 * a few YUV formats have corresponding FOURCC values.  If @format has
 * no corresponding FOURCC value, 0 is returned.
 *
 * Since: 0.10.16
 *
 * Returns: the FOURCC corresponding to @format
 */
guint32
gst_video_format_to_fourcc (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      return GST_MAKE_FOURCC ('I', '4', '2', '0');
    case GST_VIDEO_FORMAT_YV12:
      return GST_MAKE_FOURCC ('Y', 'V', '1', '2');
    case GST_VIDEO_FORMAT_YUY2:
      return GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
    case GST_VIDEO_FORMAT_UYVY:
      return GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
    case GST_VIDEO_FORMAT_AYUV:
      return GST_MAKE_FOURCC ('A', 'Y', 'U', 'V');
    default:
      return 0;
  }
}

/**
 * gst_video_format_from_rgb32_masks:
 * @red_mask: red bit mask
 * @green_mask: green bit mask
 * @blue_mask: blue bit mask
 *
 * Converts red, green, blue bit masks into the corresponding
 * #GstVideoFormat.  
 *
 * Since: 0.10.16
 *
 * Returns: the #GstVideoFormat corresponding to the bit masks
 */
GstVideoFormat
gst_video_format_from_rgb32_masks (int red_mask, int green_mask, int blue_mask)
{
  if (red_mask == 0xff000000 && green_mask == 0x00ff0000 &&
      blue_mask == 0x0000ff00) {
    return GST_VIDEO_FORMAT_RGBx;
  }
  if (red_mask == 0x0000ff00 && green_mask == 0x00ff0000 &&
      blue_mask == 0xff000000) {
    return GST_VIDEO_FORMAT_BGRx;
  }
  if (red_mask == 0x00ff0000 && green_mask == 0x0000ff00 &&
      blue_mask == 0x000000ff) {
    return GST_VIDEO_FORMAT_xRGB;
  }
  if (red_mask == 0x000000ff && green_mask == 0x0000ff00 &&
      blue_mask == 0x00ff0000) {
    return GST_VIDEO_FORMAT_xBGR;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

/**
 * gst_video_format_is_rgb:
 * @format: a #GstVideoFormat
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @format represents RGB video
 */
gboolean
gst_video_format_is_rgb (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
      return FALSE;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return TRUE;
    default:
      return FALSE;
  }
}

/**
 * gst_video_format_is_yuv:
 * @format: a #GstVideoFormat
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @format represents YUV video
 */
gboolean
gst_video_format_is_yuv (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
      return TRUE;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return FALSE;
    default:
      return FALSE;
  }
}

/**
 * gst_video_format_has_alpha:
 * @format: a #GstVideoFormat
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if @format has an alpha channel
 */
gboolean
gst_video_format_has_alpha (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return FALSE;
    case GST_VIDEO_FORMAT_AYUV:
      return TRUE;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return FALSE;
    default:
      return FALSE;
  }
}

/**
 * gst_video_format_get_row_stride:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @width: the width of video
 *
 * Calculates the row stride (number of bytes from one row of pixels to
 * the next) for the video component with an index of @component.  For
 * YUV video, Y, U, and V have component indices of 0, 1, and 2,
 * respectively.  For RGB video, R, G, and B have component indicies of
 * 0, 1, and 2, respectively.  Alpha channels, if present, have a component
 * index of 3.  The @width parameter always represents the width of the
 * video, not the component.
 *
 * Since: 0.10.16
 *
 * Returns: row stride of component @component
 */
int
gst_video_format_get_row_stride (GstVideoFormat format, int component,
    int width)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (component == 0) {
        return GST_ROUND_UP_4 (width);
      } else {
        return GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2);
      }
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return GST_ROUND_UP_4 (width * 2);
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return width * 4;
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_pixel_stride:
 * @format: a #GstVideoFormat
 * @component: the component index
 *
 * Calculates the pixel stride (number of bytes from one pixel to the
 * pixel to its immediate left) for the video component with an index
 * of @component.  See @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: pixel stride of component @component
 */
int
gst_video_format_get_pixel_stride (GstVideoFormat format, int component)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      return 1;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0) {
        return 2;
      } else {
        return 4;
      }
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return 4;
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_component_width:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @width: the width of video
 *
 * Calculates the width of the component.  See
 * @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: width of component @component
 */
int
gst_video_format_get_component_width (GstVideoFormat format, int component,
    int width)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0) {
        return width;
      } else {
        return GST_ROUND_UP_2 (width) / 2;
      }
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return width;
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_component_height:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @height: the height of video
 *
 * Calculates the height of the component.  See
 * @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: height of component @component
 */
int
gst_video_format_get_component_height (GstVideoFormat format, int component,
    int height)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (component == 0) {
        return height;
      } else {
        return GST_ROUND_UP_2 (height) / 2;
      }
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return height;
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_component_offset:
 * @format: a #GstVideoFormat
 * @component: the component index
 * @width: the width of video
 * @height: the height of video
 *
 * Calculates the offset (in bytes) of the first pixel of the component
 * with index @component.  For packed formats, this will typically be a
 * small integer (0, 1, 2, 3).  For planar formats, this will be a
 * (relatively) large offset to the beginning of the second or third
 * component planes.  See @gst_video_format_get_row_stride for a description
 * of the component index.
 *
 * Since: 0.10.16
 *
 * Returns: offset of component @component
 */
int
gst_video_format_get_component_offset (GstVideoFormat format, int component,
    int width, int height)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      if (component == 0) {
        return 0;
      } else {
        int offset;

        offset = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
        if (component == 2) {
          offset += GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
              (GST_ROUND_UP_2 (height) / 2);
        }
        return offset;
      }
    case GST_VIDEO_FORMAT_YUY2:
      if (component == 0)
        return 0;
      if (component == 1)
        return 1;
      if (component == 2)
        return 3;
      return 0;
    case GST_VIDEO_FORMAT_UYVY:
      if (component == 0)
        return 1;
      if (component == 1)
        return 0;
      if (component == 2)
        return 2;
      return 0;
    case GST_VIDEO_FORMAT_AYUV:
      if (component == 0)
        return 1;
      if (component == 1)
        return 2;
      if (component == 2)
        return 3;
      if (component == 3)
        return 0;
      return 0;
    case GST_VIDEO_FORMAT_RGBx:
      if (component == 0)
        return 0;
      if (component == 1)
        return 1;
      if (component == 2)
        return 2;
      if (component == 3)
        return 3;
      return 0;
    case GST_VIDEO_FORMAT_BGRx:
      if (component == 0)
        return 2;
      if (component == 1)
        return 1;
      if (component == 2)
        return 0;
      if (component == 3)
        return 3;
      return 0;
    case GST_VIDEO_FORMAT_xRGB:
      if (component == 0)
        return 1;
      if (component == 1)
        return 2;
      if (component == 2)
        return 3;
      if (component == 3)
        return 0;
      return 0;
    case GST_VIDEO_FORMAT_xBGR:
      if (component == 0)
        return 3;
      if (component == 1)
        return 2;
      if (component == 2)
        return 1;
      if (component == 3)
        return 0;
      return 0;
    default:
      return 0;
  }
}

/**
 * gst_video_format_get_size:
 * @format: a #GstVideoFormat
 * @width: the width of video
 * @height: the height of video
 *
 * Calculates the total number of bytes in the raw video format.  This
 * number should be used when allocating a buffer for raw video.
 *
 * Since: 0.10.16
 *
 * Returns: size (in bytes) of raw video format
 */
int
gst_video_format_get_size (GstVideoFormat format, int width, int height)
{
  int size;

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      size = GST_ROUND_UP_4 (width) * GST_ROUND_UP_2 (height);
      size += GST_ROUND_UP_4 (GST_ROUND_UP_2 (width) / 2) *
          (GST_ROUND_UP_2 (height) / 2) * 2;
      return size;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return GST_ROUND_UP_4 (width * 2) * height;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return width * 4 * height;
    default:
      return 0;
  }
}

/**
 * gst_video_format_convert:
 * @format: a #GstVideoFormat
 * @width: the width of video
 * @height: the height of video
 * @fps_n: frame rate numerator
 * @fps_d: frame rate denominator
 * @src_format: #GstFormat of the @src_value
 * @src_value: value to convert
 * @dest_format: #GstFormat of the @dest_value
 * @dest_value: pointer to destination value
 *
 * Converts among various #GstFormat types.  This function handles
 * GST_FORMAT_BYTES, GST_FORMAT_TIME, and GST_FORMAT_DEFAULT.  For
 * raw video, GST_FORMAT_DEFAULT corresponds to video frames.  This
 * function can be to handle pad queries of the type GST_QUERY_CONVERT.
 *
 * Since: 0.10.16
 *
 * Returns: TRUE if the conversion was successful.
 */
gboolean
gst_video_format_convert (GstVideoFormat format, int width, int height,
    int fps_n, int fps_d,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;
  int size;

  size = gst_video_format_get_size (format, width, height);

  GST_DEBUG ("converting value %" G_GINT64_FORMAT " from %s to %s",
      src_value, gst_format_get_name (src_format),
      gst_format_get_name (dest_format));

  if (src_format == dest_format) {
    *dest_value = src_value;
    ret = TRUE;
    goto done;
  }

  if (src_value == -1) {
    *dest_value = -1;
    ret = TRUE;
    goto done;
  }

  /* bytes to frames */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_DEFAULT) {
    if (size != 0) {
      *dest_value = gst_util_uint64_scale_int (src_value, 1, size);
    } else {
      GST_WARNING ("blocksize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* frames to bytes */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_BYTES) {
    *dest_value = gst_util_uint64_scale_int (src_value, size, 1);
    ret = TRUE;
    goto done;
  }

  /* time to frames */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_DEFAULT) {
    if (fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          fps_n, GST_SECOND * fps_d);
    } else {
      GST_WARNING ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* frames to time */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_TIME) {
    if (fps_n != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * fps_d, fps_n);
    } else {
      GST_WARNING ("framerate numerator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* time to bytes */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_BYTES) {
    if (fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          fps_n * size, GST_SECOND * fps_d);
    } else {
      GST_WARNING ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* bytes to time */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_TIME) {
    if (fps_n != 0 && size != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * fps_d, fps_n * size);
    } else {
      GST_WARNING ("framerate denominator and/or blocksize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

done:

  GST_DEBUG ("ret=%d result %" G_GINT64_FORMAT, ret, *dest_value);

  return ret;
}

void
gst_adapter_copy (GstAdapter * adapter, guint8 * dest, guint offset, guint size)
{
  GSList *g;
  int skip;

  g_return_if_fail (GST_IS_ADAPTER (adapter));
  g_return_if_fail (size > 0);

  /* we don't have enough data, return. This is unlikely
   * as one usually does an _available() first instead of copying a
   * random size. */
  if (G_UNLIKELY (offset + size > adapter->size))
    return;

  skip = adapter->skip;
  for (g = adapter->buflist; g && size > 0; g = g_slist_next (g)) {
    GstBuffer *buf;

    buf = g->data;
    if (offset < GST_BUFFER_SIZE (buf) - skip) {
      int n;

      n = MIN (GST_BUFFER_SIZE (buf) - skip - offset, size);
      memcpy (dest, GST_BUFFER_DATA (buf) + skip + offset, n);

      dest += n;
      offset = 0;
      size -= n;
    } else {
      offset -= GST_BUFFER_SIZE (buf) - skip;
    }
    skip = 0;
  }
}
#endif
