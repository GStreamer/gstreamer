/* JPEG-XS support for MPEG-TS
 *
 * Copyright (C) <2024> Centricular ltd
 *  @author Edward Hervey <edward@centricular.com>
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
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "gstbasetsmuxjpegxs.h"
#include <string.h>
#include <gst/base/gstbytewriter.h>
#include <gst/gst.h>

#define GST_CAT_DEFAULT gst_base_ts_mux_debug

GstBuffer *
gst_base_ts_mux_prepare_jpegxs (GstBuffer * buf, GstBaseTsMuxPad * pad,
    GstBaseTsMux * mux)
{
  GstMpegtsJpegXsDescriptor *private_data = pad->prepare_data;
  GstByteWriter wr;
  GstBuffer *out_buf = NULL;
  gsize header_size = 30;
  guint8 *jxes_header = NULL;
  GstClockTime seconds = buf->pts / GST_SECOND;
  GstClockTime minutes = seconds / 60;
  GstClockTime hours = minutes / 60;

  /* FIXME : Instead of constantly allocating/freeing a new header, we should:
   * * Generate this header once
   * * Update it only for the new tcod
   */

  seconds = seconds % 60;
  minutes = minutes % 60;
  hours = hours % 24;

  /* Box is fixed size */
  gst_byte_writer_init_with_size (&wr, header_size, FALSE);

  gst_byte_writer_put_uint32_be (&wr, header_size);
  /* Elementary stream header box 'jxes' == 0x6a786573 */
  gst_byte_writer_put_uint32_be (&wr, 0x6a786573);

  /* brat, frat are 32 bytes */
  gst_byte_writer_put_uint32_be (&wr, private_data->brat);
  gst_byte_writer_put_uint32_be (&wr, private_data->frat);

  /* schar, Ppih, Plev */
  gst_byte_writer_put_uint16_be (&wr, private_data->schar);
  gst_byte_writer_put_uint16_be (&wr, private_data->Ppih);
  gst_byte_writer_put_uint16_be (&wr, private_data->Plev);

  gst_byte_writer_put_uint8 (&wr, private_data->colour_primaries);
  gst_byte_writer_put_uint8 (&wr, private_data->transfer_characteristics);
  gst_byte_writer_put_uint8 (&wr, private_data->matrix_coefficients);

  gst_byte_writer_put_uint8 (&wr, private_data->video_full_range_flag << 7);

  /* put HHMMSSFF */
  gst_byte_writer_put_uint8 (&wr, (guint8) hours);
  gst_byte_writer_put_uint8 (&wr, (guint8) minutes);
  gst_byte_writer_put_uint8 (&wr, (guint8) seconds);
  gst_byte_writer_put_uint8 (&wr, 0x0);

  /* Put jxes header in buffer */
  header_size = gst_byte_writer_get_size (&wr);
  jxes_header = gst_byte_writer_reset_and_get_data (&wr);
  out_buf = gst_buffer_new_wrapped (jxes_header, header_size);

  /* Copy complete frame */
  gst_buffer_copy_into (out_buf, buf,
      GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_TIMESTAMPS |
      GST_BUFFER_COPY_MEMORY, 0, -1);
  GST_DEBUG_OBJECT (mux, "Prepared JPEGXS PES of size %d",
      (int) gst_buffer_get_size (out_buf));

  return out_buf;
}

void
gst_base_ts_mux_free_jpegxs (gpointer prepare_data)
{
  /*  Free prepare data memory object */
  g_free (prepare_data);
}
