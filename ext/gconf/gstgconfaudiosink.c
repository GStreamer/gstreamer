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

#include "gstgconfelements.h"
#include "gstgconfaudiosink.h"

static void gst_gconf_audio_sink_base_init (GstGConfAudioSinkClass * klass);
static void gst_gconf_audio_sink_class_init (GstGConfAudioSinkClass * klass);
static void gst_gconf_audio_sink_init (GstGConfAudioSink * sink);
static void gst_gconf_audio_sink_dispose (GObject * object);

static void cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data);

static GstElementStateReturn
gst_gconf_audio_sink_change_state (GstElement * element);

static GstBinClass *parent_class = NULL;

GType
gst_gconf_audio_sink_get_type (void)
{
  static GType gst_gconf_audio_sink_type = 0;

  if (!gst_gconf_audio_sink_type) {
    static const GTypeInfo gst_gconf_audio_sink_info = {
      sizeof (GstGConfAudioSinkClass),
      (GBaseInitFunc) gst_gconf_audio_sink_base_init,
      NULL,
      (GClassInitFunc) gst_gconf_audio_sink_class_init,
      NULL,
      NULL,
      sizeof (GstGConfAudioSink),
      0,
      (GInstanceInitFunc) gst_gconf_audio_sink_init,
    };

    gst_gconf_audio_sink_type = g_type_register_static (GST_TYPE_BIN,
        "GstGConfAudioSink", &gst_gconf_audio_sink_info, 0);
  }

  return gst_gconf_audio_sink_type;
}

static void
gst_gconf_audio_sink_base_init (GstGConfAudioSinkClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_gconf_audio_sink_details = {
    "GConf audio sink",
    "Sink/Audio",
    "Audio sink embedding the GConf-settings for audio output",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };
  GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (eklass, &gst_gconf_audio_sink_details);
}

static void
gst_gconf_audio_sink_class_init (GstGConfAudioSinkClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  oklass->dispose = gst_gconf_audio_sink_dispose;
  eklass->change_state = gst_gconf_audio_sink_change_state;
}

static void
gst_gconf_audio_sink_init (GstGConfAudioSink * sink)
{
  sink->pad = NULL;
  sink->kid = NULL;

  sink->client = gconf_client_get_default ();
  gconf_client_add_dir (sink->client, GST_GCONF_DIR,
      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
  gconf_client_notify_add (sink->client, GST_GCONF_DIR "/default/audiosink",
      cb_toggle_element, sink, NULL, NULL);
  cb_toggle_element (sink->client, 0, NULL, sink);

  sink->init = FALSE;
}

static void
gst_gconf_audio_sink_dispose (GObject * object)
{
  GstGConfAudioSink *sink = GST_GCONF_AUDIO_SINK (object);

  if (sink->client) {
    g_object_unref (G_OBJECT (sink->client));
    sink->client = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
cb_toggle_element (GConfClient * client,
    guint connection_id, GConfEntry * entry, gpointer data)
{
  GstGConfAudioSink *sink = GST_GCONF_AUDIO_SINK (data);
  GstPad *peer = NULL;
  GstElementState state = GST_STATE (sink);

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

  GST_DEBUG_OBJECT (sink, "Creating new kid (%ssink)",
      entry ? "audio" : "fake");
  sink->kid = entry ? gst_gconf_get_default_audio_sink () :
      gst_element_factory_make ("fakesink", "temporary-element");
  if (!sink->kid) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS, (NULL),
        ("Failed to render audio sink from GConf"));
    return;
  }
  gst_bin_add (GST_BIN (sink), sink->kid);

  /* re-attach ghostpad */
  if (sink->pad) {
    GST_DEBUG_OBJECT (sink, "Re-doing existing ghostpad");
    gst_pad_add_ghost_pad (gst_element_get_pad (sink->kid, "sink"), sink->pad);
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
  GST_DEBUG_OBJECT (sink, "Syncing state");
  gst_element_set_state (GST_ELEMENT (sink), state);

  GST_DEBUG_OBJECT (sink, "done changing gconf audio sink");
  sink->init = TRUE;
}

static GstElementStateReturn
gst_gconf_audio_sink_change_state (GstElement * element)
{
  GstGConfAudioSink *sink = GST_GCONF_AUDIO_SINK (element);

  if (GST_STATE_TRANSITION (element) == GST_STATE_NULL_TO_READY && !sink->init) {
    cb_toggle_element (sink->client, 0,
        gconf_client_get_entry (sink->client,
            GST_GCONF_DIR "/default/audiosink", NULL, TRUE, NULL), sink);
    if (!sink->init)
      return GST_STATE_FAILURE;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}
