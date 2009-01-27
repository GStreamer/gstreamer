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

/* Implementation of SMPTE 388M - Mapping A-Law coded audio into the MXF
 * Generic Container
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfalaw.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

static gboolean
mxf_is_alaw_essence_track (const MXFMetadataTimelineTrack * track)
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
    /* SMPTE 388M 6.1 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 && key->u[13] == 0x0a &&
        (key->u[14] == 0x01 || key->u[14] == 0x02 || key->u[14] == 0x03))
      return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
mxf_alaw_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps,
    MXFMetadataTimelineTrack * track,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 388M 5.1 */
  if (key->u[12] != 0x16 || (key->u[14] != 0x08 && key->u[14] != 0x09
          && key->u[14] != 0x0a)) {
    GST_ERROR ("Invalid A-Law essence element");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}


static GstCaps *
mxf_alaw_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  MXFMetadataGenericSoundEssenceDescriptor *s = NULL;
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

    if (MXF_IS_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR (track->parent.
            descriptor[i])) {
      s = (MXFMetadataGenericSoundEssenceDescriptor *) track->parent.
          descriptor[i];
      break;
    }
  }

  if (!s) {
    GST_ERROR ("No generic sound essence descriptor found for this track");
    return NULL;
  }

  *handler = mxf_alaw_handle_essence_element;

  if (s && s->audio_sampling_rate.n != 0 && s->audio_sampling_rate.d != 0 &&
      s->channel_count != 0) {

    caps = gst_caps_new_simple ("audio/x-alaw", NULL);
    mxf_metadata_generic_sound_essence_descriptor_set_caps (s, caps);

    /* TODO: Handle channel layout somehow?
     * Or is alaw limited to two channels? */
    if (!*tags)
      *tags = gst_tag_list_new ();

    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_AUDIO_CODEC,
        "A-law encoded audio", NULL);

  }

  return caps;
}

static const MXFEssenceElementHandler mxf_alaw_essence_element_handler = {
  mxf_is_alaw_essence_track,
  mxf_alaw_create_caps
};

void
mxf_alaw_init (void)
{
  mxf_essence_element_handler_register (&mxf_alaw_essence_element_handler);
}
