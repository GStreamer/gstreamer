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

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gstpad.h"
#include "gstutils.h"
#include "gstelement.h"
#include "gsttype.h"
#include "gstbin.h"
#include "gstscheduler.h"
#include "gstevent.h"

GType _gst_pad_type = 0;

/***** Start with the base GstPad class *****/
static void		gst_pad_class_init		(GstPadClass *klass);
static void		gst_pad_init			(GstPad *pad);

static gboolean 	gst_pad_try_reconnect_filtered_func (GstRealPad *srcpad, GstRealPad *sinkpad, 
							 GstCaps *caps, gboolean clear);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr	gst_pad_save_thyself		(GstObject *object, xmlNodePtr parent);
#endif

static GstObject *pad_parent_class = NULL;

GType
gst_pad_get_type(void) 
{
  if (!_gst_pad_type) {
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
      NULL
    };
    _gst_pad_type = g_type_register_static(GST_TYPE_OBJECT, "GstPad", &pad_info, 0);
  }
  return _gst_pad_type;
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
  REAL_EVENT_RECEIVED,
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

static void	gst_real_pad_dispose		(GObject *object);

static void	gst_pad_push_func		(GstPad *pad, GstBuffer *buf);

GType _gst_real_pad_type = 0;

static GstPad *real_pad_parent_class = NULL;
static guint gst_real_pad_signals[REAL_LAST_SIGNAL] = { 0 };

GType
gst_real_pad_get_type(void) {
  if (!_gst_real_pad_type) {
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
      NULL
    };
    _gst_real_pad_type = g_type_register_static(GST_TYPE_PAD, "GstRealPad", &pad_info, 0);
  }
  return _gst_real_pad_type;
}

static void
gst_real_pad_class_init (GstRealPadClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;

  real_pad_parent_class = g_type_class_ref (GST_TYPE_PAD);

  gobject_class->dispose  = GST_DEBUG_FUNCPTR (gst_real_pad_dispose);
  gobject_class->set_property  = GST_DEBUG_FUNCPTR (gst_real_pad_set_property);
  gobject_class->get_property  = GST_DEBUG_FUNCPTR (gst_real_pad_get_property);

  gst_real_pad_signals[REAL_SET_ACTIVE] =
    g_signal_new ("set_active", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, set_active), NULL, NULL,
                    gst_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1,
                    G_TYPE_BOOLEAN);
  gst_real_pad_signals[REAL_CAPS_CHANGED] =
    g_signal_new ("caps_changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, caps_changed), NULL, NULL,
                    gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_real_pad_signals[REAL_CAPS_NEGO_FAILED] =
    g_signal_new ("caps_nego_failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, caps_nego_failed), NULL, NULL,
                    gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_real_pad_signals[REAL_CONNECTED] =
    g_signal_new ("connected", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, connected), NULL, NULL,
                    gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_real_pad_signals[REAL_DISCONNECTED] =
    g_signal_new ("disconnected", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, disconnected), NULL, NULL,
                    gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);
  gst_real_pad_signals[REAL_EVENT_RECEIVED] =
    g_signal_new ("event_received", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstRealPadClass, event_received), NULL, NULL,
                    gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);

/*  gtk_object_add_arg_type ("GstRealPad::active", G_TYPE_BOOLEAN, */
/*                           GTK_ARG_READWRITE, REAL_ARG_ACTIVE); */
  g_object_class_install_property (G_OBJECT_CLASS (klass), REAL_ARG_ACTIVE,
    g_param_spec_boolean ("active", "Active", "Whether the pad is active.",
                          TRUE,G_PARAM_READWRITE));

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_pad_save_thyself);
#endif
  gstobject_class->path_string_separator = ".";
}

static void
gst_real_pad_init (GstRealPad *pad)
{
  pad->direction = GST_PAD_UNKNOWN;
  pad->peer = NULL;

  pad->sched = NULL;
  pad->sched_private = NULL;

  pad->chainfunc = NULL;
  pad->getfunc = NULL;
  pad->getregionfunc = NULL;

  pad->chainhandler = GST_DEBUG_FUNCPTR (gst_pad_push_func);
  pad->gethandler = NULL;
  pad->pullregionfunc = NULL;

  pad->bufferpoolfunc = NULL;
  pad->ghostpads = NULL;
  pad->caps = NULL;

  pad->connectfunc = NULL;
  pad->getcapsfunc = NULL;
}

static void
gst_real_pad_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case REAL_ARG_ACTIVE:
      if (g_value_get_boolean (value)) {
        GST_DEBUG (GST_CAT_PADS, "activating pad %s:%s", GST_DEBUG_PAD_NAME (object));
        GST_FLAG_UNSET (object, GST_PAD_DISABLED);
      } else {
        GST_DEBUG (GST_CAT_PADS, "de-activating pad %s:%s", GST_DEBUG_PAD_NAME (object));
        GST_FLAG_SET (object, GST_PAD_DISABLED);
      }
      g_signal_emit (G_OBJECT (object), gst_real_pad_signals[REAL_SET_ACTIVE], 0,
                      !GST_FLAG_IS_SET (object, GST_PAD_DISABLED));
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
      g_value_set_boolean (value, !GST_FLAG_IS_SET (object, GST_PAD_DISABLED));
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
  GST_RPAD_DIRECTION (pad) = direction;

  return GST_PAD (pad);
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
  GST_PAD_PADTEMPLATE (pad) = templ;
  
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

  return GST_PAD_DIRECTION (pad);
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
void 
gst_pad_set_chain_function (GstPad *pad,
		            GstPadChainFunction chain)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_CHAINFUNC(pad) = chain;
  GST_DEBUG (GST_CAT_PADS, "chainfunc for %s:%s set to %s",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (chain));
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
  GST_DEBUG (GST_CAT_PADS, "getfunc for %s:%s  set to %s",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (get));
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
  GST_DEBUG (GST_CAT_PADS, "eventfunc for %s:%s  set to %s",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (event));
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
  GST_DEBUG (GST_CAT_PADS, "getregionfunc for %s:%s set to %s",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (getregion));
}

/**
 * gst_pad_set_connect_function:
 * @pad: the pad to set the connect function for
 * @connect: the connect function
 *
 * Set the given connect function for the pad. It will be called
 * when the pad is connected or reconnected with caps.
 */
void
gst_pad_set_connect_function (GstPad *pad,
		              GstPadConnectFunction connect)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_CONNECTFUNC (pad) = connect;
  GST_DEBUG (GST_CAT_PADS, "connectfunc for %s:%s set to %s",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (connect));
}

/**
 * gst_pad_set_getcaps_function:
 * @pad: the pad to set the getcaps function for
 * @getcaps: the getcaps function
 *
 * Set the given getcaps function for the pad.
 */
void
gst_pad_set_getcaps_function (GstPad *pad,
		              GstPadGetCapsFunction getcaps)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_GETCAPSFUNC (pad) = getcaps;
  GST_DEBUG (GST_CAT_PADS, "getcapsfunc for %s:%s set to %s",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (getcaps));
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
  GST_DEBUG (GST_CAT_PADS, "bufferpoolfunc for %s:%s set to %s",
             GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (bufpool));
}

static void
gst_pad_push_func(GstPad *pad, GstBuffer *buf)
{
  if (GST_RPAD_CHAINFUNC (GST_RPAD_PEER (pad)) != NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "calling chain function %s",
               GST_DEBUG_FUNCPTR_NAME (GST_RPAD_CHAINFUNC (GST_RPAD_PEER (pad))));
    (GST_RPAD_CHAINFUNC (GST_RPAD_PEER (pad))) (pad, buf);
  } else {
    GST_DEBUG (GST_CAT_DATAFLOW, "default pad_push handler in place, no chain function");
    g_warning ("(internal error) default pad_push in place for pad %s:%s but it has no chain function", 
		    GST_DEBUG_PAD_NAME (pad));
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
            GST_DEBUG_PAD_NAME (srcpad), srcpad, GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

  /* now we need to deal with the real/ghost stuff */
  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  g_return_if_fail (GST_RPAD_PEER (realsrc) != NULL);
  g_return_if_fail (GST_RPAD_PEER (realsink) == realsrc);

  if ((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SINK) &&
      (GST_RPAD_DIRECTION (realsink) == GST_PAD_SRC)) {
    GstRealPad *temppad;

    temppad = realsrc;
    realsrc = realsink;
    realsink = temppad;
  }
  g_return_if_fail ((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC) &&
                    (GST_RPAD_DIRECTION (realsink) == GST_PAD_SINK));

  /* first clear peers */
  GST_RPAD_PEER (realsrc) = NULL;
  GST_RPAD_PEER (realsink) = NULL;

  /* reset the filters, both filters are refcounted once */
  if (GST_RPAD_FILTER (realsrc)) {
    gst_caps_unref (GST_RPAD_FILTER (realsrc));
    GST_RPAD_FILTER (realsink) = NULL;
    GST_RPAD_FILTER (realsrc) = NULL;
  }

  /* now tell the scheduler */
  if (GST_PAD_PARENT (realsrc)->sched)
    gst_scheduler_pad_disconnect (GST_PAD_PARENT (realsrc)->sched, (GstPad *)realsrc, (GstPad *)realsink);
  else if (GST_PAD_PARENT (realsink)->sched)
    gst_scheduler_pad_disconnect (GST_PAD_PARENT (realsink)->sched, (GstPad *)realsrc, (GstPad *)realsink);

  /* hold a reference, as they can go away in the signal handlers */
  gst_object_ref (GST_OBJECT (realsrc));
  gst_object_ref (GST_OBJECT (realsink));

  /* fire off a signal to each of the pads telling them that they've been disconnected */
  g_signal_emit (G_OBJECT (realsrc), gst_real_pad_signals[REAL_DISCONNECTED], 0, realsink);
  g_signal_emit (G_OBJECT (realsink), gst_real_pad_signals[REAL_DISCONNECTED], 0, realsrc);

  GST_INFO (GST_CAT_ELEMENT_PADS, "disconnected %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  gst_object_unref (GST_OBJECT (realsrc));
  gst_object_unref (GST_OBJECT (realsink));
}

/**
 * gst_pad_can_connect_filtered:
 * @srcpad: the source pad to connect
 * @sinkpad: the sink pad to connect
 * @filtercaps: the filter caps.
 *
 * Checks if the source pad and the sink pad can be connected. 
 * The filter indicates the media type that should flow trought this connection.
 *
 * Returns: TRUE if the pad can be connected, FALSE otherwise
 */
gboolean
gst_pad_can_connect_filtered (GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps)
{
  gint num_decoupled = 0;
  GstRealPad *realsrc, *realsink;

  /* generic checks */
  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  /* now we need to deal with the real/ghost stuff */
  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  g_return_val_if_fail (GST_RPAD_PEER (realsrc) == NULL, FALSE);
  g_return_val_if_fail (GST_RPAD_PEER (realsink) == NULL, FALSE);
  g_return_val_if_fail (GST_PAD_PARENT (realsrc) != NULL, FALSE);
  g_return_val_if_fail (GST_PAD_PARENT (realsink) != NULL, FALSE);

  if (realsrc->sched && realsink->sched) {
    if (GST_FLAG_IS_SET (GST_PAD_PARENT (realsrc), GST_ELEMENT_DECOUPLED))
      num_decoupled++;
    if (GST_FLAG_IS_SET (GST_PAD_PARENT (realsink), GST_ELEMENT_DECOUPLED))
      num_decoupled++;

    if (realsrc->sched != realsink->sched && num_decoupled != 1) {
      g_warning ("connecting pads with different scheds requires exactly one decoupled element (queue)");
      return FALSE;
    }
  }
  
  /* check if the directions are compatible */
  if (!(((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SINK) &&
         (GST_RPAD_DIRECTION (realsink) == GST_PAD_SRC)) ||
        ((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC) &&
         (GST_RPAD_DIRECTION (realsink) == GST_PAD_SINK))))
  {
    return FALSE;
  }
  
  return TRUE;
}
/**
 * gst_pad_can_connect:
 * @srcpad: the source pad to connect
 * @sinkpad: the sink pad to connect
 *
 * Checks if the source pad can be connected to the sink pad.
 *
 * Returns: TRUE if the pads can be connected, FALSE otherwise
 */
gboolean
gst_pad_can_connect (GstPad *srcpad, GstPad *sinkpad)
{
  return gst_pad_can_connect_filtered (srcpad, sinkpad, NULL);
}

/**
 * gst_pad_connect_filtered:
 * @srcpad: the source pad to connect
 * @sinkpad: the sink pad to connect
 * @filtercaps: the filter caps.
 *
 * Connects the source pad to the sink pad. The filter indicates the media type
 * that should flow trought this connection.
 *
 * Returns: TRUE if the pad could be connected, FALSE otherwise
 */
gboolean
gst_pad_connect_filtered (GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps)
{
  GstRealPad *realsrc, *realsink;
  gint num_decoupled = 0;

  /* generic checks */
  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  GST_INFO (GST_CAT_PADS, "connecting %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* now we need to deal with the real/ghost stuff */
  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  if ((GST_PAD (realsrc) != srcpad) || (GST_PAD (realsink) != sinkpad)) {
    GST_INFO (GST_CAT_PADS, "*actually* connecting %s:%s and %s:%s",
              GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  }

  g_return_val_if_fail (GST_RPAD_PEER (realsrc) == NULL, FALSE);
  g_return_val_if_fail (GST_RPAD_PEER (realsink) == NULL, FALSE);
  g_return_val_if_fail (GST_PAD_PARENT (realsrc) != NULL, FALSE);
  g_return_val_if_fail (GST_PAD_PARENT (realsink) != NULL, FALSE);

  if (realsrc->sched && realsink->sched) {
    if (GST_FLAG_IS_SET (GST_PAD_PARENT (realsrc), GST_ELEMENT_DECOUPLED))
      num_decoupled++;
    if (GST_FLAG_IS_SET (GST_PAD_PARENT (realsink), GST_ELEMENT_DECOUPLED))
      num_decoupled++;

    if (realsrc->sched != realsink->sched && num_decoupled != 1) {
      g_warning ("connecting pads with different scheds requires exactly one decoupled element (queue)\n");
      return FALSE;
    }
  }

  /* check for reversed directions and swap if necessary */
  if ((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SINK) &&
      (GST_RPAD_DIRECTION (realsink) == GST_PAD_SRC)) {
    GstRealPad *temppad;

    temppad = realsrc;
    realsrc = realsink;
    realsink = temppad;
  }
  g_return_val_if_fail ((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC) &&
                        (GST_RPAD_DIRECTION (realsink) == GST_PAD_SINK), FALSE);

  /* first set peers */
  GST_RPAD_PEER (realsrc) = realsink;
  GST_RPAD_PEER (realsink) = realsrc;

  /* try to negotiate the pads, we don't need to clear the caps here */
  if (!gst_pad_try_reconnect_filtered_func (realsrc, realsink, filtercaps, FALSE)) {
    GST_DEBUG (GST_CAT_CAPS, "pads cannot connect");

    GST_RPAD_PEER (realsrc) = NULL;
    GST_RPAD_PEER (realsink) = NULL;

    return FALSE;
  }

  /* fire off a signal to each of the pads telling them that they've been connected */
  g_signal_emit (G_OBJECT (realsrc), gst_real_pad_signals[REAL_CONNECTED], 0, realsink);
  g_signal_emit (G_OBJECT (realsink), gst_real_pad_signals[REAL_CONNECTED], 0, realsrc);

  /* now tell the scheduler(s) */
  if (realsrc->sched)
    gst_scheduler_pad_connect (realsrc->sched, (GstPad *)realsrc, (GstPad *)realsink);
  else if (realsink->sched)
    gst_scheduler_pad_connect (realsink->sched, (GstPad *)realsrc, (GstPad *)realsink);

  GST_INFO (GST_CAT_PADS, "connected %s:%s and %s:%s",
            GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
  gst_caps_debug (gst_pad_get_caps (GST_PAD_CAST (realsrc)), "caps of newly connected src pad");

  return TRUE;
}

/**
 * gst_pad_connect:
 * @srcpad: the source pad to connect
 * @sinkpad: the sink pad to connect
 *
 * Connects the source pad to the sink pad.
 *
 * Returns: TRUE if the pad could be connected, FALSE otherwise
 */
gboolean
gst_pad_connect (GstPad *srcpad, GstPad *sinkpad)
{
  return gst_pad_connect_filtered (srcpad, sinkpad, NULL);
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
 * Set the scheduler for the pad
 */
void
gst_pad_set_sched (GstPad *pad, GstScheduler *sched)
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
GstScheduler*
gst_pad_get_sched (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
 
  return GST_RPAD_SCHED(pad);
}

/**
 * gst_pad_unset_sched:
 * @pad: the pad to unset the scheduler for
 *
 * Unset the scheduler for the pad
 */
void
gst_pad_unset_sched (GstPad *pad)
{
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
 
  GST_RPAD_SCHED(pad) = NULL;
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

  realpad = GST_PAD_REALIZE (pad);

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

/* an internal caps negotiation helper function does:
 * 
 * 1. optinally calls the pad connect function with the provided caps
 * 2. deal with the result code of the connect function
 * 3. set fixed caps on the pad.
 */
static GstPadConnectReturn
gst_pad_try_set_caps_func (GstRealPad *pad, GstCaps *caps, gboolean notify)
{
  GstCaps *oldcaps;
  GstPadTemplate *template;
  GstElement *parent = GST_PAD_PARENT (pad);

  /* thomas: FIXME: is this the right result to return ? */
  g_return_val_if_fail (pad != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_CONNECT_REFUSED);
  
  /* if this pad has a parent and the parent is not READY, delay the
   * negotiation */
  if (parent && GST_STATE (parent) < GST_STATE_READY)
  {
    GST_DEBUG (GST_CAT_CAPS, "parent %s of pad %s:%s is not ready",
	       GST_ELEMENT_NAME (parent), GST_DEBUG_PAD_NAME (pad));
    return GST_PAD_CONNECT_DELAYED;
  }
	  
  GST_INFO (GST_CAT_CAPS, "trying to set caps %p on pad %s:%s",
            caps, GST_DEBUG_PAD_NAME (pad));
  
  if ((template = gst_pad_get_padtemplate (GST_PAD_CAST (pad)))) {
    if (!gst_caps_intersect (caps, gst_padtemplate_get_caps (template))) {
      GST_INFO (GST_CAT_CAPS, "caps did not intersect with %s:%s's padtemplate",
                GST_DEBUG_PAD_NAME (pad));
      gst_caps_debug (caps, "caps themselves (attemped to set)");
      gst_caps_debug (gst_padtemplate_get_caps (template),
                      "pad template caps that did not agree with caps");
      return GST_PAD_CONNECT_REFUSED;
    }
    /* given that the caps are fixed, we know that their intersection with the
     * padtemplate caps is the same as caps itself */
  }

  /* we need to notify the connect function */
  if (notify && GST_RPAD_CONNECTFUNC (pad)) {
    GstPadConnectReturn res;
    gchar *debug_string;

    GST_INFO (GST_CAT_CAPS, "calling connect function on pad %s:%s",
            GST_DEBUG_PAD_NAME (pad));

    /* call the connect function */
    res = GST_RPAD_CONNECTFUNC (pad) (GST_PAD (pad), caps);

    switch (res) {
      case GST_PAD_CONNECT_REFUSED:
	debug_string = "REFUSED";
	break;
      case GST_PAD_CONNECT_OK:
	debug_string = "OK";
	break;
      case GST_PAD_CONNECT_DONE:
	debug_string = "DONE";
	break;
      case GST_PAD_CONNECT_DELAYED:
	debug_string = "DELAYED";
	break;
      default:
	g_warning ("unknown return code from connect function of pad %s:%s %d",
            GST_DEBUG_PAD_NAME (pad), res);
        return GST_PAD_CONNECT_REFUSED;
    }

    GST_INFO (GST_CAT_CAPS, "got reply %s (%d) from connect function on pad %s:%s",
            debug_string, res, GST_DEBUG_PAD_NAME (pad));

    /* done means the connect function called another caps negotiate function
     * on this pad that succeeded, we dont need to continue */
    if (res == GST_PAD_CONNECT_DONE) {
      GST_INFO (GST_CAT_CAPS, "pad %s:%s is done", GST_DEBUG_PAD_NAME (pad));
      return GST_PAD_CONNECT_DONE;
    }
    if (res == GST_PAD_CONNECT_REFUSED) {
      GST_INFO (GST_CAT_CAPS, "pad %s:%s doesn't accept caps",
		    GST_DEBUG_PAD_NAME (pad));
      return GST_PAD_CONNECT_REFUSED;
    }
  }
  /* we can only set caps on the pad if they are fixed */
  if (GST_CAPS_IS_FIXED (caps)) {

    GST_INFO (GST_CAT_CAPS, "setting caps on pad %s:%s",
              GST_DEBUG_PAD_NAME (pad));
    /* if we got this far all is ok, remove the old caps, set the new one */
    oldcaps = GST_PAD_CAPS (pad);
    if (caps) gst_caps_ref (caps);
    GST_PAD_CAPS (pad) = caps;
    if (oldcaps) gst_caps_unref (oldcaps);
  }
  else {
    GST_INFO (GST_CAT_CAPS, "caps are not fixed on pad %s:%s, not setting them yet",
              GST_DEBUG_PAD_NAME (pad));
  }

  return GST_PAD_CONNECT_OK;
}

/**
 * gst_pad_try_set_caps:
 * @pad: the pad to try to set the caps on
 * @caps: the caps to set
 *
 * Try to set the caps on the given pad.
 *
 * Returns: TRUE if the caps could be set
 */
gboolean
gst_pad_try_set_caps (GstPad *pad, GstCaps *caps)
{
  GstRealPad *peer, *realpad;

  realpad = GST_PAD_REALIZE (pad);
  peer = GST_RPAD_PEER (realpad);

  GST_INFO (GST_CAT_CAPS, "trying to set caps %p on pad %s:%s",
            caps, GST_DEBUG_PAD_NAME (realpad));

  gst_caps_debug (caps, "caps that we are trying to set");

  /* setting non fixed caps on a pad is not allowed */
  if (!GST_CAPS_IS_FIXED (caps)) {
  GST_INFO (GST_CAT_CAPS, "trying to set unfixed caps on pad %s:%s, not allowed",
		  GST_DEBUG_PAD_NAME (realpad));
    g_warning ("trying to set non fixed caps on pad %s:%s, not allowed",
            GST_DEBUG_PAD_NAME (realpad));
    gst_caps_debug (caps, "unfixed caps");
    return FALSE;
  }

  /* if we have a peer try to set the caps, notifying the peerpad
   * if it has a connect function */
  if (peer && (gst_pad_try_set_caps_func (peer, caps, TRUE) != GST_PAD_CONNECT_OK))
  {
    GST_INFO (GST_CAT_CAPS, "tried to set caps on peerpad %s:%s but couldn't",
	      GST_DEBUG_PAD_NAME (peer));
    return FALSE;
  }

  /* then try to set our own caps, we don't need to be notified */
  if (gst_pad_try_set_caps_func (realpad, caps, FALSE) != GST_PAD_CONNECT_OK)
  {
    GST_INFO (GST_CAT_CAPS, "tried to set own caps on pad %s:%s but couldn't",
	      GST_DEBUG_PAD_NAME (realpad));
    return FALSE;
  }
  GST_INFO (GST_CAT_CAPS, "succeeded setting caps %p on pad %s:%s",
	    caps, GST_DEBUG_PAD_NAME (realpad));
  g_assert (GST_PAD_CAPS (pad));
			  
  return TRUE;
}

/* this is a caps negotiation convenience routine, it performs:
 *
 * 1. optionally clear any pad caps
 * 2. calculate the intersection between the two pad tamplate/getcaps caps
 * 3. calculate the intersection with the (optional) filtercaps.
 * 4. store the intersection in the pad filter
 * 5. store the app filtercaps in the pad appfilter.
 * 6. start the caps negotiation.
 */
static gboolean
gst_pad_try_reconnect_filtered_func (GstRealPad *srcpad, GstRealPad *sinkpad, GstCaps *filtercaps, gboolean clear)
{
  GstCaps *srccaps, *sinkcaps;
  GstCaps *intersection = NULL;
  GstRealPad *realsrc, *realsink;

  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  g_return_val_if_fail (GST_RPAD_PEER (realsrc) != NULL, FALSE);
  g_return_val_if_fail (GST_RPAD_PEER (realsink) == realsrc, FALSE);

  /* optinally clear the caps */
  if (clear) {
    GST_INFO (GST_CAT_PADS, "reconnect filtered %s:%s and %s:%s, clearing caps",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));

    GST_PAD_CAPS (GST_PAD (realsrc)) = NULL;
    GST_PAD_CAPS (GST_PAD (realsink)) = NULL;
  }
  else {
    GST_INFO (GST_CAT_PADS, "reconnect filtered %s:%s and %s:%s",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  }

  srccaps = gst_pad_get_caps (GST_PAD (realsrc));
  GST_INFO (GST_CAT_PADS, "dumping caps of pad %s:%s", GST_DEBUG_PAD_NAME (realsrc));
  gst_caps_debug (srccaps, "caps of src pad (pre-reconnect)");
  sinkcaps = gst_pad_get_caps (GST_PAD (realsink));
  GST_INFO (GST_CAT_PADS, "dumping caps of pad %s:%s", GST_DEBUG_PAD_NAME (realsink));
  gst_caps_debug (sinkcaps, "caps of sink pad (pre-reconnect)");

  /* first take the intersection of the pad caps */
  intersection = gst_caps_intersect (srccaps, sinkcaps);

  /* if we have no intersection but one of the caps was not NULL.. */
  if (!intersection && (srccaps || sinkcaps)) {
    /* the intersection is NULL but the pad caps were not both NULL,
     * this means they have no common format */
    GST_INFO (GST_CAT_PADS, "pads %s:%s and %s:%s have no common type",
         GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
    return FALSE;
  } else if (intersection) {
    GST_INFO (GST_CAT_PADS, "pads %s:%s and %s:%s intersected to %s caps",
         GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink), 
	 ((intersection && GST_CAPS_IS_FIXED (intersection)) ? "fixed" : "variable"));

    /* then filter this against the app filter */
    if (filtercaps) {
      GstCaps *filtered_intersection = gst_caps_intersect (intersection, filtercaps);

      /* get rid of the old intersection here */
      gst_caps_unref (intersection);

      if (!filtered_intersection) {
        GST_INFO (GST_CAT_PADS, "filtered connection between pads %s:%s and %s:%s is empty",
             GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
        return FALSE;
      }
      intersection = filtered_intersection;

      /* keep a reference to the app caps */
      GST_RPAD_APPFILTER (realsink) = filtercaps;
      GST_RPAD_APPFILTER (realsrc) = filtercaps;
    }
  }
  GST_DEBUG (GST_CAT_CAPS, "setting filter for connection to:");
  gst_caps_debug (intersection, "filter for connection");

  /* both the app filter and the filter, while stored on both peer pads, are the
     equal to the same thing on both */
  GST_RPAD_FILTER (realsrc) = intersection; 
  GST_RPAD_FILTER (realsink) = intersection; 

  return gst_pad_perform_negotiate (GST_PAD (realsrc), GST_PAD (realsink));
}

/**
 * gst_pad_perform_negotiate:
 * @srcpad: a srcpad
 * @sinkpad: a sinkpad 
 *
 * Try to negotiate the pads.
 *
 * Returns: a boolean indicating the pad succesfully negotiated.
 */
gboolean
gst_pad_perform_negotiate (GstPad *srcpad, GstPad *sinkpad) 
{
  GstCaps *intersection, *filtered_intersection;
  GstRealPad *realsrc, *realsink;
  GstCaps *srccaps, *sinkcaps, *filter;

  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);
  
  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);
    
  g_return_val_if_fail (GST_RPAD_PEER (realsrc) != NULL, FALSE);
  g_return_val_if_fail (GST_RPAD_PEER (realsink) == realsrc, FALSE);

  filter = GST_RPAD_APPFILTER (realsrc);
  if (filter) {
    GST_INFO (GST_CAT_PADS, "dumping filter for connection %s:%s-%s:%s",
              GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
    gst_caps_debug (filter, "connection filter caps");
  }

  /* calculate the new caps here */
  srccaps = gst_pad_get_caps (GST_PAD (realsrc));
  GST_INFO (GST_CAT_PADS, "dumping caps of pad %s:%s", GST_DEBUG_PAD_NAME (realsrc));
  gst_caps_debug (srccaps, "src caps, awaiting negotiation, after applying filter");
  sinkcaps = gst_pad_get_caps (GST_PAD (realsink));
  GST_INFO (GST_CAT_PADS, "dumping caps of pad %s:%s", GST_DEBUG_PAD_NAME (realsink));
  gst_caps_debug (sinkcaps, "sink caps, awaiting negotiation, after applying filter");
  intersection = gst_caps_intersect (srccaps, sinkcaps);
  filtered_intersection = gst_caps_intersect (intersection, filter);
  if (filtered_intersection) {
    gst_caps_unref (intersection);
    intersection = filtered_intersection;
  }

  /* no negotiation is performed if the pads have filtercaps */
  if (intersection) {
    GstPadConnectReturn res;

    res = gst_pad_try_set_caps_func (realsrc, intersection, TRUE);
    if (res == GST_PAD_CONNECT_REFUSED) 
      return FALSE;
    if (res == GST_PAD_CONNECT_DONE) 
      return TRUE;

    res = gst_pad_try_set_caps_func (realsink, intersection, TRUE);
    if (res == GST_PAD_CONNECT_REFUSED) 
      return FALSE;
    if (res == GST_PAD_CONNECT_DONE) 
      return TRUE;
  }
  return TRUE;
}

/**
 * gst_pad_try_reconnect_filtered:
 * @srcpad: the source"pad to reconnect
 * @sinkpad: the sink pad to reconnect
 * @filtercaps: the capabilities to use in the reconnectiong
 *
 * Try to reconnect this pad and its peer with the specified caps
 *
 * Returns: a boolean indicating the peer pad could accept the caps.
 */
gboolean
gst_pad_try_reconnect_filtered (GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps)
{
  GstRealPad *realsrc, *realsink;

  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);

  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  g_return_val_if_fail (GST_RPAD_PEER (realsrc) != NULL, FALSE);
  g_return_val_if_fail (GST_RPAD_PEER (realsink) == realsrc, FALSE);
  
  return gst_pad_try_reconnect_filtered_func (realsrc, realsink, filtercaps, TRUE);
}

/**
 * gst_pad_reconnect_filtered:
 * @srcpad: the source"pad to reconnect
 * @sinkpad: the sink pad to reconnect
 * @filtercaps: the capabilities to use in the reconnectiong
 *
 * Try to reconnect this pad and its peer with the specified caps. 
 *
 * Returns: a boolean indicating the peer pad could accept the caps.
 *    if FALSE is returned, the pads are disconnected.
 */
gboolean
gst_pad_reconnect_filtered (GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps)
{
  GstRealPad *realsrc, *realsink;

  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);

  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  g_return_val_if_fail (GST_RPAD_PEER (realsrc) != NULL, FALSE);
  g_return_val_if_fail (GST_RPAD_PEER (realsink) == realsrc, FALSE);
  
  if (!gst_pad_try_reconnect_filtered_func (realsrc, realsink, filtercaps, TRUE)) {
    gst_pad_disconnect (srcpad, GST_PAD (GST_PAD_PEER (srcpad)));
    return FALSE;
  }
  return TRUE;
}

/**
 * gst_pad_proxy_connect:
 * @pad: the pad to proxy to
 * @caps: the capabilities to use in the proxying
 *
 * Proxy the connect function to the specified pad.
 *
 * Returns: a boolean indicating the peer pad could accept the caps.
 */
GstPadConnectReturn
gst_pad_proxy_connect (GstPad *pad, GstCaps *caps)
{
  GstRealPad *peer, *realpad;

  realpad = GST_PAD_REALIZE (pad);

  peer = GST_RPAD_PEER (realpad);

  GST_INFO (GST_CAT_CAPS, "proxy connect to pad %s:%s",
            GST_DEBUG_PAD_NAME (realpad));

  if (peer && gst_pad_try_set_caps_func (peer, caps, TRUE) < 0)
    return GST_PAD_CONNECT_REFUSED;
  if (gst_pad_try_set_caps_func (realpad, caps, FALSE) < 0)
    return GST_PAD_CONNECT_REFUSED;

  return GST_PAD_CONNECT_OK;
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
  GstRealPad *realpad;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  realpad = GST_PAD_REALIZE (pad);

  GST_DEBUG (GST_CAT_CAPS, "get pad caps of %s:%s (%p)",
            GST_DEBUG_PAD_NAME (realpad), realpad);

  if (GST_PAD_CAPS (realpad)) {
    GST_DEBUG (GST_CAT_CAPS, "using pad real caps");
    return GST_PAD_CAPS (realpad);
  }
  else if GST_RPAD_GETCAPSFUNC (realpad) {
    GST_DEBUG (GST_CAT_CAPS, "using pad get function");
    return GST_RPAD_GETCAPSFUNC (realpad) (GST_PAD_CAST (realpad), NULL);
  }
  else if (GST_PAD_PADTEMPLATE (realpad)) {
    GST_DEBUG (GST_CAT_CAPS, "using pad template");
    return GST_PADTEMPLATE_CAPS (GST_PAD_PADTEMPLATE (realpad));
  }
  GST_DEBUG (GST_CAT_CAPS, "pad has no caps");

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

  if (GST_PAD_CAPS (srcpad) && GST_PAD_CAPS (sinkpad)) {
    if (!gst_caps_check_compatibility (GST_PAD_CAPS (srcpad), GST_PAD_CAPS (sinkpad))) {
      return FALSE;
    }
    else {
      return TRUE;
    }
  }
  else {
    GST_DEBUG (GST_CAT_PADS, "could not check capabilities of pads (%s:%s) and (%s:%s) %p %p",
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

  return GST_PAD (GST_PAD_PEER (pad));
}

/**
 * gst_pad_get_allowed_caps:
 * @pad: the pad to get the allowed caps from
 *
 * Get the caps of the allowed media types that can
 * go through this pad.
 *
 * Returns: the allowed caps, newly allocated
 */
GstCaps*
gst_pad_get_allowed_caps (GstPad *pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_DEBUG (GST_CAT_PROPERTIES, "get allowed caps of %s:%s", GST_DEBUG_PAD_NAME (pad));

  return gst_caps_copy (GST_RPAD_FILTER (pad));
}

/**
 * gst_pad_recalc_allowed_caps:
 * @pad: the pad to recaculate the caps of
 *
 * Attempt to reconnect the pad to its peer through its filter, 
 * set with gst_pad_[re]connect_filtered. This function is useful when a
 * plugin has new capabilities on a pad and wants to notify the peer.
 *
 * Returns: TRUE on success, FALSE otherwise.
 */
gboolean
gst_pad_recalc_allowed_caps (GstPad *pad)
{
  GstRealPad *peer;

  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_DEBUG (GST_CAT_PROPERTIES, "set allowed caps of %s:%s", GST_DEBUG_PAD_NAME (pad));

  peer = GST_RPAD_PEER (pad);
  if (peer)
    return gst_pad_try_reconnect_filtered (pad, GST_PAD (peer), GST_RPAD_APPFILTER (pad));

  return TRUE;
}

/**
 * gst_pad_get_bufferpool:
 * @pad: the pad to get the bufferpool from
 *
 * Get the bufferpool of the peer pad of the given
 * pad.
 *
 * Returns: The GstBufferPool or NULL.
 */
GstBufferPool*          
gst_pad_get_bufferpool (GstPad *pad)
{
  GstRealPad *peer;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
   
  peer = GST_RPAD_PEER (pad);

  if (!peer)
    return NULL;

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));

  if (peer->bufferpoolfunc) {
    GST_DEBUG (GST_CAT_PADS, "calling bufferpoolfunc &%s (@%p) of peer pad %s:%s",
      GST_DEBUG_FUNCPTR_NAME (peer->bufferpoolfunc), &peer->bufferpoolfunc, GST_DEBUG_PAD_NAME (((GstPad*) peer)));
    return (peer->bufferpoolfunc) (((GstPad*) peer));
  } else {
    GST_DEBUG (GST_CAT_PADS, "no bufferpoolfunc for peer pad %s:%s at %p",
		    GST_DEBUG_PAD_NAME (((GstPad*) peer)), &peer->bufferpoolfunc);
    return NULL;
  }
}

static void
gst_real_pad_dispose (GObject *object)
{
  GstPad *pad = GST_PAD (object);
  
  /* No connected pad can ever be disposed.
   * It has to have a parent to be connected and a parent would hold a reference */
  g_assert (GST_PAD_PEER (pad) == NULL);

  GST_DEBUG (GST_CAT_REFCOUNTING, "dispose %s:%s", GST_DEBUG_PAD_NAME(pad));

  if (GST_PAD_PADTEMPLATE (pad)){
    GST_DEBUG (GST_CAT_REFCOUNTING, "unreffing padtemplate'%s'", GST_OBJECT_NAME (GST_PAD_PADTEMPLATE (pad)));
    gst_object_unref (GST_OBJECT (GST_PAD_PADTEMPLATE (pad)));
    GST_PAD_PADTEMPLATE (pad) = NULL;
  }
  
  /* we destroy the ghostpads, because they are nothing without the real pad  */
  if (GST_REAL_PAD (pad)->ghostpads) {
    GList *orig, *ghostpads;

    orig = ghostpads = g_list_copy (GST_REAL_PAD (pad)->ghostpads);

    while (ghostpads) {
      GstPad *ghostpad = GST_PAD (ghostpads->data);

      if (GST_IS_ELEMENT (GST_OBJECT_PARENT (ghostpad))){
        GST_DEBUG (GST_CAT_REFCOUNTING, "removing ghost pad from element '%s'", 
			GST_OBJECT_NAME (GST_OBJECT_PARENT (ghostpad)));

        gst_element_remove_ghost_pad (GST_ELEMENT (GST_OBJECT_PARENT (ghostpad)), GST_PAD (ghostpad));
      }
      ghostpads = g_list_next (ghostpads);
    }
    g_list_free (orig);
    g_list_free (GST_REAL_PAD(pad)->ghostpads);
  }

  if (GST_IS_ELEMENT (GST_OBJECT_PARENT (pad))){
    GST_DEBUG (GST_CAT_REFCOUNTING, "removing pad from element '%s'",
		    GST_OBJECT_NAME (GST_OBJECT (GST_ELEMENT (GST_OBJECT_PARENT (pad)))));
    
    gst_element_remove_pad (GST_ELEMENT (GST_OBJECT_PARENT (pad)), pad);
  }
  
  G_OBJECT_CLASS (real_pad_parent_class)->dispose (object);
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
      peer = xmlNodeGetContent (field);
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
    /* first check to see if the peer's parent's parent is the same */
    /* we just save it off */
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

  self = xmlNewChild (parent, NULL, "ghostpad", NULL);
  xmlNewChild (self, NULL, "name", GST_PAD_NAME (pad));
  xmlNewChild (self, NULL, "parent", GST_OBJECT_NAME (GST_PAD_PARENT (pad)));

  /* FIXME FIXME FIXME! */

  return self;
}
#endif /* GST_DISABLE_LOADSAVE */

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

  if (!peer) {
    g_warning ("push on pad %s:%s but it is unconnected", GST_DEBUG_PAD_NAME (pad));
  }
  else {
    if (peer->chainhandler) {
      if (buf) {
        GST_DEBUG (GST_CAT_DATAFLOW, "calling chainhandler &%s of peer pad %s:%s",
            GST_DEBUG_FUNCPTR_NAME (peer->chainhandler), GST_DEBUG_PAD_NAME (GST_PAD (peer)));
        (peer->chainhandler) (GST_PAD_CAST (peer), buf);
	return;
      }
      else {
        g_warning ("trying to push a NULL buffer on pad %s:%s", GST_DEBUG_PAD_NAME (peer));
	return;
      }
    } 
    else {
      g_warning ("(internal error) push on pad %s:%s but it has no chainhandler", GST_DEBUG_PAD_NAME (peer));
    }
  }
  /* clean up the mess here */
  if (buf != NULL) {
    if (GST_IS_BUFFER (buf))
      gst_buffer_unref (buf);
    else
      gst_event_free (GST_EVENT (buf));
  }
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

  if (!peer) {
    gst_element_error (GST_PAD_PARENT (pad), 
		    "pull on pad %s:%s but it was unconnected", 
		    GST_ELEMENT_NAME (GST_PAD_PARENT (pad)), GST_PAD_NAME (pad),
		    NULL);
  }
  else {
    if (peer->gethandler) {
      GstBuffer *buf;

      GST_DEBUG (GST_CAT_DATAFLOW, "calling gethandler %s of peer pad %s:%s",
        GST_DEBUG_FUNCPTR_NAME (peer->gethandler), GST_DEBUG_PAD_NAME (peer));

      buf = (peer->gethandler) (GST_PAD_CAST (peer));
      if (buf)
        return buf;
      /* no null buffers allowed */
      gst_element_error (GST_PAD_PARENT (pad), 
		    "NULL buffer during pull on %s:%s", GST_DEBUG_PAD_NAME (pad), NULL);
	  
    } else {
      gst_element_error (GST_PAD_PARENT (pad), 
		    "(internal error) pull on pad %s:%s but the peer pad %s:%s has no gethandler", 
		    GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (peer),
		    NULL);
    }
  }
  return NULL;
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
      GST_DEBUG (GST_CAT_DATAFLOW, "calling pullregionfunc &%s of peer pad %s:%s",
          GST_DEBUG_FUNCPTR_NAME (peer->pullregionfunc), GST_DEBUG_PAD_NAME(GST_PAD_CAST (peer)));
      result = (peer->pullregionfunc) (GST_PAD_CAST (peer), type, offset, len);
    } else {
      GST_DEBUG (GST_CAT_DATAFLOW,"no pullregionfunc");
      result = NULL;
      break;
    }
  }
  /* FIXME */
  while (result && !(GST_BUFFER_OFFSET (result) == offset && 
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

  pad = gst_scheduler_pad_select (GST_PAD_PARENT (padlist->data)->sched, padlist);

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
      NULL
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
    g_signal_new ("pad_created", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstPadTemplateClass, pad_created), NULL, NULL,
                    gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);


  gstobject_class->path_string_separator = "*";
}

static void
gst_padtemplate_init (GstPadTemplate *templ)
{
}

/* ALWAYS padtemplates cannot have conversion specifications, it doesn't make
 * sense.
 * SOMETIMES padtemplates can do whatever they want, they are provided by the
 * element.
 * REQUEST padtemplates can be reverse-parsed (the user asks for 'sink1', the
 * 'sink%d' template is automatically selected), so we need to restrict their
 * naming.
 */
static gboolean
name_is_valid (const gchar *name, GstPadPresence presence)
{
  const gchar *str;
  
  if (presence == GST_PAD_ALWAYS) {
    if (strchr (name, '%')) {
      g_warning ("invalid name template %s: conversion specifications are not"
                 " allowed for GST_PAD_ALWAYS padtemplates", name);
      return FALSE;
    }
  } else if (presence == GST_PAD_REQUEST) {
    if ((str = strchr (name, '%')) && strchr (str + 1, '%')) {
      g_warning ("invalid name template %s: only one conversion specification"
                 " allowed in GST_PAD_REQUEST padtemplate", name);
      return FALSE;
    }
    if (str && (*(str+1) != 's' && *(str+1) != 'd')) {
      g_warning ("invalid name template %s: conversion specification must be of"
                 " type '%%d' or '%%s' for GST_PAD_REQUEST padtemplate", name);
      return FALSE;
    }
    if (str && (*(str+2) != '\0')) {
      g_warning ("invalid name template %s: conversion specification must appear"
                 " at the end of the GST_PAD_REQUEST padtemplate name", name);
      return FALSE;
    }
  }
  
  return TRUE;
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

  if (!name_is_valid (name_template, presence))
    return NULL;

  new = g_object_new(gst_padtemplate_get_type () ,NULL);

  GST_PADTEMPLATE_NAME_TEMPLATE (new) = name_template;
  GST_PADTEMPLATE_DIRECTION (new) = direction;
  GST_PADTEMPLATE_PRESENCE (new) = presence;

  va_start (var_args, caps);

  while (caps) {
    new->fixed &= caps->fixed;
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

#ifndef GST_DISABLE_LOADSAVE
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

  GST_DEBUG (GST_CAT_XML,"saving padtemplate %s", templ->name_template);

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
#endif /* !GST_DISABLE_LOADSAVE */


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
GType _gst_ghost_pad_type = 0;

static void     gst_ghost_pad_class_init         (GstGhostPadClass *klass);
static void     gst_ghost_pad_init               (GstGhostPad *pad);

static GstPad *ghost_pad_parent_class = NULL;
/* static guint gst_ghost_pad_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_ghost_pad_get_type (void) 
{
  if (!_gst_ghost_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstGhostPadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ghost_pad_class_init,
      NULL,
      NULL,
      sizeof(GstGhostPad),
      8,
      (GInstanceInitFunc) gst_ghost_pad_init,
      NULL
    };
    _gst_ghost_pad_type = g_type_register_static (GST_TYPE_PAD, "GstGhostPad", &pad_info, 0);
  }
  return _gst_ghost_pad_type;
}

static void
gst_ghost_pad_class_init (GstGhostPadClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*) klass;

  ghost_pad_parent_class = g_type_class_ref (GST_TYPE_PAD);
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
  GstRealPad *realpad;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  ghostpad = g_object_new (gst_ghost_pad_get_type () ,NULL);
  gst_pad_set_name (GST_PAD (ghostpad), name);

  realpad = (GstRealPad *) pad;

  while (!GST_IS_REAL_PAD (realpad)) {
    realpad = GST_PAD_REALIZE (realpad);
  }
  GST_GPAD_REALPAD (ghostpad) = realpad;
  GST_PAD_PADTEMPLATE (ghostpad) = GST_PAD_PADTEMPLATE (pad);

  /* add ourselves to the real pad's list of ghostpads */
  gst_pad_add_ghost_pad (pad, GST_PAD (ghostpad));

  /* FIXME need to ref the real pad here... ? */

  GST_DEBUG (GST_CAT_PADS, "created ghost pad \"%s\"", name);

  return GST_PAD (ghostpad);
}

static void 
gst_pad_event_default_dispatch (GstPad *pad, GstElement *element, GstEvent *event)
{
  GList *pads = element->pads;

  while (pads) {
    GstPad *eventpad = GST_PAD (pads->data);
    pads = g_list_next (pads);

    /* for all pads in the opposite direction that are connected */
    if (GST_PAD_DIRECTION (eventpad) != GST_PAD_DIRECTION (pad) && GST_PAD_IS_CONNECTED (eventpad)) {
      if (GST_PAD_DIRECTION (eventpad) == GST_PAD_SRC) {
        gst_pad_push (eventpad, GST_BUFFER (gst_event_copy (event)));
      }
      else {
	GstPad *peerpad = GST_PAD_CAST (GST_RPAD_PEER (eventpad));

        gst_pad_send_event (peerpad, gst_event_copy (event));
      }
    }
  }
}

/**
 * gst_pad_event_default:
 * @pad: the pad to operate on
 * @event: the event to handle
 *
 * Invoke the default event handler for the given pad.
 */
void 
gst_pad_event_default (GstPad *pad, GstEvent *event)
{
  GstElement *element = GST_PAD_PARENT (pad);

  g_signal_emit (G_OBJECT (pad), gst_real_pad_signals[REAL_EVENT_RECEIVED], 0, event);
 
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_element_set_eos (element);
      gst_pad_event_default_dispatch (pad, element, event);
      /* we have to try to schedule another element because this one is disabled */
      gst_element_yield (element);
      break;
    case GST_EVENT_FLUSH:
    default:
      gst_pad_event_default_dispatch (pad, element, event);
      break;
  }
}

/**
 * gst_pad_send_event:
 * @pad: the pad to send the event to
 * @event: the event to send to the pad.
 *
 * Send the event to the pad.
 *
 * Returns: TRUE if the event was handled.
 */
gboolean
gst_pad_send_event (GstPad *pad, GstEvent *event)
{
  gboolean handled = FALSE;

  g_return_val_if_fail (event, FALSE);

  if (GST_EVENT_SRC (event) == NULL)
    GST_EVENT_SRC (event) = gst_object_ref (GST_OBJECT (pad));

  GST_DEBUG (GST_CAT_EVENT, "have event %d on pad %s:%s",
		  GST_EVENT_TYPE (event), GST_DEBUG_PAD_NAME (pad));

  if (GST_RPAD_EVENTFUNC (pad))
    handled = GST_RPAD_EVENTFUNC (pad) (pad, event);
  else {
    GST_DEBUG(GST_CAT_EVENT, "there's no event function for pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  }

  if (!handled) {
    GST_DEBUG(GST_CAT_EVENT, "proceeding with default event behavior here");
    gst_pad_event_default (pad, event);
    handled = TRUE;
  }

  return handled;
}

