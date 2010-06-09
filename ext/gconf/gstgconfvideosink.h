/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_GCONF_VIDEO_SINK_H__
#define __GST_GCONF_VIDEO_SINK_H__

#include <gst/gst.h>
#include <gconf/gconf-client.h>

#include "gstswitchsink.h"

G_BEGIN_DECLS

#define GST_TYPE_GCONF_VIDEO_SINK \
  (gst_gconf_video_sink_get_type ())
#define GST_GCONF_VIDEO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GCONF_VIDEO_SINK, \
                               GstGConfVideoSink))
#define GST_GCONF_VIDEO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GCONF_VIDEO_SINK, \
                            GstGConfVideoSinkClass))
#define GST_IS_GCONF_VIDEO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GCONF_VIDEO_SINK))
#define GST_IS_GCONF_VIDEO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GCONF_VIDEO_SINK))

typedef struct _GstGConfVideoSink {
  GstSwitchSink parent;

  /* explicit pointers to stuff used */
  GConfClient *client;

  /* gconf notify id */
  guint notify_id;

  /* Current gconf string */
  gchar *gconf_str;
} GstGConfVideoSink;

typedef struct _GstGConfVideoSinkClass {
  GstSwitchSinkClass parent_class;
} GstGConfVideoSinkClass;

GType   gst_gconf_video_sink_get_type   (void);

G_END_DECLS

#endif /* __GST_GCONF_VIDEO_SINK_H__ */
