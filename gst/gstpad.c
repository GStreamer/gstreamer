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


//#define DEBUG_ENABLED
#include <gst/gst.h>
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
//static guint gst_pad_signals[LAST_SIGNAL] = { 0 };

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
  pad->chainfunc = NULL;
  pad->pullfunc = NULL;
  pad->pushfunc = NULL;
  pad->qosfunc = NULL;
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

  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(direction != GST_PAD_UNKNOWN, NULL);

  pad = GST_PAD(gtk_type_new(gst_pad_get_type()));
  pad->name = g_strdup(name);
  pad->direction = direction;
  return pad;
}

/**
 * gst_pad_get_direction:
 * @pad: the Pad to get the direction from
 *
 * get the direction of the pad
 *
 * Returns: the direction of the pad
 */
GstPadDirection gst_pad_get_direction(GstPad *pad) {
  g_return_val_if_fail(pad != NULL, GST_PAD_UNKNOWN);
  g_return_val_if_fail(GST_IS_PAD(pad), GST_PAD_UNKNOWN);

  return pad->direction;
}

/**
 * gst_pad_set_name:
 * @pad: the pad to set the name of
 * @name: the name of the pad
 *
 * set the name of a pad
 */
void gst_pad_set_name(GstPad *pad,gchar *name) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  if (pad->name != NULL)
    g_free(pad->name);

  pad->name = g_strdup(name);
}

/**
 * gst_pad_get_name:
 * @pad: the pad to get the name of
 *
 * get the name of a pad
 *
 * Returns: the name of the pad
 */
gchar *gst_pad_get_name(GstPad *pad) {
  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  return pad->name;
}

void gst_pad_set_pull_function(GstPad *pad,GstPadPullFunction pull) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  g_print("gstpad: pad setting pull function\n");

  pad->pullfunc = pull;
}

void gst_pad_set_chain_function(GstPad *pad,GstPadChainFunction chain) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  
  pad->chainfunc = chain;
}

void gst_pad_set_qos_function(GstPad *pad,GstPadQoSFunction qos) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  
  pad->qosfunc = qos;
}

/* gst_pad_push is handed the src pad and the buffer to push */
void gst_pad_push(GstPad *pad,GstBuffer *buffer) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(GST_PAD_CONNECTED(pad));
  g_return_if_fail(buffer != NULL);

  gst_trace_add_entry(NULL,0,buffer,"push buffer");

  // first check to see if there's a push handler
  if (pad->pushfunc != NULL) {
    //g_print("-- gst_pad_push(): putting buffer in pen and calling push handler\n");
    // put the buffer in peer's holding pen
    pad->peer->bufpen = buffer;
    // now inform the handler that the peer pad has something
    (pad->pushfunc)(pad->peer);
  // otherwise we assume we're chaining directly
  } else if (pad->chainfunc != NULL) {
    //g_print("-- gst_pad_push(): calling chain handler\n");
    (pad->chainfunc)(pad->peer,buffer);
  // else we squawk
  } else {
    g_print("-- gst_pad_push(): houston, we have a problem, no way of talking to peer\n");
  }

}

/* gst_pad_pull() is given the sink pad */
GstBuffer *gst_pad_pull(GstPad *pad) {
  GstBuffer *buf;
//  GstElement *peerparent;
//  cothread_state *state;

  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

//  g_print("-- gst_pad_pull(): attempting to pull buffer\n");

//  g_return_val_if_fail(pad->pullfunc != NULL, NULL);

  // if no buffer in pen and there's a pull handler, fire it
  if (pad->bufpen == NULL) {
    if (pad->pullfunc != NULL) {
//      g_print("-- gst_pad_pull(): calling pull handler\n");
      (pad->pullfunc)(pad->peer);
    } else {
      g_print("-- gst_pad_pull(): no buffer in pen, and no handler to get one there!!!\n");
    }
  }

  // if there's a buffer in the holding pen, use it
  if (pad->bufpen != NULL) {
//    g_print("-- gst_pad_pull(): buffer available, pulling\n");
    buf = pad->bufpen;
    pad->bufpen = NULL;
    return buf;
  // else we have a big problem...
  } else {
    g_print("-- gst_pad_pull(): uh, nothing in pen and no handler\n");
    return NULL;
  }

  return NULL;
}

void gst_pad_chain(GstPad *pad) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(pad->peer != NULL);
  g_return_if_fail(pad->chainfunc != NULL);

  if (pad->bufpen && pad->chainfunc)
    (pad->chainfunc)(pad,pad->bufpen);
}


/**
 * gst_pad_handle_qos:
 * @pad: the pad to handle the QoS message
 * @qos_message: the QoS message to handle
 *
 */
void gst_pad_handle_qos(GstPad *pad,
	               glong qos_message)
{
  GstElement *element;
  GList *pads;
  GstPad *target_pad;

  DEBUG("gst_pad_handle_qos(\"%s\",%08ld)\n", GST_ELEMENT(pad->parent)->name,qos_message);

  if (pad->qosfunc) {
    (pad->qosfunc)(pad,qos_message);
  }
  else {
    element = GST_ELEMENT(pad->peer->parent);

    pads = element->pads;
    DEBUG("gst_pad_handle_qos recurse(\"%s\",%08ld)\n", element->name,qos_message);
    while (pads) {
      target_pad = GST_PAD(pads->data);
      if (target_pad->direction == GST_PAD_SINK) {
        gst_pad_handle_qos(target_pad, qos_message);
      }
      pads = g_list_next(pads);
    }
  }

  return;
}

void gst_pad_disconnect(GstPad *srcpad,GstPad *sinkpad) {

  /* generic checks */
  g_return_if_fail(srcpad != NULL);
  g_return_if_fail(GST_IS_PAD(srcpad));
  g_return_if_fail(srcpad->peer != NULL);
  g_return_if_fail(sinkpad != NULL);
  g_return_if_fail(GST_IS_PAD(sinkpad));
  g_return_if_fail(sinkpad->peer != NULL);

  g_return_if_fail((srcpad->direction == GST_PAD_SRC) &&
                   (sinkpad->direction == GST_PAD_SINK));

  /* first clear peers */
  srcpad->peer = NULL;
  sinkpad->peer = NULL;

  srcpad->chainfunc = NULL;
  srcpad->pullfunc = NULL;
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
  srcpad->chainfunc = sinkpad->chainfunc;
  /* and the pull function */
  //srcpad->pullfunc = sinkpad->pullfunc;

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

  //g_print("set parent %s\n", gst_element_get_name(parent));

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
  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  return pad->parent;
}

GList *gst_pad_get_ghost_parents(GstPad *pad) {
  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  return pad->ghostparents;
}

guint16 gst_pad_get_type_id(GstPad *pad) {
  g_return_val_if_fail(pad != NULL, 0);
  g_return_val_if_fail(GST_IS_PAD(pad), 0);

  return pad->type;
}

void gst_pad_set_type_id(GstPad *pad,guint16 id) {
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(gst_type_find_by_id(id) != NULL);

  pad->type = id;
}

GstPad *gst_pad_get_peer(GstPad *pad) {
  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  return pad->peer;
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

  self = xmlNewChild(parent,NULL,"ghostpad",NULL);
  xmlNewChild(self,NULL,"name",pad->name);
  xmlNewChild(self,NULL,"parent",GST_ELEMENT(pad->parent)->name);

  return self;
}
