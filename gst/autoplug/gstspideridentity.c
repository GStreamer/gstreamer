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
static void                   gst_spider_identity_class_init            (GstSpiderIdentityClass *klass);
static void                   gst_spider_identity_init                  (GstSpiderIdentity *spider_identity);

/* functions set in pads, elements and stuff */
static void                   gst_spider_identity_chain                  (GstPad *pad, GstBuffer *buf);
static GstElementStateReturn  gst_spider_identity_change_state          (GstElement *element);
static GstPadConnectReturn    gst_spider_identity_connect                (GstPad *pad, GstCaps *caps);
static GstCaps*                gst_spider_identity_getcaps                (GstPad *pad, GstCaps *caps);
/* loop functions */
static void                    gst_spider_identity_dumb_loop              (GstSpiderIdentity *ident);
static void                    gst_spider_identity_src_loop              (GstSpiderIdentity *ident);
static void                    gst_spider_identity_sink_loop_typefinding  (GstSpiderIdentity *ident);
static void                    gst_spider_identity_sink_loop_emptycache  (GstSpiderIdentity *ident);

/* set/get functions */
gboolean                      gst_spider_identity_is_plugged            (GstSpiderIdentity *identity);
void                          gst_spider_identity_set_caps              (GstSpiderIdentity *identity, GstCaps *caps);

/* callback */
static void                    callback_typefind_have_type                (GstElement *typefind, GstCaps *caps, GstSpiderIdentity *identity);

/* other functions */
static void                    gst_spider_identity_start_typefinding      (GstSpiderIdentity *ident);

static                        GstElementClass                           *parent_class = NULL;
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
  gst_element_class_add_padtemplate (gstelement_class, GST_PADTEMPLATE_GET (spider_src_factory));
  gst_element_class_add_padtemplate (gstelement_class, GST_PADTEMPLATE_GET (spider_sink_factory));
  
  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_spider_identity_change_state);
  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_spider_identity_request_new_pad);
}

static GstBufferPool*
gst_spider_identity_get_bufferpool (GstPad *pad)
{
  /* fix me */
  GstSpiderIdentity *spider_identity;

  spider_identity = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_get_bufferpool (spider_identity->src);
}

static void 
gst_spider_identity_init (GstSpiderIdentity *spider_identity) 
{
  /* pads */
  spider_identity->sink = NULL;
  spider_identity->src = NULL;

  /* variables */
  spider_identity->plugged = FALSE;
  
  /* caching */
  spider_identity->cache_start = NULL;
  spider_identity->cache_end = NULL;
  
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
  g_return_val_if_fail (GST_IS_PADTEMPLATE (templ), NULL);
  ident = GST_SPIDER_IDENTITY (element);
  g_return_val_if_fail (ident != NULL, NULL);
  g_return_val_if_fail (GST_IS_SPIDER_IDENTITY (ident), NULL);
  
  switch (GST_PADTEMPLATE_DIRECTION (templ))
  {
    case GST_PAD_SINK:
      if (ident->sink != NULL) break;
      /* sink */
      GST_DEBUG(0, "element %s requests new sink pad\n", GST_ELEMENT_NAME(ident));
      ident->sink = gst_pad_new ("sink", GST_PAD_SINK);
      gst_element_add_pad (GST_ELEMENT (ident), ident->sink);
      gst_pad_set_connect_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_connect));
      gst_pad_set_getcaps_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
      return ident->sink;
    case GST_PAD_SRC:
      /* src */
      if (ident->src != NULL) break;
      GST_DEBUG(0, "element %s requests new src pad\n", GST_ELEMENT_NAME(ident));
      ident->src = gst_pad_new ("src", GST_PAD_SRC);
      gst_element_add_pad (GST_ELEMENT (ident), ident->src);
      gst_pad_set_connect_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_connect));
      gst_pad_set_getcaps_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
      return ident->src;
    default:
      break;
  }
  
  GST_DEBUG(0, "element %s requested a new pad but none could be created\n", GST_ELEMENT_NAME(ident));
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
  g_return_val_if_fail (ident != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_SPIDER_IDENTITY (ident), GST_PAD_CONNECT_REFUSED);
  
  /* autoplugger check */
  spider = GST_SPIDER (GST_ELEMENT_PARENT (ident));
  g_return_val_if_fail (spider != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_SPIDER (spider), GST_PAD_CONNECT_REFUSED);
  
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_PLAYING:
      /* start typefinding or plugging */
      if ((ident->sink != NULL) && (ident->src == NULL))
      {
        if (gst_pad_get_caps ((GstPad *) GST_PAD_PEER (ident->sink)) == NULL)
        {
          gst_spider_identity_start_typefinding (ident);
          break;
        } else {
          gst_spider_plug (ident);
        }
      }
      /* autoplug on src */
      if ((ident->src != NULL) && (ident->sink == NULL))
      {
        gst_spider_plug (ident);
      }
    default:
      break;
  }
  
  if ((ret != GST_STATE_FAILURE) && (GST_ELEMENT_CLASS (parent_class)->change_state))
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return ret;
}

static void
gst_spider_identity_start_typefinding (GstSpiderIdentity *ident)
{
  GstElement* typefind;
  
  GST_DEBUG (GST_CAT_AUTOPLUG, "element %s starts typefinding", GST_ELEMENT_NAME(ident));
  
  /* create and connect typefind object */
  typefind = gst_elementfactory_make ("typefind", g_strdup_printf("%s%s", "typefind", GST_ELEMENT_NAME(ident)));
  g_signal_connect (G_OBJECT (typefind), "have_type",
                    G_CALLBACK (callback_typefind_have_type), ident);
  gst_bin_add (GST_BIN (GST_ELEMENT_PARENT (ident)), typefind);
  gst_pad_connect (gst_element_get_compatible_pad ((GstElement *) ident, gst_element_get_pad (typefind, "sink")), gst_element_get_pad (typefind, "sink"));
  
  gst_element_set_loop_function (GST_ELEMENT (ident), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_sink_loop_typefinding));
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
callback_typefind_have_type (GstElement *typefind, GstCaps *caps, GstSpiderIdentity *ident)
{
  gboolean restart_spider = FALSE;
  
  GST_INFO (GST_CAT_AUTOPLUG, "element %s has found caps", GST_ELEMENT_NAME(ident));
  /* checks */
  
  /* we have to ref the typefind, because if me remove it the scheduler segfaults 
   * FIXME: get rid of the typefinder
   */
  gst_object_ref (GST_OBJECT (typefind));
  
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
  gst_spider_plug (ident);  
  
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
gst_spider_identity_sink_loop_typefinding (GstSpiderIdentity *ident)
{
  GstBuffer *buf;
  
  /* checks - disable for speed */
  g_return_if_fail (ident != NULL);
  g_return_if_fail (GST_IS_SPIDER_IDENTITY (ident));
  g_assert (ident->sink != NULL);
  
  /* get buffer */
  buf = gst_pad_pull (ident->sink);
  
  /* if it's an event... */
  if (GST_IS_EVENT (buf)) {
    /* handle DISCONT events, please */
    gst_pad_event_default (ident->sink, GST_EVENT (buf));
  } 

  /* add it to the end of the cache */
  gst_buffer_ref (buf);
  GST_DEBUG (0, "element %s adds buffer %p (size %d) to cache\n", GST_ELEMENT_NAME(ident),  buf, GST_BUFFER_SIZE (buf));
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
    GST_DEBUG(0, "cache from %s is empty, changing loop function\n", GST_ELEMENT_NAME(ident));
    /* free cache */
    g_list_free (ident->cache_end);
    ident->cache_end = NULL;
    
    /* remove loop function */
    gst_element_set_loop_function (GST_ELEMENT (ident), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_dumb_loop));
    gst_element_interrupt (GST_ELEMENT (ident));
  }  
}



