/* GStreamer
 * Copyright (C) 2020 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Implementation of SMPTE 436M - MXF Mappings for VBI Lines
 * and Ancillary Data Packets
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/base/gstbitreader.h>
#include <gst/base/gstbitwriter.h>
#include <gst/video/video.h>
#include <string.h>

#include "mxfvanc.h"
#include "mxfessence.h"

GST_DEBUG_CATEGORY_EXTERN (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

/* SMPTE S436M 7 */
#define MXF_TYPE_METADATA_VANC_DESCRIPTOR \
  (mxf_metadata_vanc_descriptor_get_type())
#define MXF_METADATA_VANC_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),MXF_TYPE_METADATA_VANC_DESCRIPTOR, MXFMetadataVANCDescriptor))
#define MXF_IS_METADATA_VANC_DESCRIPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MXF_TYPE_METADATA_VANC_DESCRIPTOR))
typedef struct _MXFMetadataVANCDescriptor MXFMetadataVANCDescriptor;
typedef MXFMetadataClass MXFMetadataVANCDescriptorClass;
GType mxf_metadata_vanc_descriptor_get_type (void);

struct _MXFMetadataVANCDescriptor
{
  MXFMetadataGenericDataEssenceDescriptor parent;
};

G_DEFINE_TYPE (MXFMetadataVANCDescriptor,
    mxf_metadata_vanc_descriptor,
    MXF_TYPE_METADATA_GENERIC_DATA_ESSENCE_DESCRIPTOR);

static gboolean HANDLE_AS_ST2038 = TRUE;

static void
mxf_metadata_vanc_descriptor_init (MXFMetadataVANCDescriptor * self)
{
}

static void
    mxf_metadata_vanc_descriptor_class_init
    (MXFMetadataVANCDescriptorClass * klass)
{
  MXFMetadataClass *metadata_class = (MXFMetadataClass *) klass;

  metadata_class->type = 0x015c;
}

static gboolean
mxf_is_vanc_essence_track (const MXFMetadataFileDescriptor * d)
{
  const MXFUL *key = &d->essence_container;

  /* SMPTE 436M 4.3 */
  return (mxf_is_generic_container_essence_container_label (key) &&
      key->u[12] == 0x02 && key->u[13] == 0x0e &&
      key->u[14] == 0x00 && key->u[15] == 0x00);
}

static guint16
with_parity (const guint8 word)
{
  guint8 bit8, parity;

  parity = word ^ (word >> 4);
  parity ^= (parity >> 2);
  parity ^= (parity >> 1);
  bit8 = parity & 1;

  return (word | (bit8 << 8) | ((!bit8) << 9));
}

static gboolean
get_c_not_y_channel_flag (const guint8 payload_sample_coding)
{
  /*
   * 5  - 8-bit color difference samples
   * 8  - 10-bit color difference samples
   * 11 - 8-bit color difference samples with parity
   */
  return (payload_sample_coding == 5) ||
      (payload_sample_coding == 8) || (payload_sample_coding == 11);
}

static gboolean
is_payload_10bit (const guint8 payload_sample_coding)
{
  /*
   * 7 - 10-bit luma samples
   * 8 - 10-bit color difference samples
   * 9 - 10-bit luma and color difference samples
   */
  if (payload_sample_coding == 7 || payload_sample_coding == 8
      || payload_sample_coding == 9) {
    return TRUE;
  }

  return FALSE;
}

static void
write_st2038_header (GstBitWriter * writer, guint8 c_not_y_channel_flag,
    guint16 line_number, guint16 did, guint sdid, guint16 data_count)
{
  gst_bit_writer_put_bits_uint8 (writer, 0, 6); /* 6 zero bits */
  gst_bit_writer_put_bits_uint8 (writer, c_not_y_channel_flag, 1);
  gst_bit_writer_put_bits_uint16 (writer, line_number, 11);     /* line number */
  gst_bit_writer_put_bits_uint16 (writer, 0xFFF /* Unknown/unspecified */ , 12);        /* horizontal offset */

  gst_bit_writer_put_bits_uint16 (writer, did, 10);
  gst_bit_writer_put_bits_uint16 (writer, sdid, 10);
  gst_bit_writer_put_bits_uint16 (writer, data_count, 10);
}

static GstBuffer *
mxf_vanc_to_st2038 (const guint8 * vanc_data, gsize vanc_data_size,
    guint16 line_number, guint16 payload_sample_count,
    guint8 payload_sample_coding, guint32 array_count, guint32 array_item_size)
{
  GstBitWriter writer;
  GstBitReader bit_reader;
  GstByteReader byte_reader;
  guint16 checksum, did, sdid, data_count;
  guint8 c_not_y_channel_flag, *data;
  gboolean payload_10bit;
  gsize size;
  guint i;

  c_not_y_channel_flag = get_c_not_y_channel_flag (payload_sample_coding);
  payload_10bit = is_payload_10bit (payload_sample_coding);

  if (payload_10bit) {
    gst_bit_reader_init (&bit_reader, vanc_data, vanc_data_size);

    /* Check if we can read DID, SDID and Data Count */
    if (gst_bit_reader_get_remaining (&bit_reader) < 32) {
      GST_WARNING ("Insufficient VANC data");
      return NULL;
    }

    /* See section 5.4.4 of ST-436 on 10-bit sample coding */
    did = gst_bit_reader_get_bits_uint16_unchecked (&bit_reader, 10);
    sdid = gst_bit_reader_get_bits_uint16_unchecked (&bit_reader, 10);
    data_count = gst_bit_reader_get_bits_uint16_unchecked (&bit_reader, 10);

    /* Skip 2-bit padding */
    gst_bit_reader_skip (&bit_reader, 2);

    if (payload_sample_count - 3 < data_count) {
      GST_WARNING ("Insufficient user data words");
      return NULL;
    }

    gst_bit_writer_init_with_size (&writer, 64 + data_count * 2, FALSE);
    write_st2038_header (&writer, c_not_y_channel_flag, line_number, did, sdid,
        data_count);

    /*
     * See Section 6.7 of ST-291 on Checksum Word.
     * Write data words and checksum.
     *
     * In 10-bit applications, the checksum value shall be equal to
     * the nine least significant bits of the sum of the nine least
     * significant bits of the DID, SDID, DC and UDW.
     */
    checksum = (did & 0x1FF) + (sdid & 0x1FF) + (data_count & 0x1FF);
    for (i = 0; i < data_count; i++) {
      /*
       * For a 10-bit coding, 4 bytes representing 3 source samples
       * are coded using the high-order 30-bits (bits 2 to 31) of a
       * 32-bit (4 byte) Payload Array data word. The 2 low-order
       * bits of the payload data 32-bit word (bits 0 and 1) are set
       * to zero.
       */
      guint16 udw = gst_bit_reader_get_bits_uint16_unchecked (&bit_reader, 10);
      checksum += (udw & 0x1FF);
      gst_bit_writer_put_bits_uint16 (&writer, udw, 10);

      if (i % 3 == 2) {
        gst_bit_reader_skip (&bit_reader, 2);
      }
    }

    gst_bit_writer_put_bits_uint16 (&writer, checksum & 0x1FF, 10);
  } else {
    gst_byte_reader_init (&byte_reader, vanc_data, vanc_data_size);

    /* Check if we can read DID, SDID and Data Count */
    if (gst_byte_reader_get_remaining (&byte_reader) < 3) {
      GST_WARNING ("Insufficient VANC data");
      return NULL;
    }

    did = gst_byte_reader_get_uint8_unchecked (&byte_reader);
    sdid = gst_byte_reader_get_uint8_unchecked (&byte_reader);
    data_count = gst_byte_reader_get_uint8_unchecked (&byte_reader);

    if (payload_sample_count - 3 < data_count) {
      GST_WARNING ("Insufficient user data words");
      return NULL;
    }

    gst_bit_writer_init_with_size (&writer, 64 + data_count * 2, FALSE);
    write_st2038_header (&writer, c_not_y_channel_flag, line_number,
        with_parity (did), with_parity (sdid), with_parity (data_count));

    /*
     * See Section 6.7 of ST-291 on Checksum Word.
     * Write data words and checksum.
     */
    checksum = did + sdid + data_count;
    for (i = 0; i < data_count; i++) {
      guint8 udw = gst_byte_reader_get_uint8_unchecked (&byte_reader);
      checksum += udw;
      gst_bit_writer_put_bits_uint16 (&writer, with_parity (udw), 10);
    }

    gst_bit_writer_put_bits_uint16 (&writer, with_parity (checksum & 0xFF), 10);
  }

  gst_bit_writer_align_bytes (&writer, 1);

  size = gst_bit_writer_get_size (&writer) / 8;
  data = gst_bit_writer_reset_and_get_data (&writer);

  return gst_buffer_new_wrapped (data, size);
}

static GstFlowReturn
mxf_vanc_handle_essence_element (const MXFUL * key, GstBuffer * buffer,
    GstCaps * caps,
    MXFMetadataTimelineTrack * track,
    gpointer mapping_data, GstBuffer ** outbuf)
{
  GstMapInfo map;
  GstByteReader reader;
  GstFlowReturn ret = GST_FLOW_ERROR;
  guint16 num_packets, i;

  /* SMPTE 436M 6.1 */
  if (key->u[12] != 0x17 || key->u[14] != 0x02) {
    GST_ERROR ("Invalid VANC essence element");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }

  /* Either there is no data or there is at least room for the 16bit length,
   * therefore the only invalid packet length is 1 */
  if (gst_buffer_get_size (buffer) == 1) {
    GST_ERROR ("Invalid VANC essence element size");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  gst_byte_reader_init (&reader, map.data, map.size);

  /* Some XDCAM recorders store empty vanc packets (without even the
   * length). Treat them as gaps */
  if (map.size == 0)
    goto no_data;

  num_packets = gst_byte_reader_get_uint16_be_unchecked (&reader);
  if (num_packets == 0) {
    /* SMPTE 436-1:2013 5.5 The Number of VI Lines or ANC Packets Property
     *
     * One of the properties in the VI Element is the “Number of Lines” which is
     * the number of the VI lines contained in this VI Element. This number can
     * be zero if the current VI Element does not have any VI lines in the
     * payload space. This capability can be used so every Content Package in a
     * file can have a VI Element even if the video stream does not have VI
     * lines with every frame (or field.)
     *
     * The same scheme can be used for ANC packets.
     */
    goto no_data;
  }

  for (i = 0; i < num_packets; i++) {
    G_GNUC_UNUSED guint16 line_num;
    G_GNUC_UNUSED guint8 wrapping_type;
    guint8 payload_sample_coding;
    guint16 payload_sample_count;
    guint32 array_count;
    guint32 array_item_size;
    guint8 did, sdid;
    guint8 cdp_size;

    if (gst_byte_reader_get_remaining (&reader) < 16)
      goto out;

    line_num = gst_byte_reader_get_uint16_be_unchecked (&reader);
    wrapping_type = gst_byte_reader_get_uint8_unchecked (&reader);
    payload_sample_coding = gst_byte_reader_get_uint8_unchecked (&reader);
    payload_sample_count = gst_byte_reader_get_uint16_be_unchecked (&reader);

    array_count = gst_byte_reader_get_uint32_be_unchecked (&reader);
    array_item_size = gst_byte_reader_get_uint32_be_unchecked (&reader);

    if (!HANDLE_AS_ST2038) {
      /* Skip over anything that is not 8 bit VANC */
      if (payload_sample_coding != 4 && payload_sample_coding != 5
          && payload_sample_coding != 6) {
        if (!gst_byte_reader_skip (&reader, array_count * array_item_size))
          goto out;
        continue;
      }
    }

    if (gst_byte_reader_get_remaining (&reader) < array_count * array_item_size)
      goto out;

    if (gst_byte_reader_get_remaining (&reader) < payload_sample_count)
      goto out;

    if (payload_sample_count < 2) {
      if (!gst_byte_reader_skip (&reader, array_count * array_item_size))
        goto out;
      continue;
    }

    if (!HANDLE_AS_ST2038) {
      /* Type-2 Ancillary Data Packet Format */
      did = gst_byte_reader_get_uint8_unchecked (&reader);
      sdid = gst_byte_reader_get_uint8_unchecked (&reader);

      /* Not S334 EIA-708 */
      if (did != 0x61 && sdid != 0x01) {
        GST_TRACE ("Skipping VANC data with DID/SDID 0x%02X/0x%02X", did, sdid);
        if (!gst_byte_reader_skip (&reader, array_count * array_item_size - 2))
          goto out;
        continue;
      }

      cdp_size = gst_byte_reader_get_uint8_unchecked (&reader);
      if (payload_sample_count - 3 < cdp_size) {
        if (!gst_byte_reader_skip (&reader, array_count * array_item_size - 3))
          goto out;
        continue;
      }

      gst_buffer_unmap (buffer, &map);
      *outbuf =
          gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL,
          gst_byte_reader_get_pos (&reader), cdp_size);
      gst_buffer_unref (buffer);
    } else {
      gsize byte_pos = gst_byte_reader_get_pos (&reader);
      gsize vanc_data_size = gst_byte_reader_get_remaining (&reader);

      /* Convert from ST-436M to ST-2038 */
      *outbuf = mxf_vanc_to_st2038 (&map.data[byte_pos], vanc_data_size,
          line_num, payload_sample_count, payload_sample_coding,
          array_count, array_item_size);
      if (!outbuf)
        goto no_data;

      gst_buffer_unmap (buffer, &map);
      gst_buffer_unref (buffer);
    }

    return GST_FLOW_OK;
  }

no_data:

  /* No packets or we skipped over all packets */
  *outbuf = gst_buffer_new ();
  GST_BUFFER_FLAG_SET (*outbuf, GST_BUFFER_FLAG_GAP);
  ret = GST_FLOW_OK;

out:
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  return ret;
}

static MXFEssenceWrapping
mxf_vanc_get_track_wrapping (const MXFMetadataTimelineTrack * track)
{
  g_return_val_if_fail (track != NULL, MXF_ESSENCE_WRAPPING_CUSTOM_WRAPPING);

  if (track->parent.descriptor == NULL) {
    GST_ERROR ("No descriptor found for this track");
    return MXF_ESSENCE_WRAPPING_CUSTOM_WRAPPING;
  }

  return MXF_ESSENCE_WRAPPING_FRAME_WRAPPING;
}

static GstCaps *
mxf_vanc_create_caps (MXFMetadataTimelineTrack * track, GstTagList ** tags,
    gboolean * intra_only, MXFEssenceElementHandleFunc * handler,
    gpointer * mapping_data)
{
  GstCaps *caps = NULL;
  guint i;
  MXFMetadataFileDescriptor *f = NULL;
  MXFMetadataVANCDescriptor *p = NULL;

  g_return_val_if_fail (track != NULL, NULL);

  if (track->parent.descriptor == NULL) {
    GST_ERROR ("No descriptor found for this track");
    return NULL;
  }

  for (i = 0; i < track->parent.n_descriptor; i++) {
    if (!track->parent.descriptor[i])
      continue;

    if (MXF_IS_METADATA_VANC_DESCRIPTOR (track->parent.descriptor[i])) {
      p = (MXFMetadataVANCDescriptor *) track->parent.descriptor[i];
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

  *handler = mxf_vanc_handle_essence_element;

  if (!HANDLE_AS_ST2038) {
    caps =
        gst_caps_new_simple ("closedcaption/x-cea-708", "format",
        G_TYPE_STRING, "cdp", NULL);
  } else {
    caps =
        gst_caps_new_simple ("meta/x-st-2038", "alignment",
        G_TYPE_STRING, "frame", NULL);
  }

  if (p && p->parent.parent.sample_rate.d != 0) {
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
        p->parent.parent.sample_rate.n, p->parent.parent.sample_rate.d, NULL);
  }

  *intra_only = TRUE;

  return caps;
}

static const MXFEssenceElementHandler mxf_vanc_essence_element_handler = {
  mxf_is_vanc_essence_track,
  mxf_vanc_get_track_wrapping,
  mxf_vanc_create_caps
};

static GstFlowReturn
mxf_vanc_write_func (GstBuffer * buffer,
    gpointer mapping_data, GstAdapter * adapter, GstBuffer ** outbuf,
    gboolean flush)
{
  GstMapInfo map;
  GstByteWriter writer;
  guint8 *data;
  guint size;
  guint i;
  guint8 checksum;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  size = GST_ROUND_UP_4 (map.size) + 20;

  gst_byte_writer_init_with_size (&writer, size, TRUE);

  gst_byte_writer_put_uint16_be_unchecked (&writer, 1);
  gst_byte_writer_put_uint16_be_unchecked (&writer, 9);
  gst_byte_writer_put_uint8_unchecked (&writer, 1);
  gst_byte_writer_put_uint8_unchecked (&writer, 4);
  gst_byte_writer_put_uint16_be_unchecked (&writer, map.size + 4);
  gst_byte_writer_put_uint32_be_unchecked (&writer,
      GST_ROUND_UP_4 (map.size + 4));
  gst_byte_writer_put_uint32_be_unchecked (&writer, 1);

  gst_byte_writer_put_uint16_be_unchecked (&writer, 0x6101);
  gst_byte_writer_put_uint8_unchecked (&writer, map.size);

  gst_byte_writer_put_data_unchecked (&writer, map.data, map.size);

  checksum = 0x61 + 0x01 + map.size;
  for (i = 0; i < map.size; i++)
    checksum += map.data[i];
  gst_byte_writer_put_uint8_unchecked (&writer, checksum & 0xff);

  if (GST_ROUND_UP_4 (map.size) != map.size) {
    gst_byte_writer_fill_unchecked (&writer, 0,
        GST_ROUND_UP_4 (map.size) - map.size);
  }

  data = gst_byte_writer_reset_and_get_data (&writer);

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  *outbuf = gst_buffer_new_wrapped (data, size);

  return GST_FLOW_OK;
}

/* Extract 10-bit user data words from ST 2038 packet */
static gboolean
extract_st2038_user_data (const guint8 * data, guint data_size,
    const St2038AncHeader * header, guint8 * user_data)
{
  GstBitReader reader;
  guint16 temp16;
  guint i;

  gst_bit_reader_init (&reader, data, data_size);

  /* Skip to user data: 6 + 1 + 11 + 12 + 10 + 10 + 10 = 60 bits */
  if (!gst_bit_reader_skip (&reader, 60))
    return FALSE;

  if (gst_bit_reader_get_remaining (&reader) < header->data_count * 10)
    return FALSE;

  /* Read each 10-bit user data word (take lower 8 bits) */
  for (i = 0; i < header->data_count; i++) {
    temp16 = gst_bit_reader_get_bits_uint16_unchecked (&reader, 10);
    user_data[i] = temp16 & 0xFF;
  }

  return TRUE;
}

static gboolean
parse_st2038_header (const guint8 * data, guint data_size,
    St2038AncHeader * header)
{
  GstBitReader reader;
  guint8 zeroes;
  guint16 temp16;
  guint bit_pos;

  if (data_size < 8)
    return FALSE;

  gst_bit_reader_init (&reader, data, data_size);

  /* Check if we have enough until Data Count */
  if (gst_bit_reader_get_remaining (&reader) < 50) {
    GST_WARNING ("Incomplete ST-2038 header");
    return FALSE;
  }

  /* Read 6 zero bits */
  zeroes = gst_bit_reader_get_bits_uint8_unchecked (&reader, 6);
  if (zeroes != 0) {
    GST_WARNING ("ST2038: Zero bits are not zero (got 0x%x)", zeroes);
    return FALSE;
  }

  header->c_not_y_channel_flag =
      gst_bit_reader_get_bits_uint8_unchecked (&reader, 1);
  header->line_number = gst_bit_reader_get_bits_uint16_unchecked (&reader, 11);
  header->horizontal_offset =
      gst_bit_reader_get_bits_uint16_unchecked (&reader, 12);

  temp16 = gst_bit_reader_get_bits_uint16_unchecked (&reader, 10);
  header->did = temp16 & 0xFF;

  temp16 = gst_bit_reader_get_bits_uint16_unchecked (&reader, 10);
  header->sdid = temp16 & 0xFF;

  if (!gst_bit_reader_get_bits_uint16 (&reader, &temp16, 10))
    return FALSE;
  header->data_count = temp16 & 0xFF;

  if (!gst_bit_reader_skip (&reader, header->data_count * 10))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint16 (&reader, &header->checksum, 10))
    return FALSE;

  /* Skip alignment bits (should be all 1's until byte aligned) */
  bit_pos = gst_bit_reader_get_pos (&reader);
  if (bit_pos % 8 != 0) {
    guint bits_to_skip = 8 - (bit_pos % 8);
    guint8 alignment_bits;

    if (gst_bit_reader_get_bits_uint8 (&reader, &alignment_bits, bits_to_skip)) {
      /* Verify alignment bits are all 1's */
      guint8 expected = (1 << bits_to_skip) - 1;
      if (alignment_bits != expected) {
        GST_WARNING
            ("ST2038: Alignment bits are not all 1's (got 0x%x, expected 0x%x)",
            alignment_bits, expected);
      }
    }
  }

  /* Calculate total length in bytes */
  header->len_bytes = gst_bit_reader_get_pos (&reader) / 8;

  return TRUE;
}

static GstFlowReturn
mxf_st2038_to_vanc_write_func (GstBuffer * buffer,
    gpointer mapping_data, GstAdapter * adapter, GstBuffer ** outbuf,
    gboolean flush)
{
  GstMapInfo map;
  GstByteWriter writer;
  guint8 *data;
  guint size;
  guint i, offset;
  guint total_anc_size = 0;
  guint num_anc_structures = 0;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  /* First pass: parse ST 2038 to determine total size needed */
  offset = 0;
  while (offset < map.size) {
    St2038AncHeader header;

    if (!parse_st2038_header (&map.data[offset], map.size - offset, &header))
      break;

    /*
     * Each ANC packet in ST 436M needs:
     * 2 bytes DID/SDID + 1 byte DC + data_count bytes + 1 checksum byte
     */
    guint packet_size = 4 + header.data_count;
    total_anc_size += packet_size;
    num_anc_structures++;

    offset += header.len_bytes;
  }

  if (num_anc_structures == 0) {
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
    *outbuf = NULL;
    return GST_FLOW_OK;
  }

  /*
   * Calculate total ST 436M wrapper size:
   * 16 bytes base header + 4 bytes array count + total ANC data
   */
  size = 20 + total_anc_size;

  gst_byte_writer_init_with_size (&writer, size, TRUE);

  /* See ST-436M Section 7 */
  gst_byte_writer_put_uint16_be_unchecked (&writer, num_anc_structures);

  /* Second pass: convert each ST 2038 packet to ST 436M ANC payload */
  offset = 0;
  while (offset < map.size) {
    St2038AncHeader header;
    guint8 user_data[256];
    guint8 checksum;
    guint16 did_sdid;
    guint packet_data_size;

    if (!parse_st2038_header (&map.data[offset], map.size - offset, &header))
      break;

    if (!extract_st2038_user_data (&map.data[offset], map.size - offset,
            &header, user_data))
      break;

    gst_byte_writer_put_uint16_be_unchecked (&writer, header.line_number);
    gst_byte_writer_put_uint8_unchecked (&writer, 1);   /* Wrapping type */

    /*
     * ST2038 is 10 bits and we strip off the two parity bits, so
     * use a value of 4 here which indicate 8-bit luma samples or
     * 8-bit colour difference samples.
     */
    if (header.c_not_y_channel_flag) {
      gst_byte_writer_put_uint8_unchecked (&writer, 5); /* Payload Sample Coding */
    } else {
      gst_byte_writer_put_uint8_unchecked (&writer, 4); /* Payload Sample Coding */
    }

    gst_byte_writer_put_uint16_be_unchecked (&writer, total_anc_size);  /* Payload Sample Count */

    /*
     * See Section 4.3 of ST-377 on Compound Data Types.
     * First 4 bytes define the number of elements in the array.
     * Last 4 bytes define the length of each element.
     */
    gst_byte_writer_put_uint32_be_unchecked (&writer, total_anc_size);
    gst_byte_writer_put_uint32_be_unchecked (&writer, 1);

    did_sdid = (header.did << 8) | header.sdid;
    gst_byte_writer_put_uint16_be_unchecked (&writer, did_sdid);
    gst_byte_writer_put_uint8_unchecked (&writer, header.data_count);
    gst_byte_writer_put_data_unchecked (&writer, user_data, header.data_count);

    /* Calculate checksum (8-bit sum of DID + SDID + DC + all user data) */
    checksum = header.did + header.sdid + header.data_count;
    for (i = 0; i < header.data_count; i++)
      checksum += user_data[i];
    gst_byte_writer_put_uint8_unchecked (&writer, checksum & 0xff);

    /* Pad to 4-byte boundary */
    packet_data_size = 4 + header.data_count;
    if (GST_ROUND_UP_4 (packet_data_size) != packet_data_size) {
      gst_byte_writer_fill_unchecked (&writer, 0,
          GST_ROUND_UP_4 (packet_data_size) - packet_data_size);
    }

    offset += header.len_bytes;
  }

  data = gst_byte_writer_reset_and_get_data (&writer);

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  *outbuf = gst_buffer_new_wrapped (data, size);

  return GST_FLOW_OK;
}

static const guint8 vanc_essence_container_ul[] = {
  0x06, 0x0e, 0x2b, 0x34, 0x04, 0x01, 0x01, 0x09,
  0x0d, 0x01, 0x03, 0x01, 0x02, 0x0e, 0x00, 0x00
};

static MXFMetadataFileDescriptor *
mxf_vanc_get_descriptor (GstPadTemplate * tmpl, GstCaps * caps,
    MXFEssenceElementWriteFunc * handler, gpointer * mapping_data)
{
  MXFMetadataVANCDescriptor *ret;
  GstStructure *s;
  const gchar *format;
  gint fps_n, fps_d;

  s = gst_caps_get_structure (caps, 0);
  if (!HANDLE_AS_ST2038) {
    if (strcmp (gst_structure_get_name (s), "closedcaption/x-cea-708") != 0 ||
        !(format = gst_structure_get_string (s, "format")) ||
        strcmp (format, "cdp") != 0) {
      GST_ERROR ("Invalid caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }
  } else {
    if (strcmp (gst_structure_get_name (s), "meta/x-st-2038") != 0) {
      GST_ERROR ("Invalid caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }
  }

  if (!gst_structure_get_value (s, "framerate")) {
    GST_ERROR ("Missing framerate in caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);

  ret = (MXFMetadataVANCDescriptor *)
      g_object_new (MXF_TYPE_METADATA_VANC_DESCRIPTOR, NULL);

  memcpy (&ret->parent.parent.essence_container, &vanc_essence_container_ul,
      16);

  if (HANDLE_AS_ST2038) {
    *handler = mxf_st2038_to_vanc_write_func;
  } else {
    *handler = mxf_vanc_write_func;
  }

  return (MXFMetadataFileDescriptor *) ret;
}

static void
mxf_vanc_update_descriptor (MXFMetadataFileDescriptor * d, GstCaps * caps,
    gpointer mapping_data, GstBuffer * buf)
{
  return;
}

static void
mxf_vanc_get_edit_rate (MXFMetadataFileDescriptor * a, GstCaps * caps,
    gpointer mapping_data, GstBuffer * buf, MXFMetadataSourcePackage * package,
    MXFMetadataTimelineTrack * track, MXFFraction * edit_rate)
{
  const GstStructure *s;
  gint fps_n, fps_d;

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d))
    return;

  edit_rate->n = fps_n;
  edit_rate->d = fps_d;
}

static guint32
mxf_vanc_get_track_number_template (MXFMetadataFileDescriptor * a,
    GstCaps * caps, gpointer mapping_data)
{
  return (0x17 << 24) | (0x02 << 8);
}

static MXFEssenceElementWriter mxf_vanc_essence_element_writer = {
  mxf_vanc_get_descriptor,
  mxf_vanc_update_descriptor,
  mxf_vanc_get_edit_rate,
  mxf_vanc_get_track_number_template,
  NULL,
  {{0,}}
};

/**
 * GstMXFMux!vanc_sink_%u:
 *
 * Since: 1.18
 */

void
mxf_vanc_init (void)
{
  mxf_metadata_register (MXF_TYPE_METADATA_VANC_DESCRIPTOR);
  mxf_essence_element_handler_register (&mxf_vanc_essence_element_handler);
  const char *vanc_caps;

  if (g_getenv ("GST_VANC_AS_CEA708") != NULL) {
    vanc_caps = "closedcaption/x-cea-708, format = (string) cdp, "
        "framerate = " GST_VIDEO_FPS_RANGE;
    HANDLE_AS_ST2038 = FALSE;
  } else {
    vanc_caps = "meta/x-st-2038,alignment=frame";
  }

  mxf_vanc_essence_element_writer.pad_template =
      gst_pad_template_new ("vanc_sink_%u", GST_PAD_SINK,
      GST_PAD_REQUEST, gst_caps_from_string (vanc_caps));
  memcpy (&mxf_vanc_essence_element_writer.data_definition,
      mxf_metadata_track_identifier_get (MXF_METADATA_TRACK_DATA_ESSENCE), 16);
  mxf_essence_element_writer_register (&mxf_vanc_essence_element_writer);
}
