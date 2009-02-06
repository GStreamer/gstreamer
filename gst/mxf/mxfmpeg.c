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

/* Implementation of SMPTE 381M - Mapping MPEG streams into the MXF
 * Generic Container
 */

/* TODO:
 * - Handle PES streams
 * - Fix TS/PS demuxers to forward timestamps
 * - h264 support (see SMPTE RP2008)
 * - AAC support
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "mxfmpeg.h"

#include <gst/base/gstbytereader.h>

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

/* SMPTE 381M 8.1 */
#define MXF_TYPE_METADATA_MPEG_VIDEO_DESCRIPTOR \
  (mxf_metadata_mpeg_video_descriptor_get_type())
#define MXF_METADATA_MPEG_VIDEO_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),MXF_TYPE_METADATA_MPEG_VIDEO_DESCRIPTOR, MXFMetadataMPEGVideoDescriptor))
#define MXF_IS_METADATA_MPEG_VIDEO_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MXF_TYPE_METADATA_MPEG_VIDEO_DESCRIPTOR))
typedef struct _MXFMetadataMPEGVideoDescriptor MXFMetadataMPEGVideoDescriptor;
typedef MXFMetadataBaseClass MXFMetadataMPEGVideoDescriptorClass;
GType mxf_metadata_mpeg_video_descriptor_get_type (void);

struct _MXFMetadataMPEGVideoDescriptor
{
  MXFMetadataCDCIPictureEssenceDescriptor parent;

  gboolean single_sequence;
  gboolean const_b_frames;
  guint8 coded_content_type;
  gboolean low_delay;

  gboolean closed_gop;
  gboolean identical_gop;
  guint16 max_gop;

  guint16 b_picture_count;
  guint32 bitrate;
  guint8 profile_and_level;
};

G_DEFINE_TYPE (MXFMetadataMPEGVideoDescriptor,
    mxf_metadata_mpeg_video_descriptor,
    MXF_TYPE_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR);

static gboolean
mxf_metadata_mpeg_video_descriptor_handle_tag (MXFMetadataBase * metadata,
    MXFPrimerPack * primer, guint16 tag, const guint8 * tag_data,
    guint tag_size)
{
  MXFMetadataMPEGVideoDescriptor *self =
      MXF_METADATA_MPEG_VIDEO_DESCRIPTOR (metadata);
  gboolean ret = TRUE;
  MXFUL *tag_ul = NULL;

  if (!(tag_ul =
          (MXFUL *) g_hash_table_lookup (primer->mappings,
              GUINT_TO_POINTER (((guint) tag)))))
    return FALSE;

  if (memcmp (tag_ul, &_single_sequence_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    self->single_sequence = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  single sequence = %s",
        (self->single_sequence) ? "yes" : "no");
  } else if (memcmp (tag_ul, &_constant_b_frames_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    self->const_b_frames = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  constant b frames = %s",
        (self->single_sequence) ? "yes" : "no");
  } else if (memcmp (tag_ul, &_coded_content_type_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    self->coded_content_type = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  coded content type = %u", self->coded_content_type);
  } else if (memcmp (tag_ul, &_low_delay_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    self->low_delay = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  low delay = %s", (self->low_delay) ? "yes" : "no");
  } else if (memcmp (tag_ul, &_closed_gop_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    self->closed_gop = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  closed gop = %s", (self->closed_gop) ? "yes" : "no");
  } else if (memcmp (tag_ul, &_identical_gop_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    self->identical_gop = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  identical gop = %s", (self->identical_gop) ? "yes" : "no");
  } else if (memcmp (tag_ul, &_max_gop_ul, 16) == 0) {
    if (tag_size != 2)
      goto error;
    self->max_gop = GST_READ_UINT16_BE (tag_data);
    GST_DEBUG ("  max gop = %u", self->max_gop);
  } else if (memcmp (tag_ul, &_b_picture_count_ul, 16) == 0) {
    if (tag_size != 2)
      goto error;
    self->b_picture_count = GST_READ_UINT16_BE (tag_data);
    GST_DEBUG ("  b picture count = %u", self->b_picture_count);
  } else if (memcmp (tag_ul, &_bitrate_ul, 16) == 0) {
    if (tag_size != 4)
      goto error;
    self->bitrate = GST_READ_UINT32_BE (tag_data);
    GST_DEBUG ("  bitrate = %u", self->bitrate);
  } else if (memcmp (tag_ul, &_profile_and_level_ul, 16) == 0) {
    if (tag_size != 1)
      goto error;
    self->profile_and_level = GST_READ_UINT8 (tag_data);
    GST_DEBUG ("  profile & level = %u", self->profile_and_level);
  } else {
    ret =
        MXF_METADATA_BASE_CLASS
        (mxf_metadata_mpeg_video_descriptor_parent_class)->handle_tag (metadata,
        primer, tag, tag_data, tag_size);
  }

  return ret;

error:

  GST_ERROR ("Invalid MPEG video descriptor local tag 0x%04x of size %u", tag,
      tag_size);

  return FALSE;
}

static void
mxf_metadata_mpeg_video_descriptor_init (MXFMetadataMPEGVideoDescriptor * self)
{

}

static void
    mxf_metadata_mpeg_video_descriptor_class_init
    (MXFMetadataMPEGVideoDescriptorClass * klass)
{
  MXFMetadataBaseClass *metadata_base_class = (MXFMetadataBaseClass *) klass;

  metadata_base_class->handle_tag =
      mxf_metadata_mpeg_video_descriptor_handle_tag;
}

typedef enum
{
  MXF_MPEG_ESSENCE_TYPE_OTHER = 0,
  MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG2,
  MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG4,
  MXF_MPEG_ESSENCE_TYPE_VIDEO_AVC
} MXFMPEGEssenceType;

static gboolean
mxf_is_mpeg_essence_track (const MXFMetadataTimelineTrack * track)
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
    /* SMPTE 381M 7 */
    if (mxf_is_generic_container_essence_container_label (key) &&
        key->u[12] == 0x02 &&
        (key->u[13] == 0x04 ||
            key->u[13] == 0x07 || key->u[13] == 0x08 || key->u[13] == 0x09))
      return TRUE;
  }

  return FALSE;
}

/* See ISO/IEC 13818-2 for MPEG ES format */
gboolean
mxf_mpeg_is_mpeg2_keyframe (GstBuffer * buffer)
{
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);
  guint32 tmp;

  while (gst_byte_reader_get_remaining (&reader) > 3) {
    if (gst_byte_reader_peek_uint24_be (&reader, &tmp) && tmp == 0x000001) {
      guint8 type;

      /* Found sync code */
      gst_byte_reader_skip (&reader, 3);

      if (!gst_byte_reader_get_uint8 (&reader, &type))
        break;

      /* GOP packets are meant as random access markers */
      if (type == 0xb8) {
        return TRUE;
      } else if (type == 0x00) {
        guint8 pic_type;

        if (!gst_byte_reader_skip (&reader, 5))
          break;

        if (!gst_byte_reader_get_uint8 (&reader, &pic_type))
          break;

        pic_type = (pic_type >> 3) & 0x07;
        if (pic_type == 0x01) {
          return TRUE;
        } else {
          return FALSE;
        }
      }
    } else {
      gst_byte_reader_skip (&reader, 1);
    }
  }

  return FALSE;
}

static gboolean
mxf_mpeg_is_mpeg4_keyframe (GstBuffer * buffer)
{
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buffer);
  guint32 tmp;

  while (gst_byte_reader_get_remaining (&reader) > 3) {
    if (gst_byte_reader_peek_uint24_be (&reader, &tmp) && tmp == 0x000001) {
      guint8 type;

      /* Found sync code */
      gst_byte_reader_skip (&reader, 3);

      if (!gst_byte_reader_get_uint8 (&reader, &type))
        break;

      if (type == 0xb6) {
        guint8 pic_type;

        if (!gst_byte_reader_get_uint8 (&reader, &pic_type))
          break;

        pic_type = (pic_type >> 6) & 0x03;
        if (pic_type == 0) {
          return TRUE;
        } else {
          return FALSE;
        }
      }
    } else {
      gst_byte_reader_skip (&reader, 1);
    }
  }

  g_assert_not_reached ();

  return FALSE;
}

static GstFlowReturn
mxf_mpeg_video_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataTimelineTrack * track,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  MXFMPEGEssenceType type = *((MXFMPEGEssenceType *) mapping_data);

  *outbuf = buffer;

  /* SMPTE 381M 6.1 */
  if (key->u[12] != 0x15 || (key->u[14] != 0x05 && key->u[14] != 0x06
          && key->u[14] != 0x07)) {
    GST_ERROR ("Invalid MPEG video essence element");
    return GST_FLOW_ERROR;
  }

  switch (type) {
    case MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG2:
      if (mxf_mpeg_is_mpeg2_keyframe (buffer))
        GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
      else
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
      break;
    case MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG4:
      if (mxf_mpeg_is_mpeg4_keyframe (buffer))
        GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
      else
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
      break;

    default:
      break;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
mxf_mpeg_audio_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps, MXFMetadataTimelineTrack * track,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  *outbuf = buffer;

  /* SMPTE 381M 6.2 */
  if (key->u[12] != 0x16 || (key->u[14] != 0x05 && key->u[14] != 0x06
          && key->u[14] != 0x07)) {
    GST_ERROR ("Invalid MPEG audio essence element");
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

/* RP224 */

static const MXFUL sound_essence_compression_ac3 = { {
        0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x01, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x02, 0x01, 0x00}
};

static const MXFUL sound_essence_compression_mpeg1_layer1 = { {
        0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x01, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x02, 0x04, 0x00}
};

static const MXFUL sound_essence_compression_mpeg1_layer12 = { {
        0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x01, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x02, 0x05, 0x00}
};

static const MXFUL sound_essence_compression_mpeg1_layer2 = { {
        0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x08, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x02, 0x05, 0x01}
};

static const MXFUL sound_essence_compression_mpeg2_layer1 = { {
        0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x01, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x02, 0x06, 0x00}
};

static const MXFUL sound_essence_compression_dts = { {
        0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x01, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x02, 0x1c, 0x00}
};

static const MXFUL sound_essence_compression_aac = { {
        0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x03, 0x04, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x01, 0x00}
};

static GstCaps *
mxf_mpeg_es_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data,
    MXFMetadataGenericPictureEssenceDescriptor * p,
    MXFMetadataGenericSoundEssenceDescriptor * s)
{
  GstCaps *caps = NULL;
  const gchar *codec_name = NULL;
  MXFMPEGEssenceType t, *mdata;

  *mapping_data = g_malloc (sizeof (MXFMPEGEssenceType));
  mdata = (MXFMPEGEssenceType *) * mapping_data;

  /* SMPTE RP224 */
  if (p) {
    if (mxf_ul_is_zero (&p->picture_essence_coding)) {
      GST_WARNING ("No picture essence coding defined, assuming MPEG2");
      caps =
          gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 2,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      codec_name = "MPEG-2 Video";
      t = MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG2;
      memcpy (mdata, &t, sizeof (MXFMPEGEssenceType));
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
      codec_name = "MPEG-2 Video";
      t = MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG2;
      memcpy (mdata, &t, sizeof (MXFMPEGEssenceType));
    } else if (p->picture_essence_coding.u[13] == 0x10) {
      caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 1,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      codec_name = "MPEG-1 Video";
      t = MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG2;
      memcpy (mdata, &t, sizeof (MXFMPEGEssenceType));
    } else if (p->picture_essence_coding.u[13] == 0x20) {
      MXFLocalTag *local_tag =
          (((MXFMetadataBase *) p)->other_tags) ?
          g_hash_table_lookup (((MXFMetadataBase *)
              p)->other_tags, &sony_mpeg4_extradata) : NULL;

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
      codec_name = "MPEG-4 Video";
      t = MXF_MPEG_ESSENCE_TYPE_VIDEO_MPEG4;
      memcpy (mdata, &t, sizeof (MXFMPEGEssenceType));
    } else if ((p->picture_essence_coding.u[13] >> 4) == 0x03) {
      /* RP 2008 */

      /* TODO: What about codec_data for AVC1 streams? */
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      codec_name = "h.264 Video";
      t = MXF_MPEG_ESSENCE_TYPE_VIDEO_AVC;
      memcpy (mdata, &t, sizeof (MXFMPEGEssenceType));
    } else {
      GST_ERROR ("Unsupported MPEG picture essence coding 0x%02x",
          p->picture_essence_coding.u[13]);
      caps = NULL;
    }
    if (caps)
      *handler = mxf_mpeg_video_handle_essence_element;
  } else if (s) {
    if (mxf_ul_is_zero (&s->sound_essence_compression)) {
      GST_WARNING ("Zero sound essence compression, assuming MPEG1 audio");
      caps =
          gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
          NULL);
      codec_name = "MPEG-1 Audio";
    } else if (mxf_ul_is_equal (&s->sound_essence_compression,
            &sound_essence_compression_ac3)) {
      caps = gst_caps_new_simple ("audio/ac3", NULL);
      codec_name = "AC3 Audio";
    } else if (mxf_ul_is_equal (&s->sound_essence_compression,
            &sound_essence_compression_mpeg1_layer1)) {
      caps =
          gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
          "layer", G_TYPE_INT, 1, NULL);
      codec_name = "MPEG-1 Layer 1 Audio";
    } else if (mxf_ul_is_equal (&s->sound_essence_compression,
            &sound_essence_compression_mpeg1_layer12)) {
      caps =
          gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
          NULL);
      codec_name = "MPEG-1 Audio";
    } else if (mxf_ul_is_equal (&s->sound_essence_compression,
            &sound_essence_compression_mpeg1_layer2)) {
      caps =
          gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
          "layer", G_TYPE_INT, 2, NULL);
      codec_name = "MPEG-1 Layer 2 Audio";
    } else if (mxf_ul_is_equal (&s->sound_essence_compression,
            &sound_essence_compression_mpeg2_layer1)) {
      caps =
          gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
          "layer", G_TYPE_INT, 1, "mpegaudioversion", G_TYPE_INT, 2, NULL);
      codec_name = "MPEG-2 Layer 1 Audio";
    } else if (mxf_ul_is_equal (&s->sound_essence_compression,
            &sound_essence_compression_dts)) {
      caps = gst_caps_new_simple ("audio/x-dts", NULL);
      codec_name = "Dolby DTS Audio";
    } else if (mxf_ul_is_equal (&s->sound_essence_compression,
            &sound_essence_compression_aac)) {
      caps = gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT,
          2, NULL);
      codec_name = "MPEG-2 AAC Audio";
    }

    if (caps) {
      mxf_metadata_generic_sound_essence_descriptor_set_caps (s, caps);
      *handler = mxf_mpeg_audio_handle_essence_element;
    }
  }

  if (caps) {
    if (!*tags)
      *tags = gst_tag_list_new ();
    if (codec_name)
      gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
          codec_name, NULL);
  }

  return caps;
}

static GstCaps *
mxf_mpeg_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    MXFEssenceElementHandleFunc * handler, gpointer * mapping_data)
{
  MXFMetadataFileDescriptor *f = NULL;
  MXFMetadataGenericPictureEssenceDescriptor *p = NULL;
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

    if (MXF_IS_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR (track->parent.
            descriptor[i])) {
      f = track->parent.descriptor[i];
      p = (MXFMetadataGenericPictureEssenceDescriptor *) track->
          parent.descriptor[i];
      break;
    } else if (MXF_IS_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR (track->parent.
            descriptor[i])) {
      f = track->parent.descriptor[i];
      s = (MXFMetadataGenericSoundEssenceDescriptor *) track->
          parent.descriptor[i];
      break;
    }
  }

  if (!f) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  /* SMPTE 381M 7 */
  if (f->essence_container.u[13] == 0x04) {
    GST_DEBUG ("Found MPEG ES stream");

    caps = mxf_mpeg_es_create_caps (track, tags, handler, mapping_data, p, s);
  } else if (f->essence_container.u[13] == 0x07) {
    GST_ERROR ("MPEG PES streams not supported yet");
    return NULL;
  } else if (f->essence_container.u[13] == 0x08) {
    /* FIXME: get mpeg version somehow */
    GST_DEBUG ("Found MPEG PS stream");
    caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 1,
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);

    if (!*tags)
      *tags = gst_tag_list_new ();
    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
        "MPEG PS", NULL);
  } else if (f->essence_container.u[13] == 0x09) {
    GST_DEBUG ("Found MPEG TS stream");
    caps = gst_caps_new_simple ("video/mpegts", NULL);

    if (!*tags)
      *tags = gst_tag_list_new ();
    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
        "MPEG TS", NULL);
  } else if (f->essence_container.u[13] == 0x0f) {
    GST_DEBUG ("Found h264 NAL unit stream");
    /* RP 2008 */
    /* TODO: What about codec_data? */
    caps = gst_caps_new_simple ("video/x-h264", NULL);

    if (!*tags)
      *tags = gst_tag_list_new ();
    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
        "h.264 Video", NULL);
  } else if (f->essence_container.u[13] == 0x10) {
    GST_DEBUG ("Found h264 byte stream stream");
    /* RP 2008 */
    caps = gst_caps_new_simple ("video/x-h264", NULL);

    if (!*tags)
      *tags = gst_tag_list_new ();
    gst_tag_list_add (*tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
        "h.264 Video", NULL);
  }

  if (p && caps)
    mxf_metadata_generic_picture_essence_descriptor_set_caps (p, caps);

  return caps;
}

static const MXFEssenceElementHandler mxf_mpeg_essence_element_handler = {
  mxf_is_mpeg_essence_track,
  mxf_mpeg_create_caps
};

void
mxf_mpeg_init (void)
{
  mxf_metadata_register (0x0151, MXF_TYPE_METADATA_MPEG_VIDEO_DESCRIPTOR);
  mxf_essence_element_handler_register (&mxf_mpeg_essence_element_handler);
}
