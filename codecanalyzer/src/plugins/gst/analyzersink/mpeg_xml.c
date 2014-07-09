/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "mpeg_xml.h"
#include "xml_utils.h"


static gboolean
create_seq_hdr_xml (xmlTextWriterPtr writer, GstMpegVideoSequenceHdr * seq_hdr)
{
  ANALYZER_XML_ELEMENT_START (writer, "SequenceHdr");

  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "sequence_hdr_id", "0xb3",
      "nbits", 8);

  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "horizontal_size_value",
      seq_hdr->width, "nbits", 12);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "vertical_size_value",
      seq_hdr->height, "nbits", 12);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "aspect_ratio_information",
      seq_hdr->aspect_ratio_info, "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "frame_rate_code",
      seq_hdr->frame_rate_code, "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "bit_rate_value",
      seq_hdr->bitrate_value, "nbits", 18);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "vbv_buffer_size_value",
      seq_hdr->vbv_buffer_size_value, "nbits", 10);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "constrained_parameters_flag",
      seq_hdr->constrained_parameters_flag, "nbits", 1);
#if 0
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "load_intra_quantiser_matrix",
      seq_hdr->load_intra_quantiser_matrix, "nbits", 1);
#endif
  ANALYZER_XML_ELEMENT_CREATE_MATRIX (writer, "intra_quantiser_matrix",
      seq_hdr->intra_quantizer_matrix, 8, 8);
#if 0
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "load_non_intra_quantiser_matrix",
      seq_hdr->load_non_intra_quantiser_matrix, "nbits", 1);
#endif
  ANALYZER_XML_ELEMENT_CREATE_MATRIX (writer, "non_intra_quantizer_matrix",
      seq_hdr->non_intra_quantizer_matrix, 8, 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "bit_rate_calculated",
      seq_hdr->bitrate, "nbits", 0);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "par_w_calculated",
      seq_hdr->par_w, "nbits", 0);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "par_h_calculated",
      seq_hdr->par_h, "nbits", 0);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "fps_n_calculated",
      seq_hdr->fps_n, "nbits", 0);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "fps_d_calculated",
      seq_hdr->fps_d, "nbits", 0);


  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;

error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video SequenceHeader \n");
    return FALSE;
  }
}

static gboolean
create_seq_ext_xml (xmlTextWriterPtr writer, GstMpegVideoSequenceExt * seq_ext)
{
  ANALYZER_XML_ELEMENT_START (writer, "SequenceExt");

  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "extension_identifier", "0xb5",
      "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "sequence_extension_id", "0x01",
      "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "profile", seq_ext->profile, "nbits",
      3);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "level", seq_ext->level, "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "progressive_sequence",
      seq_ext->progressive, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "chroma_fromat",
      seq_ext->chroma_format, "nbits", 2);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "horizontal_size_ext",
      seq_ext->horiz_size_ext, "nbits", 2);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "vertical_size_ext",
      seq_ext->vert_size_ext, "nbits", 2);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "bit_rate_ext", seq_ext->bitrate_ext,
      "nbits", 12);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "vbv_buffer_size_ex",
      seq_ext->vbv_buffer_size_extension, "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "low_delay", seq_ext->low_delay,
      "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "fps_ext_n", seq_ext->fps_n_ext,
      "nbits", 2);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "fps_ext_d", seq_ext->fps_d_ext,
      "nbits", 5);

  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;

error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video SequenceExt \n");
    return FALSE;
  }
}

static gboolean
create_seq_disp_ext_xml (xmlTextWriterPtr writer,
    GstMpegVideoSequenceDisplayExt * seq_disp_ext)
{
  ANALYZER_XML_ELEMENT_START (writer, "SequenceDispExt");

  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "extension_identifier", "0xb5",
      "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "sequence_display_extension_id",
      "0x02", "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "video_format",
      seq_disp_ext->video_format, "nbits", 3);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "colour_description_flag",
      seq_disp_ext->colour_description_flag, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "colour_primaries",
      seq_disp_ext->colour_primaries, "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "transfer_characteristics",
      seq_disp_ext->transfer_characteristics, "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "matrix_coefficients",
      seq_disp_ext->matrix_coefficients, "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "display_horizontal_size",
      seq_disp_ext->display_horizontal_size, "nbits", 14);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "display_vertical_size",
      seq_disp_ext->display_vertical_size, "nbits", 14);

  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;

error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video SequenceDisplayExt \n");
    return FALSE;
  }
}

static gboolean
create_gop_hdr_xml (xmlTextWriterPtr writer, GstMpegVideoGop * gop_hdr)
{
  ANALYZER_XML_ELEMENT_START (writer, "GopHdr");

  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "gop_hdr_id", "0xb8", "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "drop_frame_flag",
      gop_hdr->drop_frame_flag, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "time_code_hours", gop_hdr->hour,
      "nbits", 5);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "time_code_minutes", gop_hdr->minute,
      "nbits", 6);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "time_code_seconds", gop_hdr->second,
      "nbits", 6);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "time_code_pictures", gop_hdr->frame,
      "nbits", 6);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "closed_gop", gop_hdr->closed_gop,
      "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "broken_link", gop_hdr->broken_link,
      "nbits", 1);
  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;
error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video GopHdr \n");
    return FALSE;
  }
}

static gboolean
create_pic_hdr_xml (xmlTextWriterPtr writer, GstMpegVideoPictureHdr * pic_hdr)
{
  ANALYZER_XML_ELEMENT_START (writer, "PicHdr");

  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "picture_hdr_id", "0x00",
      "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "temporal_reference", pic_hdr->tsn,
      "nbits", 10);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "picture_coding_type",
      pic_hdr->pic_type, "nbits", 3);
#if 0
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "vbv_delay", pic_hdr->vbv_delay,
      "nbits", 16);
#endif
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "full_pel_forward_vector",
      pic_hdr->full_pel_forward_vector, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "forward_f_code",
      pic_hdr->f_code[0][0], "nbits", 3);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "full_pel_backword_vector",
      pic_hdr->full_pel_backward_vector, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "backword_f_code",
      pic_hdr->f_code[1][0], "nbits", 3);

  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;
error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video PicHdr \n");
    return FALSE;
  }
}

static gboolean
create_pic_ext_xml (xmlTextWriterPtr writer, GstMpegVideoPictureExt * pic_ext)
{
  ANALYZER_XML_ELEMENT_START (writer, "PicExt");

  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "extension_identifier", "0xb5",
      "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "picture_extension_id", "0x08",
      "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "f_code_forward_horizontal",
      pic_ext->f_code[0][0], "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "f_code_forward_vertical",
      pic_ext->f_code[0][1], "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "f_code_backward_horizontal",
      pic_ext->f_code[1][0], "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "f_cod_backward_vertical",
      pic_ext->f_code[1][1], "nbits", 4);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "intra_dc_precision",
      pic_ext->intra_dc_precision, "nbits", 2);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "picture_structure",
      pic_ext->picture_structure, "nbits", 2);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "top_field_first",
      pic_ext->top_field_first, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "frame_pred_frame_dct",
      pic_ext->frame_pred_frame_dct, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "concealment_motion_vectors",
      pic_ext->concealment_motion_vectors, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "q_scale_type",
      pic_ext->q_scale_type, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "intra_vlc_format",
      pic_ext->intra_vlc_format, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "alternate_scan",
      pic_ext->alternate_scan, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "repeat_first_field",
      pic_ext->repeat_first_field, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "chroma_420_type",
      pic_ext->chroma_420_type, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "progressive_frame",
      pic_ext->progressive_frame, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "composite_display_flag",
      pic_ext->composite_display, "nbits", 1);
  if (pic_ext->composite_display) {
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "v_axis", pic_ext->v_axis, "nbits",
        1);
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "field_sequence",
        pic_ext->field_sequence, "nbits", 3);
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "sub_carrier",
        pic_ext->sub_carrier, "nbits", 1);
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "burst_amplitude",
        pic_ext->burst_amplitude, "nbits", 7);
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "sub_carrier_phase",
        pic_ext->sub_carrier_phase, "nbits", 8);
  }
  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;
error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video PicHdr \n");
    return FALSE;
  }
}

static gboolean
create_quant_ext_xml (xmlTextWriterPtr writer,
    GstMpegVideoQuantMatrixExt * quant_ext)
{
  ANALYZER_XML_ELEMENT_START (writer, "QuantMatrixExt");

  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "extension_identifier", "0xb5",
      "nbits", 8);
  ANALYZER_XML_ELEMENT_CREATE_STRING (writer, "quant_matrix_extension_id",
      "0x03", "nbits", 4);

  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "load_intra_quantiser_matrix",
      quant_ext->load_intra_quantiser_matrix, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_MATRIX (writer, "intra_quantizer_matrix",
      quant_ext->intra_quantiser_matrix, 8, 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "load_non_intra_quantiser_matrix",
      quant_ext->load_non_intra_quantiser_matrix, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_MATRIX (writer, "non_intra_quantizer_matrix",
      quant_ext->non_intra_quantiser_matrix, 8, 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "load_chroma_intra_quantiser_matrix",
      quant_ext->load_chroma_intra_quantiser_matrix, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_MATRIX (writer, "chroma_intra_quantizer_matrix",
      quant_ext->chroma_intra_quantiser_matrix, 8, 8);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer,
      "load_chroma_non_intra_quantiser_matrix",
      quant_ext->load_chroma_non_intra_quantiser_matrix, "nbits", 1);
  ANALYZER_XML_ELEMENT_CREATE_MATRIX (writer,
      "chroma_non_intra_quantizer_matrix",
      quant_ext->chroma_non_intra_quantiser_matrix, 8, 8);

  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;
error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video QuantizationMatrices! ");
    return FALSE;
  }
  return TRUE;
}

#if 0
static gboolean
create_slice_hdr_xml (xmlTextWriterPtr writer,
    GstMpegVideoMetaSliceInfo * slice_info, gint slice_num)
{
  char header_name[256];

  sprintf (header_name, "slice_%d", slice_num);

  ANALYZER_XML_ELEMENT_START (writer, header_name);

  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "slice_hdr_identifier",
      slice_info->slice_hdr.slice_id, "nbits", 8);
  if (slice_info->slice_hdr.vertical_position_ext) {
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "vertical_position_ext",
        slice_info->slice_hdr.vertical_position_ext, "nbits", 3);
  }
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "priority_breakpoint",
      slice_info->slice_hdr.priority_breakpoint, "nbits", 7);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "quantiser_scale_code",
      slice_info->slice_hdr.quantiser_scale_code, "nbits", 5);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "slice_ext_flag",
      slice_info->slice_hdr.slice_ext_flag, "nbits", 1);

  if (!slice_info->slice_hdr.slice_ext_flag) {
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "intra_slice",
        slice_info->slice_hdr.intra_slice, "nbits", 1);
  } else {
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "intra_slice",
        slice_info->slice_hdr.intra_slice, "nbits", 1);
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "slice_picture_id_enable",
        slice_info->slice_hdr.slice_picture_id_enable, "nbits", 1);
    ANALYZER_XML_ELEMENT_CREATE_INT (writer, "slice_picture_id",
        slice_info->slice_hdr.slice_picture_id, "nbits", 6);
  }
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "header_size_calculated",
      slice_info->slice_hdr.header_size, "nbits", 0);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "mb_row_calculated",
      slice_info->slice_hdr.mb_row, "nbits", 0);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "mb_column_calculated",
      slice_info->slice_hdr.mb_column, "nbits", 0);

  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "slice_offset_calculated",
      slice_info->slice_offset, "nbits", 0);
  ANALYZER_XML_ELEMENT_CREATE_INT (writer, "slice_size_calculated",
      slice_info->slice_size, "nbits", 0);

  ANALYZER_XML_ELEMENT_END (writer);

  return TRUE;
error:
  {
    GST_ERROR ("Failed to write the xml for Mpeg2Video SliceHdr_%d \n",
        slice_num);
    return FALSE;
  }
  return TRUE;
}
#endif
gboolean
analyzer_create_mpeg2video_frame_xml (GstMpegVideoMeta * mpeg_meta,
    gchar * location, gint frame_num, Mpeg2Headers * mpeg2_hdrs)
{
  xmlTextWriterPtr writer;
  xmlDocPtr doc;
  xmlBufferPtr buf;
  xmlChar *tmp;
  gchar *file_name;
  gchar *name;
  int fd, i;
  GstMpegVideoSequenceHdr *sequencehdr = NULL;
  GstMpegVideoSequenceExt *sequenceext = NULL;
  GstMpegVideoSequenceDisplayExt *sequencedispext = NULL;
  GstMpegVideoQuantMatrixExt *quantext = NULL;

  if (!mpeg_meta)
    return FALSE;

  xmlKeepBlanksDefault (0);

  writer = xmlNewTextWriterDoc (&doc, 0);
  if (!writer) {
    GST_ERROR ("Error: creating xml text writer \n");
    return FALSE;
  }

  if (xmlTextWriterStartDocument (writer, NULL, "UTF-8", NULL) < 0) {
    GST_ERROR ("Error: xmlTextWriterStartDocument");
    return FALSE;
  }

  if (xmlTextWriterStartElement (writer, (xmlChar *) "mpeg2") < 0) {
    GST_ERROR ("Error: Failed to start the new element (root element) mpeg2");
    return FALSE;
  }

  if (xmlTextWriterWriteComment (writer,
          (xmlChar *) "Data parssed from the mpeg2 stream") < 0) {
    g_error ("Error: Failed to write the comment \n");
    return FALSE;
  }

  /* Each time we save the gerneral headers, which will get appended for
     each frame xml files */

  /* SequenceHdr */
  if (mpeg_meta->sequencehdr) {
    sequencehdr = mpeg_meta->sequencehdr;

    if (mpeg2_hdrs->sequencehdr)
      g_slice_free (GstMpegVideoSequenceHdr, mpeg2_hdrs->sequencehdr);
    mpeg2_hdrs->sequencehdr =
        g_slice_dup (GstMpegVideoSequenceHdr, mpeg_meta->sequencehdr);

  } else if (mpeg2_hdrs->sequencehdr)
    sequencehdr = mpeg2_hdrs->sequencehdr;

  /* SequenceExtHdr */
  if (mpeg_meta->sequenceext) {
    sequenceext = mpeg_meta->sequenceext;

    if (mpeg2_hdrs->sequenceext)
      g_slice_free (GstMpegVideoSequenceExt, mpeg2_hdrs->sequenceext);
    mpeg2_hdrs->sequenceext =
        g_slice_dup (GstMpegVideoSequenceExt, mpeg_meta->sequenceext);

  } else if (mpeg2_hdrs->sequenceext)
    sequenceext = mpeg2_hdrs->sequenceext;

  /* SequenceDisplayExt */
  if (mpeg_meta->sequencedispext) {
    sequencedispext = mpeg_meta->sequencedispext;

    if (mpeg2_hdrs->sequencedispext)
      g_slice_free (GstMpegVideoSequenceDisplayExt,
          mpeg2_hdrs->sequencedispext);
    mpeg2_hdrs->sequencedispext =
        g_slice_dup (GstMpegVideoSequenceDisplayExt,
        mpeg_meta->sequencedispext);

  } else if (mpeg2_hdrs->sequencedispext)
    sequencedispext = mpeg2_hdrs->sequencedispext;

  /* QuantMatrixExt */
  if (mpeg_meta->quantext) {
    quantext = mpeg_meta->quantext;

    if (mpeg2_hdrs->quantext)
      g_slice_free (GstMpegVideoQuantMatrixExt, mpeg2_hdrs->quantext);
    mpeg2_hdrs->quantext =
        g_slice_dup (GstMpegVideoQuantMatrixExt, mpeg_meta->quantext);

  } else if (mpeg2_hdrs->quantext)
    quantext = mpeg2_hdrs->quantext;

  /*Create xmls for each headers */

  if (sequencehdr)
    if (!create_seq_hdr_xml (writer, sequencehdr))
      return FALSE;

  if (sequenceext) {
    if (!create_seq_ext_xml (writer, sequenceext))
      return FALSE;
  }

  if (mpeg_meta->sequencedispext) {
    if (!create_seq_disp_ext_xml (writer, sequencedispext))
      return FALSE;
  }

  if (quantext) {
    if (!create_quant_ext_xml (writer, quantext))
      return FALSE;
  }
#if 0
  if (mpeg_meta->gophdr) {
    if (!create_gop_hdr_xml (writer, mpeg_meta->gophdr))
      return FALSE;
  }
#endif
  if (mpeg_meta->pichdr) {
    if (!create_pic_hdr_xml (writer, mpeg_meta->pichdr))
      return FALSE;
  }

  if (mpeg_meta->picext) {
    if (!create_pic_ext_xml (writer, mpeg_meta->picext))
      return FALSE;
  }
#if 0
  if (mpeg_meta->slice_info_array) {
    for (i = 0; i < mpeg_meta->slice_info_array->len; i++) {
      GstMpegVideoMetaSliceInfo *slice_info = NULL;
      slice_info =
          &g_array_index (mpeg_meta->slice_info_array,
          GstMpegVideoMetaSliceInfo, i);
      if (!slice_info) {
        g_error ("Failed to get slice details from meta.. \n");
        return FALSE;
      }
      if (!create_slice_hdr_xml (writer, slice_info, i))
        return FALSE;
    }
  }
#endif
  if (xmlTextWriterEndElement (writer) < 0) {
    g_error ("Error: Failed to end mpeg2 root element \n");
    return FALSE;
  }

  if (xmlTextWriterEndDocument (writer) < 0) {
    g_error ("Error: Ending document \n");
    return FALSE;
  }

  xmlFreeTextWriter (writer);

  /* create a new xml file for each frame */
  name = g_strdup_printf ("mpeg2-%d.xml", frame_num);
  file_name = g_build_filename (location, "xml", name, NULL);
  GST_LOG ("Created a New xml file %s to dump the parsed info", file_name);

  xmlSaveFormatFile (file_name, doc, 1);

  if (name)
    g_free (name);
  if (file_name)
    g_free (file_name);

  return TRUE;
}

gboolean
analyzer_create_mpeg2video_frame_hex (GstMpegVideoMeta * mpeg_meta,
    gint frame_num, guint * data)
{
  return TRUE;
}
