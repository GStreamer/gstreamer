/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wtay@chello.be>
 *
 * gstspideridentity.c: IDentity element for the spider autoplugger
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


#include "gstspideridentity.h"

#include "gstspider.h"

GstElementDetails gst_spider_identity_details = {
  "SpiderIdentity",
  "Filter/Autoplug",
  "connection between spider and outside elements",
  VERSION,
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
  "(C) 2002",
};


/* generic templates 
 * delete me when meging with spider.c
 */
GST_PAD_TEMPLATE_FACTORY (spider_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_REQUEST,
  NULL      /* no caps */
);

GST_PAD_TEMPLATE_FACTORY (spider_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  NULL      /* no caps */
);

/* SpiderIdentity signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

/* GObject stuff */
static void			gst_spider_identity_class_init		(GstSpiderIdentityClass *klass);
static void			gst_spider_identity_init		(GstSpiderIdentity *spider_identity);

/* functions set in pads, elements and stuff */
static void			gst_spider_identity_chain		(GstPad *pad, GstBuffer *buf);
static GstElementStateReturn	gst_spider_identity_change_state	(GstElement *element);
static GstPadConnectReturn	gst_spider_identity_connect		(GstPad *pad, GstCaps *caps);
static GstCaps *		gst_spider_identity_getcaps		(GstPad *pad, GstCaps *caps);
/* loop functions */
static void			gst_spider_identity_dumb_loop		(GstSpiderIdentity *ident);
static void                     gst_spider_identity_src_loop		(GstSpiderIdentity *ident);
static void                     gst_spider_identity_sink_loop_type_finding (GstSpiderIdentity *ident);
static void                     gst_spider_identity_sink_loop_emptycache (GstSpiderIdentity *ident);

static gboolean 		gst_spider_identity_handle_src_event 	(GstPad *pad, GstEvent *event);

/* set/get functions */
static void			gst_spider_identity_set_caps		(GstSpiderIdentity *identity, GstCaps *caps);

/* callback */
static void			callback_type_find_have_type		(GstElement *typefind, GstCaps *caps, GstSpiderIdentity *identity);

/* other functions */
static void			gst_spider_identity_start_type_finding	(GstSpiderIdentity *ident);

static GstElementClass *	parent_class				= NULL;
/* no signals
static guint gst_spider_identity_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_spider_identity_get_type (void) 
{
  static GType spider_identity_type = 0;

  if (!spider_identity_type) {
    static const GTypeInfo spider_identity_info = {
      sizeof(GstSpiderIdentityClass),      NULL,
      NULL,
      (GClassInitFunc)gst_spider_identity_class_init,
      NULL,
      NULL,
      sizeof(GstSpiderIdentity),
      0,
      (GInstanceInitFunc)gst_spider_identity_init,
    };
    spider_identity_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSpiderIdentity", &spider_identity_info, 0);
  }
  return spider_identity_type;
}

static void 
gst_spider_identity_class_init (GstSpiderIdentityClass *klass) 
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  /* add our two pad templates */
  gst_element_class_add_pad_template (gstelement_class, GST_PAD_TEMPLATE_GET (spider_src_factory));
  gst_element_class_add_pad_template (gstelement_class, GST_PAD_TEMPLATE_GET (spider_sink_factory));
  
  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_spider_identity_change_state);
  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_spider_identity_request_new_pad);
}
/* defined but not used
static GstBufferPool*
gst_spider_identity_get_bufferpool (GstPad *pad)
{*/
  /* fix me */
/*  GstSpiderIdentity *spider_identity;

  spider_identity = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_get_bufferpool (spider_identity->src);
}*/

static void 
gst_spider_identity_init (GstSpiderIdentity *ident) 
{
  /* sink */
  ident->sink = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (spider_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (ident), ident->sink);
  gst_pad_set_connect_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_connect));
  gst_pad_set_getcaps_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
  /* src */
  ident->src = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (spider_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (ident), ident->src);
  gst_pad_set_connect_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_connect));
  gst_pad_set_getcaps_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
  gst_pad_set_event_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_handle_src_event));

  /* variables */
  ident->plugged = FALSE;
  
  /* caching */
  ident->cache_start = NULL;
  ident->cache_end = NULL;
  
}

static void 
gst_spider_identity_chain (GstPad *pad, GstBuffer *buf) 
{
  GstSpiderIdentity *ident;
  
  /* g_print ("chaining on pad %s:%s with buffer %p\n", GST_DEBUG_PAD_NAME (pad), buf); */

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  if (buf == NULL) return;

  ident = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    /* start hack for current event stuff here */
    /* check for unconnected elements and send them the EOS event, too */
    if (GST_EVENT_TYPE (GST_EVENT (buf)) == GST_EVENT_EOS)
    {
      GstSpider *spider = (GstSpider *) GST_OBJECT_PARENT (ident);
      GList *list = spider->connections;
      while (list)
      {
	GstSpiderConnection *conn = (GstSpiderConnection *) list->data;
	list = g_list_next (list);
	if (conn->sink == ident)
	{
	  gst_element_set_eos (GST_ELEMENT (conn->src));
          gst_pad_push (conn->src->src, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));  
	}
      }
    }
    /* end hack for current event stuff here */

    gst_pad_event_default (ident->sink, GST_EVENT (buf));
    return;
  }

  if ((ident->src != NULL) && (GST_PAD_PEER (ident->src) != NULL)) {
    /* g_print("pushing buffer %p (refcount %d - buffersize %d) to pad %s:%s\n", buf, GST_BUFFER_REFCOUNT (buf), GST_BUFFER_SIZE (buf), GST_DEBUG_PAD_NAME (ident->src)); */
    gst_pad_push (ident->src, buf);
  } else if (GST_IS_BUFFER (buf)) {
    gst_buffer_unref (buf);
  }
}
GstSpiderIdentity*           
gst_spider_identity_new_src (gchar *name)
{
  GstSpiderIdentity *ret = (GstSpiderIdentity *) g_object_new (gst_spider_identity_get_type (), NULL);
  
  GST_ELEMENT_NAME (ret) = name;
  /* set the right functions */
  gst_element_set_loop_function (GST_ELEMENT (ret), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_src_loop));
  
  return ret;
}
GstSpiderIdentity*           
gst_spider_identity_new_sink (gchar *name)
{
  GstSpiderIdentity *ret = (GstSpiderIdentity *) g_object_new (gst_spider_identity_get_type (), NULL);
  
  GST_ELEMENT_NAME (ret) = name;

  /* set the right functions */
  gst_element_set_loop_function (GST_ELEMENT (ret), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_dumb_loop));

  return ret;
}

/* shamelessly stolen from gstqueue.c to get proxy connections */
static GstPadConnectReturn
gst_spider_identity_connect (GstPad *pad, GstCaps *caps)
{
  GstSpiderIdentity *spider_identity = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));
  GstPad *otherpad;

  if (pad == spider_identity->src) 
    otherpad = spider_identity->sink;
  else
    otherpad = spider_identity->src;

  if (otherpad != NULL)
    return gst_pad_proxy_connect (otherpad, caps);
  
  return GST_PAD_CONNECT_OK;
}

static GstCaps*
gst_spider_identity_getcaps (GstPad *pad, GstCaps *caps)
{
  GstSpiderIdentity *spider_identity = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));
  GstPad *otherpad;

  if (pad == spider_identity->src) 
    otherpad = spider_identity->sink;
  else
    otherpad = spider_identity->src;

  if (otherpad != NULL)
    return gst_pad_get_allowed_caps (otherpad);
  return NULL;
}

GstPad*
gst_spider_identity_request_new_pad  (GstElement *element, GstPadTemplate *templ, const gchar *name)
{
  GstSpiderIdentity *ident;
  
  /*checks */
  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);
  ident = GST_SPIDER_IDENTITY (element);
  g_return_val_if_fail (ident != NULL, NULL);
  g_return_val_if_fail (GST_IS_SPIDER_IDENTITY (ident), NULL);
  
  switch (GST_PAD_TEMPLATE_DIRECTION (templ))
  {
    case GST_PAD_SINK:
      if (ident->sink != NULL) break;
      /* sink */
      GST_DEBUG(0, "element %s requests new sink pad", GST_ELEMENT_NAME(ident));
      ident->sink = gst_pad_new ("sink", GST_PAD_SINK);
      gst_element_add_pad (GST_ELEMENT (ident), ident->sink);
      gst_pad_set_connect_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_connect));
      gst_pad_set_getcaps_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
      return ident->sink;
    case GST_PAD_SRC:
      /* src */
      if (ident->src != NULL) break;
      GST_DEBUG(0, "element %s requests new src pad", GST_ELEMENT_NAME(ident));
      ident->src = gst_pad_new ("src", GST_PAD_SRC);
      gst_element_add_pad (GST_ELEMENT (ident), ident->src);
      gst_pad_set_connect_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_connect));
      gst_pad_set_getcaps_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
      gst_pad_set_event_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_handle_src_event));
      return ident->src;
    default:
      break;
  }
  
  GST_DEBUG(0, "element %s requested a new pad but none could be created", GST_ELEMENT_NAME(ident));
  return NULL;
}

/* this function has to
 * - start the autoplugger
 * - start type finding
 * ...
 */
static GstElementStateReturn
gst_spider_identity_change_state (GstElement *element)
{
  GstSpiderIdentity *ident;
  GstSpider *spider;
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  
  /* element check */
  ident = GST_SPIDER_IDENTITY (element);
  g_return_val_if_fail (ident != NULL, GST_STATE_FAILURE);
  g_return_val_if_fail (GST_IS_SPIDER_IDENTITY (ident), GST_STATE_FAILURE);
  
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_PLAYING:
      /* autoplugger check */
      spider = GST_SPIDER (GST_ELEMENT_PARENT (ident));
      g_return_val_if_fail (spider != NULL, GST_STATE_FAILURE);
      g_return_val_if_fail (GST_IS_SPIDER (spider), GST_STATE_FAILURE);
  
      /* start typefinding or plugging */
      if ((GST_RPAD_PEER (ident->sink) != NULL) && (GST_RPAD_PEER (ident->src) == NULL))
      {
        if (gst_pad_get_caps ((GstPad *) GST_PAD_PEER (ident->sink)) == NULL)
        {
          gst_spider_identity_start_type_finding (ident);
          break;
        } else {
          gst_spider_identity_plug (ident);
        }
      }
      /* autoplug on src */
      if ((GST_RPAD_PEER (ident->src) != NULL) && (GST_RPAD_PEER (ident->sink) == NULL))
      {
        gst_spider_identity_plug (ident);
      }
    default:
      break;
  }
  
  if ((ret != GST_STATE_FAILURE) && (GST_ELEMENT_CLASS (parent_class)->change_state))
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return ret;
}

static void
gst_spider_identity_start_type_finding (GstSpiderIdentity *ident)
{
  GstElement* typefind;
  gboolean restart = FALSE;
  
  GST_DEBUG (GST_CAT_AUTOPLUG, "element %s starts typefinding", GST_ELEMENT_NAME(ident));
  if (GST_STATE (GST_ELEMENT_PARENT (ident)) == GST_STATE_PLAYING)
  {
    gst_element_set_state (GST_ELEMENT (GST_ELEMENT_PARENT (ident)), GST_STATE_PAUSED);
    restart = TRUE;
  }
  
  /* create and connect typefind object */
  typefind = gst_element_factory_make ("typefind", g_strdup_printf("%s%s", "typefind", GST_ELEMENT_NAME(ident)));
  g_signal_connect (G_OBJECT (typefind), "have_type",
                    G_CALLBACK (callback_type_find_have_type), ident);
  gst_bin_add (GST_BIN (GST_ELEMENT_PARENT (ident)), typefind);
  gst_pad_connect (gst_element_get_compatible_pad ((GstElement *) ident, gst_element_get_pad (typefind, "sink")), gst_element_get_pad (typefind, "sink"));
  
  gst_element_set_loop_function (GST_ELEMENT (ident), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_sink_loop_type_finding));

  if (restart)
  {
    gst_element_set_state (GST_ELEMENT (GST_ELEMENT_PARENT (ident)), GST_STATE_PLAYING);
  }
}
/* waiting for a good suggestion on where to set the caps from typefinding
 * Caps must be cleared when pad is disconnected
 * 
 * Currently we are naive and set the caps on the source of the identity object 
 * directly and hope to avoid any disturbance in the force.
 */
void
gst_spider_identity_set_caps (GstSpiderIdentity *ident, GstCaps *caps)
{
  if (ident->src)
  {
    gst_pad_try_set_caps (ident->src, caps);
  }
}


static void
callback_type_find_have_type (GstElement *typefind, GstCaps *caps, GstSpiderIdentity *ident)
{
  gboolean restart_spider = FALSE;
  
  GST_INFO (GST_CAT_AUTOPLUG, "element %s has found caps\n", GST_ELEMENT_NAME(ident));

  /* checks */  
  g_assert (GST_IS_ELEMENT (typefind));
  g_assert (GST_IS_SPIDER_IDENTITY (ident));
  
  /* pause the autoplugger */
  if (gst_element_get_state (GST_ELEMENT (GST_ELEMENT_PARENT(ident))) == GST_STATE_PLAYING)
  {
    gst_element_set_state (GST_ELEMENT (GST_ELEMENT_PARENT(ident)), GST_STATE_PAUSED);
    restart_spider = TRUE;
  }

  /* remove typefind */
  gst_pad_disconnect (ident->src, (GstPad*) GST_RPAD_PEER (ident->src));
  gst_bin_remove (GST_BIN (GST_ELEMENT_PARENT (ident)), typefind);
  
  /* set caps */
  gst_spider_identity_set_caps (ident, caps);
  
  /* set new loop function, we gotta empty the cache */
  gst_element_set_loop_function (GST_ELEMENT (ident), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_sink_loop_emptycache));
  
  /* autoplug this pad */
  gst_spider_identity_plug (ident);  
  
  /* restart autoplugger */
  if (restart_spider)
    gst_element_set_state (GST_ELEMENT (GST_ELEMENT_PARENT(ident)), GST_STATE_PLAYING);
  
}
/* since we can't set the loop function to NULL if there's a cothread for us,
 * we have to use a dumb one
 */
static void
gst_spider_identity_dumb_loop  (GstSpiderIdentity *ident)
{
  GstBuffer *buf;

  g_return_if_fail (ident != NULL);
  g_return_if_fail (GST_IS_SPIDER_IDENTITY (ident));
  g_assert (ident->sink != NULL);

  buf = gst_pad_pull (ident->sink);

  gst_spider_identity_chain (ident->sink, buf);
}
/* do nothing until we're connected - then disable yourself
 */
static void
gst_spider_identity_src_loop (GstSpiderIdentity *ident)
{
  /* checks - disable for speed */
  g_return_if_fail (ident != NULL);
  g_return_if_fail (GST_IS_SPIDER_IDENTITY (ident));
  
  /* we don't want a loop function if we're plugged */
  if (ident->sink && GST_PAD_PEER (ident->sink))
  {
    gst_element_set_loop_function (GST_ELEMENT (ident), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_dumb_loop));
    gst_spider_identity_dumb_loop (ident);
    return;
  }
  
  /* in any case, we don't want to do anything:
   * - if we're not plugged, we don't have buffers
   * - if we're plugged, we wanna be chained please
   */
  gst_element_interrupt (GST_ELEMENT (ident));
  return;  
}
/* This loop function is only needed when typefinding.
 * It works quite simple: get a new buffer, append it to the cache
 * and push it to the typefinder.
 */
static void
gst_spider_identity_sink_loop_type_finding (GstSpiderIdentity *ident)
{
  GstBuffer *buf;
  
  /* checks - disable for speed */
  g_return_if_fail (ident != NULL);
  g_return_if_fail (GST_IS_SPIDER_IDENTITY (ident));
  g_assert (ident->sink != NULL);
  
  /* get buffer */
  buf = gst_pad_pull (ident->sink);
  
  /* if it's an event... */
  while (GST_IS_EVENT (buf)) {
    /* handle DISCONT events, please */
    gst_pad_event_default (ident->sink, GST_EVENT (buf));
    buf = gst_pad_pull (ident->sink);
  } 

  /* add it to the end of the cache */
  gst_buffer_ref (buf);
  GST_DEBUG (0, "element %s adds buffer %p (size %d) to cache", GST_ELEMENT_NAME(ident),  buf, GST_BUFFER_SIZE (buf));
  ident->cache_end = g_list_prepend (ident->cache_end, buf);
  if (ident->cache_start == NULL)
    ident->cache_start = ident->cache_end;
  
  /* push the buffer */
  gst_spider_identity_chain (ident->sink, buf);
}
/* this function is needed after typefinding:
 * empty the cache and when the cache is empty - remove this function
 */
static void
gst_spider_identity_sink_loop_emptycache (GstSpiderIdentity *ident)
{
  GstBuffer *buf;
  
  /* get the buffer and push it */
  buf = GST_BUFFER (ident->cache_start->data);
  gst_spider_identity_chain (ident->sink, buf);
  
  ident->cache_start = g_list_previous (ident->cache_start);

  /* now check if we have more buffers to push */
  if (ident->cache_start == NULL)
  {
    GST_DEBUG(0, "cache from %s is empty, changing loop function", GST_ELEMENT_NAME(ident));
    /* free cache */
    g_list_free (ident->cache_end);
    ident->cache_end = NULL;
    
    /* remove loop function */
    gst_element_set_loop_function (GST_ELEMENT (ident), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_dumb_loop));
    gst_element_interrupt (GST_ELEMENT (ident));
  }  
}

static gboolean
gst_spider_identity_handle_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstSpiderIdentity *ident;

  GST_DEBUG (0, "spider_identity src_event\n");

  ident = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
    case GST_EVENT_SEEK:
      /* see if there's something cached */
      if (ident->cache_start && ident->cache_start->data) {
        GST_DEBUG (0, "spider_identity seek in cache\n");
	/* FIXME we need to find the right position in the cache, make sure we 
	 * push from that offset and send out a discont event on the 
	 * next buffer */
	return TRUE;
      }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}


