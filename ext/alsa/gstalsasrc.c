/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalsasrc.h"
#include "gstalsaclock.h"

/* elementfactory information */
static GstElementDetails gst_alsa_src_details = GST_ELEMENT_DETAILS (
  "Alsa Src",
  "Source/Audio",
  "Read from a sound card via ALSA",
  "Thomas Nyberg <thomas@codefactory.se>, "
  "Andy Wingo <apwingo@eos.ncsu.edu>, "
  "Benjamin Otte <in7y118@public.uni-hamburg.de>"
);

static GstPadTemplate *		gst_alsa_src_pad_factory 	(void);
static void			gst_alsa_src_base_init		(gpointer		g_class);
static void			gst_alsa_src_class_init		(gpointer		g_class,
								 gpointer		class_data);
static void			gst_alsa_src_init		(GstAlsaSrc *		this);
static int			gst_alsa_src_mmap		(GstAlsa *		this,
								 snd_pcm_sframes_t *	avail);
static int			gst_alsa_src_read		(GstAlsa *		this,
								 snd_pcm_sframes_t *	avail);
static void			gst_alsa_src_loop		(GstElement *		element);
static void			gst_alsa_src_flush		(GstAlsaSrc *		src);
static GstElementStateReturn	gst_alsa_src_change_state	(GstElement *		element);

static GstClockTime		gst_alsa_src_get_time		(GstAlsa *		this);

static GstAlsa *src_parent_class = NULL;

static GstPadTemplate *
gst_alsa_src_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                     gst_alsa_caps (SND_PCM_FORMAT_UNKNOWN, -1, -1));

  return template;
}

GType
gst_alsa_src_get_type (void)
{
  static GType alsa_src_type = 0;

  if (!alsa_src_type) {
    static const GTypeInfo alsa_src_info = {
      sizeof (GstAlsaSrcClass),
      gst_alsa_src_base_init,
      NULL,
      gst_alsa_src_class_init,
      NULL,
      NULL,
      sizeof (GstAlsaSrc),
      0,
      (GInstanceInitFunc) gst_alsa_src_init,
    };

    alsa_src_type = g_type_register_static (GST_TYPE_ALSA, "GstAlsaSrc", &alsa_src_info, 0);
  }
  return alsa_src_type;
}

static void
gst_alsa_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
 
  gst_element_class_add_pad_template (element_class, gst_alsa_src_pad_factory ());

  gst_element_class_set_details (element_class, &gst_alsa_src_details);
}

static void
gst_alsa_src_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class;
  GstElementClass *element_class;
  GstAlsaClass *alsa_class;
  GstAlsaSrcClass *klass;

  klass = (GstAlsaSrcClass *) g_class;
  object_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  alsa_class = (GstAlsaClass *) klass;

  if (src_parent_class == NULL)
    src_parent_class = g_type_class_ref (GST_TYPE_ALSA);

  alsa_class->stream		= SND_PCM_STREAM_CAPTURE;
  alsa_class->transmit_mmap	= gst_alsa_src_mmap;
  alsa_class->transmit_rw	= gst_alsa_src_read;

  element_class->change_state	= gst_alsa_src_change_state;
}
static void
gst_alsa_src_init (GstAlsaSrc *src)
{
  GstAlsa *this = GST_ALSA (src);

  this->pad[0] = gst_pad_new_from_template (gst_alsa_src_pad_factory (), "src");
  gst_pad_set_link_function (this->pad[0], gst_alsa_link);
  gst_pad_set_getcaps_function (this->pad[0], gst_alsa_get_caps);
  gst_element_add_pad (GST_ELEMENT (this), this->pad[0]);
  
  this->clock = gst_alsa_clock_new ("alsasrcclock", gst_alsa_src_get_time, this);
  /* we hold a ref to our clock until we're disposed */
  gst_object_ref (GST_OBJECT (this->clock));
  gst_object_sink (GST_OBJECT (this->clock));

  gst_element_set_loop_function (GST_ELEMENT (this), gst_alsa_src_loop);
}
static int
gst_alsa_src_mmap (GstAlsa *this, snd_pcm_sframes_t *avail)
{
  snd_pcm_uframes_t offset;
  snd_pcm_channel_area_t *dst;
  const snd_pcm_channel_area_t *src;
  int i, err, width = snd_pcm_format_physical_width (this->format->format);
  GstAlsaSrc *alsa_src = GST_ALSA_SRC (this);

  /* areas points to the memory areas that belong to gstreamer. */
  dst = g_malloc0 (this->format->channels * sizeof(snd_pcm_channel_area_t));

  if (((GstElement *) this)->numpads == 1) {
    /* interleaved */
    for (i = 0; i < this->format->channels; i++) {
      dst[i].addr = alsa_src->buf[0]->data;
      dst[i].first = i * width;
      dst[i].step = this->format->channels * width;
    }
  } else {
    /* noninterleaved */
    for (i = 0; i < this->format->channels; i++) {
      dst[i].addr = alsa_src->buf[i]->data;
      dst[i].first = 0;
      dst[i].step = width;
    }
  }

  if ((err = snd_pcm_mmap_begin (this->handle, &src, &offset, avail)) < 0) {
    GST_ERROR_OBJECT (this, "mmap failed: %s", snd_strerror (err));
    return -1;
  }
  if (*avail > 0 && (err = snd_pcm_areas_copy (dst, 0, src, offset, this->format->channels, *avail, this->format->format)) < 0) {
    snd_pcm_mmap_commit (this->handle, offset, 0);
    GST_ERROR_OBJECT (this, "data copy failed: %s", snd_strerror (err));
    return -1;
  }
  if ((err = snd_pcm_mmap_commit (this->handle, offset, *avail)) < 0) {
    GST_ERROR_OBJECT (this, "mmap commit failed: %s", snd_strerror (err));
    return -1;
  }

  return err;
}
static int
gst_alsa_src_read (GstAlsa *this, snd_pcm_sframes_t *avail)
{
  void *channels[this->format->channels];
  int err, i;
  GstAlsaSrc *src = GST_ALSA_SRC (this);

  if (((GstElement *) this)->numpads == 1) {
    /* interleaved */
    err = snd_pcm_readi (this->handle, src->buf[0]->data, *avail);
  } else {
    /* noninterleaved */
    for (i = 0; i < this->format->channels; i++) {
      channels[i] = src->buf[i]->data;
    }
    err = snd_pcm_readn (this->handle, channels, *avail);
  }
  /* error handling */
  if (err < 0) {
    if (err == -EPIPE) {
      gst_alsa_xrun_recovery (this);
      return 0;
    }
    GST_ERROR_OBJECT (this, "error on data access: %s", snd_strerror (err));
  }
  return err;
}
static inline gint
gst_alsa_src_adjust_rate (gint rate, gboolean aggressive)
{
  static gint rates[] = { 96000, 48000, 44100, 22050, 8000 };
  gint i;

  if (aggressive)
    return rate;

  for (i = 0; i < G_N_ELEMENTS (rates); i++) {
    if (rate >= rates[i])
      return rates[i];
  }

  return 0;
}
static gboolean
gst_alsa_src_set_caps (GstAlsaSrc *src, gboolean aggressive)
{
  GstCaps *all_caps, *caps;
  GstStructure *structure, *walk;
  gint channels, min_channels, max_channels;
  gint rate, min_rate, max_rate;
  gint i, endian, width, depth;
  gboolean sign;
  GstAlsa *this = GST_ALSA (src);

  all_caps = gst_alsa_get_caps (this->pad[0]);
  if (all_caps == NULL) return FALSE;
  /* now intersect this with all caps of the peers... */
  for (i = 0; i < GST_ELEMENT (src)->numpads; i++) {
    all_caps = gst_caps_intersect (all_caps, gst_pad_get_allowed_caps (this->pad[i]));
    if (all_caps == NULL) {
      GST_DEBUG ("No compatible caps found in alsasrc (%s)", GST_ELEMENT_NAME (this));
      return FALSE;
    }
  }

  /* construct caps */
  caps = gst_caps_new_simple ("audio/x-raw-int",
      NULL);
  g_assert (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);

  /* now try to find the best match */
  for (i = 0; i < gst_caps_get_size (all_caps); i++) {
    walk = gst_caps_get_structure (all_caps, i);
    if (!(gst_structure_get_int (walk, "signed", &sign) &&
	  gst_structure_get_int (walk, "width", &width) &&
	  gst_structure_get_int (walk, "depth", &depth))) {
      GST_ERROR_OBJECT (src, "couldn't parse my own format. Huh?");
      continue;
    }
    if (!gst_structure_get_int (walk, "endianness", &endian)) {
      endian = G_BYTE_ORDER;
    }
    gst_structure_set (structure, 
	"endianness", G_TYPE_INT, endian,
	"width",      G_TYPE_INT, width,
	"depth",      G_TYPE_INT, depth,
	"signed",     G_TYPE_BOOLEAN, sign,
	NULL);

    min_rate = gst_value_get_int_range_min (gst_structure_get_value (walk, "rate"));
    max_rate = gst_value_get_int_range_max (gst_structure_get_value (walk, "rate"));
    min_channels = gst_value_get_int_range_min (gst_structure_get_value (walk, "channels"));
    max_channels = gst_value_get_int_range_max (gst_structure_get_value (walk, "channels"));
    for (rate = max_rate;; rate--) {
      if ((rate = gst_alsa_src_adjust_rate (rate, aggressive)) < min_rate)
	break;
      gst_structure_set (structure, "rate", G_TYPE_INT, rate, NULL);
      for (channels = aggressive ? max_channels : MIN (max_channels, 2); channels >= min_channels; channels--) {
        gst_structure_set (structure, "channels", G_TYPE_INT, channels, NULL);
        GST_DEBUG ("trying new caps: %ssigned, endianness: %d, width %d, depth %d, channels %d, rate %d",
                   sign ? "" : "un", endian, width, depth, channels, rate);
        if (gst_pad_try_set_caps (this->pad[0], caps) != GST_PAD_LINK_REFUSED)
          gst_alsa_link (this->pad[0], caps);

        if (this->format) {
	  /* try to set caps here */
	  return TRUE;
	}
      }
    }
  }

  if (!aggressive)
    return gst_alsa_src_set_caps (src, TRUE);

  return FALSE;
}
/* we transmit buffers of period_size frames */
static void
gst_alsa_src_loop (GstElement *element)
{
  snd_pcm_sframes_t avail, copied;
  gint i;
  GstAlsa *this = GST_ALSA (element);
  GstAlsaSrc *src = GST_ALSA_SRC (element);

  /* set the caps on all pads */
  if (!this->format) {
    if (!gst_alsa_src_set_caps (src, FALSE)) {
      gst_element_error (element, "Could not set caps");
      return;
    }
  }

  while ((avail = gst_alsa_update_avail (this)) < this->period_size) {
    if (avail == -EPIPE) continue;
    if (avail < 0) return;
    if (snd_pcm_state(this->handle) != SND_PCM_STATE_RUNNING) {
      if (!gst_alsa_start (this))
	return;
      continue;
    };
    /* wait */
    if (gst_alsa_pcm_wait (this) == FALSE)
      return;
  }
  g_assert (avail >= this->period_size);
  /* make sure every pad has a buffer */
  for (i = 0; i < element->numpads; i++) {
    if (!src->buf[i]) {
      src->buf[i] = gst_buffer_new_and_alloc (4096);
    }
  }
  /* fill buffer with data */
  if ((copied = this->transmit (this, &avail)) <= 0)
    return;
  /* push the buffers out and let them have fun */
  for (i = 0; i < element->numpads; i++) {
    if (!src->buf[i])
      return;
    if (copied != this->period_size)
      GST_BUFFER_SIZE (src->buf[i]) = gst_alsa_samples_to_bytes (this, copied);
    GST_BUFFER_TIMESTAMP (src->buf[i]) = gst_alsa_samples_to_timestamp (this, this->transmitted);
    GST_BUFFER_DURATION (src->buf[i]) = gst_alsa_samples_to_timestamp (this, copied);
    gst_pad_push (this->pad[i], GST_DATA (src->buf[i]));
    src->buf[i] = NULL;
  }
  this->transmitted += copied;
}

static void
gst_alsa_src_flush (GstAlsaSrc *src)
{
  gint i;

  for (i = 0; i < GST_ELEMENT (src)->numpads; i++) { 
    if (src->buf[i]) {
      gst_buffer_unref (src->buf[i]);
      src->buf[i] = NULL;
    }
  }
}
static GstElementStateReturn
gst_alsa_src_change_state (GstElement *element)
{
  GstAlsaSrc *src;

  g_return_val_if_fail (element != NULL, FALSE);
  src = GST_ALSA_SRC (element);

  switch (GST_STATE_TRANSITION (element)) {
  case GST_STATE_NULL_TO_READY:
  case GST_STATE_READY_TO_PAUSED:
  case GST_STATE_PAUSED_TO_PLAYING:
  case GST_STATE_PLAYING_TO_PAUSED:
    break;
  case GST_STATE_PAUSED_TO_READY:
    gst_alsa_src_flush (src);
    break;
  case GST_STATE_READY_TO_NULL:
    break;
  default:
    g_assert_not_reached();
  }

  if (GST_ELEMENT_CLASS (src_parent_class)->change_state)
    return GST_ELEMENT_CLASS (src_parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static GstClockTime
gst_alsa_src_get_time (GstAlsa *this)
{
  snd_pcm_sframes_t delay;
  
  if (snd_pcm_delay (this->handle, &delay) == 0) {
    return GST_SECOND * (this->transmitted + delay) / this->format->rate;
  } else {
    return 0;
  }
}

