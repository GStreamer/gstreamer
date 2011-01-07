/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2007> Jan Schmidt <thaytan@mad.scientist.com>
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
#ifndef __MPEGVIDEOPARSE_H__
#define __MPEGVIDEOPARSE_H__

#include <gst/gst.h>
#include "mpegpacketiser.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEGVIDEOPARSE \
  (mpegvideoparse_get_type())
#define GST_MPEGVIDEOPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEGVIDEOPARSE,MpegVideoParse))
#define GST_MPEGVIDEOPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEGVIDEOPARSE,MpegVideoParseClass))
#define GST_IS_MPEGVIDEOPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEGVIDEOPARSE))
#define GST_IS_MPEGVIDEOPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEGVIDEOPARSE))

typedef struct _MpegVideoParse MpegVideoParse;
typedef struct _MpegVideoParseClass MpegVideoParseClass;

struct _MpegVideoParse {
  GstElement element;

  GstPad *sinkpad, *srcpad;
  GstSegment segment;
  GList *pending_segs;

  gint64 next_offset;
  gboolean need_discont;

  /* Info from the Sequence Header */
  MPEGSeqHdr seq_hdr;
  GstBuffer *seq_hdr_buf;

  /* Packetise helper */
  MPEGPacketiser packer;

  /* gather/decode queues for reverse playback */
  GList *gather;
  GList *decode;
};

struct _MpegVideoParseClass {
  GstElementClass parent_class;
};

GType mpegvideoparse_get_type(void);

G_END_DECLS

#endif /* __MPEGVIDEOPARSE_H__ */
