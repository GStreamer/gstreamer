/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpipefilter.h: 
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


#ifndef __GST_PIPEFILTER_H__
#define __GST_PIPEFILTER_H__

#include <sys/types.h>
#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_PIPEFILTER \
  (gst_pipefilter_get_type())
#define GST_PIPEFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PIPEFILTER,GstPipefilter))
#define GST_PIPEFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PIPEFILTER,GstPipefilterClass))
#define GST_IS_PIPEFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PIPEFILTER))
#define GST_IS_PIPEFILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PIPEFILTER))
    typedef enum
{
  GST_PIPEFILTER_OPEN = GST_ELEMENT_FLAG_LAST,

  GST_PIPEFILTER_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2
}
GstPipeFilterFlags;

typedef struct _GstPipefilter GstPipefilter;
typedef struct _GstPipefilterClass GstPipefilterClass;

struct _GstPipefilter
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* command */
  gchar **command;
  gchar *orig_command;
  /* fd */
  gint fdout[2];
  gint fdin[2];
  pid_t childpid;

  gulong curoffset;		/* current offset in file */
  gulong bytes_per_read;	/* bytes per read */

  gulong seq;			/* buffer sequence number */
};

struct _GstPipefilterClass
{
  GstElementClass parent_class;
};

GType gst_pipefilter_get_type (void);

G_END_DECLS
#endif /* __GST_PIPEFILTER_H__ */
