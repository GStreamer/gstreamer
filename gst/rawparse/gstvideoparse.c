/* GStreamer
 * Copyright (C) 2006 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2007,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * gstvideoparse.c:
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
/**
 * SECTION:element-videoparse
 *
 * Converts a byte stream into video frames.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvideoparse.h"

static void gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_video_parse_get_caps (GstRawParse * rp);
static void gst_video_parse_pre_push_buffer (GstRawParse * rp,
    GstBuffer * buffer);
static void gst_video_parse_decide_allocation (GstRawParse * rp,
    GstQuery * query);

static void gst_video_parse_update_info (GstVideoParse * vp);
static gboolean gst_video_parse_deserialize_int_array (const gchar * str,
    gint * dest, guint n_values);

GST_DEBUG_CATEGORY_STATIC (gst_video_parse_debug);
#define GST_CAT_DEFAULT gst_video_parse_debug

enum
{
  PROP_0,
  PROP_FORMAT,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_PAR,
  PROP_FRAMERATE,
  PROP_INTERLACED,
  PROP_TOP_FIELD_FIRST,
  PROP_STRIDES,
  PROP_OFFSETS,
  PROP_FRAMESIZE
};

#define gst_video_parse_parent_class parent_class
G_DEFINE_TYPE (GstVideoParse, gst_video_parse, GST_TYPE_RAW_PARSE);

static void
gst_video_parse_class_init (GstVideoParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstRawParseClass *rp_class = GST_RAW_PARSE_CLASS (klass);
  GstCaps *caps;

  gobject_class->set_property = gst_video_parse_set_property;
  gobject_class->get_property = gst_video_parse_get_property;

  rp_class->get_caps = gst_video_parse_get_caps;
  rp_class->pre_push_buffer = gst_video_parse_pre_push_buffer;
  rp_class->decide_allocation = gst_video_parse_decide_allocation;

  g_object_class_install_property (gobject_class, PROP_FORMAT,
      g_param_spec_enum ("format", "Format", "Format of images in raw stream",
          GST_TYPE_VIDEO_FORMAT, GST_VIDEO_FORMAT_I420,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_int ("width", "Width", "Width of images in raw stream",
          0, INT_MAX, 320, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of images in raw stream",
          0, INT_MAX, 240, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAMERATE,
      gst_param_spec_fraction ("framerate", "Frame Rate",
          "Frame rate of images in raw stream", 0, 1, G_MAXINT, 1, 25, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAR,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "Pixel aspect ratio of images in raw stream", 1, 100, 100, 1, 1, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INTERLACED,
      g_param_spec_boolean ("interlaced", "Interlaced flag",
          "True if video is interlaced", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TOP_FIELD_FIRST,
      g_param_spec_boolean ("top-field-first", "Top field first",
          "True if top field is earlier than bottom field", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_STRIDES,
      g_param_spec_string ("strides", "Strides",
          "Stride of each planes in bytes using string format: 's0,s1,s2,s3'",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OFFSETS,
      g_param_spec_string ("offsets", "Offsets",
          "Offset of each planes in bytes using string format: 'o0,o1,o2,o3'",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAMESIZE,
      g_param_spec_uint ("framesize", "Framesize",
          "Size of an image in raw stream (0: default)", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Video Parse",
      "Filter/Video",
      "Converts stream into video frames",
      "David Schleef <ds@schleef.org>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  caps = gst_caps_from_string ("video/x-raw; video/x-bayer");

  gst_raw_parse_class_set_src_pad_template (rp_class, caps);
  gst_raw_parse_class_set_multiple_frames_per_buffer (rp_class, FALSE);
  gst_caps_unref (caps);

  GST_DEBUG_CATEGORY_INIT (gst_video_parse_debug, "videoparse", 0,
      "videoparse element");
}

static void
gst_video_parse_init (GstVideoParse * vp)
{
  vp->width = 320;
  vp->height = 240;
  vp->format = GST_VIDEO_FORMAT_I420;
  vp->par_n = 1;
  vp->par_d = 1;

  gst_raw_parse_set_fps (GST_RAW_PARSE (vp), 25, 1);
  gst_video_parse_update_info (vp);
}

static void
gst_video_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (object);

  g_return_if_fail (!gst_raw_parse_is_negotiated (GST_RAW_PARSE (vp)));

  switch (prop_id) {
    case PROP_FORMAT:
      vp->format = g_value_get_enum (value);
      break;
    case PROP_WIDTH:
      vp->width = g_value_get_int (value);
      break;
    case PROP_HEIGHT:
      vp->height = g_value_get_int (value);
      break;
    case PROP_FRAMERATE:
      gst_raw_parse_set_fps (GST_RAW_PARSE (vp),
          gst_value_get_fraction_numerator (value),
          gst_value_get_fraction_denominator (value));
      break;
    case PROP_PAR:
      vp->par_n = gst_value_get_fraction_numerator (value);
      vp->par_d = gst_value_get_fraction_denominator (value);
      break;
    case PROP_INTERLACED:
      vp->interlaced = g_value_get_boolean (value);
      break;
    case PROP_TOP_FIELD_FIRST:
      vp->top_field_first = g_value_get_boolean (value);
      break;
    case PROP_STRIDES:
      if (gst_video_parse_deserialize_int_array (g_value_get_string (value),
              vp->stride, GST_VIDEO_MAX_PLANES)) {
        vp->stride_set = TRUE;
      } else {
        GST_WARNING_OBJECT (vp, "failed to deserialize given strides");
        vp->stride_set = FALSE;
      }

      break;
    case PROP_OFFSETS:
      if (gst_video_parse_deserialize_int_array (g_value_get_string (value),
              vp->offset, GST_VIDEO_MAX_PLANES)) {
        vp->offset_set = TRUE;
      } else {
        GST_WARNING_OBJECT (vp, "failed to deserialized given offsets");
        vp->offset_set = FALSE;
      }

      break;
    case PROP_FRAMESIZE:
      vp->framesize = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_video_parse_update_info (vp);
}

static void
gst_video_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (object);

  switch (prop_id) {
    case PROP_FORMAT:
      g_value_set_enum (value, vp->format);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, vp->width);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, vp->height);
      break;
    case PROP_FRAMERATE:{
      gint fps_n, fps_d;

      gst_raw_parse_get_fps (GST_RAW_PARSE (vp), &fps_n, &fps_d);
      gst_value_set_fraction (value, fps_n, fps_d);
      break;
    }
    case PROP_PAR:
      gst_value_set_fraction (value, vp->par_n, vp->par_d);
      break;
    case PROP_INTERLACED:
      g_value_set_boolean (value, vp->interlaced);
      break;
    case PROP_TOP_FIELD_FIRST:
      g_value_set_boolean (value, vp->top_field_first);
      break;
    case PROP_STRIDES:
    {
      gchar *tmp;

      tmp = g_strdup_printf ("%d,%d,%d,%d", vp->info.stride[0],
          vp->info.stride[1], vp->info.stride[2], vp->info.stride[3]);
      g_value_set_string (value, tmp);
      g_free (tmp);
      break;
    }
    case PROP_OFFSETS:
    {
      gchar *tmp;

      tmp = g_strdup_printf ("%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT
          ",%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT, vp->info.offset[0],
          vp->info.offset[1], vp->info.offset[2], vp->info.offset[3]);
      g_value_set_string (value, tmp);
      g_free (tmp);
      break;
    }
    case PROP_FRAMESIZE:
      g_value_set_uint (value, vp->info.size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_video_parse_deserialize_int_array (const gchar * str, gint * dest,
    guint n_values)
{
  gchar **strv;
  guint length;
  guint i;

  strv = g_strsplit (str, ",", n_values);
  if (strv == NULL)
    return FALSE;

  length = g_strv_length (strv);

  for (i = 0; i < length; i++) {
    gint64 val;

    val = g_ascii_strtoll (strv[i], NULL, 10);
    if (val < G_MININT || val > G_MAXINT) {
      g_strfreev (strv);
      return FALSE;
    }

    dest[i] = val;
  }

  /* fill remaining values with 0 */
  for (i = length; i < n_values; i++)
    dest[i] = 0;

  g_strfreev (strv);

  return TRUE;
}

static inline gsize
gst_video_parse_get_plane_size (GstVideoInfo * info, guint plane)
{
  gsize size = 0;

  if (GST_VIDEO_FORMAT_INFO_IS_TILED (info->finfo)) {
    gint tile_width, tile_height, x_tiles, y_tiles;

    tile_width = 1 << GST_VIDEO_FORMAT_INFO_TILE_WS (info->finfo);
    tile_height = 1 << GST_VIDEO_FORMAT_INFO_TILE_HS (info->finfo);
    x_tiles = GST_VIDEO_TILE_X_TILES (info->stride[plane]);
    y_tiles = GST_VIDEO_TILE_Y_TILES (info->stride[plane]);

    /* plane size is the size of one tile multiplied by the number of tiles */
    size = tile_width * tile_height * x_tiles * y_tiles;
  } else {
    size = info->stride[plane] *
        GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (info->finfo, plane, info->height);
  }

  return size;
}


static gboolean
gst_video_parse_update_stride (GstVideoParse * vp)
{
  GstVideoInfo *info = &vp->info;
  guint i;

  /* 1. check that provided strides are greater than the default ones */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    if (GST_VIDEO_FORMAT_INFO_IS_TILED (info->finfo)) {
      /* for tiled format, make sure there is more tiles than default */
      gint default_x_tiles, default_y_tiles, x_tiles, y_tiles;

      x_tiles = GST_VIDEO_TILE_X_TILES (vp->stride[i]);
      y_tiles = GST_VIDEO_TILE_Y_TILES (vp->stride[i]);
      default_x_tiles = GST_VIDEO_TILE_X_TILES (info->stride[i]);
      default_y_tiles = GST_VIDEO_TILE_Y_TILES (info->stride[i]);

      if (x_tiles < default_x_tiles) {
        GST_WARNING_OBJECT (vp,
            "x_tiles for plane %u is too small: got %d, min %d", i, x_tiles,
            default_x_tiles);
        return FALSE;
      }

      if (y_tiles < default_y_tiles) {
        GST_WARNING_OBJECT (vp,
            "y_tiles for plane %u is too small: got %d, min %d", i, y_tiles,
            default_y_tiles);
        return FALSE;
      }
    } else {
      if (vp->stride[i] < info->stride[i]) {
        GST_WARNING_OBJECT (vp,
            "stride for plane %u is too small: got %d, min %d", i,
            vp->stride[i], info->stride[i]);
        return FALSE;
      }
    }
  }

  /* 2. update stride and plane offsets */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    if (vp->stride[i] != info->stride[i]) {
      info->stride[i] = vp->stride[i];

      if (i > 0) {
        /* update offset to reflect stride change for plane > 0 */
        info->offset[i] = info->offset[i - 1] +
            gst_video_parse_get_plane_size (info, i - 1);
      }

      vp->need_videometa = TRUE;
    }
  }

  return TRUE;
}

static gboolean
gst_video_parse_update_offset (GstVideoParse * vp)
{
  GstVideoInfo *info = &vp->info;
  guint i;

  /* 1. check that provided offsets are greaters than the default ones and are
   * consistent with plane size */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    gsize min_offset = info->offset[i];

    if (i > 0)
      min_offset = MAX (min_offset,
          vp->offset[i - 1] + gst_video_parse_get_plane_size (info, i - 1));

    if (vp->offset[i] < min_offset) {
      GST_WARNING_OBJECT (vp,
          "offset for plane %u is too small: got %d, min %" G_GSIZE_FORMAT, i,
          vp->offset[i], min_offset);
      return FALSE;
    }
  }

  /* 2. update offsets */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    if (vp->offset[i] != info->offset[i]) {
      info->offset[i] = vp->offset[i];
      vp->need_videometa = TRUE;
    }
  }

  return TRUE;
}

static void
gst_video_parse_update_info (GstVideoParse * vp)
{
  GstVideoInfo *info = &vp->info;
  gint fps_n, fps_d;
  gint framesize;
  guint i;
  gboolean update_size = FALSE;

  gst_raw_parse_get_fps (GST_RAW_PARSE (vp), &fps_n, &fps_d);

  gst_video_info_init (info);
  gst_video_info_set_format (info, vp->format, vp->width, vp->height);
  info->fps_n = fps_n;
  info->fps_d = fps_d;
  info->par_n = vp->par_n;
  info->par_d = vp->par_d;
  info->interlace_mode = vp->interlaced ?
      GST_VIDEO_INTERLACE_MODE_INTERLEAVED :
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  vp->need_videometa = FALSE;

  if (vp->stride_set) {
    if (gst_video_parse_update_stride (vp))
      update_size = TRUE;
    else
      GST_WARNING_OBJECT (vp, "invalid strides set, use default ones");
  }

  if (vp->offset_set) {
    if (gst_video_parse_update_offset (vp))
      update_size = TRUE;
    else
      GST_WARNING_OBJECT (vp, "invalid offsets set, use default ones");
  }

  if (update_size) {
    framesize = 0;

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
      gint planesize = info->offset[i];
      planesize += gst_video_parse_get_plane_size (info, i);

      if (planesize > framesize)
        framesize = planesize;
    }

    info->size = framesize;
  }

  if (vp->framesize) {
    /* user requires a specific framesize, just make sure it's bigger than
     * the current one */

    if (vp->framesize > vp->info.size)
      vp->info.size = vp->framesize;
    else
      GST_WARNING_OBJECT (vp, "invalid framesize set: got %u, min: %"
          G_GSIZE_FORMAT, vp->framesize, vp->info.size);
  }

  GST_DEBUG_OBJECT (vp, "video info: %ux%u, format %s, size %" G_GSIZE_FORMAT
      ", stride {%d,%d,%d,%d}, offset {%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT
      ",%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT "}",
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
      GST_VIDEO_INFO_NAME (info), GST_VIDEO_INFO_SIZE (info),
      info->stride[0], info->stride[1], info->stride[2], info->stride[3],
      info->offset[0], info->offset[1], info->offset[2], info->offset[3]);

  /* update base class framesize */
  framesize = GST_VIDEO_INFO_SIZE (info);
  gst_raw_parse_set_framesize (GST_RAW_PARSE (vp), framesize);
}

static GstCaps *
gst_video_parse_get_caps (GstRawParse * rp)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (rp);

  return gst_video_info_to_caps (&vp->info);
}

static gboolean
gst_video_parse_copy_frame (GstVideoParse * vp, GstBuffer * dest,
    GstVideoInfo * dest_info, GstBuffer * src, GstVideoInfo * src_info)
{
  GstVideoFrame src_frame;
  GstVideoFrame dest_frame;
  gboolean ret;

  if (!gst_video_frame_map (&src_frame, src_info, src, GST_MAP_READ)) {
    GST_ERROR_OBJECT (vp, "failed to map src frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&dest_frame, dest_info, dest, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (vp, "failed to map dest frame");
    gst_video_frame_unmap (&src_frame);
    return FALSE;
  }

  ret = gst_video_frame_copy (&dest_frame, &src_frame);

  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dest_frame);

  return ret;
}

static void
gst_video_parse_pre_push_buffer (GstRawParse * rp, GstBuffer * buffer)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (rp);

  if (vp->do_copy) {
    GstVideoInfo info;
    GstBuffer *outbuf;

    gst_video_info_init (&info);
    gst_video_info_set_format (&info, vp->format, vp->width, vp->height);

    GST_DEBUG_OBJECT (vp, "copying frame to remove padding");

    outbuf = gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (&info), NULL);

    if (!gst_video_parse_copy_frame (vp, outbuf, &info, buffer, &vp->info))
      GST_WARNING_OBJECT (vp, "failed to copy frame");

    gst_buffer_replace_all_memory (buffer, gst_buffer_get_all_memory (outbuf));
    gst_buffer_unref (outbuf);
  } else {
    GstVideoInfo *info = &vp->info;
    GstVideoFrameFlags flags = GST_VIDEO_FRAME_FLAG_NONE;

    if (vp->interlaced && vp->top_field_first)
      flags = GST_VIDEO_FRAME_FLAG_TFF;

    gst_buffer_add_video_meta_full (buffer, flags, GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
        GST_VIDEO_INFO_N_PLANES (info), info->offset, info->stride);
  }

  if (vp->interlaced) {
    if (vp->top_field_first) {
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    } else {
      GST_BUFFER_FLAG_UNSET (buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    }
  }
}

static void
gst_video_parse_decide_allocation (GstRawParse * rp, GstQuery * query)
{
  GstVideoParse *vp = GST_VIDEO_PARSE (rp);
  gboolean has_videometa;

  has_videometa = gst_query_find_allocation_meta (query,
      GST_VIDEO_META_API_TYPE, NULL);

  /* no need to copy if downstream supports videometa or if we don't need
   * them */
  if (has_videometa || !vp->need_videometa)
    return;

  vp->do_copy = TRUE;
}
