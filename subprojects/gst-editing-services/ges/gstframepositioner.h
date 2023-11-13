/* GStreamer
 * Copyright (C) 2013 Mathieu Duponchelle <mduponchelle1@gmail.com>
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

#ifndef _GST_FRAME_POSITIONNER_H_
#define _GST_FRAME_POSITIONNER_H_

#include <gst/base/gstbasetransform.h>
#include <ges/ges-track-element.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GST_TYPE_FRAME_POSITIONNER   (gst_frame_positioner_get_type())
#define GST_FRAME_POSITIONNER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FRAME_POSITIONNER,GstFramePositioner))
#define GST_FRAME_POSITIONNER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FRAME_POSITIONNER,GstFramePositionerClass))
#define GST_IS_FRAME_POSITIONNER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FRAME_POSITIONNER))
#define GST_IS_FRAME_POSITIONNER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FRAME_POSITIONNER))

typedef struct _GstFramePositioner GstFramePositioner;
typedef struct _GstFramePositionerClass GstFramePositionerClass;

struct _GstFramePositioner
{
  GstBaseTransform base_framepositioner;

  GstElement *capsfilter;

  GESTrackElement *track_source;
  GESTrack *current_track;

  gboolean scale_in_compositor;
  gdouble alpha;
  gdouble posx;
  gdouble posy;
  guint zorder;
  gdouble width;
  gdouble height;
  gint operator;
  gint natural_width;
  gint natural_height;
  gint track_width;
  gint track_height;
  gint fps_n;
  gint fps_d;

  gint par_n;
  gint par_d;

  gboolean user_positioned;

  /*  This should never be made public, no padding needed */
};

struct _GstFramePositionerClass
{
  GstBaseTransformClass base_framepositioner_class;
};

G_GNUC_INTERNAL GType gst_compositor_operator_get_type_and_default_value (int *default_operator_value);
G_GNUC_INTERNAL void ges_frame_positioner_set_source_and_filter (GstFramePositioner *pos,
						  GESTrackElement *trksrc,
						  GstElement *capsfilter);
G_GNUC_INTERNAL GType gst_frame_positioner_get_type (void);

G_END_DECLS

#endif
