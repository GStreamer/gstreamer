/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_TRACE_H__
#define __GST_TRACE_H__

void gst_trace_read_tsc(guint64 *dst);

typedef struct _GstTrace GstTrace;
typedef struct _GstTraceEntry GstTraceEntry;

struct _GstTrace {
  /* where this trace is going */
  gchar *filename;
  int fd;

  /* current buffer, size, head offset */
  GstTraceEntry *buf;
  gint bufsize;
  gint bufoffset;
};

struct _GstTraceEntry {
  guint64 timestamp;
  guint32 sequence;
  guint32 data;
  gchar message[112];
};

GstTrace *gst_trace_new(guchar *filename,gint size);
void gst_trace_destroy(GstTrace *trace);
void gst_trace_flush(GstTrace *trace);
#define gst_trace_get_size(trace) ((trace)->bufsize)
#define gst_trace_get_offset(trace) ((trace)->bufoffset)
#define gst_trace_get_remaining(trace) ((trace)->bufsize - (trace)->bufoffset)
void gst_trace_set_default(GstTrace *trace);

void _gst_trace_add_entry(GstTrace *trace,guint32 seq,guint32 data,gchar *msg);

#define TRACE_ENABLE

#ifdef TRACE_ENABLE
extern gint _gst_trace_on;
#define gst_trace_add_entry(trace,seq,data,msg) \
  if (_gst_trace_on) { \
    _gst_trace_add_entry(trace,(guint32)seq,(guint32)data,msg); \
  }
#else
#define gst_trace_add_entry(trace,seq,data,msg)
#endif

#endif /* __GST_TRACE_H__ */
