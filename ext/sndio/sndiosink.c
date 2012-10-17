/*
 * Copyright (C) <2008> Jacob Meuser <jakemsr@sdf.lonestar.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * SECTION:element-sndiosink
 * @see_also: #GstAutoAudioSink
 *
 * <refsect2>
 * <para>
 * This element outputs sound to a sound card using sndio.
 * </para>
 * <para>
 * Simple example pipeline that plays an Ogg/Vorbis file via sndio:
 * <programlisting>
 * gst-launch -v filesrc location=foo.ogg ! decodebin ! audioconvert ! audioresample ! sndiosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sndiosink.h"
#include <unistd.h>
#include <errno.h>

#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_EXTERN (gst_sndio_debug);
#define GST_CAT_DEFAULT gst_sndio_debug

enum
{
  PROP_0,
  PROP_HOST
};

static GstStaticPadTemplate sndio_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { 1234, 4321 }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) { 8, 16, 24, 32 }, "
        "rate = (int) [ 8000, 192000 ], " "channels = (int) [ 1, 16 ] ")
    );

static void gst_sndiosink_finalize (GObject * object);

static GstCaps *gst_sndiosink_getcaps (GstBaseSink * bsink);

static gboolean gst_sndiosink_open (GstAudioSink * asink);
static gboolean gst_sndiosink_close (GstAudioSink * asink);
static gboolean gst_sndiosink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_sndiosink_unprepare (GstAudioSink * asink);
static guint gst_sndiosink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_sndiosink_delay (GstAudioSink * asink);
static void gst_sndiosink_reset (GstAudioSink * asink);

static void gst_sndiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sndiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_sndiosink_cb (void *addr, int delta);

GST_BOILERPLATE (GstSndioSink, gst_sndiosink, GstAudioSink,
    GST_TYPE_AUDIO_SINK);

static void
gst_sndiosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class,
      "Sndio audio sink",
      "Sink/Audio",
      "Plays audio through sndio", "Jacob Meuser <jakemsr@sdf.lonestar.org>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sndio_sink_factory));
}

static void
gst_sndiosink_class_init (GstSndioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_sndiosink_finalize;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_sndiosink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_sndiosink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_sndiosink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_sndiosink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_sndiosink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_sndiosink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_sndiosink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_sndiosink_reset);

  gobject_class->set_property = gst_sndiosink_set_property;
  gobject_class->get_property = gst_sndiosink_get_property;

  /* default value is filled in the _init method */
  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "Device or socket sndio will access", NULL, G_PARAM_READWRITE));
}

static void
gst_sndiosink_init (GstSndioSink * sndiosink, GstSndioSinkClass * klass)
{
  sndiosink->hdl = NULL;
  sndiosink->host = g_strdup (g_getenv ("AUDIODEVICE"));
}

static void
gst_sndiosink_finalize (GObject * object)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (object);

  gst_caps_replace (&sndiosink->cur_caps, NULL);
  g_free (sndiosink->host);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_sndiosink_getcaps (GstBaseSink * bsink)
{
  GstSndioSink *sndiosink;

  sndiosink = GST_SNDIOSINK (bsink);

  /* no hdl, we're done with the template caps */
  if (sndiosink->cur_caps == NULL) {
    GST_LOG_OBJECT (sndiosink, "getcaps called, returning template caps");
    return NULL;
  }

  GST_LOG_OBJECT (sndiosink, "returning %" GST_PTR_FORMAT, sndiosink->cur_caps);

  return gst_caps_ref (sndiosink->cur_caps);
}

static gboolean
gst_sndiosink_open (GstAudioSink * asink)
{
  GstPadTemplate *pad_template;
  GstSndioSink *sndiosink;
  struct sio_par par;
  struct sio_cap cap;
  GArray *rates, *chans;
  GValue rates_v = { 0 };
  GValue chans_v = { 0 };
  GValue value = { 0 };
  struct sio_enc enc;
  struct sio_conf conf;
  int confs[SIO_NCONF];
  int rate, chan;
  int i, j, k;
  int nconfs;

  sndiosink = GST_SNDIOSINK (asink);

  GST_DEBUG_OBJECT (sndiosink, "open");

  /* conect */
  sndiosink->hdl = sio_open (sndiosink->host, SIO_PLAY, 0);

  if (sndiosink->hdl == NULL)
    goto couldnt_connect;

  /* Use sndio defaults as the only encodings, but get the supported
   * sample rates and number of channels.
   */

  if (!sio_getpar (sndiosink->hdl, &par))
    goto no_server_info;

  if (!sio_getcap (sndiosink->hdl, &cap))
    goto no_server_info;

  rates = g_array_new (FALSE, FALSE, sizeof (int));
  chans = g_array_new (FALSE, FALSE, sizeof (int));

  /* find confs that have the default encoding */
  nconfs = 0;
  for (i = 0; i < cap.nconf; i++) {
    for (j = 0; j < SIO_NENC; j++) {
      if (cap.confs[i].enc & (1 << j)) {
        enc = cap.enc[j];
        if (enc.bits == par.bits && enc.sig == par.sig && enc.le == par.le) {
          confs[nconfs] = i;
          nconfs++;
          break;
        }
      }
    }
  }

  /* find the rates and channels of the confs that have the default encoding */
  for (i = 0; i < nconfs; i++) {
    conf = cap.confs[confs[i]];
    /* rates */
    for (j = 0; j < SIO_NRATE; j++) {
      if (conf.rate & (1 << j)) {
        rate = cap.rate[j];
        for (k = 0; k < rates->len && rate; k++) {
          if (rate == g_array_index (rates, int, k))
              rate = 0;
        }
        /* add in ascending order */
        if (rate) {
          for (k = 0; k < rates->len; k++) {
            if (rate < g_array_index (rates, int, k))
            {
              g_array_insert_val (rates, k, rate);
              break;
            }
          }
          if (k == rates->len)
            g_array_append_val (rates, rate);
        }
      }
    }
    /* channels */
    for (j = 0; j < SIO_NCHAN; j++) {
      if (conf.pchan & (1 << j)) {
        chan = cap.pchan[j];
        for (k = 0; k < chans->len && chan; k++) {
          if (chan == g_array_index (chans, int, k))
              chan = 0;
        }
        /* add in ascending order */
        if (chan) {
          for (k = 0; k < chans->len; k++) {
            if (chan < g_array_index (chans, int, k))
            {
              g_array_insert_val (chans, k, chan);
              break;
            }
          }
          if (k == chans->len)
            g_array_append_val (chans, chan);
        }
      }
    }
  }
  /* not sure how this can happen, but it might */
  if (cap.nconf == 0) {
    g_array_append_val (rates, par.rate);
    g_array_append_val (chans, par.pchan);
  }

  g_value_init (&rates_v, GST_TYPE_LIST);
  g_value_init (&chans_v, GST_TYPE_LIST);
  g_value_init (&value, G_TYPE_INT);

  for (i = 0; i < rates->len; i++) {
    g_value_set_int (&value, g_array_index (rates, int, i));
    gst_value_list_append_value (&rates_v, &value);
  }
  for (i = 0; i < chans->len; i++) {
    g_value_set_int (&value, g_array_index (chans, int, i));
    gst_value_list_append_value (&chans_v, &value);
  }

  g_array_free (rates, TRUE);
  g_array_free (chans, TRUE);

  pad_template = gst_static_pad_template_get (&sndio_sink_factory);
  sndiosink->cur_caps =
      gst_caps_copy (gst_pad_template_get_caps (pad_template));
  gst_object_unref (pad_template);

  for (i = 0; i < sndiosink->cur_caps->structs->len; i++) {
    GstStructure *s;

    s = gst_caps_get_structure (sndiosink->cur_caps, i);
    gst_structure_set (s, "endianness", G_TYPE_INT, par.le ? 1234 : 4321, NULL);
    gst_structure_set (s, "signed", G_TYPE_BOOLEAN, par.sig ? TRUE : FALSE,
        NULL);
    gst_structure_set (s, "width", G_TYPE_INT, par.bits, NULL);
    // gst_structure_set (s, "depth", G_TYPE_INT, par.bps * 8, NULL); /* XXX */
    gst_structure_set_value (s, "rate", &rates_v);
    gst_structure_set_value (s, "channels", &chans_v);
  }

  return TRUE;

  /* ERRORS */
couldnt_connect:
  {
    GST_ELEMENT_ERROR (sndiosink, RESOURCE, OPEN_WRITE,
        (_("Could not establish connection to sndio")),
        ("can't open connection to sndio"));
    return FALSE;
  }
no_server_info:
  {
    GST_ELEMENT_ERROR (sndiosink, RESOURCE, OPEN_WRITE,
        (_("Failed to query sndio capabilities")),
        ("couldn't get sndio info!"));
    return FALSE;
  }
}

static gboolean
gst_sndiosink_close (GstAudioSink * asink)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (asink);

  GST_DEBUG_OBJECT (sndiosink, "close");

  gst_caps_replace (&sndiosink->cur_caps, NULL);
  sio_close (sndiosink->hdl);
  sndiosink->hdl = NULL;

  return TRUE;
}

static void
gst_sndiosink_cb (void *addr, int delta)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK ((GstAudioSink *) addr);

  sndiosink->realpos += delta;

  if (sndiosink->realpos >= sndiosink->playpos)
    sndiosink->latency = 0;
  else
    sndiosink->latency = sndiosink->playpos - sndiosink->realpos;
}

static gboolean
gst_sndiosink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (asink);
  struct sio_par par;
  int spec_bpf;

  GST_DEBUG_OBJECT (sndiosink, "prepare");

  sndiosink->playpos = sndiosink->realpos = sndiosink->latency = 0;

  sio_initpar (&par);
  par.sig = spec->sign;
  par.le = !spec->bigend;
  par.bits = spec->width;
  // par.bps = spec->depth / 8;  /* XXX */
  par.rate = spec->rate;
  par.pchan = spec->channels;

  spec_bpf = ((spec->width / 8) * spec->channels);

  par.appbufsz = (spec->segsize * spec->segtotal) / spec_bpf;

  if (!sio_setpar (sndiosink->hdl, &par))
    goto cannot_configure;

  sio_getpar (sndiosink->hdl, &par);

  spec->sign = par.sig;
  spec->bigend = !par.le;
  spec->width = par.bits;
  // spec->depth = par.bps * 8;  /* XXX */
  spec->rate = par.rate;
  spec->channels = par.pchan;

  sndiosink->bpf = par.bps * par.pchan;

  spec->segsize = par.round * par.pchan * par.bps;
  spec->segtotal = par.bufsz / par.round;

  /* FIXME: this is wrong for signed ints (and the
   * audioringbuffers should do it for us anyway) */
  spec->silence_sample[0] = 0;
  spec->silence_sample[1] = 0;
  spec->silence_sample[2] = 0;
  spec->silence_sample[3] = 0;

  sio_onmove (sndiosink->hdl, gst_sndiosink_cb, sndiosink);

  if (!sio_start (sndiosink->hdl))
    goto cannot_start;

  GST_INFO_OBJECT (sndiosink, "successfully opened connection to sndio");

  return TRUE;

  /* ERRORS */
cannot_configure:
  {
    GST_ELEMENT_ERROR (sndiosink, RESOURCE, OPEN_WRITE,
        (_("Could not configure sndio")), ("can't configure sndio"));
    return FALSE;
  }
cannot_start:
  {
    GST_ELEMENT_ERROR (sndiosink, RESOURCE, OPEN_WRITE,
        (_("Could not start sndio")), ("can't start sndio"));
    return FALSE;
  }
}

static gboolean
gst_sndiosink_unprepare (GstAudioSink * asink)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (asink);

  if (sndiosink->hdl == NULL)
    return TRUE;

  sio_stop (sndiosink->hdl);

  return TRUE;
}

static guint
gst_sndiosink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (asink);
  guint done;

  done = sio_write (sndiosink->hdl, data, length);

  if (done == 0)
    goto write_error;

  sndiosink->playpos += (done / sndiosink->bpf);

  data = (char *) data + done;

  return done;

  /* ERRORS */
write_error:
  {
    GST_ELEMENT_ERROR (sndiosink, RESOURCE, WRITE,
        ("Failed to write data to sndio"), GST_ERROR_SYSTEM);
    return 0;
  }
}

static guint
gst_sndiosink_delay (GstAudioSink * asink)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (asink);

  if (sndiosink->latency == (guint) - 1) {
    GST_WARNING_OBJECT (asink, "couldn't get latency");
    return 0;
  }

  GST_DEBUG_OBJECT (asink, "got latency: %u", sndiosink->latency);

  return sndiosink->latency;
}

static void
gst_sndiosink_reset (GstAudioSink * asink)
{
  /* no way to flush the buffers with sndio ? */

  GST_DEBUG_OBJECT (asink, "reset called");
}

static void
gst_sndiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (object);

  switch (prop_id) {
    case PROP_HOST:
      g_free (sndiosink->host);
      sndiosink->host = g_value_dup_string (value);
      break;
    default:
      break;
  }
}

static void
gst_sndiosink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSndioSink *sndiosink = GST_SNDIOSINK (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, sndiosink->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
