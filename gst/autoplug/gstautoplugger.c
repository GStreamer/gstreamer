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
  (GTK_CHECK_CAST((obj),GST_TYPE_AUTOPLUGGER,GstAutoplugger))
#define GST_AUTOPLUGGER_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUGGER,GstAutopluggerClass))
#define GST_IS_AUTOPLUGGER(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_AUTOPLUGGER))
#define GST_IS_AUTOPLUGGER_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUGGER))

typedef struct _GstAutoplugger GstAutoplugger;
typedef struct _GstAutopluggerClass GstAutopluggerClass;

struct _GstAutoplugger {
  GstBin bin;

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

static void			gst_autoplugger_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void			gst_autoplugger_get_arg		(GtkObject *object, GtkArg *arg, guint id);

//static GstElementStateReturn	gst_autoplugger_change_state	(GstElement *element);


static void	gst_autoplugger_external_sink_caps_changed	(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_src_caps_changed	(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_sink_caps_nego_failed	(GstPad *pad, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_src_caps_nego_failed	(GstPad *pad, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_sink_connected		(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_src_connected		(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger);

static void	gst_autoplugger_cache_first_buffer		(GstElement *element,GstBuffer *buf,GstAutoplugger *autoplugger);
static void	gst_autoplugger_cache_empty			(GstElement *element, GstAutoplugger *autoplugger);
static void	gst_autoplugger_typefind_have_type		(GstElement *element, GstCaps *caps, GstAutoplugger *autoplugger);

static GstElementClass *parent_class = NULL;
//static guint gst_autoplugger_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_autoplugger_get_type(void) {
  static GtkType autoplugger_type = 0;

  if (!autoplugger_type) {
    static const GtkTypeInfo autoplugger_info = {
      "GstAutoplugger",
      sizeof(GstAutoplugger),
      sizeof(GstAutopluggerClass),
      (GtkClassInitFunc)gst_autoplugger_class_init,
      (GtkObjectInitFunc)gst_autoplugger_init,
      (GtkArgSetFunc)gst_autoplugger_set_arg,
      (GtkArgGetFunc)gst_autoplugger_get_arg,
      (GtkClassInitFunc)NULL,
    };
    autoplugger_type = gtk_type_unique (GST_TYPE_BIN, &autoplugger_info);
  }
  return autoplugger_type;
}

static void
gst_autoplugger_class_init (GstAutopluggerClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

/*
  gst_autoplugger_signals[_EMPTY] =
    gtk_signal_new ("_empty", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstAutopluggerClass, _empty),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
  gtk_object_class_add_signals (gtkobject_class, gst_autoplugger_signals, LAST_SIGNAL);
*/

/*
  gtk_object_add_arg_type ("GstAutoplugger::buffer_count", GTK_TYPE_INT,
                           GTK_ARG_READABLE, ARG_BUFFER_COUNT);
  gtk_object_add_arg_type ("GstAutoplugger::reset", GTK_TYPE_BOOL,
                           GTK_ARG_WRITABLE, ARG_RESET);
*/

  gtkobject_class->set_arg = gst_autoplugger_set_arg;
  gtkobject_class->get_arg = gst_autoplugger_get_arg;

//  gstelement_class->change_state = gst_autoplugger_change_state;
}

static void
gst_autoplugger_init (GstAutoplugger *autoplugger)
{
  // create the autoplugger cache, which is the fundamental unit of the autopluggerger
  // FIXME we need to find a way to set element's name before _init
  // FIXME ... so we can name the subelements uniquely
  autoplugger->cache = gst_elementfactory_make("autoplugcache", "unnamed_autoplugcache");
  g_return_if_fail (autoplugger->cache != NULL);

  // attach signals to the cache
  gtk_signal_connect (GTK_OBJECT (autoplugger->cache), "first_buffer",
                      GTK_SIGNAL_FUNC (gst_autoplugger_cache_first_buffer), autoplugger);

  // add the cache to self
  gst_bin_add (GST_BIN(autoplugger), autoplugger->cache);

  // get the cache's pads so we can attach stuff to them
  autoplugger->cache_sinkpad = gst_element_get_pad (autoplugger->cache, "sink");
  autoplugger->cache_srcpad = gst_element_get_pad (autoplugger->cache, "src");

  // attach handlers to the typefind pads
  gtk_signal_connect (GTK_OBJECT (autoplugger->cache_sinkpad), "caps_changed",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_sink_caps_changed), autoplugger);
  gtk_signal_connect (GTK_OBJECT (autoplugger->cache_srcpad), "caps_changed",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_src_caps_changed), autoplugger);
  gtk_signal_connect (GTK_OBJECT (autoplugger->cache_sinkpad), "caps_nego_failed",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_sink_caps_nego_failed), autoplugger);
  gtk_signal_connect (GTK_OBJECT (autoplugger->cache_srcpad), "caps_nego_failed",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_src_caps_nego_failed), autoplugger);
  gtk_signal_connect (GTK_OBJECT (autoplugger->cache_sinkpad), "connected",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_sink_connected), autoplugger);
  gtk_signal_connect (GTK_OBJECT (autoplugger->cache_srcpad), "connected",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_src_connected), autoplugger);

  // ghost both of these pads to the outside world
  gst_element_add_ghost_pad (GST_ELEMENT(autoplugger), autoplugger->cache_sinkpad, "sink");
  gst_element_add_ghost_pad (GST_ELEMENT(autoplugger), autoplugger->cache_srcpad, "src");
}


static void
gst_autoplugger_external_sink_connected(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger)
{
  GstPadTemplate *peertemplate;
  GstCaps *peercaps, *peertemplatecaps;

  GST_INFO(GST_CAT_AUTOPLUG, "have cache:sink connected");
//  autoplugger->sinkpadpeer = peerpad;

  if (autoplugger->sinkpadpeer) {
    peercaps = GST_PAD_CAPS(autoplugger->sinkpadpeer);
    if (peercaps)
      GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer: %s",
               gst_caps_get_mime(peercaps));
    peertemplate = GST_PAD_PADTEMPLATE(autoplugger->sinkpadpeer);
    if (peertemplate) {
      peertemplatecaps = GST_PADTEMPLATE_CAPS(peertemplate);
      if (peertemplatecaps) {
        GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer's padtemplate %s",
                 gst_caps_get_mime(peertemplatecaps));
        GST_DEBUG(GST_CAT_AUTOPLUG, "turning on caps nego proxying in cache\n");
        gtk_object_set(GTK_OBJECT(autoplugger->cache),"caps_proxy",TRUE,NULL);
      }
    }
  }
}

static void
gst_autoplugger_external_src_connected(GstPad *pad, GstPad *peerpad, GstAutoplugger *autoplugger)
{
  GstPadTemplate *peertemplate;
  GstCaps *peercaps, *peertemplatecaps;

  GST_INFO(GST_CAT_AUTOPLUG, "have cache:src connected");
//  autoplugger->srcpadpeer = peerpad;

  if (autoplugger->srcpadpeer) {
    peercaps = GST_PAD_CAPS(autoplugger->srcpadpeer);
    if (peercaps)
      GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer: %s",
               gst_caps_get_mime(peercaps));
    peertemplate = GST_PAD_PADTEMPLATE(autoplugger->srcpadpeer);
    if (peertemplate) {
      peertemplatecaps = GST_PADTEMPLATE_CAPS(peertemplate);
      if (peertemplatecaps) {
        GST_INFO(GST_CAT_AUTOPLUG, "there are some caps on this pad's peer's padtemplate %s",
                 gst_caps_get_mime(peertemplatecaps));
        autoplugger->sinktemplatecaps = peertemplatecaps;
        GST_DEBUG(GST_CAT_AUTOPLUG, "turning on caps nego proxying in cache\n");
        gtk_object_set(GTK_OBJECT(autoplugger->cache),"caps_proxy",TRUE,NULL);
      }
    }
  }
}


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


static void
gst_autoplugger_autoplug(GstAutoplugger *autoplugger,GstPad *srcpad,GstCaps *srccaps,GstCaps *sinkcaps)
{
  GstPad *sinkpad;

  sinkpad = GST_PAD(GST_PAD_PEER(srcpad));
  GST_DEBUG(GST_CAT_AUTOPLUG,"disconnecting %s:%s and %s:%s to autoplug between them\n",
            GST_DEBUG_PAD_NAME(srcpad),GST_DEBUG_PAD_NAME(sinkpad));
  GST_DEBUG(GST_CAT_AUTOPLUG,"srcpadcaps are of type %s\n",gst_caps_get_mime(srccaps));
  GST_DEBUG(GST_CAT_AUTOPLUG,"sinkpadcaps are of type %s\n",gst_caps_get_mime(sinkcaps));

// try to PAUSE the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

  // disconnect the pads
  GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting the pads that will be joined by an autobin\n");
  gst_pad_disconnect(srcpad,sinkpad);

  if (!autoplugger->autoplug) {
    autoplugger->autoplug = gst_autoplugfactory_make("static");
  }
  GST_DEBUG(GST_CAT_AUTOPLUG, "building autoplugged bin between caps\n");
  autoplugger->autobin = gst_autoplug_to_caps(autoplugger->autoplug,
    srccaps,sinkcaps,NULL);
  g_return_if_fail(autoplugger->autobin != NULL);
  gst_bin_add(GST_BIN(autoplugger),autoplugger->autobin);

  GST_DEBUG(GST_CAT_AUTOPLUG, "copying failed caps to srcpad %s:%s to ensure renego\n",GST_DEBUG_PAD_NAME(autoplugger->cache_srcpad));
//  gst_pad_set_caps(srcpad,srccaps);

  // attach the autoplugged bin
  GST_DEBUG(GST_CAT_AUTOPLUG, "attaching the autoplugged bin between the two pads\n");
  gst_pad_connect(srcpad,gst_element_get_pad(autoplugger->autobin,"sink"));
  gst_pad_connect(gst_element_get_pad(autoplugger->autobin,"src_00"),sinkpad);

// try to PLAY the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

}

static void
gst_autoplugger_external_sink_caps_nego_failed(GstPad *pad, GstAutoplugger *autoplugger)
{
  GstPad *srcpad_peer;
  GstPadTemplate *srcpad_peer_template;
  GstCaps *srcpad_peer_caps;
  GstPad *sinkpad_peer;
  GstCaps *sinkpad_peer_caps;

  GST_INFO(GST_CAT_AUTOPLUG, "have caps nego failure on sinkpad %s:%s!!!",GST_DEBUG_PAD_NAME(pad));

  srcpad_peer = GST_PAD(GST_PAD_PEER(autoplugger->cache_srcpad));
  g_return_if_fail(srcpad_peer != NULL);
  srcpad_peer_template = GST_PAD_PADTEMPLATE(srcpad_peer);
  g_return_if_fail(srcpad_peer_template != NULL);
  srcpad_peer_caps = GST_PADTEMPLATE_CAPS(srcpad_peer_template);
  g_return_if_fail(srcpad_peer_caps != NULL);

  sinkpad_peer = GST_PAD(GST_PAD_PEER(pad));
  g_return_if_fail(sinkpad_peer != NULL);
  sinkpad_peer_caps = GST_PAD_CAPS(sinkpad_peer);
  g_return_if_fail(sinkpad_peer_caps != NULL);

  gst_autoplugger_autoplug(autoplugger,autoplugger->cache_srcpad,sinkpad_peer_caps,srcpad_peer_caps);
}

static void
gst_autoplugger_external_src_caps_nego_failed(GstPad *pad, GstAutoplugger *autoplugger)
{
  GST_INFO(GST_CAT_AUTOPLUG, "have caps nego failure on src!!!");
}


static void
gst_autoplugger_cache_empty(GstElement *element, GstAutoplugger *autoplugger)
{
  GstPad *cache_sinkpad_peer,*cache_srcpad_peer;

  GST_INFO(GST_CAT_AUTOPLUG, "autoplugger cache has hit empty, we can now remove it");

// try to PAUSE the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

  // disconnect the cache from its peers
  GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting autoplugcache from its peers\n");
  cache_sinkpad_peer = GST_PAD (GST_PAD_PEER(autoplugger->cache_sinkpad));
  cache_srcpad_peer = GST_PAD (GST_PAD_PEER(autoplugger->cache_srcpad));
  gst_pad_disconnect(cache_sinkpad_peer,autoplugger->cache_sinkpad);
  gst_pad_disconnect(autoplugger->cache_srcpad,cache_srcpad_peer);

  // remove the cache from self
  GST_DEBUG(GST_CAT_AUTOPLUG, "removing the cache from the autoplugger\n");
  gst_bin_remove (GST_BIN(autoplugger), autoplugger->cache);

  // connect the two pads
  GST_DEBUG(GST_CAT_AUTOPLUG, "reconnecting the autoplugcache's former peers\n");
  gst_pad_connect(cache_sinkpad_peer,cache_srcpad_peer);

// try to PLAY the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

  xmlSaveFile("autoplugger.gst", gst_xml_write(GST_ELEMENT_SCHED(autoplugger)->parent));

  GST_INFO(GST_CAT_AUTOPLUG, "autoplugger_cache_empty finished");
}

static void
gst_autoplugger_typefind_have_type(GstElement *element, GstCaps *caps, GstAutoplugger *autoplugger) 
{
  GST_INFO(GST_CAT_AUTOPLUG, "typefind claims to have a type: %s",gst_caps_get_mime(caps));

gst_schedule_show(GST_ELEMENT_SCHED(autoplugger));

// try to PAUSE the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

  // first disconnect the typefind and shut it down
  GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting typefind from the cache\n");
  gst_pad_disconnect(autoplugger->cache_srcpad,autoplugger->typefind_sinkpad);
  gst_bin_remove(GST_BIN(autoplugger),autoplugger->typefind);

  // FIXME FIXME now we'd compare caps and see if we need to autoplug something in the middle, but for 
  // now we're going to just reconnect where we left off
  // FIXME FIXME FIXME!!!: this should really be done in the caps failure!!!
/*
  if (!autoplugger->autoplug) {
    autoplugger->autoplug = gst_autoplugfactory_make("static");
  }
  autoplugger->autobin = gst_autoplug_to_caps(autoplugger->autoplug,
      caps,autoplugger->sinktemplatecaps,NULL);
  g_return_if_fail(autoplugger->autobin != NULL);
  gst_bin_add(GST_BIN(autoplugger),autoplugger->autobin);

//  // re-attach the srcpad's original peer to the cache
//  GST_DEBUG(GST_CAT_AUTOPLUG, "reconnecting the cache to the downstream peer\n");
//  gst_pad_connect(autoplugger->cache_srcpad,autoplugger->srcpadpeer);

  // attach the autoplugged bin
  GST_DEBUG(GST_CAT_AUTOPLUG, "attaching the autoplugged bin between cache and downstream peer\n");
  gst_pad_connect(autoplugger->cache_srcpad,gst_element_get_pad(autoplugger->autobin,"sink"));
  gst_pad_connect(gst_element_get_pad(autoplugger->autobin,"src_00"),autoplugger->srcpadpeer);
*/

  // reattach the original outside srcpad
  GST_DEBUG(GST_CAT_AUTOPLUG,"re-attaching downstream peer to autoplugcache\n");
  gst_pad_connect(autoplugger->cache_srcpad,autoplugger->srcpadpeer);

  // now reset the autoplugcache
  GST_DEBUG(GST_CAT_AUTOPLUG, "resetting the cache to send first buffer(s) again\n");
  gtk_object_set(GTK_OBJECT(autoplugger->cache),"reset",TRUE,NULL);

  // attach the cache_empty handler
  // FIXME this is the wrong place, it shouldn't be done until we get successful caps nego!
  gtk_signal_connect(GTK_OBJECT(autoplugger->cache),"cache_empty",
                     GTK_SIGNAL_FUNC(gst_autoplugger_cache_empty),autoplugger);

// try to PLAY the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

  GST_INFO(GST_CAT_AUTOPLUG, "typefind_have_type finished");
gst_schedule_show(GST_ELEMENT_SCHED(autoplugger));
}

static void
gst_autoplugger_cache_first_buffer(GstElement *element,GstBuffer *buf,GstAutoplugger *autoplugger)
{
return;
  GST_INFO(GST_CAT_AUTOPLUG, "have first buffer through cache");
  autoplugger->cache_first_buffer = TRUE;

  // if there are no established caps, worry
  if (!autoplugger->sinkcaps) {
    GST_INFO(GST_CAT_AUTOPLUG, "have no caps for the buffer, Danger Will Robinson!");

gst_schedule_show(GST_ELEMENT_SCHED(autoplugger));

// try to PAUSE the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PAUSED);

    // detach the srcpad
    GST_DEBUG(GST_CAT_AUTOPLUG, "disconnecting cache from its downstream peer\n");
    autoplugger->srcpadpeer = GST_PAD(GST_PAD_PEER(autoplugger->cache_srcpad));
    gst_pad_disconnect(autoplugger->cache_srcpad,autoplugger->srcpadpeer);

    // instantiate the typefind and set up the signal handlers
    if (!autoplugger->typefind) {
      GST_DEBUG(GST_CAT_AUTOPLUG, "creating typefind and setting signal handler\n");
      autoplugger->typefind = gst_elementfactory_make("typefind","unnamed_typefind");
      autoplugger->typefind_sinkpad = gst_element_get_pad(autoplugger->typefind,"sink");
      gtk_signal_connect(GTK_OBJECT(autoplugger->typefind),"have_type",
                         GTK_SIGNAL_FUNC (gst_autoplugger_typefind_have_type), autoplugger);
    }
    // add it to self and attach it
    GST_DEBUG(GST_CAT_AUTOPLUG, "adding typefind to self and connecting to cache\n");
    gst_bin_add(GST_BIN(autoplugger),autoplugger->typefind);
    gst_pad_connect(autoplugger->cache_srcpad,autoplugger->typefind_sinkpad);

    // bring the typefind into playing state
    GST_DEBUG(GST_CAT_AUTOPLUG, "setting typefind state to PLAYING\n");
    gst_element_set_state(autoplugger->cache,GST_STATE_PLAYING);

// try to PLAY the whole thing
gst_element_set_state(GST_ELEMENT_SCHED(autoplugger)->parent,GST_STATE_PLAYING);

    GST_INFO(GST_CAT_AUTOPLUG,"here we go into nothingness, hoping the typefind will return us to safety");
gst_schedule_show(GST_ELEMENT_SCHED(autoplugger));
  } else {
//    // attach the cache_empty handler, since the cache simply isn't needed
//    gtk_signal_connect(GTK_OBJECT(autoplugger->cache),"cache_empty",
//                       GTK_SIGNAL_FUNC(gst_autoplugger_cache_empty),autoplugger);  
  }
}

static void
gst_autoplugger_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstAutoplugger *autoplugger;

  autoplugger = GST_AUTOPLUGGER (object);

  switch (id) {
    default:
      break;
  }
}

static void
gst_autoplugger_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstAutoplugger *autoplugger;

  autoplugger = GST_AUTOPLUGGER (object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new ("autoplugger", GST_TYPE_AUTOPLUGGER,
                                    &gst_autoplugger_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_plugin_add_factory (plugin, factory);

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "autoplugger",
  plugin_init
};

