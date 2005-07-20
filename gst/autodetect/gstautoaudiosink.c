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

static void gst_auto_audio_sink_base_init (GstAutoAudioSinkClass * klass);
static void gst_auto_audio_sink_class_init (GstAutoAudioSinkClass * klass);
static void gst_auto_audio_sink_init (GstAutoAudioSink * sink);
static void gst_auto_audio_sink_detect (GstAutoAudioSink * sink, gboolean fake);
static GstElementStateReturn
gst_auto_audio_sink_change_state (GstElement * element);

static GstBinClass *parent_class = NULL;

GType
gst_auto_audio_sink_get_type (void)
{
  static GType gst_auto_audio_sink_type = 0;

  if (!gst_auto_audio_sink_type) {
    static const GTypeInfo gst_auto_audio_sink_info = {
      sizeof (GstAutoAudioSinkClass),
      (GBaseInitFunc) gst_auto_audio_sink_base_init,
      NULL,
      (GClassInitFunc) gst_auto_audio_sink_class_init,
      NULL,
      NULL,
      sizeof (GstAutoAudioSink),
      0,
      (GInstanceInitFunc) gst_auto_audio_sink_init,
    };

    gst_auto_audio_sink_type = g_type_register_static (GST_TYPE_BIN,
        "GstAutoAudioSink", &gst_auto_audio_sink_info, 0);
  }

  return gst_auto_audio_sink_type;
}

static void
gst_auto_audio_sink_base_init (GstAutoAudioSinkClass * klass)
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

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  eklass->change_state = gst_auto_audio_sink_change_state;
}

static void
gst_auto_audio_sink_init (GstAutoAudioSink * sink)
{
  sink->pad = NULL;
  sink->kid = NULL;
  gst_auto_audio_sink_detect (sink, TRUE);
  sink->init = FALSE;
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
          if (gst_element_set_state (el, GST_STATE_READY) == GST_STATE_SUCCESS) {
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

static void
gst_auto_audio_sink_detect (GstAutoAudioSink * sink, gboolean fake)
{
  GstElement *esink;
  GstPad *peer = NULL;

  /* save ghostpad */
  if (sink->pad) {
    gst_object_ref (GST_OBJECT (sink->pad));
    peer = GST_PAD_PEER (GST_PAD_REALIZE (sink->pad));
    if (peer)
      gst_pad_unlink (peer, sink->pad);
  }

  /* kill old element */
  if (sink->kid) {
    GST_DEBUG_OBJECT (sink, "Removing old kid");
    gst_bin_remove (GST_BIN (sink), sink->kid);
    sink->kid = NULL;
  }

  /* find element */
  GST_DEBUG_OBJECT (sink, "Creating new kid (%ssink)", fake ? "fake" : "audio");
  if (fake) {
    esink = gst_element_factory_make ("fakesink", "temporary-sink");
  } else if (!(esink = gst_auto_audio_sink_find_best (sink))) {
    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
        ("Failed to find a supported audio sink"));
    return;
  }
  sink->kid = esink;
  gst_bin_add (GST_BIN (sink), esink);

  /* attach ghost pad */
  if (sink->pad) {
    GST_DEBUG_OBJECT (sink, "Re-doing existing ghostpad");
    gst_pad_add_ghost_pad (gst_element_get_pad (sink->kid, "sink"), sink->pad);
    if (GST_ELEMENT (sink)->pads == NULL)
      gst_element_add_pad (GST_ELEMENT (sink), sink->pad);
  } else {
    GST_DEBUG_OBJECT (sink, "Creating new ghostpad");
    sink->pad = gst_ghost_pad_new ("sink",
        gst_element_get_pad (sink->kid, "sink"));
    gst_element_add_pad (GST_ELEMENT (sink), sink->pad);
  }
  if (peer) {
    GST_DEBUG_OBJECT (sink, "Linking...");
    gst_pad_link (peer, sink->pad);
  }

  GST_DEBUG_OBJECT (sink, "done changing auto audio sink");
  sink->init = TRUE;
}

static GstElementStateReturn
gst_auto_audio_sink_change_state (GstElement * element)
{
  GstAutoAudioSink *sink = GST_AUTO_AUDIO_SINK (element);

  if (GST_STATE_TRANSITION (element) == GST_STATE_NULL_TO_READY && !sink->init) {
    gst_auto_audio_sink_detect (sink, FALSE);
    if (!sink->init)
      return GST_STATE_FAILURE;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}
