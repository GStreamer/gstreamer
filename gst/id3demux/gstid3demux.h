/* Copyright 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2003-2004 Benjamin Otte <otte@gnome.org>
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

#ifndef __GST_ID3DEMUX_H__
#define __GST_ID3DEMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ID3DEMUX \
  (gst_gst_id3demux_get_type())
#define GST_ID3DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ID3DEMUX,GstID3Demux))
#define GST_ID3DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ID3DEMUX,GstID3Demux))
#define GST_IS_ID3DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ID3DEMUX))
#define GST_IS_ID3DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ID3DEMUX))

typedef struct _GstID3Demux      GstID3Demux;
typedef struct _GstID3DemuxClass GstID3DemuxClass;

typedef enum {
  GST_ID3DEMUX_READID3V2,
  GST_ID3DEMUX_TYPEFINDING,
  GST_ID3DEMUX_STREAMING
} GstID3DemuxState;

struct _GstID3Demux
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* Number of bytes to remove from the start of file (ID3v2) */
  guint strip_start;
  /* Number of bytes to remove from the end of file (ID3v1) */
  guint strip_end;
  
  gint64 upstream_size;

  GstID3DemuxState state;
  GstBuffer *collect;
  GstCaps *src_caps;
  
  gboolean prefer_v1;
  GstTagList *event_tags;
  GstTagList *parsed_tags;
  gboolean send_tag_event;
};

struct _GstID3DemuxClass 
{
  GstElementClass parent_class;
};

GType gst_gst_id3demux_get_type (void);

G_END_DECLS

#endif /* __GST_ID3DEMUX_H__ */
