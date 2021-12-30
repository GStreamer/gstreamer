/* GStreamer
 * Copyright (C) 2020-2021 Collabora Ltd.
 *   @author: Jakub Adam <jakub.adam@collabora.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:rtphdrextcolorspace
 * @title: GstRtphdrext-Colorspace
 * @short_description: Helper methods for dealing with Color Space RTP header
 * extension as defined in  http://www.webrtc.org/experiments/rtp-hdrext/color-space
 * @see_also: #GstRTPHeaderExtension, #GstRTPBasePayload, #GstRTPBaseDepayload
 *
 * Since: 1.20
 */

#include "gstrtphdrext-colorspace.h"

#include "gstrtpelements.h"

#include <gst/base/gstbytereader.h>
#include <gst/video/video-color.h>
#include <gst/video/video-hdr.h>

GST_DEBUG_CATEGORY_STATIC (rtphdrext_colorspace_debug);
#define GST_CAT_DEFAULT (rtphdrext_colorspace_debug)

/**
 * GstRTPHeaderExtensionColorspace:
 * @parent: the parent #GstRTPHeaderExtension
 *
 * Instance struct for Color Space RTP header extension.
 *
 * http://www.webrtc.org/experiments/rtp-hdrext/color-space
 */
struct _GstRTPHeaderExtensionColorspace
{
  GstRTPHeaderExtension parent;

  GstVideoColorimetry colorimetry;
  GstVideoChromaSite chroma_site;
  GstVideoMasteringDisplayInfo mdi;
  GstVideoContentLightLevel cll;
  gboolean has_hdr_meta;
};

G_DEFINE_TYPE_WITH_CODE (GstRTPHeaderExtensionColorspace,
    gst_rtp_header_extension_colorspace, GST_TYPE_RTP_HEADER_EXTENSION,
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "rtphdrextcolorspace", 0,
        "RTP Color Space Header Extension");
    );
GST_ELEMENT_REGISTER_DEFINE (rtphdrextcolorspace, "rtphdrextcolorspace",
    GST_RANK_MARGINAL, GST_TYPE_RTP_HEADER_EXTENSION_COLORSPACE);

static void
gst_rtp_header_extension_colorspace_init (GstRTPHeaderExtensionColorspace *
    self)
{
}

static GstRTPHeaderExtensionFlags
gst_rtp_header_extension_colorspace_get_supported_flags (GstRTPHeaderExtension *
    ext)
{
  GstRTPHeaderExtensionColorspace *self =
      GST_RTP_HEADER_EXTENSION_COLORSPACE (ext);

  return self->has_hdr_meta ?
      GST_RTP_HEADER_EXTENSION_TWO_BYTE : GST_RTP_HEADER_EXTENSION_ONE_BYTE;
}

static gsize
gst_rtp_header_extension_colorspace_get_max_size (GstRTPHeaderExtension * ext,
    const GstBuffer * buffer)
{
  GstRTPHeaderExtensionColorspace *self =
      GST_RTP_HEADER_EXTENSION_COLORSPACE (ext);

  return self->has_hdr_meta ?
      GST_RTP_HDREXT_COLORSPACE_WITH_HDR_META_SIZE :
      GST_RTP_HDREXT_COLORSPACE_SIZE;
}

static gssize
gst_rtp_header_extension_colorspace_write (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size)
{
  GstRTPHeaderExtensionColorspace *self =
      GST_RTP_HEADER_EXTENSION_COLORSPACE (ext);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean is_frame_last_buffer;
  guint8 *ptr = data;
  guint8 range;
  guint8 horizontal_site;
  guint8 vertical_site;

  g_return_val_if_fail (size >=
      gst_rtp_header_extension_colorspace_get_max_size (ext, NULL), -1);
  g_return_val_if_fail (write_flags &
      gst_rtp_header_extension_colorspace_get_supported_flags (ext), -1);

  if (self->colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN &&
      self->colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_UNKNOWN &&
      self->colorimetry.range == GST_VIDEO_COLOR_RANGE_UNKNOWN &&
      self->colorimetry.transfer == GST_VIDEO_TRANSFER_UNKNOWN) {
    /* Nothing to write. */
    return 0;
  }

  gst_rtp_buffer_map (output, GST_MAP_READ, &rtp);
  is_frame_last_buffer = gst_rtp_buffer_get_marker (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  if (!is_frame_last_buffer) {
    /* Only a video frame's final packet should carry color space info. */
    return 0;
  }

  *ptr++ = gst_video_color_primaries_to_iso (self->colorimetry.primaries);
  *ptr++ = gst_video_transfer_function_to_iso (self->colorimetry.transfer);
  *ptr++ = gst_video_color_matrix_to_iso (self->colorimetry.matrix);

  switch (self->colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      range = 2;
      break;
    case GST_VIDEO_COLOR_RANGE_16_235:
      range = 1;
      break;
    default:
      range = 0;
      break;
  }

  if (self->chroma_site & GST_VIDEO_CHROMA_SITE_H_COSITED) {
    horizontal_site = 1;
  } else if (self->chroma_site & GST_VIDEO_CHROMA_SITE_NONE) {
    horizontal_site = 2;
  } else {
    horizontal_site = 0;
  }

  if (self->chroma_site & GST_VIDEO_CHROMA_SITE_V_COSITED) {
    vertical_site = 1;
  } else if (self->chroma_site & GST_VIDEO_CHROMA_SITE_NONE) {
    vertical_site = 2;
  } else {
    vertical_site = 0;
  }

  *ptr++ = (range << 4) + (horizontal_site << 2) + vertical_site;

  if (self->has_hdr_meta) {
    guint i;

    GST_WRITE_UINT16_BE (ptr,
        self->mdi.max_display_mastering_luminance / 10000);
    ptr += 2;
    GST_WRITE_UINT16_BE (ptr, self->mdi.min_display_mastering_luminance);
    ptr += 2;

    for (i = 0; i < 3; ++i) {
      GST_WRITE_UINT16_BE (ptr, self->mdi.display_primaries[i].x);
      ptr += 2;
      GST_WRITE_UINT16_BE (ptr, self->mdi.display_primaries[i].y);
      ptr += 2;
    }

    GST_WRITE_UINT16_BE (ptr, self->mdi.white_point.x);
    ptr += 2;
    GST_WRITE_UINT16_BE (ptr, self->mdi.white_point.y);
    ptr += 2;

    GST_WRITE_UINT16_BE (ptr, self->cll.max_content_light_level);
    ptr += 2;
    GST_WRITE_UINT16_BE (ptr, self->cll.max_frame_average_light_level);
    ptr += 2;
  }

  return ptr - data;
}

static gboolean
parse_colorspace (GstByteReader * reader, GstVideoColorimetry * colorimetry,
    GstVideoChromaSite * chroma_site)
{
  guint8 val;

  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (colorimetry != NULL, FALSE);
  g_return_val_if_fail (chroma_site != NULL, FALSE);

  if (gst_byte_reader_get_remaining (reader) < GST_RTP_HDREXT_COLORSPACE_SIZE) {
    return FALSE;
  }

  if (!gst_byte_reader_get_uint8 (reader, &val)) {
    return FALSE;
  }
  colorimetry->primaries = gst_video_color_primaries_from_iso (val);

  if (!gst_byte_reader_get_uint8 (reader, &val)) {
    return FALSE;
  }
  colorimetry->transfer = gst_video_transfer_function_from_iso (val);

  if (!gst_byte_reader_get_uint8 (reader, &val)) {
    return FALSE;
  }
  colorimetry->matrix = gst_video_color_matrix_from_iso (val);

  *chroma_site = GST_VIDEO_CHROMA_SITE_UNKNOWN;

  if (!gst_byte_reader_get_uint8 (reader, &val)) {
    return FALSE;
  }
  switch ((val >> 2) & 0x03) {
    case 1:
      *chroma_site |= GST_VIDEO_CHROMA_SITE_H_COSITED;
      break;
    case 2:
      *chroma_site |= GST_VIDEO_CHROMA_SITE_NONE;
      break;
  }

  switch (val & 0x03) {
    case 1:
      *chroma_site |= GST_VIDEO_CHROMA_SITE_V_COSITED;
      break;
    case 2:
      *chroma_site |= GST_VIDEO_CHROMA_SITE_NONE;
      break;
  }

  switch (val >> 4) {
    case 1:
      colorimetry->range = GST_VIDEO_COLOR_RANGE_16_235;
      break;
    case 2:
      colorimetry->range = GST_VIDEO_COLOR_RANGE_0_255;
      break;
    default:
      colorimetry->range = GST_VIDEO_COLOR_RANGE_UNKNOWN;
      break;
  }

  return TRUE;
}

static gboolean
parse_colorspace_with_hdr_meta (GstByteReader * reader,
    GstVideoColorimetry * colorimetry,
    GstVideoChromaSite * chroma_site,
    GstVideoMasteringDisplayInfo * mastering_display_info,
    GstVideoContentLightLevel * content_light_level)
{
  guint i;
  guint16 val16;

  g_return_val_if_fail (reader != NULL, FALSE);
  g_return_val_if_fail (mastering_display_info != NULL, FALSE);
  g_return_val_if_fail (content_light_level != NULL, FALSE);

  if (gst_byte_reader_get_remaining (reader) <
      GST_RTP_HDREXT_COLORSPACE_WITH_HDR_META_SIZE) {
    return FALSE;
  }

  if (!parse_colorspace (reader, colorimetry, chroma_site)) {
    return FALSE;
  }

  if (!gst_byte_reader_get_uint16_be (reader, &val16)) {
    return FALSE;
  }
  mastering_display_info->max_display_mastering_luminance = val16 * 10000;

  if (!gst_byte_reader_get_uint16_be (reader, &val16)) {
    return FALSE;
  }
  mastering_display_info->min_display_mastering_luminance = val16;

  for (i = 0; i < 3; ++i) {
    if (!gst_byte_reader_get_uint16_be (reader,
            &mastering_display_info->display_primaries[i].x)) {
      return FALSE;
    }

    if (!gst_byte_reader_get_uint16_be (reader,
            &mastering_display_info->display_primaries[i].y)) {
      return FALSE;
    }
  }

  if (!gst_byte_reader_get_uint16_be (reader,
          &mastering_display_info->white_point.x)) {
    return FALSE;
  }
  if (!gst_byte_reader_get_uint16_be (reader,
          &mastering_display_info->white_point.y)) {
    return FALSE;
  }

  if (!gst_byte_reader_get_uint16_be (reader,
          &content_light_level->max_content_light_level)) {
    return FALSE;
  }
  if (!gst_byte_reader_get_uint16_be (reader,
          &content_light_level->max_frame_average_light_level)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_rtp_header_extension_colorspace_read (GstRTPHeaderExtension * ext,
    GstRTPHeaderExtensionFlags read_flags, const guint8 * data, gsize size,
    GstBuffer * buffer)
{
  GstRTPHeaderExtensionColorspace *self =
      GST_RTP_HEADER_EXTENSION_COLORSPACE (ext);
  gboolean has_hdr_meta;
  GstByteReader *reader;
  GstVideoColorimetry colorimetry;
  GstVideoChromaSite chroma_site;
  GstVideoMasteringDisplayInfo mdi;
  GstVideoContentLightLevel cll;
  gboolean caps_update_needed;
  gboolean result;

  if (size != GST_RTP_HDREXT_COLORSPACE_SIZE &&
      size != GST_RTP_HDREXT_COLORSPACE_WITH_HDR_META_SIZE) {
    GST_WARNING_OBJECT (ext, "Invalid Color Space header extension size %"
        G_GSIZE_FORMAT, size);
    return FALSE;
  }

  has_hdr_meta = size == GST_RTP_HDREXT_COLORSPACE_WITH_HDR_META_SIZE;

  reader = gst_byte_reader_new (data, size);

  if (has_hdr_meta) {
    result = parse_colorspace_with_hdr_meta (reader, &colorimetry, &chroma_site,
        &mdi, &cll);
  } else {
    result = parse_colorspace (reader, &colorimetry, &chroma_site);
  }

  g_clear_pointer (&reader, gst_byte_reader_free);

  if (!gst_video_colorimetry_is_equal (&self->colorimetry, &colorimetry)) {
    caps_update_needed = TRUE;
    self->colorimetry = colorimetry;
  }

  if (self->chroma_site != chroma_site) {
    caps_update_needed = TRUE;
    self->chroma_site = chroma_site;
  }

  if (self->has_hdr_meta != has_hdr_meta) {
    caps_update_needed = TRUE;
    self->has_hdr_meta = has_hdr_meta;
  }

  if (has_hdr_meta) {
    if (!gst_video_mastering_display_info_is_equal (&self->mdi, &mdi)) {
      caps_update_needed = TRUE;
      self->mdi = mdi;
    }
    if (!gst_video_content_light_level_is_equal (&self->cll, &cll)) {
      caps_update_needed = TRUE;
      self->cll = cll;
    }
  }

  if (caps_update_needed) {
    gst_rtp_header_extension_set_wants_update_non_rtp_src_caps (ext, TRUE);
  }

  return result;
}

static gboolean
    gst_rtp_header_extension_colorspace_set_non_rtp_sink_caps
    (GstRTPHeaderExtension * ext, const GstCaps * caps)
{
  GstRTPHeaderExtensionColorspace *self =
      GST_RTP_HEADER_EXTENSION_COLORSPACE (ext);
  GstStructure *s;
  const gchar *colorimetry;
  const gchar *chroma_site;

  s = gst_caps_get_structure (caps, 0);

  colorimetry = gst_structure_get_string (s, "colorimetry");
  if (colorimetry) {
    gst_video_colorimetry_from_string (&self->colorimetry, colorimetry);

    self->has_hdr_meta =
        gst_video_mastering_display_info_from_caps (&self->mdi, caps);

    gst_video_content_light_level_from_caps (&self->cll, caps);
  }

  chroma_site = gst_structure_get_string (s, "chroma-site");
  if (chroma_site) {
    self->chroma_site = gst_video_chroma_from_string (chroma_site);
  }

  return TRUE;
}

static gboolean
    gst_rtp_header_extension_colorspace_update_non_rtp_src_caps
    (GstRTPHeaderExtension * ext, GstCaps * caps)
{
  GstRTPHeaderExtensionColorspace *self =
      GST_RTP_HEADER_EXTENSION_COLORSPACE (ext);

  gchar *color_str;

  gst_structure_remove_fields (gst_caps_get_structure (caps, 0),
      "mastering-display-info", "content-light-level", NULL);

  if ((color_str = gst_video_colorimetry_to_string (&self->colorimetry))) {
    gst_caps_set_simple (caps, "colorimetry", G_TYPE_STRING, color_str, NULL);
    g_free (color_str);
  }
  if (self->chroma_site != GST_VIDEO_CHROMA_SITE_UNKNOWN) {
    gst_caps_set_simple (caps, "chroma-site", G_TYPE_STRING,
        gst_video_chroma_to_string (self->chroma_site), NULL);
  }
  if (self->has_hdr_meta) {
    gst_video_mastering_display_info_add_to_caps (&self->mdi, caps);
    gst_video_content_light_level_add_to_caps (&self->cll, caps);
  }

  return TRUE;
}

static void
    gst_rtp_header_extension_colorspace_class_init
    (GstRTPHeaderExtensionColorspaceClass * klass)
{
  GstRTPHeaderExtensionClass *rtp_hdr_class =
      GST_RTP_HEADER_EXTENSION_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  rtp_hdr_class->get_supported_flags =
      gst_rtp_header_extension_colorspace_get_supported_flags;
  rtp_hdr_class->get_max_size =
      gst_rtp_header_extension_colorspace_get_max_size;
  rtp_hdr_class->write = gst_rtp_header_extension_colorspace_write;
  rtp_hdr_class->read = gst_rtp_header_extension_colorspace_read;
  rtp_hdr_class->set_non_rtp_sink_caps =
      gst_rtp_header_extension_colorspace_set_non_rtp_sink_caps;
  rtp_hdr_class->update_non_rtp_src_caps =
      gst_rtp_header_extension_colorspace_update_non_rtp_src_caps;

  gst_element_class_set_static_metadata (gstelement_class,
      "Color Space", GST_RTP_HDREXT_ELEMENT_CLASS,
      "Extends RTP packets with color space and high dynamic range (HDR) information.",
      "Jakub Adam <jakub.adam@collabora.com>");
  gst_rtp_header_extension_class_set_uri (rtp_hdr_class,
      GST_RTP_HDREXT_COLORSPACE_URI);
}
