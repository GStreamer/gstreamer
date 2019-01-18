/* GStreamer GES plugin
 *
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gesdemux.h
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef __GES_DEMUX_H__
#define __GES_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <ges/ges.h>

G_BEGIN_DECLS

GType ges_demux_get_type (void);

#define GES_DEMUX_TYPE (ges_demux_get_type ())
#define GES_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_DEMUX_TYPE, GESDemux))
#define GES_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GES_DEMUX_TYPE, GESDemuxClass))
#define GES_IS_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_DEMUX_TYPE))
#define GES_IS_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_DEMUX_TYPE))
#define GES_DEMUX_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_DEMUX_TYPE, GESDemuxClass))

typedef struct {
  GstBin parent;

  GESTimeline *timeline;
  GstPad *sinkpad;

  GstAdapter *input_adapter;
} GESDemux;

typedef struct {
  GstBinClass parent;

} GESDemuxClass;

G_END_DECLS
#endif /* __GES_DEMUX_H__ */

