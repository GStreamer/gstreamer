/* GStreamer
 * Copyright (C) 2008-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* Implementation of SMPTE 384M - Mapping of Uncompressed Pictures into the MXF
 * Generic Container
 */

/* TODO: 
 *   - Handle CDCI essence
 *   - Handle more formats with RGBA descriptor (4:4:4 / 4:4:4:4 YUV, RGB656, ...)
 *   - Correctly transform for the GStreamer strides
 *   - Handle all the dimensions and other properties in the picture
 *     essence descriptors correctly according to S377M Annex E
 *   - Handle interlaced correctly, i.e. weave until we support one-field-per-buffer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include <gst/video/video.h>

#include "mxfup.h"
#include "mxfwrite.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

static const struct
{
  const gchar *caps;
  guint32 n_pixel_layout;
  guint8 pixel_layout[10];
} _rgba_mapping_table[] = {
  {
    GST_VIDEO_CAPS_RGB, 3, {
  'R', 8, 'G', 8, 'B', 8}}, {
    GST_VIDEO_CAPS_BGR, 3, {
  'B', 8, 'G', 8, 'R', 8}}, {
    GST_VIDEO_CAPS_YUV ("v308"), 3, {
  'Y', 8, 'U', 8, 'V', 8}}, {
    GST_VIDEO_CAPS_xRGB, 4, {
  'F', 8, 'R', 8, 'G', 8, 'B', 8}}, {
    GST_VIDEO_CAPS_RGBx, 4, {
  'R', 8, 'G', 8, 'B', 8, 'F', 8}}, {
    GST_VIDEO_CAPS_xBGR, 4, {
  'F', 8, 'B', 8, 'G', 8, 'R', 8}}, {
    GST_VIDEO_CAPS_BGRx, 4, {
  'B', 8, 'G', 8, 'R', 8, 'F', 8}}, {
    GST_VIDEO_CAPS_RGBA, 4, {
  'R', 8, 'G', 8, 'B', 8, 'A', 8}}, {
    GST_VIDEO_CAPS_ARGB, 4, {
  'A', 8, 'R', 8, 'G', 8, 'B', 8}}, {
    GST_VIDEO_CAPS_BGRA, 4, {
  'B', 8, 'G', 8, 'R', 8, 'A', 8}}, {
    GST_VIDEO_CAPS_ABGR, 4, {
  'A', 8, 'B', 8, 'G', 8, 'R', 8}}, {
    GST_VIDEO_CAPS_YUV ("AYUV"), 4, {
  'A', 8, 'Y', 8, 'U', 8, 'V', 8}}
};

typedef struct
{
  guint32 image_start_offset;
  guint32 image_end_offset;
} MXFUPMappingData;

static gboolean
mxf_is_up_essence_track (const MXFMetadataTimelineTrack * track)
{
  guint i;

  g_return_val_if_fail (track != NULL, FALSE);

  if (track->parent.descriptor == NULL)
    return FALSE;

  for (i = 0; i < track->parent.n_descriptor; i++) {
    MXFMetadataFileDescriptor *d = track->parent.descriptor[i];
    MXFUL *key;

    if (!d)
      continue;

    key = &d->essence_container;
    /* SMPTE 384M 8 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 && key->u[13] == 0x05 && key->u[15] <= 0x03)
      return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
mxf_up_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps,
    MXFMetadataTimelineTrack * track,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  MXFUPMappingData *data = mapping_data;

  /* SMPTE 384M 7.1 */
  if (key->u[12] != 0x15 || (key->u[14] != 0x01 && key->u[14] != 0x02
          && key->u[14] != 0x03 && key->u[14] != 0x04)) {
    GST_ERROR ("Invalid uncompressed picture essence element");
    return GST_FLOW_ERROR;
  }

  if (!data || (data->image_start_offset == 0 && data->image_end_offset == 0)) {
    *outbuf = buffer;
  } else {
    if (data->image_start_offset + data->image_end_offset
        > GST_BUFFER_SIZE (buffer)) {
      gst_buffer_unref (buffer);
      GST_ERROR ("Invalid buffer size");
      return GST_FLOW_ERROR;
    } else {
      GST_BUFFER_DATA (buffer) += data->image_start_offset;
      GST_BUFFER_SIZE (buffer) -= data->image_start_offset;
      GST_BUFFER_SIZE (buffer) -= data->image_end_offset;
      *outbuf = buffer;
    }
  }

  return GST_FLOW_OK;
}

static GstCaps *
mxf_up_rgba_create_caps (MXFMetadataTimelineTrack * track,
    MXFMetadataRGBAPictureEssenceDescriptor * d, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  GstCaps *caps = NULL;
  guint i;

  if (!d->pixel_layout) {
    GST_ERROR ("No pixel layout");
    return NULL;
  }

  for (i = 0; i < G_N_ELEMENTS (_rgba_mapping_table); i++) {
    if (d->n_pixel_layout != _rgba_mapping_table[i].n_pixel_layout)
      continue;

    if (memcmp (d->pixel_layout, &_rgba_mapping_table[i].pixel_layout,
            _rgba_mapping_table[i].n_pixel_layout * 2) == 0) {
      caps = gst_caps_from_string (_rgba_mapping_table[i].caps);
      break;
    }
  }

  if (caps) {
    MXFUPMappingData *data = g_new0 (MXFUPMappingData, 1);

    data->image_start_offset =
        ((MXFMetadataGenericPictureEssenceDescriptor *) d)->image_start_offset;
    data->image_end_offset =
        ((MXFMetadataGenericPictureEssenceDescriptor *) d)->image_end_offset;

    *mapping_data = data;
  } else {
    GST_WARNING ("Unsupported pixel layout");
  }

  return caps;
}

static GstCaps *
mxf_up_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  MXFMetadataGenericPictureEssenceDescriptor *p = NULL;
  MXFMetadataCDCIPictureEssenceDescriptor *c = NULL;
  MXFMetadataRGBAPictureEssenceDescriptor *r = NULL;
  guint i;
  GstCaps *caps = NULL;

  g_return_val_if_fail (track != NULL, NULL);

  if (track->parent.descriptor == NULL) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  for (i = 0; i < track->parent.n_descriptor; i++) {
    if (!track->parent.descriptor[i])
      continue;

    if (MXF_IS_METADATA_RGBA_PICTURE_ESSENCE_DESCRIPTOR (track->
            parent.descriptor[i])) {
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->
          parent.descriptor[i];
      r = (MXFMetadataRGBAPictureEssenceDescriptor *) track->
          parent.descriptor[i];
      break;
    } else if (MXF_IS_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR (track->parent.
            descriptor[i])) {
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->
          parent.descriptor[i];
      c = (MXFMetadataCDCIPictureEssenceDescriptor *) track->
          parent.descriptor[i];
    }
  }

  if (!p) {
    GST_ERROR ("No picture essence descriptor found for this track");
    return NULL;
  }

  *handler = mxf_up_handle_essence_element;

  if (r) {
    caps = mxf_up_rgba_create_caps (track, r, tags, handler, mapping_data);
  } else {
    GST_ERROR ("CDCI uncompressed picture essence not supported yet");
    return NULL;
  }

  if (caps) {
    mxf_metadata_generic_picture_essence_descriptor_set_caps (p, caps);
  }

  return caps;
}

static const MXFEssenceElementHandler mxf_up_essence_element_handler = {
  mxf_is_up_essence_track,
  mxf_up_create_caps
};

static GstFlowReturn
mxf_up_write_func (GstBuffer * buffer, GstCaps * caps, gpointer mapping_data,
    GstAdapter * adapter, GstBuffer ** outbuf, gboolean flush)
{
  *outbuf = buffer;
  return GST_FLOW_OK;
}

static const guint8 up_essence_container_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01,
  0x0D, 0x01, 0x03, 0x01, 0x02, 0x05, 0x7F, 0x01
};

static MXFMetadataFileDescriptor *
mxf_up_get_rgba_descriptor (GstPadTemplate * tmpl, GstCaps * caps,
    MXFEssenceElementWriteFunc * handler, gpointer * mapping_data)
{
  MXFMetadataRGBAPictureEssenceDescriptor *ret;
  guint i;
  GstCaps *tmp, *intersection;

  ret = (MXFMetadataRGBAPictureEssenceDescriptor *)
      gst_mini_object_new (MXF_TYPE_METADATA_RGBA_PICTURE_ESSENCE_DESCRIPTOR);

  for (i = 0; i < G_N_ELEMENTS (_rgba_mapping_table); i++) {
    tmp = gst_caps_from_string (_rgba_mapping_table[i].caps);
    intersection = gst_caps_intersect (caps, tmp);
    gst_caps_unref (tmp);

    if (!gst_caps_is_empty (intersection)) {
      gst_caps_unref (intersection);
      ret->n_pixel_layout = _rgba_mapping_table[i].n_pixel_layout;
      ret->pixel_layout = g_new0 (guint8, ret->n_pixel_layout * 2);
      memcpy (ret->pixel_layout, _rgba_mapping_table[i].pixel_layout,
          ret->n_pixel_layout * 2);
      break;
    }
    gst_caps_unref (intersection);
  }

  memcpy (&ret->parent.parent.essence_container, &up_essence_container_ul, 16);

  mxf_metadata_generic_picture_essence_descriptor_from_caps (&ret->parent,
      caps);

  *handler = mxf_up_write_func;

  return (MXFMetadataFileDescriptor *) ret;
}

static MXFMetadataFileDescriptor *
mxf_up_get_cdci_descriptor (GstPadTemplate * tmpl, GstCaps * caps,
    MXFEssenceElementWriteFunc * handler, gpointer * mapping_data)
{
  g_assert_not_reached ();
  return NULL;
}

static MXFMetadataFileDescriptor *
mxf_up_get_descriptor (GstPadTemplate * tmpl, GstCaps * caps,
    MXFEssenceElementWriteFunc * handler, gpointer * mapping_data)
{
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);
  if (strcmp (gst_structure_get_name (s), "video/x-raw-rgb") == 0) {
    return mxf_up_get_rgba_descriptor (tmpl, caps, handler, mapping_data);
  } else if (strcmp (gst_structure_get_name (s), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    if (!gst_structure_get_fourcc (s, "format", &fourcc))
      return NULL;

    if (fourcc == GST_MAKE_FOURCC ('A', 'Y', 'U', 'V') ||
        fourcc == GST_MAKE_FOURCC ('v', '3', '0', '8'))
      return mxf_up_get_rgba_descriptor (tmpl, caps, handler, mapping_data);

    return mxf_up_get_cdci_descriptor (tmpl, caps, handler, mapping_data);
  }

  g_assert_not_reached ();
}

static void
mxf_up_update_descriptor (MXFMetadataFileDescriptor * d, GstCaps * caps,
    gpointer mapping_data, GstBuffer * buf)
{
  return;
}

static void
mxf_up_get_edit_rate (MXFMetadataFileDescriptor * a, GstCaps * caps,
    gpointer mapping_data, GstBuffer * buf, MXFMetadataSourcePackage * package,
    MXFMetadataTimelineTrack * track, MXFFraction * edit_rate)
{
  edit_rate->n = a->sample_rate.n;
  edit_rate->d = a->sample_rate.d;
}

static guint32
mxf_up_get_track_number_template (MXFMetadataFileDescriptor * a,
    GstCaps * caps, gpointer mapping_data)
{
  return (0x15 << 24) | (0x02 << 8);
}

static MXFEssenceElementWriter mxf_up_essence_element_writer = {
  mxf_up_get_descriptor,
  mxf_up_update_descriptor,
  mxf_up_get_edit_rate,
  mxf_up_get_track_number_template,
  NULL,
  {{0,}}
};

void
mxf_up_init (void)
{
  mxf_essence_element_handler_register (&mxf_up_essence_element_handler);

  mxf_up_essence_element_writer.pad_template =
      gst_pad_template_new ("up_video_sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
      gst_caps_from_string (GST_VIDEO_CAPS_RGB "; "
          GST_VIDEO_CAPS_BGR "; "
          GST_VIDEO_CAPS_RGBx "; "
          GST_VIDEO_CAPS_xRGB "; "
          GST_VIDEO_CAPS_BGRx "; "
          GST_VIDEO_CAPS_xBGR "; "
          GST_VIDEO_CAPS_ARGB "; "
          GST_VIDEO_CAPS_RGBA "; "
          GST_VIDEO_CAPS_ABGR "; "
          GST_VIDEO_CAPS_BGRA "; "
          GST_VIDEO_CAPS_YUV ("AYUV") "; " GST_VIDEO_CAPS_YUV ("v308")));

  memcpy (&mxf_up_essence_element_writer.data_definition,
      mxf_metadata_track_identifier_get (MXF_METADATA_TRACK_PICTURE_ESSENCE),
      16);
  mxf_essence_element_writer_register (&mxf_up_essence_element_writer);
}
