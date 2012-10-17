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
 * SECTION:element-sndiosrc
 * @see_also: #GstAutoAudioSrc
 *
 * <refsect2>
 * <para>
 * This element retrieves samples from a sound card using sndio.
 * </para>
 * <para>
 * Simple example pipeline that records an Ogg/Vorbis file via sndio:
 * <programlisting>
 * gst-launch -v sndiosrc ! audioconvert ! vorbisenc ! oggmux ! filesink location=foo.ogg 
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sndiosrc.h"
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

static GstStaticPadTemplate sndio_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { 1234, 4321 }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) { 8, 16, 24, 32 }, "
        "rate = (int) [ 8000, 192000 ], " "channels = (int) [ 1, 16 ] ")
    );

static void gst_sndiosrc_finalize (GObject * object);

static GstCaps *gst_sndiosrc_getcaps (GstBaseSrc * bsrc);

static gboolean gst_sndiosrc_open (GstAudioSrc * asrc);
static gboolean gst_sndiosrc_close (GstAudioSrc * asrc);
static gboolean gst_sndiosrc_prepare (GstAudioSrc * asrc,
    GstRingBufferSpec * spec);
static gboolean gst_sndiosrc_unprepare (GstAudioSrc * asrc);
static guint gst_sndiosrc_read (GstAudioSrc * asrc, gpointer data,
    guint length);
static guint gst_sndiosrc_delay (GstAudioSrc * asrc);
static void gst_sndiosrc_reset (GstAudioSrc * asrc);

static void gst_sndiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sndiosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_sndiosrc_cb (void *addr, int delta);

GST_BOILERPLATE (GstSndioSrc, gst_sndiosrc, GstAudioSrc, GST_TYPE_AUDIO_SRC);

static void
gst_sndiosrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class,
      "Sndio audio source",
      "Source/Audio",
      "Records audio through sndio", "Jacob Meuser <jakemsr@sdf.lonestar.org>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sndio_src_factory));
}

static void
gst_sndiosrc_class_init (GstSndioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstBaseAudioSrcClass *gstbaseaudiosrc_class;
  GstAudioSrcClass *gstaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;
  gstaudiosrc_class = (GstAudioSrcClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_sndiosrc_finalize;

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_sndiosrc_getcaps);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_sndiosrc_open);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_sndiosrc_close);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_sndiosrc_prepare);
  gstaudiosrc_class->unprepare = GST_DEBUG_FUNCPTR (gst_sndiosrc_unprepare);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_sndiosrc_read);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_sndiosrc_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_sndiosrc_reset);

  gobject_class->set_property = gst_sndiosrc_set_property;
  gobject_class->get_property = gst_sndiosrc_get_property;

  /* default value is filled in the _init method */
  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "Device or socket sndio will access", NULL, G_PARAM_READWRITE));
}

static void
gst_sndiosrc_init (GstSndioSrc * sndiosrc, GstSndioSrcClass * klass)
{
  sndiosrc->hdl = NULL;
  sndiosrc->host = g_strdup (g_getenv ("AUDIODEVICE"));
}

static void
gst_sndiosrc_finalize (GObject * object)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (object);

  gst_caps_replace (&sndiosrc->cur_caps, NULL);
  g_free (sndiosrc->host);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_sndiosrc_getcaps (GstBaseSrc * bsrc)
{
  GstSndioSrc *sndiosrc;

  sndiosrc = GST_SNDIOSRC (bsrc);

  /* no hdl, we're done with the template caps */
  if (sndiosrc->cur_caps == NULL) {
    GST_LOG_OBJECT (sndiosrc, "getcaps called, returning template caps");
    return NULL;
  }

  GST_LOG_OBJECT (sndiosrc, "returning %" GST_PTR_FORMAT, sndiosrc->cur_caps);

  return gst_caps_ref (sndiosrc->cur_caps);
}

static gboolean
gst_sndiosrc_open (GstAudioSrc * asrc)
{
  GstPadTemplate *pad_template;
  GstSndioSrc *sndiosrc;
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

  sndiosrc = GST_SNDIOSRC (asrc);

  GST_DEBUG_OBJECT (sndiosrc, "open");

  /* connect */
  sndiosrc->hdl = sio_open (sndiosrc->host, SIO_REC, 0);

  if (sndiosrc->hdl == NULL)
    goto couldnt_connect;

  /* Use sndio defaults as the only encodings, but get the supported
   * sample rates and number of channels.
   */

  if (!sio_getpar (sndiosrc->hdl, &par))
    goto no_server_info;

  if (!sio_getcap (sndiosrc->hdl, &cap))
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
      if (conf.rchan & (1 << j)) {
        chan = cap.rchan[j];
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
    g_array_append_val (chans, par.rchan);
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

  pad_template = gst_static_pad_template_get (&sndio_src_factory);
  sndiosrc->cur_caps = gst_caps_copy (gst_pad_template_get_caps (pad_template));
  gst_object_unref (pad_template);

  for (i = 0; i < sndiosrc->cur_caps->structs->len; i++) {
    GstStructure *s;

    s = gst_caps_get_structure (sndiosrc->cur_caps, i);
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
    GST_ELEMENT_ERROR (sndiosrc, RESOURCE, OPEN_READ,
        (_("Could not establish connection to sndio")),
        ("can't open connection to sndio"));
    return FALSE;
  }
no_server_info:
  {
    GST_ELEMENT_ERROR (sndiosrc, RESOURCE, OPEN_READ,
        (_("Failed to query sndio capabilities")),
        ("couldn't get sndio info!"));
    return FALSE;
  }
}

static gboolean
gst_sndiosrc_close (GstAudioSrc * asrc)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (asrc);

  GST_DEBUG_OBJECT (sndiosrc, "close");

  gst_caps_replace (&sndiosrc->cur_caps, NULL);
  sio_close (sndiosrc->hdl);
  sndiosrc->hdl = NULL;

  return TRUE;
}

static void
gst_sndiosrc_cb (void *addr, int delta)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC ((GstAudioSrc *) addr);

  sndiosrc->realpos += delta;

  if (sndiosrc->readpos >= sndiosrc->realpos)
    sndiosrc->latency = 0;
  else
    sndiosrc->latency = sndiosrc->realpos - sndiosrc->readpos;
}

static gboolean
gst_sndiosrc_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (asrc);
  struct sio_par par;
  int spec_bpf;

  GST_DEBUG_OBJECT (sndiosrc, "prepare");

  sndiosrc->readpos = sndiosrc->realpos = sndiosrc->latency = 0;

  sio_initpar (&par);
  par.sig = spec->sign;
  par.le = !spec->bigend;
  par.bits = spec->width;
  // par.bps = spec->depth / 8;  /* XXX */
  par.rate = spec->rate;
  par.rchan = spec->channels;

  spec_bpf = ((spec->width / 8) * spec->channels);

  par.round = spec->segsize / spec_bpf;
  par.appbufsz = (spec->segsize * spec->segtotal) / spec_bpf;

  if (!sio_setpar (sndiosrc->hdl, &par))
    goto cannot_configure;

  sio_getpar (sndiosrc->hdl, &par);

  spec->sign = par.sig;
  spec->bigend = !par.le;
  spec->width = par.bits;
  // spec->depth = par.bps * 8;  /* XXX */
  spec->rate = par.rate;
  spec->channels = par.rchan;

  sndiosrc->bpf = par.bps * par.rchan;

  spec->segsize = par.round * par.rchan * par.bps;
  spec->segtotal = par.bufsz / par.round;

  /* FIXME: this is wrong for signed ints (and the
   * audioringbuffers should do it for us anyway) */
  spec->silence_sample[0] = 0;
  spec->silence_sample[1] = 0;
  spec->silence_sample[2] = 0;
  spec->silence_sample[3] = 0;

  sio_onmove (sndiosrc->hdl, gst_sndiosrc_cb, sndiosrc);

  if (!sio_start (sndiosrc->hdl))
    goto cannot_start;

  GST_INFO_OBJECT (sndiosrc, "successfully opened connection to sndio");

  return TRUE;

  /* ERRORS */
cannot_configure:
  {
    GST_ELEMENT_ERROR (sndiosrc, RESOURCE, OPEN_READ,
        (_("Could not configure sndio")), ("can't configure sndio"));
    return FALSE;
  }
cannot_start:
  {
    GST_ELEMENT_ERROR (sndiosrc, RESOURCE, OPEN_READ,
        (_("Could not start sndio")), ("can't start sndio"));
    return FALSE;
  }
}

static gboolean
gst_sndiosrc_unprepare (GstAudioSrc * asrc)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (asrc);

  if (sndiosrc->hdl == NULL)
    return TRUE;

  sio_stop (sndiosrc->hdl);

  return TRUE;
}

static guint
gst_sndiosrc_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (asrc);
  guint done;

  done = sio_read (sndiosrc->hdl, data, length);

  if (done == 0)
    goto read_error;

  sndiosrc->readpos += (done / sndiosrc->bpf);

  data = (char *) data + done;

  return done;

  /* ERRORS */
read_error:
  {
    GST_ELEMENT_ERROR (sndiosrc, RESOURCE, READ,
        ("Failed to read data from sndio"), GST_ERROR_SYSTEM);
    return 0;
  }
}

static guint
gst_sndiosrc_delay (GstAudioSrc * asrc)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (asrc);

  if (sndiosrc->latency == (guint) - 1) {
    GST_WARNING_OBJECT (asrc, "couldn't get latency");
    return 0;
  }

  GST_DEBUG_OBJECT (asrc, "got latency: %u", sndiosrc->latency);

  return sndiosrc->latency;
}

static void
gst_sndiosrc_reset (GstAudioSrc * asrc)
{
  /* no way to flush the buffers with sndio ? */

  GST_DEBUG_OBJECT (asrc, "reset called");
}

static void
gst_sndiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (object);

  switch (prop_id) {
    case PROP_HOST:
      g_free (sndiosrc->host);
      sndiosrc->host = g_value_dup_string (value);
      break;
    default:
      break;
  }
}

static void
gst_sndiosrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSndioSrc *sndiosrc = GST_SNDIOSRC (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, sndiosrc->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
