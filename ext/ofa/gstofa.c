/* GStreamer ofa fingerprinting element
 * Copyright (C) 2006 M. Derezynski
 * Copyright (C) 2008 Eric Buehl
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
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
 * Boston, MA  02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <ofa1/ofa.h>
#include "gstofa.h"

#define PAD_CAPS \
	"audio/x-raw, " \
        "format = { S16LE, S16BE }, " \
        "rate = (int) [ 1, MAX ], " \
        "channels = (int) [ 1, 2 ]"

GST_DEBUG_CATEGORY_STATIC (gst_ofa_debug);
#define GST_CAT_DEFAULT gst_ofa_debug

enum
{
  PROP_0,
  PROP_FINGERPRINT,
};

#define parent_class gst_ofa_parent_class
G_DEFINE_TYPE (GstOFA, gst_ofa, GST_TYPE_AUDIO_FILTER);

static void gst_ofa_finalize (GObject * object);
static void gst_ofa_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_ofa_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_ofa_sink_event (GstBaseTransform * trans, GstEvent * event);

static void
gst_ofa_finalize (GObject * object)
{
  GstOFA *ofa = GST_OFA (object);

  if (ofa->adapter) {
    g_object_unref (ofa->adapter);
    ofa->adapter = NULL;
  }

  g_free (ofa->fingerprint);
  ofa->fingerprint = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ofa_class_init (GstOFAClass * klass)
{
  GstBaseTransformClass *gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAudioFilterClass *audio_filter_class = GST_AUDIO_FILTER_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstCaps *caps;

  gobject_class->get_property = gst_ofa_get_property;

  g_object_class_install_property (gobject_class, PROP_FINGERPRINT,
      g_param_spec_string ("fingerprint", "Resulting fingerprint",
          "Resulting fingerprint", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_ofa_finalize;

  gstbasetrans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_ofa_transform_ip);
  gstbasetrans_class->sink_event = GST_DEBUG_FUNCPTR (gst_ofa_sink_event);
  gstbasetrans_class->passthrough_on_same_caps = TRUE;

  gst_element_class_set_static_metadata (gstelement_class, "OFA",
      "MusicIP Fingerprinting element",
      "Find a music fingerprint using MusicIP's libofa",
      "Milosz Derezynski <internalerror@gmail.com>, "
      "Eric Buehl <eric.buehl@gmail.com>");

  caps = gst_caps_from_string (PAD_CAPS);
  gst_audio_filter_class_add_pad_templates (audio_filter_class, caps);
  gst_caps_unref (caps);
}

static void
create_fingerprint (GstOFA * ofa)
{
  GstAudioFilter *audiofilter = GST_AUDIO_FILTER (ofa);
  const guint8 *samples;
  const gchar *fingerprint;
  gint rate, channels, endianness;
  GstTagList *tags;
  gsize available;

  available = gst_adapter_available (ofa->adapter);

  if (available == 0) {
    GST_WARNING_OBJECT (ofa, "No data to take fingerprint from");
    ofa->record = FALSE;
    return;
  }

  rate = GST_AUDIO_INFO_RATE (&audiofilter->info);
  channels = GST_AUDIO_INFO_CHANNELS (&audiofilter->info);
  if (GST_AUDIO_INFO_ENDIANNESS (&audiofilter->info) == G_BIG_ENDIAN)
    endianness = OFA_BIG_ENDIAN;
  else
    endianness = OFA_LITTLE_ENDIAN;


  GST_DEBUG_OBJECT (ofa, "Generating fingerprint for %" G_GSIZE_FORMAT
      " samples", available / sizeof (gint16));

  samples = gst_adapter_map (ofa->adapter, available);

  fingerprint = ofa_create_print ((unsigned char *) samples, endianness,
      available / sizeof (gint16), rate, (channels == 2) ? 1 : 0);

  gst_adapter_unmap (ofa->adapter);
  gst_adapter_flush (ofa->adapter, available);

  if (fingerprint == NULL) {
    GST_WARNING_OBJECT (ofa, "Failed to generate fingerprint");
    goto done;
  }

  GST_INFO_OBJECT (ofa, "Generated fingerprint: %s", fingerprint);
  ofa->fingerprint = g_strdup (fingerprint);

  // FIXME: combine with upstream tags
  tags = gst_tag_list_new (GST_TAG_OFA_FINGERPRINT, ofa->fingerprint, NULL);
  gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (ofa),
      gst_event_new_tag (tags));

  g_object_notify (G_OBJECT (ofa), "fingerprint");

done:

  ofa->record = FALSE;
}

static gboolean
gst_ofa_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstOFA *ofa = GST_OFA (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_SEGMENT:
      GST_DEBUG_OBJECT (ofa, "Got %s event, clearing buffer",
          GST_EVENT_TYPE_NAME (event));
      gst_adapter_clear (ofa->adapter);
      /* FIXME: should we really always reset this instead of using an
       * already-existing fingerprint? Assumes fingerprints are always
       * extracted in a separate pipeline instead of a live playback
       * situation */
      ofa->record = TRUE;
      g_free (ofa->fingerprint);
      ofa->fingerprint = NULL;
      break;
    case GST_EVENT_EOS:
      /* we got to the end of the stream but never generated a fingerprint
       * (probably under 135 seconds)
       */
      if (!ofa->fingerprint && ofa->record)
        create_fingerprint (ofa);
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static void
gst_ofa_init (GstOFA * ofa)
{
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (ofa), TRUE);

  ofa->fingerprint = NULL;
  ofa->record = TRUE;

  ofa->adapter = gst_adapter_new ();
}

static GstFlowReturn
gst_ofa_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstAudioFilter *audiofilter = GST_AUDIO_FILTER (trans);
  GstOFA *ofa = GST_OFA (trans);
  guint64 nframes;
  GstClockTime duration;
  gint rate, channels;

  rate = GST_AUDIO_INFO_RATE (&audiofilter->info);
  channels = GST_AUDIO_INFO_CHANNELS (&audiofilter->info);

  if (rate == 0 || channels == 0)
    return GST_FLOW_NOT_NEGOTIATED;

  if (!ofa->record)
    return GST_FLOW_OK;

  gst_adapter_push (ofa->adapter, gst_buffer_copy (buf));

  nframes = gst_adapter_available (ofa->adapter) / (channels * 2);
  duration = GST_FRAMES_TO_CLOCK_TIME (nframes, rate);

  if (duration >= 135 * GST_SECOND && ofa->fingerprint == NULL)
    create_fingerprint (ofa);

  return GST_FLOW_OK;
}

static void
gst_ofa_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOFA *ofa = GST_OFA (object);

  switch (prop_id) {
    case PROP_FINGERPRINT:
      g_value_set_string (value, ofa->fingerprint);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;
  int major, minor, rev;

  GST_DEBUG_CATEGORY_INIT (gst_ofa_debug, "ofa", 0, "ofa element");

  ofa_get_version (&major, &minor, &rev);

  GST_DEBUG ("libofa %d.%d.%d", major, minor, rev);

  ret = gst_element_register (plugin, "ofa", GST_RANK_NONE, GST_TYPE_OFA);

  if (ret) {
    /* TODO: get this into core */
    gst_tag_register (GST_TAG_OFA_FINGERPRINT, GST_TAG_FLAG_META,
        G_TYPE_STRING, "ofa fingerprint", "OFA fingerprint", NULL);
  }

  return ret;
}

/* FIXME: someone write a libofa replacement with an LGPL or BSD license */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ofa,
    "Calculate MusicIP fingerprint from audio files",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
