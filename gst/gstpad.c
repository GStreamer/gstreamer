/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpad.c: Pads for connecting elements together
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

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstpad.h"
#include "gstelement.h"
#include "gsttype.h"
#include "gstbin.h"


/***** Start with the base GstPad class *****/
static void		gst_pad_class_init		(GstPadClass *klass);
static void		gst_pad_init			(GstPad *pad);

static xmlNodePtr	gst_pad_save_thyself		(GstObject *object, xmlNodePtr parent);


static GstObject *pad_parent_class = NULL;

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
  pad_parent_class = gtk_type_class(GST_TYPE_OBJECT);
}

static void
gst_pad_init (GstPad *pad)
{
  pad->element_private = NULL;

  pad->padtemplate = NULL;
}



/***** Then do the Real Pad *****/
/* Pad signals and args */
enum {
  REAL_SET_ACTIVE,
  REAL_CAPS_CHANGED,
  /* FILL ME */
  REAL_LAST_SIGNAL
};

enum {
  REAL_ARG_0,
  REAL_ARG_ACTIVE,
  /* FILL ME */
};

static void		gst_real_pad_class_init		(GstRealPadClass *klass);
static void		gst_real_pad_init		(GstRealPad *pad);

static void		gst_real_pad_set_arg		(GtkObject *object,GtkArg *arg,guint id);
static void		gst_real_pad_get_arg		(GtkObject *object,GtkArg *arg,guint id);

static void		gst_real_pad_destroy		(GtkObject *object);

static void		gst_pad_push_func		(GstPad *pad, GstBuffer *buf);
static gboolean		gst_pad_eos_func                (GstPad *pad);


static GstPad *real_pad_parent_class = NULL;
static guint gst_real_pad_signals[REAL_LAST_SIGNAL] = { 0 };

GtkType
gst_real_pad_get_type(void) {
  static GtkType pad_type = 0;

  if (!pad_type) {
    static const GtkTypeInfo pad_info = {
      "GstRealPad",
      sizeof(GstRealPad),
      sizeof(GstRealPadClass),
      (GtkClassInitFunc)gst_real_pad_class_init,
      (GtkObjectInitFunc)gst_real_pad_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    pad_type = gtk_type_unique(GST_TYPE_PAD,&pad_info);
  }
  return pad_type;
}

static void
gst_real_pad_class_init (GstRealPadClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstObjectClass *gstobject_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;

  real_pad_parent_class = gtk_type_class(GST_TYPE_PAD);

  gst_real_pad_signals[REAL_SET_ACTIVE] =
    gtk_signal_new ("set_active", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstRealPadClass, set_active),
                    gtk_marshal_NONE__BOOL, GTK_TYPE_NONE, 1,
                    GTK_TYPE_BOOL);
  gst_real_pad_signals[REAL_CAPS_CHANGED] =
    gtk_signal_new ("caps_changed", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstRealPadClass, caps_changed),
                    gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
                    GTK_TYPE_POINTER);
  gtk_object_class_add_signals (gtkobject_class, gst_real_pad_signals, REAL_LAST_SIGNAL);

  gtk_object_add_arg_type ("GstRealPad::active", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, REAL_ARG_ACTIVE);

  gtkobject_class->destroy = gst_real_pad_destroy;
  gtkobject_class->set_arg = gst_real_pad_set_arg;
  gtkobject_class->get_arg = gst_real_pad_get_arg;

  gstobject_class->save_thyself = gst_pad_save_thyself;
  gstobject_class->path_string_separator = ".";
}

static void
gst_real_pad_init (GstRealPad *pad)
{
  pad->direction = GST_PAD_UNKNOWN;
  pad->peer = NULL;

  pad->chainfunc = NULL;
  pad->getfunc = NULL;
  pad->getregionfunc = NULL;
  pad->qosfunc = NULL;
  pad->eosfunc = gst_pad_eos_func;

  pad->pushfunc = GST_DEBUG_FUNCPTR(gst_pad_push_func);
  pad->pullfunc = NULL;
  pad->pullregionfunc = NULL;

  pad->ghostpads = NULL;
  pad->caps = NULL;
}

static void
gst_real_pad_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  g_return_if_fail(GST_IS_PAD(object));

  switch (id) {
    case REAL_ARG_ACTIVE:
      if (GTK_VALUE_BOOL(*arg)) {
        gst_info("gstpad: activating pad\n");
        GST_FLAG_UNSET(object,GST_PAD_DISABLED);
      } else {
        gst_info("gstpad: de-activating pad\n");
        GST_FLAG_SET(object,GST_PAD_DISABLED);
      }
      gtk_signal_emit(GTK_OBJECT(object), gst_real_pad_signals[REAL_SET_ACTIVE],
                      ! GST_FLAG_IS_SET(object,GST_PAD_DISABLED));
      break;
    default:
      break;
  }
}

static void
gst_real_pad_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_PAD (object));

  switch (id) {
    case REAL_ARG_ACTIVE:
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
  GstRealPad *pad;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (direction != GST_PAD_UNKNOWN, NULL);

  pad = gtk_type_new (gst_real_pad_get_type ());
  gst_object_set_name (GST_OBJECT (pad), name);
  GST_RPAD_DIRECTION(pad) = direction;

  return GST_PAD(pad);
}

/**
 * gst_pad_new_from_template:
 * @templ: the pad template to use
 * @name: the name of the element
 *
 * Create a new pad with given name from the given template.
 *
 * Returns: new pad
 */
GstPad*
gst_pad_new_from_template (GstPadTemplate *templ,
		           gchar *name)
{
  GstPad *pad;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (templ != NULL, NULL);

  pad = gst_pad_new (name, templ->direction);
  GST_PAD_CAPS(pad) = templ->caps;
  GST_PAD_PADTEMPLATE(pad) = templ;

  return pad;
}

/**
 * gst_pad_get_direction:
 * @pad: the Pad to get the direction from
 *
 * Get the direction of the pad.
 *
 * Returns: the direction of the pad
 */
GstPadDirection
gst_pad_get_direction (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, GST_PAD_UNKNOWN);
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_UNKNOWN);

  return GST_PAD_DIRECTION(pad);
}

/**
 * gst_pad_set_name:
 * @pad: the pad to set the name of
 * @name: the name of the pad
 *
 * Set the name of a pad.
 */
void
gst_pad_set_name (GstPad *pad,
		  const gchar *name)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  gst_object_set_name (GST_OBJECT (pad), name);
}

/**
 * gst_pad_get_name:
 * @pad: the pad to get the name of
 *
 * Get the name of a pad.
 *
 * Returns: the name of the pad, don't free.
 */
const gchar*
gst_pad_get_name (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_OBJECT_NAME (pad);
}

/**
 * gst_pad_set_chain_function:
 * @pad: the pad to set the chain function for
 * @chain: the chain function
 *
 * Set the given chain function for the pad.
 */
void gst_pad_set_chain_function (GstPad *pad,
		                 GstPadChainFunction chain)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_CHAINFUNC(pad) = chain;
  GST_DEBUG (0,"chainfunc for %s:%s(@%p) at %p is set to %p\n",
             GST_DEBUG_PAD_NAME(pad),pad,&GST_RPAD_CHAINFUNC(pad),chain);
}

/**
 * gst_pad_set_get_function:
 * @pad: the pad to set the get function for
 * @get: the get function
 *
 * Set the given get function for the pad.
 */
void
gst_pad_set_get_function (GstPad *pad,
			  GstPadGetFunction get)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_GETFUNC(pad) = get;
  GST_DEBUG (0,"getfunc for %s:%s(@%p) at %p is set to %p\n",
             GST_DEBUG_PAD_NAME(pad),pad,&GST_RPAD_GETFUNC(pad),get);
}

/**
 * gst_pad_set_getregion_function:
 * @pad: the pad to set the getregion function for
 * @getregion: the getregion function
 *
 * Set the given getregion function for the pad.
 */
void
gst_pad_set_getregion_function (GstPad *pad,
				GstPadGetRegionFunction getregion)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_GETREGIONFUNC(pad) = getregion;
  GST_DEBUG (0,"getregionfunc for %s:%s(@%p) at %p is set to %p\n",
             GST_DEBUG_PAD_NAME(pad),pad,&GST_RPAD_GETREGIONFUNC(pad),getregion);
}

/**
 * gst_pad_set_qos_function:
 * @pad: the pad to set the qos function for
 * @qos: the qos function
 *
 * Set the given qos function for the pad.
 */
void
gst_pad_set_qos_function (GstPad *pad,
		          GstPadQoSFunction qos)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_QOSFUNC(pad) = qos;
  GST_DEBUG (0,"qosfunc for %s:%s(@%p) at %p is set to %p\n",
             GST_DEBUG_PAD_NAME(pad),pad,&GST_RPAD_QOSFUNC(pad),qos);
}

/**
 * gst_pad_set_eos_function:
 * @pad: the pad to set the eos function for
 * @eos: the eos function
 *
 * Set the given EOS function for the pad.
 */
void
gst_pad_set_eos_function (GstPad *pad,
		          GstPadEOSFunction eos)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_EOSFUNC(pad) = eos;
  GST_DEBUG (0,"eosfunc for %s:%s(@%p) at %p is set to %p\n",
             GST_DEBUG_PAD_NAME(pad),pad,&GST_RPAD_EOSFUNC(pad),eos);
}



static void
gst_pad_push_func(GstPad *pad, GstBuffer *buf)
{
  if (GST_RPAD_CHAINFUNC(GST_RPAD_PEER(pad)) != NULL) {
    GST_DEBUG (0,"calling chain function\n");
    (GST_RPAD_CHAINFUNC(GST_RPAD_PEER(pad)))(pad,buf);
  } else {
    GST_DEBUG (0,"got a problem here: default pad_push handler in place, no chain function\n");
  }
}


/**
 * gst_pad_handle_qos:
 * @pad: the pad to handle the QoS message
 * @qos_message: the QoS message to handle
 *
 * Pass the qos message downstream.
 */
void
gst_pad_handle_qos(GstPad *pad,
	           glong qos_message)
{
  GstElement *element;
  GList *pads;
  GstPad *target_pad;

  GST_DEBUG (0,"gst_pad_handle_qos(\"%s\",%08ld)\n", GST_OBJECT_NAME (GST_PAD_PARENT (pad)),qos_message);

  if (GST_RPAD_QOSFUNC(pad)) {
    (GST_RPAD_QOSFUNC(pad)) (pad,qos_message);
  } else {
    element = GST_ELEMENT (GST_PAD_PARENT(GST_RPAD_PEER(pad)));

    pads = element->pads;
    GST_DEBUG (0,"gst_pad_handle_qos recurse(\"%s\",%08ld)\n", GST_ELEMENT_NAME (element), qos_message);
    while (pads) {
      target_pad = GST_PAD (pads->data);
      if (GST_RPAD_DIRECTION(target_pad) == GST_PAD_SINK) {
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
 * Disconnects the source pad from the sink pad.
 */
void
gst_pad_disconnect (GstPad *srcpad,
		    GstPad *sinkpad)
{
  GstRealPad *realsrc, *realsink;

  /* generic checks */
  g_return_if_fail (srcpad != NULL);
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (sinkpad != NULL);
  g_return_if_fail (GST_IS_PAD (sinkpad));

  // now we need to deal with the real/ghost stuff
  realsrc = GST_PAD_REALIZE(srcpad);
  realsink = GST_PAD_REALIZE(sinkpad);

  g_return_if_fail (GST_RPAD_PEER(realsrc) != NULL);
  g_return_if_fail (GST_RPAD_PEER(realsink) != NULL);

  g_return_if_fail ((GST_RPAD_DIRECTION(realsrc) == GST_PAD_SRC) &&
                    (GST_RPAD_DIRECTION(realsink) == GST_PAD_SINK));

  /* first clear peers */
  GST_RPAD_PEER(realsrc) = NULL;
  GST_RPAD_PEER(realsink) = NULL;

  GST_INFO (GST_CAT_ELEMENT_PADS, "disconnected %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME(srcpad), GST_DEBUG_PAD_NAME(sinkpad));
}

/**
 * gst_pad_connect:
 * @srcpad: the source pad to connect
 * @sinkpad: the sink pad to connect
 *
 * Connects the source pad to the sink pad.
 */
void
gst_pad_connect (GstPad *srcpad,
		 GstPad *sinkpad)
{
  GstRealPad *realsrc, *realsink;
  GstRealPad *temppad;

  /* generic checks */
  g_return_if_fail(srcpad != NULL);
  g_return_if_fail(GST_IS_PAD(srcpad));
  g_return_if_fail(sinkpad != NULL);
  g_return_if_fail(GST_IS_PAD(sinkpad));

  // now we need to deal with the real/ghost stuff
  realsrc = GST_PAD_REALIZE(srcpad);
  realsink = GST_PAD_REALIZE(sinkpad);

  g_return_if_fail(GST_RPAD_PEER(realsrc) == NULL);
  g_return_if_fail(GST_RPAD_PEER(realsink) == NULL);

  /* check for reversed directions and swap if necessary */
  if ((GST_RPAD_DIRECTION(realsrc) == GST_PAD_SINK) &&
      (GST_RPAD_DIRECTION(realsink) == GST_PAD_SRC)) {
    temppad = realsrc;
    realsrc = realsink;
    realsink = temppad;
  }
  g_return_if_fail((GST_RPAD_DIRECTION(realsrc) == GST_PAD_SRC) &&
                   (GST_RPAD_DIRECTION(realsink) == GST_PAD_SINK));

  if (!gst_pad_check_compatibility (srcpad, sinkpad)) {
    g_warning ("gstpad: connecting incompatible pads (%s:%s) and (%s:%s)\n",
                       GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
  }
  else {
    GST_DEBUG (0,"gstpad: connecting compatible pads (%s:%s) and (%s:%s)\n",
                       GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
  }

  /* first set peers */
  GST_RPAD_PEER(realsrc) = realsink;
  GST_RPAD_PEER(realsink) = realsrc;

  /* set the connected flag */
  /* FIXME: set connected flag */

  GST_INFO (GST_CAT_ELEMENT_PADS, "connected %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME(srcpad), GST_DEBUG_PAD_NAME(sinkpad));
}

/**
 * gst_pad_set_parent:
 * @pad: the pad to set the parent
 * @parent: the object to set the parent to
 *
 * Sets the parent object of a pad.
 */
void
gst_pad_set_parent (GstPad *pad,
                    GstObject *parent)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_PARENT (pad) == NULL);
  g_return_if_fail (parent != NULL);
  g_return_if_fail (GTK_IS_OBJECT (parent));
  g_return_if_fail ((gpointer)pad != (gpointer)parent);

  gst_object_set_parent (GST_OBJECT (pad), parent);
}

/**
 * gst_pad_get_parent:
 * @pad: the pad to get the parent from
 *
 * Get the parent object of this pad.
 *
 * Returns: the parent object
 */
GstObject*
gst_pad_get_parent (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_OBJECT_PARENT (pad);
}

/**
 * gst_pad_add_ghost_pad:
 * @pad: the pad to set the ghost parent
 * @ghostpad: the ghost pad to add
 *
 * Add a ghost pad to a pad.
 */
void
gst_pad_add_ghost_pad (GstPad *pad,
		       GstPad *ghostpad)
{
  GstRealPad *realpad;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (ghostpad != NULL);
  g_return_if_fail (GST_IS_GHOST_PAD (ghostpad));

  realpad = GST_PAD_REALIZE(pad);

  realpad->ghostpads = g_list_prepend (realpad->ghostpads, ghostpad);
}


/**
 * gst_pad_remove_ghost_pad:
 * @pad: the pad to remove the ghost parent
 * @ghostpad: the ghost pad to remove from the pad
 *
 * Remove a ghost pad from a pad.
 */
void
gst_pad_remove_ghost_pad (GstPad *pad,
		          GstPad *ghostpad)
{
  GstRealPad *realpad;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (ghostpad != NULL);
  g_return_if_fail (GST_IS_GHOST_PAD (ghostpad));

  realpad = GST_PAD_REALIZE (pad);

  realpad->ghostpads = g_list_remove (realpad->ghostpads, ghostpad);
}

/**
 * gst_pad_get_ghost_pad_list:
 * @pad: the pad to get the ghost parents from
 *
 * Get the ghost parents of this pad.
 *
 * Returns: a GList of ghost pads
 */
GList*
gst_pad_get_ghost_pad_list (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_REALIZE(pad)->ghostpads;
}

/**
 * gst_pad_set_caps_list:
 * @pad: the pad to set the caps to
 * @caps: a GList of the capabilities to attach to this pad
 *
 * Set the capabilities of this pad.
 */
void
gst_pad_set_caps_list (GstPad *pad,
                       GList *caps)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));		// NOTE this restriction

  GST_PAD_CAPS(pad) = caps;
}

/**
 * gst_pad_get_caps_list:
 * @pad: the pad to get the capabilities from
 *
 * Get the capabilities of this pad.
 *
 * Returns: a list of the capabilities of this pad
 */
GList *
gst_pad_get_caps_list (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_CAPS(pad);
}

/**
 * gst_pad_get_caps_by_name:
 * @pad: the pad to get the capabilities from
 * @name: the name of the capability to get
 *
 * Get the capabilities  with the given name from this pad.
 *
 * Returns: a capability or NULL if not found
 */
GstCaps *
gst_pad_get_caps_by_name (GstPad *pad, gchar *name)
{
  GList *caps;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  caps = GST_PAD_CAPS(pad);

  while (caps) {
    GstCaps *cap = (GstCaps *)caps->data;

    if (!strcmp (cap->name, name))
      return cap;

    caps = g_list_next (caps);
  }

  return NULL;
}

/**
 * gst_pad_check_compatibility:
 * @srcpad: the srcpad to check
 * @sinkpad: the sinkpad to check against
 *
 * Check if two pads have compatible capabilities.
 *
 * Returns: TRUE if they are compatible or the capabilities
 * could not be checked
 */
gboolean
gst_pad_check_compatibility (GstPad *srcpad, GstPad *sinkpad)
{
  GstRealPad *realsrc, *realsink;

  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  realsrc = GST_PAD_REALIZE(srcpad);
  realsink = GST_PAD_REALIZE(sinkpad);

  if (GST_RPAD_CAPS(realsrc) && GST_RPAD_CAPS(realsink)) {
    if (!gst_caps_list_check_compatibility (GST_RPAD_CAPS(realsrc), GST_RPAD_CAPS(realsink))) {
      return FALSE;
    }
    else {
      return TRUE;
    }
  }
  else {
    GST_DEBUG (0,"gstpad: could not check capabilities of pads (%s:%s) and (%s:%s)\n",
		    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
    return TRUE;
  }
}

/**
 * gst_pad_get_peer:
 * @pad: the pad to get the peer from
 *
 * Get the peer pad of this pad.
 *
 * Returns: the peer pad
 */
GstPad*
gst_pad_get_peer (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD(GST_PAD_PEER(pad));
}

// FIXME this needs to be rethought soon
static void
gst_real_pad_destroy (GtkObject *object)
{
  GstPad *pad = GST_PAD (object);

//  g_print("in gst_pad_real_destroy()\n");

  g_list_free (GST_REAL_PAD(pad)->ghostpads);
}


/**
 * gst_pad_load_and_connect:
 * @self: the XML node to read the description from
 * @parent: the element that has the pad
 *
 * Read the pad definition from the XML node and connect the given pad
 * in element to a pad of an element up in the hierarchy.
 */
void
gst_pad_load_and_connect (xmlNodePtr self,
		          GstObject *parent)
{
  xmlNodePtr field = self->childs;
  GstPad *pad = NULL, *targetpad;
  guchar *peer = NULL;
  gchar **split;
  GstElement *target;
  GstObject *grandparent;

  while (field) {
    if (!strcmp (field->name, "name")) {
      pad = gst_element_get_pad (GST_ELEMENT (parent), xmlNodeGetContent (field));
    }
    else if (!strcmp(field->name, "peer")) {
      peer = g_strdup (xmlNodeGetContent (field));
    }
    field = field->next;
  }
  g_return_if_fail (pad != NULL);

  if (peer == NULL) return;

  split = g_strsplit (peer, ".", 2);

  g_return_if_fail (split[0] != NULL);
  g_return_if_fail (split[1] != NULL);

  grandparent = gst_object_get_parent (parent);

  if (grandparent && GST_IS_BIN (grandparent)) {
    target = gst_bin_get_by_name_recurse_up (GST_BIN (grandparent), split[0]);
  }
  else
    goto cleanup;

  if (target == NULL) goto cleanup;

  targetpad = gst_element_get_pad (target, split[1]);

  if (targetpad == NULL) goto cleanup;

  gst_pad_connect (pad, targetpad);

cleanup:
  g_strfreev (split);
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
static xmlNodePtr
gst_pad_save_thyself (GstObject *object,
		      xmlNodePtr parent)
{
  GstRealPad *realpad;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_REAL_PAD (object), NULL);

  realpad = GST_REAL_PAD(object);

  xmlNewChild(parent,NULL,"name", GST_PAD_NAME (realpad));
  if (GST_RPAD_PEER(realpad) != NULL) {
    peer = GST_PAD(GST_RPAD_PEER(realpad));
    // first check to see if the peer's parent's parent is the same
    // we just save it off
    xmlNewChild(parent,NULL,"peer",g_strdup_printf("%s.%s",
                    GST_OBJECT_NAME (GST_PAD_PARENT (peer)), GST_PAD_NAME (peer)));
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
 * Saves the ghost pad into an xml representation.
 *
 * Returns: the xml representation of the pad
 */
xmlNodePtr
gst_pad_ghost_save_thyself (GstPad *pad,
		            GstElement *bin,
			    xmlNodePtr parent)
{
  xmlNodePtr self;

  g_return_val_if_fail (GST_IS_GHOST_PAD (pad), NULL);

  self = xmlNewChild(parent,NULL,"ghostpad",NULL);
  xmlNewChild(self,NULL,"name", GST_PAD_NAME (pad));
  xmlNewChild(self,NULL,"parent", GST_OBJECT_NAME (GST_PAD_PARENT (pad)));

  // FIXME FIXME FIXME!

  return self;
}

#ifndef gst_pad_push
void gst_pad_push(GstPad *pad,GstBuffer *buf) {
  GstRealPad *peer = GST_RPAD_PEER(pad);
  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  if (peer->pushfunc) {
    GST_DEBUG (0,"calling pushfunc &%s of peer pad %s:%s\n",
          GST_DEBUG_FUNCPTR_NAME(peer->pushfunc),GST_DEBUG_PAD_NAME(((GstPad*)peer)));
    (peer->pushfunc)(((GstPad*)peer),buf);
  } else
    GST_DEBUG (0,"no pushfunc\n");
}
#endif

#ifndef gst_pad_pull
GstBuffer *gst_pad_pull(GstPad *pad) {
  GstRealPad *peer = GST_RPAD_PEER(pad);
  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  if (peer->pullfunc) {
    GST_DEBUG (0,"calling pullfunc &%s (@%p) of peer pad %s:%s\n",
      GST_DEBUG_FUNCPTR_NAME(peer->pullfunc),&peer->pullfunc,GST_DEBUG_PAD_NAME(((GstPad*)peer)));
    return (peer->pullfunc)(((GstPad*)peer));
  } else {
    GST_DEBUG (0,"no pullfunc for peer pad %s:%s at %p\n",GST_DEBUG_PAD_NAME(((GstPad*)peer)),&peer->pullfunc);
    return NULL;
  }
}
#endif

#ifndef gst_pad_pullregion
GstBuffer *gst_pad_pullregion(GstPad *pad,gulong offset,gulong size) {
  GstRealPad *peer = GST_RPAD_PEER(pad);
  GST_DEBUG_ENTER("(%s:%s,%ld,%ld)",GST_DEBUG_PAD_NAME(pad),offset,size);
  if (peer->pullregionfunc) {
    GST_DEBUG (0,"calling pullregionfunc &%s of peer pad %s:%s\n",
          GST_DEBUG_FUNCPTR_NAME(peer->pullregionfunc),GST_DEBUG_PAD_NAME(((GstPad*)peer)));
    return (peer->pullregionfunc)(((GstPad*)peer),offset,size);
  } else {
    GST_DEBUG (0,"no pullregionfunc\n");
    return NULL;
  }
}
#endif

/************************************************************************
 *
 * templates
 *
 */

/**
 * gst_padtemplate_new:
 * @factory: the padfactory to use
 *
 * Creates a new padtemplate from the factory.
 *
 * Returns: the new padtemplate
 */
GstPadTemplate*
gst_padtemplate_new (GstPadFactory *factory)
{
  GstPadTemplate *new;
  GstPadFactoryEntry tag;
  gint i = 0;
  guint counter = 0;

  g_return_val_if_fail (factory != NULL, NULL);

  new = g_new0 (GstPadTemplate, 1);

  tag = (*factory)[i++];
  g_return_val_if_fail (tag != NULL, new);
  new->name_template = g_strdup ((gchar *)tag);

  tag = (*factory)[i++];
  new->direction = GPOINTER_TO_UINT (tag);

  tag = (*factory)[i++];
  new->presence = GPOINTER_TO_UINT (tag);

  tag = (*factory)[i++];

  while (GPOINTER_TO_INT (tag) == 1) {
    new->caps = g_list_append (new->caps, gst_caps_register_count ((GstCapsFactory *)&(*factory)[i], &counter));
    i+=counter;
    tag = (*factory)[i++];
  }

  return new;
}

/**
 * gst_padtemplate_create:
 * @name_template: the name template
 * @direction: the direction for the template
 * @presence: the presence of the pad
 * @caps: a list of capabilities for the template
 *
 * Creates a new padtemplate from the given arguments.
 *
 * Returns: the new padtemplate
 */
GstPadTemplate*
gst_padtemplate_create (gchar *name_template,
		        GstPadDirection direction, GstPadPresence presence,
		        GList *caps)
{
  GstPadTemplate *new;

  new = g_new0 (GstPadTemplate, 1);

  new->name_template = name_template;
  new->direction = direction;
  new->presence = presence;
  new->caps = caps;

  return new;
}


/**
 * gst_padtemplate_save_thyself:
 * @templ: the padtemplate to save
 * @parent: the parent XML tree
 *
 * Saves the padtemplate into XML.
 *
 * Returns: the new XML tree
 */
xmlNodePtr
gst_padtemplate_save_thyself (GstPadTemplate *templ, xmlNodePtr parent)
{
  xmlNodePtr subtree;
  GList *caps;
  guchar *presence;

  xmlNewChild(parent,NULL,"nametemplate", templ->name_template);
  xmlNewChild(parent,NULL,"direction", (templ->direction == GST_PAD_SINK? "sink":"src"));

  switch (templ->presence) {
    case GST_PAD_ALWAYS:
      presence = "always";
      break;
    case GST_PAD_SOMETIMES:
      presence = "sometimes";
      break;
    case GST_PAD_REQUEST:
      presence = "request";
      break;
    default:
      presence = "unknown";
      break;
  }
  xmlNewChild(parent,NULL,"presence", presence);

  caps = templ->caps;
  while (caps) {
    GstCaps *cap = (GstCaps *)caps->data;

    subtree = xmlNewChild (parent, NULL, "caps", NULL);
    gst_caps_save_thyself (cap, subtree);

    caps = g_list_next (caps);
  }

  return parent;
}

/**
 * gst_padtemplate_load_thyself:
 * @parent: the source XML tree
 *
 * Loads a padtemplate from the XML tree.
 *
 * Returns: the new padtemplate
 */
GstPadTemplate*
gst_padtemplate_load_thyself (xmlNodePtr parent)
{
  xmlNodePtr field = parent->childs;
  GstPadTemplate *factory = g_new0 (GstPadTemplate, 1);

  while (field) {
    if (!strcmp(field->name, "nametemplate")) {
      factory->name_template = xmlNodeGetContent(field);
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
      g_free (value);
    }
    if (!strcmp(field->name, "presence")) {
      gchar *value = xmlNodeGetContent(field);

      if (!strcmp(value, "always")) {
        factory->presence = GST_PAD_ALWAYS;
      }
      else if (!strcmp(value, "sometimes")) {
        factory->presence = GST_PAD_SOMETIMES;
      }
      else if (!strcmp(value, "request")) {
        factory->presence = GST_PAD_REQUEST;
      }
      g_free (value);
    }
    else if (!strcmp(field->name, "caps")) {
      factory->caps = g_list_append(factory->caps, gst_caps_load_thyself (field));
    }
    field = field->next;
  }
  return factory;
}


static gboolean
gst_pad_eos_func(GstPad *pad)
{
  GstElement *element;
  GList *pads;
  GstPad *srcpad;
  gboolean result = TRUE, success;

  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_REAL_PAD(pad), FALSE);	// NOTE the restriction

  GST_INFO (GST_CAT_PADS,"attempting to set EOS on sink pad %s:%s",GST_DEBUG_PAD_NAME(pad));

  element = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (pad)));
//  g_return_val_if_fail (element != NULL, FALSE);
//  g_return_val_if_fail (GST_IS_ELEMENT(element), FALSE);

  pads = gst_element_get_pad_list(element);
  while (pads) {
    srcpad = GST_PAD(pads->data);
    pads = g_list_next(pads);

    if (gst_pad_get_direction(srcpad) == GST_PAD_SRC) {
      result = gst_pad_eos(GST_REAL_PAD(srcpad));
      if (result == FALSE) success = FALSE;
    }
  }

  if (result == FALSE) return FALSE;

  GST_INFO (GST_CAT_PADS,"set EOS on sink pad %s:%s",GST_DEBUG_PAD_NAME(pad));
  GST_FLAG_SET (pad, GST_PAD_EOS);

  return TRUE;
}

/**
 * gst_pad_set_eos:
 * @pad: the pad to set to eos
 *
 * Sets the given pad to the EOS state.
 *
 * Returns: TRUE if it succeeded
 */
gboolean
gst_pad_set_eos(GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_REAL_PAD(pad), FALSE);		// NOTE the restriction
  g_return_val_if_fail (GST_PAD_CONNECTED(pad), FALSE);

  GST_INFO (GST_CAT_PADS,"attempting to set EOS on src pad %s:%s",GST_DEBUG_PAD_NAME(pad));

  if (!gst_pad_eos(GST_REAL_PAD(pad))) {
    return FALSE;
  }

  GST_INFO (GST_CAT_PADS,"set EOS on src pad %s:%s",GST_DEBUG_PAD_NAME(pad));
  GST_FLAG_SET (pad, GST_PAD_EOS);

  gst_element_signal_eos (GST_ELEMENT (GST_PAD_PARENT (pad)));

  return TRUE;
}

/*
GstPad *
gst_pad_select(GstPad *nextpad, ...) {
  va_list args;
  GstPad *pad;
  GSList *pads = NULL;

  // construct the list of pads
  va_start (args, nextpad);
  while ((pad = va_arg (args, GstPad*)))
    pads = g_slist_prepend (pads, pad);
  va_end (args);

  // now switch to the nextpad
*/


/**
 * gst_pad_set_element_private:
 * @pad: the pad to set the private data to
 * @priv: The private data to attach to the pad
 *
 * Set the given private data pointer to the pad. This
 * function can only be used by the element that own the
 * pad.
 */
void
gst_pad_set_element_private (GstPad *pad, gpointer priv)
{
  pad->element_private = priv;
}

/**
 * gst_pad_get_element_private:
 * @pad: the pad to get the private data of
 *
 * Get the private data of a pad. The private data can
 * only be set by the parent element of this pad.
 *
 * Returns: a pointer to the private data.
 */
gpointer
gst_pad_get_element_private (GstPad *pad)
{
  return pad->element_private;
}









/***** ghost pads *****/

static void     gst_ghost_pad_class_init         (GstGhostPadClass *klass);
static void     gst_ghost_pad_init               (GstGhostPad *pad);

static GstPad *ghost_pad_parent_class = NULL;
//static guint gst_ghost_pad_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_ghost_pad_get_type(void) {
  static GtkType pad_type = 0;

  if (!pad_type) {
    static const GtkTypeInfo pad_info = {
      "GstGhostPad",
      sizeof(GstGhostPad),
      sizeof(GstGhostPadClass),
      (GtkClassInitFunc)gst_ghost_pad_class_init,
      (GtkObjectInitFunc)gst_ghost_pad_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    pad_type = gtk_type_unique(GST_TYPE_PAD,&pad_info);
  }
  return pad_type;
}

static void
gst_ghost_pad_class_init (GstGhostPadClass *klass)
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  ghost_pad_parent_class = gtk_type_class(GST_TYPE_PAD);
}

static void
gst_ghost_pad_init (GstGhostPad *pad)
{
  pad->realpad = NULL;
}

/**
 * gst_ghost_pad_new:
 * @name: name of the new ghost pad
 * @pad: the pad to create a ghost pad of
 *
 * Create a new ghost pad associated with the given pad.
 *
 * Returns: new ghost pad
 */
GstPad*
gst_ghost_pad_new (gchar *name,
                   GstPad *pad)
{
  GstGhostPad *ghostpad;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD(pad), NULL);

  ghostpad = gtk_type_new (gst_ghost_pad_get_type ());
  gst_pad_set_name (GST_PAD (ghostpad), name);
  GST_GPAD_REALPAD(ghostpad) = GST_PAD_REALIZE(pad);

  // add ourselves to the real pad's list of ghostpads
  gst_pad_add_ghost_pad (pad, GST_PAD(ghostpad));

  // FIXME need to ref the real pad here... ?

  GST_DEBUG(0,"created ghost pad \"%s\"\n",name);

  return GST_PAD(ghostpad);
}
