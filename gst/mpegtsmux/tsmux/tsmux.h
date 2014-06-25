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

#ifndef __TSMUX_H__
#define __TSMUX_H__

#include <glib.h>

#include <gst/mpegts/mpegts.h>

#include "tsmuxcommon.h"
#include "tsmuxstream.h"

G_BEGIN_DECLS

#define TSMUX_MAX_ES_INFO_LENGTH ((1 << 12) - 1)

#define TSMUX_PID_AUTO ((guint16)-1)

#define TSMUX_START_PROGRAM_ID 0x0001
#define TSMUX_START_PMT_PID 0x0020
#define TSMUX_START_ES_PID 0x0040

typedef struct TsMuxSection TsMuxSection;
typedef struct TsMux TsMux;

typedef gboolean (*TsMuxWriteFunc) (GstBuffer * buf, void *user_data, gint64 new_pcr);
typedef void (*TsMuxAllocFunc) (GstBuffer ** buf, void *user_data);

struct TsMuxSection {
  TsMuxPacketInfo pi;
  GstMpegtsSection *section;
};

/* Information for the streams associated with one program */
struct TsMuxProgram {
  TsMuxSection pmt;
  /* PMT version */
  guint8   pmt_version;
  /* trigger for writing PMT */
  gboolean pmt_changed;

  /* interval between PMT in MPEG PTS clock time */
  guint    pmt_interval;
  /* last time PMT written in MPEG PTS clock time */
  gint64   last_pmt_ts;

  /* program ID for the PAT */
  guint16 pgm_number;
  /* PID to write the PMT */
  guint16 pmt_pid;

  /* stream which carries the PCR */
  TsMuxStream *pcr_stream;

  /* programs TsMuxStream's */
  GArray *streams;
};

struct TsMux {
  /* TsMuxStream* array of all streams */
  guint nb_streams;
  GList *streams;

  /* TsMuxProgram* array of all programs */
  guint nb_programs;
  GList *programs;

  /* next auto-generated misc id */
  guint16 next_pgm_no;
  guint16 next_pmt_pid;
  guint16 next_stream_pid;

  /* Table with TsMuxSection to write */
  GHashTable *si_sections;

  TsMuxSection pat;
  /* PAT transport_stream_id */
  guint16 transport_id;
  /* PAT version */
  guint8   pat_version;
  /* trigger writing PAT */
  gboolean pat_changed;
  /* interval between PAT in MPEG PTS clock time */
  guint    pat_interval;
  /* last time PAT written in MPEG PTS clock time */
  gint64   last_pat_ts;

  /* trigger writing Service Information Tables */
  gboolean si_changed;
  /* interval between SIT in MPEG PTS clock time */
  guint    si_interval;
  /* last time SIT written in MPEG PTS clock time */
  gint64   last_si_ts;

  /* callback to write finished packet */
  TsMuxWriteFunc write_func;
  void *write_func_data;
  /* callback to alloc new packet buffer */
  TsMuxAllocFunc alloc_func;
  void *alloc_func_data;

  /* scratch space for writing ES_info descriptors */
  guint8 es_info_buf[TSMUX_MAX_ES_INFO_LENGTH];
};

/* create/free new muxer session */
TsMux *		tsmux_new 			(void);
void 		tsmux_free 			(TsMux *mux);

/* Setting muxing session properties */
void 		tsmux_set_write_func 		(TsMux *mux, TsMuxWriteFunc func, void *user_data);
void 		tsmux_set_alloc_func 		(TsMux *mux, TsMuxAllocFunc func, void *user_data);
void 		tsmux_set_pat_interval          (TsMux *mux, guint interval);
guint 		tsmux_get_pat_interval          (TsMux *mux);
guint16		tsmux_get_new_pid 		(TsMux *mux);

/* pid/program management */
TsMuxProgram *	tsmux_program_new 		(TsMux *mux, gint prog_id);
void 		tsmux_program_free 		(TsMuxProgram *program);
void 		tsmux_set_pmt_interval          (TsMuxProgram *program, guint interval);
guint 		tsmux_get_pmt_interval   	(TsMuxProgram *program);

/* SI table management */
void            tsmux_set_si_interval           (TsMux *mux, guint interval);
guint           tsmux_get_si_interval           (TsMux *mux);
gboolean        tsmux_add_mpegts_si_section     (TsMux * mux, GstMpegtsSection * section);

/* stream management */
TsMuxStream *	tsmux_create_stream 		(TsMux *mux, TsMuxStreamType stream_type, guint16 pid, gchar *language);
TsMuxStream *	tsmux_find_stream 		(TsMux *mux, guint16 pid);

void 		tsmux_program_add_stream 	(TsMuxProgram *program, TsMuxStream *stream);
void 		tsmux_program_set_pcr_stream 	(TsMuxProgram *program, TsMuxStream *stream);

/* writing stuff */
gboolean 	tsmux_write_stream_packet 	(TsMux *mux, TsMuxStream *stream);

G_END_DECLS

#endif
