/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wtay@chello.be>
 *
 * gstspider.c: element to automatically connect sinks and sources
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
 
/**
 * TODO:
 * - handle automatic removal of unneeded elements
 * - make the spider handle and send events (esp. new media)
 * - allow disconnecting
 * - implement a way to allow merging/splitting (aka tee)
 * - find ways to define which elements to use when plugging
 * - remove pads
 * - improve typefinding
 * - react to errors inside the pipeline
 * - implement more properties, change the current
 * - emit signals (most important: "NOT PLUGGABLE")
 * - implement something for reporting the state of the spider
 *   to allow easier debugging.
 *   (could be useful for bins in general)
 * - fix bugs
 * ...
 */
 
#include "gstspider.h"
#include "gstspideridentity.h"
#include "gstsearchfuncs.h"
 
/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_PLUGTYPE,
  /* FILL ME TOO */
};

/* generic templates */
GST_PADTEMPLATE_FACTORY (spider_src_factory,
  "src_%02d",
  GST_PAD_SRC,
  GST_PAD_REQUEST,
  NULL      /* no caps */
);

GST_PADTEMPLATE_FACTORY (spider_sink_factory,
  "sink_%02d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  NULL      /* no caps */
);
/* standard GObject stuff */
static void                        gst_spider_class_init                         (GstSpiderClass *klass);
static void                        gst_spider_init                               (GstSpider *spider);
static void                        gst_spider_dispose                            (GObject *object);

/* element class functions */
static GstPad*                     gst_spider_request_new_pad                    (GstElement *element, GstPadTemplate *templ, const gchar *name);
static void                        gst_spider_set_property                       (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void                        gst_spider_get_property                       (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* autoplugging functions */
static GstElement *                gst_spider_find_element_to_plug               (GstElement *src, GstElementFactory *fac, GstPadDirection dir);
static GstPadConnectReturn         gst_spider_plug_peers                         (GstSpider *spider, GstSpiderIdentity *src, GstSpiderIdentity *sink);
static GstPadConnectReturn         gst_spider_create_and_plug                    (GstSpider *spider, GstElement *src, GstElement *sink, GList *plugpath);

/* random functions */
static gchar *			   gst_spider_unused_elementname                 (GstBin *bin, const gchar *startwith);

/* === variables === */
static                             GstElementClass                               *parent_class = NULL;

/* no signals yet
static guint gst_spider_signals[LAST_SIGNAL] = { 0 };*/

/* GObject and GStreamer init functions */
GType
gst_spider_get_type(void)
{
  static GType spider_type = 0;

  if (!spider_type) {
    static const GTypeInfo spider_info = {
      sizeof(GstSpiderClass),      NULL,
      NULL,
      (GClassInitFunc)gst_spider_class_init,
      NULL,
      NULL,
      sizeof(GstSpider),
      0,
      (GInstanceInitFunc)gst_spider_init,
    };
    spider_type = g_type_register_static (GST_TYPE_BIN, "GstSpider", &spider_info, 0);
  }
  return spider_type;
}

static void
gst_spider_class_init (GstSpiderClass *klass)
{
  GObjectClass     *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_BIN);

  /* properties */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PLUGTYPE,
  g_param_spec_int ("plugtype", "plug direction", "encoding, decoding or anything",
                      GST_SPIDER_ANY, GST_SPIDER_PLUGTYPES - 1, 0, G_PARAM_READWRITE));

  gobject_class->set_property = gst_spider_set_property;
  gobject_class->get_property = gst_spider_get_property;

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_spider_request_new_pad);
}
static void 
gst_spider_init (GstSpider *spider) 
{
  spider->plugtype = GST_SPIDER_ANY;
}

static GstPad *
gst_spider_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *name)
{
  GstPad *returnpad;
  gchar *padname;
  GstSpiderIdentity *identity;
  GstSpider *spider;
  
  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_PADTEMPLATE (templ), NULL);
  
  spider = GST_SPIDER (element);
  
  /* create an identity object, so we have a pad */
  switch ( GST_PADTEMPLATE_DIRECTION (templ))
  {
    case GST_PAD_SRC:
      padname = gst_spider_unused_elementname ((GstBin *)spider, "src_");
      identity = gst_spider_identity_new_src (padname);
      break;
    case GST_PAD_SINK:
      padname = gst_spider_unused_elementname ((GstBin *)spider, "sink_");
      identity = gst_spider_identity_new_sink (padname);
      break;
    case GST_PAD_UNKNOWN:
    default:
      g_warning("Spider: you must request a source or sink pad.");
      return NULL;
  }
  
  /* connect a ghost pad on the right side of the identity and set the requested template */
  returnpad = gst_spider_identity_request_new_pad  (GST_ELEMENT (identity), templ, NULL);

  /* FIXME: use the requested name for the pad */

  gst_object_ref (GST_OBJECT (templ));
  GST_PAD_PADTEMPLATE (returnpad) = templ;
  
  gst_bin_add (GST_BIN (element), GST_ELEMENT (identity));
  
  returnpad = gst_element_add_ghost_pad (element, returnpad, padname);
  GST_DEBUG (GST_CAT_ELEMENT_PADS, "successuflly created requested pad %s:%s\n", GST_DEBUG_PAD_NAME (returnpad));
  
  return returnpad;
}

static void
gst_spider_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSpider *spider;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SPIDER (object));

  spider = GST_SPIDER (object);

  switch (prop_id) {
    case ARG_PLUGTYPE:
      switch (g_value_get_int (value))
      {
        case GST_SPIDER_ANY:
          spider->plugtype = GST_SPIDER_ANY;
          GST_DEBUG (0,"spider: setting plugtype to ANY\n");
          break;
        case GST_SPIDER_ENCODE:
          spider->plugtype = GST_SPIDER_ENCODE;
          GST_DEBUG (0,"spider: setting plugtype to ENCODE\n");
          break;
        case GST_SPIDER_DECODE:
          spider->plugtype = GST_SPIDER_DECODE;
          GST_DEBUG (0,"spider: setting plugtype to DECODE\n");
          break;
        default:
          GST_DEBUG (0,"spider: invalid value %d while setting plugtype\n", g_value_get_int (value));
          break;
      }
      break;
    default:
      break;
  }
}
static void
gst_spider_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSpider *spider;

  /* it's not null if we got it, but it might not be ours */
  spider = GST_SPIDER(object);

  switch (prop_id) {
    case ARG_PLUGTYPE:
      g_value_set_int (value, spider->plugtype);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
/* get a name for an element that isn't used yet */
static gchar *
gst_spider_unused_elementname (GstBin *bin, const gchar *startwith)
{
  gchar * name = g_strdup_printf ("%s%d", startwith, 0);
  guint i;
  
  for (i = 0; gst_bin_get_by_name (bin, name) != NULL; )
  {
    g_free (name);
    name = g_strdup_printf ("%s%d", startwith, ++i);
  }
  
  return name;
}
/* callback and struct for the data supplied to the callback that is used to connect dynamic pads */
typedef struct {
  GstSpider *spider;
  GstElement *sink;
  GList *plugpath;
  gulong signal_id;
} GstSpiderConnectSometimes;
static void
gst_spider_connect_sometimes (GstElement *src, GstPad *pad, GstSpiderConnectSometimes *data)
{
  gboolean restart = FALSE;
  if (gst_element_get_state ((GstElement *) data->spider) == GST_STATE_PLAYING)
  {
    restart = TRUE;
    gst_element_set_state ((GstElement *) data->spider, GST_STATE_PAUSED);
  }
  gst_spider_create_and_plug (data->spider, src, data->sink, data->plugpath);
  g_signal_handler_disconnect (src, data->signal_id);
  if (restart)
  {
    gst_element_set_state ((GstElement *) data->spider, GST_STATE_PLAYING);
  }
  g_free (data);
  
  gst_element_interrupt (src);
}
/* connects newsrc to newsink using the elementfactories in plugpath */
static GstPadConnectReturn
gst_spider_create_and_plug (GstSpider *spider, GstElement *src, GstElement *sink, GList *plugpath)
{
  GstElement *element;
  
  /* get the next element */
  if (plugpath == NULL)
  {
    element = sink;
  } else {
    element = gst_elementfactory_create ((GstElementFactory *) plugpath->data, 
                             gst_spider_unused_elementname (GST_BIN (spider), GST_OBJECT_NAME (plugpath->data)));
    gst_bin_add (GST_BIN (spider), element);
  }
  /* insert and connect new element */
  if (!gst_element_connect_elements (src, element))
  {
    /* check if the src has SOMETIMES templates. If so, connect a callback */
    GList *templs = gst_element_get_padtemplate_list (src);
	 
    /* remove element that couldn't be connected, if it wasn't the endpoint */
    if (element != sink)
      gst_bin_remove (GST_BIN (spider), element);
    
    while (templs) {
      GstPadTemplate *templ = (GstPadTemplate *) templs->data;
      if ((GST_PADTEMPLATE_DIRECTION (templ) == GST_PAD_SRC) && (GST_PADTEMPLATE_PRESENCE(templ) == GST_PAD_SOMETIMES))
      {
	GstSpiderConnectSometimes *data = g_new (GstSpiderConnectSometimes, 1);
        GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "adding callback to connect element %s to %s\n", GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (element));
	data->spider = spider;
	data->sink = sink;
	data->plugpath = plugpath;
	data->signal_id = g_signal_connect (G_OBJECT (src), "new_pad", 
		                            G_CALLBACK (gst_spider_connect_sometimes), data);
 
	return GST_PAD_CONNECT_DELAYED;
      }
      templs = g_list_next (templs);
    }
    GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "no chance to connect element %s to %s\n", GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (element));
    g_list_free (plugpath);
    return GST_PAD_CONNECT_REFUSED;
  }
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "added element %s and attached it to element %s\n", GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (src));
  plugpath = g_list_delete_link (plugpath, plugpath);
  
  /* recursively connect the rest of the elements */
  if (element != sink) {
    return gst_spider_create_and_plug (spider, element, sink, plugpath);
  }
  
  return GST_PAD_CONNECT_DONE;
}
/* checks, if src is already connected to an element from factory fac on direction dir */
static GstElement *
gst_spider_find_element_to_plug (GstElement *src, GstElementFactory *fac, GstPadDirection dir)
{
  GList *padlist = GST_ELEMENT_PADS (src);
  
  while (padlist)
  {
    GstPad *pad = (GstPad *) GST_PAD_REALIZE (padlist->data);
    /* is the pad on the right side and is it connected? */
    if ((GST_PAD_DIRECTION (pad) == dir) && (pad = GST_PAD (GST_RPAD_PEER (pad))))
    {
      /* is the element the pad is connected to of the right type? */
      GstElement *element = GST_PAD_PARENT (pad);
      if (GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element))->elementfactory == fac) {
        return element;
      }
    }
    padlist = g_list_next (padlist);
  }
  
  return NULL;
}
/* plugs a pad into the autoplugger if it isn't plugged yet */
void
gst_spider_plug  (GstSpiderIdentity *ident)
{
  GstSpider *spider;
  GList *plugto;
  GstPad *plugpad;
  
  /* checks */
  g_return_if_fail (ident != NULL);
  g_return_if_fail (GST_IS_SPIDER_IDENTITY (ident));
  spider = GST_SPIDER (GST_ELEMENT_PARENT (ident));
  g_assert (spider != NULL);
  g_assert (GST_IS_SPIDER (spider));
  
  /* return, if we're already plugged */
  if (ident->plugged) return;
  
  if (ident->sink && GST_PAD_PEER (ident->sink))
  {
    if (ident->src && GST_PAD_PEER (ident->src))
    {
      /* Hey, the ident is connected on both sides */
      g_warning ("Trying to autoplug a connected element. Aborting...");
      return;
    } else {
      plugpad = ident->sink;
    }
  } else {
    if (ident->src && GST_PAD_PEER (ident->src))
    {
      plugpad = ident->src;
    } else {
      /* the ident isn't connected on either side */
      g_warning ("Trying to autoplug an unconnected element. Aborting...");
      return;
    }
  }

  /* now iterate all possible pads and connect */
  plugto = gst_element_get_pad_list (GST_ELEMENT (spider));
  while (plugto)
  {
    GstPad *otherpad = (GstPad *) GST_GPAD_REALPAD (plugto->data);
    GstSpiderIdentity *peer = (GstSpiderIdentity *) GST_PAD_PARENT (otherpad);
    /* we only want to connect to the other side */
    if (GST_PAD_DIRECTION (plugpad) != GST_PAD_DIRECTION (otherpad))
    {
      /* we only connect to plugged in elements */
      if (peer->plugged == TRUE) 
      {
        /* plug in the right direction */
        if (plugpad == ident->src)
        {
          gst_spider_plug_peers (spider, peer, ident);
        } else {
          gst_spider_plug_peers (spider, ident, peer);
        }
      }
    }
    plugto = g_list_next (plugto);
  }
  
  ident->plugged = TRUE;
}
/* connect the src Identity element to the sink identity element
 * Returns: DONE, if item could be plugged, DELAYED, if a callback is needed or REFUSED,
 * if no connection was possible
 */
static GstPadConnectReturn
gst_spider_plug_peers (GstSpider *spider, GstSpiderIdentity *src, GstSpiderIdentity *sink)
{
  GstElement *element, *newsrc, *newsink;
  GstPad *pad, *compat;
  GList *plugpath;
  GList *neededfactories;
  GList *templist;
  gboolean result = TRUE;
  
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "trying to plug from %s to %s\n", GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink));
  
  neededfactories = (GList *) gst_elementfactory_get_list ();
  /* use only elements which have sources and sinks and where the sinks have caps */
  neededfactories = gst_autoplug_factories_filters_with_sink_caps (neededfactories);
  
  /* use only the elements with exactly 1 sink/src when decoding/encoding */
  if (spider->plugtype == GST_SPIDER_ENCODE)
  {
    templist = neededfactories;
    neededfactories = gst_autoplug_factories_at_most_templates (neededfactories, GST_PAD_SRC, 1);
    g_list_free (templist);
  }
  if (spider->plugtype == GST_SPIDER_DECODE)
  {
    templist = neededfactories;
    neededfactories = gst_autoplug_factories_at_most_templates (neededfactories, GST_PAD_SINK, 1);
    g_list_free (templist);
  }
  
  /* find a path from src to sink */
  plugpath = gst_autoplug_sp (gst_pad_get_caps ((GstPad *) GST_RPAD_PEER (src->sink)), gst_pad_get_caps ((GstPad *) GST_RPAD_PEER (sink->src)), neededfactories);
  
  /* if there is no way to plug: return */
  if (plugpath == NULL) {
    GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "no chance to plug from %s to %s\n", GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink));
    return FALSE;
  }
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "found a connection that needs %d elements\n", g_list_length (plugpath));

  /* now remove non-needed elements from the beginning of the path 
   * alter src to point to the new element where we need to start 
   * plugging and alter the plugpath to represent the elements, that must be plugged
   */
  newsrc = (GstElement *) src;
  while (element = gst_spider_find_element_to_plug (newsrc, (GstElementFactory *) plugpath->data, GST_PAD_SRC))
  {
    newsrc = element;
    plugpath = g_list_delete_link (plugpath, plugpath);
  }
  /* now do the same at the end */
  newsink = (GstElement *) sink;
  templist = g_list_last (plugpath);
  while (element = gst_spider_find_element_to_plug (newsink, (GstElementFactory *) plugpath->data, GST_PAD_SINK))
  {
    GList *cur = templist;
    newsink = element;
    templist = g_list_previous (templist);
    g_list_delete_link (cur, cur);    
  }
  
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT, "%d elements must be inserted to establish the connection\n", g_list_length (plugpath));
  /* create the elements and plug them */
  result = gst_spider_create_and_plug (spider, newsrc, newsink, plugpath);

  /* free no longer needed data */
  g_list_free (neededfactories);
  
  return result;  
}

GstElementDetails gst_spider_details = {
  "Spider",
  "Filter/Autplug",
  "Automatically connect sinks and sources",
  VERSION,
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
  "(C) 2002",
};

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("spider", GST_TYPE_SPIDER,
                                   &gst_spider_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (spider_src_factory));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (spider_sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstspider",
  plugin_init
};

  
  