/* GStreamer
 * Copyright (C) 2001 RidgeRun, Inc. (www.ridgerun.com)
 *
 * gstautoplugger.c: Data  for the dynamic autopluggerger
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

#include <gst/gst.h>

GstElementDetails gst_autoplugger_details = {
  "Dynamic autoplugger",
  "Autoplugger",
  "Magic element that converts from any type to any other",
  VERSION,
  "Erik Walthinsen <omega@temple-baptist.com>",
  "(C) 2001 RidgeRun, Inc. (www.ridgerun.com)",
};

#define GST_TYPE_AUTOPLUGGER \
  (gst_autoplugger_get_type())
#define GST_AUTOPLUGGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUTOPLUGGER,GstAutoplugger))
#define GST_AUTOPLUGGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUGGER,GstAutopluggerClass))
#define GST_IS_AUTOPLUGGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUTOPLUGGER))
#define GST_IS_AUTOPLUGGER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUGGER))

typedef struct _GstAutoplugger GstAutoplugger;
typedef struct _GstAutopluggerClass GstAutopluggerClass;

struct _GstAutoplugger {
  GstBin bin;
  gint paused;

  GstElement *cache;
  gboolean cache_first_buffer;
  GstPad *cache_sinkpad, *cache_srcpad;

  GstElement *typefind;
  GstPad *typefind_sinkpad;

  GstPad *sinkpadpeer, *srcpadpeer;
  GstCaps *sinkcaps, *srccaps;

  GstCaps *sinktemplatecaps;

  GstAutoplug *autoplug;
  GstElement *autobin;

  gboolean disable_nocaps;
};

struct _GstAutopluggerClass {
  GstBinClass parent_class;
};


/*  signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
};


static void			gst_autoplugger_class_init	(GstAutopluggerClass *klass);
static void			gst_autoplugger_init		(GstAutoplugger *queue);

static void			gst_autoplugger_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_autoplugger_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/*static GstElementStateReturn	gst_autoplugger_change_state	(GstElement *element);*/


static void	gst_autoplugger_external_sink_caps_changed	(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_src_caps_changed	(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_sink_caps_nego_failed	(GstPad *pad, gboolean *result, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_src_caps_nego_failed	(GstPad *pad, gboolean *result, GstAutoplugger *autoplugger);
/* defined but not used
static void	gst_autoplugger_external_sink_connected		(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_src_connected		(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger);
*/
static void	gst_autoplugger_cache_first_buffer		(GstElement *element,GstBuffer *buf,GstAutoplugger *autoplugger);
static void	gst_autoplugger_cache_empty			(GstElement *element, GstAutoplugger *autoplugger);
static void	gst_autoplugger_type_find_have_type		(GstElement *element, GstCaps *caps, GstAutoplugger *autoplugger);

static GstElementClass *parent_class = NULL;
/*static guint gst_autoplugger_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_autoplugger_get_type(void) {
  static GType autoplugger_type = 0;

  if (!autoplugger_type) {
    static const GTypeInfo autoplugger_info = {
      sizeof(GstAutopluggerClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_autoplugger_class_init,
      NULL,
      NULL,
      sizeof(GstAutoplugger),
      0,
      (GInstanceInitFunc)gst_autoplugger_init,
    };
    autoplugger_type = g_type_register_static (GST_TYPE_BIN, "GstAutoplugger", &autoplugger_info, 0);
  }
  return autoplugger_type;
}

static void
gst_autoplugger_class_init (GstAutopluggerClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

/*
  gst_autoplugger_signals[_EMPTY] =
    g_signal_new ("_empty", G_OBJECT_TYPE(gobject_class), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstAutopluggerClass, _empty), NULL, NULL,
                    g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
*/

/*
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFER_COUNT,
    g_param_spec_int("buffer_count","buffer_count","buffer_count",
                      0,G_MAXINT,0,G_PARAM_READABLE)); * CHECKME! *
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RESET,
    g_param_spec_boolean("reset","reset","reset",
                         FALSE,G_PARAM_WRITABLE)); * CHECKME! *
*/

  gobject_class->set_property = gst_autoplugger_set_property;
  gobject_class->get_property = gst_autoplugger_get_property;

/*  gstelement_class->change_state = gst_autoplugger_change_state; */
}

static void
gst_autoplugger_init (GstAutoplugger *autoplugger)
{
  /* create the autoplugger cache, which is the fundamental unit of the autopluggerger */
  /* FIXME we need to find a way to set element's name before _init */
  /* FIXME ... so we can name the subelements uniquely 		    */
  autoplugger->cache = gst_element_factory_make("autoplugcache", "unnamed_autoplugcache");
  g_return_if_fail (autoplugger->cache != NULL);

  GST_DEBUG(GST_CAT_AUTOPLUG, "turning on caps nego proxying in cache");
  g_object_set(G_OBJECT(autoplugger->cache),"caps_proxy",TRUE,NULL);

  /* attach signals to the cache */
  g_signal_connect (G_OBJECT (autoplugger->cache), "first_buffer",
                     G_CALLBACK (gst_autoplugger_cache_first_buffer), autoplugger);

  /* add the cache to self */
  gst_bin_add (GST_BIN(autoplugger), autoplugger->cache);

  /* get the cache's pads so we can attach stuff to them */
  autoplugger->cache_sinkpad = gst_element_get_pad (autoplugger->cache, "sink");
  autoplugger->cache_srcpad = gst_element_get_pad (autoplugger->cache, "src");

  /* attach handlers to the typefind pads */
  g_signal_connect (G_OBJECT (autoplugger->cache_sinkpad), "caps_changed",
                     G_CALLBACK (gst_autoplugger_external_sink_caps_changed), autoplugger);
  g_signal_connect (G_OBJECT (autoplugger->cache_srcpad), "caps_changed",
                     G_CALLBACK (gst_autoplugger_external_src_caps_changed), autoplugger);
  g_signal_connect (G_OBJECT (autoplugger->cache_sinkpad), "caps_nego_failed",
                     G_CALLBACK (gst_autoplugger_external_sink_caps_nego_failed), autoplugger);
  g_signal_connect (G_OBJECT (autoplugger->cache_srcpad), "caps_nego_failed",
                     G_CALLBACK (gst_autoplugger_external_src_caps_nego_failed), autoplugger);
/*  g_signal_connect (G_OBJECT (autoplugger->cache_sinkpad), "connected",    */
/*                     gst_autoplugger_external_sink_connected, autoplugger);*/
/*  g_signal_connect (G_OBJECT (autoplugger->cache_srcpad), "connected",     */	
/*                     gst_autoplugger_external_src_connected, autoplugger); */

  /* ghost both of these pads to the outside world */
  gst_element_add_ghost_pad (GST_ELEMENT(autoplugger), autoplugger->cache_sinkpad, "sink");
  gst_element_add_ghost_pad (GST_ELEMENT(autoplugger), autoplugger->cache_srcpad, "src");
}

/* defined but not used
G_GNUC_UNUSED static void
gst_autoplugger_external_sink_connected(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger)
{
  GstPadTemplate *peertemplate;
  GstCaps *peercaps, *peertemplatecaps;

  GST_INFO(GST_CAT_AUTOPLUG, "have cache:sink connected");*/
/*  autoplugger->sinkpadpeer = peerpad; */
/*
  if (autoplugger->sinkpadpeer) {
    peercaps = GST_PAD_CAPS(autoplugger->sinkpadpeer);
    if (peercaps)
      GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer: %s",
               gst_caps_get_mime(peercaps));
    peertemplate = GST_PAD_PAD_TEMPLATE(autoplugger->sinkpadpeer);
    if (peertemplate) {
      peertemplatecaps = GST_PAD_TEMPLATE_CAPS(peertemplate);
      if (peertemplatecaps) {
        GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer's padtemplate %s",
                 gst_caps_get_mime(peertemplatecaps));
      }
    }
  }
}

G_GNUC_UNUSED static void
gst_autoplugger_external_src_connected(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger)
{
  GstPadTemplate *peertemplate;
  GstCaps *peercaps, *peertemplatecaps;

  GST_INFO(GST_CAT_AUTOPLUG, "have cache:src connected");*/
/*  autoplugger->srcpadpeer = peerpad; */
/*
  if (autoplugger->srcpadpeer) {
    peercaps = GST_PAD_CAPS(autoplugger->srcpadpeer);
    if (peercaps)
      GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer: %s",
               gst_caps_get_mime(peercaps));
    peertemplate = GST_PAD_PAD_TEMPLATE(autoplugger->srcpadpeer);
    if (peertemplate) {
      peertemplatecaps = GST_PAD_TEMPLATE_CAPS(peertemplate);
      if (peertemplatecaps) {
        GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer's padtemplate %s",
                 gst_caps_get_mime(peertemplatecaps));
        autoplugger->sinktemplatecaps = peertemplatecaps;*/
/*        GST_DEBUG(GST_CAT_AUTOPLUG, "turning on caps nego proxying in cache"); */
/*        gtk_object_set(G_OBJECT(autoplugger->cache),"caps_proxy",TRUE,NULL);*/
/*      }
    }
  }
}*/


static void
gst_autoplugger_external_sink_caps_changed(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger)
{
  GST_INFO(GST_CAT_AUTOPLUG, "have cache:sink caps of %s\n",gst_caps_get_mime(caps));
  autoplugger->sinkcaps = caps;
}

static void
gst_autoplugger_external_src_caps_changed(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger)
{
  GST_INFO(GST_CAT_AUTOPLUG, "have cache:src caps of %s\n",gst_caps_get_mime(caps));
  autoplugger->srccaps = caps;
}


static gboolean
gst_autoplugger_autoplug(GstAutoplugger *autoplugger,GstPad *srcpad,GstCaps *srccaps,GstCaps *sinkcaps)
{
  GstPad *sinkpad;

  sinkpad = GST_PAD(GST_PAD_PEER(srcpad));
  GST_DEBUG(GST_CAT_AUTOPLUG,"disconnecting %s:%s and %s:%s to autoplug between them",
            GST_DEBUG_PAD_NAME(srcpad),GST_DEBUG_PAD_NAME(sinkpad));
  GST_DEBUG(GST_CAT_AUTOPLUG,"srcpadcaps are of type %s",gst_caps_get_mime(srccaps));
  GST_DEBUG(GST_CAT_AUTOPLUG,"sinkpadcaps are of type %s",gst_caps_get_mime(sinkcaps));

  /* disconnect the pads */
  GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting the pads that will be joined by an autobin");
  gst_pad_disconnect(srcpad,sinkpad);

  if (!autoplugger->autoplug) {
    autoplugger->autoplug = gst_autoplug_factory_make("static");
    g_return_val_if_fail(autoplugger->autoplug != NULL, FALSE);
  }
  GST_DEBUG(GST_CAT_AUTOPLUG, "building autoplugged bin between caps");
  autoplugger->autobin = gst_autoplug_to_caps(autoplugger->autoplug,
    srccaps,sinkcaps,NULL);
  g_return_val_if_fail(autoplugger->autobin != NULL, FALSE);
  gst_bin_add(GST_BIN(autoplugger),autoplugger->autobin);

gst_scheduler_show(GST_ELEMENT_SCHED(autoplugger));

  /* FIXME this is a hack */
/*  GST_DEBUG(GST_CAT_AUTOPLUG, "copying failed caps to srcpad %s:%s to ensure renego",GST_DEBUG_PAD_NAME(autoplugger->cache_srcpad)); */
/*  gst_pad_set_caps(srcpad,srccaps); */

  if (GST_PAD_CAPS(srcpad) == NULL) GST_DEBUG(GST_CAT_AUTOPLUG,"no caps on cache:src!");

  /* attach the autoplugged bin */
  GST_DEBUG(GST_CAT_AUTOPLUG, "attaching the autoplugged bin between the two pads");
  gst_pad_connect(srcpad,gst_element_get_pad(autoplugger->autobin,"sink"));
gst_scheduler_show(GST_ELEMENT_SCHED(autoplugger));
  gst_pad_connect(gst_element_get_pad(autoplugger->autobin,"src_00"),sinkpad);
gst_scheduler_show(GST_ELEMENT_SCHED(autoplugger));

  /* FIXME try to force the renego */
/*  GST_DEBUG(GST_CAT_AUTOPLUG, "trying to force everyone to nego"); */
/*  gst_pad_renegotiate(gst_element_get_pad(autoplugger->autobin,"sink")); */
/*  gst_pad_renegotiate(sinkpad); */

  return TRUE;
}

static void
gst_autoplugger_external_sink_caps_nego_failed(GstPad *pad, gboolean *result, GstAutoplugger *autoplugger)
{
  GstPad *srcpad_peer;
  GstPadTemplate *srcpad_peer_template;
  GstCaps *srcpad_peer_caps;
  GstPad *sinkpad_peer;
  GstCaps *sinkpad_peer_caps;

  GST_INFO(GST_CAT_AUTOPLUG, "have caps nego failure on sinkpad %s:%s!!!",GST_DEBUG_PAD_NAME(pad));

  autoplugger->paused++;
  if (autoplugger->paused == 1)
    /* try to PAUSE the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

  srcpad_peer = GST_PAD(GST_PAD_PEER(autoplugger->cache_srcpad));
  g_return_if_fail(srcpad_peer != NULL);
  srcpad_peer_template = GST_PAD_PAD_TEMPLATE(srcpad_peer);
  g_return_if_fail(srcpad_peer_template != NULL);
  srcpad_peer_caps = GST_PAD_TEMPLATE_CAPS(srcpad_peer_template);
  g_return_if_fail(srcpad_peer_caps != NULL);

  sinkpad_peer = GST_PAD(GST_PAD_PEER(pad));
  g_return_if_fail(sinkpad_peer != NULL);
  sinkpad_peer_caps = GST_PAD_CAPS(sinkpad_peer);
  g_return_if_fail(sinkpad_peer_caps != NULL);

  if (gst_autoplugger_autoplug(autoplugger,autoplugger->cache_srcpad,sinkpad_peer_caps,srcpad_peer_caps))
    *result = TRUE;

  autoplugger->paused--;
  if (autoplugger->paused == 0)
    /* try to PLAY the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

  GST_INFO(GST_CAT_AUTOPLUG, "done dealing with caps nego failure on sinkpad %s:%s",GST_DEBUG_PAD_NAME(pad));
}

static void
gst_autoplugger_external_src_caps_nego_failed(GstPad *pad, gboolean *result, GstAutoplugger *autoplugger)
{
  GstCaps *srcpad_caps;
  GstPad *srcpad_peer;
  GstPadTemplate *srcpad_peer_template;
  GstCaps *srcpad_peer_caps;

  GST_INFO(GST_CAT_AUTOPLUG, "have caps nego failure on srcpad %s:%s!!!",GST_DEBUG_PAD_NAME(pad));

  autoplugger->paused++;
  if (autoplugger->paused == 1)
    /* try to PAUSE the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

  srcpad_caps = GST_PAD_CAPS(autoplugger->cache_srcpad);

  srcpad_peer = GST_PAD(GST_PAD_PEER(autoplugger->cache_srcpad));
  g_return_if_fail(srcpad_peer != NULL);
  srcpad_peer_template = GST_PAD_PAD_TEMPLATE(srcpad_peer);
  g_return_if_fail(srcpad_peer_template != NULL);
  srcpad_peer_caps = GST_PAD_TEMPLATE_CAPS(srcpad_peer_template);
  g_return_if_fail(srcpad_peer_caps != NULL);

  if (gst_autoplugger_autoplug(autoplugger,autoplugger->cache_srcpad,srcpad_caps,srcpad_peer_caps))
    *result = TRUE;

  autoplugger->paused--;
  if (autoplugger->paused == 0)
    /* try to PLAY the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

  autoplugger->disable_nocaps = TRUE;

  GST_INFO(GST_CAT_AUTOPLUG, "done dealing with caps nego failure on srcpad %s:%s",GST_DEBUG_PAD_NAME(pad));
}


static void
gst_autoplugger_cache_empty(GstElement *element, GstAutoplugger *autoplugger)
{
  GstPad *cache_sinkpad_peer,*cache_srcpad_peer;

  GST_INFO(GST_CAT_AUTOPLUG, "autoplugger cache has hit empty, we can now remove it");

  autoplugger->paused++;
  if (autoplugger->paused == 1)
    /* try to PAUSE the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

  /* disconnect the cache from its peers */
  GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting autoplugcache from its peers");
  cache_sinkpad_peer = GST_PAD (GST_PAD_PEER(autoplugger->cache_sinkpad));
  cache_srcpad_peer = GST_PAD (GST_PAD_PEER(autoplugger->cache_srcpad));
  gst_pad_disconnect(cache_sinkpad_peer,autoplugger->cache_sinkpad);
  gst_pad_disconnect(autoplugger->cache_srcpad,cache_srcpad_peer);

  /* remove the cache from self */
  GST_DEBUG(GST_CAT_AUTOPLUG, "removing the cache from the autoplugger");
  gst_bin_remove (GST_BIN(autoplugger), autoplugger->cache);

  /* connect the two pads */
  GST_DEBUG(GST_CAT_AUTOPLUG, "reconnecting the autoplugcache's former peers");
  gst_pad_connect(cache_sinkpad_peer,cache_srcpad_peer);

  autoplugger->paused--;
  if (autoplugger->paused == 0)
    /* try to PLAY the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

/*  xmlSaveFile("autoplugger.gst", gst_xml_write(GST_ELEMENT_SCHED(autoplugger)->parent)); */

  GST_INFO(GST_CAT_AUTOPLUG, "autoplugger_cache_empty finished");
}

static void
gst_autoplugger_type_find_have_type(GstElement *element, GstCaps *caps, GstAutoplugger *autoplugger) 
{
  GST_INFO(GST_CAT_AUTOPLUG, "typefind claims to have a type: %s",gst_caps_get_mime(caps));

gst_scheduler_show(GST_ELEMENT_SCHED(autoplugger));

  autoplugger->paused++;
  if (autoplugger->paused == 1)
    /* try to PAUSE the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

  /* first disconnect the typefind and shut it down */
  GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting typefind from the cache");
  gst_pad_disconnect(autoplugger->cache_srcpad,autoplugger->typefind_sinkpad);
  gst_bin_remove(GST_BIN(autoplugger),autoplugger->typefind);

  /* FIXME FIXME now we'd compare caps and see if we need to autoplug something in the middle, but for  */
  /* now we're going to just reconnect where we left off */
  /* FIXME FIXME FIXME!!!: this should really be done in the caps failure!!! */
/*
  if (!autoplugger->autoplug) {
    autoplugger->autoplug = gst_autoplug_factory_make("static");
  }
  autoplugger->autobin = gst_autoplug_to_caps(autoplugger->autoplug,
      caps,autoplugger->sinktemplatecaps,NULL);
  g_return_if_fail(autoplugger->autobin != NULL);
  gst_bin_add(GST_BIN(autoplugger),autoplugger->autobin);

*  // re-attach the srcpad's original peer to the cache *
*  GST_DEBUG(GST_CAT_AUTOPLUG, "reconnecting the cache to the downstream peer"); *
*  gst_pad_connect(autoplugger->cache_srcpad,autoplugger->srcpadpeer); *

  * attach the autoplugged bin *
  GST_DEBUG(GST_CAT_AUTOPLUG, "attaching the autoplugged bin between cache and downstream peer");
  gst_pad_connect(autoplugger->cache_srcpad,gst_element_get_pad(autoplugger->autobin,"sink"));
  gst_pad_connect(gst_element_get_pad(autoplugger->autobin,"src_00"),autoplugger->srcpadpeer);
*/

  /* FIXME set the caps on the new connection
   *  GST_DEBUG(GST_CAT_AUTOPLUG,"forcing caps on the typefound pad");
   * gst_pad_set_caps(autoplugger->cache_srcpad,caps);
   * reattach the original outside srcpad
   */ 
   GST_DEBUG(GST_CAT_AUTOPLUG,"re-attaching downstream peer to autoplugcache");
  gst_pad_connect(autoplugger->cache_srcpad,autoplugger->srcpadpeer);

  /* now reset the autoplugcache */
  GST_DEBUG(GST_CAT_AUTOPLUG, "resetting the cache to send first buffer(s) again");
  g_object_set(G_OBJECT(autoplugger->cache),"reset",TRUE,NULL);

  /* attach the cache_empty handler */
  /* FIXME this is the wrong place, it shouldn't be done until we get successful caps nego! */
  g_signal_connect (G_OBJECT(autoplugger->cache),"cache_empty",
                     G_CALLBACK (gst_autoplugger_cache_empty), autoplugger);

  autoplugger->paused--;
  if (autoplugger->paused == 0)
    /* try to PLAY the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

  GST_INFO(GST_CAT_AUTOPLUG, "typefind_have_type finished");
gst_scheduler_show(GST_ELEMENT_SCHED(autoplugger));
}

static void
gst_autoplugger_cache_first_buffer(GstElement *element,GstBuffer *buf,GstAutoplugger *autoplugger)
{
  GST_INFO(GST_CAT_AUTOPLUG, "have first buffer through cache");
  autoplugger->cache_first_buffer = TRUE;

  /* if there are no established caps, worry */
  if (!autoplugger->sinkcaps) {
    GST_INFO(GST_CAT_AUTOPLUG, "have no caps for the buffer, Danger Will Robinson!");

if (autoplugger->disable_nocaps) {
  GST_DEBUG(GST_CAT_AUTOPLUG, "not dealing with lack of caps this time");
  return;
}

gst_scheduler_show(GST_ELEMENT_SCHED(autoplugger));

  autoplugger->paused++;
  if (autoplugger->paused == 1)
    /* try to PAUSE the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

    /* detach the srcpad */
    GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting cache from its downstream peer");
    autoplugger->srcpadpeer = GST_PAD(GST_PAD_PEER(autoplugger->cache_srcpad));
    gst_pad_disconnect(autoplugger->cache_srcpad,autoplugger->srcpadpeer);

    /* instantiate the typefind and set up the signal handlers */
    if (!autoplugger->typefind) {
      GST_DEBUG(GST_CAT_AUTOPLUG, "creating typefind and setting signal handler");
      autoplugger->typefind = gst_element_factory_make("typefind","unnamed_type_find");
      autoplugger->typefind_sinkpad = gst_element_get_pad(autoplugger->typefind,"sink");
      g_signal_connect (G_OBJECT(autoplugger->typefind),"have_type",
                         G_CALLBACK (gst_autoplugger_type_find_have_type), autoplugger);
    }
    /* add it to self and attach it */
    GST_DEBUG(GST_CAT_AUTOPLUG, "adding typefind to self and connecting to cache");
    gst_bin_add(GST_BIN(autoplugger),autoplugger->typefind);
    gst_pad_connect(autoplugger->cache_srcpad,autoplugger->typefind_sinkpad);

    /* bring the typefind into playing state */
    GST_DEBUG(GST_CAT_AUTOPLUG, "setting typefind state to PLAYING");
    gst_element_set_state(autoplugger->cache,GST_STATE_PLAYING);

  autoplugger->paused--;
  if (autoplugger->paused == 0)
    /* try to PLAY the whole thing */
    gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

    GST_INFO(GST_CAT_AUTOPLUG,"here we go into nothingness, hoping the typefind will return us to safety");
gst_scheduler_show(GST_ELEMENT_SCHED(autoplugger));
  } else {
/*    * attach the cache_empty handler, since the cache simply isn't needed *
 *    g_signal_connect (G_OBJECT(autoplugger->cache),"cache_empty",
 *                       gst_autoplugger_cache_empty,autoplugger);
 */ 
  }
}

static void
gst_autoplugger_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAutoplugger *autoplugger;

  autoplugger = GST_AUTOPLUGGER (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_autoplugger_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAutoplugger *autoplugger;

  autoplugger = GST_AUTOPLUGGER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new ("autoplugger", GST_TYPE_AUTOPLUGGER,
                                    &gst_autoplugger_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "autoplugger",
  plugin_init
};

