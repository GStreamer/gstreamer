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

#include "gstalsasink.h"
#include "gstalsaclock.h"

/* elementfactory information */
static GstElementDetails gst_alsa_sink_details = GST_ELEMENT_DETAILS (
  "Alsa Sink",
  "Sink/Audio",
  "Output to a sound card via ALSA",
  "Thomas Nyberg <thomas@codefactory.se>, "
  "Andy Wingo <apwingo@eos.ncsu.edu>, "
  "Benjamin Otte <in7y118@public.uni-hamburg.de>"
);

static GstPadTemplate *		gst_alsa_sink_pad_factory 	(void);
static GstPadTemplate *		gst_alsa_sink_request_pad_factory(void);
static void			gst_alsa_sink_base_init		(gpointer		g_class);
static void			gst_alsa_sink_class_init	(gpointer		g_klass,
								 gpointer		class_data);
static void			gst_alsa_sink_init		(GstAlsaSink *		this);
static inline void		gst_alsa_sink_flush_one_pad	(GstAlsaSink *		sink,
								 gint 			i);
static void			gst_alsa_sink_flush_pads	(GstAlsaSink *		sink);
static int			gst_alsa_sink_mmap		(GstAlsa *		this,
								 snd_pcm_sframes_t *	avail);
static int			gst_alsa_sink_write		(GstAlsa *		this,
								 snd_pcm_sframes_t *	avail);
static void			gst_alsa_sink_loop		(GstElement *		element);
static gboolean			gst_alsa_sink_check_event	(GstAlsaSink *		sink, 
								 gint			pad_nr);
static GstElementStateReturn	gst_alsa_sink_change_state	(GstElement *		element);

static GstClockTime		gst_alsa_sink_get_time		(GstAlsa *		this);

static GstAlsa *sink_parent_class = NULL;

static GstPadTemplate *
gst_alsa_sink_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                     gst_alsa_caps (SND_PCM_FORMAT_UNKNOWN, -1, -1));

  return template;
}
static GstPadTemplate *
gst_alsa_sink_request_pad_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template)
    template =
      gst_pad_template_new ("sink%d", GST_PAD_SINK, GST_PAD_REQUEST,
                                     gst_alsa_caps (SND_PCM_FORMAT_UNKNOWN, -1, 1));

  return template;
}
GType
gst_alsa_sink_get_type (void)
{
  static GType alsa_sink_type = 0;

  if (!alsa_sink_type) {
    static const GTypeInfo alsa_sink_info = {
      sizeof (GstAlsaSinkClass),
      gst_alsa_sink_base_init,
      NULL,
      gst_alsa_sink_class_init,
      NULL,
      NULL,
      sizeof (GstAlsaSink),
      0,
      (GInstanceInitFunc) gst_alsa_sink_init,
    };

    alsa_sink_type = g_type_register_static (GST_TYPE_ALSA, "GstAlsaSink", &alsa_sink_info, 0);
  }
  return alsa_sink_type;
}

static void
gst_alsa_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
 
  gst_element_class_add_pad_template (element_class, gst_alsa_sink_pad_factory ());
  gst_element_class_add_pad_template (element_class, gst_alsa_sink_request_pad_factory ());

  gst_element_class_set_details (element_class, &gst_alsa_sink_details);
}

static void
gst_alsa_sink_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class;
  GstElementClass *element_class;
  GstAlsaClass *alsa_class;
  GstAlsaSinkClass *klass;

  klass = (GstAlsaSinkClass *) g_class;
  object_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  alsa_class = (GstAlsaClass *) klass;

  if (sink_parent_class == NULL)
    sink_parent_class = g_type_class_ref (GST_TYPE_ALSA);
  
  alsa_class->stream		= SND_PCM_STREAM_PLAYBACK;
  alsa_class->transmit_mmap	= gst_alsa_sink_mmap;
  alsa_class->transmit_rw	= gst_alsa_sink_write;

  element_class->change_state	= gst_alsa_sink_change_state;
}
static void
gst_alsa_sink_init (GstAlsaSink *sink)
{
  GstAlsa *this = GST_ALSA (sink);

  this->pad[0] = gst_pad_new_from_template (gst_alsa_sink_pad_factory (), "sink");
  gst_pad_set_link_function (this->pad[0], gst_alsa_link);
  gst_pad_set_getcaps_function (this->pad[0], gst_alsa_get_caps);
  gst_element_add_pad (GST_ELEMENT (this), this->pad[0]);
  
  this->clock = gst_alsa_clock_new ("alsasinkclock", gst_alsa_sink_get_time, this);
  /* we hold a ref to our clock until we're disposed */
  gst_object_ref (GST_OBJECT (this->clock));
  gst_object_sink (GST_OBJECT (this->clock));

  gst_element_set_loop_function (GST_ELEMENT (this), gst_alsa_sink_loop);
}

static inline void
gst_alsa_sink_flush_one_pad (GstAlsaSink *sink, gint i)
{
  switch (sink->behaviour[i]) {
  case 0:
    if (sink->buf[i])
      gst_data_unref (GST_DATA (sink->buf[i]));
    sink->buf[i] = NULL;  
    sink->data[i] = NULL;
    sink->behaviour[i] = 0;
    sink->size[i] = 0;
    break;
  case 1:
    g_free (sink->data[i]);
    sink->data[i] = NULL;
    sink->behaviour[i] = 0;
    sink->size[i] = 0;
    break;
  default:
    g_assert_not_reached ();
  }
}
static void
gst_alsa_sink_flush_pads (GstAlsaSink *sink)
{
  gint i;

  for (i = 0; i < GST_ELEMENT (sink)->numpads; i++) {
    /* flush twice to unref buffer when behaviour == 1 */
    gst_alsa_sink_flush_one_pad (sink, i);
    gst_alsa_sink_flush_one_pad (sink, i);
  }
}
/* TRUE, if everything should continue */
static gboolean
gst_alsa_sink_check_event (GstAlsaSink *sink, gint pad_nr)
{
  gboolean cont = TRUE;
  GstEvent *event = GST_EVENT (sink->buf[pad_nr]);
  GstAlsa *this = GST_ALSA (sink);

  if (event) {
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        gst_alsa_set_eos (this);
        cont = FALSE;
        break;
      case GST_EVENT_INTERRUPT:
	cont = FALSE;
        break;
      case GST_EVENT_DISCONTINUOUS: 
	{
	  gint64 value;
	  
	  /* only the first pad my seek */
	  if (pad_nr != 0) {
	    break;	    
	  }
	  if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &value)) {
            if (!gst_clock_handle_discont (GST_ELEMENT (this)->clock, value))
              GST_WARNING_OBJECT (this, "clock couldn't handle discontinuity");
          }

	  if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT, &value)) {
            /* don't change the value ; it's in samples already */
          } else if (gst_event_discont_get_value (event, GST_FORMAT_BYTES, &value)) {
            if (this->format) value = gst_alsa_bytes_to_samples (this, value);
          } else if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &value)) {
            if (this->format) value = gst_alsa_timestamp_to_samples (this, value);
          } else {
            GST_WARNING_OBJECT (this, "could not acquire samplecount after seek, the clock might screw your pipeline now");
            break;
          }

          /* if the clock is running */
	  if (GST_CLOCK_TIME_IS_VALID (this->clock->start_time)) {
	    g_assert (this->format);
            /* adjust the start time */
	    this->clock->start_time +=
	      gst_alsa_samples_to_timestamp (this, this->transmitted) -
	      gst_alsa_samples_to_timestamp (this, value);
	  }
	  this->transmitted = value;
	  break;
        }
      default:
        GST_INFO_OBJECT (this, "got an unknown event (Type: %d)", GST_EVENT_TYPE (event));
        break;
    }
    gst_event_unref (event);
    sink->buf[pad_nr] = NULL;
  } else {
    /* the element at the top of the chain did not emit an event. */
    g_assert_not_reached ();
  }
  return cont;
}
static int
gst_alsa_sink_mmap (GstAlsa *this, snd_pcm_sframes_t *avail)
{
  snd_pcm_uframes_t offset;
  const snd_pcm_channel_area_t *dst;
  snd_pcm_channel_area_t *src;
  GstAlsaSink *sink = GST_ALSA_SINK (this);
  int i, err, width = snd_pcm_format_physical_width (this->format->format);

  /* areas points to the memory areas that belong to gstreamer. */
  src = g_malloc0 (this->format->channels * sizeof(snd_pcm_channel_area_t));

  if (((GstElement *) this)->numpads == 1) {
    /* interleaved */
    for (i = 0; i < this->format->channels; i++) {
      src[i].addr = sink->data[0];
      src[i].first = i * width;
      src[i].step = this->format->channels * width;
    }
  } else {
    /* noninterleaved */
    for (i = 0; i < this->format->channels; i++) {
      src[i].addr = sink->data[i];
      src[i].first = 0;
      src[i].step = width;
    }
  }

  if ((err = snd_pcm_mmap_begin (this->handle, &dst, &offset, avail)) < 0) {
    GST_ERROR_OBJECT (this, "mmap failed: %s", snd_strerror (err));
    return -1;
  }

  if ((err = snd_pcm_areas_copy (dst, offset, src, 0, this->format->channels, *avail, this->format->format)) < 0) {
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
gst_alsa_sink_write (GstAlsa *this, snd_pcm_sframes_t *avail)
{
  GstAlsaSink *sink = GST_ALSA_SINK (this);
  void *channels[this->format->channels];
  int err, i;

  if (((GstElement *) this)->numpads == 1) {
    /* interleaved */
    err = snd_pcm_writei (this->handle, sink->data[0], *avail);
  } else {
    /* noninterleaved */
    for (i = 0; i < this->format->channels; i++) {
      channels[i] = sink->data[i];
    }
    err = snd_pcm_writen (this->handle, channels, *avail);
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
static void
gst_alsa_sink_loop (GstElement *element)
{
  snd_pcm_sframes_t avail, avail2, copied, sample_diff, max_discont;
  snd_pcm_uframes_t samplestamp;
  gint i;
  guint bytes; /* per channel */
  GstAlsa *this = GST_ALSA (element);
  GstAlsaSink *sink = GST_ALSA_SINK (element);

  g_return_if_fail (sink != NULL);

sink_restart:

  avail = gst_alsa_update_avail (this);
  if (avail == -EPIPE) goto sink_restart;
  if (avail < 0) return;
  if (avail > 0) {

  /* Not enough space. We grab data nonetheless and sleep afterwards */
    if (avail < this->period_size) {
      avail = this->period_size;
    }
    
    /* check how many bytes we still have in all our bytestreams */
    /* initialize this value to a somewhat sane state, we might alloc this much data below (which would be a bug, but who knows)... */
    bytes = this->period_size * this->period_count * element->numpads * 8; /* must be > max sample size in bytes */
    for (i = 0; i < element->numpads; i++) {
      g_assert (this->pad[i] != NULL);
      while (sink->size[i] == 0) {
        if (!sink->buf[i])
          sink->buf[i] = GST_BUFFER (gst_pad_pull (this->pad[i]));
        if (GST_IS_EVENT (sink->buf[i])) {
	  if (gst_alsa_sink_check_event (sink, i))
	    continue;
	  return;
	}
        /* caps nego failed somewhere */
        if (this->format == NULL) {
          gst_element_error (GST_ELEMENT (this), "alsasink: No caps available");
          return;
        }
        samplestamp = gst_alsa_timestamp_to_samples (this, GST_BUFFER_TIMESTAMP (sink->buf[i]));
        max_discont = gst_alsa_timestamp_to_samples (this, this->max_discont);
        sample_diff = samplestamp - this->transmitted;

        if ((!GST_BUFFER_TIMESTAMP_IS_VALID (sink->buf[i])) ||
            (-max_discont <= sample_diff && sample_diff <= max_discont)) {

          /* difference between expected and current is < GST_ALSA_DEVIATION */
no_difference:
	  sink->size[i] = sink->buf[i]->size;
          sink->data[i] = sink->buf[i]->data;
          sink->behaviour[i] = 0;
	} else if (sample_diff > 0) {
	  /* there are empty samples in front of us, fill them with silence */
	  int samples = MIN (bytes, sample_diff) *
	             (element->numpads == 1 ? this->format->channels : 1);
	  int size = samples * snd_pcm_format_physical_width (this->format->format) / 8;
	  GST_INFO_OBJECT (this, "Allocating %d bytes (%ld samples) now to resync: sample %ld expected, but got %ld\n", 
			   size, MIN (bytes, sample_diff), this->transmitted, samplestamp);
	  sink->data[i] = g_try_malloc (size);
	  if (!sink->data[i]) {
	    GST_WARNING_OBJECT (this, "error allocating %d bytes, buffers unsynced now.", size);
	    goto no_difference;
	  }
	  sink->size[i] = size;
	  if (0 != snd_pcm_format_set_silence (this->format->format, sink->data[i], samples)) {
	    GST_WARNING_OBJECT (this, "error silencing buffer, enjoy the noise.");
	  }
	  sink->behaviour[i] = 1;
	} else if (gst_alsa_samples_to_bytes (this, -sample_diff) >= sink->buf[i]->size) {
	  GST_INFO_OBJECT (this, "Skipping %lu samples to resync (complete buffer): sample %ld expected, but got %ld\n", 
			   gst_alsa_bytes_to_samples (this, sink->buf[i]->size), this->transmitted, samplestamp);	              
	  /* this buffer is way behind */
	  gst_buffer_unref (sink->buf[i]);
	  sink->buf[i] = NULL;
	  continue;
	} else if (sample_diff < 0) {
	  gint difference = gst_alsa_samples_to_bytes (this, -samplestamp);
	  GST_INFO_OBJECT (this, "Skipping %lu samples to resync: sample %ld expected, but got %ld\n",
			   (gulong) -sample_diff, this->transmitted, samplestamp);
	  /* this buffer is only a bit behind */
          sink->size[i] = sink->buf[i]->size - difference;
          sink->data[i] = sink->buf[i]->data + difference;
          sink->behaviour[i] = 0;
	} else {
	  g_assert_not_reached ();
	}
      }
      bytes = MIN (bytes, sink->size[i]);
    }

    avail = MIN (avail, gst_alsa_bytes_to_samples (this, bytes));

    /* wait until the hw buffer has enough space */
    while (gst_element_get_state (element) == GST_STATE_PLAYING && (avail2 = gst_alsa_update_avail (this)) < avail) {
      if (avail2 <= -EPIPE) goto sink_restart;
      if (avail2 < 0) return;
      if (avail2 < avail && snd_pcm_state(this->handle) != SND_PCM_STATE_RUNNING)
	if (!gst_alsa_start (this)) return;
      if (gst_alsa_pcm_wait (this) == FALSE)
        return;
    }

    /* FIXME: lotsa stuff can have happened while fetching data. Do we need to check something? */
  
    /* put this data into alsa */
    if ((copied = this->transmit (this, &avail)) < 0)
      return;
    /* update our clock */
    this->transmitted += copied;
    /* flush the data */
    bytes = gst_alsa_samples_to_bytes (this, copied);
    for (i = 0; i < element->numpads; i++) {
      if ((sink->size[i] -= bytes) == 0) {
        gst_alsa_sink_flush_one_pad (sink, i);
        continue;
      }
      g_assert (sink->size[i] > 0);
      if (sink->behaviour[i] != 1)
        sink->data[i] += bytes;
    }
  }

  if (snd_pcm_state(this->handle) != SND_PCM_STATE_RUNNING && snd_pcm_avail_update (this->handle) == 0) {
    gst_alsa_start (this);
  }

}

static GstElementStateReturn
gst_alsa_sink_change_state (GstElement *element)
{
  GstAlsaSink *sink;

  g_return_val_if_fail (element != NULL, FALSE);
  sink = GST_ALSA_SINK (element);

  switch (GST_STATE_TRANSITION (element)) {
  case GST_STATE_NULL_TO_READY:
  case GST_STATE_READY_TO_PAUSED:
  case GST_STATE_PAUSED_TO_PLAYING:
  case GST_STATE_PLAYING_TO_PAUSED:
    break;
  case GST_STATE_PAUSED_TO_READY:
    gst_alsa_sink_flush_pads (sink);
    break;
  case GST_STATE_READY_TO_NULL:
    break;
  default:
    g_assert_not_reached();
  }

  if (GST_ELEMENT_CLASS (sink_parent_class)->change_state)
    return GST_ELEMENT_CLASS (sink_parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static GstClockTime
gst_alsa_sink_get_time (GstAlsa *this)
{
  snd_pcm_sframes_t delay;
  
  if (snd_pcm_delay (this->handle, &delay) == 0) {
    return GST_SECOND * (GstClockTime) (this->transmitted > delay ? this->transmitted - delay : 0) / this->format->rate;
  } else {
    return 0;
  }
}

