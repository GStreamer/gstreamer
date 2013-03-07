/* 
 * Copyright 2006, 2007, 2008 Fluendo S.A. 
 *  Authors: Jan Schmidt <jan@fluendo.com>
 *           Kapil Agrawal <kapil@fluendo.com>
 *           Julien Moutte <julien@fluendo.com>
 *
 * This library is licensed under 4 different licenses and you
 * can choose to use it under the terms of any one of them. The
 * four licenses are the MPL 1.1, the LGPL, the GPL and the MIT
 * license.
 *
 * MPL:
 * 
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * LGPL:
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
 * GPL:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * MIT:
 *
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mpegtsmux_aac.h"
#include <string.h>

#define GST_CAT_DEFAULT mpegtsmux_debug

GstBuffer *
mpegtsmux_prepare_aac (GstBuffer * buf, MpegTsPadData * data, MpegTsMux * mux)
{
  guint8 adts_header[7] = { 0, };
  gsize out_size = gst_buffer_get_size (buf) + 7;
  GstBuffer *out_buf = gst_buffer_new_and_alloc (out_size);
  gsize out_offset = 0;
  guint8 rate_idx = 0, channels = 0, obj_type = 0;
  GstMapInfo codec_data_map;
  GstMapInfo buf_map;

  GST_DEBUG_OBJECT (mux, "Preparing AAC buffer for output");

  gst_buffer_copy_into (out_buf, buf,
      GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_TIMESTAMPS, 0, 0);

  gst_buffer_map (data->codec_data, &codec_data_map, GST_MAP_READ);

  /* Generate ADTS header */
  obj_type = GST_READ_UINT8 (codec_data_map.data) >> 3;
  rate_idx = (GST_READ_UINT8 (codec_data_map.data) & 0x7) << 1;
  rate_idx |= (GST_READ_UINT8 (codec_data_map.data + 1) & 0x80) >> 7;
  channels = (GST_READ_UINT8 (codec_data_map.data + 1) & 0x78) >> 3;
  GST_DEBUG_OBJECT (mux, "Rate index %u, channels %u, object type %u", rate_idx,
      channels, obj_type);
  /* Sync point over a full byte */
  adts_header[0] = 0xFF;
  /* Sync point continued over first 4 bits + static 4 bits
   * (ID, layer, protection)*/
  adts_header[1] = 0xF1;
  /* Object type over first 2 bits */
  adts_header[2] = (obj_type - 1) << 6;
  /* rate index over next 4 bits */
  adts_header[2] |= (rate_idx << 2);
  /* channels over last 2 bits */
  adts_header[2] |= (channels & 0x4) >> 2;
  /* channels continued over next 2 bits + 4 bits at zero */
  adts_header[3] = (channels & 0x3) << 6;
  /* frame size over last 2 bits */
  adts_header[3] |= (out_size & 0x1800) >> 11;
  /* frame size continued over full byte */
  adts_header[4] = (out_size & 0x1FF8) >> 3;
  /* frame size continued first 3 bits */
  adts_header[5] = (out_size & 0x7) << 5;
  /* buffer fullness (0x7FF for VBR) over 5 last bits */
  adts_header[5] |= 0x1F;
  /* buffer fullness (0x7FF for VBR) continued over 6 first bits + 2 zeros for
   * number of raw data blocks */
  adts_header[6] = 0xFC;

  /* Insert ADTS header */
  gst_buffer_fill (out_buf, out_offset, adts_header, 7);
  out_offset += 7;

  gst_buffer_map (buf, &buf_map, GST_MAP_READ);

  /* Now copy complete frame */
  gst_buffer_fill (out_buf, out_offset, buf_map.data, buf_map.size);

  gst_buffer_unmap (data->codec_data, &codec_data_map);
  gst_buffer_unmap (buf, &buf_map);

  return out_buf;
}
