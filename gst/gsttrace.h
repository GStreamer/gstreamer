/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsttrace.h: Header for tracing functions (depracated)
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

#ifndef GST_DISABLE_TRACE

#include <glib.h>

G_BEGIN_DECLS typedef struct _GstTrace GstTrace;
typedef struct _GstTraceEntry GstTraceEntry;

struct _GstTrace
{
  /* where this trace is going */
  gchar *filename;
  int fd;

  /* current buffer, size, head offset */
  GstTraceEntry *buf;
  gint bufsize;
  gint bufoffset;
};

struct _GstTraceEntry
{
  gint64 timestamp;
  guint32 sequence;
  guint32 data;
  gchar message[112];
};



GstTrace *gst_trace_new (gchar * filename, gint size);

void gst_trace_destroy (GstTrace * trace);
void gst_trace_flush (GstTrace * trace);
void gst_trace_text_flush (GstTrace * trace);

#define 	gst_trace_get_size(trace) 	((trace)->bufsize)
#define 	gst_trace_get_offset(trace) 	((trace)->bufoffset)
#define 	gst_trace_get_remaining(trace) 	((trace)->bufsize - (trace)->bufoffset)
void gst_trace_set_default (GstTrace * trace);

void _gst_trace_add_entry (GstTrace * trace, guint32 seq,
    guint32 data, gchar * msg);

void gst_trace_read_tsc (gint64 * dst);


typedef enum
{
  GST_ALLOC_TRACE_LIVE = (1 << 0),
  GST_ALLOC_TRACE_MEM_LIVE = (1 << 1)
}
GstAllocTraceFlags;

typedef struct _GstAllocTrace GstAllocTrace;

struct _GstAllocTrace
{
  gchar *name;
  gint flags;

  gint live;
  GSList *mem_live;
};

gboolean gst_alloc_trace_available (void);
G_CONST_RETURN GList *gst_alloc_trace_list (void);
GstAllocTrace *_gst_alloc_trace_register (const gchar * name);

int gst_alloc_trace_live_all (void);
void gst_alloc_trace_print_all (void);
void gst_alloc_trace_set_flags_all (GstAllocTraceFlags flags);

GstAllocTrace *gst_alloc_trace_get (const gchar * name);
void gst_alloc_trace_print (const GstAllocTrace * trace);
void gst_alloc_trace_set_flags (GstAllocTrace * trace,
    GstAllocTraceFlags flags);


#ifndef GST_DISABLE_ALLOC_TRACE
#define	gst_alloc_trace_register(name) _gst_alloc_trace_register (name);
#define	gst_alloc_trace_new(trace, mem) 		\
G_STMT_START {						\
  if ((trace)->flags & GST_ALLOC_TRACE_LIVE) 		\
    (trace)->live++;					\
  if ((trace)->flags & GST_ALLOC_TRACE_MEM_LIVE) 	\
    (trace)->mem_live = 				\
      g_slist_prepend ((trace)->mem_live, mem);		\
} G_STMT_END

#define	gst_alloc_trace_free(trace, mem) 		\
G_STMT_START {						\
  if ((trace)->flags & GST_ALLOC_TRACE_LIVE) 		\
    (trace)->live--;					\
  if ((trace)->flags & GST_ALLOC_TRACE_MEM_LIVE) 	\
    (trace)->mem_live = 				\
      g_slist_remove ((trace)->mem_live, mem); 		\
} G_STMT_END

#else
#define	gst_alloc_trace_register(name) (NULL)
#define	gst_alloc_trace_new(trace, mem)
#define	gst_alloc_trace_free(trace, mem)
#endif


#ifndef GST_DISABLE_TRACE
extern gint _gst_trace_on;

#define gst_trace_add_entry(trace,seq,data,msg) \
  if (_gst_trace_on) { \
    _gst_trace_add_entry(trace,(guint32)seq,(guint32)data,msg); \
  }
#else
#define gst_trace_add_entry(trace,seq,data,msg)
#endif

#else /* GST_DISABLE_TRACE */

#pragma GCC poison 	gst_trace_new
#pragma GCC poison	gst_trace_destroy
#pragma GCC poison 	gst_trace_flush
#pragma GCC poison	gst_trace_text_flush
#pragma GCC poison 	gst_trace_get_size
#pragma GCC poison 	gst_trace_get_offset
#pragma GCC poison 	gst_trace_get_remaining
#pragma GCC poison 	gst_trace_set_default
#pragma GCC poison 	_gst_trace_add_entry
#pragma GCC poison 	gst_trace_read_tsc
#pragma GCC poison 	gst_trace_add_entry

#define		gst_alloc_trace_register(name)
#define		gst_alloc_trace_new(trace, mem)
#define		gst_alloc_trace_free(trace, mem)

#define		gst_alloc_trace_available()	(FALSE)
#define		gst_alloc_trace_list()		(NULL)
#define		_gst_alloc_trace_register(name)	(NULL)

#define		gst_alloc_trace_print_all()
#define		gst_alloc_trace_set_flags_all(flags)

#define		gst_alloc_trace_get(name)	(NULL)
#define		gst_alloc_trace_print(trace)
#define		gst_alloc_trace_set_flags(trace,flags)


#endif /* GST_DISABLE_TRACE */

G_END_DECLS
#endif /* __GST_TRACE_H__ */
