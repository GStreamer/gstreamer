/* -*- Mode: C; c-basic-offset: 4 -*- */
/*
    Copyright (C) 2002, 2003 Andy Wingo <wingo@pobox.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "gstjack.h"


static GstBinClass *parent_class = NULL;

static void gst_jack_bin_init (GstJackBin * this);
static void gst_jack_bin_class_init (GstJackBinClass * klass);

static GstElementStateReturn gst_jack_bin_change_state (GstElement * element);

/* jack callbacks */
static int process (jack_nframes_t nframes, void *arg);
static int sample_rate (jack_nframes_t nframes, void *arg);
static int buffer_size (jack_nframes_t nframes, void *arg);
static void shutdown (void *arg);

static void sighup_handler (int sig);
static GstJackBin *_jackbin = NULL;
static gboolean watchdog_please_set_the_jackbin_to_ready = FALSE;

/* fixme: we need a watchdog thread to see if we have received a SIGHUP, and if
 * so set the state of the bin to READY. */

GType
gst_jack_bin_get_type (void)
{
  static GType jack_bin_type = 0;

  if (!jack_bin_type) {
    static const GTypeInfo jack_bin_info = {
      sizeof (GstJackBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_jack_bin_class_init,
      NULL,
      NULL,
      sizeof (GstJackBin),
      0,
      (GInstanceInitFunc) gst_jack_bin_init,
    };

    jack_bin_type =
        g_type_register_static (GST_TYPE_BIN, "GstJackBin", &jack_bin_info, 0);
  }
  return jack_bin_type;
}

static void
gst_jack_bin_class_init (GstJackBinClass * klass)
{
  GObjectClass *object_class;
  GstElementClass *element_class;

  object_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  element_class->change_state = gst_jack_bin_change_state;
}

static void
gst_jack_bin_init (GstJackBin * this)
{
  GST_DEBUG ("initializing jack bin");

  /* jack bins are managing bins and iterate themselves */
  GST_FLAG_SET (this, GST_BIN_FLAG_MANAGER);
  GST_FLAG_SET (this, GST_BIN_SELF_SCHEDULABLE);

  /* make a new scheduler and associate it with the bin */
  gst_scheduler_factory_make (NULL, GST_ELEMENT (this));
}

static GstElementStateReturn
gst_jack_bin_change_state (GstElement * element)
{
  GstJackBin *this;
  GList *l = NULL;
  GstJackPad *pad;

  g_return_val_if_fail (element != NULL, FALSE);
  this = GST_JACK_BIN (element);

  switch (GST_STATE_PENDING (element)) {
    case GST_STATE_NULL:
      JACK_DEBUG ("jackbin: NULL state");
      if (this->client) {
        JACK_DEBUG ("jackbin: closing client");
        jack_client_close (this->client);
        this->client = NULL;
      }

      if (_jackbin)
        signal (SIGHUP, SIG_DFL);
      _jackbin = NULL;

      if (GST_ELEMENT_CLASS (parent_class)->change_state)
        return GST_ELEMENT_CLASS (parent_class)->change_state (element);
      break;

    case GST_STATE_READY:
      JACK_DEBUG ("jackbin: READY");

      _jackbin = this;
      signal (SIGHUP, sighup_handler);

      if (!this->client) {
        if (!(this->client = jack_client_new ("gst-jack"))) {
          g_warning ("jack server not running?");
          return GST_STATE_FAILURE;
        }

        gst_scheduler_setup (GST_ELEMENT_SCHED (this));

        jack_set_process_callback (this->client, process, this);
        jack_set_sample_rate_callback (this->client, sample_rate, this);
        jack_set_buffer_size_callback (this->client, buffer_size, this);
        this->nframes = jack_get_buffer_size (this->client);
        jack_on_shutdown (this->client, shutdown, this);
      }

      if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
        l = this->src_pads;
        while (l) {
          JACK_DEBUG ("jackbin: unregistering pad %s:%s",
              GST_JACK_PAD (l)->name, GST_JACK_PAD (l)->peer_name);
          jack_port_unregister (this->client, GST_JACK_PAD (l)->port);
          l = g_list_next (l);
        }
        l = this->sink_pads;
        while (l) {
          JACK_DEBUG ("jackbin: unregistering pad %s:%s",
              GST_JACK_PAD (l)->name, GST_JACK_PAD (l)->peer_name);
          jack_port_unregister (this->client, GST_JACK_PAD (l)->port);
          l = g_list_next (l);
        }
        GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_OPEN);

        if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
          JACK_DEBUG ("jackbin: deactivating client");
          jack_deactivate (this->client);
          GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_ACTIVE);
        }
      }

      if (GST_ELEMENT_CLASS (parent_class)->change_state)
        return GST_ELEMENT_CLASS (parent_class)->change_state (element);
      break;

    case GST_STATE_PAUSED:
      JACK_DEBUG ("jackbin: PAUSED");

      if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
        l = this->src_pads;
        while (l) {
          pad = GST_JACK_PAD (l);
          JACK_DEBUG ("jackbin: registering input port %s (peer %s)", pad->name,
              pad->peer_name);
          pad->port =
              jack_port_register (this->client, pad->name,
              JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput | JackPortIsTerminal, 0);
          l = g_list_next (l);
        }
        l = this->sink_pads;
        while (l) {
          pad = GST_JACK_PAD (l);
          JACK_DEBUG ("jackbin: registering output port %s (peer %s)",
              pad->name, pad->peer_name);
          pad->port =
              jack_port_register (this->client, pad->name,
              JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal,
              0);
          l = g_list_next (l);
        }

        /* must activate before connecting */
        if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
          JACK_DEBUG ("jackbin: activating client");
          jack_activate (this->client);
          GST_FLAG_SET (GST_OBJECT (this), GST_JACK_ACTIVE);
        }

        l = this->src_pads;
        while (l) {
          pad = GST_JACK_PAD (l);
          JACK_DEBUG ("connecting jack port %s to gst jack port %s",
              pad->peer_name, jack_port_name (pad->port));
          if (jack_connect (this->client, pad->peer_name,
                  jack_port_name (pad->port))) {
            g_warning ("jackbin: could not connect %s and %s", pad->peer_name,
                jack_port_name (pad->port));
            return GST_STATE_FAILURE;
          }
          l = g_list_next (l);
        }
        l = this->sink_pads;
        while (l) {
          pad = GST_JACK_PAD (l);
          JACK_DEBUG ("connecting gst jack port %s to jack port %s",
              jack_port_name (pad->port), pad->peer_name);
          if (jack_connect (this->client, jack_port_name (pad->port),
                  pad->peer_name)) {
            g_warning ("jackbin: could not connect %s and %s", pad->peer_name,
                jack_port_name (pad->port));
            return GST_STATE_FAILURE;
          }
          l = g_list_next (l);
        }

        JACK_DEBUG ("jackbin: setting OPEN flag");
        GST_FLAG_SET (GST_OBJECT (this), GST_JACK_OPEN);

        if (GST_ELEMENT_CLASS (parent_class)->change_state)
          return GST_ELEMENT_CLASS (parent_class)->change_state (element);
      } else {
        if (GST_ELEMENT_CLASS (parent_class)->change_state)
          return GST_ELEMENT_CLASS (parent_class)->change_state (element);
      }

      break;
    case GST_STATE_PLAYING:
      JACK_DEBUG ("jackbin: PLAYING");

      if (GST_ELEMENT_CLASS (parent_class)->change_state)
        return GST_ELEMENT_CLASS (parent_class)->change_state (element);
      break;
  }

  JACK_DEBUG ("jackbin: state change finished");

  return GST_STATE_SUCCESS;
}

/* jack callbacks */

/* keep in mind that these run in another thread, mm-kay? */

static int
process (jack_nframes_t nframes, void *arg)
{
  GstJackBin *bin = (GstJackBin *) arg;
  GstJackPad *pad;
  GList *l;

  g_assert (bin);

  JACK_DEBUG ("jackbin: process()");

  if (GST_STATE (bin) != GST_STATE_PLAYING) {
    JACK_DEBUG ("jackbin: bin is not PLAYING yet, returning");
    return 0;
  } else {
    JACK_DEBUG ("jackbin: we are PLAYING, let's process()");
  }

  l = bin->src_pads;
  while (l) {
    pad = GST_JACK_PAD (l);
    pad->data = jack_port_get_buffer (pad->port, nframes);
    l = g_list_next (l);
  }

  l = bin->sink_pads;
  while (l) {
    pad = GST_JACK_PAD (l);
    pad->data = jack_port_get_buffer (pad->port, nframes);
    l = g_list_next (l);
  }

  bin->nframes = nframes;

  JACK_DEBUG ("jackbin: iterating to process %ld frames of audio...", nframes);
  if (!gst_bin_iterate (GST_BIN (bin))) {
    g_warning ("bin failed to iterate");
    return -1;
  }

  /* that's all folks */

  return 0;
}

static int
sample_rate (jack_nframes_t nframes, void *arg)
{
  GstJackBin *bin = (GstJackBin *) arg;

  JACK_DEBUG ("the sample rate is now %lu/sec\n", nframes);
  bin->rate = nframes;
  return 0;
}

static int
buffer_size (jack_nframes_t nframes, void *arg)
{
  GstJackBin *bin = (GstJackBin *) arg;

  JACK_DEBUG ("the buffer size is now %lu\n", nframes);
  bin->nframes = nframes;
  return 0;
}

static void
shutdown (void *arg)
{
/*    GstJackClient *client = (GstJackClient*) arg; */
  printf ("shutdown %p\n", arg);
/*    gst_element_set_state (GST_ELEMENT (client->manager), GST_STATE_READY); */
}

static void
sighup_handler (int sig)
{
  g_message ("got sighup, setting state to READY");
  watchdog_please_set_the_jackbin_to_ready = TRUE;
}
