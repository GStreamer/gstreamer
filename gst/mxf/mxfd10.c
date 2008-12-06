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

/* Implementation of SMPTE 386M - Mapping Type-D10 essence data into the MXF
 * Generic Container
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfd10.h"
#include "mxfmpeg.h"
#include "mxfaes-bwf.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

gboolean
mxf_is_d10_essence_track (const MXFMetadataTrack * track)
{
  guint i;

  g_return_val_if_fail (track != NULL, FALSE);

  if (track->descriptor == NULL)
    return FALSE;

  for (i = 0; i < track->n_descriptor; i++) {
    MXFMetadataFileDescriptor *d = track->descriptor[i];
    MXFUL *key = &d->essence_container;
    /* SMPTE 386M 5.1 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 && key->u[13] == 0x01 &&
        (key->u[14] >= 0x01 && key->u[14] <= 0x06) &&
        (key->u[15] == 0x01 || key->u[15] == 0x02))
      return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
mxf_d10_picture_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, MXFMetadataStructuralComponent * component,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 386M 5.2.1 */
  if (key->u[12] != 0x05 || key->u[13] != 0x01 || key->u[14] != 0x01) {
    GST_ERROR ("Invalid D10 picture essence element");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
mxf_d10_sound_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, MXFMetadataStructuralComponent * component,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  guint i, j, nsamples;
  gint width, channels;
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const guint8 *indata;
  guint8 *outdata;

  if (!gst_structure_get_int (s, "width", &width) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR ("Invalid caps");
    return GST_FLOW_ERROR;
  }

  width /= 8;

  /* SMPTE 386M 5.3.1 */
  if (key->u[12] != 0x06 || key->u[13] != 0x01 || key->u[14] != 0x10) {
    GST_ERROR ("Invalid D10 sound essence element");
    return GST_FLOW_ERROR;
  }

  /* Now transform raw AES3 into raw audio, see SMPTE 331M */
  if ((GST_BUFFER_SIZE (buffer) - 4) % 32 != 0) {
    GST_ERROR ("Invalid D10 sound essence buffer size");
    return GST_FLOW_ERROR;
  }

  nsamples = ((GST_BUFFER_SIZE (buffer) - 4) / 4) / 8;

  *outbuf = gst_buffer_new_and_alloc (nsamples * width * channels);
  gst_buffer_copy_metadata (*outbuf, buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS |
      GST_BUFFER_COPY_CAPS);

  indata = GST_BUFFER_DATA (buffer);
  outdata = GST_BUFFER_DATA (*outbuf);

  /* Skip 32 bit header */
  indata += 4;

  for (i = 0; i < nsamples; i++) {
    for (j = 0; j < channels; j++) {
      guint32 in = GST_READ_UINT32_LE (indata);

      /* Remove first 4 and last 4 bits as they only
       * contain status data. Shift the 24 bit samples
       * to the correct width afterwards. */
      if (width == 2) {
        in = (in >> 12) & 0xffff;
        GST_WRITE_UINT16_LE (outdata, in);
      } else if (width == 3) {
        in = (in >> 4) & 0xffffff;
        GST_WRITE_UINT24_LE (outdata, in);
      }
      indata += 4;
      outdata += width;
    }
    /* There are always 8 channels but only the first
     * ones contain valid data, skip the others */
    indata += 4 * (8 - channels);
  }

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

GstCaps *
mxf_d10_create_caps (MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, GstTagList ** tags,
    MXFEssenceElementHandler * handler, gpointer * mapping_data)
{
  MXFMetadataGenericPictureEssenceDescriptor *p = NULL;
  MXFMetadataGenericSoundEssenceDescriptor *s = NULL;
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
        MXF_METADATA_MPEG_VIDEO_DESCRIPTOR ||
        ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR) {
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->descriptor[i];
      break;
    } else if (((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR ||
        ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_WAVE_AUDIO_ESSENCE_DESCRIPTOR ||
        ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_AES3_AUDIO_ESSENCE_DESCRIPTOR) {
      s = (MXFMetadataGenericSoundEssenceDescriptor *) track->descriptor[i];
      break;
    }
  }

  if (!s && !p) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  if (!*tags)
    *tags = gst_tag_list_new ();

  if (s) {
    if (s->channel_count == 0 ||
        s->quantization_bits == 0 ||
        s->audio_sampling_rate.n == 0 || s->audio_sampling_rate.d == 0) {
      GST_ERROR ("Invalid descriptor");
      return NULL;
    }

    if (s->quantization_bits != 16 && s->quantization_bits != 24) {
      GST_ERROR ("Invalid width %u", s->quantization_bits);
      return NULL;
    }

    /* FIXME: set channel layout */

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT,
        (gint) (((gdouble) s->audio_sampling_rate.n) /
            ((gdouble) s->audio_sampling_rate.d) + 0.5), "channels",
        G_TYPE_INT, s->channel_count, "signed", G_TYPE_BOOLEAN,
        (s->quantization_bits != 8), "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
        "depth", G_TYPE_INT, s->quantization_bits, "width", G_TYPE_INT,
        s->quantization_bits, NULL);

    *handler = mxf_d10_sound_handle_essence_element;

    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
        "SMPTE D-10 Audio", NULL);
  } else if (p) {
    caps =
        gst_caps_new_simple ("video/mpeg", "systemstream", G_TYPE_BOOLEAN,
        FALSE, "mpegversion", G_TYPE_INT, 2, NULL);
    mxf_metadata_generic_picture_essence_descriptor_set_caps (p, caps);

    *handler = mxf_d10_picture_handle_essence_element;
    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
        "SMPTE D-10 Video", NULL);
  }

  return caps;
}
