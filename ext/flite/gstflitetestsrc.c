/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2005 Stefan Kost <ensonic@users.sf.net>
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

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/audio/multichannel.h>

#include <flite/flite.h>

#define GST_TYPE_FLITE_TEST_SRC \
  (gst_flite_test_src_get_type())
#define GST_FLITE_TEST_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLITE_TEST_SRC,GstFliteTestSrc))
#define GST_FLITE_TEST_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLITE_TEST_SRC,GstFliteTestSrcClass))
#define GST_IS_FLITE_TEST_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLITE_TEST_SRC))
#define GST_IS_FLITE_TEST_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLITE_TEST_SRC))

typedef struct _GstFliteTestSrc GstFliteTestSrc;
typedef struct _GstFliteTestSrcClass GstFliteTestSrcClass;

struct _GstFliteTestSrc
{
  GstBaseSrc parent;

  int samplerate;
  int n_channels;
  GstAudioChannelPosition *layout;

  int channel;

  cst_voice *voice;
};

struct _GstFliteTestSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_flite_test_src_get_type (void);



GST_DEBUG_CATEGORY_STATIC (flite_test_src_debug);
#define GST_CAT_DEFAULT flite_test_src_debug

static const GstElementDetails gst_flite_test_src_details =
GST_ELEMENT_DETAILS ("Flite speech test source",
    "Source/Audio",
    "Creates audio test signals identifying channels",
    "David Schleef <ds@schleef.org>");

enum
{
  PROP_0,
  PROP_LAST
};


static GstStaticPadTemplate gst_flite_test_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 48000, " "channels = (int) [1,8]")
    );


GST_BOILERPLATE (GstFliteTestSrc, gst_flite_test_src, GstBaseSrc,
    GST_TYPE_BASE_SRC);

static void gst_flite_test_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_flite_test_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_flite_test_src_start (GstBaseSrc * basesrc);
static gboolean gst_flite_test_src_stop (GstBaseSrc * basesrc);
static GstFlowReturn gst_flite_test_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint length, GstBuffer ** buffer);
static gboolean
gst_flite_test_src_set_caps (GstBaseSrc * basesrc, GstCaps * caps);


static void
gst_flite_test_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (flite_test_src_debug, "flitetestsrc", 0,
      "Flite Audio Test Source");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_flite_test_src_src_template));
  gst_element_class_set_details (element_class, &gst_flite_test_src_details);
}

static void
gst_flite_test_src_class_init (GstFliteTestSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->set_property = gst_flite_test_src_set_property;
  gobject_class->get_property = gst_flite_test_src_get_property;

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_flite_test_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_flite_test_src_stop);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_flite_test_src_create);
  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_flite_test_src_set_caps);
}

static void
gst_flite_test_src_init (GstFliteTestSrc * src, GstFliteTestSrcClass * g_class)
{
#if 0
  GstPad *pad = GST_BASE_SRC_PAD (src);
#endif

  src->samplerate = 48000;

  /* we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);

  gst_base_src_set_blocksize (GST_BASE_SRC (src), -1);
}

static gboolean
gst_flite_test_src_set_caps (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstFliteTestSrc *src = GST_FLITE_TEST_SRC (basesrc);
  GstStructure *structure;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "channels", &src->n_channels);

  g_free (src->layout);

  if (src->n_channels < 3) {
    src->layout = g_malloc (sizeof (GstAudioChannelPosition) * 2);
    if (src->n_channels == 1) {
      src->layout[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
    } else {
      src->layout[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      src->layout[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    }
  } else {
    src->layout = gst_audio_get_channel_positions (structure);
    if (src->layout == NULL) {
      /* thanks, libgstaudio, for returning us NULL instead of
       * doing this yourself. */
      int i;
      src->layout =
          g_malloc (sizeof (GstAudioChannelPosition) * src->n_channels);
      for (i = 0; i < src->n_channels; i++) {
        src->layout[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
      }
    }
  }

  return ret;
}

#if 0
static gboolean
gst_flite_test_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstFliteTestSrc *src = GST_FLITE_TEST_SRC (basesrc);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (src_fmt == dest_fmt) {
        dest_val = src_val;
        goto done;
      }

      switch (src_fmt) {
        case GST_FORMAT_DEFAULT:
          switch (dest_fmt) {
            case GST_FORMAT_TIME:
              /* samples to time */
              dest_val =
                  gst_util_uint64_scale_int (src_val, GST_SECOND,
                  src->samplerate);
              break;
            default:
              goto error;
          }
          break;
        case GST_FORMAT_TIME:
          switch (dest_fmt) {
            case GST_FORMAT_DEFAULT:
              /* time to samples */
              dest_val =
                  gst_util_uint64_scale_int (src_val, src->samplerate,
                  GST_SECOND);
              break;
            default:
              goto error;
          }
          break;
        default:
          goto error;
      }
    done:
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
      break;
  }

  return res;
  /* ERROR */
error:
  {
    GST_DEBUG_OBJECT (src, "query failed");
    return FALSE;
  }
}
#endif


#if 0
static void
gst_flite_test_src_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration)) {
        *end = timestamp + duration;
      }
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}
#endif

cst_voice *register_cmu_us_kal ();



static gboolean
gst_flite_test_src_start (GstBaseSrc * basesrc)
{
  GstFliteTestSrc *src = GST_FLITE_TEST_SRC (basesrc);

  src->voice = register_cmu_us_kal ();
  src->n_channels = 2;

  return TRUE;
}

static gboolean
gst_flite_test_src_stop (GstBaseSrc * basesrc)
{
  return TRUE;
}

char *
get_channel_name (GstFliteTestSrc * src, int channel)
{
  const char *numbers[10] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
    "nine"
  };
  const char *names[GST_AUDIO_CHANNEL_POSITION_NUM] = {
    "mono", "front left", "front right", "rear center",
    "rear left", "rear right", "low frequency effects",
    "front center", "front left center", "front right center",
    "side left", "side right",
    "none"
  };
  const char *name;

  if (src->layout[channel] == GST_AUDIO_CHANNEL_POSITION_INVALID) {
    name = "invalid";
  } else {
    name = names[src->layout[channel]];
  }

  return g_strdup_printf ("%s, %s", numbers[channel], name);
}

static GstFlowReturn
gst_flite_test_src_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstFliteTestSrc *src;
  GstBuffer *buf;
  cst_wave *wave;
  char *text;
  int i;
  gint16 *data;

  src = GST_FLITE_TEST_SRC (basesrc);

  text = get_channel_name (src, src->channel);

  wave = flite_text_to_wave (text, src->voice);
  g_free (text);
  cst_wave_resample (wave, 48000);

  GST_DEBUG ("type %s, sample_rate %d, num_samples %d, num_channels %d",
      wave->type, wave->sample_rate, wave->num_samples, wave->num_channels);

  buf = gst_buffer_new_and_alloc (src->n_channels * sizeof (gint16) *
      wave->num_samples);

  data = (void *) GST_BUFFER_DATA (buf);
  memset (data, 0, src->n_channels * sizeof (gint16) * wave->num_samples);
  for (i = 0; i < wave->num_samples; i++) {
    data[i * src->n_channels + src->channel] = wave->samples[i];
  }

  src->channel++;
  if (src->channel == src->n_channels)
    src->channel = 0;

  *buffer = buf;

  return GST_FLOW_OK;
}

static void
gst_flite_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
#if 0
  GstFliteTestSrc *src = GST_FLITE_TEST_SRC (object);
#endif

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_flite_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
#if 0
  GstFliteTestSrc *src = GST_FLITE_TEST_SRC (object);
#endif

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
