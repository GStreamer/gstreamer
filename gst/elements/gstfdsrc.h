/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfdsrc.h: 
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


#ifndef __GST_FDSRC_H__
#define __GST_FDSRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_FDSRC \
  (gst_fdsrc_get_type())
#define GST_FDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FDSRC,GstFdSrc))
#define GST_FDSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FDSRC,GstFdSrcClass))
#define GST_IS_FDSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FDSRC))
#define GST_IS_FDSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FDSRC))
typedef struct _GstFdSrc GstFdSrc;
typedef struct _GstFdSrcClass GstFdSrcClass;

struct _GstFdSrc
{
  GstElement element;
  /* pads */
  GstPad *srcpad;

  /* fd */
  gint fd;

  gulong curoffset;		/* current offset in file */
  gulong blocksize;		/* bytes per read */
  guint64 timeout;		/* read timeout, in nanoseconds */

  gulong seq;			/* buffer sequence number */
};

struct _GstFdSrcClass
{
  GstElementClass parent_class;

  /* signals */
  void (*timeout) (GstElement * element);
};

GType gst_fdsrc_get_type (void);

G_END_DECLS
#endif /* __GST_FDSRC_H__ */
