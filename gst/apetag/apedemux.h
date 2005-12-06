/* GStreamer APEv1/2 tag reader
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_APE_DEMUX_H__
#define __GST_APE_DEMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_APE_DEMUX \
  (gst_ape_demux_get_type ())
#define GST_APE_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_APE_DEMUX, GstApeDemux))
#define GST_APE_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_APE_DEMUX, GstApeDemux))
#define GST_IS_APE_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_APE_DEMUX))
#define GST_IS_APE_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_APE_DEMUX))

typedef enum {
  GST_APE_DEMUX_TAGREAD,
  GST_APE_DEMUX_IDENTITY
} GstApeDemuxState;

typedef struct _GstApeDemux {
  GstElement parent;

  /* pads */
  GstPad *srcpad, *sinkpad;

  /* tag read state */
  GstApeDemuxState state;

  /* length of ape tag at start/end */
  guint64 start_off, end_off;
} GstApeDemux;

typedef struct _GstApeDemuxClass {
  GstElementClass parent_class;
} GstApeDemuxClass;

GType           gst_ape_demux_get_type          (void);

G_END_DECLS

#endif /* __GST_APE_DEMUX_H__ */
