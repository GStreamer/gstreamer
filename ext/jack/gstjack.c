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
#include "gstjack.h"
#include <gst/audio/audio.h>


/* TODO:
   
   this element is still nonfunctional

   - work out the src side (caps setting, etc)

   future core TODO:
   - make a jack clock provider
   - add GST_ELEMENT_FIXED_DATA_RATE, GST_ELEMENT_QOS,
     GST_ELEMENT_CHANGES_DATA_RATE element flags, and make the scheduler
     sensitive to them
*/


static GstElementDetails gst_jack_sink_details = {  
    "Jack Sink",
    "Sink/Audio",
    "Output to a Jack processing network: see README for more info",
    VERSION,
    "Andy Wingo <wingo@pobox.com>",
    "(C) 2002 "
};

static GstElementDetails gst_jack_src_details = {  
    "Jack Src",
    "Source/Audio",
    "Input from a Jack processing network: see README for more info",
    VERSION,
    "Andy Wingo <wingo@pobox.com>",
    "(C) 2002",
};


static GstElement *parent_class = NULL;

static void gst_jack_init(GstJack *this);
static void gst_jack_class_init(GstJackClass *klass);

static GstPadTemplate *gst_jack_src_request_pad_factory();
static GstPadTemplate *gst_jack_sink_request_pad_factory();

static GstPad* gst_jack_request_new_pad (GstElement *element, GstPadTemplate *templ, const
                                         gchar *name);

static void gst_jack_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_jack_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstElementStateReturn gst_jack_change_state(GstElement *element);
static GstPadConnectReturn gst_jack_connect (GstPad *pad, GstCaps *caps);

static void gst_jack_loop (GstElement *element);

/* jack callbacks */
static int process (nframes_t nframes, void *arg);
static int buffer_size (nframes_t nframes, void *arg);
static int sample_rate (nframes_t nframes, void *arg);
static void shutdown (void *arg);


enum {
    ARG_0,
    ARG_PORT_NAME_PREFIX,
};


GType
gst_jack_get_type (void) 
{
  static GType jack_type = 0;

  if (!jack_type) {
    static const GTypeInfo jack_info = {
      sizeof(GstJackClass),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      sizeof(GstJack),
      0,
      NULL,
    };
    jack_type = g_type_register_static (GST_TYPE_ELEMENT, "GstJack", &jack_info, 0);
  }
  return jack_type;
}

GType
gst_jack_sink_get_type (void) 
{
    static GType jack_type = 0;

    if (!jack_type) {
        static const GTypeInfo jack_info = {
            sizeof(GstJackClass),
            NULL,
            NULL,
            (GClassInitFunc)gst_jack_class_init,
            NULL,
            NULL,
            sizeof(GstJack),
            0,
            (GInstanceInitFunc)gst_jack_init,
        };
        jack_type = g_type_register_static (GST_TYPE_JACK, "GstJackSink", &jack_info, 0);
    }
    return jack_type;
}

GType
gst_jack_src_get_type (void) 
{
    static GType jack_type = 0;
    
    if (!jack_type) {
        static const GTypeInfo jack_info = {
            sizeof(GstJackClass),
            NULL,
            NULL,
            (GClassInitFunc)gst_jack_class_init,
            NULL,
            NULL,
            sizeof(GstJack),
            0,
            (GInstanceInitFunc)gst_jack_init,
        };
        jack_type = g_type_register_static (GST_TYPE_JACK, "GstJackSrc", &jack_info, 0);
    }
    return jack_type;
}

static GstPadTemplate*
gst_jack_src_request_pad_factory(void)
{
    static GstPadTemplate *template = NULL;
    
    if (!template)
        template = gst_padtemplate_new("%s", GST_PAD_SRC, GST_PAD_REQUEST, 
                                       gst_caps_new("src", "audio/raw",
                                                    GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS),
                                       NULL);
    
    return template;
}

static GstPadTemplate*
gst_jack_sink_request_pad_factory(void)
{
    static GstPadTemplate *template = NULL;
    
    if (!template)
        template = gst_padtemplate_new("%s", GST_PAD_SINK, GST_PAD_REQUEST, 
                                       gst_caps_new("sink", "audio/raw",
                                                    GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS),
                                       NULL);
    
    return template;
}

static void
gst_jack_class_init(GstJackClass *klass)
{
    GObjectClass *object_class;
    GstElementClass *element_class;
    
    object_class = (GObjectClass *)klass;
    element_class = (GstElementClass *)klass;
    
    if (parent_class == NULL)
        parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
    
    object_class->get_property = gst_jack_get_property;
    object_class->set_property = gst_jack_set_property;
    
    g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PORT_NAME_PREFIX,
                                    g_param_spec_string("port-name-prefix","Port name prefix",
                                                        "String to prepend to jack port names",
                                                        "gst-",
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    element_class->change_state = gst_jack_change_state;
    
    element_class->request_new_pad = gst_jack_request_new_pad;
}

static void
gst_jack_init(GstJack *this)
{
    if (G_OBJECT_TYPE (this) == GST_TYPE_JACK_SRC) {
        this->direction = GST_PAD_SRC;
    } else if (G_OBJECT_TYPE (this) == GST_TYPE_JACK_SINK) {
        this->direction = GST_PAD_SINK;
    } else {
        g_assert_not_reached ();
    }
    
    gst_element_set_loop_function(GST_ELEMENT(this), gst_jack_loop);
}

static GstPad*
gst_jack_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *name) 
{
    GstJack *this;
    gchar *newname;
    GList *l, **pad_list;
    GstJackPad *pad;
    
    g_return_val_if_fail ((this = GST_JACK (element)), NULL);
    
    if (!this->client)
        pad_list = &this->pads;
    else if (this->direction == GST_PAD_SRC)
        pad_list = &this->client->src_pads;
    else
        pad_list = &this->client->src_pads;
    
    if (name) {
        l = *pad_list;
        while (l) {
            if (strcmp (GST_JACK_PAD(l)->name, name) == 0) {
                g_warning("requested port name %s already in use.", name);
                return NULL;
            }
            l = l->next;
        }
        newname = g_strdup (name);
    } else {
        newname = g_strdup ("alsa_pcm:out_1");
    }
    
    pad = g_new0(GstJackPad, 1);
    
    pad->name = g_strdup_printf ("%s%d", this->port_name_prefix, 1); /* fixme :) */
    pad->peer_name = newname;
    pad->pad = gst_pad_new_from_template (templ, newname);
    gst_element_add_pad (GST_ELEMENT (this), pad->pad);
    gst_pad_set_connect_function (pad->pad, gst_jack_connect);
    
    this->pads = g_list_append (this->pads, pad);
    
    g_print ("returning from request_new_pad, pad %s created, to connect to %s\n", pad->name, pad->peer_name);
    return pad->pad;
}

static void
gst_jack_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstJack *this;
    
    this = (GstJack *)object;
    switch (prop_id) {
    case ARG_PORT_NAME_PREFIX:
        if (this->port_name_prefix)
            g_free (this->port_name_prefix);
        this->port_name_prefix = g_strdup (g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        return;
    }
}

static void
gst_jack_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstJack *this;

    this = (GstJack *)object;

    switch (prop_id) {
    case ARG_PORT_NAME_PREFIX:
        g_value_set_string (value, this->port_name_prefix);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static GstElementStateReturn
gst_jack_change_state (GstElement *element)
{
    GstJack *this;
    GstJackClient *client;
    GstBin *bin;
    GList *l = NULL, **pads;
    enum JackPortFlags flags;
    GstJackPad *pad;
    
    g_return_val_if_fail (element != NULL, FALSE);
    this = GST_JACK (element);
    client = this->client;
    
    switch (GST_STATE_PENDING (element)) {
    case GST_STATE_NULL:
        g_message ("jack: NULL state");
        if (client) {
            g_message ("jack: closing client");
            jack_client_close (client->client);
        }
            
        break;
        
    case GST_STATE_READY:
        g_message ("jack: READY");
        if (!this->client) {
            if (!(bin = gst_element_get_managing_bin (element))) {
                g_warning ("jack element %s cannot be brought to READY state without a managing bin",
                           GST_OBJECT_NAME (GST_OBJECT (element)));
                return GST_STATE_FAILURE;
            }

            client = g_object_get_data (G_OBJECT (bin), "gst-jack-client");

            if (!client) {
                g_message ("jack: making new client");
                client = g_new0 (GstJackClient, 1);
                if (!(client->client = jack_client_new ("gst-jack"))) {
                    g_warning ("jack server not running?");
                    return GST_STATE_FAILURE;
                }
                
                jack_set_process_callback (client->client, process, client);
                jack_set_buffer_size_callback (client->client, buffer_size, client);
                jack_set_sample_rate_callback (client->client, sample_rate, client);
                jack_on_shutdown (client->client, shutdown, client);
                
                client->refcount = 1;
                g_object_set_data (G_OBJECT (bin), "gst-jack-client", client);
                client->manager = bin;
                GST_FLAG_SET (GST_OBJECT (bin), GST_BIN_SELF_SCHEDULABLE);
            } else {
                g_message ("jack: refcounting existing client");
                client->refcount++;
            }
            this->client = client;
            
            /* fixme: verify that all names are unique */
            l = this->pads;
            pads = (this->direction == GST_PAD_SRC) ? &client->src_pads : &client->sink_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                g_message ("jack: appending pad %s:%s to list", pad->name, pad->peer_name);
                *pads = g_list_append (*pads, pad);
                l = g_list_next (l);
            }
        }
        
        /* fixme: there are a *lot* of problems here */
        if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
            l = this->pads;
            while (l) {
                g_message ("jack: unregistering pad %s:%s", GST_JACK_PAD (l)->name, GST_JACK_PAD (l)->peer_name);
                jack_port_unregister (client->client, GST_JACK_PAD (l)->port);
                l = g_list_next (l);
            }
            GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_OPEN);
        }
        break;
        
    case GST_STATE_PAUSED:
        g_message ("jack: PAUSED");
        g_assert (client);
        
        if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_OPEN)) {
            l = this->pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                
                if (this->direction == GST_PAD_SINK)
                    flags = JackPortIsOutput;
                else
                    flags = JackPortIsInput;
                
                g_message ("jack: registering pad %s:%s", pad->name, pad->peer_name);
                pad->port = jack_port_register (client->client, pad->name, JACK_DEFAULT_AUDIO_TYPE, flags, 0);
                g_message ("connecting gst jack port %s to jack port %s", jack_port_name (pad->port), pad->peer_name);
                if (jack_connect (client->client, jack_port_name (pad->port), pad->peer_name)) {
                    g_warning ("could not connect %s and %s", pad->peer_name, jack_port_name (pad->port));
                    return GST_STATE_FAILURE;
                }
                
                l = g_list_next (l);
            }
            g_message ("jack: setting OPEN flag");
            GST_FLAG_SET (GST_OBJECT (this), GST_JACK_OPEN);
        }

        if (GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
            g_message ("jack: deactivating client");
            jack_deactivate (client->client);
            GST_FLAG_UNSET (GST_OBJECT (this), GST_JACK_ACTIVE);
        }
        break;
    case GST_STATE_PLAYING:
        g_message ("jack: PLAYING");
        if (!GST_FLAG_IS_SET (GST_OBJECT (this), GST_JACK_ACTIVE)) {
            g_message ("jack: activating client");
            jack_activate (client->client);
            GST_FLAG_SET (GST_OBJECT (this), GST_JACK_ACTIVE);
        }
        break;
    }
    
    g_message ("jack: state change finished");
    
    if (GST_ELEMENT_CLASS (parent_class)->change_state)
        return GST_ELEMENT_CLASS (parent_class)->change_state (element);

    return GST_STATE_SUCCESS;
}

static GstPadConnectReturn
gst_jack_connect (GstPad *pad, GstCaps *caps)
{
  GstJack *this;
  
  this = GST_JACK (gst_pad_get_parent (pad));
  g_return_val_if_fail (this != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_JACK (this), GST_PAD_CONNECT_REFUSED);
  
  if (GST_CAPS_IS_FIXED (caps)) {
      if (this->client && gst_caps_get_int (caps, "rate") != this->client->rate)
          return GST_PAD_CONNECT_REFUSED;
      
      return GST_PAD_CONNECT_OK;
  }
  
  return GST_PAD_CONNECT_DELAYED;
}

static void
gst_jack_loop (GstElement *element)
{
    GstJack *this;
    GList *pads;
    gint len;
    gchar *peeked;
    gint avail;
    GstEvent *event;
    GstJackPad *pad;
    GstBuffer *buffer;
    
    this = GST_JACK (element);
    
    g_return_if_fail(this != NULL);
    len = this->client->nframes * sizeof (sample_t);
    
    do {
        pads = this->pads;
        while (pads) {
            pad = GST_JACK_PAD (pads);
            
            if (this->direction == GST_PAD_SINK) {
                if (pad->bs)
                    pad->bs = gst_bytestream_new (pad->pad);
                
                if (!(peeked = gst_bytestream_peek_bytes (pad->bs, len))) {
                    gst_bytestream_get_status(pad->bs, &avail, &event);
                    if (event) {
                        g_warning("got an event on jacksink");
                        if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
                            /* really, we should just cut this pad out of the graph. let
                             * me know when this is needed ;)
                             * also, for sample accuracy etc, we should play avail
                             * bytes, but hey. */
                            gst_element_set_eos(GST_ELEMENT(this));
                            gst_event_free(event);
                        }
                    } else {
                        /* the element at the top of the chain did not emit an eos
                         * event. this is a Bug(tm) */
                        g_assert_not_reached();
                    }
                }
                
                memcpy (pad->data, peeked, len);
                gst_bytestream_flush (pad->bs, len);
            } else {
                buffer = gst_buffer_new ();
                GST_BUFFER_DATA (buffer)    = pad->data;
                GST_BUFFER_SIZE (buffer)    = len;
                GST_BUFFER_MAXSIZE (buffer) = len;
                
                gst_pad_push (pad->pad, buffer);
            }
            pads = g_list_next (pads);
        }
        
	gst_element_yield (element);
    } while (TRUE);
}

/* jack callbacks */

static int
process (nframes_t nframes, void *arg)
{
    GstJackClient *client = (GstJackClient*) arg;
    GstJackPad *pad;
    GList *l;
    
    g_assert (client);
    
    g_message ("jack: process()");

    l = client->src_pads;
    while (l) {
        pad = GST_JACK_PAD (l);
        pad->data = jack_port_get_buffer (pad->port, nframes);
        l = g_list_next (l);
    }
    
    l = client->sink_pads;
    while (l) {
        pad = GST_JACK_PAD (l);
        pad->data = jack_port_get_buffer (pad->port, nframes);
        l = g_list_next (l);
    }
    
    client->nframes = nframes;
    
    if (!gst_bin_iterate (client->manager)) {
        g_warning ("bin failed to iterate");
        return -1;
    }
    
    /* that's all folks */
    
    return 0;      
}

static int
buffer_size (nframes_t nframes, void *arg)
{
    printf ("the maximum buffer size is now %lu\n", nframes);
    return 0;
}

static int
sample_rate (nframes_t nframes, void *arg)
{
    GstJackClient *client = (GstJackClient*) arg;
    printf ("the sample rate is now %lu/sec\n", nframes);
    client->rate = nframes;
    return 0;
}

static void
shutdown (void *arg)
{
    GstJackClient *client = (GstJackClient*) arg;
    printf ("shutdown\n");
/*    gst_element_set_state (GST_ELEMENT (client->manager), GST_STATE_READY); */
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
    GstElementFactory *factory;
    
    if (!gst_library_load ("gstbytestream")) {
        gst_info("jack: could not load support library: 'gstbytestream'\n");
        return FALSE;
    }
    
    factory = gst_elementfactory_new ("jacksrc", GST_TYPE_JACK_SRC, &gst_jack_src_details);
    g_return_val_if_fail (factory != NULL, FALSE);
    gst_elementfactory_add_padtemplate (factory, gst_jack_src_request_pad_factory());
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    
    factory = gst_elementfactory_new ("jacksink", GST_TYPE_JACK_SINK, &gst_jack_sink_details);
    g_return_val_if_fail (factory != NULL, FALSE);
    gst_elementfactory_add_padtemplate (factory, gst_jack_sink_request_pad_factory());
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    
    gst_plugin_set_longname(plugin, "JACK plugin library");
    
    return TRUE;
}

GstPluginDesc plugin_desc = {
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "jack",
    plugin_init
};


