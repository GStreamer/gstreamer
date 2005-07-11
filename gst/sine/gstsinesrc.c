/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstsinesrc.c: 
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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <gst/control/control.h>

#include "gstsinesrc.h"

/* elementfactory information */
GstElementDetails gst_sinesrc_details = {
  "Sine-wave src",
  "Source/Audio",
  "Create a sine wave of a given frequency and volume",
  "Erik Walthinsen <omega@cse.ogi.edu>"
};


/* SineSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_TABLESIZE,
  ARG_SAMPLES_PER_BUFFER,
  ARG_FREQ,
  ARG_VOLUME,
  ARG_SYNC,
  ARG_TIMESTAMP_OFFSET,
};

static GstStaticPadTemplate gst_sinesrc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) [ 1, MAX ], " "channels = (int) 1")
    );

static void gst_sinesrc_class_init (GstSineSrcClass * klass);
static void gst_sinesrc_base_init (GstSineSrcClass * klass);
static void gst_sinesrc_init (GstSineSrc * src);
static void gst_sinesrc_dispose (GObject * object);

static void gst_sinesrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_sinesrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_sinesrc_setcaps (GstBaseSrc * basesrc, GstCaps * caps);
static void gst_sinesrc_src_fixate (GstPad * pad, GstCaps * caps);

static void gst_sinesrc_update_freq (const GValue * value, gpointer data);
static void gst_sinesrc_populate_sinetable (GstSineSrc * src);
static inline void gst_sinesrc_update_table_inc (GstSineSrc * src);

static const GstQueryType *gst_sinesrc_get_query_types (GstPad * pad);
static gboolean gst_sinesrc_src_query (GstPad * pad, GstQuery * query);

static GstFlowReturn gst_sinesrc_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer);
static gboolean gst_sinesrc_start (GstBaseSrc * basesrc);

static GstElementClass *parent_class = NULL;

/*static guint gst_sinesrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_sinesrc_get_type (void)
{
  static GType sinesrc_type = 0;

  if (!sinesrc_type) {
    static const GTypeInfo sinesrc_info = {
      sizeof (GstSineSrcClass),
      (GBaseInitFunc) gst_sinesrc_base_init, NULL,
      (GClassInitFunc) gst_sinesrc_class_init, NULL, NULL,
      sizeof (GstSineSrc), 0,
      (GInstanceInitFunc) gst_sinesrc_init,
    };

    sinesrc_type = g_type_register_static (GST_TYPE_BASE_SRC, "GstSineSrc",
        &sinesrc_info, 0);
  }
  return sinesrc_type;
}

static void
gst_sinesrc_base_init (GstSineSrcClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sinesrc_src_template));
  gst_element_class_set_details (element_class, &gst_sinesrc_details);
}

static void
gst_sinesrc_class_init (GstSineSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_SRC);

  gobject_class->set_property = gst_sinesrc_set_property;
  gobject_class->get_property = gst_sinesrc_get_property;
  gobject_class->dispose = gst_sinesrc_dispose;

  g_object_class_install_property (gobject_class, ARG_TABLESIZE,
      g_param_spec_int ("tablesize", "tablesize", "tablesize",
          1, G_MAXINT, 1024, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_SAMPLES_PER_BUFFER,
      g_param_spec_int ("samplesperbuffer", "Samples per buffer",
          "Number of samples in each outgoing buffer",
          1, G_MAXINT, 1024, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_FREQ,
      g_param_spec_double ("freq", "Frequency", "Frequency of sine source",
          0.0, 20000.0, 440.0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume",
          0.0, 1.0, 0.8, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Synchronize to clock",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TIMESTAMP_OFFSET,
      g_param_spec_int64 ("timestamp-offset", "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE));



  //gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR ();
  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_sinesrc_setcaps);
  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_sinesrc_start);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_sinesrc_create);
}

static void
gst_sinesrc_init (GstSineSrc * src)
{
  src->srcpad = GST_BASE_SRC (src)->srcpad;

  gst_pad_set_fixatecaps_function (src->srcpad, gst_sinesrc_src_fixate);
  gst_pad_set_query_function (src->srcpad, gst_sinesrc_src_query);
  gst_pad_set_query_type_function (src->srcpad, gst_sinesrc_get_query_types);

  src->samplerate = 44100;
  src->volume = 1.0;
  src->freq = 440.0;
  src->sync = FALSE;

  src->table_pos = 0.0;
  src->table_size = 1024;
  src->samples_per_buffer = 1024;
  src->timestamp = G_GINT64_CONSTANT (0);
  src->offset = G_GINT64_CONSTANT (0);
  src->timestamp_offset = G_GINT64_CONSTANT (0);

  src->seq = 0;

  src->dpman = gst_dpman_new ("sinesrc_dpman", GST_ELEMENT (src));

  gst_dpman_add_required_dparam_callback (src->dpman,
      g_param_spec_double ("freq", "Frequency (Hz)", "Frequency of the tone",
          10.0, 10000.0, 350.0, G_PARAM_READWRITE),
      "hertz", gst_sinesrc_update_freq, src);

  gst_dpman_add_required_dparam_direct (src->dpman,
      g_param_spec_double ("volume", "Volume", "Volume of the tone",
          0.0, 1.0, 0.8, G_PARAM_READWRITE), "scalar", &(src->volume)
      );

  gst_dpman_set_rate (src->dpman, src->samplerate);

  gst_sinesrc_populate_sinetable (src);
  gst_sinesrc_update_table_inc (src);

}

static void
gst_sinesrc_dispose (GObject * object)
{
  GstSineSrc *sinesrc = GST_SINESRC (object);

  g_free (sinesrc->table_data);
  sinesrc->table_data = NULL;

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_sinesrc_src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_caps_structure_fixate_field_nearest_int (structure, "rate", 44100);
}

static gboolean
gst_sinesrc_setcaps (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstSineSrc *sinesrc;
  const GstStructure *structure;
  gboolean ret;

  sinesrc = GST_SINESRC (basesrc);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &sinesrc->samplerate);

  return ret;
}

static const GstQueryType *
gst_sinesrc_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0,
  };

  return query_types;
}

static gboolean
gst_sinesrc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstSineSrc *src;

  src = GST_SINESRC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          current = src->timestamp;
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:       /* samples */
          current = src->offset / 2;    /* 16bpp audio */
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          current = src->offset;
          res = TRUE;
          break;
        default:
          break;

          if (res) {
            gst_query_set_position (query, format, current, -1);
          }
      }
      break;
    }
    default:
      break;
  }

  return res;
}

static GstFlowReturn
gst_sinesrc_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstSineSrc *src;
  GstBuffer *buf;
  guint tdiff;

  gint16 *samples;
  gint i = 0;

  src = GST_SINESRC (basesrc);

  if (!src->tags_pushed) {
    GstTagList *taglist;
    GstEvent *event;

    taglist = gst_tag_list_new ();

    gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
        GST_TAG_DESCRIPTION, "sine wave", NULL);

    event = gst_event_new_tag (taglist);
    gst_pad_push_event (basesrc->srcpad, event);
    src->tags_pushed = TRUE;
  }

  /* not negotiated, fixate and set */
  if (GST_PAD_CAPS (basesrc->srcpad) == NULL) {
    GstCaps *caps;

    /* see whatever we are allowed to do */
    caps = gst_pad_get_allowed_caps (basesrc->srcpad);
    /* fix unfixed values */
    gst_sinesrc_src_fixate (basesrc->srcpad, caps);
    /* and use those */
    gst_pad_set_caps (basesrc->srcpad, caps);
    gst_caps_unref (caps);
  }

  tdiff = src->samples_per_buffer * GST_SECOND / src->samplerate;

  /* note: the 2 is because of the format we use */
  buf = gst_buffer_new_and_alloc (src->samples_per_buffer * 2);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (basesrc->srcpad));

  GST_BUFFER_TIMESTAMP (buf) = src->timestamp + src->timestamp_offset;
  /* offset is the number of samples */
  GST_BUFFER_OFFSET (buf) = src->offset;
  GST_BUFFER_OFFSET_END (buf) = src->offset + src->samples_per_buffer;
  GST_BUFFER_DURATION (buf) = tdiff;

  samples = (gint16 *) GST_BUFFER_DATA (buf);

  GST_DPMAN_PREPROCESS (src->dpman, src->samples_per_buffer, src->timestamp);

  src->timestamp += tdiff;
  src->offset += src->samples_per_buffer;

  while (GST_DPMAN_PROCESS (src->dpman, i)) {
#if 0
    src->table_lookup = (gint) (src->table_pos);
    src->table_lookup_next = src->table_lookup + 1;
    src->table_interp = src->table_pos - src->table_lookup;

    /* wrap the array lookups if we're out of bounds */
    if (src->table_lookup_next >= src->table_size) {
      src->table_lookup_next -= src->table_size;
      if (src->table_lookup >= src->table_size) {
        src->table_lookup -= src->table_size;
        src->table_pos -= src->table_size;
      }
    }

    src->table_pos += src->table_inc;

    /*no interpolation */
    /*samples[i] = src->table_data[src->table_lookup] */
    /*               * src->volume * 32767.0; */

    /*linear interpolation */
    samples[i] = ((src->table_interp * (src->table_data[src->table_lookup_next]
                - src->table_data[src->table_lookup]
            )
        ) + src->table_data[src->table_lookup]
        ) * src->volume * 32767.0;
#endif
    src->accumulator += 2 * M_PI * src->freq / src->samplerate;
    if (src->accumulator >= 2 * M_PI) {
      src->accumulator -= 2 * M_PI;
    }
    samples[i] = sin (src->accumulator) * src->volume * 32767.0;

    i++;
  }

  *buffer = buf;

  return GST_FLOW_OK;
}

static void
gst_sinesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSineSrc *src;

  g_return_if_fail (GST_IS_SINESRC (object));
  src = GST_SINESRC (object);

  switch (prop_id) {
    case ARG_TABLESIZE:
      src->table_size = g_value_get_int (value);
      gst_sinesrc_populate_sinetable (src);
      gst_sinesrc_update_table_inc (src);
      break;
    case ARG_SAMPLES_PER_BUFFER:
      src->samples_per_buffer = g_value_get_int (value);
      break;
    case ARG_FREQ:
      gst_dpman_bypass_dparam (src->dpman, "freq");
      gst_sinesrc_update_freq (value, src);
      break;
    case ARG_VOLUME:
      gst_dpman_bypass_dparam (src->dpman, "volume");
      src->volume = g_value_get_double (value);
      break;
    case ARG_SYNC:
      src->sync = g_value_get_boolean (value);
      break;
    case ARG_TIMESTAMP_OFFSET:
      src->timestamp_offset = g_value_get_int64 (value);
      break;
    default:
      break;
  }
}

static void
gst_sinesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSineSrc *src;

  g_return_if_fail (GST_IS_SINESRC (object));
  src = GST_SINESRC (object);

  switch (prop_id) {
    case ARG_TABLESIZE:
      g_value_set_int (value, src->table_size);
      break;
    case ARG_SAMPLES_PER_BUFFER:
      g_value_set_int (value, src->samples_per_buffer);
      break;
    case ARG_FREQ:
      g_value_set_double (value, src->freq);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, src->volume);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, src->sync);
      break;
    case ARG_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->timestamp_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_sinesrc_start (GstBaseSrc * basesrc)
{
  GstSineSrc *src = GST_SINESRC (basesrc);

  src->timestamp = G_GINT64_CONSTANT (0);
  src->offset = G_GINT64_CONSTANT (0);

  return TRUE;
}

static void
gst_sinesrc_populate_sinetable (GstSineSrc * src)
{
  gint i;
  gdouble pi2scaled = M_PI * 2 / src->table_size;
  gdouble *table = g_new (gdouble, src->table_size);

  for (i = 0; i < src->table_size; i++) {
    table[i] = (gdouble) sin (i * pi2scaled);
  }

  g_free (src->table_data);
  src->table_data = table;
}

static void
gst_sinesrc_update_freq (const GValue * value, gpointer data)
{
  GstSineSrc *src = (GstSineSrc *) data;

  g_return_if_fail (GST_IS_SINESRC (src));

  src->freq = g_value_get_double (value);
  src->table_inc = src->table_size * src->freq / src->samplerate;

  GST_DEBUG ("freq %f", src->freq);
}

static inline void
gst_sinesrc_update_table_inc (GstSineSrc * src)
{
  src->table_inc = src->table_size * src->freq / src->samplerate;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* initialize dparam support library */
  gst_control_init (NULL, NULL);

  return gst_element_register (plugin, "sinesrc",
      GST_RANK_NONE, GST_TYPE_SINESRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sine",
    "Sine audio wave generator",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
