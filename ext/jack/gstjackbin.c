/* -*- Mode: C; c-basic-offset: 4 -*- */
/*
    Copyright (C) 2002 Andy Wingo <wingo@pobox.com>

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

#include <stdlib.h>
#include <string.h>
#include "gstjack.h"


static GstBinClass *parent_class = NULL;

static void gst_jack_bin_init(GstJackBin *this);
static void gst_jack_bin_class_init(GstJackBinClass *klass);

static GstElementStateReturn gst_jack_bin_change_state(GstElement *element);

/* jack callbacks */
static int process (jack_nframes_t nframes, void *arg);
static int buffer_size (jack_nframes_t nframes, void *arg);
static int sample_rate (jack_nframes_t nframes, void *arg);
static void shutdown (void *arg);


GType
gst_jack_bin_get_type (void) 
{
    static GType jack_bin_type = 0;

    if (!jack_bin_type) {
        static const GTypeInfo jack_bin_info = {
            sizeof(GstJackBinClass),
            NULL,
            NULL,
            (GClassInitFunc)gst_jack_bin_class_init,
            NULL,
            NULL,
            sizeof(GstJackBin),
            0,
            (GInstanceInitFunc)gst_jack_bin_init,
        };
        jack_bin_type = g_type_register_static (GST_TYPE_BIN, "GstJackBin", &jack_bin_info, 0);
    }
    return jack_bin_type;
}

static void
gst_jack_bin_class_init(GstJackBinClass *klass)
{
    GObjectClass *object_class;
    GstElementClass *element_class;
    
    object_class = (GObjectClass *)klass;
    element_class = (GstElementClass *)klass;
    
    parent_class = g_type_class_ref(GST_TYPE_BIN);

    element_class->change_state = gst_jack_bin_change_state;
}

static void
gst_jack_bin_init(GstJackBin *this)
{
    GST_DEBUG (GST_CAT_THREAD, "initializing jack bin");
    
    /* jack bins are managing bins and iterate themselves */
    GST_FLAG_SET (this, GST_BIN_FLAG_MANAGER);
    GST_FLAG_SET (this, GST_BIN_SELF_SCHEDULABLE);
    
    /* make a new scheduler and associate it with the bin */
    gst_scheduler_factory_make (NULL, GST_ELEMENT (this));

    this->cond = g_cond_new ();
    this->lock = g_mutex_new ();
    g_mutex_lock (this->lock);
    this->sched_setup = FALSE;
}

static GstElementStateReturn
gst_jack_bin_change_state (GstElement *element)
{
    GstJackBin *this;
    GList *l = NULL;
    GstJackPad *pad;
    
    g_return_val_if_fail (element != NULL, FALSE);
    this = GST_JACK_BIN (element);
    
    switch (GST_STATE_PENDING (element)) {
    case GST_STATE_NULL:
        g_message ("jackbin: NULL state");
        if (this->client) {
            g_message ("jack: closing client");
            jack_client_close (this->client);
        }
        
        if (GST_ELEMENT_CLASS (parent_class)->change_state)
            return GST_ELEMENT_CLASS (parent_class)->change_state (element);
        break;
        
    case GST_STATE_READY:
        g_message ("jackbin: READY");
        if (!this->client) {
          if (!(this->client = jack_client_new ("gst-jack"))) {
            g_warning ("jack server not running?");
            return GST_STATE_FAILURE;
          }
                
          jack_set_process_callback (this->client, process, this);
          jack_set_buffer_size_callback (this->client, buffer_size, this);
          jack_set_sample_rate_callback (this->client, sample_rate, this);
          jack_on_shutdown (this->client, shutdown, this);
        }
        
        /* fixme: there are a *lot* of problems here */
        if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
            l = this->src_pads;
            while (l) {
                g_message ("jack: unregistering pad %s:%s", GST_JACK_PAD (l)->name, GST_JACK_PAD (l)->peer_name);
                jack_port_unregister (this->client, GST_JACK_PAD (l)->port);
                l = g_list_next (l);
            }
            l = this->sink_pads;
            while (l) {
                g_message ("jack: unregistering pad %s:%s", GST_JACK_PAD (l)->name, GST_JACK_PAD (l)->peer_name);
                jack_port_unregister (this->client, GST_JACK_PAD (l)->port);
                l = g_list_next (l);
            }
            GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_OPEN);

            if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
                g_message ("jack: deactivating client");
                jack_deactivate (this->client);
                GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_ACTIVE);
            }
        }
            
        if (GST_ELEMENT_CLASS (parent_class)->change_state)
            return GST_ELEMENT_CLASS (parent_class)->change_state (element);
        break;
        
    case GST_STATE_PAUSED:
        g_message ("jackbin: PAUSED");
        
        if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
            l = this->src_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                g_message ("jack: registering output port %s (peer %s)", pad->name, pad->peer_name);
                pad->port = jack_port_register (this->client, pad->name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput|JackPortIsTerminal, 0);
                l = g_list_next (l);
            }
            l = this->sink_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                g_message ("jack: registering input port %s (peer %s)", pad->name, pad->peer_name);
                pad->port = jack_port_register (this->client, pad->name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
                l = g_list_next (l);
            }

            /* must activate before connecting */
            if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
                g_message ("jack: activating client");
                jack_activate (this->client);
                GST_FLAG_SET (GST_OBJECT (this), GST_JACK_ACTIVE);
            }

            g_cond_wait (this->cond, this->lock);

            l = this->src_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                g_message ("connecting gst jack port %s to jack port %s", jack_port_name (pad->port), pad->peer_name);
                if (jack_connect (this->client, jack_port_name (pad->port), pad->peer_name)) {
                    g_warning ("jack: could not connect %s and %s", pad->peer_name, jack_port_name (pad->port));
                    return GST_STATE_FAILURE;
                }
                l = g_list_next (l);
            }
            l = this->sink_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                g_message ("connecting gst jack port %s to jack port %s", jack_port_name (pad->port), pad->peer_name);
                if (jack_connect (this->client, jack_port_name (pad->port), pad->peer_name)) {
                    g_warning ("jack: could not connect %s and %s", pad->peer_name, jack_port_name (pad->port));
                    return GST_STATE_FAILURE;
                }
                l = g_list_next (l);
            }

            g_message ("jackbin: setting OPEN flag");
            GST_FLAG_SET (GST_OBJECT (this), GST_JACK_OPEN);
        } else {
            g_cond_wait (this->cond, this->lock);
        }

        break;
    case GST_STATE_PLAYING:
        g_message ("jack: PLAYING");

        g_cond_wait (this->cond, this->lock);
        break;
    }
    
    g_message ("jack: state change finished");
    
    return GST_STATE_SUCCESS;
}

/* jack callbacks */

/* keep in mind that these run in another thread, mm-kay? */

static int
process (jack_nframes_t nframes, void *arg)
{
    GstJackBin *bin = (GstJackBin*) arg;
    GstJackPad *pad;
    GList *l;
    
    g_assert (bin);
    
    g_message ("jack: process()");

    if (GST_STATE_PENDING (bin) != GST_STATE_VOID_PENDING) {
        g_message ("jackbin: doing state change from %s to %s",
                   gst_element_state_get_name (GST_STATE (bin)), 
                   gst_element_state_get_name (GST_STATE_PENDING (bin)));

        /* FIXME: this breaks ultra-low latency... */
        g_mutex_lock (bin->lock);
            
        switch (GST_STATE_TRANSITION (bin)) {
        case GST_STATE_READY_TO_PAUSED:
            if (!bin->sched_setup) {
                gst_scheduler_setup (GST_ELEMENT_SCHED (bin));
                bin->sched_setup = TRUE;
            }
            break;
        }
        
        /* do the chaining up from within the jack thread, so that child
           elements are initialized from within the proper thread */
        if (GST_ELEMENT_CLASS (parent_class)->change_state)
            GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT_CAST (bin));
        
        g_cond_signal (bin->cond);
        g_mutex_unlock (bin->lock);
    }

    if (GST_STATE (bin) != GST_STATE_PLAYING) {
        g_message ("jackbin: bin is not PLAYING yet, returning");
        return 0;
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
    
    if (!gst_bin_iterate (GST_BIN_CAST (bin))) {
        g_warning ("bin failed to iterate");
        return -1;
    }
    
    /* that's all folks */
    
    return 0;      
}

static int
buffer_size (jack_nframes_t nframes, void *arg)
{
    printf ("the maximum buffer size is now %lu\n", nframes);
    return 0;
}

static int
sample_rate (jack_nframes_t nframes, void *arg)
{
    GstJackBin *bin = (GstJackBin*) arg;
    printf ("the sample rate is now %lu/sec\n", nframes);
    bin->rate = nframes;
    return 0;
}

static void
shutdown (void *arg)
{
/*    GstJackClient *client = (GstJackClient*) arg; */
    printf ("shutdown %p\n", arg);
/*    gst_element_set_state (GST_ELEMENT (client->manager), GST_STATE_READY); */
}

