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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
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

/* elementfactory information */
static GstElementDetails gst_jack_bin_details = {
    "Jack Bin",
    "Generic/Bin",
    "GPL",
    "Jack processing bin: see README for more info",
    VERSION,
    "Andy Wingo <wingo@pobox.com>",
    "(C) 2002 "
};

static GstElementDetails gst_jack_sink_details = {  
    "Jack Sink",
    "Sink/Audio",
    "GPL",
    "Output to a Jack processing network: see README for more info",
    VERSION,
    "Andy Wingo <wingo@pobox.com>",
    "(C) 2002 "
};

static GstElementDetails gst_jack_src_details = {  
    "Jack Src",
    "Source/Audio",
    "GPL",
    "Input from a Jack processing network: see README for more info",
    VERSION,
    "Andy Wingo <wingo@pobox.com>",
    "(C) 2002",
};


static GHashTable *port_name_counts = NULL;
static GstElementClass *parent_class = NULL;

static void gst_jack_init(GstJack *this);
static void gst_jack_class_init(GstJackClass *klass);

static GstPadTemplate *gst_jack_src_request_pad_factory();
static GstPadTemplate *gst_jack_sink_request_pad_factory();

static GstPad* gst_jack_request_new_pad (GstElement *element, GstPadTemplate *templ, const
                                         gchar *name);

static void gst_jack_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_jack_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstElementStateReturn gst_jack_change_state(GstElement *element);
static GstPadLinkReturn gst_jack_connect (GstPad *pad, GstCaps *caps);

static void gst_jack_loop (GstElement *element);


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
    
    if (!template) {
	GstCaps *caps;
	caps = gst_caps_new("src",
			    "audio/x-raw-float",
                            GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_PROPS);
        template = gst_pad_template_new("%s", GST_PAD_SRC,
					GST_PAD_REQUEST, caps, NULL);
    }
    
    return template;
}

static GstPadTemplate*
gst_jack_sink_request_pad_factory(void)
{
    static GstPadTemplate *template = NULL;
    
    if (!template) {
	GstCaps *caps;
	caps = gst_caps_new ("sink",
			     "audio/x-raw-float",
                             GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_PROPS);
        template = gst_pad_template_new("%s", GST_PAD_SINK,
					GST_PAD_REQUEST, caps, NULL);
    }
    
    return template;
}

static void
gst_jack_class_init(GstJackClass *klass)
{
    GObjectClass *object_class;
    GstElementClass *element_class;
    gchar *prefix;
    
    object_class = (GObjectClass *)klass;
    element_class = (GstElementClass *)klass;
    
    if (parent_class == NULL)
        parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
    
    object_class->get_property = gst_jack_get_property;
    object_class->set_property = gst_jack_set_property;
    
    if (GST_IS_JACK_SINK_CLASS (klass))
        prefix = "gst-out-";
    else
        prefix = "gst-in-";
    
    g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PORT_NAME_PREFIX,
                                    g_param_spec_string("port-name-prefix","Port name prefix",
                                                        "String to prepend to jack port names",
                                                        prefix,
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
    gint count;
    
    g_return_val_if_fail ((this = GST_JACK (element)), NULL);
    
    if (!this->bin)
        pad_list = &this->pads;
    else if (this->direction == GST_PAD_SRC)
        pad_list = &this->bin->src_pads;
    else
        pad_list = &this->bin->sink_pads;
    
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
        if (this->direction == GST_PAD_SINK)
            newname = g_strdup ("alsa_pcm:playback_1");
        else
            newname = g_strdup ("alsa_pcm:capture_1");
    }
    
    pad = g_new0(GstJackPad, 1);
    
    if (!port_name_counts)
        port_name_counts = g_hash_table_new (g_str_hash, g_str_equal);

    count = GPOINTER_TO_INT (g_hash_table_lookup (port_name_counts, this->port_name_prefix));
    g_hash_table_insert (port_name_counts, g_strdup (this->port_name_prefix), GINT_TO_POINTER (count+1));

    pad->name = g_strdup_printf ("%s%d", this->port_name_prefix, count);

    pad->peer_name = newname;
    pad->pad = gst_pad_new_from_template (templ, newname);
    gst_element_add_pad (GST_ELEMENT (this), pad->pad);
    gst_pad_set_link_function (pad->pad, gst_jack_connect);
    
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
    GList *l = NULL, **pads;
    GstJackPad *pad;
    GstCaps *caps;
    
    g_return_val_if_fail (element != NULL, FALSE);
    this = GST_JACK (element);
    
    switch (GST_STATE_PENDING (element)) {
    case GST_STATE_NULL:
        JACK_DEBUG ("%s: NULL", GST_OBJECT_NAME (GST_OBJECT (this)));

        break;
        
    case GST_STATE_READY:
        JACK_DEBUG ("%s: READY", GST_OBJECT_NAME (GST_OBJECT (this)));

        if (!this->bin) {
            if (!(this->bin = (GstJackBin*)gst_element_get_managing_bin (element))
                || !GST_IS_JACK_BIN (this->bin)) {
                this->bin = NULL;
                g_warning ("jack element %s needs to be contained in a jack bin.",
                           GST_OBJECT_NAME (GST_OBJECT (element)));
                return GST_STATE_FAILURE;
            }

            /* fixme: verify that all names are unique */
            l = this->pads;
            pads = (this->direction == GST_PAD_SRC) ? &this->bin->src_pads : &this->bin->sink_pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                JACK_DEBUG ("%s: appending pad %s:%s to list", GST_OBJECT_NAME (this), pad->name, pad->peer_name);
                *pads = g_list_append (*pads, pad);
                l = g_list_next (l);
            }
        }
        break;
        
    case GST_STATE_PAUSED:
        JACK_DEBUG ("%s: PAUSED", GST_OBJECT_NAME (GST_OBJECT (this)));

        if (GST_STATE (element) == GST_STATE_READY) {
            /* we're in READY->PAUSED */
            l = this->pads;
            while (l) {
                pad = GST_JACK_PAD (l);
                caps = gst_pad_get_caps (pad->pad);
                gst_caps_set (caps, "rate", GST_PROPS_INT_TYPE, (gint) this->bin->rate, NULL);
                if (gst_pad_try_set_caps (pad->pad, caps) <= 0)
                    return GST_STATE_FAILURE;
                l = g_list_next (l);
            }
        }
        break;
    case GST_STATE_PLAYING:
        JACK_DEBUG ("%s: PLAYING", GST_OBJECT_NAME (GST_OBJECT (this)));
        break;
    }
    
    JACK_DEBUG ("%s: state change finished", GST_OBJECT_NAME (this));
    
    if (GST_ELEMENT_CLASS (parent_class)->change_state)
        return GST_ELEMENT_CLASS (parent_class)->change_state (element);

    return GST_STATE_SUCCESS;
}

static GstPadLinkReturn
gst_jack_connect (GstPad *pad, GstCaps *caps)
{
  GstJack *this;
  gint rate;
  
  this = GST_JACK (gst_pad_get_parent (pad));
  g_return_val_if_fail (this != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_JACK (this), GST_PAD_LINK_REFUSED);
  
  if (GST_CAPS_IS_FIXED (caps)) {
      gst_caps_get_int (caps, "rate", &rate);
      if (this->bin && rate != this->bin->rate)
          return GST_PAD_LINK_REFUSED;
      
      return GST_PAD_LINK_OK;
  }
  
  return GST_PAD_LINK_DELAYED;
}

static void
gst_jack_loop (GstElement *element)
{
    GstJack *this;
    GList *pads;
    gint len, peeked_len;
    guint8 *peeked;
    gint avail;
    GstEvent *event;
    GstJackPad *pad;
    GstBuffer *buffer;
    
    this = GST_JACK (element);
    
    g_return_if_fail(this != NULL);
    len = this->bin->nframes * sizeof (jack_default_audio_sample_t);
    
    do {
        pads = this->pads;
        while (pads) {
            pad = GST_JACK_PAD (pads);
            
            if (this->direction == GST_PAD_SINK) {
                if (!pad->bs)
                    pad->bs = gst_bytestream_new (pad->pad);
                
            read:
                peeked_len = gst_bytestream_peek_bytes (pad->bs, &peeked, len);
                if (peeked_len < len) {
                    gst_bytestream_get_status(pad->bs, &avail, &event);
                    if (event) {
                        g_warning("got an event on jacksink");
                        if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
                            /* really, we should just cut this pad out of the graph. let
                             * me know when this is needed ;)
                             * also, for sample accuracy etc, we should play avail
                             * bytes, but hey. */
                            gst_element_set_eos (element);
                            gst_event_unref (event);
//                            gst_element_yield (element); /* shouldn't return */
                            return;
                        }
                        goto read;
                    } else {
                        /* the element at the top of the chain did not emit an eos
                         * event. this is a Bug(tm) */
                        g_assert_not_reached();
                    }
                }
                
                
                memcpy (pad->data, peeked, peeked_len);
                gst_bytestream_flush (pad->bs, peeked_len);
            } else {
                buffer = gst_buffer_new ();
                GST_BUFFER_DATA (buffer)    = pad->data;
                GST_BUFFER_SIZE (buffer)    = len;
                GST_BUFFER_MAXSIZE (buffer) = len;
		GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_DONTFREE);
                
                gst_pad_push (pad->pad, buffer);
            }
            pads = g_list_next (pads);
        }
        
        return;
//	gst_element_yield (element);
    } while (TRUE);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
    GstElementFactory *factory;
    
    if (!gst_library_load ("gstbytestream"))
        return FALSE;
    
    factory = gst_element_factory_new ("jackbin", GST_TYPE_JACK_BIN, &gst_jack_bin_details);
    g_return_val_if_fail (factory != NULL, FALSE);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    
    factory = gst_element_factory_new ("jacksrc", GST_TYPE_JACK_SRC, &gst_jack_src_details);
    g_return_val_if_fail (factory != NULL, FALSE);
    gst_element_factory_add_pad_template (factory, gst_jack_src_request_pad_factory());
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    
    factory = gst_element_factory_new ("jacksink", GST_TYPE_JACK_SINK, &gst_jack_sink_details);
    g_return_val_if_fail (factory != NULL, FALSE);
    gst_element_factory_add_pad_template (factory, gst_jack_sink_request_pad_factory());
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


