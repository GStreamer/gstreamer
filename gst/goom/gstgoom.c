/* gstgoom.c: implementation of goom drawing element
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
#include <gst/bytestream/adapter.h>
#include "goom_core.h"

GST_DEBUG_CATEGORY_STATIC (goom_debug);
#define GST_CAT_DEFAULT goom_debug

#define GOOM_SAMPLES 512

#define GST_TYPE_GOOM (gst_goom_get_type())
#define GST_GOOM(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GOOM,GstGOOM))
#define GST_GOOM_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GOOM,GstGOOM))
#define GST_IS_GOOM(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GOOM))
#define GST_IS_GOOM_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GOOM))

typedef struct _GstGOOM GstGOOM;
typedef struct _GstGOOMClass GstGOOMClass;

struct _GstGOOM
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;
  GstAdapter *adapter;

  /* input tracking */
  gint sample_rate;

  gint16 datain[2][GOOM_SAMPLES];
  /* the timestamp of the next frame */
  GstClockTime audio_basetime;
  guint64 samples_consumed;

  /* video state */
  gdouble fps;
  gint width;
  gint height;
  gint channels;
  gboolean srcnegotiated;

  gboolean disposed;
};

struct _GstGOOMClass
{
  GstElementClass parent_class;
};

GType gst_goom_get_type (void);


/* elementfactory information */
static GstElementDetails gst_goom_details = {
  "GOOM: what a GOOM!",
  "Visualization",
  "Takes frames of data and outputs video frames using the GOOM filter",
  "Wim Taymans <wim.taymans@chello.be>"
};

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",    /* the name of the pads */
    GST_PAD_SINK,               /* type of the pad */
    GST_PAD_ALWAYS,             /* ALWAYS/SOMETIMES */
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );


static void gst_goom_class_init (GstGOOMClass * klass);
static void gst_goom_base_init (GstGOOMClass * klass);
static void gst_goom_init (GstGOOM * goom);
static void gst_goom_dispose (GObject * object);

static GstElementStateReturn gst_goom_change_state (GstElement * element);

static void gst_goom_chain (GstPad * pad, GstData * _data);

static GstPadLinkReturn gst_goom_sinkconnect (GstPad * pad,
    const GstCaps * caps);
static GstPadLinkReturn gst_goom_srcconnect (GstPad * pad,
    const GstCaps * caps);
static GstCaps *gst_goom_src_fixate (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

GType
gst_goom_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstGOOMClass),
      (GBaseInitFunc) gst_goom_base_init,
      NULL,
      (GClassInitFunc) gst_goom_class_init,
      NULL,
      NULL,
      sizeof (GstGOOM),
      0,
      (GInstanceInitFunc) gst_goom_init,
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstGOOM", &info, 0);
  }
  return type;
}

static void
gst_goom_base_init (GstGOOMClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &gst_goom_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_goom_class_init (GstGOOMClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_goom_dispose;

  gstelement_class->change_state = gst_goom_change_state;

  GST_DEBUG_CATEGORY_INIT (goom_debug, "goom", 0, "goom visualisation element");
}

static void
gst_goom_init (GstGOOM * goom)
{
  /* create the sink and src pads */
  goom->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  goom->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (goom), goom->sinkpad);
  gst_element_add_pad (GST_ELEMENT (goom), goom->srcpad);

  GST_FLAG_SET (goom, GST_ELEMENT_EVENT_AWARE);

  gst_pad_set_chain_function (goom->sinkpad, gst_goom_chain);
  gst_pad_set_link_function (goom->sinkpad, gst_goom_sinkconnect);

  gst_pad_set_link_function (goom->srcpad, gst_goom_srcconnect);
  gst_pad_set_fixate_function (goom->srcpad, gst_goom_src_fixate);

  goom->adapter = gst_adapter_new ();

  goom->width = 320;
  goom->height = 200;
  goom->fps = 25.;              /* desired frame rate */
  goom->channels = 0;
  goom->sample_rate = 0;
  goom->audio_basetime = GST_CLOCK_TIME_NONE;
  goom->samples_consumed = 0;
  goom->disposed = FALSE;

  /* set to something */
  goom_init (50, 50);
}

static void
gst_goom_dispose (GObject * object)
{
  GstGOOM *goom = GST_GOOM (object);

  if (!goom->disposed) {
    goom_close ();
    goom->disposed = TRUE;

    g_object_unref (goom->adapter);
    goom->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstPadLinkReturn
gst_goom_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstGOOM *goom;
  GstStructure *structure;

  goom = GST_GOOM (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &goom->channels);
  gst_structure_get_int (structure, "rate", &goom->sample_rate);
  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_goom_srcconnect (GstPad * pad, const GstCaps * caps)
{
  GstGOOM *goom;
  GstStructure *structure;

  goom = GST_GOOM (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &goom->width);
  gst_structure_get_int (structure, "height", &goom->height);
  gst_structure_get_double (structure, "framerate", &goom->fps);

  goom_set_resolution (goom->width, goom->height);
  goom->srcnegotiated = TRUE;

  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_goom_src_fixate (GstPad * pad, const GstCaps * caps)
{
  GstCaps *newcaps;
  GstStructure *structure;

  if (!gst_caps_is_simple (caps))
    return NULL;

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_caps_structure_fixate_field_nearest_int (structure, "width", 320)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_int (structure, "height", 240)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
          30.0)) {
    return newcaps;
  }

  /* failed to fixate */
  gst_caps_free (newcaps);
  return NULL;
}

static void
gst_goom_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *bufin = GST_BUFFER (_data);
  GstGOOM *goom;
  guint32 bytesperread;
  gint16 *data;
  gint samples_per_frame;

  goom = GST_GOOM (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (bufin)) {
    GstEvent *event = GST_EVENT (bufin);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
        gint64 value = 0;

        gst_event_discont_get_value (event, GST_FORMAT_TIME, &value);
        gst_adapter_clear (goom->adapter);
        goom->audio_basetime = value;
        goom->samples_consumed = 0;
        GST_DEBUG ("Got discont. Adjusting time to=%" G_GUINT64_FORMAT, value);
      }
      default:
        gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

  if (goom->channels == 0) {
    GST_ELEMENT_ERROR (goom, CORE, NEGOTIATION, (NULL),
        ("Format wasn't negotiated before chain function"));
    gst_buffer_unref (bufin);
    return;
  }

  if (!GST_PAD_IS_USABLE (goom->srcpad)) {
    gst_buffer_unref (bufin);
    return;
  }

  if (goom->audio_basetime == GST_CLOCK_TIME_NONE)
    goom->audio_basetime = GST_BUFFER_TIMESTAMP (bufin);

  if (goom->audio_basetime == GST_CLOCK_TIME_NONE)
    goom->audio_basetime = 0;

  bytesperread = GOOM_SAMPLES * goom->channels * sizeof (gint16);
  samples_per_frame = goom->sample_rate / goom->fps;
  data = (gint16 *) GST_BUFFER_DATA (bufin);

  gst_adapter_push (goom->adapter, bufin);

  GST_DEBUG ("Input buffer has %d samples, time=%" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (bufin) * sizeof (gint16) * goom->channels,
      GST_BUFFER_TIMESTAMP (bufin));

  /* Collect samples until we have enough for an output frame */
  while (gst_adapter_available (goom->adapter) > MAX (bytesperread,
          samples_per_frame * goom->channels * sizeof (gint16))) {
    const guint16 *data =
        (const guint16 *) gst_adapter_peek (goom->adapter, bytesperread);
    GstBuffer *bufout;
    guchar *out_frame;
    GstClockTimeDiff frame_duration = GST_SECOND / goom->fps;
    gint i;

    if (goom->channels == 2) {
      for (i = 0; i < GOOM_SAMPLES; i++) {
        goom->datain[0][i] = *data++;
        goom->datain[1][i] = *data++;
      }
    } else {
      for (i = 0; i < GOOM_SAMPLES; i++) {
        goom->datain[0][i] = *data;
        goom->datain[1][i] = *data++;
      }
    }

    bufout = gst_buffer_new_and_alloc (goom->width * goom->height * 4);
    GST_BUFFER_TIMESTAMP (bufout) =
        goom->audio_basetime +
        (GST_SECOND * goom->samples_consumed / goom->sample_rate);
    GST_BUFFER_DURATION (bufout) = frame_duration;
    GST_BUFFER_SIZE (bufout) = goom->width * goom->height * 4;

    out_frame = (guchar *) goom_update (goom->datain);
    memcpy (GST_BUFFER_DATA (bufout), out_frame, GST_BUFFER_SIZE (bufout));

    GST_DEBUG ("Pushing frame with time=%" G_GUINT64_FORMAT ", duration=%"
        G_GUINT64_FORMAT, GST_BUFFER_TIMESTAMP (bufout),
        GST_BUFFER_DURATION (bufout));
    gst_pad_push (goom->srcpad, GST_DATA (bufout));

    goom->samples_consumed += samples_per_frame;
    gst_adapter_flush (goom->adapter,
        samples_per_frame * goom->channels * sizeof (gint16));
  }
}

static GstElementStateReturn
gst_goom_change_state (GstElement * element)
{
  GstGOOM *goom = GST_GOOM (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    case GST_STATE_READY_TO_PAUSED:
      goom->audio_basetime = GST_CLOCK_TIME_NONE;
      goom->srcnegotiated = FALSE;
      gst_adapter_clear (goom->adapter);
      break;
    case GST_STATE_PAUSED_TO_READY:
      goom->channels = 0;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;
  return gst_element_register (plugin, "goom", GST_RANK_NONE, GST_TYPE_GOOM);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "goom",
    "GOOM visualization filter",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
