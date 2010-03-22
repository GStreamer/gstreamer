/* GStreamer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* FIXME: workaround for SoundTouch.h of version 1.3.1 defining those
 * variables while it shouldn't. */
#undef VERSION
#undef PACKAGE_VERSION
#undef PACKAGE_TARNAME
#undef PACKAGE_STRING
#undef PACKAGE_NAME
#undef PACKAGE_BUGREPORT
#undef PACKAGE

#define FLOAT_SAMPLES 1
#include <soundtouch/BPMDetect.h>

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <math.h>
#include <string.h>
#include "gstbpmdetect.hh"

GST_DEBUG_CATEGORY_STATIC (gst_bpm_detect_debug);
#define GST_CAT_DEFAULT gst_bpm_detect_debug

#define GST_BPM_DETECT_GET_PRIVATE(o) (o->priv)

struct _GstBPMDetectPrivate
{
  gfloat bpm;
#ifdef HAVE_SOUNDTOUCH_1_4
    soundtouch::BPMDetect * detect;
#else
  BPMDetect *detect;
#endif
};

#define ALLOWED_CAPS \
    "audio/x-raw-float, "                                     \
    " width = (int) 32, "                                     \
    " endianness = (int) BYTE_ORDER, "                        \
    " rate = (int) [ 8000, MAX ], "                           \
    " channels = (int) [ 1, 2 ]"

GST_BOILERPLATE (GstBPMDetect, gst_bpm_detect, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER);

static void gst_bpm_detect_finalize (GObject * object);
static gboolean gst_bpm_detect_stop (GstBaseTransform * trans);
static gboolean gst_bpm_detect_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_bpm_detect_transform_ip (GstBaseTransform * trans,
    GstBuffer * in);
static gboolean gst_bpm_detect_setup (GstAudioFilter * filter,
    GstRingBufferSpec * format);

static void
gst_bpm_detect_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  gst_element_class_set_details_simple (element_class, "BPM Detector",
      "Filter/Analyzer/Audio", "Detect the BPM of an audio stream",
      "Sebastian Dröge <slomo@circular-chaos.org>");

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (g_class),
      caps);
  gst_caps_unref (caps);
}

static void
gst_bpm_detect_class_init (GstBPMDetectClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAudioFilterClass *filter_class = GST_AUDIO_FILTER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_bpm_detect_debug, "bpm_detect", 0,
      "audio bpm detection element");

  gobject_class->finalize = gst_bpm_detect_finalize;

  trans_class->stop = GST_DEBUG_FUNCPTR (gst_bpm_detect_stop);
  trans_class->event = GST_DEBUG_FUNCPTR (gst_bpm_detect_event);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_bpm_detect_transform_ip);
  trans_class->passthrough_on_same_caps = TRUE;

  filter_class->setup = GST_DEBUG_FUNCPTR (gst_bpm_detect_setup);

  g_type_class_add_private (gobject_class, sizeof (GstBPMDetectPrivate));
}

static void
gst_bpm_detect_init (GstBPMDetect * bpm_detect, GstBPMDetectClass * g_class)
{
  bpm_detect->priv = G_TYPE_INSTANCE_GET_PRIVATE ((bpm_detect),
      GST_TYPE_BPM_DETECT, GstBPMDetectPrivate);

  bpm_detect->priv->detect = NULL;
  bpm_detect->bpm = 0.0;
}

static void
gst_bpm_detect_finalize (GObject * object)
{
  GstBPMDetect *bpm_detect = GST_BPM_DETECT (object);

  if (bpm_detect->priv->detect) {
    delete bpm_detect->priv->detect;

    bpm_detect->priv->detect = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_bpm_detect_stop (GstBaseTransform * trans)
{
  GstBPMDetect *bpm_detect = GST_BPM_DETECT (trans);

  if (bpm_detect->priv->detect) {
    delete bpm_detect->priv->detect;

    bpm_detect->priv->detect = NULL;
  }
  bpm_detect->bpm = 0.0;

  return TRUE;
}

static gboolean
gst_bpm_detect_event (GstBaseTransform * trans, GstEvent * event)
{
  GstBPMDetect *bpm_detect = GST_BPM_DETECT (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
    case GST_EVENT_NEWSEGMENT:
      if (bpm_detect->priv->detect) {
        delete bpm_detect->priv->detect;

        bpm_detect->priv->detect = NULL;
      }
      bpm_detect->bpm = 0.0;
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
gst_bpm_detect_setup (GstAudioFilter * filter, GstRingBufferSpec * format)
{
  GstBPMDetect *bpm_detect = GST_BPM_DETECT (filter);

  if (bpm_detect->priv->detect) {
    delete bpm_detect->priv->detect;

    bpm_detect->priv->detect = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_bpm_detect_transform_ip (GstBaseTransform * trans, GstBuffer * in)
{
  GstBPMDetect *bpm_detect = GST_BPM_DETECT (trans);
  GstAudioFilter *filter = GST_AUDIO_FILTER (trans);
  gint nsamples;
  gfloat bpm;

  if (G_UNLIKELY (!bpm_detect->priv->detect)) {
    if (filter->format.channels == 0 || filter->format.rate == 0) {
      GST_ERROR_OBJECT (bpm_detect, "No channels or rate set yet");
      return GST_FLOW_ERROR;
    }
#ifdef HAVE_SOUNDTOUCH_1_4
    bpm_detect->priv->detect =
        new soundtouch::BPMDetect (filter->format.channels,
        filter->format.rate);
#else
    bpm_detect->priv->detect =
        new BPMDetect (filter->format.channels, filter->format.rate);
#endif
  }

  nsamples = GST_BUFFER_SIZE (in) / (4 * filter->format.channels);

  /* For stereo BPMDetect->inputSamples() does downmixing into the input
   * data but our buffer data shouldn't be modified.
   */
  if (filter->format.channels == 1) {
    gfloat *inbuf = (gfloat *) GST_BUFFER_DATA (in);

    while (nsamples > 0) {
      bpm_detect->priv->detect->inputSamples (inbuf, MIN (nsamples, 2048));
      nsamples -= 2048;
      inbuf += 2048;
    }
  } else {
    gfloat *inbuf, *intmp, data[2 * 2048];

    inbuf = (gfloat *) GST_BUFFER_DATA (in);
    intmp = data;

    while (nsamples > 0) {
      memcpy (intmp, inbuf, sizeof (gfloat) * 2 * MIN (nsamples, 2048));
      bpm_detect->priv->detect->inputSamples (intmp, MIN (nsamples, 2048));
      nsamples -= 2048;
      inbuf += 2048 * 2;
    }
  }

  bpm = bpm_detect->priv->detect->getBpm ();
  if (bpm >= 1.0 && fabs (bpm_detect->bpm - bpm) >= 1.0) {
    GstTagList *tags = gst_tag_list_new ();

    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE_ALL, GST_TAG_BEATS_PER_MINUTE,
        bpm, (void *) NULL);
    gst_element_found_tags (GST_ELEMENT (bpm_detect), tags);

    GST_INFO_OBJECT (bpm_detect, "Detected BPM: %lf\n", bpm);
    bpm_detect->bpm = bpm;
  }

  return GST_FLOW_OK;
}
