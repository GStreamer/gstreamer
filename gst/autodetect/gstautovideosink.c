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

/**
 * SECTION:element-autovideosink
 * @see_also: autoaudiosink, ximagesink, xvimagesink, sdlvideosink
 *
 * <refsect2>
 * <para>
 * autovideosink is a video sink that automatically detects an appropriate
 * video sink to use.  It does so by scanning the registry for all elements
 * that have <quote>Sink</quote> and <quote>Audio</quote> in the class field
 * of their element information, and also have a non-zero autoplugging rank.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m videotestsrc ! autovideosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstautovideosink.h"
#include "gstautodetect.h"

static GstStateChangeReturn
gst_auto_video_sink_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstAutoVideoSink, gst_auto_video_sink, GstBin, GST_TYPE_BIN);

static void
gst_auto_video_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  const GstElementDetails gst_auto_video_sink_details =
      GST_ELEMENT_DETAILS ("Auto video sink",
      "Sink/Video",
      "Wrapper video sink for automatically detected video sink",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (eklass, &gst_auto_video_sink_details);
}

static void
gst_auto_video_sink_class_init (GstAutoVideoSinkClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  eklass->change_state = GST_DEBUG_FUNCPTR (gst_auto_video_sink_change_state);
}

/*
 * Hack to make initial linking work; ideally, this'd work even when
 * no target has been assigned to the ghostpad yet.
 */

static void
gst_auto_video_sink_reset (GstAutoVideoSink * sink)
{
  GstPad *targetpad;

  /* fakesink placeholder */
  if (sink->kid) {
    gst_element_set_state (sink->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (sink), sink->kid);
  }
  sink->kid = gst_element_factory_make ("fakesink", "tempsink");
  gst_bin_add (GST_BIN (sink), sink->kid);

  /* pad */
  targetpad = gst_element_get_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
}

static void
gst_auto_video_sink_init (GstAutoVideoSink * sink,
    GstAutoVideoSinkClass * g_class)
{
  sink->pad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pad);

  gst_auto_video_sink_reset (sink);

  GST_OBJECT_FLAG_SET (sink, GST_ELEMENT_IS_SINK);
}

static gboolean
gst_auto_video_sink_factory_filter (GstPluginFeature * feature, gpointer data)
{
  guint rank;
  const gchar *klass;

  /* we only care about element factories */
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  /* video sinks */
  klass = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (feature));
  if (!(strstr (klass, "Sink") && strstr (klass, "Video")))
    return FALSE;

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (feature);
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  return TRUE;
}

static gint
gst_auto_video_sink_compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;
  return strcmp (gst_plugin_feature_get_name (f2),
      gst_plugin_feature_get_name (f1));
}

static GstElement *
gst_auto_video_sink_find_best (GstAutoVideoSink * sink)
{
  GstElement *choice = NULL;
  GList *list, *walk;

  list = gst_registry_feature_filter (gst_registry_get_default (),
      (GstPluginFeatureFilter) gst_auto_video_sink_factory_filter, FALSE, sink);
  list = g_list_sort (list, (GCompareFunc) gst_auto_video_sink_compare_ranks);

  for (walk = list; walk != NULL; walk = walk->next) {
    GstElementFactory *f = GST_ELEMENT_FACTORY (walk->data);
    GstElement *el;

    GST_DEBUG_OBJECT (sink, "Trying %s", GST_PLUGIN_FEATURE (f)->name);
    if ((el = gst_element_factory_create (f, "actual-sink"))) {
      GstStateChangeReturn ret;

      GST_DEBUG_OBJECT (sink, "Changing state to READY");

      ret = gst_element_set_state (el, GST_STATE_READY);
      if (ret == GST_STATE_CHANGE_SUCCESS) {
        GST_DEBUG_OBJECT (sink, "success");
        choice = el;
        goto done;
      }

      GST_WARNING_OBJECT (sink, "Couldn't set READY: %d", ret);
      ret = gst_element_set_state (el, GST_STATE_NULL);
      if (ret != GST_STATE_CHANGE_SUCCESS)
        GST_WARNING_OBJECT (sink,
            "Couldn't set element to NULL prior to disposal.");

      gst_object_unref (el);
    }
  }

done:
  gst_plugin_feature_list_free (list);

  return choice;
}

static gboolean
gst_auto_video_sink_detect (GstAutoVideoSink * sink)
{
  GstElement *esink;
  GstPad *targetpad;

  if (sink->kid) {
    gst_element_set_state (sink->kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (sink), sink->kid);
    sink->kid = NULL;
  }

  /* find element */
  GST_DEBUG_OBJECT (sink, "Creating new kid");
  if (!(esink = gst_auto_video_sink_find_best (sink))) {
    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
        ("Failed to find a supported video sink"));
    return FALSE;
  }

  sink->kid = esink;
  gst_bin_add (GST_BIN (sink), esink);

  /* attach ghost pad */
  GST_DEBUG_OBJECT (sink, "Re-assigning ghostpad");
  targetpad = gst_element_get_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (sink, "done changing auto video sink");

  return TRUE;
}

static GstStateChangeReturn
gst_auto_video_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstAutoVideoSink *sink = GST_AUTO_VIDEO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_auto_video_sink_detect (sink))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_auto_video_sink_reset (sink);
      break;
    default:
      break;
  }

  return ret;
}
