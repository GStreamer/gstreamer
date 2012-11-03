/* GStreamer
 * Copyright (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (c) 2007 Jan Schmidt <thaytan@mad.scientist.com>
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
 */

#ifndef __GST_SWITCH_SINK_H__
#define __GST_SWITCH_SINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SWITCH_SINK \
  (gst_switch_sink_get_type ())
#define GST_SWITCH_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SWITCH_SINK, \
                               GstSwitchSink))
#define GST_SWITCH_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SWITCH_SINK, \
                            GstSwitchSinkClass))
#define GST_IS_SWITCH_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SWITCH_SINK))
#define GST_IS_SWITCH_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SWITCH_SINK))

typedef struct _GstSwitchSink {
  GstBin parent;

  GstElement *kid;
  GstElement *new_kid;
  GstPad *pad;

  /* If a custom child has been set... */
  gboolean have_kid;
} GstSwitchSink;

typedef struct _GstSwitchSinkClass {
  GstBinClass parent_class;
} GstSwitchSinkClass;

GType   gst_switch_sink_get_type   (void);

gboolean gst_switch_sink_set_child (GstSwitchSink *ssink, GstElement *new_kid);

G_END_DECLS

#endif /* __GST_SWITCH_SINK_H__ */
