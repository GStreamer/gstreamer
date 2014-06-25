/* 
 * Copyright 2006 BBC and Fluendo S.A. 
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

#include <string.h>

#include <gst/mpegts/mpegts.h>

#include "tsmux.h"
#include "tsmuxstream.h"

#define GST_CAT_DEFAULT mpegtsmux_debug

/* Maximum total data length for a PAT section is 1024 bytes, minus an 
 * 8 byte header, then the length of each program entry is 32 bits, 
 * then finally a 32 bit CRC. Thus the maximum number of programs in this mux
 * is (1024 - 8 - 4) / 4 = 253 because it only supports single section PATs */
#define TSMUX_MAX_PROGRAMS 253

#define TSMUX_SECTION_HDR_SIZE 8

#define TSMUX_DEFAULT_NETWORK_ID 0x0001
#define TSMUX_DEFAULT_TS_ID 0x0001

/* HACK: We use a fixed buffering offset for the PCR at the moment - 
 * this is the amount 'in advance' of the stream that the PCR sits.
 * 1/8 second atm */
#define TSMUX_PCR_OFFSET (TSMUX_CLOCK_FREQ / 8)

/* Times per second to write PCR */
#define TSMUX_DEFAULT_PCR_FREQ (25)

/* Base for all written PCR and DTS/PTS,
 * so we have some slack to go backwards */
#define CLOCK_BASE (TSMUX_CLOCK_FREQ * 10 * 360)

static gboolean tsmux_write_pat (TsMux * mux);
static gboolean tsmux_write_pmt (TsMux * mux, TsMuxProgram * program);
static void
tsmux_section_free (TsMuxSection * section)
{
  gst_mpegts_section_unref (section->section);
  g_slice_free (TsMuxSection, section);
}

/**
 * tsmux_new:
 *
 * Create a new muxer session.
 *
 * Returns: A new #TsMux object.
 */
TsMux *
tsmux_new (void)
{
  TsMux *mux;

  mux = g_slice_new0 (TsMux);

  mux->transport_id = TSMUX_DEFAULT_TS_ID;

  mux->next_pgm_no = TSMUX_START_PROGRAM_ID;
  mux->next_pmt_pid = TSMUX_START_PMT_PID;
  mux->next_stream_pid = TSMUX_START_ES_PID;

  mux->pat_changed = TRUE;
  mux->last_pat_ts = -1;
  mux->pat_interval = TSMUX_DEFAULT_PAT_INTERVAL;

  mux->si_changed = TRUE;
  mux->last_si_ts = -1;
  mux->si_interval = TSMUX_DEFAULT_SI_INTERVAL;

  mux->si_sections = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) tsmux_section_free);

  return mux;
}

/**
 * tsmux_set_write_func:
 * @mux: a #TsMux
 * @func: a user callback function
 * @user_data: user data passed to @func
 *
 * Set the callback function and user data to be called when @mux has output to
 * produce. @user_data will be passed as user data in @func.
 */
void
tsmux_set_write_func (TsMux * mux, TsMuxWriteFunc func, void *user_data)
{
  g_return_if_fail (mux != NULL);

  mux->write_func = func;
  mux->write_func_data = user_data;
}

/**
 * tsmux_set_alloc_func:
 * @mux: a #TsMux
 * @func: a user callback function
 * @user_data: user data passed to @func
 *
 * Set the callback function and user data to be called when @mux needs
 * a new buffer to write a packet into.
 * @user_data will be passed as user data in @func.
 */
void
tsmux_set_alloc_func (TsMux * mux, TsMuxAllocFunc func, void *user_data)
{
  g_return_if_fail (mux != NULL);

  mux->alloc_func = func;
  mux->alloc_func_data = user_data;
}

/**
 * tsmux_set_pat_interval:
 * @mux: a #TsMux
 * @freq: a new PAT interval
 *
 * Set the interval (in cycles of the 90kHz clock) for writing out the PAT table.
 *
 * Many transport stream clients might have problems if the PAT table is not
 * inserted in the stream at regular intervals, especially when initially trying
 * to figure out the contents of the stream.
 */
void
tsmux_set_pat_interval (TsMux * mux, guint freq)
{
  g_return_if_fail (mux != NULL);

  mux->pat_interval = freq;
}

/**
 * tsmux_get_pat_interval:
 * @mux: a #TsMux
 *
 * Get the configured PAT interval. See also tsmux_set_pat_interval().
 *
 * Returns: the configured PAT interval
 */
guint
tsmux_get_pat_interval (TsMux * mux)
{
  g_return_val_if_fail (mux != NULL, 0);

  return mux->pat_interval;
}

/**
 * tsmux_set_si_interval:
 * @mux: a #TsMux
 * @freq: a new SI table interval
 *
 * Set the interval (in cycles of the 90kHz clock) for writing out the SI tables.
 *
 */
void
tsmux_set_si_interval (TsMux * mux, guint freq)
{
  g_return_if_fail (mux != NULL);

  mux->si_interval = freq;
}

/**
 * tsmux_get_si_interval:
 * @mux: a #TsMux
 *
 * Get the configured SI table interval. See also tsmux_set_si_interval().
 *
 * Returns: the configured SI interval
 */
guint
tsmux_get_si_interval (TsMux * mux)
{
  g_return_val_if_fail (mux != NULL, 0);

  return mux->si_interval;
}

/**
 * tsmux_add_mpegts_si_section:
 * @mux: a #TsMux
 * @section: (transfer full): a #GstMpegtsSection to add
 *
 * Add a Service Information #GstMpegtsSection to the stream
 *
 * Returns: #TRUE on success, #FALSE otherwise
 */
gboolean
tsmux_add_mpegts_si_section (TsMux * mux, GstMpegtsSection * section)
{
  TsMuxSection *tsmux_section;

  g_return_val_if_fail (mux != NULL, FALSE);
  g_return_val_if_fail (section != NULL, FALSE);
  g_return_val_if_fail (mux->si_sections != NULL, FALSE);

  tsmux_section = g_slice_new0 (TsMuxSection);

  GST_DEBUG ("Adding mpegts section with type %d to mux",
      section->section_type);

  tsmux_section->section = section;
  tsmux_section->pi.pid = section->pid;

  g_hash_table_insert (mux->si_sections,
      GINT_TO_POINTER (section->section_type), tsmux_section);

  mux->si_changed = TRUE;

  return TRUE;
}

/**
 * tsmux_free:
 * @mux: a #TsMux
 *
 * Free all resources associated with @mux. After calling this function @mux can
 * not be used anymore.
 */
void
tsmux_free (TsMux * mux)
{
  GList *cur;

  g_return_if_fail (mux != NULL);

  /* Free PAT section */
  if (mux->pat.section)
    gst_mpegts_section_unref (mux->pat.section);

  /* Free all programs */
  for (cur = mux->programs; cur; cur = cur->next) {
    TsMuxProgram *program = (TsMuxProgram *) cur->data;

    tsmux_program_free (program);
  }
  g_list_free (mux->programs);

  /* Free all streams */
  for (cur = mux->streams; cur; cur = cur->next) {
    TsMuxStream *stream = (TsMuxStream *) cur->data;

    tsmux_stream_free (stream);
  }
  g_list_free (mux->streams);

  /* Free SI table sections */
  g_hash_table_destroy (mux->si_sections);

  g_slice_free (TsMux, mux);
}

static gint
tsmux_program_compare (TsMuxProgram * program, gint * needle)
{
  return (program->pgm_number - *needle);
}

/**
 * tsmux_program_new:
 * @mux: a #TsMux
 *
 * Create a new program in the mising session @mux.
 *
 * Returns: a new #TsMuxProgram or %NULL when the maximum number of programs has
 * been reached.
 */
TsMuxProgram *
tsmux_program_new (TsMux * mux, gint prog_id)
{
  TsMuxProgram *program;

  g_return_val_if_fail (mux != NULL, NULL);

  /* Ensure we have room for another program */
  if (mux->nb_programs == TSMUX_MAX_PROGRAMS)
    return NULL;

  program = g_slice_new0 (TsMuxProgram);

  program->pmt_changed = TRUE;
  program->last_pmt_ts = -1;
  program->pmt_interval = TSMUX_DEFAULT_PMT_INTERVAL;

  if (prog_id == 0) {
    program->pgm_number = mux->next_pgm_no++;
    while (g_list_find_custom (mux->programs, &program->pgm_number,
            (GCompareFunc) tsmux_program_compare) != NULL) {
      program->pgm_number = mux->next_pgm_no++;
    }
  } else {
    program->pgm_number = prog_id;
    while (g_list_find_custom (mux->programs, &program->pgm_number,
            (GCompareFunc) tsmux_program_compare) != NULL) {
      program->pgm_number++;
    }
  }

  program->pmt_pid = mux->next_pmt_pid++;
  program->pcr_stream = NULL;

  program->streams = g_array_sized_new (FALSE, TRUE, sizeof (TsMuxStream *), 1);

  mux->programs = g_list_prepend (mux->programs, program);
  mux->nb_programs++;
  mux->pat_changed = TRUE;

  return program;
}

/**
 * tsmux_set_pmt_interval:
 * @program: a #TsMuxProgram
 * @freq: a new PMT interval
 *
 * Set the interval (in cycles of the 90kHz clock) for writing out the PMT table.
 *
 * Many transport stream clients might have problems if the PMT table is not
 * inserted in the stream at regular intervals, especially when initially trying
 * to figure out the contents of the stream.
 */
void
tsmux_set_pmt_interval (TsMuxProgram * program, guint freq)
{
  g_return_if_fail (program != NULL);

  program->pmt_interval = freq;
}

/**
 * tsmux_get_pmt_interval:
 * @program: a #TsMuxProgram
 *
 * Get the configured PMT interval. See also tsmux_set_pmt_interval().
 *
 * Returns: the configured PMT interval
 */
guint
tsmux_get_pmt_interval (TsMuxProgram * program)
{
  g_return_val_if_fail (program != NULL, 0);

  return program->pmt_interval;
}

/**
 * tsmux_program_add_stream:
 * @program: a #TsMuxProgram
 * @stream: a #TsMuxStream
 *
 * Add @stream to @program.
 */
void
tsmux_program_add_stream (TsMuxProgram * program, TsMuxStream * stream)
{
  g_return_if_fail (program != NULL);
  g_return_if_fail (stream != NULL);

  g_array_append_val (program->streams, stream);
  program->pmt_changed = TRUE;
}

/**
 * tsmux_program_set_pcr_stream:
 * @program: a #TsMuxProgram
 * @stream: a #TsMuxStream
 *
 * Set @stream as the PCR stream for @program, overwriting the previously
 * configured PCR stream. When @stream is NULL, program will have no PCR stream
 * configured.
 */
void
tsmux_program_set_pcr_stream (TsMuxProgram * program, TsMuxStream * stream)
{
  g_return_if_fail (program != NULL);

  if (program->pcr_stream == stream)
    return;

  if (program->pcr_stream != NULL)
    tsmux_stream_pcr_unref (program->pcr_stream);
  if (stream)
    tsmux_stream_pcr_ref (stream);
  program->pcr_stream = stream;

  program->pmt_changed = TRUE;
}

/**
 * tsmux_get_new_pid:
 * @mux: a #TsMux
 *
 * Get a new free PID.
 *
 * Returns: a new free PID.
 */
guint16
tsmux_get_new_pid (TsMux * mux)
{
  g_return_val_if_fail (mux != NULL, -1);

  /* make sure this PID is free
   * (and not taken by a specific earlier request) */
  do {
    mux->next_stream_pid++;
  } while (tsmux_find_stream (mux, mux->next_stream_pid));

  return mux->next_stream_pid;
}

/**
 * tsmux_create_stream:
 * @mux: a #TsMux
 * @stream_type: a #TsMuxStreamType
 * @pid: the PID of the new stream.
 *
 * Create a new stream of @stream_type in the muxer session @mux.
 *
 * When @pid is set to #TSMUX_PID_AUTO, a new free PID will automatically
 * be allocated for the new stream.
 *
 * Returns: a new #TsMuxStream.
 */
TsMuxStream *
tsmux_create_stream (TsMux * mux, TsMuxStreamType stream_type, guint16 pid,
    gchar * language)
{
  TsMuxStream *stream;
  guint16 new_pid;

  g_return_val_if_fail (mux != NULL, NULL);

  if (pid == TSMUX_PID_AUTO) {
    new_pid = tsmux_get_new_pid (mux);
  } else {
    new_pid = pid & 0x1FFF;
  }

  /* Ensure we're not creating a PID collision */
  if (tsmux_find_stream (mux, new_pid))
    return NULL;

  stream = tsmux_stream_new (new_pid, stream_type);

  mux->streams = g_list_prepend (mux->streams, stream);
  mux->nb_streams++;

  if (language)
    g_strlcat (stream->language, language, 3 * sizeof (gchar));
  else
    g_strlcat (stream->language, "eng", 3 * sizeof (gchar));

  return stream;
}

/**
 * tsmux_find_stream:
 * @mux: a #TsMux
 * @pid: the PID to find.
 *
 * Find the stream associated wih PID.
 *
 * Returns: a #TsMuxStream with @pid or NULL when the stream was not found.
 */
TsMuxStream *
tsmux_find_stream (TsMux * mux, guint16 pid)
{
  TsMuxStream *found = NULL;
  GList *cur;

  g_return_val_if_fail (mux != NULL, NULL);

  for (cur = mux->streams; cur; cur = cur->next) {
    TsMuxStream *stream = (TsMuxStream *) cur->data;

    if (tsmux_stream_get_pid (stream) == pid) {
      found = stream;
      break;
    }
  }
  return found;
}

static gboolean
tsmux_get_buffer (TsMux * mux, GstBuffer ** buf)
{
  g_return_val_if_fail (buf, FALSE);

  if (G_UNLIKELY (!mux->alloc_func))
    return FALSE;

  mux->alloc_func (buf, mux->alloc_func_data);

  if (!*buf)
    return FALSE;

  g_assert (gst_buffer_get_size (*buf) == TSMUX_PACKET_LENGTH);
  return TRUE;
}

static gboolean
tsmux_packet_out (TsMux * mux, GstBuffer * buf, gint64 pcr)
{
  if (G_UNLIKELY (mux->write_func == NULL)) {
    if (buf)
      gst_buffer_unref (buf);
    return TRUE;
  }

  return mux->write_func (buf, mux->write_func_data, pcr);
}

/*
 * adaptation_field() {
 *   adaptation_field_length                              8 uimsbf
 *   if(adaptation_field_length >0) {
 *     discontinuity_indicator                            1 bslbf
 *     random_access_indicator                            1 bslbf
 *     elementary_stream_priority_indicator               1 bslbf
 *     PCR_flag                                           1 bslbf
 *     OPCR_flag                                          1 bslbf
 *     splicing_point_flag                                1 bslbf
 *     transport_private_data_flag                        1 bslbf
 *     adaptation_field_extension_flag                    1 bslbf
 *     if(PCR_flag == '1') {
 *       program_clock_reference_base                    33 uimsbf
 *       reserved                                         6 bslbf
 *       program_clock_reference_extension                9 uimsbf
 *     }
 *     if(OPCR_flag == '1') {
 *       original_program_clock_reference_base           33 uimsbf
 *       reserved                                         6 bslbf
 *       original_program_clock_reference_extension       9 uimsbf
 *     }
 *     if (splicing_point_flag == '1') {
 *       splice_countdown                                 8 tcimsbf
 *     }
 *     if(transport_private_data_flag == '1') {
 *       transport_private_data_length                    8 uimsbf
 *       for (i=0; i<transport_private_data_length;i++){
 *         private_data_byte                              8 bslbf
 *       }
 *     }
 *     if (adaptation_field_extension_flag == '1' ) {
 *       adaptation_field_extension_length                8 uimsbf
 *       ltw_flag                                         1 bslbf
 *       piecewise_rate_flag                              1 bslbf
 *       seamless_splice_flag                             1 bslbf
 *       reserved                                         5 bslbf
 *       if (ltw_flag == '1') {
 *         ltw_valid_flag                                 1 bslbf
 *         ltw_offset                                    15 uimsbf
 *       }
 *       if (piecewise_rate_flag == '1') {
 *         reserved                                       2 bslbf
 *         piecewise_rate                                22 uimsbf
 *       }
 *       if (seamless_splice_flag == '1'){
 *         splice_type                                    4 bslbf
 *         DTS_next_AU[32..30]                            3 bslbf
 *         marker_bit                                     1 bslbf
 *         DTS_next_AU[29..15]                           15 bslbf
 *         marker_bit                                     1 bslbf
 *         DTS_next_AU[14..0]                            15 bslbf
 *         marker_bit                                     1 bslbf
 *       }
 *       for ( i=0;i<N;i++) {
 *         reserved                                       8 bslbf
 *       }
 *     }
 *     for (i=0;i<N;i++){
 *       stuffing_byte                                    8 bslbf
 *     }
 *   }
 * }
 */
static gboolean
tsmux_write_adaptation_field (guint8 * buf,
    TsMuxPacketInfo * pi, guint8 min_length, guint8 * written)
{
  guint8 pos = 2;
  guint8 flags = 0;

  g_assert (min_length <= TSMUX_PAYLOAD_LENGTH);

  /* Write out all the fields from the packet info only if the 
   * user set the flag to request the adaptation field - if the flag
   * isn't set, we're just supposed to write stuffing bytes */
  if (pi->flags & TSMUX_PACKET_FLAG_ADAPTATION) {
    TS_DEBUG ("writing adaptation fields");
    if (pi->flags & TSMUX_PACKET_FLAG_DISCONT)
      flags |= 0x80;
    if (pi->flags & TSMUX_PACKET_FLAG_RANDOM_ACCESS)
      flags |= 0x40;
    if (pi->flags & TSMUX_PACKET_FLAG_PRIORITY)
      flags |= 0x20;
    if (pi->flags & TSMUX_PACKET_FLAG_WRITE_PCR) {
      guint64 pcr_base;
      guint32 pcr_ext;

      pcr_base = (pi->pcr / 300);
      pcr_ext = (pi->pcr % 300);

      flags |= 0x10;
      TS_DEBUG ("Writing PCR %" G_GUINT64_FORMAT " + ext %u", pcr_base,
          pcr_ext);
      buf[pos++] = (pcr_base >> 25) & 0xff;
      buf[pos++] = (pcr_base >> 17) & 0xff;
      buf[pos++] = (pcr_base >> 9) & 0xff;
      buf[pos++] = (pcr_base >> 1) & 0xff;
      buf[pos++] = ((pcr_base << 7) & 0x80) | ((pcr_ext >> 8) & 0x01);
      buf[pos++] = (pcr_ext) & 0xff;
    }
    if (pi->flags & TSMUX_PACKET_FLAG_WRITE_OPCR) {
      guint64 opcr_base;
      guint32 opcr_ext;

      opcr_base = (pi->opcr / 300);
      opcr_ext = (pi->opcr % 300);

      flags |= 0x08;
      TS_DEBUG ("Writing OPCR");
      buf[pos++] = (opcr_base >> 25) & 0xff;
      buf[pos++] = (opcr_base >> 17) & 0xff;
      buf[pos++] = (opcr_base >> 9) & 0xff;
      buf[pos++] = (opcr_base >> 1) & 0xff;
      buf[pos++] = ((opcr_base << 7) & 0x80) | ((opcr_ext >> 8) & 0x01);
      buf[pos++] = (opcr_ext) & 0xff;
    }
    if (pi->flags & TSMUX_PACKET_FLAG_WRITE_SPLICE) {
      flags |= 0x04;
      buf[pos++] = pi->splice_countdown;
    }
    if (pi->private_data_len > 0) {
      flags |= 0x02;
      /* Private data to write, ensure we have enough room */
      if ((1 + pi->private_data_len) > (TSMUX_PAYLOAD_LENGTH - pos))
        return FALSE;
      buf[pos++] = pi->private_data_len;
      memcpy (&(buf[pos]), pi->private_data, pi->private_data_len);
      pos += pi->private_data_len;
      TS_DEBUG ("%u bytes of private data", pi->private_data_len);
    }
    if (pi->flags & TSMUX_PACKET_FLAG_WRITE_ADAPT_EXT) {
      flags |= 0x01;
      TS_DEBUG ("FIXME: write Adaptation extension");
      /* Write an empty extension for now */
      buf[pos++] = 1;
      buf[pos++] = 0;
    }
  }
  /* Write the flags at the start */
  buf[1] = flags;

  /* Stuffing bytes if needed */
  while (pos < min_length)
    buf[pos++] = 0xff;

  /* Write the adaptation field length, which doesn't include its own byte */
  buf[0] = pos - 1;

  if (written)
    *written = pos;

  return TRUE;
}

static gboolean
tsmux_write_ts_header (guint8 * buf, TsMuxPacketInfo * pi,
    guint * payload_len_out, guint * payload_offset_out)
{
  guint8 *tmp;
  guint8 adaptation_flag;
  guint8 adapt_min_length = 0;
  guint8 adapt_len = 0;
  guint payload_len;
  gboolean write_adapt = FALSE;

  /* Sync byte */
  buf[0] = TSMUX_SYNC_BYTE;

  TS_DEBUG ("PID 0x%04x, counter = 0x%01x, %u bytes avail", pi->pid,
      pi->packet_count & 0x0f, pi->stream_avail);

  /* 3 bits: 
   *   transport_error_indicator
   *   payload_unit_start_indicator
   *   transport_priority: (00)
   * 13 bits: PID
   */
  tmp = buf + 1;
  if (pi->packet_start_unit_indicator) {
    tsmux_put16 (&tmp, 0x4000 | pi->pid);
  } else
    tsmux_put16 (&tmp, pi->pid);

  /* 2 bits: scrambling_control (NOT SUPPORTED) (00)
   * 2 bits: adaptation field control (1x has_adaptation_field | x1 has_payload)
   * 4 bits: continuity counter (xxxx)
   */
  adaptation_flag = pi->packet_count & 0x0f;

  if (pi->flags & TSMUX_PACKET_FLAG_ADAPTATION) {
    write_adapt = TRUE;
  }

  if (pi->stream_avail < TSMUX_PAYLOAD_LENGTH) {
    /* Need an adaptation field regardless for stuffing */
    adapt_min_length = TSMUX_PAYLOAD_LENGTH - pi->stream_avail;
    write_adapt = TRUE;
  }

  if (write_adapt) {
    gboolean res;

    /* Flag the adaptation field presence */
    adaptation_flag |= 0x20;
    res = tsmux_write_adaptation_field (buf + TSMUX_HEADER_LENGTH,
        pi, adapt_min_length, &adapt_len);
    if (G_UNLIKELY (res == FALSE))
      return FALSE;

    /* Should have written at least the number of bytes we requested */
    g_assert (adapt_len >= adapt_min_length);
  }

  /* The amount of packet data we wrote is the remaining space after
   * the adaptation field */
  *payload_len_out = payload_len = TSMUX_PAYLOAD_LENGTH - adapt_len;
  *payload_offset_out = TSMUX_HEADER_LENGTH + adapt_len;

  /* Now if we are going to write out some payload, flag that fact */
  if (payload_len > 0 && pi->stream_avail > 0) {
    /* Flag the presence of a payload */
    adaptation_flag |= 0x10;

    /* We must have enough data to fill the payload, or some calculation
     * went wrong */
    g_assert (payload_len <= pi->stream_avail);

    /* Packet with payload, increment the continuity counter */
    pi->packet_count++;
  }

  /* Write the byte of transport_scrambling_control, adaptation_field_control 
   * + continuity counter out */
  buf[3] = adaptation_flag;


  if (write_adapt) {
    TS_DEBUG ("Adaptation field of size >= %d + %d bytes payload",
        adapt_len, payload_len);
  } else {
    TS_DEBUG ("Payload of %d bytes only", payload_len);
  }

  return TRUE;
}

static gboolean
tsmux_section_write_packet (GstMpegtsSectionType * type,
    TsMuxSection * section, TsMux * mux)
{
  GstBuffer *section_buffer;
  GstBuffer *packet_buffer = NULL;
  GstMemory *mem;
  guint8 *packet;
  guint8 *data;
  gsize data_size = 0;
  gsize payload_written;
  guint len = 0, offset = 0, payload_len = 0;

  g_return_val_if_fail (section != NULL, FALSE);
  g_return_val_if_fail (mux != NULL, FALSE);

  /* Mark the start of new PES unit */
  section->pi.packet_start_unit_indicator = TRUE;

  data = gst_mpegts_section_packetize (section->section, &data_size);

  if (!data) {
    TS_DEBUG ("Could not packetize section");
    return FALSE;
  }

  /* Mark payload data size */
  section->pi.stream_avail = data_size;
  payload_written = 0;

  /* Wrap section data in a buffer without free function.
     The data will be freed when the GstMpegtsSection is destroyed. */
  section_buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      data, data_size, 0, data_size, NULL, NULL);

  TS_DEBUG ("Section buffer with size %" G_GSIZE_FORMAT " created",
      gst_buffer_get_size (section_buffer));

  while (section->pi.stream_avail > 0) {

    packet = g_malloc (TSMUX_PACKET_LENGTH);

    if (section->pi.packet_start_unit_indicator) {
      /* Wee need room for a pointer byte */
      section->pi.stream_avail++;

      if (!tsmux_write_ts_header (packet, &section->pi, &len, &offset))
        goto fail;

      /* Write the pointer byte */
      packet[offset++] = 0x00;
      payload_len = len - 1;

    } else {
      if (!tsmux_write_ts_header (packet, &section->pi, &len, &offset))
        goto fail;
      payload_len = len;
    }

    /* Wrap the TS header and adaption field in a GstMemory */
    mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
        packet, TSMUX_PACKET_LENGTH, 0, offset, packet, g_free);

    TS_DEBUG ("Creating packet buffer at offset "
        "%" G_GSIZE_FORMAT " with length %u", payload_written, payload_len);

    packet_buffer = gst_buffer_copy_region (section_buffer, GST_BUFFER_COPY_ALL,
        payload_written, payload_len);

    /* Prepend the header to the section data */
    gst_buffer_prepend_memory (packet_buffer, mem);

    TS_DEBUG ("Writing %d bytes to section. %d bytes remaining",
        len, section->pi.stream_avail - len);

    /* Push the packet without PCR */
    if G_UNLIKELY
      (!tsmux_packet_out (mux, packet_buffer, -1)) {
      /* Buffer given away */
      packet_buffer = NULL;
      goto fail;
      }

    packet_buffer = NULL;
    section->pi.stream_avail -= len;
    payload_written += payload_len;
    section->pi.packet_start_unit_indicator = FALSE;
  }

  gst_buffer_unref (section_buffer);

  return TRUE;

fail:
  if (packet)
    g_free (packet);
  if (section_buffer)
    gst_buffer_unref (section_buffer);
  return FALSE;
}

static gboolean
tsmux_write_si (TsMux * mux)
{
  g_hash_table_foreach (mux->si_sections,
      (GHFunc) tsmux_section_write_packet, mux);

  mux->si_changed = FALSE;

  return TRUE;

}

/**
 * tsmux_write_stream_packet:
 * @mux: a #TsMux
 * @stream: a #TsMuxStream
 *
 * Write a packet of @stream.
 *
 * Returns: TRUE if the packet could be written.
 */
gboolean
tsmux_write_stream_packet (TsMux * mux, TsMuxStream * stream)
{
  guint payload_len, payload_offs;
  TsMuxPacketInfo *pi = &stream->pi;
  gboolean res;
  gint64 cur_pcr = -1;
  GstBuffer *buf = NULL;
  GstMapInfo map;

  g_return_val_if_fail (mux != NULL, FALSE);
  g_return_val_if_fail (stream != NULL, FALSE);

  if (tsmux_stream_is_pcr (stream)) {
    gint64 cur_pts = tsmux_stream_get_pts (stream);
    gboolean write_pat;
    gboolean write_si;
    GList *cur;

    cur_pcr = 0;
    if (cur_pts != -1) {
      TS_DEBUG ("TS for PCR stream is %" G_GINT64_FORMAT, cur_pts);
    }

    /* FIXME: The current PCR needs more careful calculation than just
     * writing a fixed offset */
    if (cur_pts != -1) {
      /* CLOCK_BASE >= TSMUX_PCR_OFFSET */
      cur_pts += CLOCK_BASE;
      cur_pcr = (cur_pts - TSMUX_PCR_OFFSET) *
          (TSMUX_SYS_CLOCK_FREQ / TSMUX_CLOCK_FREQ);
    }

    /* Need to decide whether to write a new PCR in this packet */
    if (stream->last_pcr == -1 ||
        (cur_pcr - stream->last_pcr >
            (TSMUX_SYS_CLOCK_FREQ / TSMUX_DEFAULT_PCR_FREQ))) {

      stream->pi.flags |=
          TSMUX_PACKET_FLAG_ADAPTATION | TSMUX_PACKET_FLAG_WRITE_PCR;
      stream->pi.pcr = cur_pcr;
      stream->last_pcr = cur_pcr;
    } else {
      cur_pcr = -1;
    }

    /* check if we need to rewrite pat */
    if (mux->last_pat_ts == -1 || mux->pat_changed)
      write_pat = TRUE;
    else if (cur_pts >= mux->last_pat_ts + mux->pat_interval)
      write_pat = TRUE;
    else
      write_pat = FALSE;

    if (write_pat) {
      mux->last_pat_ts = cur_pts;
      if (!tsmux_write_pat (mux))
        return FALSE;
    }

    /* check if we need to rewrite sit */
    if (mux->last_si_ts == -1 || mux->si_changed)
      write_si = TRUE;
    else if (cur_pts >= mux->last_si_ts + mux->si_interval)
      write_si = TRUE;
    else
      write_si = FALSE;

    if (write_si) {
      mux->last_si_ts = cur_pts;
      if (!tsmux_write_si (mux))
        return FALSE;
    }

    /* check if we need to rewrite any of the current pmts */
    for (cur = mux->programs; cur; cur = cur->next) {
      TsMuxProgram *program = (TsMuxProgram *) cur->data;
      gboolean write_pmt;

      if (program->last_pmt_ts == -1 || program->pmt_changed)
        write_pmt = TRUE;
      else if (cur_pts >= program->last_pmt_ts + program->pmt_interval)
        write_pmt = TRUE;
      else
        write_pmt = FALSE;

      if (write_pmt) {
        program->last_pmt_ts = cur_pts;
        if (!tsmux_write_pmt (mux, program))
          return FALSE;
      }
    }
  }

  pi->packet_start_unit_indicator = tsmux_stream_at_pes_start (stream);
  if (pi->packet_start_unit_indicator) {
    tsmux_stream_initialize_pes_packet (stream);
    if (stream->dts != -1)
      stream->dts += CLOCK_BASE;
    if (stream->pts != -1)
      stream->pts += CLOCK_BASE;
  }
  pi->stream_avail = tsmux_stream_bytes_avail (stream);

  /* obtain buffer */
  if (!tsmux_get_buffer (mux, &buf))
    return FALSE;

  gst_buffer_map (buf, &map, GST_MAP_READ);

  if (!tsmux_write_ts_header (map.data, pi, &payload_len, &payload_offs))
    goto fail;


  if (!tsmux_stream_get_data (stream, map.data + payload_offs, payload_len))
    goto fail;

  gst_buffer_unmap (buf, &map);

  res = tsmux_packet_out (mux, buf, cur_pcr);

  /* Reset all dynamic flags */
  stream->pi.flags &= TSMUX_PACKET_FLAG_PES_FULL_HEADER;

  return res;

  /* ERRORS */
fail:
  {
    gst_buffer_unmap (buf, &map);
    if (buf)
      gst_buffer_unref (buf);
    return FALSE;
  }
}

/**
 * tsmux_program_free:
 * @program: a #TsMuxProgram
 *
 * Free the resources of @program. After this call @program can not be used
 * anymore.
 */
void
tsmux_program_free (TsMuxProgram * program)
{
  g_return_if_fail (program != NULL);

  /* Free PMT section */
  if (program->pmt.section)
    gst_mpegts_section_unref (program->pmt.section);

  g_array_free (program->streams, TRUE);
  g_slice_free (TsMuxProgram, program);
}

static gboolean
tsmux_write_pat (TsMux * mux)
{

  if (mux->pat_changed) {
    /* program_association_section ()
     * for (i = 0; i < N; i++) {
     *    program_number                         16   uimsbf
     *    reserved                                3   bslbf
     *    network_PID_or_program_map_PID         13   uimbsf
     * }
     * CRC_32                                    32   rbchof
     */
    GList *cur;
    GPtrArray *pat;

    pat = gst_mpegts_pat_new ();

    for (cur = mux->programs; cur; cur = cur->next) {
      GstMpegtsPatProgram *pat_pgm;
      TsMuxProgram *program = (TsMuxProgram *) cur->data;

      pat_pgm = gst_mpegts_pat_program_new ();
      pat_pgm->program_number = program->pgm_number;
      pat_pgm->network_or_program_map_PID = program->pmt_pid;

      g_ptr_array_add (pat, pat_pgm);
    }

    if (mux->pat.section)
      gst_mpegts_section_unref (mux->pat.section);

    mux->pat.section = gst_mpegts_section_from_pat (pat, mux->transport_id);

    mux->pat.section->version_number = mux->pat_version++;

    TS_DEBUG ("PAT has %d programs", mux->nb_programs);
    mux->pat_changed = FALSE;
  }

  return tsmux_section_write_packet (GINT_TO_POINTER (GST_MPEGTS_SECTION_PAT),
      &mux->pat, mux);
}

static gboolean
tsmux_write_pmt (TsMux * mux, TsMuxProgram * program)
{

  if (program->pmt_changed) {
    /* program_association_section ()
     * reserved                                   3   bslbf
     * PCR_PID                                   13   uimsbf
     * reserved                                   4   bslbf
     * program_info_length                       12   uimsbf
     * for (i = 0; i < N; i++)
     *   descriptor ()
     *
     * for (i = 0; i < N1; i++) {
     *    stream_type                             8   uimsbf
     *    reserved                                3   bslbf
     *    elementary_PID                         13   uimbsf
     *    reserved                                4   bslbf
     *    ES_info_length                         12   uimbsf
     *    for (i = 0; i < N1; i++) {
     *      descriptor ();
     *    }
     * }
     */
    GstMpegtsDescriptor *descriptor;
    GstMpegtsPMT *pmt;
    guint8 desc[] = { 0x0F, 0xFF, 0xFC, 0xFC };
    guint i;

    pmt = gst_mpegts_pmt_new ();

    if (program->pcr_stream == NULL)
      pmt->pcr_pid = 0x1FFF;
    else
      pmt->pcr_pid = tsmux_stream_get_pid (program->pcr_stream);

    descriptor = gst_mpegts_descriptor_from_registration ("HDMV", NULL, 0);
    g_ptr_array_add (pmt->descriptors, descriptor);

    descriptor = gst_mpegts_descriptor_from_custom (0x88, desc, 4);
    g_ptr_array_add (pmt->descriptors, descriptor);

    /* Write out the entries */
    for (i = 0; i < program->streams->len; i++) {
      GstMpegtsPMTStream *pmt_stream;
      TsMuxStream *stream = g_array_index (program->streams, TsMuxStream *, i);

      pmt_stream = gst_mpegts_pmt_stream_new ();

      /* FIXME: Use API to retrieve this from the stream */
      pmt_stream->stream_type = stream->stream_type;
      pmt_stream->pid = tsmux_stream_get_pid (stream);

      /* Write any ES descriptors needed */
      tsmux_stream_get_es_descrs (stream, pmt_stream);
      g_ptr_array_add (pmt->streams, pmt_stream);
    }

    TS_DEBUG ("PMT for program %d has %d streams",
        program->pgm_number, program->streams->len);

    pmt->program_number = program->pgm_number;

    program->pmt.pi.pid = program->pmt_pid;
    program->pmt_changed = FALSE;

    if (program->pmt.section)
      gst_mpegts_section_unref (program->pmt.section);

    program->pmt.section = gst_mpegts_section_from_pmt (pmt, program->pmt_pid);
    program->pmt.section->version_number = program->pmt_version++;
  }

  return tsmux_section_write_packet (GINT_TO_POINTER (GST_MPEGTS_SECTION_PMT),
      &program->pmt, mux);
}
