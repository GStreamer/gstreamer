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

/* Implementation of SMPTE 383M - Mapping DV-DIF data into the MXF
 * Generic Container
 */

/* TODO:
 *  - playbin hangs on a lot of MXF/DV-DIF files (bug #563827)
 *  - decodebin2 creates loops inside the linking graph (bug #563828)
 *  - Forwarding of timestamps in dvdemux?
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfdv-dif.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

static gboolean
mxf_is_dv_dif_essence_track (const MXFMetadataTimelineTrack * track)
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
    /* SMPTE 383M 8 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 && key->u[13] == 0x02)
      return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
mxf_dv_dif_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps,
    MXFMetadataTimelineTrack * track,
    MXFMetadataSourceClip * component, gpointer mapping_data,
    GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 383 6.1.1 */
  if (key->u[12] != 0x18 || (key->u[14] != 0x01 && key->u[14] != 0x02)) {
    GST_ERROR ("Invalid DV-DIF essence element");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}


static GstCaps *
mxf_dv_dif_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  MXFMetadataFileDescriptor *f = NULL;
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

    if (MXF_IS_METADATA_FILE_DESCRIPTOR (track->parent.descriptor[i]) &&
        !MXF_IS_METADATA_MULTIPLE_DESCRIPTOR (track->parent.descriptor[i])) {
      f = track->parent.descriptor[i];
    }
  }

  if (!f) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  *handler = mxf_dv_dif_handle_essence_element;
  /* SMPTE 383M 8 */

  /* TODO: might be video or audio only, use values of the generic sound/picture
   * descriptor in the caps in that case
   */
  if (f->essence_container.u[13] == 0x02) {
    GST_DEBUG ("Found DV-DIF stream");
    caps =
        gst_caps_new_simple ("video/x-dv", "systemstream", G_TYPE_BOOLEAN, TRUE,
        NULL);

    if (!*tags)
      *tags = gst_tag_list_new ();

    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_CODEC, "DV-DIF",
        NULL);
  }

  return caps;
}

static const MXFEssenceElementHandler mxf_dv_dif_essence_element_handler = {
  mxf_is_dv_dif_essence_track,
  mxf_dv_dif_create_caps
};

void
mxf_dv_dif_init (void)
{
  mxf_essence_element_handler_register (&mxf_dv_dif_essence_element_handler);
}
