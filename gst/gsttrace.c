/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsttrace.c: Tracing functions (depracated)
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


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "gst_private.h"

#include "gsttrace.h"


__inline__ void read_tsc(guint64 *dst) {
#ifdef HAVE_RDTSC
  guint64 tsc;
  __asm__ __volatile__ ("rdtsc" : "=A" (tsc));
  *dst = tsc;
#else
  *dst = 0;
#endif
}

void gst_trace_read_tsc(guint64 *dst) {
  read_tsc(dst);
}

GstTrace *_gst_trace_default = NULL;
gint _gst_trace_on = 1;

GstTrace *gst_trace_new(guchar *filename,gint size) {
  GstTrace *trace = g_malloc(sizeof(GstTrace));

  g_return_val_if_fail(trace != NULL,NULL);
  trace->filename = g_strdup(filename);
  g_print("opening '%s'\n",trace->filename);
  trace->fd = open(trace->filename,O_RDWR|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
  perror("opening trace file");
  g_return_val_if_fail(trace->fd > 0,NULL);
  trace->buf = g_malloc(size * sizeof(GstTraceEntry));
  g_return_val_if_fail(trace->buf != NULL,NULL);
  trace->bufsize = size;
  trace->bufoffset = 0;

  return trace;
}

void gst_trace_destroy(GstTrace *trace) {
  g_return_if_fail(trace != NULL);
  g_return_if_fail(trace->buf != NULL);

  if (gst_trace_get_remaining(trace) > 0)
    gst_trace_flush(trace);
  close(trace->fd);
  g_free(trace->buf);
  g_free(trace);
}

void gst_trace_flush(GstTrace *trace) {
  if (!trace) {
    trace = _gst_trace_default;
    if (!trace ) return;
  }

  write(trace->fd,trace->buf,trace->bufoffset * sizeof(GstTraceEntry));
  trace->bufoffset = 0;
}

void gst_trace_text_flush(GstTrace *trace) {
  int i;
  const int strsize = 20+1 + 10+1 + 10+1 + 112+1 + 1;
  char str[strsize];

  if (!trace) {
    trace = _gst_trace_default;
    if (!trace ) return;
  }

  for (i=0; i<trace->bufoffset; i++) {
    snprintf(str, strsize, "%20lld %10d %10d %s\n",
        trace->buf[i].timestamp,
        trace->buf[i].sequence,
        trace->buf[i].data,
        trace->buf[i].message);
    write(trace->fd,str,strlen(str));
  }
  trace->bufoffset = 0;
}

void gst_trace_set_default(GstTrace *trace) {
  g_return_if_fail(trace != NULL);
  _gst_trace_default = trace;
}

void _gst_trace_add_entry(GstTrace *trace,guint32 seq,guint32 data,gchar *msg) {
  GstTraceEntry *entry;
  if (!trace) {
    trace = _gst_trace_default;
    if (!trace ) return;
  }

  entry = trace->buf + trace->bufoffset;
  read_tsc(&(entry->timestamp));
  entry->sequence = seq;
  entry->data = data;
  strncpy(entry->message,msg,112);
  trace->bufoffset++;

  gst_trace_flush(trace);
}
