/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include <gst/video/video.h>

#include "mxfup.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

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
    MXFMetadataStructuralComponent * component, gpointer mapping_data,
    GstBuffer ** outbuf)
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

  if (!d->pixel_layout) {
    GST_ERROR ("No pixel layout");
    return NULL;
  }

  if (d->n_pixel_layout == 3) {
    if (d->pixel_layout[0] == 'R' && d->pixel_layout[2] == 'G'
        && d->pixel_layout[4] == 'B' && d->pixel_layout[1] == 8
        && d->pixel_layout[3] == 8 && d->pixel_layout[5] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_RGB);
    } else if (d->pixel_layout[0] == 'B' && d->pixel_layout[2] == 'G'
        && d->pixel_layout[4] == 'R' && d->pixel_layout[1] == 8
        && d->pixel_layout[3] == 8 && d->pixel_layout[5] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_BGR);
    } else {
      GST_ERROR ("Unsupport 3 component pixel layout");
      return NULL;
    }
  } else if (d->n_pixel_layout == 4) {
    if (d->pixel_layout[0] == 'R' && d->pixel_layout[2] == 'G'
        && d->pixel_layout[4] == 'B' && d->pixel_layout[6] == 'F'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_RGBx);
    } else if (d->pixel_layout[0] == 'B' && d->pixel_layout[2] == 'G'
        && d->pixel_layout[4] == 'R' && d->pixel_layout[6] == 'F'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_BGRx);
    } else if (d->pixel_layout[0] == 'F' && d->pixel_layout[2] == 'R'
        && d->pixel_layout[4] == 'G' && d->pixel_layout[6] == 'B'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_xRGB);
    } else if (d->pixel_layout[0] == 'F' && d->pixel_layout[2] == 'B'
        && d->pixel_layout[4] == 'G' && d->pixel_layout[6] == 'R'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_xBGR);
    } else if (d->pixel_layout[0] == 'A' && d->pixel_layout[2] == 'R'
        && d->pixel_layout[4] == 'G' && d->pixel_layout[6] == 'B'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_ARGB);
    } else if (d->pixel_layout[0] == 'A' && d->pixel_layout[2] == 'B'
        && d->pixel_layout[4] == 'G' && d->pixel_layout[6] == 'R'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_ABGR);
    } else if (d->pixel_layout[0] == 'R' && d->pixel_layout[2] == 'G'
        && d->pixel_layout[4] == 'B' && d->pixel_layout[6] == 'A'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_RGBA);
    } else if (d->pixel_layout[0] == 'B' && d->pixel_layout[2] == 'G'
        && d->pixel_layout[4] == 'R' && d->pixel_layout[6] == 'A'
        && d->pixel_layout[1] == 8 && d->pixel_layout[3] == 8
        && d->pixel_layout[5] == 8 && d->pixel_layout[7] == 8) {
      caps = gst_caps_from_string (GST_VIDEO_CAPS_BGRA);
    } else {
      GST_ERROR ("Unsupport 4 component pixel layout");
      return NULL;
    }
  } else {
    GST_ERROR ("Pixel layouts with %u components not supported yet",
        d->n_pixel_layout);
    return NULL;
  }

  if (caps) {
    MXFUPMappingData *data = g_new0 (MXFUPMappingData, 1);

    data->image_start_offset =
        ((MXFMetadataGenericPictureEssenceDescriptor *) d)->image_start_offset;
    data->image_end_offset =
        ((MXFMetadataGenericPictureEssenceDescriptor *) d)->image_end_offset;

    *mapping_data = data;
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

    if (MXF_IS_METADATA_RGBA_PICTURE_ESSENCE_DESCRIPTOR (track->parent.
            descriptor[i])) {
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->parent.
          descriptor[i];
      r = (MXFMetadataRGBAPictureEssenceDescriptor *) track->parent.
          descriptor[i];
      break;
    } else if (MXF_IS_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR (track->
            parent.descriptor[i])) {
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->parent.
          descriptor[i];
      c = (MXFMetadataCDCIPictureEssenceDescriptor *) track->parent.
          descriptor[i];
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

void
mxf_up_init (void)
{
  mxf_essence_element_handler_register (&mxf_up_essence_element_handler);
}
