/* gstsynaesthesia.c: implementation of synaesthesia drawing element
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
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
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/bytestream/adapter.h>
#include "synaescope.h"

#define SYNAES_SAMPLES 512
#define SYNAES_WIDTH 320
#define SYNAES_HEIGHT 200

#define GST_TYPE_SYNAESTHESIA (gst_synaesthesia_get_type())
#define GST_SYNAESTHESIA(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SYNAESTHESIA,GstSynaesthesia))
#define GST_SYNAESTHESIA_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SYNAESTHESIA,GstSynaesthesia))
#define GST_IS_SYNAESTHESIA(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SYNAESTHESIA))
#define GST_IS_SYNAESTHESIA_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SYNAESTHESIA))

typedef struct _GstSynaesthesia GstSynaesthesia;
typedef struct _GstSynaesthesiaClass GstSynaesthesiaClass;

struct _GstSynaesthesia
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;
  GstAdapter *adapter;

  /* the timestamp of the next frame */
  guint64 audio_basetime;
  guint64 samples_consumed;
  gint16 datain[2][SYNAES_SAMPLES];

  /* video state */
  gdouble fps;
  gint width;
  gint height;
  gint channels;

  /* Audio state */
  gint sample_rate;
};

struct _GstSynaesthesiaClass
{
  GstElementClass parent_class;
};

GType gst_synaesthesia_get_type (void);


/* elementfactory information */
static const GstElementDetails gst_synaesthesia_details =
GST_ELEMENT_DETAILS ("Synaesthesia",
    "Visualization",
    "Creates video visualizations of audio input, using stereo and pitch information",
    "Richard Boulton <richard@tartarus.org>");

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static GstStaticPadTemplate gst_synaesthesia_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate gst_synaesthesia_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );


static void gst_synaesthesia_base_init (gpointer g_class);
static void gst_synaesthesia_class_init (GstSynaesthesiaClass * klass);
static void gst_synaesthesia_init (GstSynaesthesia * synaesthesia);
static void gst_synaesthesia_finalize (GObject * object);
static void gst_synaesthesia_dispose (GObject * object);

static void gst_synaesthesia_chain (GstPad * pad, GstData * _data);

static GstStateChangeReturn
gst_synaesthesia_change_state (GstElement * element, GstStateChange transition);

static GstCaps *gst_synaesthesia_src_getcaps (GstPad * pad);
static GstPadLinkReturn
gst_synaesthesia_src_link (GstPad * pad, const GstCaps * caps);
static GstPadLinkReturn
gst_synaesthesia_sink_link (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

GType
gst_synaesthesia_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstSynaesthesiaClass),
      gst_synaesthesia_base_init,
      NULL,
      (GClassInitFunc) gst_synaesthesia_class_init,
      NULL,
      NULL,
      sizeof (GstSynaesthesia),
      0,
      (GInstanceInitFunc) gst_synaesthesia_init,
    };

    type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSynaesthesia", &info, 0);
  }
  return type;
}

static void
gst_synaesthesia_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_synaesthesia_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_synaesthesia_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_synaesthesia_sink_template));
}

static void
gst_synaesthesia_class_init (GstSynaesthesiaClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_synaesthesia_change_state;

  gobject_class->dispose = gst_synaesthesia_dispose;
  gobject_class->finalize = gst_synaesthesia_finalize;
}

static void
gst_synaesthesia_init (GstSynaesthesia * synaesthesia)
{
  /* create the sink and src pads */
  synaesthesia->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_synaesthesia_sink_template), "sink");
  synaesthesia->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_synaesthesia_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->sinkpad);
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->srcpad);

  gst_pad_set_chain_function (synaesthesia->sinkpad, gst_synaesthesia_chain);
  gst_pad_set_link_function (synaesthesia->sinkpad, gst_synaesthesia_sink_link);

  gst_pad_set_getcaps_function (synaesthesia->srcpad,
      gst_synaesthesia_src_getcaps);
  gst_pad_set_link_function (synaesthesia->srcpad, gst_synaesthesia_src_link);

  GST_OBJECT_FLAG_SET (synaesthesia, GST_ELEMENT_EVENT_AWARE);

  synaesthesia->adapter = gst_adapter_new ();

  /* reset the initial video state */
  synaesthesia->width = SYNAES_WIDTH;
  synaesthesia->height = SYNAES_HEIGHT;
  synaesthesia->fps = 25.0;     /* desired frame rate */

  synaesthesia->sample_rate = 0;
  synaesthesia->channels = 2;

  synaesthesia->audio_basetime = GST_CLOCK_TIME_NONE;
  synaesthesia->samples_consumed = 0;

  synaesthesia_init (synaesthesia->width, synaesthesia->height);
}

static void
gst_synaesthesia_dispose (GObject * object)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (object);

  if (synaesthesia->adapter) {
    g_object_unref (synaesthesia->adapter);
    synaesthesia->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_synaesthesia_finalize (GObject * object)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (object);

  synaesthesia_close ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPadLinkReturn
gst_synaesthesia_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstSynaesthesia *synaesthesia;
  GstStructure *structure;
  gint channels;
  gint rate;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate)) {
    return GST_PAD_LINK_REFUSED;
  }

  if (synaesthesia->channels != 2)
    return GST_PAD_LINK_REFUSED;

  synaesthesia->sample_rate = rate;

  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_synaesthesia_src_getcaps (GstPad * pad)
{
  GstCaps *caps;
  const GstCaps *templcaps;
  gint i;

  templcaps = gst_pad_get_pad_template_caps (pad);
  caps = gst_caps_copy (templcaps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure, "width", G_TYPE_INT, SYNAES_WIDTH, "height",
        G_TYPE_INT, SYNAES_HEIGHT, NULL);
  }
  return caps;
}

static GstPadLinkReturn
gst_synaesthesia_src_link (GstPad * pad, const GstCaps * caps)
{
  GstSynaesthesia *synaesthesia;
  GstStructure *structure;
  gint w, h;
  gdouble fps;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &w) ||
      !gst_structure_get_int (structure, "height", &h) ||
      !gst_structure_get_double (structure, "framerate", &fps)) {
    return GST_PAD_LINK_REFUSED;
  }

  if ((w != SYNAES_WIDTH) || (h != SYNAES_HEIGHT))
    return GST_PAD_LINK_REFUSED;

  synaesthesia->width = w;
  synaesthesia->height = h;
  synaesthesia->fps = fps;

  synaesthesia_init (synaesthesia->width, synaesthesia->height);

  return GST_PAD_LINK_OK;
}

static void
gst_synaesthesia_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *bufin = GST_BUFFER (_data);
  GstSynaesthesia *synaesthesia;
  guint32 bytesperread;
  gint samples_per_frame;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));

  GST_DEBUG ("Synaesthesia: chainfunc called");

  if (GST_IS_EVENT (bufin)) {
    GstEvent *event = GST_EVENT (bufin);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
        gint64 value = 0;

        gst_event_discont_get_value (event, GST_FORMAT_TIME, &value);
        synaesthesia->audio_basetime = value;
        synaesthesia->samples_consumed = 0;
        gst_adapter_clear (synaesthesia->adapter);
      }
      default:
        gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

  if (!GST_PAD_IS_USABLE (synaesthesia->srcpad)) {
    gst_buffer_unref (bufin);
    return;
  }

  if (synaesthesia->audio_basetime == GST_CLOCK_TIME_NONE)
    synaesthesia->audio_basetime = GST_BUFFER_TIMESTAMP (bufin);

  if (synaesthesia->audio_basetime == GST_CLOCK_TIME_NONE)
    synaesthesia->audio_basetime = 0;

  bytesperread = SYNAES_SAMPLES * synaesthesia->channels * sizeof (gint16);
  samples_per_frame = synaesthesia->sample_rate / synaesthesia->fps;

  gst_adapter_push (synaesthesia->adapter, bufin);

  while (gst_adapter_available (synaesthesia->adapter) >
      MAX (bytesperread,
          samples_per_frame * synaesthesia->channels * sizeof (gint16))) {
    const guint16 *data =
        (const guint16 *) gst_adapter_peek (synaesthesia->adapter,
        bytesperread);
    GstBuffer *bufout;
    guchar *out_frame;
    GstClockTimeDiff frame_duration = GST_SECOND / synaesthesia->fps;
    gint i;

    for (i = 0; i < SYNAES_SAMPLES; i++) {
      synaesthesia->datain[0][i] = *data++;
      synaesthesia->datain[1][i] = *data++;
    }

    bufout =
        gst_buffer_new_and_alloc (synaesthesia->width * synaesthesia->height *
        4);
    GST_BUFFER_TIMESTAMP (bufout) =
        synaesthesia->audio_basetime +
        (GST_SECOND * synaesthesia->samples_consumed /
        synaesthesia->sample_rate);
    GST_BUFFER_DURATION (bufout) = frame_duration;
    GST_BUFFER_SIZE (bufout) = synaesthesia->width * synaesthesia->height * 4;

    out_frame = (guchar *) synaesthesia_update (synaesthesia->datain);
    memcpy (GST_BUFFER_DATA (bufout), out_frame, GST_BUFFER_SIZE (bufout));
    gst_pad_push (synaesthesia->srcpad, GST_DATA (bufout));

    synaesthesia->samples_consumed += samples_per_frame;
    gst_adapter_flush (synaesthesia->adapter, samples_per_frame *
        synaesthesia->channels * sizeof (gint16));
  }
}

static GstStateChangeReturn
gst_synaesthesia_change_state (GstElement * element, GstStateChange transition)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      synaesthesia->audio_basetime = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (synaesthesia->adapter);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  return gst_element_register (plugin, "synaesthesia", GST_RANK_NONE,
      GST_TYPE_SYNAESTHESIA);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "synaesthesia",
    "Creates video visualizations of audio input, using stereo and pitch information",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
