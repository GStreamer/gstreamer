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

/* Implementation of SMPTE S2019-4 - Mapping VC-3 coding units into the MXF
 * Generic Container
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfvc3.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

static const guint8 picture_essence_coding_vc3_avid[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x0e, 0x04, 0x02, 0x01, 0x02,
  0x04, 0x01, 0x00
};

static gboolean
mxf_is_vc3_essence_track (const MXFMetadataTimelineTrack * track)
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
    /* SMPTE S2019-4 7 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 && key->u[13] == 0x11 &&
        (key->u[14] == 0x01 || key->u[14] == 0x02)) {
      return TRUE;
    } else if (mxf_is_avid_essence_container_label (key)) {
      MXFMetadataGenericPictureEssenceDescriptor *p;

      if (!MXF_IS_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR (d))
        return FALSE;
      p = MXF_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR (d);

      key = &p->picture_essence_coding;
      if (memcmp (key, picture_essence_coding_vc3_avid, 16) == 0)
        return TRUE;
    }
  }

  return FALSE;
}

static GstFlowReturn
mxf_vc3_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps,
    MXFMetadataTimelineTrack * track,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 2019-4 6.1 */
  if (key->u[12] != 0x15 || (key->u[14] != 0x05 && key->u[14] != 0x06)) {
    GST_ERROR ("Invalid VC-3 essence element");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}


static GstCaps *
mxf_vc3_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  MXFMetadataFileDescriptor *f = NULL;
  MXFMetadataGenericPictureEssenceDescriptor *p = NULL;
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

    if (MXF_IS_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR (track->
            parent.descriptor[i])) {
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->parent.
          descriptor[i];
      f = track->parent.descriptor[i];
      break;
    } else if (MXF_IS_METADATA_FILE_DESCRIPTOR (track->parent.descriptor[i]) &&
        !MXF_IS_METADATA_MULTIPLE_DESCRIPTOR (track->parent.descriptor[i])) {
      f = track->parent.descriptor[i];
    }
  }

  if (!f) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  *handler = mxf_vc3_handle_essence_element;

  caps = gst_caps_new_simple ("video/x-dnxhd", NULL);
  if (p) {
    mxf_metadata_generic_picture_essence_descriptor_set_caps (p, caps);
  } else {
    GST_WARNING ("Only a generic file descriptor found");
  }

  if (!*tags)
    *tags = gst_tag_list_new ();
  gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
      "VC-3 Video", NULL);

  return caps;
}

static const MXFEssenceElementHandler mxf_vc3_essence_element_handler = {
  mxf_is_vc3_essence_track,
  mxf_vc3_create_caps
};

void
mxf_vc3_init (void)
{
  mxf_essence_element_handler_register (&mxf_vc3_essence_element_handler);
}
