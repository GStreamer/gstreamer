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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstautoaudiosink.h"
#include "gstautodetect.h"

static GstStateChangeReturn
gst_auto_audio_sink_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstAutoAudioSink, gst_auto_audio_sink, GstBin, GST_TYPE_BIN);

static void
gst_auto_audio_sink_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_auto_audio_sink_details = {
    "Auto audio sink",
    "Sink/Audio",
    "Audio sink embedding the Auto-settings for audio output",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };
  GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (eklass, &gst_auto_audio_sink_details);
}

static void
gst_auto_audio_sink_class_init (GstAutoAudioSinkClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  eklass->change_state = gst_auto_audio_sink_change_state;
}

/*
 * Hack to make initial linking work; ideally, this'd work even when
 * no target has been assigned to the ghostpad yet.
 */

static void
gst_auto_audio_sink_reset (GstAutoAudioSink * sink)
{
  GstPad *targetpad;

  /* fakesink placeholder */
  if (sink->kid) {
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
gst_auto_audio_sink_init (GstAutoAudioSink * sink,
    GstAutoAudioSinkClass * g_class)
{
  sink->pad = gst_ghost_pad_new_notarget ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pad);

  gst_auto_audio_sink_reset (sink);
}

static gboolean
gst_auto_audio_sink_factory_filter (GstPluginFeature * feature, gpointer data)
{
  guint rank;
  const gchar *klass;

  /* we only care about element factories */
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  /* audio sinks */
  klass = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (feature));
  if (strcmp (klass, "Sink/Audio") != 0)
    return FALSE;

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (feature);
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  return TRUE;
}

static gint
gst_auto_audio_sink_compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;
  return strcmp (gst_plugin_feature_get_name (f2),
      gst_plugin_feature_get_name (f1));
}

static GstElement *
gst_auto_audio_sink_find_best (GstAutoAudioSink * sink)
{
  GList *list, *item;
  GstElement *choice = NULL;
  gboolean ss = TRUE;

  list = gst_registry_pool_feature_filter (
      (GstPluginFeatureFilter) gst_auto_audio_sink_factory_filter, FALSE, sink);
  list = g_list_sort (list, (GCompareFunc) gst_auto_audio_sink_compare_ranks);

  /* FIXME:
   * - soundservers have no priority yet.
   * - soundserversinks should only be chosen if already running, or if
   *    the user explicitely wants this to run... That is not easy.
   */

  while (1) {
    GST_LOG ("Trying to find %s", ss ? "soundservers" : "audio devices");

    for (item = list; item != NULL; item = item->next) {
      GstElementFactory *f = GST_ELEMENT_FACTORY (item->data);
      GstElement *el;

      if ((el = gst_element_factory_create (f, "actual-sink"))) {
        gboolean has_p = g_object_class_find_property (G_OBJECT_GET_CLASS (el),
            "soundserver-running") ? TRUE : FALSE;

        if (ss == has_p) {
          if (ss) {
            gboolean r;

            g_object_get (G_OBJECT (el), "soundserver-running", &r, NULL);
            if (r) {
              GST_LOG ("%s - soundserver is running",
                  GST_PLUGIN_FEATURE (f)->name);
            } else {
              GST_LOG ("%s - Soundserver is not running",
                  GST_PLUGIN_FEATURE (f)->name);
              goto next;
            }
          }
          GST_LOG ("Testing %s", GST_PLUGIN_FEATURE (f)->name);
          if (gst_element_set_state (el,
                  GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS) {
            gst_element_set_state (el, GST_STATE_NULL);
            GST_LOG ("This worked!");
            choice = el;
            goto done;
          }
        }

      next:
        gst_object_unref (GST_OBJECT (el));
      }
    }

    if (!ss)
      break;
    ss = FALSE;
  }

done:
  g_list_free (list);

  return choice;
}

static gboolean
gst_auto_audio_sink_detect (GstAutoAudioSink * sink)
{
  GstElement *esink;
  GstPad *targetpad;

  if (sink->kid) {
    gst_bin_remove (GST_BIN (sink), sink->kid);
    sink->kid = NULL;
  }

  /* find element */
  GST_DEBUG_OBJECT (sink, "Creating new kid");
  if (!(esink = gst_auto_audio_sink_find_best (sink))) {
    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
        ("Failed to find a supported audio sink"));
    return FALSE;
  }
  sink->kid = esink;
  gst_bin_add (GST_BIN (sink), esink);

  /* attach ghost pad */
  GST_DEBUG_OBJECT (sink, "Re-assigning ghostpad");
  targetpad = gst_element_get_pad (sink->kid, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (sink, "done changing auto audio sink");

  return TRUE;
}

static GstStateChangeReturn
gst_auto_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstAutoAudioSink *sink = GST_AUTO_AUDIO_SINK (element);

  GST_DEBUG_OBJECT (element, "Change state 0x%x", transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_auto_audio_sink_detect (sink))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_auto_audio_sink_reset (sink);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}
