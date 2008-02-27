/*
 * camutils.c - 
 * Copyright (C) 2007 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#include <gst/gst.h>
#include <string.h>

#include "cam.h"
#include "camutils.h"

#define GST_CAT_DEFAULT cam_debug_cat

/* From the spec: 
 * length_field() {
 *               size_indicator
 *               if (size_indicator == 0)
 *                        length_value
 *               else if (size_indicator == 1) {
 *                        length_field_size
 *                        for (i=0; i<length_field_size; i++) {
 *                                length_value_byte
 *                        }
 *               }
 * }
*/

guint8
cam_calc_length_field_size (guint length)
{
  guint field_len;

  if (length < G_MAXUINT8)
    field_len = 1;
  else if (length <= G_MAXUINT16)
    field_len = 3;
  else if (length <= (1 << 24) - 1)
    field_len = 4;
  else
    field_len = 5;

  return field_len;
}

/* write a length_field */
guint8
cam_write_length_field (guint8 * buff, guint length)
{
  guint8 field_len = cam_calc_length_field_size (length);

  if (buff) {
    switch (field_len) {
      case 1:
        buff[0] = length;
        break;
      case 2:
        g_return_val_if_reached (0);
        break;
      case 3:
        buff[0] = TPDU_HEADER_SIZE_INDICATOR | (field_len - 1);
        buff[1] = length >> 8;
        buff[2] = length & 0xFF;
        break;
      case 4:
        buff[0] = TPDU_HEADER_SIZE_INDICATOR | (field_len - 1);
        buff[1] = length >> 16;
        buff[2] = (length >> 8) & 0xFF;
        buff[3] = length & 0xFF;
        break;
      case 5:
        buff[0] = TPDU_HEADER_SIZE_INDICATOR | (field_len - 1);
        buff[1] = length >> 24;
        buff[2] = (length >> 16) & 0xFF;
        buff[3] = (length >> 8) & 0xFF;
        buff[4] = length & 0xFF;
        break;
      default:
        g_return_val_if_reached (0);
    }
  }

  return field_len;
}

/* read a length_field */
guint8
cam_read_length_field (guint8 * buff, guint * length)
{
  guint i;
  guint field_len;
  guint8 len;

  if (buff[0] <= G_MAXINT8) {
    field_len = 1;
    len = buff[0];
  } else {
    field_len = buff[0] & ~TPDU_HEADER_SIZE_INDICATOR;
    if (field_len > 4) {
      GST_ERROR ("length_field length exceeds 4 bytes: %d", field_len);
      field_len = 0;
      len = 0;
    } else {
      len = 0;
      for (i = 0; i < field_len; ++i)
        len = (len << 8) | *++buff;

      /* count the size indicator byte */
      field_len += 1;
    }
  }

  if (length)
    *length = len;

  return field_len;
}

/*
 * ca_pmt () {
 *    ca_pmt_tag                                              24        uimsbf
 *    length_field()
 *    ca_pmt_list_management                                   8        uimsbf
 *    program_number                                          16        uimsbf
 *    reserved                                                 2        bslbf
 *    version_number                                           5        uimsbf
 *    current_next_indicator                                   1        bslbf
 *    reserved                                                 4        bslbf
 *    program_info_length                                     12        uimsbf
 *    if (program_info_length != 0) {
 *      ca_pmt_cmd_id at program level                         8        uimsbf
 *      for (i=0; i<n; i++) {
 *        CA_descriptor() programme level 
 *      }
 *   }
 *   for (i=0; i<n; i++) {
 *     stream_type                                             8       uimsbf
 *     reserved                                                3       bslbf
 *     elementary_PID  elementary stream PID                  13        uimsbf
 *     reserved                                                4       bslbf
 *     ES_info_length                                         12        uimsbf
 *     if (ES_info_length != 0) {
 *       ca_pmt_cmd_id at ES level                             8       uimsbf
 *       for (i=0; i<n; i++) {
 *         CA_descriptor() elementary stream level 
 *       }
 *     }
 *   }
 * }
 */

static guint
get_ca_descriptors_length (GValueArray * descriptors)
{
  guint i;
  guint len = 0;
  GValue *value;
  GString *desc;

  if (descriptors != NULL) {
    for (i = 0; i < descriptors->n_values; ++i) {
      value = g_value_array_get_nth (descriptors, i);
      desc = (GString *) g_value_get_boxed (value);

      if (desc->str[0] == 0x09)
        len += desc->len;
    }
  }

  return len;
}

static guint8 *
write_ca_descriptors (guint8 * body, GValueArray * descriptors)
{
  guint i;
  GValue *value;
  GString *desc;

  if (descriptors != NULL) {
    for (i = 0; i < descriptors->n_values; ++i) {
      value = g_value_array_get_nth (descriptors, i);
      desc = (GString *) g_value_get_boxed (value);

      if (desc->str[0] == 0x09) {
        memcpy (body, desc->str, desc->len);
        body += desc->len;
      }
    }
  }

  return body;
}

guint8 *
cam_build_ca_pmt (GstStructure * pmt, guint8 list_management, guint8 cmd_id,
    guint * size)
{
  guint body_size = 0;
  guint8 *buffer;
  guint8 *body;
  GList *lengths = NULL;
  guint len = 0;
  const GValue *streams;
  guint program_number;
  guint version_number;
  guint i;
  const GValue *value;
  GstStructure *stream;
  GValueArray *program_descriptors = NULL;
  GValueArray *stream_descriptors = NULL;

  gst_structure_get_uint (pmt, "program-number", &program_number);
  gst_structure_get_uint (pmt, "version-number", &version_number);
  streams = gst_structure_get_value (pmt, "streams");
  value = gst_structure_get_value (pmt, "descriptors");
  if (value != NULL) {
    program_descriptors = g_value_get_boxed (value);
    /* get the length of program level CA_descriptor()s */
    len = get_ca_descriptors_length (program_descriptors);
    if (len > 0)
      /* add one byte for the program level cmd_id */
      len += 1;
  }
  lengths = g_list_append (lengths, GINT_TO_POINTER (len));
  body_size += 6 + len;

  /* get the length of stream level CA_descriptor()s */
  if (streams != NULL) {
    for (i = 0; i < gst_value_list_get_size (streams); ++i) {
      value = gst_value_list_get_value (streams, i);
      stream = g_value_get_boxed (value);

      value = gst_structure_get_value (stream, "descriptors");
      if (value != NULL) {
        stream_descriptors = g_value_get_boxed (value);

        len = get_ca_descriptors_length (stream_descriptors);
        if (len > 0)
          /* one byte for the stream level cmd_id */
          len += 1;
      }

      lengths = g_list_append (lengths, GINT_TO_POINTER (len));
      body_size += 5 + len;

    }
  }
  buffer = g_malloc0 (body_size);
  body = buffer;

  *body++ = list_management;

  GST_WRITE_UINT16_BE (body, program_number);
  body += 2;

  *body++ = (version_number << 1) | 0x01;

  len = GPOINTER_TO_INT (lengths->data);
  lengths = g_list_delete_link (lengths, lengths);
  GST_WRITE_UINT16_BE (body, len);
  body += 2;

  if (len != 0) {
    *body++ = cmd_id;

    body = write_ca_descriptors (body, program_descriptors);
  }

  for (i = 0; i < gst_value_list_get_size (streams); ++i) {
    guint stream_type;
    guint stream_pid;

    value = gst_value_list_get_value (streams, i);
    stream = g_value_get_boxed (value);

    gst_structure_get_uint (stream, "stream-type", &stream_type);
    gst_structure_get_uint (stream, "pid", &stream_pid);
    value = gst_structure_get_value (stream, "descriptors");
    stream_descriptors = g_value_get_boxed (value);

    *body++ = stream_type;
    GST_WRITE_UINT16_BE (body, stream_pid);
    body += 2;
    len = GPOINTER_TO_INT (lengths->data);
    lengths = g_list_delete_link (lengths, lengths);
    GST_WRITE_UINT16_BE (body, len);
    body += 2;

    if (len != 0) {
      *body++ = cmd_id;
      body = write_ca_descriptors (body, stream_descriptors);
    }
  }

  *size = body_size;
  return buffer;
}
