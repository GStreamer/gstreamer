/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include <gst/gstpad.h>
#include <gst/gstelement.h>
#include <gst/gsttype.h>


/* Pad signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_pad_class_init(GstPadClass *klass);
static void gst_pad_init(GstPad *pad);
static void gst_pad_real_destroy(GtkObject *object);


static GstObject *parent_class = NULL;
static guint gst_pad_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_pad_get_type(void) {
  static GtkType pad_type = 0;

  if (!pad_type) {
    static const GtkTypeInfo pad_info = {
      "GstPad",
      sizeof(GstPad),
      sizeof(GstPadClass),
      (GtkClassInitFunc)gst_pad_class_init,
      (GtkObjectInitFunc)gst_pad_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    pad_type = gtk_type_unique(GST_TYPE_OBJECT,&pad_info);
  }
  return pad_type;
}

static void
gst_pad_class_init(GstPadClass *klass) {
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_OBJECT);

  gtkobject_class->destroy = gst_pad_real_destroy;
}

static void gst_pad_init(GstPad *pad) {
  pad->type = 0;
  pad->direction = GST_PAD_UNKNOWN;
  pad->peer = NULL;
  pad->chain = NULL;
  pad->pull = NULL;
  pad->parent = NULL;
  pad->ghostparents = NULL;
}

/**
 * gst_pad_new:
 * @name: name of new pad
 * @direction: either GST_PAD_SRC or GST_PAD_SINK
 *
 * Create a new pad with given name.
 *
 * Returns: new pad
 */
GstPad *gst_pad_new(gchar *name,GstPadDirection direction) {
  GstPad *pad;

  g_return_if_fail(name != NULL);
  g_return_if_fail(direction != GST_PAD_UNKNOWN);

  pad = GST_PAD(gtk_type_new(gst_pad_get_type()));
  pad->name = g_strdup(name);
  pad->direction = direction;
  return pad;
}

GstPadDirection gst_pad_get_direction(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  return pad->direction;
}

void gst_pad_set_name(GstPad *pad,gchar *name) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  if (pad->name != NULL)
    g_free(pad->name);

  pad->name = g_strdup(name);
}

gchar *gst_pad_get_name(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  return pad->name;
}

void gst_pad_set_pull_function(GstPad *pad,GstPadPullFunction pull) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

	fprintf(stderr, "pad setting pull function\n");
  
  pad->pull = pull;
}

void gst_pad_set_chain_function(GstPad *pad,GstPadChainFunction chain) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  
  pad->chain = chain;
}

void gst_pad_push(GstPad *pad,GstBuffer *buffer) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buffer != NULL);

  gst_trace_add_entry(NULL,0,buffer,"push buffer");
  // if the chain function exists for the pad, call it directly
  if (pad->chain)
    (pad->chain)(pad->peer,buffer);
  // else we're likely going to have to coroutine it
  else {
    pad->peer->bufpen = buffer;
    g_print("would switch to a coroutine here...\n");
    if (!GST_IS_ELEMENT(pad->peer->parent))
      g_print("eek, this isn't an element!\n");
    if (GST_ELEMENT(pad->peer->parent)->threadstate != NULL)
      cothread_switch(GST_ELEMENT(pad->peer->parent)->threadstate);
  }
}

GstBuffer *gst_pad_pull(GstPad *pad) {
  GstBuffer *buf;
  GstElement *peerparent;
  cothread_state *state;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

	// if the pull function exists for the pad, call it directly
	if (pad->pull) {
		return (pad->pull)(pad->peer);
  // else we're likely going to have to coroutine it
	} else if (pad->bufpen == NULL) {
    g_print("no buffer available, will have to do something about it\n");
    peerparent = GST_ELEMENT(pad->peer->parent);
    // if they're a cothread too, we can just switch to them
    if (peerparent->threadstate != NULL) {
      cothread_switch(peerparent->threadstate);
    // otherwise we have to switch to the main thread
    } else {
      state = cothread_main(GST_ELEMENT(pad->parent)->threadstate->ctx);
      g_print("switching to supposed 0th thread at %p\n",state);
      cothread_switch(state);
    }
  } else {
    g_print("buffer available, pulling\n");
    buf = pad->bufpen;
    pad->bufpen = NULL;
    return buf;
  }
}

void gst_pad_chain(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(pad->peer != NULL);
  g_return_if_fail(pad->chain != NULL);

  if (pad->bufpen)
    (pad->chain)(pad,pad->bufpen);
}

void gst_pad_connect(GstPad *srcpad,GstPad *sinkpad) {
  GstPad *temppad;

  /* generic checks */
  g_return_if_fail(srcpad != NULL);
  g_return_if_fail(GST_IS_PAD(srcpad));
  g_return_if_fail(srcpad->peer == NULL);
  g_return_if_fail(sinkpad != NULL);
  g_return_if_fail(GST_IS_PAD(sinkpad));
  g_return_if_fail(sinkpad->peer == NULL);
//  g_return_if_fail(sinkpad->chain != NULL);

  /* check for reversed directions and swap if necessary */
  if ((srcpad->direction == GST_PAD_SINK) &&
      (sinkpad->direction == GST_PAD_SRC)) {
    temppad = srcpad;
    srcpad = sinkpad;
    sinkpad = temppad;
  }
  g_return_if_fail((srcpad->direction == GST_PAD_SRC) &&
                   (sinkpad->direction == GST_PAD_SINK));

  /* first set peers */
  srcpad->peer = sinkpad;
  sinkpad->peer = srcpad;

  /* now copy the chain pointer from sink to src */
  srcpad->chain = sinkpad->chain;
  /* and the pull function */
  srcpad->pull = sinkpad->pull;

  /* set the connected flag */
  /* FIXME: set connected flag */
}

void gst_pad_set_parent(GstPad *pad,GstObject *parent) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(pad->parent == NULL);
  g_return_if_fail(parent != NULL);
  g_return_if_fail(GTK_IS_OBJECT(parent));
  g_return_if_fail((gpointer)pad != (gpointer)parent);

  pad->parent = parent;
}

void gst_pad_add_ghost_parent(GstPad *pad,GstObject *parent) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(parent != NULL);
  g_return_if_fail(GTK_IS_OBJECT(parent));

  pad->ghostparents = g_list_prepend(pad->ghostparents,parent);
}


void gst_pad_remove_ghost_parent(GstPad *pad,GstObject *parent) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(parent != NULL);
  g_return_if_fail(GTK_IS_OBJECT(parent));

  pad->ghostparents = g_list_remove(pad->ghostparents,parent);
}

GstObject *gst_pad_get_parent(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  return pad->parent;
}

GList *gst_pad_get_ghost_parents(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  return pad->ghostparents;
}

guint32 gst_pad_get_type_id(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  return pad->type;
}

void gst_pad_set_type_id(GstPad *pad,guint16 id) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(gst_type_find_by_id(id) != NULL);

  pad->type = id;
}

GstPad *gst_pad_get_peer(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  return pad->peer;
}

GstPadDirection gst_pad_get_directory(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  return pad->direction;
}

static void gst_pad_real_destroy(GtkObject *object) {
  GstPad *pad = GST_PAD(object);

//  g_print("in gst_pad_real_destroy()\n");

  if (pad->name)
    g_free(pad->name);
  g_list_free(pad->ghostparents);
}

xmlNodePtr gst_pad_save_thyself(GstPad *pad,xmlNodePtr parent) {
  xmlNodePtr self;
  GstPad *peer;

  self = xmlNewChild(parent,NULL,"pad",NULL);
  xmlNewChild(self,NULL,"name",pad->name);
  if (pad->peer != NULL) {
    peer = pad->peer;
    // first check to see if the peer's parent's parent is the same
    if (pad->parent->parent == peer->parent->parent)
      // we just save it off
      xmlNewChild(self,NULL,"peer",g_strdup_printf("%s.%s",
                    GST_ELEMENT(peer->parent)->name,peer->name));
  } else
    xmlNewChild(self,NULL,"peer","");

  return self;
}

xmlNodePtr gst_pad_ghost_save_thyself(GstPad *pad,GstElement *bin,xmlNodePtr parent) {
  xmlNodePtr self;
  GstPad *peer;

  self = xmlNewChild(parent,NULL,"ghostpad",NULL);
  xmlNewChild(self,NULL,"name",pad->name);
  xmlNewChild(self,NULL,"parent",GST_ELEMENT(pad->parent)->name);

  return self;
}
