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
#include "gstdebug.h"
#include "gstpad.h"
#include "gstelement.h"
#include "gsttype.h"

/* Pad signals and args */
enum {
  SET_ACTIVE,
  CAPS_CHANGED,
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_ACTIVE,
  /* FILL ME */
};


static void 	gst_pad_class_init		(GstPadClass *klass);
static void 	gst_pad_init			(GstPad *pad);

static void 	gst_pad_set_arg			(GtkObject *object,GtkArg *arg,guint id);
static void 	gst_pad_get_arg			(GtkObject *object,GtkArg *arg,guint id);

static void 	gst_pad_real_destroy		(GtkObject *object);

static void 	gst_pad_push_func		(GstPad *pad, GstBuffer *buf);

static GstObject *pad_parent_class = NULL;
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
gst_pad_class_init (GstPadClass *klass) 
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  pad_parent_class = gtk_type_class(GST_TYPE_OBJECT);

  gst_pad_signals[SET_ACTIVE] =
    gtk_signal_new ("set_active", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstPadClass, set_active),
                    gtk_marshal_NONE__BOOL, GTK_TYPE_NONE, 1,
                    GTK_TYPE_BOOL);
  gst_pad_signals[CAPS_CHANGED] =
    gtk_signal_new ("caps_changed", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstPadClass, caps_changed),
                    gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
                    GTK_TYPE_POINTER);

  gtk_object_add_arg_type ("GstPad::active", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_ACTIVE);

  gtkobject_class->destroy = gst_pad_real_destroy;
  gtkobject_class->set_arg = gst_pad_set_arg;
  gtkobject_class->get_arg = gst_pad_get_arg;
}

static void 
gst_pad_init (GstPad *pad) 
{
  pad->direction = GST_PAD_UNKNOWN;
  pad->peer = NULL;

  pad->chainfunc = NULL;
  pad->getfunc = NULL;
  pad->getregionfunc = NULL;
  pad->qosfunc = NULL;

  pad->pushfunc = NULL; //GST_DEBUG_FUNCPTR(gst_pad_push_func);
  pad->pullfunc = NULL;
  pad->pullregionfunc = NULL;

  pad->parent = NULL;
  pad->ghostparents = NULL;
  pad->caps = NULL;

  pad->padtemplate = NULL;
}

static void
gst_pad_set_arg (GtkObject *object,GtkArg *arg,guint id) {
  g_return_if_fail(GST_IS_PAD(object));

  switch (id) {
    case ARG_ACTIVE:
      if (GTK_VALUE_BOOL(*arg)) {
        gst_info("gstpad: activating pad\n");
        GST_FLAG_UNSET(object,GST_PAD_DISABLED);
      } else {
        gst_info("gstpad: de-activating pad\n");
        GST_FLAG_SET(object,GST_PAD_DISABLED);
      }
      gtk_signal_emit(GTK_OBJECT(object), gst_pad_signals[SET_ACTIVE],
                      ! GST_FLAG_IS_SET(object,GST_PAD_DISABLED));
      break;
    default:
      break;
  }
}

static void
gst_pad_get_arg (GtkObject *object,
                    GtkArg *arg,
                    guint id)
{
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_PAD (object));

  switch (id) {
    case ARG_ACTIVE:
      GTK_VALUE_BOOL (*arg) = ! GST_FLAG_IS_SET (object, GST_PAD_DISABLED);
      break;
    default:
      break;
  }
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
GstPad*
gst_pad_new (gchar *name,
	     GstPadDirection direction) 
{
  GstPad *pad;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (direction != GST_PAD_UNKNOWN, NULL);

  pad = GST_PAD (gtk_type_new (gst_pad_get_type ()));
  pad->name = g_strdup (name);
  pad->direction = direction;

  return pad;
}

/**
 * gst_pad_new_from_template:
 * @temp: the pad template to use
 * @name: the name of the element
 *
 * Create a new pad with given name from the given template.
 *
 * Returns: new pad
 */
GstPad*
gst_pad_new_from_template (GstPadTemplate *temp,
		           gchar *name) 
{
  GstPad *pad;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (temp != NULL, NULL);

  pad = gst_pad_new (name, temp->direction);
  pad->caps = temp->caps;
  pad->padtemplate = temp;

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
GstPadDirection 
gst_pad_get_direction (GstPad *pad) 
{
  g_return_val_if_fail (pad != NULL, GST_PAD_UNKNOWN);
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_UNKNOWN);

  return pad->direction;
}

/**
 * gst_pad_set_name:
 * @pad: the pad to set the name of
 * @name: the name of the pad
 *
 * set the name of a pad
 */
void 
gst_pad_set_name (GstPad *pad, 
		  const gchar *name) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  if (pad->name != NULL)
    g_free (pad->name);

  pad->name = g_strdup (name);
}

/**
 * gst_pad_get_name:
 * @pad: the pad to get the name of
 *
 * get the name of a pad
 *
 * Returns: the name of the pad, don't free.
 */
const gchar*
gst_pad_get_name (GstPad *pad) 
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return pad->name;
}

/**
 * gst_pad_set_chain_function:
 * @pad: the pad to set the chain function for
 * @chain: the chain function
 *
 * Set the given chain function for the pad
 */
void gst_pad_set_chain_function (GstPad *pad,
		                 GstPadChainFunction chain) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  
  pad->chainfunc = chain;
}

/**
 * gst_pad_set_pull_function:
 * @pad: the pad to set the get function for
 * @get: the get function
 *
 * Set the given get function for the pad
 */
void 
gst_pad_set_get_function (GstPad *pad,
			  GstPadGetFunction get) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  // the if and such should optimize out when DEBUG is off
  DEBUG("setting get function for %s:%s\n",GST_DEBUG_PAD_NAME(pad));

  pad->getfunc = get;
  DEBUG("getfunc for %s:%s(@%p) at %p is set to %p\n",GST_DEBUG_PAD_NAME(pad),pad,&pad->getfunc,get);
}

/**
 * gst_pad_set_getregion_function:
 * @pad: the pad to set the getregion function for
 * @getregion: the getregion function
 *
 * Set the given getregion function for the pad
 */
void 
gst_pad_set_getregion_function (GstPad *pad,
				GstPadGetRegionFunction getregion) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  DEBUG("gstpad: pad setting getregion function\n");

  pad->getregionfunc = getregion;
}

/**
 * gst_pad_set_qos_function:
 * @pad: the pad to set the qos function for
 * @qos: the qos function
 *
 * Set the given qos function for the pad
 */
void 
gst_pad_set_qos_function (GstPad *pad,
		          GstPadQoSFunction qos) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  
  pad->qosfunc = qos;
}

static void
gst_pad_push_func(GstPad *pad, GstBuffer *buf) 
{
  if (pad->peer->chainfunc != NULL) {
    DEBUG("calling chain function\n");
    (pad->peer->chainfunc)(pad,buf);
  } else {
    DEBUG("got a problem here: default pad_push handler in place, no chain function\n");
  }
}

/**
 * gst_pad_push:
 * @pad: the pad to push
 * @buffer: the buffer to push
 *
 * pushes a buffer along a src pad
 */
/*
void 
gst_pad_push (GstPad *pad, 
	      GstBuffer *buffer) 
{
  GstPad *peer;

  DEBUG_ENTER("(pad:'%s'(@%p),buffer:%p)",gst_pad_get_name(pad),pad,buffer);

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(GST_PAD_CONNECTED(pad));
  g_return_if_fail(buffer != NULL);

  // if the pad has been disabled, unreference the pad and let it drop
  if (GST_FLAG_IS_SET(pad,GST_PAD_DISABLED)) {
    g_print("gst_pad_push: pad disabled, dropping buffer\n");
    gst_buffer_unref(buffer);
    return;
  }

  gst_trace_add_entry(NULL,0,buffer,"push buffer");

  peer = pad->peer;
  g_return_if_fail(peer != NULL);

  // first check to see if there's a push handler
  if (pad->pushfunc != NULL) {
    DEBUG("putting the buffer in the pen and calling pushfunc\n");
    // put the buffer in peer's holding pen
    peer->bufpen = buffer;
    // now inform the handler that the peer pad has something
    (pad->pushfunc)(peer);
  // otherwise we assume we're chaining directly
  } else if (peer->chainfunc != NULL) {
    DEBUG("calling chain function\n");
    //g_print("-- gst_pad_push(): calling chain handler\n");
    (peer->chainfunc)(peer,buffer);
  // else we squawk
  } else {
    g_print("-- gst_pad_push(): houston, we have a problem, no way of talking to peer\n");
  }
}
*/

/**
 * gst_pad_pull:
 * @pad: the pad to pull
 *
 * pulls a buffer along a sink pad
 *
 * Returns: the buffer that was pulled
 */
/*
GstBuffer*
gst_pad_pull (GstPad *pad) 
{
  GstBuffer *buf;

  DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));

  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  // check to see if the peer pad is disabled.  return NULL if it is
  // FIXME: this may be the wrong way to go about it
  if (GST_FLAG_IS_SET(pad->peer,GST_PAD_DISABLED)) {
    DEBUG("pad disabled, returning NULL\n");
    return NULL;
  }

  // if no buffer in pen and there's a pull handler, fire it
  if (pad->bufpen == NULL) {
    if (pad->pullfunc != NULL) {
      DEBUG("calling pullfunc to fill buffer pen\n");
      (pad->pullfunc)(pad->peer);
    } else {
      DEBUG("no buffer in pen, and no handler (# %p) to get one there!!!\n",&pad->pullfunc);
      g_return_val_if_fail(pad->pullfunc != NULL, NULL);
    }
  }

  // if there's a buffer in the holding pen, use it
  if (pad->bufpen != NULL) {
    buf = pad->bufpen;
    pad->bufpen = NULL;
    return buf;
  // else we have a big problem...
  } else {
    DEBUG("no buffer in pen, and no handler\n");
    g_return_val_if_fail(pad->pullfunc != NULL, NULL);
    return NULL;
  }

  return NULL;
}
*/

/**
 * gst_pad_pull_region:
 * @pad: the pad to pull
 * @offset: the offset to pull
 * @size: the size to pull
 *
 * pulls a buffer along a sink pad with a given offset and size
 *
 * Returns: the buffer that was pulled
 */
/*
GstBuffer*
gst_pad_pull_region (GstPad *pad, 
		     gulong offset, 
		     gulong size) 
{
  GstBuffer *buf;

  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  DEBUG("-- gst_pad_pull_region(%s:%s): region (%lu,%lu)\n", 
		      GST_ELEMENT(pad->parent)->name, pad->peer->name,
		      offset, size);

  // if no buffer in pen and there's a pull handler, fire it
  if (pad->bufpen == NULL) {
    if (pad->pullregionfunc != NULL) {
      (pad->pullregionfunc)(pad->peer, offset, size);
    } else {
      g_print("-- gst_pad_pull_region(%s:%s): no buffer in pen, and no handler to get one there!!!\n", 
		      GST_ELEMENT(pad->parent)->name, pad->name);
    }
  }

  // if there's a buffer in the holding pen, use it
  if (pad->bufpen != NULL) {
    buf = pad->bufpen;
    pad->bufpen = NULL;
    return buf;
  // else we have a big problem...
  } else {
    g_print("-- gst_pad_pull_region(%s:%s): no buffer in pen, and no handler\n", 
		      GST_ELEMENT(pad->parent)->name, pad->peer->name);
    return NULL;
  }

  return NULL;
}
*/

/**
 * gst_pad_chain:
 * @pad: the pad to chain
 *
 * call the chain function of the given pad
 */
void 
gst_pad_chain (GstPad *pad) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (pad->peer != NULL);
  g_return_if_fail (pad->chainfunc != NULL);

  if (pad->bufpen && pad->chainfunc)
    (pad->chainfunc) (pad,pad->bufpen);
}


/**
 * gst_pad_handle_qos:
 * @pad: the pad to handle the QoS message
 * @qos_message: the QoS message to handle
 *
 * pass the qos message downstream
 */
void 
gst_pad_handle_qos(GstPad *pad,
	           glong qos_message)
{
  GstElement *element;
  GList *pads;
  GstPad *target_pad;

  DEBUG("gst_pad_handle_qos(\"%s\",%08ld)\n", GST_ELEMENT(pad->parent)->name,qos_message);

  if (pad->qosfunc) {
    (pad->qosfunc) (pad,qos_message);
  }
  else {
    element = GST_ELEMENT (pad->peer->parent);

    pads = element->pads;
    DEBUG("gst_pad_handle_qos recurse(\"%s\",%08ld)\n", element->name,qos_message);
    while (pads) {
      target_pad = GST_PAD (pads->data);
      if (target_pad->direction == GST_PAD_SINK) {
        gst_pad_handle_qos (target_pad, qos_message);
      }
      pads = g_list_next (pads);
    }
  }

  return;
}

/**
 * gst_pad_disconnect:
 * @srcpad: the source pad to disconnect
 * @sinkpad: the sink pad to disconnect
 *
 * disconnects the source pad from the sink pad
 */
void 
gst_pad_disconnect (GstPad *srcpad,
		    GstPad *sinkpad) 
{
  /* generic checks */
  g_return_if_fail (srcpad != NULL);
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (srcpad->peer != NULL);
  g_return_if_fail (sinkpad != NULL);
  g_return_if_fail (GST_IS_PAD (sinkpad));
  g_return_if_fail (sinkpad->peer != NULL);

  g_return_if_fail ((srcpad->direction == GST_PAD_SRC) &&
                    (sinkpad->direction == GST_PAD_SINK));

  /* first clear peers */
  srcpad->peer = NULL;
  sinkpad->peer = NULL;

}

/**
 * gst_pad_connect:
 * @srcpad: the source pad to connect
 * @sinkpad: the sink pad to connect
 *
 * connects the source pad to the sink pad
 */
void 
gst_pad_connect (GstPad *srcpad,
		 GstPad *sinkpad) 
{
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
 
  /* chack pad compatibility */
  if (srcpad->caps && sinkpad->caps) {
    if (!gst_caps_check_compatibility (srcpad->caps, sinkpad->caps))
      g_warning ("gstpad: connecting incompatible pads (%s:%s) and (%s:%s)\n",
		    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
    else
      DEBUG ("gstpad: connecting compatible pads (%s:%s) and (%s:%s)\n",
		    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
  }
  else
    DEBUG ("gstpad: could not check capabilities of pads (%s:%s) and (%s:%s)\n", 
		    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

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

/**
 * gst_pad_set_parent:
 * @pad: the pad to set the parent 
 * @parent: the object to set the parent to
 *
 * sets the parent object of a pad.
 */
void 
gst_pad_set_parent (GstPad *pad,
		    GstObject *parent) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (pad->parent == NULL);
  g_return_if_fail (parent != NULL);
  g_return_if_fail (GTK_IS_OBJECT (parent));
  g_return_if_fail ((gpointer)pad != (gpointer)parent);

  //g_print("set parent %s\n", gst_element_get_name(parent));

  pad->parent = parent;
}

/**
 * gst_pad_add_ghost_parent:
 * @pad: the pad to set the ghost parent 
 * @parent: the object to set the ghost parent to
 *
 * add a ghost parent object to a pad.
 */
void 
gst_pad_add_ghost_parent (GstPad *pad,
		          GstObject *parent) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (parent != NULL);
  g_return_if_fail (GTK_IS_OBJECT (parent));

  pad->ghostparents = g_list_prepend (pad->ghostparents, parent);
}


/**
 * gst_pad_remove_ghost_parent:
 * @pad: the pad to remove the ghost parent 
 * @parent: the object to remove the ghost parent from
 *
 * remove a ghost parent object from a pad.
 */
void 
gst_pad_remove_ghost_parent (GstPad *pad,
		             GstObject *parent) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (parent != NULL);
  g_return_if_fail (GTK_IS_OBJECT (parent));

  pad->ghostparents = g_list_remove (pad->ghostparents, parent);
}

/**
 * gst_pad_get_parent:
 * @pad: the pad to get the parent from
 *
 * get the parent object of this pad
 *
 * Returns: the parent object
 */
GstObject*
gst_pad_get_parent (GstPad *pad) 
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return pad->parent;
}

/**
 * gst_pad_get_ghost_parents:
 * @pad: the pad to get the ghost parents from
 *
 * get the ghost parents of this pad
 *
 * Returns: a list of ghost parent objects
 */
GList*
gst_pad_get_ghost_parents (GstPad *pad) 
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return pad->ghostparents;
}

/**
 * gst_pad_set_caps:
 * @pad: the pad to set the caps to
 * @caps: the caps to attach to this pad 
 *
 * set the capabilities of this pad
 */
void 
gst_pad_set_caps (GstPad *pad, 
		  GstCaps *caps) 
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  pad->caps = caps;
}
/**
 * gst_pad_get_caps:
 * @pad: the pad to get the capabilities from
 *
 * get the capabilities of this pad
 *
 * Return; the capabilities of this pad
 */
GstCaps * 
gst_pad_get_caps (GstPad *pad) 
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return pad->caps;
}

/**
 * gst_pad_get_peer:
 * @pad: the pad to get the peer from
 *
 * Get the peer pad of this pad
 *
 * Returns: the peer pad
 */
GstPad*
gst_pad_get_peer (GstPad *pad) 
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return pad->peer;
}

static void 
gst_pad_real_destroy (GtkObject *object) 
{
  GstPad *pad = GST_PAD (object);

//  g_print("in gst_pad_real_destroy()\n");

  if (pad->name)
    g_free (pad->name);
  g_list_free (pad->ghostparents);
}


/**
 * gst_pad_load_and_connect:
 * @parent: the parent XML node to read the description from
 * @element: the element that has the source pad
 * @elements: a hashtable with elements
 *
 * Read the pad definition from the XML node and connect the given pad
 * in element to a pad of an element in the hashtable.
 */
void 
gst_pad_load_and_connect (xmlNodePtr parent, 
		          GstObject *element, 
			  GHashTable *elements) 
{
  xmlNodePtr field = parent->childs;
  GstPad *pad = NULL, *targetpad;
  guchar *peer = NULL;
  gchar **split;
  GstElement *target;

  while (field) {
    if (!strcmp(field->name, "name")) {
      pad = gst_element_get_pad(GST_ELEMENT(element), xmlNodeGetContent(field));
    }
    else if (!strcmp(field->name, "peer")) {
      peer = g_strdup(xmlNodeGetContent(field));
    }
    field = field->next;
  }
  g_return_if_fail(pad != NULL);

  if (peer == NULL) return;

  split = g_strsplit(peer, ".", 2);

  g_return_if_fail(split[0] != NULL);
  g_return_if_fail(split[1] != NULL);

  target = (GstElement *)g_hash_table_lookup(elements, split[0]);

  if (target == NULL) goto cleanup;

  targetpad = gst_element_get_pad(target, split[1]);

  if (targetpad == NULL) goto cleanup;

  gst_pad_connect(pad, targetpad);

cleanup:
  g_strfreev(split);
}


/**
 * gst_pad_save_thyself:
 * @pad: the pad to save
 * @parent: the parent XML node to save the description in
 *
 * Saves the pad into an xml representation
 *
 * Returns: the xml representation of the pad
 */
xmlNodePtr 
gst_pad_save_thyself (GstPad *pad,
		      xmlNodePtr parent) 
{
  GstPad *peer;

  xmlNewChild(parent,NULL,"name",pad->name);
  if (pad->peer != NULL) {
    peer = pad->peer;
    // first check to see if the peer's parent's parent is the same
    //if (pad->parent->parent == peer->parent->parent)
      // we just save it off
      xmlNewChild(parent,NULL,"peer",g_strdup_printf("%s.%s",
                    GST_ELEMENT(peer->parent)->name,peer->name));
  } else
    xmlNewChild(parent,NULL,"peer","");

  return parent;
}

/**
 * gst_pad_ghost_save_thyself:
 * @pad: the pad to save
 * @bin: the bin
 * @parent: the parent XML node to save the description in
 *
 * Saves the ghost pad into an xml representation
 *
 * Returns: the xml representation of the pad
 */
xmlNodePtr 
gst_pad_ghost_save_thyself (GstPad *pad,
		            GstElement *bin,
			    xmlNodePtr parent) 
{
  xmlNodePtr self;

  self = xmlNewChild(parent,NULL,"ghostpad",NULL);
  xmlNewChild(self,NULL,"name",pad->name);
  xmlNewChild(self,NULL,"parent",GST_ELEMENT(pad->parent)->name);

  return self;
}

#ifndef gst_pad_push
void gst_pad_push(GstPad *pad,GstBuffer *buf) {
  DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  if (pad->peer->pushfunc) {
    DEBUG("calling pushfunc &%s of peer pad %s:%s\n",
          GST_DEBUG_FUNCPTR_NAME(pad->peer->pushfunc),GST_DEBUG_PAD_NAME(pad->peer));
    (pad->peer->pushfunc)(pad->peer,buf);
  } else
    DEBUG("no pushfunc\n");
}
#endif

#ifndef gst_pad_pull
GstBuffer *gst_pad_pull(GstPad *pad) {
  GstPad *peer = pad->peer;
  DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  if (peer->pullfunc) {
    DEBUG("calling pullfunc &%s (@%p) of peer pad %s:%s\n",
      GST_DEBUG_FUNCPTR_NAME(peer->pullfunc),&peer->pullfunc,GST_DEBUG_PAD_NAME(peer));
    return (peer->pullfunc)(peer);
  } else {
    DEBUG("no pullfunc for peer pad %s:%s at %p\n",GST_DEBUG_PAD_NAME(peer),&peer->pullfunc);
    return NULL;
  }
}
#endif

#ifndef gst_pad_pullregion
GstBuffer *gst_pad_pullregion(GstPad *pad,gulong offset,gulong size) {
  DEBUG_ENTER("(%s:%s,%ld,%ld)",GST_DEBUG_PAD_NAME(pad),offset,size);
  if (pad->peer->pullregionfunc) {
    DEBUG("calling pullregionfunc &%s of peer pad %s:%s\n",
          GST_DEBUG_FUNCPTR_NAME(pad->peer->pullregionfunc),GST_DEBUG_PAD_NAME(pad->peer));
    return (pad->peer->pullregionfunc)(pad->peer,offset,size);
  } else {
    DEBUG("no pullregionfunc\n");
    return NULL;
  }
}
#endif

/************************************************************************
 * 
 * templates
 *
 */

GstPadTemplate*   
gst_padtemplate_new (GstPadFactory *factory) 
{
  GstPadTemplate *new;
  GstPadFactoryEntry tag;
  gint i = 0;

  g_return_val_if_fail (factory != NULL, NULL);

  new = g_new0 (GstPadTemplate, 1);

  tag = (*factory)[i++];
  g_return_val_if_fail (tag != NULL, new);
  new->name_template = g_strdup ((gchar *)tag);

  tag = (*factory)[i++];
  new->direction = GPOINTER_TO_UINT (tag);

  tag = (*factory)[i++];
  new->presence = GPOINTER_TO_UINT (tag);

  new->caps = gst_caps_register ((GstCapsFactory *)&(*factory)[i]);

  return new;
}

GstPadTemplate*
gst_padtemplate_create (gchar *name_template,
		        GstPadDirection direction, GstPadPresence presence,
		        GstCaps *caps)
{
  GstPadTemplate *new;
  
  new = g_new0 (GstPadTemplate, 1);

  new->name_template = name_template;
  new->direction = direction;
  new->presence = presence;
  new->caps = caps;

  return new;
}


xmlNodePtr
gst_padtemplate_save_thyself (GstPadTemplate *pad, xmlNodePtr parent)
{
  xmlNodePtr subtree;

  xmlNewChild(parent,NULL,"nametemplate", pad->name_template);
  xmlNewChild(parent,NULL,"direction", (pad->direction == GST_PAD_SINK? "sink":"src"));
  xmlNewChild(parent,NULL,"presence", (pad->presence == GST_PAD_ALWAYS? "always":"sometimes"));
  subtree = xmlNewChild(parent,NULL,"caps", NULL);

  gst_caps_save_thyself (pad->caps, subtree);

  return parent;
}

GstPadTemplate*   
gst_padtemplate_load_thyself (xmlNodePtr parent)
{
  xmlNodePtr field = parent->childs;
  GstPadTemplate *factory = g_new0 (GstPadTemplate, 1);

  while (field) {
    if (!strcmp(field->name, "nametemplate")) {
      factory->name_template = g_strdup(xmlNodeGetContent(field));
    }
    if (!strcmp(field->name, "direction")) {
      gchar *value = xmlNodeGetContent(field);

      factory->direction = GST_PAD_UNKNOWN;
      if (!strcmp(value, "sink")) {
        factory->direction = GST_PAD_SINK;
      }
      else if (!strcmp(value, "src")) {
        factory->direction = GST_PAD_SRC;
      }
    }
    if (!strcmp(field->name, "presence")) {
      gchar *value = xmlNodeGetContent(field);

      if (!strcmp(value, "always")) {
        factory->presence = GST_PAD_ALWAYS;
      }
      else if (!strcmp(value, "sometimes")) {
        factory->presence = GST_PAD_SOMETIMES;
      }
    }
    else if (!strcmp(field->name, "caps")) {
      factory->caps = gst_caps_load_thyself (field);
    }
    field = field->next;
  }
  return factory;
}

