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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "gst_private.h"
#include "gstinfo.h"

#include "gsttrace.h"

static
#ifdef __inline__
  __inline__
#endif
    void
read_tsc (gint64 * dst)
{
#ifdef HAVE_RDTSC
  guint64 tsc;
  __asm__ __volatile__ ("rdtsc":"=A" (tsc));

  *dst = tsc;
#else
  *dst = 0;
#endif
}

void
gst_trace_read_tsc (gint64 * dst)
{
  read_tsc (dst);
}

GstTrace *_gst_trace_default = NULL;
gint _gst_trace_on = 1;

GstTrace *
gst_trace_new (gchar * filename, gint size)
{
  GstTrace *trace = g_malloc (sizeof (GstTrace));

  g_return_val_if_fail (trace != NULL, NULL);
  trace->filename = g_strdup (filename);
  GST_DEBUG ("opening '%s'\n", trace->filename);
#ifndef S_IWUSR
#define S_IWUSR S_IWRITE
#endif
#ifndef S_IRUSR
#define S_IRUSR S_IREAD
#endif
  trace->fd =
      open (trace->filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  perror ("opening trace file");
  g_return_val_if_fail (trace->fd > 0, NULL);
  trace->buf = g_malloc (size * sizeof (GstTraceEntry));
  g_return_val_if_fail (trace->buf != NULL, NULL);
  trace->bufsize = size;
  trace->bufoffset = 0;

  return trace;
}

void
gst_trace_destroy (GstTrace * trace)
{
  g_return_if_fail (trace != NULL);
  g_return_if_fail (trace->buf != NULL);

  if (gst_trace_get_remaining (trace) > 0)
    gst_trace_flush (trace);
  close (trace->fd);
  g_free (trace->buf);
  g_free (trace);
}

void
gst_trace_flush (GstTrace * trace)
{
  if (!trace) {
    trace = _gst_trace_default;
    if (!trace)
      return;
  }

  write (trace->fd, trace->buf, trace->bufoffset * sizeof (GstTraceEntry));
  trace->bufoffset = 0;
}

void
gst_trace_text_flush (GstTrace * trace)
{
  int i;

#define STRSIZE (20 + 1 + 10 + 1 + 10 + 1 + 112 + 1 + 1)
  char str[STRSIZE];

  if (!trace) {
    trace = _gst_trace_default;
    if (!trace)
      return;
  }

  for (i = 0; i < trace->bufoffset; i++) {
    g_snprintf (str, STRSIZE, "%20" G_GINT64_FORMAT " %10d %10d %s\n",
        trace->buf[i].timestamp,
        trace->buf[i].sequence, trace->buf[i].data, trace->buf[i].message);
    write (trace->fd, str, strlen (str));
  }
  trace->bufoffset = 0;
#undef STRSIZE
}

void
gst_trace_set_default (GstTrace * trace)
{
  g_return_if_fail (trace != NULL);
  _gst_trace_default = trace;
}

void
_gst_trace_add_entry (GstTrace * trace, guint32 seq, guint32 data, gchar * msg)
{
  GstTraceEntry *entry;

  if (!trace) {
    trace = _gst_trace_default;
    if (!trace)
      return;
  }

  entry = trace->buf + trace->bufoffset;
  read_tsc (&(entry->timestamp));
  entry->sequence = seq;
  entry->data = data;
  strncpy (entry->message, msg, 112);
  trace->bufoffset++;

  gst_trace_flush (trace);
}


/* global flags */
static GstAllocTraceFlags _gst_trace_flags = 0;

/* list of registered tracers */
static GList *_gst_alloc_tracers = NULL;

/**
 * gst_alloc_trace_available:
 *
 * Check if alloc tracing was commiled into the core
 *
 * Returns: TRUE if the core was compiled with alloc
 * tracing enabled.
 */
gboolean
gst_alloc_trace_available (void)
{
#ifdef GST_DISABLE_ALLOC_TRACE
  return FALSE;
#else
  return TRUE;
#endif
}

/**
 * _gst_alloc_trace_register:
 * @name: the name of the new alloc trace object.
 *
 * Register an get a handle to a GstAllocTrace object that
 * can be used to trace memory allocations.
 *
 * Returns: A handle to a GstAllocTrace.
 */
GstAllocTrace *
_gst_alloc_trace_register (const gchar * name)
{
  GstAllocTrace *trace;

  g_return_val_if_fail (name, NULL);

  trace = g_new0 (GstAllocTrace, 1);
  trace->name = g_strdup (name);
  trace->live = 0;
  trace->mem_live = NULL;
  trace->flags = _gst_trace_flags;

  _gst_alloc_tracers = g_list_prepend (_gst_alloc_tracers, trace);

  return trace;
}

/**
 * gst_alloc_trace_list:
 *
 * Get a list of all registered alloc trace objects.
 *
 * Returns: a GList of GstAllocTrace objects.
 */
const GList *
gst_alloc_trace_list (void)
{
  return _gst_alloc_tracers;
}

/**
 * gst_alloc_trace_live_all:
 *
 * Returns the total number of live registered alloc trace objects.
 */
int
gst_alloc_trace_live_all (void)
{
  GList *walk = _gst_alloc_tracers;
  int num = 0;

  while (walk) {
    GstAllocTrace *trace = (GstAllocTrace *) walk->data;

    num += trace->live;

    walk = g_list_next (walk);
  }

  return num;
}

/**
 * gst_alloc_trace_print_all:
 *
 * Print the status of all registered alloc trace objectes.
 */
void
gst_alloc_trace_print_all (void)
{
  GList *walk = _gst_alloc_tracers;

  while (walk) {
    GstAllocTrace *trace = (GstAllocTrace *) walk->data;

    gst_alloc_trace_print (trace);

    walk = g_list_next (walk);
  }
}

/**
 * gst_alloc_trace_set_flags_all:
 * @flags: the options to enable
 *
 * Enable the specified options on all registered alloc trace
 * objects.
 */
void
gst_alloc_trace_set_flags_all (GstAllocTraceFlags flags)
{
  GList *walk = _gst_alloc_tracers;

  while (walk) {
    GstAllocTrace *trace = (GstAllocTrace *) walk->data;

    GST_DEBUG ("set flags on %p\n", trace);
    gst_alloc_trace_set_flags (trace, flags);

    walk = g_list_next (walk);
  }
  _gst_trace_flags = flags;
}

/**
 * gst_alloc_trace_get:
 * @name: the name of the alloc trace object
 *
 * Get the named alloc trace object.
 *
 * Returns: a GstAllocTrace with the given name or NULL when
 * no alloc tracer was registered with that name.
 */
GstAllocTrace *
gst_alloc_trace_get (const gchar * name)
{
  GList *walk = _gst_alloc_tracers;

  g_return_val_if_fail (name, NULL);

  while (walk) {
    GstAllocTrace *trace = (GstAllocTrace *) walk->data;

    if (!strcmp (trace->name, name))
      return trace;

    walk = g_list_next (walk);
  }
  return NULL;
}

/**
 * gst_alloc_trace_print:
 * @trace: the GstAllocTrace to print
 *
 * Print the status of the given GstAllocTrace.
 */
void
gst_alloc_trace_print (const GstAllocTrace * trace)
{
  GSList *mem_live;

  g_return_if_fail (trace != NULL);

  g_print ("%s (%p): flags %d", trace->name, trace, trace->flags);

  if (trace->flags & GST_ALLOC_TRACE_LIVE) {
    g_print (", live %d", trace->live);
  }
  if (trace->flags & GST_ALLOC_TRACE_MEM_LIVE) {
    mem_live = trace->mem_live;

    if (!mem_live) {
      g_print (", no live memory");
    } else {
      g_print (", dumping live memory: ");

      while (mem_live) {
        g_print ("%p ", mem_live->data);
        mem_live = g_slist_next (mem_live);
      }
      g_print ("\ntotal %d", g_slist_length (trace->mem_live));
    }
  }
  g_print ("\n");
}

/**
 * gst_alloc_trace_set_flags:
 * @trace: the GstAllocTrace 
 * @flags: flags to set 
 *
 * Enable the given features on the given GstAllocTrace object.
 */
void
gst_alloc_trace_set_flags (GstAllocTrace * trace, GstAllocTraceFlags flags)
{
  g_return_if_fail (trace != NULL);

  trace->flags = flags;
}
