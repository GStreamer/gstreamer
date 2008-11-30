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

/* Implementation of SMPTE 381M - Mapping MPEG streams into the MXF
 * Generic Container
 */

/* TODO:
 * - Handle PES streams
 * - Fix TS/PS demuxers to forward timestamps
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfmpeg.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

/* SMPTE 381M 8.1 - ULs of local tags */
static const guint8 _single_sequence_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x02, 0x00, 0x00
};

static const guint8 _constant_b_frames_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x03, 0x00, 0x00
};

static const guint8 _coded_content_type_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x04, 0x00, 0x00
};

static const guint8 _low_delay_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x05, 0x00, 0x00
};

static const guint8 _closed_gop_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x06, 0x00, 0x00
};

static const guint8 _identical_gop_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x07, 0x00, 0x00
};

static const guint8 _max_gop_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x08, 0x00, 0x00
};

static const guint8 _b_picture_count_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x09, 0x00, 0x00
};

static const guint8 _bitrate_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x0b, 0x00, 0x00
};

static const guint8 _profile_and_level_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x04, 0x01, 0x06, 0x02, 0x01,
  0x0a, 0x00, 0x00
};

gboolean
mxf_metadata_mpeg_video_descriptor_handle_tag (MXFMetadataGenericDescriptor * d,
    const MXFPrimerPack * primer, guint16 tag, const guint8 * tag_data,
    guint16 tag_size)
{
  MXFMetadataMPEGVideoDescriptor *descriptor =
      (MXFMetadataMPEGVideoDescriptor *) d;
  gboolean ret = FALSE;
  MXFUL *tag_ul = NULL;

  if (!(tag_ul =
          (MXFUL *) g_hash_table_lookup (primer->mappings,
              GUINT_TO_POINTER (((guint) tag)))))
    return FALSE;

  if (memcmp (tag_ul, &_single_sequence_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    descriptor->single_sequence = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  single sequence = %s",
        (descriptor->single_sequence) ? "yes" : "no");
    ret = TRUE;
  } else if (memcmp (tag_ul, &_constant_b_frames_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    descriptor->const_b_frames = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  constant b frames = %s",
        (descriptor->single_sequence) ? "yes" : "no");
    ret = TRUE;
  } else if (memcmp (tag_ul, &_coded_content_type_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    descriptor->coded_content_type = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  coded content type = %u", descriptor->coded_content_type);
    ret = TRUE;
  } else if (memcmp (tag_ul, &_low_delay_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    descriptor->low_delay = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  low delay = %s", (descriptor->low_delay) ? "yes" : "no");
    ret = TRUE;
  } else if (memcmp (tag_ul, &_closed_gop_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    descriptor->closed_gop = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  closed gop = %s", (descriptor->closed_gop) ? "yes" : "no");
    ret = TRUE;
  } else if (memcmp (tag_ul, &_identical_gop_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    descriptor->identical_gop = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  identical gop = %s",
        (descriptor->identical_gop) ? "yes" : "no");
    ret = TRUE;
  } else if (memcmp (tag_ul, &_max_gop_ul, 16) == 0) {
    if (tag_size != 2)
      goto error;
    descriptor->max_gop = GST_READ_UINT16_BE (tag_data);
    GST_DEBUG ("  max gop = %u", descriptor->max_gop);
    ret = TRUE;
  } else if (memcmp (tag_ul, &_b_picture_count_ul, 16) == 0) {
    if (tag_size != 2)
      goto error;
    descriptor->b_picture_count = GST_READ_UINT16_BE (tag_data);
    GST_DEBUG ("  b picture count = %u", descriptor->b_picture_count);
    ret = TRUE;
  } else if (memcmp (tag_ul, &_bitrate_ul, 16) == 0) {
    if (tag_size != 4)
      goto error;
    descriptor->bitrate = GST_READ_UINT32_BE (tag_data);
    GST_DEBUG ("  bitrate = %u", descriptor->bitrate);
    ret = TRUE;
  } else if (memcmp (tag_ul, &_profile_and_level_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    descriptor->profile_and_level = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  profile & level = %u", descriptor->profile_and_level);
    ret = TRUE;
  } else {
    ret =
        mxf_metadata_cdci_picture_essence_descriptor_handle_tag (d, primer, tag,
        tag_data, tag_size);
  }

  return ret;

error:
  GST_ERROR ("Invalid mpeg video descriptor tag 0x%04x of size %u", tag,
      tag_size);

  return FALSE;
}

void mxf_metadata_mpeg_video_descriptor_reset
    (MXFMetadataMPEGVideoDescriptor * descriptor)
{
  g_return_if_fail (descriptor != NULL);

  mxf_metadata_cdci_picture_essence_descriptor_reset (
      (MXFMetadataCDCIPictureEssenceDescriptor *) descriptor);

  MXF_METADATA_DESCRIPTOR_CLEAR (descriptor, MXFMetadataMPEGVideoDescriptor,
      MXFMetadataCDCIPictureEssenceDescriptor);
}

gboolean
mxf_is_mpeg_video_essence_track (const MXFMetadataTrack * track)
{
  guint i;

  g_return_val_if_fail (track != NULL, FALSE);

  if (track->descriptor == NULL)
    return FALSE;

  for (i = 0; i < track->n_descriptor; i++) {
    MXFMetadataFileDescriptor *d = track->descriptor[i];
    MXFUL *key = &d->essence_container;
    /* SMPTE 381M 7 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 &&
        (key->u[13] == 0x04 ||
            key->u[13] == 0x07 || key->u[13] == 0x08 || key->u[13] == 0x09))
      return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
mxf_mpeg_video_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, MXFMetadataStructuralComponent * component,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 381M 6.1 */
  if (key->u[12] != 0x15 || (key->u[14] != 0x05 && key->u[14] != 0x06
          && key->u[14] != 0x07)) {
    GST_ERROR ("Invalid MPEG video essence element");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

/* Private uid used by SONY C0023S01.mxf,
 * taken from the ffmpeg mxf demuxer */
static const guint8 sony_mpeg4_extradata[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x01, 0x0e, 0x06, 0x06, 0x02, 0x02,
  0x01, 0x00, 0x00
};

GstCaps *
mxf_mpeg_video_create_caps (MXFMetadataGenericPackage * package,
    MXFMetadataTrack * track, GstTagList ** tags,
    MXFEssenceElementHandler * handler, gpointer * mapping_data)
{
  MXFMetadataMPEGVideoDescriptor *d = NULL;
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
        MXF_METADATA_MPEG_VIDEO_DESCRIPTOR) {
      d = (MXFMetadataMPEGVideoDescriptor *) track->descriptor[i];
      f = track->descriptor[i];
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->descriptor[i];
      break;
    } else if (((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR ||
        ((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR) {
      f = track->descriptor[i];
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->descriptor[i];
      break;
    } else if (((MXFMetadataGenericDescriptor *) track->descriptor[i])->type ==
        MXF_METADATA_FILE_DESCRIPTOR) {
      f = track->descriptor[i];
    }
  }

  if (!f) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  *handler = mxf_mpeg_video_handle_essence_element;
  /* SMPTE 381M 7 */
  if (f->essence_container.u[13] == 0x04) {
    GST_DEBUG ("Found MPEG ES stream");

    /* SMPTE RP224 */
    if (!p || mxf_ul_is_zero (&p->picture_essence_coding)) {
      GST_WARNING
          ("No picture essence descriptor or no picture essence coding defined");
      caps =
          gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 2,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    } else if (p->picture_essence_coding.u[0] != 0x06
        || p->picture_essence_coding.u[1] != 0x0e
        || p->picture_essence_coding.u[2] != 0x2b
        || p->picture_essence_coding.u[3] != 0x34
        || p->picture_essence_coding.u[4] != 0x04
        || p->picture_essence_coding.u[5] != 0x01
        || p->picture_essence_coding.u[6] != 0x01
        || p->picture_essence_coding.u[8] != 0x04
        || p->picture_essence_coding.u[9] != 0x01
        || p->picture_essence_coding.u[10] != 0x02
        || p->picture_essence_coding.u[11] != 0x02
        || p->picture_essence_coding.u[12] != 0x01) {
      GST_ERROR ("No MPEG picture essence coding");
      caps = NULL;
    } else if (p->picture_essence_coding.u[13] >= 0x01 &&
        p->picture_essence_coding.u[13] <= 0x08) {
      caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 2,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    } else if (p->picture_essence_coding.u[13] == 0x10) {
      caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 1,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    } else if (p->picture_essence_coding.u[13] == 0x20) {
      MXFLocalTag *local_tag =
          (((MXFMetadataGenericDescriptor *) f)->
          other_tags) ? g_hash_table_lookup (((MXFMetadataGenericDescriptor *)
              f)->other_tags, &sony_mpeg4_extradata) : NULL;

      caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);

      if (local_tag) {
        GstBuffer *codec_data = NULL;
        codec_data = gst_buffer_new_and_alloc (local_tag->size);
        memcpy (GST_BUFFER_DATA (codec_data), local_tag->data, local_tag->size);
        gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data,
            NULL);
        gst_buffer_unref (codec_data);
      }

    } else {
      GST_ERROR ("Unsupported MPEG picture essence coding 0x%02x",
          p->picture_essence_coding.u[13]);
      caps = NULL;
    }
  } else if (f->essence_container.u[13] == 0x07) {
    GST_ERROR ("MPEG PES streams not supported yet");
    return NULL;
  } else if (f->essence_container.u[13] == 0x08) {
    /* FIXME: get mpeg version somehow */
    GST_DEBUG ("Found MPEG PS stream");
    caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 1,
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (f->essence_container.u[13] == 0x09) {
    GST_DEBUG ("Found MPEG TS stream");
    caps = gst_caps_new_simple ("video/mpegts", NULL);

  }

  if (p)
    mxf_metadata_generic_picture_essence_descriptor_set_caps (p, caps);

  return caps;
}
