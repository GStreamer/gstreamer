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

/* Implementation of SMPTE 422M - Mapping JPEG2000 codestreams into the MXF
 * Generic Container
 */

/* TODO:
 *  - parse the jpeg2000 sub-descriptor, see SMPTE 422M 7.2
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfjpeg2000.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

gboolean
mxf_is_jpeg2000_essence_track (const MXFMetadataTrack * track)
{
  guint i;

  g_return_val_if_fail (track != NULL, FALSE);

  if (track->descriptor == NULL)
    return FALSE;

  for (i = 0; i < track->n_descriptor; i++) {
    MXFMetadataFileDescriptor *d = track->descriptor[i];
    MXFUL *key = &d->essence_container;
    /* SMPTE 422M 5.4 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 && key->u[13] == 0x0c &&
        (key->u[14] == 0x01 || key->u[14] == 0x02))
      return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
mxf_jpeg2000_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, MXFMetadataStructuralComponent * component,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 422M 5.1 */
  if (key->u[12] != 0x15 || (key->u[14] != 0x08 && key->u[14] != 0x09)) {
    GST_ERROR ("Invalid JPEG2000 essence element");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}


GstCaps *
mxf_jpeg2000_create_caps (MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, GstTagList ** tags,
    MXFEssenceElementHandler * handler, gpointer * mapping_data)
{
  MXFMetadataFileDescriptor *f = NULL;
  MXFMetadataGenericPictureEssenceDescriptor *p = NULL;
  guint i;
  GstCaps *caps = NULL;

  g_return_val_if_fail (package != NULL, NULL);
  g_return_val_if_fail (track != NULL, NULL);

  if (track->descriptor == NULL) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  for (i = 0; i < track->n_descriptor; i++) {
    if (((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR ||
        ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_RGBA_PICTURE_ESSENCE_DESCRIPTOR ||
        ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR) {
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->descriptor[i];
      f = track->descriptor[i];
      break;
    } else if (((MXFMetadataGenericDescriptor *) track->
            descriptor[i])->is_file_descriptor
        && ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type !=
        MXF_METADATA_MULTIPLE_DESCRIPTOR) {
      f = track->descriptor[i];
    }
  }

  if (!f) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  *handler = mxf_jpeg2000_handle_essence_element;

  /* TODO: What about other field values? */
  caps = gst_caps_new_simple ("image/x-j2c", "fields", G_TYPE_INT, 1, NULL);
  if (p) {
    mxf_metadata_generic_picture_essence_descriptor_set_caps (p, caps);
  } else {
    GST_WARNING ("Only a generic file descriptor found");
  }

  if (!*tags)
    *tags = gst_tag_list_new ();
  gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
      "JPEG 2000", NULL);

  return caps;
}
