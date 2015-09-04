/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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

/**
 * SECTION:gesvideosource
 * @short_description: Base Class for video sources
 *
 * <refsect1 id="GESVideoSource.children_properties" role="properties">
 * <title role="children_properties.title">Children Properties</title>
 * <para>You can use the following children properties through the
 * #ges_track_element_set_child_property and alike set of methods:</para>
 * <informaltable frame="none">
 * <tgroup cols="3">
 * <colspec colname="properties_type" colwidth="150px"/>
 * <colspec colname="properties_name" colwidth="200px"/>
 * <colspec colname="properties_flags" colwidth="400px"/>
 * <tbody>
 * <row>
 *  <entry role="property_type"><link linkend="gdouble"><type>double</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--alpha">alpha</link></entry>
 *  <entry>The desired alpha for the stream.</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="gint"><type>gint</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--posx">posx</link></entry>
 *  <entry>The desired x position for the stream.</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="gint"><type>gint</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--posy">posy</link></entry>
 *  <entry>The desired y position for the stream</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="guint"><type>guint</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--zorder">zorder</link></entry>
 *  <entry>The desired z order for the stream</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="gint"><type>gint</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--width">width</link></entry>
 *  <entry>The desired width for that source. Set to 0 if size is not mandatory, will be set to width of the current track.</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="gint"><type>gint</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--height">height</link></entry>
 *  <entry>The desired height for that source. Set to 0 if size is not mandatory, will be set to height of the current track.</entry>
 * </row>
 * </tbody>
 * </tgroup>
 * </informaltable>
 * </refsect1>
 */

#include <gst/pbutils/missing-plugins.h>

#include "ges-internal.h"
#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-video-source.h"
#include "ges-layer.h"
#include "gstframepositionner.h"

#define parent_class ges_video_source_parent_class
G_DEFINE_ABSTRACT_TYPE (GESVideoSource, ges_video_source, GES_TYPE_SOURCE);

struct _GESVideoSourcePrivate
{
  GstFramePositionner *positionner;
  GstElement *capsfilter;
};

/* TrackElement VMethods */

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  gboolean res;
  GESVideoSource *self = GES_VIDEO_SOURCE (element);

  res = GES_TIMELINE_ELEMENT_CLASS (parent_class)->set_priority (element,
      priority);

  if (res && self->priv->positionner)
    g_object_set (self->priv->positionner, "zorder",
        G_MAXUINT - priority, NULL);

  return res;
}

static void
post_missing_element_message (GstElement * element, const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (element, name);
  gst_element_post_message (element, msg);
}

static GstElement *
ges_video_source_create_element (GESTrackElement * trksrc)
{
  GstElement *topbin;
  GstElement *sub_element;
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GESVideoSourceClass *source_class = GES_VIDEO_SOURCE_GET_CLASS (trksrc);
  GESVideoSource *self;
  GstElement *positionner, *videoscale, *videorate, *capsfilter, *videoconvert,
      *deinterlace;
  const gchar *props[] = { "alpha", "posx", "posy", "width", "height", NULL };
  GESTimelineElement *parent;

  if (!source_class->create_source)
    return NULL;

  sub_element = source_class->create_source (trksrc);

  self = (GESVideoSource *) trksrc;

  /* That positionner will add metadata to buffers according to its
     properties, acting like a proxy for our smart-mixer dynamic pads. */
  positionner = gst_element_factory_make ("framepositionner", "frame_tagger");
  g_object_set (positionner, "zorder",
      G_MAXUINT - GES_TIMELINE_ELEMENT_PRIORITY (self), NULL);

  videoscale =
      gst_element_factory_make ("videoscale", "track-element-videoscale");
  videoconvert =
      gst_element_factory_make ("videoconvert", "track-element-videoconvert");
  videorate = gst_element_factory_make ("videorate", "track-element-videorate");
  deinterlace = gst_element_factory_make ("deinterlace", "deinterlace");
  if (deinterlace == NULL) {
    deinterlace = gst_element_factory_make ("avdeinterlace", "deinterlace");
  }
  capsfilter =
      gst_element_factory_make ("capsfilter", "track-element-capsfilter");

  ges_frame_positionner_set_source_and_filter (GST_FRAME_POSITIONNER
      (positionner), trksrc, capsfilter);

  ges_track_element_add_children_props (trksrc, positionner, NULL, NULL, props);

  if (deinterlace == NULL) {
    post_missing_element_message (sub_element, "deinterlace");

    GST_ELEMENT_WARNING (sub_element, CORE, MISSING_PLUGIN,
        ("Missing element '%s' - check your GStreamer installation.",
            "deinterlace"), ("deinterlacing won't work"));
    topbin =
        ges_source_create_topbin ("videosrcbin", sub_element, queue,
        videoconvert, positionner, videoscale, videorate, capsfilter, NULL);
  } else {
    topbin =
        ges_source_create_topbin ("videosrcbin", sub_element, queue,
        videoconvert, deinterlace, positionner, videoscale, videorate,
        capsfilter, NULL);
  }

  parent = ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (trksrc));
  if (parent) {
    self->priv->positionner = GST_FRAME_POSITIONNER (positionner);
    gst_object_unref (parent);
  } else {
    GST_ERROR ("No parent timeline element, SHOULD NOT HAPPEN");
  }

  self->priv->capsfilter = capsfilter;

  return topbin;
}

static void
ges_video_source_class_init (GESVideoSourceClass * klass)
{
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);
  GESVideoSourceClass *video_source_class = GES_VIDEO_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESVideoSourcePrivate));

  element_class->set_priority = _set_priority;

  track_class->nleobject_factorytype = "nlesource";
  track_class->create_element = ges_video_source_create_element;
  video_source_class->create_source = NULL;
}

static void
ges_video_source_init (GESVideoSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_VIDEO_SOURCE, GESVideoSourcePrivate);
  self->priv->positionner = NULL;
  self->priv->capsfilter = NULL;
}
