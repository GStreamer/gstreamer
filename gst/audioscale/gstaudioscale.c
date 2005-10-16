/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
/* Element-Checklist-Version: 5 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>

/*#define DEBUG_ENABLED */
#include "gstaudioscale.h"
#include <gst/audio/audio.h>
#include <gst/resample/resample.h>

GST_DEBUG_CATEGORY_STATIC (audioscale_debug);
#define GST_CAT_DEFAULT audioscale_debug

/* elementfactory information */
static GstElementDetails gst_audioscale_details =
GST_ELEMENT_DETAILS ("Audio scaler",
    "Filter/Converter/Audio",
    "Resample audio",
    "David Schleef <ds@schleef.org>");

/* Audioscale signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FILTERLEN,
  ARG_METHOD
      /* FILL ME */
};

#define SUPPORTED_CAPS \
  GST_STATIC_CAPS (\
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "signed = (boolean) true")
#if 0
  /* disabled because it segfaults */
#define NOTHING "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, MAX ], " \
    "endianness = (int) BYTE_ORDER, " "width = (int) 32")
#endif
static GstStaticPadTemplate gst_audioscale_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static GstStaticPadTemplate gst_audioscale_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

#define GST_TYPE_AUDIOSCALE_METHOD (gst_audioscale_method_get_type())
static GType
gst_audioscale_method_get_type (void)
{
  static GType audioscale_method_type = 0;
  static GEnumValue audioscale_methods[] = {
    {GST_RESAMPLE_NEAREST, "0", "Nearest"},
    {GST_RESAMPLE_BILINEAR, "1", "Bilinear"},
    {GST_RESAMPLE_SINC, "2", "Sinc"},
    {0, NULL, NULL},
  };

  if (!audioscale_method_type) {
    audioscale_method_type = g_enum_register_static ("GstAudioscaleMethod",
        audioscale_methods);
  }
  return audioscale_method_type;
}

static void gst_audioscale_base_init (gpointer g_class);
static void gst_audioscale_class_init (AudioscaleClass * klass);
static void gst_audioscale_init (Audioscale * audioscale);
static void gst_audioscale_dispose (GObject * object);

static void gst_audioscale_chain (GstPad * pad, GstData * _data);
static GstStateChangeReturn gst_audioscale_change_state (GstElement * element,
    GstStateChange transition);

static void gst_audioscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audioscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void *gst_audioscale_get_buffer (void *priv, unsigned int size);

static GstElementClass *parent_class = NULL;

/*static guint gst_audioscale_signals[LAST_SIGNAL] = { 0 }; */

GType
audioscale_get_type (void)
{
  static GType audioscale_type = 0;

  if (!audioscale_type) {
    static const GTypeInfo audioscale_info = {
      sizeof (AudioscaleClass),
      gst_audioscale_base_init,
      NULL,
      (GClassInitFunc) gst_audioscale_class_init,
      NULL,
      NULL,
      sizeof (Audioscale), 0, (GInstanceInitFunc) gst_audioscale_init,
    };

    audioscale_type =
        g_type_register_static (GST_TYPE_ELEMENT, "Audioscale",
        &audioscale_info, 0);
  }
  return audioscale_type;
}

static void
gst_audioscale_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audioscale_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audioscale_sink_template));

  gst_element_class_set_details (gstelement_class, &gst_audioscale_details);
}

static void
gst_audioscale_class_init (AudioscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_audioscale_set_property;
  gobject_class->get_property = gst_audioscale_get_property;
  gobject_class->dispose = gst_audioscale_dispose;
  gstelement_class->change_state = gst_audioscale_change_state;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILTERLEN,
      g_param_spec_int ("filter_length", "filter_length", "filter_length",
          0, G_MAXINT, 16, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_AUDIOSCALE_METHOD, GST_RESAMPLE_SINC,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (audioscale_debug, "audioscale", 0,
      "audioscale element");
}

static GstStaticCaps gst_audioscale_passthru_caps =
GST_STATIC_CAPS ("audio/x-raw-int, channels = [ 3, MAX ]");
static GstStaticCaps gst_audioscale_convert_caps =
GST_STATIC_CAPS ("audio/x-raw-int, channels = [ 1, 2 ]");

static GstCaps *
gst_audioscale_expand_caps (const GstCaps * caps)
{
  GstCaps *caps1, *caps2;
  int i;

  caps1 = gst_caps_intersect (caps,
      gst_static_caps_get (&gst_audioscale_passthru_caps));
  caps2 = gst_caps_intersect (caps,
      gst_static_caps_get (&gst_audioscale_convert_caps));

  for (i = 0; i < gst_caps_get_size (caps2); i++) {
    GstStructure *structure = gst_caps_get_structure (caps2, i);

    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        NULL);
  }

  gst_caps_append (caps1, caps2);

  return caps1;
}

static GstCaps *
gst_audioscale_getcaps (GstPad * pad)
{
  Audioscale *audioscale;
  GstPad *otherpad;
  GstCaps *othercaps;
  GstCaps *caps;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == audioscale->srcpad) ? audioscale->sinkpad :
      audioscale->srcpad;
  othercaps = gst_pad_get_allowed_caps (otherpad);
  caps = gst_audioscale_expand_caps (othercaps);
  gst_caps_free (othercaps);

  return caps;
}

static GstCaps *
gst_audioscale_fixate (GstPad * pad, const GstCaps * caps)
{
  Audioscale *audioscale;
  gst_resample_t *r;
  GstPad *otherpad;
  int rate;
  GstCaps *copy;
  GstStructure *structure;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
  r = &(audioscale->gst_resample_template);
  if (pad == audioscale->srcpad) {
    otherpad = audioscale->sinkpad;
    rate = r->i_rate;
  } else {
    otherpad = audioscale->srcpad;
    rate = r->o_rate;
  }
  if (!GST_PAD_IS_NEGOTIATING (otherpad))
    return NULL;
  if (gst_caps_get_size (caps) > 1)
    return NULL;

  copy = gst_caps_copy (caps);
  structure = gst_caps_get_structure (copy, 0);
  if (gst_caps_structure_fixate_field_nearest_int (structure, "rate", rate))
    return copy;
  gst_caps_free (copy);
  return NULL;
}

static GstPadLinkReturn
gst_audioscale_link (GstPad * pad, const GstCaps * caps)
{
  Audioscale *audioscale;
  gst_resample_t *r;
  GstStructure *structure;
  double *rate, *otherrate;
  double temprate;

  int temp;
  gboolean ret;
  GstPadLinkReturn link_ret;
  GstPad *otherpad;
  GstCaps *copy;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
  r = &(audioscale->gst_resample_template);

  if (pad == audioscale->srcpad) {
    otherpad = audioscale->sinkpad;
    rate = &r->o_rate;
    otherrate = &r->i_rate;
  } else {
    otherpad = audioscale->srcpad;
    rate = &r->i_rate;
    otherrate = &r->o_rate;
  }

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &temp);
  ret &= gst_structure_get_int (structure, "channels", &r->channels);
  g_return_val_if_fail (ret, GST_PAD_LINK_REFUSED);
  *rate = (double) temp;

  copy = gst_audioscale_expand_caps (caps);
  link_ret = gst_pad_try_set_caps_nonfixed (otherpad, copy);
  gst_caps_free (copy);
  if (GST_PAD_LINK_FAILED (link_ret))
    return link_ret;

  caps = gst_pad_get_negotiated_caps (otherpad);
  g_return_val_if_fail (caps, GST_PAD_LINK_REFUSED);
  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &temp);
  g_return_val_if_fail (ret, GST_PAD_LINK_REFUSED);
  *otherrate = (double) temp;
  if (g_str_equal (gst_structure_get_name (structure), "audio/x-raw-float")) {
    r->format = GST_RESAMPLE_FLOAT;
  } else {
    r->format = GST_RESAMPLE_S16;
  }

  audioscale->passthru = (r->i_rate == r->o_rate);
  audioscale->increase = (r->o_rate >= r->i_rate);
  /* now create audioscale iterations */
  audioscale->num_iterations = 0;

  temprate = r->i_rate;
  while (TRUE) {
    if (r->o_rate > r->i_rate) {
      if (temprate >= r->o_rate)
        break;
      temprate *= 2;
    } else {
      if (temprate <= r->o_rate)
        break;
      temprate /= 2;
    }
    audioscale->num_iterations++;
  }




  if (audioscale->num_iterations > 0) {
    audioscale->offsets = g_new0 (gint64, audioscale->num_iterations);
    audioscale->gst_resample = g_new0 (gst_resample_t, 1);
    audioscale->gst_resample->priv = audioscale;
    audioscale->gst_resample->get_buffer = gst_audioscale_get_buffer;
    audioscale->gst_resample->method = r->method;
    audioscale->gst_resample->channels = r->channels;
    audioscale->gst_resample->filter_length = r->filter_length;
    audioscale->gst_resample->format = r->format;
    if (audioscale->increase) {
      temprate = r->o_rate;

      while (temprate / 2 >= r->i_rate) {
        temprate = temprate / 2;
      }
      /* now temprate is output rate of gstresample */
      GST_DEBUG ("gstresample will increase rate from %f to %f", r->i_rate,
          temprate);
      audioscale->gst_resample->o_rate = temprate;
      audioscale->gst_resample->i_rate = r->i_rate;
    } else {
      temprate = r->i_rate;

      while (temprate / 2 >= r->o_rate) {
        temprate = temprate / 2;
      }
      /* now temprate is input rate of gstresample */
      GST_DEBUG ("gstresample will decrease rate from %f to %f", temprate,
          r->o_rate);
      audioscale->gst_resample->o_rate = r->o_rate;
      audioscale->gst_resample->i_rate = temprate;
    }
    audioscale->passthru =
        (audioscale->gst_resample->i_rate == audioscale->gst_resample->o_rate);
    if (!audioscale->passthru)
      audioscale->num_iterations--;
    GST_DEBUG ("Number of iterations: %d", audioscale->num_iterations);

    gst_resample_init (audioscale->gst_resample);
  }

  return link_ret;
}

static void *
gst_audioscale_get_buffer (void *priv, unsigned int size)
{
  Audioscale *audioscale = priv;

  GST_DEBUG ("size requested: %u irate: %f orate: %f", size,
      audioscale->gst_resample->i_rate, audioscale->gst_resample->o_rate);
  audioscale->outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (audioscale->outbuf) = size;
  GST_BUFFER_DATA (audioscale->outbuf) = g_malloc (size);
  GST_BUFFER_TIMESTAMP (audioscale->outbuf) =
      audioscale->gst_resample_offset * GST_SECOND /
      audioscale->gst_resample->o_rate;
  audioscale->gst_resample_offset +=
      size / sizeof (gint16) / audioscale->gst_resample->channels;

  return GST_BUFFER_DATA (audioscale->outbuf);
}

/* reduces rate by factor of 2 */
GstBuffer *
gst_audioscale_decrease_rate (Audioscale * audioscale,
    GstBuffer * buf, double outrate, int cur_iteration)
{
  gint i, j, curoffset;
  GstBuffer *outbuf = gst_buffer_new ();
  gint16 *outdata;
  gint16 *indata;

  GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (buf) / 2;
  outdata = g_malloc (GST_BUFFER_SIZE (outbuf));
  indata = (gint16 *) GST_BUFFER_DATA (buf);

  GST_DEBUG
      ("iteration = %d channels = %d in size = %d out size = %d outrate = %f",
      cur_iteration, audioscale->gst_resample_template.channels,
      GST_BUFFER_SIZE (buf), GST_BUFFER_SIZE (outbuf), outrate);
  curoffset = 0;
  for (i = 0; i < GST_BUFFER_SIZE (buf) / (sizeof (gint16));
      i += 2 * audioscale->gst_resample_template.channels) {
    for (j = 0; j < audioscale->gst_resample_template.channels; j++) {
      outdata[curoffset + j] =
          (indata[i + j] + indata[i + j +
              audioscale->gst_resample_template.channels]) / 2;
    }
    curoffset += audioscale->gst_resample_template.channels;
  }

  GST_BUFFER_DATA (outbuf) = (gpointer) outdata;
  GST_BUFFER_TIMESTAMP (outbuf) =
      audioscale->offsets[cur_iteration] * GST_SECOND / outrate;
  audioscale->offsets[cur_iteration] +=
      GST_BUFFER_SIZE (outbuf) / sizeof (gint16) /
      audioscale->gst_resample->channels;
  return outbuf;
}

/* increases rate by factor of 2 */
GstBuffer *
gst_audioscale_increase_rate (Audioscale * audioscale,
    GstBuffer * buf, double outrate, int cur_iteration)
{
  gint i, j, curoffset;
  GstBuffer *outbuf = gst_buffer_new ();
  gint16 *outdata;
  gint16 *indata;

  GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (buf) * 2;
  outdata = g_malloc (GST_BUFFER_SIZE (outbuf));
  indata = (gint16 *) GST_BUFFER_DATA (buf);

  GST_DEBUG
      ("iteration = %d channels = %d in size = %d out size = %d out rate = %f",
      cur_iteration, audioscale->gst_resample_template.channels,
      GST_BUFFER_SIZE (buf), GST_BUFFER_SIZE (outbuf), outrate);
  curoffset = 0;
  for (i = 0; i < GST_BUFFER_SIZE (buf) / (sizeof (gint16));
      i += audioscale->gst_resample_template.channels) {
    for (j = 0; j < audioscale->gst_resample_template.channels; j++) {
      outdata[curoffset] = indata[i + j];
      outdata[curoffset + audioscale->gst_resample_template.channels] =
          indata[i + j];
      curoffset++;
    }
    curoffset += audioscale->gst_resample_template.channels;
  }

  GST_BUFFER_DATA (outbuf) = (gpointer) outdata;
  GST_BUFFER_TIMESTAMP (outbuf) =
      audioscale->offsets[cur_iteration] * GST_SECOND / outrate;
  audioscale->offsets[cur_iteration] +=
      GST_BUFFER_SIZE (outbuf) / sizeof (gint16) /
      audioscale->gst_resample->channels;
  return outbuf;
}

static void
gst_audioscale_init (Audioscale * audioscale)
{
  gst_resample_t *r;

  audioscale->num_iterations = 1;

  audioscale->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audioscale_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (audioscale), audioscale->sinkpad);
  gst_pad_set_chain_function (audioscale->sinkpad, gst_audioscale_chain);
  gst_pad_set_link_function (audioscale->sinkpad, gst_audioscale_link);
  gst_pad_set_getcaps_function (audioscale->sinkpad, gst_audioscale_getcaps);
  gst_pad_set_fixate_function (audioscale->sinkpad, gst_audioscale_fixate);

  audioscale->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_audioscale_src_template), "src");

  gst_element_add_pad (GST_ELEMENT (audioscale), audioscale->srcpad);
  gst_pad_set_link_function (audioscale->srcpad, gst_audioscale_link);
  gst_pad_set_getcaps_function (audioscale->srcpad, gst_audioscale_getcaps);
  gst_pad_set_fixate_function (audioscale->srcpad, gst_audioscale_fixate);

  r = &(audioscale->gst_resample_template);

  r->priv = audioscale;
  r->get_buffer = gst_audioscale_get_buffer;
  r->method = GST_RESAMPLE_SINC;
  r->channels = 0;
  r->filter_length = 16;
  r->i_rate = -1;
  r->o_rate = -1;
  r->format = GST_RESAMPLE_S16;
  /*r->verbose = 1; */

  audioscale->gst_resample = NULL;
  audioscale->outbuf = NULL;
  audioscale->offsets = NULL;
  audioscale->gst_resample_offset = 0;
  audioscale->increase = FALSE;

  GST_OBJECT_FLAG_SET (audioscale, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_audioscale_dispose (GObject * object)
{
  Audioscale *audioscale = GST_AUDIOSCALE (object);

  if (audioscale->gst_resample) {
    gst_resample_close (audioscale->gst_resample);
    g_free (audioscale->gst_resample);
    audioscale->gst_resample = NULL;
  }
  if (audioscale->offsets) {
    g_free (audioscale->offsets);
    audioscale->offsets = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audioscale_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstBuffer *tempbuf, *tempbuf2;
  GstClockTime outduration;

  Audioscale *audioscale;
  guchar *data;
  gulong size;
  gint i;
  double outrate;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (_data)) {
    GstEvent *e = GST_EVENT (_data);

    switch (GST_EVENT_TYPE (e)) {
      case GST_EVENT_DISCONTINUOUS:{
        gint64 new_off = 0;

        if (!audioscale->gst_resample) {
          GST_LOG ("Discont before negotiation took place - ignoring");
        } else if (gst_event_discont_get_value (e, GST_FORMAT_TIME, &new_off)) {
          /* time -> out-sample */
          new_off = new_off * audioscale->gst_resample->o_rate / GST_SECOND;
        } else if (gst_event_discont_get_value (e,
                GST_FORMAT_DEFAULT, &new_off)) {
          /* in-sample -> out-sample */
          new_off *= audioscale->gst_resample->o_rate;
          new_off /= audioscale->gst_resample->i_rate;
        } else if (gst_event_discont_get_value (e, GST_FORMAT_BYTES, &new_off)) {
          new_off /= audioscale->gst_resample->channels;
          new_off /=
              (audioscale->gst_resample->format == GST_RESAMPLE_S16) ? 2 : 4;
          new_off *= audioscale->gst_resample->o_rate;
          new_off /= audioscale->gst_resample->i_rate;
        } else {
          /* *sigh* */
          GST_DEBUG ("Discont without value - ignoring");
        }
        audioscale->gst_resample_offset = new_off;
        /* fall-through */
      }
      default:
        gst_pad_event_default (pad, e);
        break;
    }
    return;
  } else if (GST_BUFFER_TIMESTAMP_IS_VALID (buf) && audioscale->gst_resample) {
    /* update time for out-sample */
    audioscale->gst_resample_offset = GST_BUFFER_TIMESTAMP (buf) *
        audioscale->gst_resample->o_rate / GST_SECOND;
  }

  if (audioscale->passthru && audioscale->num_iterations == 0) {
    gst_pad_push (audioscale->srcpad, GST_DATA (buf));
    return;
  }

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  outduration = GST_BUFFER_DURATION (buf);

  GST_DEBUG ("gst_audioscale_chain: got buffer of %ld bytes in '%s'\n",
      size, gst_element_get_name (GST_ELEMENT (audioscale)));

  tempbuf = buf;
  outrate = audioscale->gst_resample_template.i_rate;
  if (audioscale->increase && !audioscale->passthru) {
    GST_DEBUG ("doing gstresample");
    gst_resample_scale (audioscale->gst_resample, data, size);
    tempbuf = audioscale->outbuf;
    gst_buffer_unref (buf);
    outrate = audioscale->gst_resample->o_rate;
  }
  for (i = 0; i < audioscale->num_iterations; i++) {
    tempbuf2 = tempbuf;
    GST_DEBUG ("doing %s",
        audioscale->
        increase ? "gst_audioscale_increase_rate" :
        "gst_audioscale_decrease_rate");

    if (audioscale->increase) {
      outrate *= 2;
      tempbuf = gst_audioscale_increase_rate (audioscale, tempbuf, outrate, i);
    } else {
      outrate /= 2;
      tempbuf = gst_audioscale_decrease_rate (audioscale, tempbuf, outrate, i);
    }

    gst_buffer_unref (tempbuf2);
    data = GST_BUFFER_DATA (tempbuf);
    size = GST_BUFFER_SIZE (tempbuf);
  }
  if (!audioscale->increase && !audioscale->passthru) {
    gst_resample_scale (audioscale->gst_resample, data, size);
    gst_buffer_unref (tempbuf);
    tempbuf = audioscale->outbuf;
  }
  GST_BUFFER_DURATION (tempbuf) = outduration;
  gst_pad_push (audioscale->srcpad, GST_DATA (tempbuf));

}

static GstStateChangeReturn
gst_audioscale_change_state (GstElement * element, GstStateChange transition)
{
  Audioscale *audioscale = GST_AUDIOSCALE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      audioscale->gst_resample_offset = 0;
      break;
    default:
      break;
  }

  return parent_class->change_state (element, transition);
}

static void
gst_audioscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Audioscale *src;
  gst_resample_t *r;

  g_return_if_fail (GST_IS_AUDIOSCALE (object));
  src = GST_AUDIOSCALE (object);
  r = &(src->gst_resample_template);

  switch (prop_id) {
    case ARG_FILTERLEN:
      r->filter_length = g_value_get_int (value);
      GST_DEBUG_OBJECT (GST_ELEMENT (src), "new filter length %d\n",
          r->filter_length);
      break;
    case ARG_METHOD:
      r->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_resample_reinit (r);
}

static void
gst_audioscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Audioscale *src;
  gst_resample_t *r;

  src = GST_AUDIOSCALE (object);
  r = &(src->gst_resample_template);

  switch (prop_id) {
    case ARG_FILTERLEN:
      g_value_set_int (value, r->filter_length);
      break;
    case ARG_METHOD:
      g_value_set_enum (value, r->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "audioscale", GST_RANK_SECONDARY,
          GST_TYPE_AUDIOSCALE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audioscale",
    "Resamples audio", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
