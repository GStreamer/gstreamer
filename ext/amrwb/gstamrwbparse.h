/* GStreamer Adaptive Multi-Rate Wide-Band (AMR-WB) plugin
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
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

#ifndef __GST_AMRWBPARSE_H__
#define __GST_AMRWBPARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <amrwb/typedef.h>
#include <amrwb/dec_if.h>
#include <amrwb/if_rom.h>

G_BEGIN_DECLS

#define GST_TYPE_AMRWBPARSE			\
  (gst_amrwbparse_get_type())
#define GST_AMRWBPARSE(obj)						\
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMRWBPARSE, GstAmrwbParse))
#define GST_AMRWBPARSE_CLASS(klass)					\
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMRWBPARSE, GstAmrwbParseClass))
#define GST_IS_AMRWBPARSE(obj)					\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMRWBPARSE))
#define GST_IS_AMRWBPARSE_CLASS(klass)				\
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMRWBPARSE))

typedef struct _GstAmrwbParse GstAmrwbParse;
typedef struct _GstAmrwbParseClass GstAmrwbParseClass;

typedef gboolean (*GstAmrwbSeekHandler) (GstAmrwbParse * amrwbparse, GstPad * pad,
		    GstEvent * event);

struct _GstAmrwbParse {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  GstAdapter *adapter;

  gboolean seekable;
  gboolean need_header;
  gint64 offset;
  gint block;
  
  GstAmrwbSeekHandler seek_handler;

  guint64 ts;

  /* for seeking etc */
  GstSegment segment;
};

struct _GstAmrwbParseClass {
  GstElementClass parent_class;
};

GType gst_amrwbparse_get_type (void);

G_END_DECLS

#endif /* __GST_AMRWBPARSE_H__ */
