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
 * <row>
 *  <entry role="property_type"><link linkend="GstDeinterlaceModes"><type>GstDeinterlaceModes</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--deinterlace-mode">deinterlace-mode</link></entry>
 *  <entry>Deinterlace Mode</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="GstDeinterlaceFields"><type>GstDeinterlaceFields</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--deinterlace-fields">deinterlace-fields</link></entry>
 *  <entry>Fields to use for deinterlacing</entry>
 * </row>
 * <row>
 *  <entry role="property_type"><link linkend="GstDeinterlaceFieldLayout"><type>GstDeinterlaceFieldLayout</type></link></entry>
 *  <entry role="property_name"><link linkend="GESVideoSource--deinterlace-tff">deinterlace-tff</link></entry>
 *  <entry>Deinterlace top field first</entry>
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
#include "gstframepositioner.h"

#define parent_class ges_video_source_parent_class
G_DEFINE_ABSTRACT_TYPE (GESVideoSource, ges_video_source, GES_TYPE_SOURCE);

struct _GESVideoSourcePrivate
{
  GstFramePositioner *positioner;
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

  if (res && self->priv->positioner)
    g_object_set (self->priv->positioner, "zorder", G_MAXUINT - priority, NULL);

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
  GstElement *positioner, *videoscale, *videorate, *capsfilter, *videoconvert,
      *deinterlace;
  const gchar *positioner_props[] =
      { "alpha", "posx", "posy", "width", "height", NULL };
  const gchar *deinterlace_props[] = { "mode", "fields", "tff", NULL };

  if (!source_class->create_source)
    return NULL;

  sub_element = source_class->create_source (trksrc);

  self = (GESVideoSource *) trksrc;

  /* That positioner will add metadata to buffers according to its
     properties, acting like a proxy for our smart-mixer dynamic pads. */
  positioner = gst_element_factory_make ("framepositioner", "frame_tagger");
  g_object_set (positioner, "zorder",
      G_MAXUINT - GES_TIMELINE_ELEMENT_PRIORITY (self), NULL);

  videoscale =
      gst_element_factory_make ("videoscale", "track-element-videoscale");
  videoconvert =
      gst_element_factory_make ("videoconvert", "track-element-videoconvert");
  videorate = gst_element_factory_make ("videorate", "track-element-videorate");
  deinterlace = gst_element_factory_make ("deinterlace", "deinterlace");
  capsfilter =
      gst_element_factory_make ("capsfilter", "track-element-capsfilter");

  ges_frame_positioner_set_source_and_filter (GST_FRAME_POSITIONNER
      (positioner), trksrc, capsfilter);

  ges_track_element_add_children_props (trksrc, positioner, NULL, NULL,
      positioner_props);

  if (deinterlace == NULL) {
    post_missing_element_message (sub_element, "deinterlace");

    GST_ELEMENT_WARNING (sub_element, CORE, MISSING_PLUGIN,
        ("Missing element '%s' - check your GStreamer installation.",
            "deinterlace"), ("deinterlacing won't work"));
    topbin =
        ges_source_create_topbin ("videosrcbin", sub_element, queue,
        videoconvert, positioner, videoscale, videorate, capsfilter, NULL);
  } else {
    ges_track_element_add_children_props (trksrc, deinterlace, NULL, NULL,
        deinterlace_props);
    topbin =
        ges_source_create_topbin ("videosrcbin", sub_element, queue,
        videoconvert, deinterlace, positioner, videoscale, videorate,
        capsfilter, NULL);
  }

  self->priv->positioner = GST_FRAME_POSITIONNER (positioner);
  self->priv->positioner->scale_in_compositor =
      !GES_VIDEO_SOURCE_GET_CLASS (self)->ABI.abi.disable_scale_in_compositor;
  self->priv->capsfilter = capsfilter;

  return topbin;
}

static gboolean
_lookup_child (GESTimelineElement * object,
    const gchar * prop_name, GObject ** element, GParamSpec ** pspec)
{
  gboolean res;

  gchar *clean_name;

  if (!g_strcmp0 (prop_name, "deinterlace-fields"))
    clean_name = g_strdup ("GstDeinterlace::fields");
  else if (!g_strcmp0 (prop_name, "deinterlace-mode"))
    clean_name = g_strdup ("GstDeinterlace::mode");
  else if (!g_strcmp0 (prop_name, "deinterlace-tff"))
    clean_name = g_strdup ("GstDeinterlace::tff");
  else if (!g_strcmp0 (prop_name, "tff") ||
      !g_strcmp0 (prop_name, "fields") || !g_strcmp0 (prop_name, "mode")) {
    GST_DEBUG_OBJECT (object, "Not allowed to use GstDeinterlace %s"
        " property without prefixing its name", prop_name);
    return FALSE;
  } else
    clean_name = g_strdup (prop_name);

  res =
      GES_TIMELINE_ELEMENT_CLASS (ges_video_source_parent_class)->lookup_child
      (object, clean_name, element, pspec);

  g_free (clean_name);

  return res;
}

static void
ges_video_source_class_init (GESVideoSourceClass * klass)
{
  GESTrackElementClass *track_element_class = GES_TRACK_ELEMENT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);
  GESVideoSourceClass *video_source_class = GES_VIDEO_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESVideoSourcePrivate));

  element_class->set_priority = _set_priority;
  element_class->lookup_child = _lookup_child;

  track_element_class->nleobject_factorytype = "nlesource";
  track_element_class->create_element = ges_video_source_create_element;
  video_source_class->create_source = NULL;
}

static void
ges_video_source_init (GESVideoSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_VIDEO_SOURCE, GESVideoSourcePrivate);
  self->priv->positioner = NULL;
  self->priv->capsfilter = NULL;
}
