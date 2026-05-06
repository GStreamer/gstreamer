/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#ifndef __IOSURFACE_OUTPUT_PATHS_H__
#define __IOSURFACE_OUTPUT_PATHS_H__

#include <gst/check/gstcheck.h>
#include <gst/iosurface/gstiosurface.h>
#include <gst/video/video.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <IOSurface/IOSurfaceRef.h>

static inline GstMemory *
peek_video_plane_memory (GstBuffer * buffer, const GstVideoInfo * info,
    guint plane)
{
  GstVideoMeta *meta = gst_buffer_get_video_meta (buffer);
  guint idx, length;
  gsize offset, skip;

  offset = meta ? meta->offset[plane] :
      GST_VIDEO_INFO_PLANE_OFFSET (info, plane);

  if (!gst_buffer_find_memory (buffer, offset, 1, &idx, &length, &skip))
    return NULL;

  fail_unless (skip == 0, "Expected video plane %u to start at memory offset 0",
      plane);

  return gst_buffer_peek_memory (buffer, idx);
}

static inline void
assert_iosurface_plane_valid (IOSurfaceRef surface, guint plane)
{
  gsize n_planes = IOSurfaceGetPlaneCount (surface);

  /* Non-planar IOSurfaces report zero planes and use plane index 0. */
  fail_unless (n_planes == 0 ? plane == 0 : plane < n_planes,
      "Expected IOSurface plane %u to fit within %" G_GSIZE_FORMAT " planes",
      plane, n_planes);
}

static inline gboolean
iosurface_pixel_format_matches_video_format (OSType pixel_format,
    GstVideoFormat video_format)
{
  switch (video_format) {
    case GST_VIDEO_FORMAT_NV12:
      return pixel_format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
          pixel_format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
    case GST_VIDEO_FORMAT_BGRA:
      return pixel_format == kCVPixelFormatType_32BGRA;
    case GST_VIDEO_FORMAT_UYVY:
      return pixel_format == kCVPixelFormatType_422YpCbCr8;
    case GST_VIDEO_FORMAT_YUY2:
      return pixel_format == kCVPixelFormatType_422YpCbCr8_yuvs;
    default:
      return TRUE;
  }
}

static inline guint
iosurface_video_plane_width (const GstVideoInfo * info, guint plane)
{
  gint comp[GST_VIDEO_MAX_COMPONENTS];

  gst_video_format_info_component (info->finfo, plane, comp);
  return GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]);
}

static inline guint
iosurface_video_plane_row_size (const GstVideoInfo * info, guint plane)
{
  gint comp[GST_VIDEO_MAX_COMPONENTS];
  guint row_size;

  gst_video_format_info_component (info->finfo, plane, comp);
  row_size = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]) *
      GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);

  if (row_size == 0)
    row_size = GST_VIDEO_INFO_PLANE_STRIDE (info, plane);

  return row_size;
}

static inline guint
iosurface_video_plane_height (const GstVideoInfo * info, guint plane)
{
  gint comp[GST_VIDEO_MAX_COMPONENTS];

  gst_video_format_info_component (info->finfo, plane, comp);
  return GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);
}

static inline gsize
iosurface_get_width_for_plane (IOSurfaceRef surface, guint plane)
{
  return IOSurfaceGetPlaneCount (surface) == 0 ?
      IOSurfaceGetWidth (surface) : IOSurfaceGetWidthOfPlane (surface, plane);
}

static inline gsize
iosurface_get_height_for_plane (IOSurfaceRef surface, guint plane)
{
  return IOSurfaceGetPlaneCount (surface) == 0 ?
      IOSurfaceGetHeight (surface) : IOSurfaceGetHeightOfPlane (surface, plane);
}

static inline gsize
iosurface_get_bytes_per_row_for_plane (IOSurfaceRef surface, guint plane)
{
  return IOSurfaceGetPlaneCount (surface) == 0 ?
      IOSurfaceGetBytesPerRow (surface) :
      IOSurfaceGetBytesPerRowOfPlane (surface, plane);
}

static inline gpointer
iosurface_get_base_address_for_plane (IOSurfaceRef surface, guint plane)
{
  return IOSurfaceGetPlaneCount (surface) == 0 ?
      IOSurfaceGetBaseAddress (surface) :
      IOSurfaceGetBaseAddressOfPlane (surface, plane);
}

static inline void
assert_iosurface_memory_matches_plane (GstMemory * mem,
    const GstVideoInfo * info, GstVideoMeta * meta, guint video_plane,
    IOSurfaceRef surface, guint iosurface_plane, const gchar * label)
{
  GstMapInfo map_info;
  gpointer base_address;
  gsize iosurface_width;
  gsize iosurface_height;
  gsize iosurface_stride;
  guint plane_width;
  guint row_size;
  guint plane_height;

  fail_unless (gst_memory_map (mem, &map_info, GST_MAP_READ),
      "%s: expected IOSurface memory for video plane %u to be mappable", label,
      video_plane);

  base_address = iosurface_get_base_address_for_plane (surface, iosurface_plane);
  fail_unless (base_address != NULL,
      "%s: expected non-null IOSurface base address for video plane %u", label,
      video_plane);
  fail_unless (map_info.data == base_address,
      "%s: expected mapped memory %p to match IOSurface base address %p for "
      "video plane %u", label, map_info.data, base_address, video_plane);

  iosurface_width = iosurface_get_width_for_plane (surface, iosurface_plane);
  iosurface_height = iosurface_get_height_for_plane (surface, iosurface_plane);
  iosurface_stride =
      iosurface_get_bytes_per_row_for_plane (surface, iosurface_plane);
  plane_width = iosurface_video_plane_width (info, video_plane);
  row_size = iosurface_video_plane_row_size (info, video_plane);
  plane_height = iosurface_video_plane_height (info, video_plane);

  fail_unless (iosurface_width >= plane_width,
      "%s: expected IOSurface width %" G_GSIZE_FORMAT " to cover video plane "
      "%u width %u", label, iosurface_width, video_plane, plane_width);
  fail_unless (iosurface_height >= plane_height,
      "%s: expected IOSurface height %" G_GSIZE_FORMAT " to cover video plane "
      "%u height %u", label, iosurface_height, video_plane, plane_height);
  fail_unless (iosurface_stride >= row_size,
      "%s: expected IOSurface stride %" G_GSIZE_FORMAT " to cover video plane "
      "%u row size %u", label, iosurface_stride, video_plane, row_size);

  if (meta) {
    fail_unless (meta->stride[video_plane] == (gint) iosurface_stride,
        "%s: expected video meta stride %d to match IOSurface stride %"
        G_GSIZE_FORMAT " for video plane %u", label, meta->stride[video_plane],
        iosurface_stride, video_plane);
  }

  gst_memory_unmap (mem, &map_info);
}

static inline void
assert_iosurface_buffer_matches_caps (GstBuffer * buffer, GstCaps * caps,
    const gchar * label)
{
  GstVideoInfo info;
  GstVideoMeta *meta;
  guint n_planes;
  guint n_mem;

  fail_unless (buffer != NULL, "%s: expected output buffer", label);
  fail_unless (caps != NULL, "%s: expected output caps", label);
  fail_unless (gst_video_info_from_caps (&info, caps),
      "%s: expected video caps, got %" GST_PTR_FORMAT, label, caps);

  meta = gst_buffer_get_video_meta (buffer);
  n_planes = GST_VIDEO_INFO_N_PLANES (&info);
  n_mem = gst_buffer_n_memory (buffer);

  fail_unless (gst_is_iosurface_buffer (buffer),
      "%s: expected IOSurface-backed buffer", label);
  fail_unless (n_mem == n_planes,
      "%s: expected one IOSurface memory per video plane, got %u memories and "
      "%u planes", label, n_mem, n_planes);

  for (guint plane = 0; plane < n_planes; plane++) {
    GstMemory *mem = peek_video_plane_memory (buffer, &info, plane);
    IOSurfaceRef surface = NULL;
    guint iosurface_plane = G_MAXUINT;

    fail_unless (mem != NULL, "%s: expected memory for video plane %u", label,
        plane);
    fail_unless (gst_iosurface_memory_peek_surface (mem, &surface,
            &iosurface_plane), "%s: expected IOSurface for video plane %u",
        label, plane);
    fail_unless (surface != NULL, "%s: expected non-null IOSurface for plane %u",
        label, plane);
    fail_unless (iosurface_pixel_format_matches_video_format
        (IOSurfaceGetPixelFormat (surface), GST_VIDEO_INFO_FORMAT (&info)),
        "%s: expected IOSurface pixel format %" GST_FOURCC_FORMAT
        " to match video format %s", label,
        GST_FOURCC_ARGS (IOSurfaceGetPixelFormat (surface)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&info)));
    assert_iosurface_plane_valid (surface, iosurface_plane);
    assert_iosurface_memory_matches_plane (mem, &info, meta, plane, surface,
        iosurface_plane, label);
  }
}

#endif /* __IOSURFACE_OUTPUT_PATHS_H__ */
