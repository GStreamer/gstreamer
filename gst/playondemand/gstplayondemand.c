/* GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstplayondemand.h"


#define GST_POD_MAX_PLAY_PTRS 128     /* maximum number of simultaneous plays */
#define GST_POD_NUM_MEASURES  8       /* default number of measures */
#define GST_POD_NUM_BEATS     16      /* default number of beats in a measure */
#define GST_POD_BUFPOOL_SIZE  4096    /* gstreamer buffer size to use if no
                                     bufferpool is available, must be divisible
                                     by sizeof(gfloat) */
#define GST_POD_BUFPOOL_NUM   6       /* number of buffers to allocate per chunk in
                                     sink buffer pool */
#define GST_POD_BUFFER_SIZE   882000  /* enough space for 5 seconds of 32-bit float
                                     audio at 44100 samples per second ... */

/* elementfactory information */
static GstElementDetails play_on_demand_details = {
  "Play On Demand",
  "Filter/Audio/Effect",
  "LGPL",
  "Plays a stream at specific times, or when it receives a signal",
  VERSION,
  "Leif Morgan Johnson <leif@ambient.2y.net>",
  "(C) 2001",
};


/* Filter signals and args */
enum {
  /* FILL ME */
  PLAY_SIGNAL,
  RESET_SIGNAL,
  LAST_SIGNAL
};

static guint gst_pod_filter_signals[LAST_SIGNAL] = { 0 };

enum {
  PROP_0,
  PROP_SILENT,
  PROP_PLAYFROMBEGINNING,
  PROP_BUFFERSIZE,
  PROP_NUM_BEATS,
  PROP_NUM_MEASURES
};

static GstPadTemplate*
play_on_demand_sink_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new
      ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_append(gst_caps_new ("sink_int",  "audio/raw",
                                    GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
                      gst_caps_new ("sink_float", "audio/raw",
                                    GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS)),
      NULL);
  }
  return template;
}

static GstPadTemplate*
play_on_demand_src_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new
      ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
       gst_caps_append (gst_caps_new ("src_float", "audio/raw",
                                      GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS),
                        gst_caps_new ("src_int", "audio/raw",
                                      GST_AUDIO_INT_PAD_TEMPLATE_PROPS)),
       NULL);

  return template;
}

static void play_on_demand_class_init   (GstPlayOnDemandClass *klass);
static void play_on_demand_init         (GstPlayOnDemand *filter);

static void play_on_demand_set_property (GObject *object,
                                         guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);
static void play_on_demand_get_property (GObject *object,
                                         guint prop_id,
                                         GValue *value,
                                         GParamSpec *pspec);

static GstPadLinkReturn play_on_demand_pad_connect (GstPad *pad, GstCaps *caps);

static void play_on_demand_loop         (GstElement *elem);

static void play_on_demand_set_clock    (GstElement *elem, GstClock *clock);

static void play_on_demand_play_handler  (GstElement *elem);
static void play_on_demand_add_play_ptr  (GstPlayOnDemand *filter, guint pos);
static void play_on_demand_reset_handler (GstElement *elem);

static void play_on_demand_update_plays_from_clock (GstPlayOnDemand *filter);

static GstElementClass *parent_class = NULL;

static GstBufferPool*
play_on_demand_get_bufferpool (GstPad *pad)
{
  GstPlayOnDemand *filter;

  filter = GST_PLAYONDEMAND(gst_pad_get_parent(pad));

  return gst_pad_get_bufferpool(filter->srcpad);
}

static GstPadLinkReturn
play_on_demand_pad_connect (GstPad *pad, GstCaps *caps)
{
  const gchar *format;
  GstPlayOnDemand *filter;

  g_return_val_if_fail(caps != NULL, GST_PAD_LINK_DELAYED);
  g_return_val_if_fail(pad  != NULL, GST_PAD_LINK_DELAYED);

  filter = GST_PLAYONDEMAND(GST_PAD_PARENT(pad));

  gst_caps_get_string(caps, "format", &format);

  gst_caps_get_int(caps, "rate",     &filter->rate);
  gst_caps_get_int(caps, "channels", &filter->channels);

  if (strcmp(format, "int") == 0) {
    filter->format     = GST_PLAYONDEMAND_FORMAT_INT;
    gst_caps_get_int     (caps, "width",      &filter->width);
    gst_caps_get_int     (caps, "depth",      &filter->depth);
    gst_caps_get_int     (caps, "law",        &filter->law);
    gst_caps_get_int     (caps, "endianness", &filter->endianness);
    gst_caps_get_boolean (caps, "signed",     &filter->is_signed);

    if (!filter->silent) {
      g_print ("PlayOnDemand : channels %d, rate %d\n",
               filter->channels, filter->rate);
      g_print ("PlayOnDemand : format int, bit width %d, endianness %d, signed %s\n",
               filter->width, filter->endianness, filter->is_signed ? "yes" : "no");
    }

    filter->buffer_samples = filter->buffer_size;
    filter->buffer_samples /= (filter->width) ? filter->width / 8 : 1;
    filter->buffer_samples /= (filter->channels) ? filter->channels : 1;
} else if (strcmp(format, "float") == 0) {
    filter->format     = GST_PLAYONDEMAND_FORMAT_FLOAT;
    gst_caps_get_string (caps, "layout",    &filter->layout);
    gst_caps_get_float  (caps, "intercept", &filter->intercept);
    gst_caps_get_float  (caps, "slope",     &filter->slope);

    if (!filter->silent) {
      g_print ("PlayOnDemand : channels %d, rate %d\n",
               filter->channels, filter->rate);
      g_print ("PlayOnDemand : format float, layout %s, intercept %f, slope %f\n",
               filter->layout, filter->intercept, filter->slope);
    }

    filter->buffer_samples = filter->buffer_size / sizeof(gfloat);
    filter->buffer_samples /= (filter->channels) ? filter->channels : 1;
  }

  if (GST_CAPS_IS_FIXED (caps))
    return gst_pad_try_set_caps (filter->srcpad, caps);
  return GST_PAD_LINK_DELAYED;
}

GType
gst_play_on_demand_get_type (void)
{
  static GType play_on_demand_type = 0;

  if (! play_on_demand_type) {
    static const GTypeInfo play_on_demand_info = {
      sizeof(GstPlayOnDemandClass),
      NULL,
      NULL,
      (GClassInitFunc) play_on_demand_class_init,
      NULL,
      NULL,
      sizeof(GstPlayOnDemand),
      0,
      (GInstanceInitFunc) play_on_demand_init,
    };
    play_on_demand_type = g_type_register_static(GST_TYPE_ELEMENT,
                                                 "GstPlayOnDemand",
                                                 &play_on_demand_info, 0);
  }
  return play_on_demand_type;
}

static void
play_on_demand_class_init (GstPlayOnDemandClass *klass)
{
  GObjectClass    *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class    = (GObjectClass *)    klass;
  gstelement_class = (GstElementClass *) klass;

  gst_pod_filter_signals[PLAY_SIGNAL] =
    g_signal_new("play",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstPlayOnDemandClass, play),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  gst_pod_filter_signals[RESET_SIGNAL] =
    g_signal_new("reset",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstPlayOnDemandClass, reset),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  klass->play  = play_on_demand_play_handler;
  klass->reset = play_on_demand_reset_handler;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), PROP_SILENT,
    g_param_spec_boolean("silent","silent","silent",
                         TRUE, G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), PROP_PLAYFROMBEGINNING,
    g_param_spec_boolean("play-from-beginning","play-from-beginning","play-from-beginning",
                         TRUE, G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), PROP_BUFFERSIZE,
    g_param_spec_uint("buffer-size","buffer-size","buffer-size",
                      0, G_MAXUINT - 1, GST_POD_BUFFER_SIZE, G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), PROP_NUM_BEATS,
    g_param_spec_uint("num-beats","num-beats","num-beats",
                      0, G_MAXUINT - 1, GST_POD_NUM_BEATS, G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), PROP_NUM_MEASURES,
    g_param_spec_uint("num-measures","num-measures","num-measures",
                      0, G_MAXUINT - 1, GST_POD_NUM_MEASURES, G_PARAM_READWRITE));

  gobject_class->set_property = play_on_demand_set_property;
  gobject_class->get_property = play_on_demand_get_property;

  gstelement_class->set_clock = play_on_demand_set_clock;
}

static void
play_on_demand_init (GstPlayOnDemand *filter)
{
  guint i;

  filter->srcpad = gst_pad_new_from_template(play_on_demand_src_factory(), "src");
  filter->sinkpad = gst_pad_new_from_template(play_on_demand_sink_factory(), "sink");

  gst_pad_set_bufferpool_function(filter->sinkpad, play_on_demand_get_bufferpool);
  gst_pad_set_link_function(filter->sinkpad, play_on_demand_pad_connect);

  gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

  gst_element_set_loop_function(GST_ELEMENT(filter), play_on_demand_loop);

  filter->buffer      = g_new(gchar, GST_POD_BUFFER_SIZE);
  filter->buffer_size = GST_POD_BUFFER_SIZE;
  filter->start       = 0;
  filter->write       = 0;

  filter->eos                 = FALSE;
  filter->buffer_filled_once  = FALSE;
  filter->play_from_beginning = TRUE;
  filter->silent              = TRUE;

  filter->clock       = NULL;
  filter->last_time   = 0;

  filter->num_beats    = GST_POD_NUM_BEATS;
  filter->num_measures = GST_POD_NUM_MEASURES;
  filter->total_beats  = filter->num_beats * filter->num_measures;
  filter->times = g_new(guint64, filter->num_measures);
  for (i = 0; i < filter->num_measures; i++) {
    filter->times[i] = 0;
  }

  /* the plays are stored as an array of buffer offsets. this initializes the
     array to `blank' values (G_MAXUINT is the `invalid' index). */
  filter->plays  = g_new(guint, GST_POD_MAX_PLAY_PTRS);
  for (i = 0; i < GST_POD_MAX_PLAY_PTRS; i++) {
    filter->plays[i] = G_MAXUINT;
  }
}

static void
play_on_demand_loop (GstElement *elem)
{
  GstPlayOnDemand *filter = GST_PLAYONDEMAND(elem);
  guint            num_in, num_out, num_filter, max_filter;
  GstBuffer       *in, *out;
  register guint   j, k, t;
  guint            w, offset;

  g_return_if_fail(filter != NULL);
  g_return_if_fail(GST_IS_PLAYONDEMAND(filter));

  filter->bufpool = gst_pad_get_bufferpool(filter->srcpad);

  if (filter->bufpool == NULL) {
    filter->bufpool = gst_buffer_pool_get_default(GST_POD_BUFPOOL_SIZE,
                                                  GST_POD_BUFPOOL_NUM);
  }

  in = gst_pad_pull(filter->sinkpad);

  if (filter->format == GST_PLAYONDEMAND_FORMAT_INT) {
    if (filter->width == 16) {
      gint16 min = -32768;
      gint16 max = 32767;
      gint16 zero = 0;
#define _TYPE_ gint16
#include "filter.func"
#undef _TYPE_
    } else if (filter->width == 8) {
      gint8 min = -128;
      gint8 max = 127;
      gint8 zero = 0;
#define _TYPE_ gint8
#include "filter.func"
#undef _TYPE_
    }
  } else if (filter->format == GST_PLAYONDEMAND_FORMAT_FLOAT) {
    gfloat min = -1.0;
    gfloat max = 1.0;
    gfloat zero = 0.0;
#define _TYPE_ gfloat
#include "filter.func"
#undef _TYPE_
  }
}

static void
play_on_demand_set_clock (GstElement *elem, GstClock *clock)
{
  GstPlayOnDemand *filter;
  g_return_if_fail(GST_IS_PLAYONDEMAND(elem));
  filter = GST_PLAYONDEMAND(elem);
  g_return_if_fail(filter != NULL);

  filter->clock = clock;
}

static void
play_on_demand_play_handler (GstElement *elem)
{
  GstPlayOnDemand *filter;

  g_return_if_fail(GST_IS_PLAYONDEMAND(elem));
  filter = GST_PLAYONDEMAND(elem);
  g_return_if_fail(filter != NULL);

  play_on_demand_add_play_ptr(filter, filter->start);
}

static void
play_on_demand_add_play_ptr (GstPlayOnDemand *filter, guint pos)
{
  register guint i;

  for (i = 0; i < GST_POD_MAX_PLAY_PTRS; i++) {
    if (filter->plays[i] == G_MAXUINT) {
      filter->plays[i] = pos;
      return;
    }
  }
}

static void
play_on_demand_reset_handler(GstElement *elem)
{
  GstPlayOnDemand *filter;
  register guint i;

  g_return_if_fail(GST_IS_PLAYONDEMAND(elem));
  filter = GST_PLAYONDEMAND(elem);
  g_return_if_fail(filter != NULL);

  for (i = 0; i < GST_POD_MAX_PLAY_PTRS; i++) {
    filter->plays[i] = G_MAXUINT;
  }

  filter->start = 0;
  filter->write = 0;
  filter->eos = FALSE;
  filter->buffer_filled_once = FALSE;

  for (i = 0; i < filter->num_measures; i++) {
    filter->times[i] = 0;
  }
}

#define GST_POD_SAMPLE_OFFSET(f, dt) (((f)->start - ((dt) / (f)->rate)) % (f)->buffer_samples)

static void
play_on_demand_update_plays_from_clock(GstPlayOnDemand *filter)
{
  register guint t;
  guint          total, beats, last, time;

  g_return_if_fail(GST_IS_PLAYONDEMAND(filter));
  g_return_if_fail(filter != NULL);

  if (filter->clock) {
    total = filter->total_beats;
    beats = filter->num_beats;

    last = filter->last_time;
    time = (guint) ((gst_clock_get_time(filter->clock) / 10000000LL) % total);
    filter->last_time = time;

    GST_DEBUG(0, "--- clock time %u, last %u, total %u", time, last, total);

    /* if the current time is less than the last time, the clock has wrapped
       around the total number of beats ... we need to count back to 0 and then
       wrap around to the end. */
    if (time < last) {
      for (t = time; t != G_MAXUINT; t--) {
        if (filter->times[t / beats] & ((guint64) 1 << (t % beats))) {
          play_on_demand_add_play_ptr(filter,
                                      GST_POD_SAMPLE_OFFSET(filter, time - t));
        }
      }

      time = total - 1;
    }

    for (t = time; t > last; t--) {
      if (filter->times[t / beats] & ((guint64) 1 << (t % beats))) {
        play_on_demand_add_play_ptr(filter,
                                    GST_POD_SAMPLE_OFFSET(filter, time - t));
      }
    }
  }
}

void
gst_play_on_demand_set_beat(GstPlayOnDemand *filter, const guint measure,
                            const guint beat, const gboolean value)
{
  g_return_if_fail(GST_IS_PLAYONDEMAND(filter));
  g_return_if_fail(filter != NULL);
  g_return_if_fail(filter->num_measures > measure);
  g_return_if_fail(filter->total_beats < (filter->num_beats * measure + beat));

  if (value) {
    filter->times[measure] |= (1 << beat);
  } else {
    filter->times[measure] &= (((guint64) -1) ^ (1 << beat));
  }
}

gboolean
gst_play_on_demand_get_beat(GstPlayOnDemand *filter, const guint measure,
                            const guint beat)
{
  g_return_val_if_fail(GST_IS_PLAYONDEMAND(filter), FALSE);
  g_return_val_if_fail(filter != NULL, FALSE);
  g_return_val_if_fail(filter->num_measures > measure, FALSE);
  g_return_val_if_fail(filter->total_beats >
                       (filter->num_beats * measure + beat), FALSE);

  return ((filter->times[measure] >> beat) & ((guint64) 1));
}

void
gst_play_on_demand_toggle_beat(GstPlayOnDemand *filter, const guint measure,
                               const guint beat)
{
  g_return_if_fail(GST_IS_PLAYONDEMAND(filter));
  g_return_if_fail(filter != NULL);
  g_return_if_fail(filter->num_measures > measure);
  g_return_if_fail(filter->total_beats > (filter->num_beats * measure + beat));

  filter->times[measure] ^= (1 << beat);
}

static void
play_on_demand_set_property (GObject *object, guint prop_id,
                             const GValue *value, GParamSpec *pspec)
{
  GstPlayOnDemand *filter;
  register guchar  c;
  register guint   i;
  gchar           *new_buffer;
  guint64         *new_measures;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PLAYONDEMAND(object));
  filter = GST_PLAYONDEMAND(object);
  g_return_if_fail(filter != NULL);

  switch (prop_id) {
  case PROP_BUFFERSIZE:
    filter->buffer_size = g_value_get_uint(value);

    if (filter->format == GST_PLAYONDEMAND_FORMAT_FLOAT) {
      filter->buffer_samples = filter->buffer_size \
        / sizeof(gfloat) / filter->channels;
    } else {
      filter->buffer_samples = filter->buffer_size \
        / filter->width / filter->channels;
    }

    /* allocate space for a new buffer, copy old data, remove invalid play
       pointers. */
    new_buffer = g_new(gchar, filter->buffer_size);
    for (c = 0; c < filter->buffer_size; c++) {
      new_buffer[c] = filter->buffer[c];
    }

    g_free(filter->buffer);
    filter->buffer = new_buffer;

    for (i = 0; i < GST_POD_MAX_PLAY_PTRS; i++) {
      if (filter->plays[i] > filter->buffer_size) {
        filter->plays[i] = G_MAXUINT;
      }
    }
    break;
  case PROP_NUM_BEATS:
    filter->num_beats = g_value_get_uint(value);
    filter->total_beats = filter->num_measures * filter->num_beats;
    break;
  case PROP_NUM_MEASURES:
    filter->num_measures = g_value_get_uint(value);
    filter->total_beats = filter->num_measures * filter->num_beats;

    /* reallocate space for beat information, copy old data. this will remove
       measures at the end if the number of measures shrinks. */
    new_measures = g_new(guint64, filter->num_measures);
    for (i = 0; i < filter->num_measures; i++) {
      new_measures[i] = filter->times[i];
    }

    g_free(filter->times);
    filter->times = new_measures;
    break;
  case PROP_SILENT:
    filter->silent = g_value_get_boolean(value);
    break;
  case PROP_PLAYFROMBEGINNING:
    filter->play_from_beginning = g_value_get_boolean(value);
    play_on_demand_reset_handler(GST_ELEMENT(filter));
    break;
  default:
    break;
  }
}

static void
play_on_demand_get_property (GObject *object, guint prop_id,
                             GValue *value, GParamSpec *pspec)
{
  GstPlayOnDemand *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_PLAYONDEMAND(object));
  filter = GST_PLAYONDEMAND(object);
  g_return_if_fail(filter != NULL);

  switch (prop_id) {
  case PROP_BUFFERSIZE:
    g_value_set_uint(value, filter->buffer_size);
    break;
  case PROP_SILENT:
    g_value_set_boolean(value, filter->silent);
    break;
  case PROP_PLAYFROMBEGINNING:
    g_value_set_boolean(value, filter->play_from_beginning);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("playondemand",
                                   GST_TYPE_PLAYONDEMAND,
                                   &play_on_demand_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template(factory, play_on_demand_src_factory());
  gst_element_factory_add_pad_template(factory, play_on_demand_sink_factory());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE(factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "playondemand",
  plugin_init
};
