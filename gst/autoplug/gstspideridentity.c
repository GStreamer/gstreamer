/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wtay@chello.be>
 *
 * gstspideridentity.c: identity element for the spider autoplugger
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstspideridentity.h"
#include "gstspider.h"

GST_DEBUG_CATEGORY_STATIC (gst_spider_identity_debug);
#define GST_CAT_DEFAULT gst_spider_identity_debug

static GstElementDetails gst_spider_identity_details = GST_ELEMENT_DETAILS (
  "SpiderIdentity",
  "Generic",
  "Link between spider and outside elements",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>"
);


/* generic templates 
 * delete me when meging with spider.c
 */
static GstStaticPadTemplate spider_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate spider_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY
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
static GstPadLinkReturn		gst_spider_identity_link		(GstPad *pad, const GstCaps *caps);
static GstCaps *		gst_spider_identity_getcaps		(GstPad *pad);
/* loop functions */
static void			gst_spider_identity_dumb_loop		(GstSpiderIdentity *ident);
static void                     gst_spider_identity_src_loop		(GstSpiderIdentity *ident);
static void                     gst_spider_identity_sink_loop_type_finding (GstSpiderIdentity *ident);

static gboolean 		gst_spider_identity_handle_src_event 	(GstPad *pad, GstEvent *event);

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
    spider_identity_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSpiderIdentity", 
						   &spider_identity_info, 0);
    GST_DEBUG_CATEGORY_INIT (gst_spider_identity_debug, "spideridentity", 
			     0, "spider autoplugging proxy element");
  }
  return spider_identity_type;
}

static void 
gst_spider_identity_class_init (GstSpiderIdentityClass *klass) 
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  /* add our two pad templates */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&spider_src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&spider_sink_factory));
  gst_element_class_set_details (gstelement_class, &gst_spider_identity_details);
  
  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_spider_identity_change_state);
  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_spider_identity_request_new_pad);
}

static void 
gst_spider_identity_init (GstSpiderIdentity *ident) 
{
  /* sink */
  ident->sink = gst_pad_new_from_template (
      gst_static_pad_template_get (&spider_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (ident), ident->sink);
  gst_pad_set_link_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_link));
  gst_pad_set_getcaps_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
  /* src */
  ident->src = gst_pad_new_from_template (
      gst_static_pad_template_get (&spider_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (ident), ident->src);
  gst_pad_set_link_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_link));
  gst_pad_set_getcaps_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
  gst_pad_set_event_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_handle_src_event));

  /* variables */
  ident->plugged = FALSE;
}

static void 
gst_spider_identity_chain (GstPad *pad, GstBuffer *buf) 
{
  GstSpiderIdentity *ident;
  
  /*g_print ("chaining on pad %s:%s with buffer %p\n", GST_DEBUG_PAD_NAME (pad), buf);*/

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  if (buf == NULL) return;

  ident = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    /* start hack for current event stuff here */
    /* check for unlinked elements and send them the EOS event, too */
    if (GST_EVENT_TYPE (GST_EVENT (buf)) == GST_EVENT_EOS)
    {
      GstSpider *spider = (GstSpider *) GST_OBJECT_PARENT (ident);
      GList *list = spider->links;
      while (list)
      {
	GstSpiderConnection *conn = (GstSpiderConnection *) list->data;
	list = g_list_next (list);
        if (conn->current != (GstElement *) conn->src) {
          GST_DEBUG ("sending EOS to unconnected element %s from %s", 
	       GST_ELEMENT_NAME (conn->src), GST_ELEMENT_NAME (ident));
          gst_pad_push (conn->src->src, GST_DATA (GST_BUFFER (gst_event_new (GST_EVENT_EOS))));  
          gst_element_set_eos (GST_ELEMENT (conn->src));
	}
      }
    }
    /* end hack for current event stuff here */

    gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  if ((ident->src != NULL) && (GST_PAD_PEER (ident->src) != NULL)) {
    /* g_print("pushing buffer %p (refcount %d - buffersize %d) to pad %s:%s\n", buf, GST_BUFFER_REFCOUNT (buf), GST_BUFFER_SIZE (buf), GST_DEBUG_PAD_NAME (ident->src)); */
    GST_LOG ( "push %p %" G_GINT64_FORMAT, buf, GST_BUFFER_OFFSET (buf));
    gst_pad_push (ident->src, GST_DATA (buf));
  } else if (GST_IS_BUFFER (buf)) {
    gst_buffer_unref (buf);
  }
}
GstSpiderIdentity*           
gst_spider_identity_new_src (gchar *name)
{
  GstSpiderIdentity *ret = (GstSpiderIdentity *) gst_element_factory_make ("spideridentity", name);
  /* set the right functions */
  gst_element_set_loop_function (GST_ELEMENT (ret), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_src_loop));
  
  return ret;
}
GstSpiderIdentity*           
gst_spider_identity_new_sink (gchar *name)
{
  GstSpiderIdentity *ret = (GstSpiderIdentity *) gst_element_factory_make ("spideridentity", name);

  /* set the right functions */
  gst_element_set_loop_function (GST_ELEMENT (ret), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_dumb_loop));

  return ret;
}

/* shamelessly stolen from gstqueue.c to get proxy links */
static GstPadLinkReturn
gst_spider_identity_link (GstPad *pad, const GstCaps *caps)
{
  GstSpiderIdentity *spider_identity = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));
  GstPad *otherpad;

  if (pad == spider_identity->src) 
    otherpad = spider_identity->sink;
  else
    otherpad = spider_identity->src;

  if (otherpad != NULL)
    return gst_pad_proxy_link (otherpad, caps);
  
  return GST_PAD_LINK_OK;
}

static GstCaps*
gst_spider_identity_getcaps (GstPad *pad)
{
  GstSpiderIdentity *spider_identity = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstPad *peer;

  if (pad == spider_identity->src) 
    otherpad = spider_identity->sink;
  else
    otherpad = spider_identity->src;

  if (otherpad != NULL) {
    peer = GST_PAD_PEER (otherpad);

    if (peer)
      return gst_pad_get_caps (peer);
  }
  return gst_caps_new_any ();
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
      GST_DEBUG ( "element %s requests new sink pad", GST_ELEMENT_NAME(ident));
      ident->sink = gst_pad_new ("sink", GST_PAD_SINK);
      gst_element_add_pad (GST_ELEMENT (ident), ident->sink);
      gst_pad_set_link_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_link));
      gst_pad_set_getcaps_function (ident->sink, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
      return ident->sink;
    case GST_PAD_SRC:
      /* src */
      if (ident->src != NULL) break;
      GST_DEBUG ( "element %s requests new src pad", GST_ELEMENT_NAME(ident));
      ident->src = gst_pad_new ("src", GST_PAD_SRC);
      gst_element_add_pad (GST_ELEMENT (ident), ident->src);
      gst_pad_set_link_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_link));
      gst_pad_set_getcaps_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_getcaps));
      gst_pad_set_event_function (ident->src, GST_DEBUG_FUNCPTR (gst_spider_identity_handle_src_event));
      return ident->src;
    default:
      break;
  }
  
  GST_DEBUG ( "element %s requested a new pad but none could be created", GST_ELEMENT_NAME(ident));
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
/*  GstElement* typefind;
  gchar *name;*/
  gboolean restart = FALSE;
  
  GST_DEBUG ("element %s starts typefinding", GST_ELEMENT_NAME(ident));
  if (GST_STATE (GST_ELEMENT_PARENT (ident)) == GST_STATE_PLAYING)
  {
    gst_element_set_state (GST_ELEMENT (GST_ELEMENT_PARENT (ident)), GST_STATE_PAUSED);
    restart = TRUE;
  }

  gst_element_set_loop_function (GST_ELEMENT (ident), (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_sink_loop_type_finding));

  if (restart)
  {
    gst_element_set_state (GST_ELEMENT (GST_ELEMENT_PARENT (ident)), GST_STATE_PLAYING);
  }
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

  buf = GST_BUFFER (gst_pad_pull (ident->sink));

  gst_spider_identity_chain (ident->sink, buf);
}
/* do nothing until we're linked - then disable yourself
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
  gst_element_interrupt (GST_ELEMENT (ident));
}
/* This loop function is only needed when typefinding.
 */
typedef struct {
  GstBuffer *buffer;
  guint best_probability;
  GstCaps *caps;
} SpiderTypeFind;
guint8 *
spider_find_peek (gpointer data, gint64 offset, guint size)
{
  SpiderTypeFind *find = (SpiderTypeFind *) data;
  gint64 buffer_offset = GST_BUFFER_OFFSET_IS_VALID (find->buffer) ? 
			 GST_BUFFER_OFFSET (find->buffer) : 0;
  
  if (offset >= buffer_offset && offset + size <= buffer_offset + GST_BUFFER_SIZE (find->buffer)) {
    GST_LOG ("peek %"G_GINT64_FORMAT", %u successful", offset, size);
    return GST_BUFFER_DATA (find->buffer) + offset - buffer_offset;
  } else {
    GST_LOG ("peek %"G_GINT64_FORMAT", %u failed", offset, size);
    return NULL;
  }
}
static void
spider_find_suggest (gpointer data, guint probability, const GstCaps *caps)
{
  SpiderTypeFind *find = (SpiderTypeFind *) data;
  G_GNUC_UNUSED gchar *caps_str;

  caps_str = gst_caps_to_string (caps);
  GST_INFO ("suggest %u, %s", probability, caps_str);
  g_free (caps_str);
  if (probability > find->best_probability) {
    gst_caps_replace (&find->caps, gst_caps_copy (caps));
    find->best_probability = probability;
  }
}
static void
gst_spider_identity_sink_loop_type_finding (GstSpiderIdentity *ident)
{
  GstData *data;
  GstTypeFind gst_find;
  SpiderTypeFind find;
  GList *walk, *type_list = NULL;

  g_return_if_fail (GST_IS_SPIDER_IDENTITY (ident));

  data = gst_pad_pull (ident->sink);
  while (!GST_IS_BUFFER (data)) {
    gst_spider_identity_chain (ident->sink, GST_BUFFER (data));
    data = gst_pad_pull (ident->sink);
  }
  
  find.buffer = GST_BUFFER (data);
  /* maybe there are already valid caps now? */
  find.caps = gst_pad_get_caps (ident->sink);
  if (find.caps != NULL) {
    goto plug;
  }
  
  /* now do the actual typefinding with the supplied buffer */
  walk = type_list = gst_type_find_factory_get_list ();
    
  find.best_probability = 0;
  find.caps = NULL;
  gst_find.data = &find;
  gst_find.peek = spider_find_peek;
  gst_find.suggest = spider_find_suggest;
  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    GST_DEBUG ("trying typefind function %s", GST_PLUGIN_FEATURE_NAME (factory));
    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      goto plug;
    walk = g_list_next (walk);
  }
  if (find.best_probability > 0)
    goto plug;
  gst_element_error(GST_ELEMENT(ident), "Could not find media type", NULL);
  find.buffer = GST_BUFFER (gst_event_new (GST_EVENT_EOS));

end:
  /* remove loop function */
  gst_element_set_loop_function (GST_ELEMENT (ident), 
                                (GstElementLoopFunction) GST_DEBUG_FUNCPTR (gst_spider_identity_dumb_loop));
  
  /* push the buffer */
  gst_spider_identity_chain (ident->sink, find.buffer);
  
  return;

plug:
  GST_INFO ("typefind function found caps"); 
  g_assert (gst_pad_try_set_caps (ident->src, find.caps) > 0);
  gst_caps_debug (find.caps, "spider starting caps");
  gst_caps_free (find.caps);
  if (type_list)
    g_list_free (type_list);

  gst_spider_identity_plug (ident);

  goto end;
}

static gboolean
gst_spider_identity_handle_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstSpiderIdentity *ident;

  GST_DEBUG ( "spider_identity src_event");

  ident = GST_SPIDER_IDENTITY (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
    case GST_EVENT_SEEK:
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}
