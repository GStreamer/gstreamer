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
#include "gstscheduler.h"


/***** Start with the base GstPad class *****/
static void		gst_pad_class_init		(GstPadClass *klass);
static void		gst_pad_init			(GstPad *pad);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr	gst_pad_save_thyself		(GstObject *object, xmlNodePtr parent);
#endif

static GstObject *pad_parent_class = NULL;

GType
gst_pad_get_type(void) {
  static GType pad_type = 0;

  if (!pad_type) {
    static const GTypeInfo pad_info = {
      sizeof(GstPadClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_pad_class_init,
      NULL,
      NULL,
      sizeof(GstPad),
      32,
      (GInstanceInitFunc)gst_pad_init,
    };
    pad_type = g_type_register_static(GST_TYPE_OBJECT, "GstPad", &pad_info, 0);
  }
  return pad_type;
}

static void
gst_pad_class_init (GstPadClass *klass)
{
  pad_parent_class = g_type_class_ref(GST_TYPE_OBJECT);
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
  REAL_CAPS_NEGO_FAILED,
  REAL_CONNECTED,
  REAL_DISCONNECTED,
  /* FILL ME */
  REAL_LAST_SIGNAL
};

enum {
  REAL_ARG_0,
  REAL_ARG_ACTIVE,
  /* FILL ME */
};

static void	gst_real_pad_class_init		(GstRealPadClass *klass);
static void	gst_real_pad_init		(GstRealPad *pad);

static void	gst_real_pad_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_real_pad_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_real_pad_destroy		(GObject *object);

static void	gst_pad_push_func		(GstPad *pad, GstBuffer *buf);


static GstPad *real_pad_parent_class = NULL;
static guint gst_real_pad_signals[REAL_LAST_SIGNAL] = { 0 };

GType
gst_real_pad_get_type(void) {
  static GType pad_type = 0;

  if (!pad_type) {
    static const GTypeInfo pad_info = {
      sizeof(GstRealPadClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_real_pad_class_init,
      NULL,
      NULL,
      sizeof(GstRealPad),
      32,
      (GInstanceInitFunc)gst_real_pad_init,
    };
    pad_type = g_type_register_static(GST_TYPE_PAD, "GstRealPad", &pad_info, 0);
  }
  return pad_type;
}

static void
gst_real_pad_class_init (GstRealPadClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;

  real_pad_parent_class = g_type_class_ref(GST_TYPE_PAD);

// FIXME!
//  gobject_class->destroy  = GST_DEBUG_FUNCPTR(gst_real_pad_destroy);
  gobject_class->set_property  = GST_DEBUG_FUNCPTR(gst_real_pad_set_property);
  gobject_class->get_property  = GST_DEBUG_FUNCPTR(gst_real_pad_get_property);

  gst_real_pad_signals[REAL_SET_ACTIVE] =
    g_signal_newc ("set_active", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, set_active), NULL, NULL,
                    g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1,
                    G_TYPE_BOOLEAN);
  gst_real_pad_signals[REAL_CAPS_CHANGED] =
    g_signal_newc ("caps_changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, caps_changed), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_real_pad_signals[REAL_CAPS_NEGO_FAILED] =
    g_signal_newc ("caps_nego_failed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, caps_nego_failed), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_real_pad_signals[REAL_CONNECTED] =
    g_signal_newc ("connected", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, connected), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_real_pad_signals[REAL_DISCONNECTED] =
    g_signal_newc ("disconnected", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, disconnected), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);

//  gtk_object_add_arg_type ("GstRealPad::active", G_TYPE_BOOLEAN,
//                           GTK_ARG_READWRITE, REAL_ARG_ACTIVE);
  g_object_class_install_property (G_OBJECT_CLASS(klass), REAL_ARG_ACTIVE,
    g_param_spec_boolean("active","Active","Whether the pad is active.",
                         TRUE,G_PARAM_READWRITE));

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR(gst_pad_save_thyself);
#endif
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

  pad->chainhandler = GST_DEBUG_FUNCPTR(gst_pad_push_func);
  pad->gethandler = NULL;
  pad->pullregionfunc = NULL;

  pad->bufferpoolfunc = NULL;
  pad->ghostpads = NULL;
  pad->caps = NULL;
}

static void
gst_real_pad_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GST_IS_PAD(object));

  switch (prop_id) {
    case REAL_ARG_ACTIVE:
      if (g_value_get_boolean(value)) {
        GST_DEBUG(GST_CAT_PADS,"activating pad %s:%s\n",GST_DEBUG_PAD_NAME(object));
        GST_FLAG_UNSET(object,GST_PAD_DISABLED);
      } else {
        GST_DEBUG(GST_CAT_PADS,"de-activating pad %s:%s\n",GST_DEBUG_PAD_NAME(object));
        GST_FLAG_SET(object,GST_PAD_DISABLED);
      }
      g_signal_emit(G_OBJECT(object), gst_real_pad_signals[REAL_SET_ACTIVE], 0,
                      ! GST_FLAG_IS_SET(object,GST_PAD_DISABLED));
      break;
    default:
      break;
  }
}

static void
gst_real_pad_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case REAL_ARG_ACTIVE:
      g_value_set_boolean(value, ! GST_FLAG_IS_SET (object, GST_PAD_DISABLED) );
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

  pad = g_object_new (gst_real_pad_get_type (), NULL);
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
  gst_object_ref (GST_OBJECT (templ));
  gst_object_sink (GST_OBJECT (templ));
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
  GST_DEBUG (GST_CAT_PADS,"chainfunc for %s:%s set to %s\n",
             GST_DEBUG_PAD_NAME(pad),GST_DEBUG_FUNCPTR_NAME(chain));
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
  GST_DEBUG (GST_CAT_PADS,"getfunc for %s:%s  set to %s\n",
             GST_DEBUG_PAD_NAME(pad),GST_DEBUG_FUNCPTR_NAME(get));
}

/**
 * gst_pad_set_event_function:
 * @pad: the pad to set the event handler for
 * @event: the event handler
 *
 * Set the given event handler for the pad.
 */
void
gst_pad_set_event_function (GstPad *pad,
                            GstPadEventFunction event)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_EVENTFUNC(pad) = event;
  GST_DEBUG (GST_CAT_PADS,"eventfunc for %s:%s  set to %s\n",
             GST_DEBUG_PAD_NAME(pad),GST_DEBUG_FUNCPTR_NAME(event));
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
  GST_DEBUG (GST_CAT_PADS,"getregionfunc for %s:%s set to %s\n",
             GST_DEBUG_PAD_NAME(pad),GST_DEBUG_FUNCPTR_NAME(getregion));
}

/**
 * gst_pad_set_negotiate_function:
 * @pad: the pad to set the negotiate function for
 * @nego: the negotiate function
 *
 * Set the given negotiate function for the pad.
 */
void
gst_pad_set_negotiate_function (GstPad *pad,
		                GstPadNegotiateFunction nego)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_NEGOTIATEFUNC(pad) = nego;
  GST_DEBUG (GST_CAT_PADS,"negotiatefunc for %s:%s set to %s\n",
             GST_DEBUG_PAD_NAME(pad),GST_DEBUG_FUNCPTR_NAME(nego));
}

/**
 * gst_pad_set_newcaps_function:
 * @pad: the pad to set the newcaps function for
 * @newcaps: the newcaps function
 *
 * Set the given newcaps function for the pad.
 */
void
gst_pad_set_newcaps_function (GstPad *pad,
		              GstPadNewCapsFunction newcaps)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_NEWCAPSFUNC (pad) = newcaps;
  GST_DEBUG (GST_CAT_PADS,"newcapsfunc for %s:%s set to %s\n",
             GST_DEBUG_PAD_NAME(pad),GST_DEBUG_FUNCPTR_NAME(newcaps));
}

/**
 * gst_pad_set_bufferpool_function:
 * @pad: the pad to set the bufferpool function for
 * @bufpool: the bufferpool function
 *
 * Set the given bufferpool function for the pad.
 */
void
gst_pad_set_bufferpool_function (GstPad *pad,
		                 GstPadBufferPoolFunction bufpool)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_BUFFERPOOLFUNC (pad) = bufpool;
  GST_DEBUG (GST_CAT_PADS,"bufferpoolfunc for %s:%s set to %s\n",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME(bufpool));
}

static void
gst_pad_push_func(GstPad *pad, GstBuffer *buf)
{
  if (GST_RPAD_CHAINFUNC(GST_RPAD_PEER(pad)) != NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW,"calling chain function %s\n",
               GST_DEBUG_FUNCPTR_NAME(GST_RPAD_CHAINFUNC(GST_RPAD_PEER(pad))));
    (GST_RPAD_CHAINFUNC(GST_RPAD_PEER(pad)))(pad,buf);
  } else {
    GST_DEBUG (GST_CAT_DATAFLOW,"got a problem here: default pad_push handler in place, no chain function\n");
  }
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

  GST_INFO (GST_CAT_ELEMENT_PADS, "disconnecting %s:%s(%p) and %s:%s(%p)",
            GST_DEBUG_PAD_NAME(srcpad), srcpad, GST_DEBUG_PAD_NAME(sinkpad), sinkpad);

  // now we need to deal with the real/ghost stuff
  realsrc = GST_PAD_REALIZE(srcpad);
  realsink = GST_PAD_REALIZE(sinkpad);

  g_return_if_fail (GST_RPAD_PEER(realsrc) != NULL);
  g_return_if_fail (GST_RPAD_PEER(realsink) != NULL);

  if ((GST_RPAD_DIRECTION(realsrc) == GST_PAD_SINK) &&
      (GST_RPAD_DIRECTION(realsink) == GST_PAD_SRC)) {
    GstRealPad *temppad;

    temppad = realsrc;
    realsrc = realsink;
    realsink = temppad;
  }
  g_return_if_fail ((GST_RPAD_DIRECTION(realsrc) == GST_PAD_SRC) &&
                    (GST_RPAD_DIRECTION(realsink) == GST_PAD_SINK));

  /* first clear peers */
  GST_RPAD_PEER(realsrc) = NULL;
  GST_RPAD_PEER(realsink) = NULL;

  /* fire off a signal to each of the pads telling them that they've been disconnected */
  g_signal_emit(G_OBJECT(realsrc), gst_real_pad_signals[REAL_DISCONNECTED], 0, realsink);
  g_signal_emit(G_OBJECT(realsink), gst_real_pad_signals[REAL_DISCONNECTED], 0, realsrc);

  // now tell the scheduler
  if (realsrc->sched)
    GST_SCHEDULE_PAD_DISCONNECT (realsrc->sched, (GstPad *)realsrc, (GstPad *)realsink);
//  if (realsink->sched)
//    GST_SCHEDULE_PAD_DISCONNECT (realsink->sched, (GstPad *)realsrc, (GstPad *)realsink);

  GST_INFO (GST_CAT_ELEMENT_PADS, "disconnected %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME(srcpad), GST_DEBUG_PAD_NAME(sinkpad));
}

/**
 * gst_pad_connect:
 * @srcpad: the source pad to connect
 * @sinkpad: the sink pad to connect
 *
 * Connects the source pad to the sink pad.
 *
 * Returns: TRUE if the pad could be connected
 */
gboolean
gst_pad_connect (GstPad *srcpad,
		 GstPad *sinkpad)
{
  GstRealPad *realsrc, *realsink;
  gboolean negotiated = FALSE;

  /* generic checks */
  g_return_val_if_fail(srcpad != NULL, FALSE);
  g_return_val_if_fail(GST_IS_PAD(srcpad), FALSE);
  g_return_val_if_fail(sinkpad != NULL, FALSE);
  g_return_val_if_fail(GST_IS_PAD(sinkpad), FALSE);

  GST_INFO (GST_CAT_PADS, "connecting %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME(srcpad), GST_DEBUG_PAD_NAME(sinkpad));

  // now we need to deal with the real/ghost stuff
  realsrc = GST_PAD_REALIZE(srcpad);
  realsink = GST_PAD_REALIZE(sinkpad);

  if ((GST_PAD (realsrc) != srcpad) || (GST_PAD (realsink) != sinkpad))
    GST_INFO (GST_CAT_PADS, "*actually* connecting %s:%s and %s:%s",
              GST_DEBUG_PAD_NAME(realsrc), GST_DEBUG_PAD_NAME(realsink));

  g_return_val_if_fail(GST_RPAD_PEER(realsrc) == NULL, FALSE);
  g_return_val_if_fail(GST_RPAD_PEER(realsink) == NULL, FALSE);

  /* check for reversed directions and swap if necessary */
  if ((GST_RPAD_DIRECTION(realsrc) == GST_PAD_SINK) &&
      (GST_RPAD_DIRECTION(realsink) == GST_PAD_SRC)) {
    GstRealPad *temppad;

    temppad = realsrc;
    realsrc = realsink;
    realsink = temppad;
  }
  g_return_val_if_fail((GST_RPAD_DIRECTION(realsrc) == GST_PAD_SRC) &&
                       (GST_RPAD_DIRECTION(realsink) == GST_PAD_SINK), FALSE);


  /* first set peers */
  GST_RPAD_PEER(realsrc) = realsink;
  GST_RPAD_PEER(realsink) = realsrc;

  if (GST_PAD_CAPS (srcpad)) {
    GST_DEBUG(GST_CAT_PADS, "renegotiation from srcpad\n");
    negotiated = gst_pad_renegotiate (srcpad);
  }
  else if (GST_PAD_CAPS (sinkpad)) {
    GST_DEBUG(GST_CAT_PADS, "renegotiation from sinkpad\n");
    negotiated = gst_pad_renegotiate (sinkpad);
  }
  else {
    GST_DEBUG(GST_CAT_PADS, "not renegotiating connection\n");
    negotiated = TRUE;
  }

  if (!negotiated) {
    GST_INFO(GST_CAT_PADS, "pads %s:%s and %s:%s failed to negotiate, disconnecting",
             GST_DEBUG_PAD_NAME(srcpad), GST_DEBUG_PAD_NAME(sinkpad));
    gst_pad_disconnect (GST_PAD (realsrc), GST_PAD (realsink));
    return FALSE;
  }

  /* fire off a signal to each of the pads telling them that they've been connected */
  g_signal_emit(G_OBJECT(realsrc), gst_real_pad_signals[REAL_CONNECTED], 0, realsink);
  g_signal_emit(G_OBJECT(realsink), gst_real_pad_signals[REAL_CONNECTED], 0, realsrc);

  // now tell the scheduler(s)
  if (realsrc->sched)
    GST_SCHEDULE_PAD_CONNECT (realsrc->sched, (GstPad *)realsrc, (GstPad *)realsink);
  else if (realsink->sched)
    GST_SCHEDULE_PAD_CONNECT (realsink->sched, (GstPad *)realsrc, (GstPad *)realsink);

  GST_INFO (GST_CAT_PADS, "connected %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME(srcpad), GST_DEBUG_PAD_NAME(sinkpad));
  return TRUE;
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
  g_return_if_fail (GST_IS_OBJECT (parent));
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
GstElement*
gst_pad_get_parent (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PARENT (pad);
}

/**
 * gst_pad_get_padtemplate:
 * @pad: the pad to get the padtemplate from
 *
 * Get the padtemplate object of this pad.
 *
 * Returns: the padtemplate object
 */
GstPadTemplate*
gst_pad_get_padtemplate (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PADTEMPLATE (pad); 
}

/**
 * gst_pad_set_sched:
 * @pad: the pad to set the scheduler for
 * @sched: The scheduler to set
 *
 * Set the sceduler for the pad
 */
void
gst_pad_set_sched (GstPad *pad, GstSchedule *sched)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));

  GST_RPAD_SCHED(pad) = sched;
}

/**
 * gst_pad_get_sched:
 * @pad: the pad to get the scheduler from
 *
 * Get the scheduler of the pad
 *
 * Returns: the scheduler of the pad.
 */
GstSchedule*
gst_pad_get_sched (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_RPAD_SCHED(pad);
}

/**
 * gst_pad_get_real_parent:
 * @pad: the pad to get the parent from
 *
 * Get the real parent object of this pad. If the pad
 * is a ghostpad, the actual owner of the real pad is
 * returned, as opposed to the gst_pad_get_parent().
 *
 * Returns: the parent object
 */
GstElement*
gst_pad_get_real_parent (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PARENT (GST_PAD (GST_PAD_REALIZE (pad)));
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
 * gst_pad_set_caps:
 * @pad: the pad to set the caps to
 * @caps: the capabilities to attach to this pad
 *
 * Set the capabilities of this pad.
 *
 * Returns: a boolean indicating the caps could be set on the pad
 */
gboolean
gst_pad_set_caps (GstPad *pad,
                  GstCaps *caps)
{
  GstCaps *oldcaps;

  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_REAL_PAD (pad), FALSE);		// NOTE this restriction

  GST_INFO (GST_CAT_CAPS, "setting caps %p on pad %s:%s",
            caps, GST_DEBUG_PAD_NAME(pad));

  if (!gst_caps_check_compatibility (caps, gst_pad_get_padtemplate_caps (pad))) {
    g_warning ("pad %s:%s tried to set caps incompatible with its padtemplate\n",
		    GST_DEBUG_PAD_NAME (pad));
    //return FALSE;
  }
  
  oldcaps = GST_PAD_CAPS (pad);

  if (caps)
    gst_caps_ref (caps);
  GST_PAD_CAPS(pad) = caps;

  if (oldcaps)
    gst_caps_unref (oldcaps);

  return gst_pad_renegotiate (pad);
}

/**
 * gst_pad_get_caps:
 * @pad: the pad to get the capabilities from
 *
 * Get the capabilities of this pad.
 *
 * Returns: the capabilities of this pad
 */
GstCaps*
gst_pad_get_caps (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_CAPS (pad))
    return GST_PAD_CAPS (pad);
  else if (GST_PAD_PADTEMPLATE (pad))
    return GST_PADTEMPLATE_CAPS (GST_PAD_PADTEMPLATE (pad));

  return NULL;
}

/**
 * gst_pad_get_padtemplate_caps:
 * @pad: the pad to get the capabilities from
 *
 * Get the capabilities of this pad.
 *
 * Returns: a list of the capabilities of this pad
 */
GstCaps*
gst_pad_get_padtemplate_caps (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_PADTEMPLATE (pad))
    return GST_PADTEMPLATE_CAPS (GST_PAD_PADTEMPLATE (pad));

  return NULL;
}


/**
 * gst_padtemplate_get_caps_by_name:
 * @templ: the padtemplate to get the capabilities from
 * @name: the name of the capability to get
 *
 * Get the capability with the given name from this padtemplate.
 *
 * Returns: a capability or NULL if not found
 */
GstCaps*
gst_padtemplate_get_caps_by_name (GstPadTemplate *templ, const gchar *name)
{
  GstCaps *caps;

  g_return_val_if_fail (templ != NULL, NULL);

  caps = GST_PADTEMPLATE_CAPS (templ);
  if (!caps) 
    return NULL;

  return gst_caps_get_by_name (caps, name);
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
  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  if (GST_PAD_CAPS(srcpad) && GST_PAD_CAPS(sinkpad)) {
    if (!gst_caps_check_compatibility (GST_PAD_CAPS(srcpad), GST_PAD_CAPS(sinkpad))) {
      return FALSE;
    }
    else {
      return TRUE;
    }
  }
  else {
    GST_DEBUG (GST_CAT_PADS,"could not check capabilities of pads (%s:%s) and (%s:%s) %p %p\n",
		    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad), 
		    GST_PAD_CAPS (srcpad), GST_PAD_CAPS (sinkpad));
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

/**
 * gst_pad_get_bufferpool:
 * @pad: the pad to get the bufferpool from
 *
 * Get the bufferpool of the peer pad of the given
 * pad
 *
 * Returns: The GstBufferPool or NULL.
 */
GstBufferPool*          
gst_pad_get_bufferpool (GstPad *pad)
{
  GstRealPad *peer;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
   
  peer = GST_RPAD_PEER(pad);

  g_return_val_if_fail (peer != NULL, NULL);

  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));

  if (peer->bufferpoolfunc) {
    GST_DEBUG (GST_CAT_PADS,"calling bufferpoolfunc &%s (@%p) of peer pad %s:%s\n",
      GST_DEBUG_FUNCPTR_NAME(peer->bufferpoolfunc),&peer->bufferpoolfunc,GST_DEBUG_PAD_NAME(((GstPad*)peer)));
    return (peer->bufferpoolfunc)(((GstPad*)peer));
  } else {
    GST_DEBUG (GST_CAT_PADS,"no bufferpoolfunc for peer pad %s:%s at %p\n",GST_DEBUG_PAD_NAME(((GstPad*)peer)),&peer->bufferpoolfunc);
    return NULL;
  }
}

static void
gst_real_pad_destroy (GObject *object)
{
  GstPad *pad = GST_PAD (object);

  GST_DEBUG (GST_CAT_REFCOUNTING, "destroy %s:%s\n", GST_DEBUG_PAD_NAME(pad));

  if (GST_PAD (pad)->padtemplate)
    gst_object_unref (GST_OBJECT (GST_PAD (pad)->padtemplate));

  if (GST_PAD_PEER (pad))
    gst_pad_disconnect (pad, GST_PAD (GST_PAD_PEER (pad)));

  if (GST_IS_ELEMENT (GST_OBJECT_PARENT (pad)))
    gst_element_remove_pad (GST_ELEMENT (GST_OBJECT_PARENT (pad)), pad);

  // FIXME we should destroy the ghostpads, because they are nothing without the real pad
  if (GST_REAL_PAD (pad)->ghostpads) {
    GList *orig, *ghostpads;

    orig = ghostpads = g_list_copy (GST_REAL_PAD (pad)->ghostpads);

    while (ghostpads) {
      GstPad *ghostpad = GST_PAD (ghostpads->data);

      if (GST_IS_ELEMENT (GST_OBJECT_PARENT (ghostpad)))
        gst_element_remove_pad (GST_ELEMENT (GST_OBJECT_PARENT (ghostpad)), ghostpad);

      ghostpads = g_list_next (ghostpads);
    }
    g_list_free (orig);
    g_list_free (GST_REAL_PAD(pad)->ghostpads);
  }

// FIXME !!
//  if (G_OBJECT_CLASS (real_pad_parent_class)->destroy)
//    G_OBJECT_CLASS (real_pad_parent_class)->destroy (object);
}


#ifndef GST_DISABLE_LOADSAVE
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
  xmlNodePtr field = self->xmlChildrenNode;
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
#endif // GST_DISABLE_LOADSAVE

static gboolean
gst_pad_renegotiate_func (GstPad *pad, gpointer *data1, GstPad *peerpad, gpointer *data2, GstCaps **newcaps)
{
  GstRealPad *currentpad, *otherpad;
  gpointer *currentdata, *otherdata;
  GstPadNegotiateReturn result;
  gint counter = 0;
  
  g_return_val_if_fail (pad != NULL, FALSE);

  currentpad = GST_PAD_REALIZE (pad);
  otherpad = GST_REAL_PAD (peerpad);
  currentdata = data1;
  otherdata = data2;

  GST_DEBUG (GST_CAT_NEGOTIATION, "negotiating pad %s:%s and %s:%s data:%p\n",
            GST_DEBUG_PAD_NAME(currentpad), GST_DEBUG_PAD_NAME(otherpad), currentdata);

  do {
    gboolean matchtempl;
    
    if (!*newcaps) {
      if (otherpad->negotiatefunc) {
        GstRealPad *temp;
        gpointer *tempdata;

        GST_DEBUG (GST_CAT_NEGOTIATION, "requesting other caps from pad %s:%s data:%p\n",
                        GST_DEBUG_PAD_NAME(otherpad), otherdata);
        otherpad->negotiatefunc (GST_PAD (otherpad), newcaps, otherdata);

        temp = otherpad;
        otherpad = currentpad;
        currentpad = temp;

        tempdata = otherdata;
        otherdata = currentdata;
        currentdata = tempdata;
      }
    }

    GST_DEBUG (GST_CAT_NEGOTIATION, "checking compatibility with pad %s:%s\n",
                     GST_DEBUG_PAD_NAME(otherpad));
    matchtempl = gst_caps_check_compatibility (*newcaps, gst_pad_get_padtemplate_caps (GST_PAD (otherpad)));

    GST_DEBUG (GST_CAT_NEGOTIATION, "caps compatibility check %s\n", (matchtempl?"ok":"fail"));

    if (matchtempl) {
      GST_DEBUG (GST_CAT_NEGOTIATION, "checking if other pad %s:%s can negotiate data:%p\n",
                     GST_DEBUG_PAD_NAME(otherpad), otherdata);
      if (otherpad->negotiatefunc) {
        GstRealPad *temp;
        gpointer *tempdata;

        GST_DEBUG (GST_CAT_NEGOTIATION, "switching pad for next phase\n");

        temp = currentpad;
        currentpad = otherpad;
        otherpad = temp;

        tempdata = otherdata;
        otherdata = currentdata;
        currentdata = tempdata;
      }
      else if (gst_caps_check_compatibility (*newcaps, GST_PAD_CAPS (otherpad))) {
        GST_DEBUG (GST_CAT_NEGOTIATION, "negotiation succeeded\n");
        return TRUE;
      }
      else {
	*newcaps = GST_PAD_CAPS (otherpad);
	if (*newcaps) gst_caps_ref(*newcaps);
      }
    }
    else {
      *newcaps = GST_PAD_CAPS (otherpad);
      if (*newcaps) gst_caps_ref(*newcaps);
    }

    counter++;

    if (currentpad->negotiatefunc) {
      GST_DEBUG (GST_CAT_NEGOTIATION, "calling negotiate function on pad %s:%s data: %p\n",
		      GST_DEBUG_PAD_NAME (currentpad), currentdata);
      result = currentpad->negotiatefunc (GST_PAD (currentpad), newcaps, currentdata);

      switch (result) {
        case GST_PAD_NEGOTIATE_FAIL:
          GST_DEBUG (GST_CAT_NEGOTIATION, "negotiation failed\n");
          return FALSE;
        case GST_PAD_NEGOTIATE_AGREE:
          GST_DEBUG (GST_CAT_NEGOTIATION, "negotiation succeeded\n");
          return TRUE;
        case GST_PAD_NEGOTIATE_TRY:
          GST_DEBUG (GST_CAT_NEGOTIATION, "try another option\n");
          break;
	default:
          GST_DEBUG (GST_CAT_NEGOTIATION, "invalid return\n");
          break;
      }
    }
    else {
      GST_DEBUG (GST_CAT_NEGOTIATION, "negotiation failed, no more options\n");
      return FALSE;
    }
      
  } while (counter < 100);

  g_warning ("negotiation between (%s:%s) and (%s:%s) failed: too many attempts (%d)\n",
            GST_DEBUG_PAD_NAME(pad), GST_DEBUG_PAD_NAME(peerpad), counter);

  GST_DEBUG (GST_CAT_NEGOTIATION, "negotiation failed, too many attempts\n");
  
  return FALSE;
}

/**
 * gst_pad_renegotiate:
 * @pad: the pad to perform the negotiation on
 *
 * Perform the negotiation process with the peer pad.
 *
 * Returns: TRUE if the negotiation process succeded
 */
gboolean
gst_pad_renegotiate (GstPad *pad)
{
  GstCaps *newcaps = NULL;
  GstRealPad *peerpad, *currentpad, *otherpad;
  gboolean result;
  gpointer data1 = NULL, data2 = NULL;
  
  g_return_val_if_fail (pad != NULL, FALSE);

  peerpad = GST_PAD_PEER (pad);

  currentpad = GST_PAD_REALIZE (pad);

  if (!peerpad) {
    GST_DEBUG (GST_CAT_NEGOTIATION, "no peer pad for pad %s:%s\n",
                 GST_DEBUG_PAD_NAME(currentpad));
    return TRUE;
  }
   
  otherpad = GST_REAL_PAD (peerpad);

  GST_INFO (GST_CAT_NEGOTIATION, "negotiating pad %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME(pad), GST_DEBUG_PAD_NAME(peerpad));

  newcaps = GST_PAD_CAPS (pad);
  
  result = gst_pad_renegotiate_func (GST_PAD (currentpad), &data1, GST_PAD (otherpad), &data2, &newcaps);

  if (!result) {
    GST_DEBUG (GST_CAT_NEGOTIATION, "firing caps_nego_failed signal on %s:%s and %s:%s to give it a chance to succeed\n",
               GST_DEBUG_PAD_NAME(currentpad),GST_DEBUG_PAD_NAME(otherpad));
    g_signal_emit (G_OBJECT(currentpad), 
                     gst_real_pad_signals[REAL_CAPS_NEGO_FAILED], 0, &result);
    g_signal_emit (G_OBJECT(otherpad), 
                     gst_real_pad_signals[REAL_CAPS_NEGO_FAILED], 0, &result);
    if (result)
      GST_DEBUG (GST_CAT_NEGOTIATION, "caps_nego_failed handler claims success at renego, believing\n");
  }

  if (result) {
    GST_DEBUG (GST_CAT_NEGOTIATION, "pads aggreed on caps :)\n");

  newcaps = GST_PAD_CAPS (pad);
    //g_return_val_if_fail(newcaps != NULL, FALSE);	// FIXME is this valid?

    /* here we have some sort of aggreement of the caps */
    GST_PAD_CAPS (currentpad) = gst_caps_ref (newcaps);
    if (GST_RPAD_NEWCAPSFUNC (currentpad))
      GST_RPAD_NEWCAPSFUNC (currentpad) (GST_PAD (currentpad), newcaps);

    GST_PAD_CAPS (otherpad) = gst_caps_ref (newcaps);
    if (GST_RPAD_NEWCAPSFUNC (otherpad))
      GST_RPAD_NEWCAPSFUNC (otherpad) (GST_PAD (otherpad), newcaps);

    GST_DEBUG (GST_CAT_NEGOTIATION, "firing caps_changed signal on %s:%s and %s:%s\n",
               GST_DEBUG_PAD_NAME(currentpad),GST_DEBUG_PAD_NAME(otherpad));
    g_signal_emit (G_OBJECT(currentpad), 
                     gst_real_pad_signals[REAL_CAPS_CHANGED], 0, GST_PAD_CAPS(currentpad));
    g_signal_emit (G_OBJECT(otherpad), 
                     gst_real_pad_signals[REAL_CAPS_CHANGED], 0, GST_PAD_CAPS(otherpad));
  }

  return result;
}

/**
 * gst_pad_negotiate_proxy:
 * @srcpad: the pad that proxies
 * @destpad: the pad to proxy the negotiation to
 * @caps: the current caps
 *
 * Proxies the negotiation pad from srcpad to destpad. Further
 * negotiation is done on the peers of both pad instead.
 *
 * Returns: the result of the negotiation preocess.
 */
GstPadNegotiateReturn
gst_pad_negotiate_proxy (GstPad *srcpad, GstPad *destpad, GstCaps **caps)
{
  GstRealPad *srcpeer;
  GstRealPad *destpeer;
  gboolean result;
  gpointer data1 = NULL, data2 = NULL;

  g_return_val_if_fail (srcpad != NULL, GST_PAD_NEGOTIATE_FAIL);
  g_return_val_if_fail (destpad != NULL, GST_PAD_NEGOTIATE_FAIL);

  GST_DEBUG (GST_CAT_NEGOTIATION, "negotiation proxied from pad (%s:%s) to pad (%s:%s)\n", 
		  GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (destpad));

  srcpeer = GST_RPAD_PEER (srcpad);
  destpeer = GST_RPAD_PEER (destpad);

  if (srcpeer && destpeer) {
    result = gst_pad_renegotiate_func (GST_PAD (srcpeer), &data1, GST_PAD (destpeer), &data2, caps);

    if (result) {
      GST_DEBUG (GST_CAT_NEGOTIATION, "pads (%s:%s) and (%s:%s) aggreed on caps :)\n",
		  GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (destpad));

      /* here we have some sort of aggreement of the caps */
      GST_PAD_CAPS (destpeer) = *caps;
      if (GST_RPAD_NEWCAPSFUNC (destpeer))
        GST_RPAD_NEWCAPSFUNC (destpeer) (GST_PAD (destpeer), *caps);

      GST_PAD_CAPS (destpad) = *caps;
      if (GST_RPAD_NEWCAPSFUNC (destpad))
        GST_RPAD_NEWCAPSFUNC (destpad) (GST_PAD (destpad), *caps);
    }
    else {
      GST_DEBUG (GST_CAT_NEGOTIATION, "pads did not aggree on caps :(\n");
      return GST_PAD_NEGOTIATE_FAIL;
    }
  }
  else {
    GST_PAD_CAPS (destpad) = *caps;
    if (GST_RPAD_NEWCAPSFUNC (destpad))
      GST_RPAD_NEWCAPSFUNC (destpad) (GST_PAD (destpad), *caps);
  }

  return GST_PAD_NEGOTIATE_AGREE;
}

#ifndef GST_DISABLE_LOADSAVE
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
#endif // GST_DISABLE_LOADSAVE

#ifndef gst_pad_push
/**
 * gst_pad_push:
 * @pad: the pad to push
 * @buf: the buffer to push
 *
 * Push a buffer to the peer of the pad.
 */
void 
gst_pad_push (GstPad *pad, GstBuffer *buf) 
{
  GstRealPad *peer = GST_RPAD_PEER (pad);

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));

  g_return_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC);
  g_return_if_fail (peer != NULL);
  
  if (peer->chainhandler) {
    GST_DEBUG (GST_CAT_DATAFLOW, "calling chainhandler &%s of peer pad %s:%s\n",
          GST_DEBUG_FUNCPTR_NAME (peer->chainhandler), GST_DEBUG_PAD_NAME (((GstPad*)peer)));
    (peer->chainhandler) (((GstPad*)peer), buf);
  } else
    GST_DEBUG (GST_CAT_DATAFLOW, "no chainhandler\n");
}
#endif

#ifndef gst_pad_pull
/**
 * gst_pad_pull:
 * @pad: the pad to pull
 *
 * Pull a buffer from the peer pad.
 *
 * Returns: a new buffer from the peer pad.
 */
GstBuffer*
gst_pad_pull (GstPad *pad) 
{
  GstRealPad *peer = GST_RPAD_PEER(pad);
  
  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));

  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK, NULL);
  g_return_val_if_fail (peer != NULL, NULL);

  if (peer->gethandler) {
    GST_DEBUG (GST_CAT_DATAFLOW,"calling gethandler %s of peer pad %s:%s\n",
      GST_DEBUG_FUNCPTR_NAME(peer->gethandler),GST_DEBUG_PAD_NAME(peer));
    return (peer->gethandler)(((GstPad*)peer));
  } else {
    GST_DEBUG (GST_CAT_DATAFLOW,"no gethandler for peer pad %s:%s at %p\n",GST_DEBUG_PAD_NAME(((GstPad*)peer)),&peer->gethandler);
    return NULL;
  }
}
#endif

#ifndef gst_pad_pullregion
/**
 * gst_pad_pullregion:
 * @pad: the pad to pull the region from
 * @type: the regiontype
 * @offset: the offset/start of the buffer to pull
 * @len: the length of the buffer to pull
 *
 * Pull a buffer region from the peer pad. The region to pull can be 
 * specified with a offset/lenght pair or with a start/legnth time
 * indicator as specified by the type parameter.
 *
 * Returns: a new buffer from the peer pad with data in the specified
 * region.
 */
GstBuffer*
gst_pad_pullregion (GstPad *pad, GstRegionType type, guint64 offset, guint64 len) 
{
  GstRealPad *peer;
  GstBuffer *result = NULL;
  
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK, NULL);

  do {
    peer = GST_RPAD_PEER(pad);
    g_return_val_if_fail (peer != NULL, NULL);

    if (result) 
      gst_buffer_unref (result);

    GST_DEBUG_ENTER("(%s:%s,%d,%lld,%lld)",GST_DEBUG_PAD_NAME(pad),type,offset,len);

    if (peer->pullregionfunc) {
      GST_DEBUG (GST_CAT_DATAFLOW,"calling pullregionfunc &%s of peer pad %s:%s\n",
          GST_DEBUG_FUNCPTR_NAME(peer->pullregionfunc),GST_DEBUG_PAD_NAME(((GstPad*)peer)));
      result = (peer->pullregionfunc)(((GstPad*)peer),type,offset,len);
    } else {
      GST_DEBUG (GST_CAT_DATAFLOW,"no pullregionfunc\n");
      result = NULL;
      break;
    }
  }
  while (result && ! GST_BUFFER_FLAG_IS_SET (result, GST_BUFFER_EOS) 
	   && !(GST_BUFFER_OFFSET (result) == offset && 
	   GST_BUFFER_SIZE (result) == len));

  return result;
}
#endif

/**
 * gst_pad_peek:
 * @pad: the pad to peek
 *
 * Peek for a buffer from the peer pad.
 *
 * Returns: a from the peer pad or NULL if the peer has no buffer.
 */
GstBuffer*
gst_pad_peek (GstPad *pad)
{
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK, NULL);

  return GST_RPAD_BUFPEN (GST_RPAD_PEER (pad));
}

/**
 * gst_pad_select:
 * @padlist: A list of pads 
 *
 * Wait for a buffer on the list of pads.
 *
 * Returns: The pad that has a buffer available, use 
 * #gst_pad_pull to get the buffer.
 */
GstPad*
gst_pad_select (GList *padlist)
{
  GstPad *pad;

  pad = gst_schedule_pad_select (gst_pad_get_sched (GST_PAD (padlist->data)), padlist);

  return pad;
}

/**
 * gst_pad_selectv:
 * @pad: The first pad to perform the select on 
 * @...: More pads
 *
 * Wait for a buffer on the given of pads.
 *
 * Returns: The pad that has a buffer available, use 
 * #gst_pad_pull to get the buffer.
 */
GstPad*
gst_pad_selectv (GstPad *pad, ...)
{
  GstPad *result;
  GList *padlist = NULL;
  va_list var_args;

  if (pad == NULL)
    return NULL;

  va_start (var_args, pad);

  while (pad) {
    padlist = g_list_prepend (padlist, pad);
    pad = va_arg (var_args, GstPad *);
  }
  result = gst_pad_select (padlist);
  g_list_free (padlist);

  va_end (var_args);
  
  return result;
}

/************************************************************************
 *
 * templates
 *
 */
static void		gst_padtemplate_class_init	(GstPadTemplateClass *klass);
static void		gst_padtemplate_init		(GstPadTemplate *templ);

enum {
  TEMPL_PAD_CREATED,
  /* FILL ME */
  TEMPL_LAST_SIGNAL
};

static GstObject *padtemplate_parent_class = NULL;
static guint gst_padtemplate_signals[TEMPL_LAST_SIGNAL] = { 0 };

GType
gst_padtemplate_get_type (void)
{
  static GType padtemplate_type = 0;

  if (!padtemplate_type) {
    static const GTypeInfo padtemplate_info = {
      sizeof(GstPadTemplateClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_padtemplate_class_init,
      NULL,
      NULL,
      sizeof(GstPadTemplate),
      32,
      (GInstanceInitFunc)gst_padtemplate_init,
    };
    padtemplate_type = g_type_register_static(GST_TYPE_OBJECT, "GstPadTemplate", &padtemplate_info, 0);
  }
  return padtemplate_type;
}

static void
gst_padtemplate_class_init (GstPadTemplateClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;

  padtemplate_parent_class = g_type_class_ref(GST_TYPE_OBJECT);

  gst_padtemplate_signals[TEMPL_PAD_CREATED] =
    g_signal_newc ("pad_created", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstPadTemplateClass, pad_created), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    GST_TYPE_PAD);


  gstobject_class->path_string_separator = "*";
}

static void
gst_padtemplate_init (GstPadTemplate *templ)
{
}

/**
 * gst_padtemplate_new:
 * @name_template: the name template
 * @direction: the direction for the template
 * @presence: the presence of the pad
 * @caps: a list of capabilities for the template
 * @...: more capabilities
 *
 * Creates a new padtemplate from the given arguments.
 *
 * Returns: the new padtemplate
 */
GstPadTemplate*
gst_padtemplate_new (gchar *name_template,
		     GstPadDirection direction, GstPadPresence presence,
		     GstCaps *caps, ...)
{
  GstPadTemplate *new;
  va_list var_args;
  GstCaps *thecaps = NULL;

  g_return_val_if_fail (name_template != NULL, NULL);

  new = g_object_new(gst_padtemplate_get_type () ,NULL);

  GST_PADTEMPLATE_NAME_TEMPLATE (new) = name_template;
  GST_PADTEMPLATE_DIRECTION (new) = direction;
  GST_PADTEMPLATE_PRESENCE (new) = presence;

  va_start (var_args, caps);

  while (caps) {
    thecaps = gst_caps_append (thecaps, caps);
    caps = va_arg (var_args, GstCaps*);
  }
  va_end (var_args);
  
  GST_PADTEMPLATE_CAPS (new) = thecaps;

  return new;
}

/**
 * gst_padtemplate_get_caps:
 * @templ: the padtemplate to use
 *
 * Get the capabilities of the padtemplate
 *
 * Returns: a GstCaps*
 */
GstCaps*
gst_padtemplate_get_caps (GstPadTemplate *templ)
{
  g_return_val_if_fail (templ != NULL, NULL);

  return GST_PADTEMPLATE_CAPS (templ);
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
  guchar *presence;

  GST_DEBUG (GST_CAT_XML,"saving padtemplate %s\n", templ->name_template);

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

  if (GST_PADTEMPLATE_CAPS (templ)) {
    subtree = xmlNewChild (parent, NULL, "caps", NULL);
    gst_caps_save_thyself (GST_PADTEMPLATE_CAPS (templ), subtree);
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
  xmlNodePtr field = parent->xmlChildrenNode;
  GstPadTemplate *factory;
  gchar *name_template = NULL;
  GstPadDirection direction = GST_PAD_UNKNOWN;
  GstPadPresence presence = GST_PAD_ALWAYS;
  GstCaps *caps = NULL;

  while (field) {
    if (!strcmp(field->name, "nametemplate")) {
      name_template = xmlNodeGetContent(field);
    }
    if (!strcmp(field->name, "direction")) {
      gchar *value = xmlNodeGetContent(field);

      if (!strcmp(value, "sink")) {
        direction = GST_PAD_SINK;
      }
      else if (!strcmp(value, "src")) {
        direction = GST_PAD_SRC;
      }
      g_free (value);
    }
    if (!strcmp(field->name, "presence")) {
      gchar *value = xmlNodeGetContent(field);

      if (!strcmp(value, "always")) {
        presence = GST_PAD_ALWAYS;
      }
      else if (!strcmp(value, "sometimes")) {
        presence = GST_PAD_SOMETIMES;
      }
      else if (!strcmp(value, "request")) {
        presence = GST_PAD_REQUEST;
      }
      g_free (value);
    }
    else if (!strcmp(field->name, "caps")) {
      caps = gst_caps_load_thyself (field);
    }
    field = field->next;
  }

  factory = gst_padtemplate_new (name_template, direction, presence, caps, NULL);

  return factory;
}


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

GType
gst_ghost_pad_get_type(void) {
  static GType pad_type = 0;

  if (!pad_type) {
    static const GTypeInfo pad_info = {
      sizeof(GstGhostPadClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_ghost_pad_class_init,
      NULL,
      NULL,
      sizeof(GstGhostPad),
      8,
      (GInstanceInitFunc)gst_ghost_pad_init,
    };
    pad_type = g_type_register_static(GST_TYPE_PAD, "GstGhostPad", &pad_info, 0);
  }
  return pad_type;
}

static void
gst_ghost_pad_class_init (GstGhostPadClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  ghost_pad_parent_class = g_type_class_ref(GST_TYPE_PAD);
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

  ghostpad = g_object_new(gst_ghost_pad_get_type () ,NULL);
  gst_pad_set_name (GST_PAD (ghostpad), name);
  GST_GPAD_REALPAD(ghostpad) = GST_PAD_REALIZE(pad);
  GST_PAD_PADTEMPLATE(ghostpad) = GST_PAD_PADTEMPLATE(pad);

  // add ourselves to the real pad's list of ghostpads
  gst_pad_add_ghost_pad (pad, GST_PAD(ghostpad));

  // FIXME need to ref the real pad here... ?

  GST_DEBUG(GST_CAT_PADS,"created ghost pad \"%s\"\n",name);

  return GST_PAD(ghostpad);
}


gboolean
gst_pad_event (GstPad *pad, void *event)
{
  GstRealPad *peer;
  gboolean handled = FALSE;

  GST_DEBUG(GST_CAT_EVENT, "have event %d on pad %s:%s\n",(gint)event,GST_DEBUG_PAD_NAME(pad));

  peer = GST_RPAD_PEER(pad);
  if (GST_RPAD_EVENTFUNC(peer))
    handled = GST_RPAD_EVENTFUNC(peer) (peer, event);

  else {
    GST_DEBUG(GST_CAT_EVENT, "there's no event function for peer %s:%s\n",GST_DEBUG_PAD_NAME(peer));
  }

  if (!handled) {
    GST_DEBUG(GST_CAT_EVENT, "would proceed with default behavior here\n");
    gst_pad_event_default(peer,event);
  }
}

/* pad is the receiving pad */
static void 
gst_pad_event_default(GstPad *pad, void *event)
{
  switch((gint)event) {
    case GST_EVENT_EOS:
      if (GST_PAD_PARENT(pad)->numsrcpads == 1)
        gst_element_signal_eos(GST_PAD_PARENT(pad));
      else
        GST_DEBUG(GST_CAT_EVENT, "WARNING: no default behavior for EOS with multiple srcpads\n");
      break;
  }
}
