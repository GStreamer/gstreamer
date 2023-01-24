/*
 *  gstvaapiimage.c - VA image abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapiimage
 * @short_description: VA image abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapiimage.h"
#include "gstvaapiimage_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define SWAP_UINT(a, b) do { \
        guint v = a;         \
        a = b;               \
        b = v;               \
    } while (0)

static gboolean
_gst_vaapi_image_map (GstVaapiImage * image, GstVaapiImageRaw * raw_image);

static gboolean _gst_vaapi_image_unmap (GstVaapiImage * image);

static gboolean
_gst_vaapi_image_set_image (GstVaapiImage * image, const VAImage * va_image);

/*
 * VAImage wrapper
 */

static gboolean
vaapi_image_is_linear (const VAImage * va_image)
{
  guint i, width, height, width2, height2, data_size;

  for (i = 1; i < va_image->num_planes; i++)
    if (va_image->offsets[i] < va_image->offsets[i - 1])
      return FALSE;

  width = va_image->width;
  height = va_image->height;
  width2 = (width + 1) / 2;
  height2 = (height + 1) / 2;

  switch (va_image->format.fourcc) {
    case VA_FOURCC ('N', 'V', '1', '2'):
    case VA_FOURCC ('Y', 'V', '1', '2'):
    case VA_FOURCC ('I', '4', '2', '0'):
      data_size = width * height + 2 * width2 * height2;
      break;
    case VA_FOURCC ('Y', 'U', 'Y', '2'):
    case VA_FOURCC ('U', 'Y', 'V', 'Y'):
    case VA_FOURCC ('R', 'G', '1', '6'):
      data_size = 2 * width * height;
      break;
    case VA_FOURCC ('Y', '8', '0', '0'):
      data_size = width * height;
      break;
    case VA_FOURCC ('A', 'Y', 'U', 'V'):
    case VA_FOURCC ('A', 'R', 'G', 'B'):
    case VA_FOURCC ('R', 'G', 'B', 'A'):
    case VA_FOURCC ('A', 'B', 'G', 'R'):
    case VA_FOURCC ('B', 'G', 'R', 'A'):
    case VA_FOURCC ('X', 'R', 'G', 'B'):
    case VA_FOURCC ('R', 'G', 'B', 'X'):
    case VA_FOURCC ('X', 'B', 'G', 'R'):
    case VA_FOURCC ('B', 'G', 'R', 'X'):
    case VA_FOURCC ('Y', '2', '1', '0'):
    case VA_FOURCC ('Y', '4', '1', '0'):
    case VA_FOURCC ('A', 'R', '3', '0'):
    case VA_FOURCC ('Y', '2', '1', '2'):
      data_size = 4 * width * height;
      break;
    case VA_FOURCC ('P', '0', '1', '0'):
    case VA_FOURCC ('P', '0', '1', '2'):
      data_size = 2 * (width * height + 2 * width2 * height2);
      break;
    case VA_FOURCC ('R', 'G', '2', '4'):
    case VA_FOURCC ('4', '4', '4', 'P'):
      data_size = 3 * width * height;
      break;
    case VA_FOURCC ('Y', '4', '1', '2'):
      data_size = 8 * width * height;
      break;
    default:
      GST_ERROR ("FIXME: incomplete formats %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (va_image->format.fourcc));
      data_size = G_MAXUINT;
      break;
  }
  return va_image->data_size == data_size;
}

static void
gst_vaapi_image_free (GstVaapiImage * image)
{
  GstVaapiDisplay *const display = GST_VAAPI_IMAGE_DISPLAY (image);
  VAImageID image_id;
  VAStatus status;

  _gst_vaapi_image_unmap (image);

  image_id = GST_VAAPI_IMAGE_ID (image);
  GST_DEBUG ("image %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (image_id));

  if (image_id != VA_INVALID_ID) {
    GST_VAAPI_DISPLAY_LOCK (display);
    status = vaDestroyImage (GST_VAAPI_DISPLAY_VADISPLAY (display), image_id);
    GST_VAAPI_DISPLAY_UNLOCK (display);
    if (!vaapi_check_status (status, "vaDestroyImage()"))
      GST_WARNING ("failed to destroy image %" GST_VAAPI_ID_FORMAT,
          GST_VAAPI_ID_ARGS (image_id));
    GST_VAAPI_IMAGE_ID (image) = VA_INVALID_ID;
  }

  gst_vaapi_display_replace (&GST_VAAPI_IMAGE_DISPLAY (image), NULL);

  g_free (image);
}

static gboolean
_gst_vaapi_image_create (GstVaapiImage * image, GstVideoFormat format)
{
  GstVaapiDisplay *const display = GST_VAAPI_IMAGE_DISPLAY (image);
  const VAImageFormat *va_format;
  VAStatus status;

  if (!gst_vaapi_display_has_image_format (display, format))
    return FALSE;

  va_format = gst_vaapi_video_format_to_va_format (format);
  if (!va_format)
    return FALSE;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateImage (GST_VAAPI_DISPLAY_VADISPLAY (display),
      (VAImageFormat *) va_format,
      image->width, image->height, &image->internal_image);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (status != VA_STATUS_SUCCESS ||
      image->internal_image.format.fourcc != va_format->fourcc)
    return FALSE;

  image->internal_format = format;
  return TRUE;
}

static gboolean
gst_vaapi_image_create (GstVaapiImage * image, GstVideoFormat format,
    guint width, guint height)
{
  const VAImageFormat *va_format;
  VAImageID image_id;

  image->format = format;
  image->width = width;
  image->height = height;

  if (!_gst_vaapi_image_create (image, format)) {
    switch (format) {
      case GST_VIDEO_FORMAT_I420:
        format = GST_VIDEO_FORMAT_YV12;
        break;
      case GST_VIDEO_FORMAT_YV12:
        format = GST_VIDEO_FORMAT_I420;
        break;
      default:
        format = 0;
        break;
    }
    if (!format || !_gst_vaapi_image_create (image, format))
      return FALSE;
  }
  image->image = image->internal_image;
  image_id = image->image.image_id;

  if (image->format != image->internal_format) {
    switch (image->format) {
      case GST_VIDEO_FORMAT_YV12:
      case GST_VIDEO_FORMAT_I420:
        va_format = gst_vaapi_video_format_to_va_format (image->format);
        if (!va_format)
          return FALSE;
        image->image.format = *va_format;
        SWAP_UINT (image->image.offsets[1], image->image.offsets[2]);
        SWAP_UINT (image->image.pitches[1], image->image.pitches[2]);
        break;
      default:
        break;
    }
  }
  image->is_linear = vaapi_image_is_linear (&image->image);

  GST_DEBUG ("image %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (image_id));
  GST_VAAPI_IMAGE_ID (image) = image_id;
  return TRUE;
}

static void
gst_vaapi_image_init (GstVaapiImage * image, GstVaapiDisplay * display)
{
  /* TODO(victor): implement image copy mechanism, it's almost
   * there */
  gst_mini_object_init (GST_MINI_OBJECT_CAST (image), 0,
      GST_TYPE_VAAPI_IMAGE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vaapi_image_free);

  GST_VAAPI_IMAGE_DISPLAY (image) = gst_object_ref (display);
  GST_VAAPI_IMAGE_ID (image) = VA_INVALID_ID;
  image->internal_image.image_id = VA_INVALID_ID;
  image->internal_image.buf = VA_INVALID_ID;
  image->image.image_id = VA_INVALID_ID;
  image->image.buf = VA_INVALID_ID;
  image->image_data = NULL;
  image->internal_format = image->format = GST_VIDEO_FORMAT_UNKNOWN;
  image->width = image->height = 0;
  image->is_linear = FALSE;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiImage, gst_vaapi_image);

/**
 * gst_vaapi_image_get_display:
 * @image: a #GstVaapiImage
 *
 * Returns the #GstVaapiDisplay this @image is bound to.
 *
 * Return value: the parent #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_image_get_display (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, NULL);
  return GST_VAAPI_IMAGE_DISPLAY (image);
}

/**
 * gst_vaapi_image_new:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVideoFormat
 * @width: the requested image width
 * @height: the requested image height
 *
 * Creates a new #GstVaapiImage with the specified format and
 * dimensions.
 *
 * Return value: the newly allocated #GstVaapiImage object
 */
GstVaapiImage *
gst_vaapi_image_new (GstVaapiDisplay * display,
    GstVideoFormat format, guint width, guint height)
{
  GstVaapiImage *image;

  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  GST_DEBUG ("format %s, size %ux%u", gst_vaapi_video_format_to_string (format),
      width, height);

  image = g_new (GstVaapiImage, 1);
  if (!image)
    return NULL;

  gst_vaapi_image_init (image, display);

  if (!gst_vaapi_image_create (image, format, width, height))
    goto error;
  return image;

  /* ERRORS */
error:
  {
    gst_vaapi_image_unref (image);
    return NULL;
  }
}

/**
 * gst_vaapi_image_new_with_image:
 * @display: a #GstVaapiDisplay
 * @va_image: a VA image
 *
 * Creates a new #GstVaapiImage from a foreign VA image. The image
 * format and dimensions will be extracted from @va_image. This
 * function is mainly used by gst_vaapi_surface_derive_image() to bind
 * a VA image to a #GstVaapiImage object.
 *
 * Return value: the newly allocated #GstVaapiImage object
 */
GstVaapiImage *
gst_vaapi_image_new_with_image (GstVaapiDisplay * display, VAImage * va_image)
{
  GstVaapiImage *image;

  g_return_val_if_fail (va_image, NULL);
  g_return_val_if_fail (va_image->image_id != VA_INVALID_ID, NULL);
  g_return_val_if_fail (va_image->buf != VA_INVALID_ID, NULL);

  GST_DEBUG ("VA image 0x%08x, format %" GST_FOURCC_FORMAT ", size %ux%u",
      va_image->image_id,
      GST_FOURCC_ARGS (va_image->format.fourcc),
      va_image->width, va_image->height);

  image = g_new (GstVaapiImage, 1);
  if (!image)
    return NULL;

  gst_vaapi_image_init (image, display);

  if (!_gst_vaapi_image_set_image (image, va_image))
    goto error;
  return image;

  /* ERRORS */
error:
  {
    gst_vaapi_image_unref (image);
    return NULL;
  }
}

/**
 * gst_vaapi_image_get_id:
 * @image: a #GstVaapiImage
 *
 * Returns the underlying VAImageID of the @image.
 *
 * Return value: the underlying VA image id
 */
GstVaapiID
gst_vaapi_image_get_id (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, VA_INVALID_ID);

  return GST_VAAPI_IMAGE_ID (image);
}

/**
 * gst_vaapi_image_get_image:
 * @image: a #GstVaapiImage
 * @va_image: a VA image
 *
 * Fills @va_image with the VA image used internally.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_get_image (GstVaapiImage * image, VAImage * va_image)
{
  g_return_val_if_fail (image != NULL, FALSE);

  if (va_image)
    *va_image = image->image;

  return TRUE;
}

/*
 * _gst_vaapi_image_set_image:
 * @image: a #GstVaapiImage
 * @va_image: a VA image
 *
 * Initializes #GstVaapiImage with a foreign VA image. This function
 * will try to "linearize" the VA image. i.e. making sure that the VA
 * image offsets into the data buffer are in increasing order with the
 * number of planes available in the image.
 *
 * This is an internal function used by gst_vaapi_image_new_with_image().
 *
 * Return value: %TRUE on success
 */
gboolean
_gst_vaapi_image_set_image (GstVaapiImage * image, const VAImage * va_image)
{
  GstVideoFormat format;
  VAImage alt_va_image;
  const VAImageFormat *alt_va_format;

  format = gst_vaapi_video_format_from_va_format (&va_image->format);
  if (format == GST_VIDEO_FORMAT_UNKNOWN)
    return FALSE;

  image->internal_image = *va_image;
  image->internal_format = format;
  image->is_linear = vaapi_image_is_linear (va_image);
  image->image = *va_image;
  image->format = format;
  image->width = va_image->width;
  image->height = va_image->height;

  GST_VAAPI_IMAGE_ID (image) = va_image->image_id;

  /* Try to linearize image */
  if (!image->is_linear) {
    switch (format) {
      case GST_VIDEO_FORMAT_I420:
        format = GST_VIDEO_FORMAT_YV12;
        break;
      case GST_VIDEO_FORMAT_YV12:
        format = GST_VIDEO_FORMAT_I420;
        break;
      default:
        format = 0;
        break;
    }
    if (format &&
        (alt_va_format = gst_vaapi_video_format_to_va_format (format))) {
      alt_va_image = *va_image;
      alt_va_image.format = *alt_va_format;
      SWAP_UINT (alt_va_image.offsets[1], alt_va_image.offsets[2]);
      SWAP_UINT (alt_va_image.pitches[1], alt_va_image.pitches[2]);
      if (vaapi_image_is_linear (&alt_va_image)) {
        image->image = alt_va_image;
        image->format = format;
        image->is_linear = TRUE;
        GST_DEBUG ("linearized image to %s format",
            gst_vaapi_video_format_to_string (format));
      }
    }
  }
  return TRUE;
}

/**
 * gst_vaapi_image_get_format:
 * @image: a #GstVaapiImage
 *
 * Returns the #GstVideoFormat the @image was created with.
 *
 * Return value: the #GstVideoFormat
 */
GstVideoFormat
gst_vaapi_image_get_format (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, 0);

  return image->format;
}

/**
 * gst_vaapi_image_get_width:
 * @image: a #GstVaapiImage
 *
 * Returns the @image width.
 *
 * Return value: the image width, in pixels
 */
guint
gst_vaapi_image_get_width (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, 0);

  return image->width;
}

/**
 * gst_vaapi_image_get_height:
 * @image: a #GstVaapiImage
 *
 * Returns the @image height.
 *
 * Return value: the image height, in pixels.
 */
guint
gst_vaapi_image_get_height (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, 0);

  return image->height;
}

/**
 * gst_vaapi_image_get_size:
 * @image: a #GstVaapiImage
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiImage.
 */
void
gst_vaapi_image_get_size (GstVaapiImage * image, guint * pwidth,
    guint * pheight)
{
  g_return_if_fail (image != NULL);

  if (pwidth)
    *pwidth = image->width;

  if (pheight)
    *pheight = image->height;
}

/**
 * gst_vaapi_image_is_linear:
 * @image: a #GstVaapiImage
 *
 * Checks whether the @image has data planes allocated from a single
 * buffer and offsets into that buffer are in increasing order with
 * the number of planes.
 *
 * Return value: %TRUE if image data planes are allocated from a single buffer
 */
gboolean
gst_vaapi_image_is_linear (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, FALSE);

  return image->is_linear;
}

/**
 * gst_vaapi_image_is_mapped:
 * @image: a #GstVaapiImage
 *
 * Checks whether the @image is currently mapped or not.
 *
 * Return value: %TRUE if the @image is mapped
 */
static inline gboolean
_gst_vaapi_image_is_mapped (GstVaapiImage * image)
{
  return image->image_data != NULL;
}

gboolean
gst_vaapi_image_is_mapped (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, FALSE);

  return _gst_vaapi_image_is_mapped (image);
}

/**
 * gst_vaapi_image_map:
 * @image: a #GstVaapiImage
 *
 * Maps the image data buffer. The actual pixels are returned by the
 * gst_vaapi_image_get_plane() function.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_map (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, FALSE);

  return _gst_vaapi_image_map (image, NULL);
}

gboolean
_gst_vaapi_image_map (GstVaapiImage * image, GstVaapiImageRaw * raw_image)
{
  GstVaapiDisplay *display;
  VAStatus status;
  guint i;

  if (_gst_vaapi_image_is_mapped (image))
    goto map_success;

  display = GST_VAAPI_IMAGE_DISPLAY (image);
  if (!display)
    return FALSE;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaMapBuffer (GST_VAAPI_DISPLAY_VADISPLAY (display),
      image->image.buf, (void **) &image->image_data);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaMapBuffer()"))
    return FALSE;

map_success:
  if (raw_image) {
    const VAImage *const va_image = &image->image;
    raw_image->format = image->format;
    raw_image->width = va_image->width;
    raw_image->height = va_image->height;
    raw_image->num_planes = va_image->num_planes;
    for (i = 0; i < raw_image->num_planes; i++) {
      raw_image->pixels[i] = (guchar *) image->image_data +
          va_image->offsets[i];
      raw_image->stride[i] = va_image->pitches[i];
    }
  }
  return TRUE;
}

/**
 * gst_vaapi_image_unmap:
 * @image: a #GstVaapiImage
 *
 * Unmaps the image data buffer. Pointers to pixels returned by
 * gst_vaapi_image_get_plane() are then no longer valid.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_unmap (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, FALSE);

  return _gst_vaapi_image_unmap (image);
}

gboolean
_gst_vaapi_image_unmap (GstVaapiImage * image)
{
  GstVaapiDisplay *display;
  VAStatus status;

  if (!_gst_vaapi_image_is_mapped (image))
    return TRUE;

  display = GST_VAAPI_IMAGE_DISPLAY (image);
  if (!display)
    return FALSE;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaUnmapBuffer (GST_VAAPI_DISPLAY_VADISPLAY (display),
      image->image.buf);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaUnmapBuffer()"))
    return FALSE;

  image->image_data = NULL;
  return TRUE;
}

/**
 * gst_vaapi_image_get_plane_count:
 * @image: a #GstVaapiImage
 *
 * Retrieves the number of planes available in the @image. The @image
 * must be mapped for this function to work properly.
 *
 * Return value: the number of planes available in the @image
 */
guint
gst_vaapi_image_get_plane_count (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, 0);
  g_return_val_if_fail (_gst_vaapi_image_is_mapped (image), 0);

  return image->image.num_planes;
}

/**
 * gst_vaapi_image_get_plane:
 * @image: a #GstVaapiImage
 * @plane: the requested plane number
 *
 * Retrieves the pixels data to the specified @plane. The @image must
 * be mapped for this function to work properly.
 *
 * Return value: the pixels data of the specified @plane
 */
guchar *
gst_vaapi_image_get_plane (GstVaapiImage * image, guint plane)
{
  g_return_val_if_fail (image != NULL, NULL);
  g_return_val_if_fail (_gst_vaapi_image_is_mapped (image), NULL);
  g_return_val_if_fail (plane < image->image.num_planes, NULL);

  return image->image_data + image->image.offsets[plane];
}

/**
 * gst_vaapi_image_get_pitch:
 * @image: a #GstVaapiImage
 * @plane: the requested plane number
 *
 * Retrieves the line size (stride) of the specified @plane. The
 * @image must be mapped for this function to work properly.
 *
 * Return value: the line size (stride) of the specified plane
 */
guint
gst_vaapi_image_get_pitch (GstVaapiImage * image, guint plane)
{
  g_return_val_if_fail (image != NULL, 0);
  g_return_val_if_fail (_gst_vaapi_image_is_mapped (image), 0);
  g_return_val_if_fail (plane < image->image.num_planes, 0);

  return image->image.pitches[plane];
}

/**
 * gst_vaapi_image_get_data_size:
 * @image: a #GstVaapiImage
 *
 * Retrieves the underlying image data size. This function could be
 * used to determine whether the image has a compatible layout with
 * another image structure.
 *
 * Return value: the whole image data size of the @image
 */
guint
gst_vaapi_image_get_data_size (GstVaapiImage * image)
{
  g_return_val_if_fail (image != NULL, 0);

  return image->image.data_size;
}

#include <gst/video/gstvideometa.h>

static gboolean
init_image_from_video_meta (GstVaapiImageRaw * raw_image, GstVideoMeta * vmeta)
{
  GST_FIXME ("map from GstVideoMeta + add fini_image_from_buffer()");
  return FALSE;
}

static gboolean
init_image_from_buffer (GstVaapiImageRaw * raw_image, GstBuffer * buffer)
{
  GstVideoMeta *const vmeta = gst_buffer_get_video_meta (buffer);

  return vmeta ? init_image_from_video_meta (raw_image, vmeta) : FALSE;
}

/* Copy N lines of an image */
static inline void
memcpy_pic (guchar * dst,
    guint dst_stride,
    const guchar * src, guint src_stride, guint len, guint height)
{
  guint i;

  for (i = 0; i < height; i++) {
    memcpy (dst, src, len);
    dst += dst_stride;
    src += src_stride;
  }
}

/* Copy NV12 images */
static void
copy_image_NV12 (GstVaapiImageRaw * dst_image,
    GstVaapiImageRaw * src_image, const GstVaapiRectangle * rect)
{
  guchar *dst, *src;
  guint dst_stride, src_stride;

  /* Y plane */
  dst_stride = dst_image->stride[0];
  dst = dst_image->pixels[0] + rect->y * dst_stride + rect->x;
  src_stride = src_image->stride[0];
  src = src_image->pixels[0] + rect->y * src_stride + rect->x;
  memcpy_pic (dst, dst_stride, src, src_stride, rect->width, rect->height);

  /* UV plane */
  dst_stride = dst_image->stride[1];
  dst = dst_image->pixels[1] + (rect->y / 2) * dst_stride + (rect->x & -2);
  src_stride = src_image->stride[1];
  src = src_image->pixels[1] + (rect->y / 2) * src_stride + (rect->x & -2);
  memcpy_pic (dst, dst_stride, src, src_stride, rect->width, rect->height / 2);
}

/* Copy YV12 images */
static void
copy_image_YV12 (GstVaapiImageRaw * dst_image,
    GstVaapiImageRaw * src_image, const GstVaapiRectangle * rect)
{
  guchar *dst, *src;
  guint dst_stride, src_stride;
  guint i, x, y, w, h;

  /* Y plane */
  dst_stride = dst_image->stride[0];
  dst = dst_image->pixels[0] + rect->y * dst_stride + rect->x;
  src_stride = src_image->stride[0];
  src = src_image->pixels[0] + rect->y * src_stride + rect->x;
  memcpy_pic (dst, dst_stride, src, src_stride, rect->width, rect->height);

  /* U/V planes */
  x = rect->x / 2;
  y = rect->y / 2;
  w = rect->width / 2;
  h = rect->height / 2;
  for (i = 1; i < dst_image->num_planes; i++) {
    dst_stride = dst_image->stride[i];
    dst = dst_image->pixels[i] + y * dst_stride + x;
    src_stride = src_image->stride[i];
    src = src_image->pixels[i] + y * src_stride + x;
    memcpy_pic (dst, dst_stride, src, src_stride, w, h);
  }
}

/* Copy YUY2 images */
static void
copy_image_YUY2 (GstVaapiImageRaw * dst_image,
    GstVaapiImageRaw * src_image, const GstVaapiRectangle * rect)
{
  guchar *dst, *src;
  guint dst_stride, src_stride;

  /* YUV 4:2:2, full vertical resolution */
  dst_stride = dst_image->stride[0];
  dst = dst_image->pixels[0] + rect->y * dst_stride + rect->x * 2;
  src_stride = src_image->stride[0];
  src = src_image->pixels[0] + rect->y * src_stride + rect->x * 2;
  memcpy_pic (dst, dst_stride, src, src_stride, rect->width * 2, rect->height);
}

/* Copy RGBA images */
static void
copy_image_RGBA (GstVaapiImageRaw * dst_image,
    GstVaapiImageRaw * src_image, const GstVaapiRectangle * rect)
{
  guchar *dst, *src;
  guint dst_stride, src_stride;

  dst_stride = dst_image->stride[0];
  dst = dst_image->pixels[0] + rect->y * dst_stride + rect->x;
  src_stride = src_image->stride[0];
  src = src_image->pixels[0] + rect->y * src_stride + rect->x;
  memcpy_pic (dst, dst_stride, src, src_stride, 4 * rect->width, rect->height);
}

static gboolean
copy_image (GstVaapiImageRaw * dst_image,
    GstVaapiImageRaw * src_image, const GstVaapiRectangle * rect)
{
  GstVaapiRectangle default_rect;

  if (dst_image->format != src_image->format ||
      dst_image->width != src_image->width ||
      dst_image->height != src_image->height)
    return FALSE;

  if (rect) {
    if (rect->x >= src_image->width ||
        rect->x + rect->width > src_image->width ||
        rect->y >= src_image->height ||
        rect->y + rect->height > src_image->height)
      return FALSE;
  } else {
    default_rect.x = 0;
    default_rect.y = 0;
    default_rect.width = src_image->width;
    default_rect.height = src_image->height;
    rect = &default_rect;
  }

  switch (dst_image->format) {
    case GST_VIDEO_FORMAT_NV12:
      copy_image_NV12 (dst_image, src_image, rect);
      break;
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420:
      copy_image_YV12 (dst_image, src_image, rect);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      copy_image_YUY2 (dst_image, src_image, rect);
      break;
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_BGRA:
      copy_image_RGBA (dst_image, src_image, rect);
      break;
    default:
      GST_ERROR ("unsupported image format for copy");
      return FALSE;
  }
  return TRUE;
}

/**
 * gst_vaapi_image_get_buffer:
 * @image: a #GstVaapiImage
 * @buffer: a #GstBuffer
 * @rect: a #GstVaapiRectangle expressing a region, or %NULL for the
 *   whole image
 *
 * Transfers pixels data contained in the @image into the #GstBuffer.
 * Both image structures shall have the same format.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_get_buffer (GstVaapiImage * image,
    GstBuffer * buffer, GstVaapiRectangle * rect)
{
  GstVaapiImageRaw dst_image, src_image;
  gboolean success;

  g_return_val_if_fail (image != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  if (!init_image_from_buffer (&dst_image, buffer))
    return FALSE;
  if (dst_image.format != image->format)
    return FALSE;
  if (dst_image.width != image->width || dst_image.height != image->height)
    return FALSE;

  if (!_gst_vaapi_image_map (image, &src_image))
    return FALSE;

  success = copy_image (&dst_image, &src_image, rect);

  if (!_gst_vaapi_image_unmap (image))
    return FALSE;

  return success;
}

/**
 * gst_vaapi_image_get_raw:
 * @image: a #GstVaapiImage
 * @dst_image: a #GstVaapiImageRaw
 * @rect: a #GstVaapiRectangle expressing a region, or %NULL for the
 *   whole image
 *
 * Transfers pixels data contained in the @image into the #GstVaapiImageRaw.
 * Both image structures shall have the same format.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_get_raw (GstVaapiImage * image,
    GstVaapiImageRaw * dst_image, GstVaapiRectangle * rect)
{
  GstVaapiImageRaw src_image;
  gboolean success;

  g_return_val_if_fail (image != NULL, FALSE);

  if (!_gst_vaapi_image_map (image, &src_image))
    return FALSE;

  success = copy_image (dst_image, &src_image, rect);

  if (!_gst_vaapi_image_unmap (image))
    return FALSE;

  return success;
}

/**
 * gst_vaapi_image_update_from_buffer:
 * @image: a #GstVaapiImage
 * @buffer: a #GstBuffer
 * @rect: a #GstVaapiRectangle expressing a region, or %NULL for the
 *   whole image
 *
 * Transfers pixels data contained in the #GstBuffer into the
 * @image. Both image structures shall have the same format.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_update_from_buffer (GstVaapiImage * image,
    GstBuffer * buffer, GstVaapiRectangle * rect)
{
  GstVaapiImageRaw dst_image, src_image;
  gboolean success;

  g_return_val_if_fail (image != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  if (!init_image_from_buffer (&src_image, buffer))
    return FALSE;
  if (src_image.format != image->format)
    return FALSE;
  if (src_image.width != image->width || src_image.height != image->height)
    return FALSE;

  if (!_gst_vaapi_image_map (image, &dst_image))
    return FALSE;

  success = copy_image (&dst_image, &src_image, rect);

  if (!_gst_vaapi_image_unmap (image))
    return FALSE;

  return success;
}

/**
 * gst_vaapi_image_update_from_raw:
 * @image: a #GstVaapiImage
 * @src_image: a #GstVaapiImageRaw
 * @buffer: a #GstBuffer
 * @rect: a #GstVaapiRectangle expressing a region, or %NULL for the
 *   whole image
 *
 * Transfers pixels data contained in the #GstVaapiImageRaw into the
 * @image. Both image structures shall have the same format.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_update_from_raw (GstVaapiImage * image,
    GstVaapiImageRaw * src_image, GstVaapiRectangle * rect)
{
  GstVaapiImageRaw dst_image;
  gboolean success;

  g_return_val_if_fail (image != NULL, FALSE);

  if (!_gst_vaapi_image_map (image, &dst_image))
    return FALSE;

  success = copy_image (&dst_image, src_image, rect);

  if (!_gst_vaapi_image_unmap (image))
    return FALSE;

  return success;
}

/**
 * gst_vaapi_image_copy:
 * @dst_image: the target #GstVaapiImage
 * @src_image: the source #GstVaapiImage
 *
 * Copies pixels data from @src_image to @dst_image. Both images shall
 * have the same format and size.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_copy (GstVaapiImage * dst_image, GstVaapiImage * src_image)
{
  GstVaapiImageRaw dst_image_raw, src_image_raw;
  gboolean success = FALSE;

  g_return_val_if_fail (dst_image != NULL, FALSE);
  g_return_val_if_fail (src_image != NULL, FALSE);

  if (!_gst_vaapi_image_map (dst_image, &dst_image_raw))
    goto end;
  if (!_gst_vaapi_image_map (src_image, &src_image_raw))
    goto end;

  success = copy_image (&dst_image_raw, &src_image_raw, NULL);

end:
  _gst_vaapi_image_unmap (src_image);
  _gst_vaapi_image_unmap (dst_image);
  return success;
}
