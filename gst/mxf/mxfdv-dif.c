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

gboolean
mxf_is_dv_dif_essence_track (const MXFMetadataTrack * track)
{
  guint i;

  g_return_val_if_fail (track != NULL, FALSE);

  if (track->descriptor == NULL)
    return FALSE;

  for (i = 0; i < track->n_descriptor; i++) {
    MXFMetadataFileDescriptor *d = track->descriptor[i];
    MXFUL *key = &d->essence_container;
    /* SMPTE 383M 8 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 && key->u[13] == 0x02)
      return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
mxf_dv_dif_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, MXFMetadataStructuralComponent * component,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 383 6.1.1 */
  if (key->u[12] != 0x18 || (key->u[14] != 0x01 && key->u[14] != 0x02)) {
    GST_ERROR ("Invalid DV-DIF essence element");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}


GstCaps *
mxf_dv_dif_create_caps (MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, GstTagList ** tags,
    MXFEssenceElementHandler * handler, gpointer * mapping_data)
{
  MXFMetadataFileDescriptor *f = NULL;
  guint i;
  GstCaps *caps = NULL;

  g_return_val_if_fail (package != NULL, NULL);
  g_return_val_if_fail (track != NULL, NULL);

  if (track->descriptor == NULL) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  for (i = 0; i < track->n_descriptor; i++) {
    if (((MXFMetadataGenericDescriptor *) track->descriptor[i])->
        is_file_descriptor
        && ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type !=
        MXF_METADATA_MULTIPLE_DESCRIPTOR) {
      f = track->descriptor[i];
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
