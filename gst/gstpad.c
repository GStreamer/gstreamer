/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpad.c: Pads for linking elements together
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

#include "gst_private.h"

#include "gstpad.h"
#include "gstmarshal.h"
#include "gstutils.h"
#include "gstelement.h"
#include "gstpipeline.h"
#include "gstbin.h"
#include "gstscheduler.h"
#include "gstevent.h"
#include "gstinfo.h"
#include "gsterror.h"
#include "gstvalue.h"

GST_DEBUG_CATEGORY_STATIC (debug_dataflow);
#define DEBUG_DATA(obj,data,notice) G_STMT_START{\
  if (!data) { \
    GST_CAT_DEBUG_OBJECT (debug_dataflow, obj, "NULL data value"); \
  } else if (GST_IS_EVENT (data)) { \
    GST_CAT_DEBUG_OBJECT (debug_dataflow, obj, "%s event %p (type %d, refcount %d)", notice, data, \
	GST_EVENT_TYPE (data), GST_DATA_REFCOUNT_VALUE (data)); \
  } else { \
    GST_CAT_LOG_OBJECT (debug_dataflow, obj, "%s buffer %p (size %u, refcount %d)", notice, data, \
	GST_BUFFER_SIZE (data), GST_BUFFER_REFCOUNT_VALUE (data)); \
  } \
}G_STMT_END
#define GST_CAT_DEFAULT GST_CAT_PADS

/* realize and pad and grab the lock of the realized pad. */
#define GST_PAD_REALIZE_AND_LOCK(pad, realpad, lost_ghostpad) 	\
  GST_LOCK (pad);						\
  realpad = GST_PAD_REALIZE (pad);				\
  if (G_UNLIKELY (realpad == NULL)) {				\
    GST_UNLOCK (pad);						\
    goto lost_ghostpad;						\
  }								\
  if (G_UNLIKELY (pad != GST_PAD_CAST (realpad))) {		\
    GST_LOCK (realpad);						\
    GST_UNLOCK (pad);						\
  }

enum
{
  TEMPL_PAD_CREATED,
  /* FILL ME */
  TEMPL_LAST_SIGNAL
};

static GstObject *padtemplate_parent_class = NULL;
static guint gst_pad_template_signals[TEMPL_LAST_SIGNAL] = { 0 };

GType _gst_pad_type = 0;

/***** Start with the base GstPad class *****/
static void gst_pad_class_init (GstPadClass * klass);
static void gst_pad_init (GstPad * pad);
static void gst_pad_dispose (GObject * object);

static void gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_pad_save_thyself (GstObject * object, xmlNodePtr parent);
#endif

static GstObject *pad_parent_class = NULL;

GType
gst_pad_get_type (void)
{
  if (!_gst_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstPadClass), NULL, NULL,
      (GClassInitFunc) gst_pad_class_init, NULL, NULL,
      sizeof (GstPad),
      0,
      (GInstanceInitFunc) gst_pad_init, NULL
    };

    _gst_pad_type = g_type_register_static (GST_TYPE_OBJECT, "GstPad",
        &pad_info, 0);

    GST_DEBUG_CATEGORY_INIT (debug_dataflow, "GST_DATAFLOW",
        GST_DEBUG_BOLD | GST_DEBUG_FG_GREEN, "dataflow inside pads");
  }
  return _gst_pad_type;
}

static void
gst_pad_class_init (GstPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  pad_parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pad_dispose);
}

static void
gst_pad_init (GstPad * pad)
{
  /* all structs are initialized to NULL by glib */
}
static void
gst_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  gst_pad_set_pad_template (pad, NULL);
  /* FIXME, we have links to many other things like caps
   * and the peer pad... */

  G_OBJECT_CLASS (pad_parent_class)->dispose (object);
}



/***** Then do the Real Pad *****/
/* Pad signals and args */
enum
{
  REAL_LINKED,
  REAL_UNLINKED,
  REAL_REQUEST_LINK,
  /* FILL ME */
  REAL_LAST_SIGNAL
};

enum
{
  REAL_ARG_0,
  REAL_ARG_CAPS,
  REAL_ARG_ACTIVE
      /* FILL ME */
};

static void gst_real_pad_class_init (GstRealPadClass * klass);
static void gst_real_pad_init (GstRealPad * pad);
static void gst_real_pad_dispose (GObject * object);
static void gst_real_pad_finalize (GObject * object);

static void gst_real_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_real_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_real_pad_get_caps_unlocked (GstRealPad * realpad);

GType _gst_real_pad_type = 0;

static GstPad *real_pad_parent_class = NULL;
static guint gst_real_pad_signals[REAL_LAST_SIGNAL] = { 0 };

GType
gst_real_pad_get_type (void)
{
  if (!_gst_real_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstRealPadClass), NULL, NULL,
      (GClassInitFunc) gst_real_pad_class_init, NULL, NULL,
      sizeof (GstRealPad),
      0,
      (GInstanceInitFunc) gst_real_pad_init, NULL
    };

    _gst_real_pad_type = g_type_register_static (GST_TYPE_PAD, "GstRealPad",
        &pad_info, 0);
  }
  return _gst_real_pad_type;
}

static void
gst_real_pad_class_init (GstRealPadClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  real_pad_parent_class = g_type_class_ref (GST_TYPE_PAD);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_real_pad_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_real_pad_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_real_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_real_pad_get_property);

  gst_real_pad_signals[REAL_LINKED] =
      g_signal_new ("linked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRealPadClass, linked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  gst_real_pad_signals[REAL_UNLINKED] =
      g_signal_new ("unlinked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRealPadClass, unlinked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  gst_real_pad_signals[REAL_REQUEST_LINK] =
      g_signal_new ("request_link", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRealPadClass, request_link), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 0);

  g_object_class_install_property (G_OBJECT_CLASS (klass), REAL_ARG_ACTIVE,
      g_param_spec_boolean ("active", "Active", "Whether the pad is active.",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), REAL_ARG_CAPS,
      g_param_spec_boxed ("caps", "Caps", "The capabilities of the pad",
          GST_TYPE_CAPS, G_PARAM_READABLE));

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_pad_save_thyself);
#endif
  gstobject_class->path_string_separator = ".";
}

static void
gst_real_pad_init (GstRealPad * pad)
{
  pad->direction = GST_PAD_UNKNOWN;
  pad->peer = NULL;

  pad->chainfunc = NULL;

  pad->ghostpads = NULL;
  pad->caps = NULL;

  pad->linkfunc = NULL;
  pad->getcapsfunc = NULL;

  pad->eventfunc = gst_pad_event_default;
  pad->convertfunc = gst_pad_convert_default;
  pad->queryfunc = gst_pad_query_default;
  pad->intlinkfunc = gst_pad_get_internal_links_default;

  pad->eventmaskfunc = gst_pad_get_event_masks_default;
  pad->formatsfunc = gst_pad_get_formats_default;
  pad->querytypefunc = gst_pad_get_query_types_default;

  GST_FLAG_UNSET (pad, GST_PAD_ACTIVE);

  pad->preroll_lock = g_mutex_new ();
  pad->preroll_cond = g_cond_new ();

  pad->stream_rec_lock = g_new (GStaticRecMutex, 1);
  g_static_rec_mutex_init (pad->stream_rec_lock);

  pad->block_cond = g_cond_new ();

  gst_probe_dispatcher_init (&pad->probedisp);
}

static void
gst_real_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case REAL_ARG_ACTIVE:
      g_warning ("FIXME: not useful any more!!!");
      gst_pad_set_active (GST_PAD (object), g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_real_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case REAL_ARG_ACTIVE:
      g_value_set_boolean (value, GST_FLAG_IS_SET (object, GST_PAD_ACTIVE));
      break;
    case REAL_ARG_CAPS:
      g_value_set_boxed (value, GST_PAD_CAPS (GST_REAL_PAD (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* FIXME-0.9: Replace these custom functions with proper inheritance via _init
   functions and object properties. update: probably later in the cycle. */
/**
 * gst_pad_custom_new:
 * @type: the #Gtype of the pad.
 * @name: the name of the new pad.
 * @direction: the #GstPadDirection of the pad.
 *
 * Creates a new pad with the given name and type in the given direction.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned. 
 * This function makes a copy of the name so you can safely free the name.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 *
 * MT safe.
 */
GstPad *
gst_pad_custom_new (GType type, const gchar * name, GstPadDirection direction)
{
  GstRealPad *pad;

  pad = g_object_new (type, NULL);
  gst_object_set_name (GST_OBJECT (pad), name);
  GST_RPAD_DIRECTION (pad) = direction;

  return GST_PAD_CAST (pad);
}

/**
 * gst_pad_new:
 * @name: the name of the new pad.
 * @direction: the #GstPadDirection of the pad.
 *
 * Creates a new real pad with the given name in the given direction.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 * This function makes a copy of the name so you can safely free the name.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 *
 * MT safe.
 */
GstPad *
gst_pad_new (const gchar * name, GstPadDirection direction)
{
  return gst_pad_custom_new (gst_real_pad_get_type (), name, direction);
}

/**
 * gst_pad_custom_new_from_template:
 * @type: the custom #GType of the pad.
 * @templ: the #GstPadTemplate to instantiate from.
 * @name: the name of the new pad.
 *
 * Creates a new custom pad with the given name from the given template.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 * This function makes a copy of the name so you can safely free the name.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_custom_new_from_template (GType type, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *pad;

  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);

  pad = gst_pad_custom_new (type, name, templ->direction);
  gst_pad_set_pad_template (pad, templ);

  return pad;
}

/**
 * gst_pad_new_from_template:
 * @templ: the pad template to use
 * @name: the name of the element
 *
 * Creates a new real pad with the given name from the given template.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 * This function makes a copy of the name so you can safely free the name.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_new_from_template (GstPadTemplate * templ, const gchar * name)
{
  return gst_pad_custom_new_from_template (gst_real_pad_get_type (),
      templ, name);
}

/**
 * gst_pad_get_direction:
 * @pad: a #GstPad to get the direction of.
 *
 * Gets the direction of the pad. The direction of the pad is
 * decided at construction time so this function does not take 
 * the LOCK.
 *
 * Returns: the #GstPadDirection of the pad.
 *
 * MT safe.
 */
GstPadDirection
gst_pad_get_direction (GstPad * pad)
{
  GstPadDirection result;
  GstRealPad *realpad;

  /* PAD_UNKNOWN is a little silly but we need some sort of
   * error return value */
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_UNKNOWN);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);
  result = GST_RPAD_DIRECTION (realpad);
  GST_UNLOCK (realpad);

  return result;

  /* errors */
lost_ghostpad:
  {
    return GST_PAD_UNKNOWN;
  }
}

/**
 * gst_pad_set_active:
 * @pad: the #GstPad to activate or deactivate.
 * @active: TRUE to activate the pad.
 *
 * Activates or deactivates the given pad.
 *
 * Returns: TRUE if the operation was successfull.
 *
 * MT safe.
 */
gboolean
gst_pad_set_active (GstPad * pad, GstActivateMode mode)
{
  /* implement me */
  return FALSE;
}


/**
 * gst_pad_is_active:
 * @pad: the #GstPad to query
 *
 * Query if a pad is active
 *
 * Returns: TRUE if the pad is active.
 *
 * MT safe.
 */
gboolean
gst_pad_is_active (GstPad * pad)
{
  gboolean result = FALSE;
  GstRealPad *realpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);
  result = GST_FLAG_IS_SET (realpad, GST_PAD_ACTIVE);
  GST_UNLOCK (realpad);

  return result;

lost_ghostpad:
  {
    return FALSE;
  }
}

/**
 * gst_pad_set_blocked_async:
 * @pad: the #GstPad to block or unblock
 * @blocked: boolean indicating we should block or unblock
 * @callback: #GstPadBlockCallback that will be called when the
 *            operation succeeds.
 * @user_data: user data passed to the callback
 *
 * Blocks or unblocks the dataflow on a pad. The provided callback
 * is called when the operation succeeds. This can take a while as
 * the pad can only become blocked when real dataflow is happening.
 * When the pipeline is stalled, for example in PAUSED, this can
 * take an indeterminate amount of time.
 * You can pass NULL as the callback to make this call block. Be
 * carefull with this blocking call as it might not return for
 * reasons stated above.
 *
 * Returns: TRUE if the pad could be blocked. This function can fail
 *   if wrong parameters were passed or the pad was already in the 
 *   requested state.
 *
 * MT safe.
 */
gboolean
gst_pad_set_blocked_async (GstPad * pad, gboolean blocked,
    GstPadBlockCallback callback, gpointer user_data)
{
  gboolean was_blocked;
  GstRealPad *realpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);

  was_blocked = GST_RPAD_IS_BLOCKED (realpad);

  if (G_UNLIKELY (was_blocked == blocked))
    goto had_right_state;

  if (blocked) {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, realpad, "blocking pad %s:%s",
        GST_DEBUG_PAD_NAME (realpad));

    GST_FLAG_SET (realpad, GST_PAD_BLOCKED);
    realpad->block_callback = callback;
    realpad->block_data = user_data;
    if (!callback) {
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, realpad, "waiting for block");
      GST_PAD_BLOCK_WAIT (realpad);
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, realpad, "blocked");
    }
  } else {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, realpad, "unblocking pad %s:%s",
        GST_DEBUG_PAD_NAME (realpad));

    GST_FLAG_UNSET (realpad, GST_PAD_BLOCKED);

    realpad->block_callback = callback;
    realpad->block_data = user_data;

    if (callback) {
      GST_PAD_BLOCK_SIGNAL (realpad);
    } else {
      GST_PAD_BLOCK_SIGNAL (realpad);
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, realpad, "waiting for unblock");
      GST_PAD_BLOCK_WAIT (realpad);
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, realpad, "unblocked");
    }
  }
  GST_UNLOCK (realpad);

  return TRUE;

lost_ghostpad:
  {
    return FALSE;
  }
had_right_state:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, realpad,
        "pad %s:%s was in right state", GST_DEBUG_PAD_NAME (realpad));
    GST_UNLOCK (realpad);
    return FALSE;
  }
}

/**
 * gst_pad_set_blocked:
 * @pad: the #GstPad to block or unblock
 * @blocked: boolean indicating we should block or unblock
 *
 * Blocks or unblocks the dataflow on a pad. This function is
 * a shortcut for @gst_pad_set_blocked_async() with a NULL
 * callback.
 *
 * Returns: TRUE if the pad could be blocked. This function can fail
 *   wrong parameters were passed or the pad was already in the 
 *   requested state.
 *
 * MT safe.
 */
gboolean
gst_pad_set_blocked (GstPad * pad, gboolean blocked)
{
  return gst_pad_set_blocked_async (pad, blocked, NULL, NULL);
}

/**
 * gst_pad_is_blocked:
 * @pad: the #GstPad to query 
 *
 * Checks if the pad is blocked or not. This function returns the
 * last requested state of the pad. It is not certain that the pad
 * is actually blocked at this point.
 *
 * Returns: TRUE if the pad is blocked.
 *
 * MT safe.
 */
gboolean
gst_pad_is_blocked (GstPad * pad)
{
  gboolean result = FALSE;
  GstRealPad *realpad;

  g_return_val_if_fail (GST_IS_PAD (pad), result);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);
  result = GST_FLAG_IS_SET (realpad, GST_PAD_BLOCKED);
  GST_UNLOCK (realpad);

  return result;

lost_ghostpad:
  {
    return FALSE;
  }
}

/**
 * gst_pad_set_activate_function:
 * @pad: a real sink #GstPad.
 * @chain: the #GstPadActivateFunction to set.
 *
 * Sets the given activate function for the pad. The activate function is called to
 * start or stop dataflow on a pad.
 */
void
gst_pad_set_activate_function (GstPad * pad, GstPadActivateFunction activate)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_ACTIVATEFUNC (pad) = activate;
  GST_CAT_DEBUG (GST_CAT_PADS, "activatefunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (activate));
}

/**
 * gst_pad_set_loop_function:
 * @pad: a real sink #GstPad.
 * @chain: the #GstPadLoopFunction to set.
 *
 * Sets the given loop function for the pad. The loop function is called 
 * repeadedly to pull/push buffers from/to the peer pad.
 */
void
gst_pad_set_loop_function (GstPad * pad, GstPadLoopFunction loop)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_LOOPFUNC (pad) = loop;
  GST_CAT_DEBUG (GST_CAT_PADS, "loopfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (loop));
}

/**
 * gst_pad_set_chain_function:
 * @pad: a real sink #GstPad.
 * @chain: the #GstPadChainFunction to set.
 *
 * Sets the given chain function for the pad. The chain function is called to
 * process a #GstBuffer input buffer.
 */
void
gst_pad_set_chain_function (GstPad * pad, GstPadChainFunction chain)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_RPAD_DIRECTION (pad) == GST_PAD_SINK);

  GST_RPAD_CHAINFUNC (pad) = chain;
  GST_CAT_DEBUG (GST_CAT_PADS, "chainfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (chain));
}

/**
 * gst_pad_set_getrange_function:
 * @pad: a real source #GstPad.
 * @get: the #GstPadGetRangeFunction to set.
 *
 * Sets the given getrange function for the pad. The getrange function is called to
 * produce a new #GstBuffer to start the processing pipeline. Getrange functions cannot
 * return %NULL.
 */
void
gst_pad_set_getrange_function (GstPad * pad, GstPadGetRangeFunction get)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_RPAD_DIRECTION (pad) == GST_PAD_SRC);

  GST_RPAD_GETRANGEFUNC (pad) = get;

  GST_CAT_DEBUG (GST_CAT_PADS, "getrangefunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (get));
}

/**
 * gst_pad_set_event_function:
 * @pad: a real source #GstPad.
 * @event: the #GstPadEventFunction to set.
 *
 * Sets the given event handler for the pad.
 */
void
gst_pad_set_event_function (GstPad * pad, GstPadEventFunction event)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_EVENTFUNC (pad) = event;

  GST_CAT_DEBUG (GST_CAT_PADS, "eventfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (event));
}

/**
 * gst_pad_set_event_mask_function:
 * @pad: a real #GstPad of either direction.
 * @mask_func: the #GstPadEventMaskFunction to set.
 *
 * Sets the given event mask function for the pad.
 */
void
gst_pad_set_event_mask_function (GstPad * pad,
    GstPadEventMaskFunction mask_func)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_EVENTMASKFUNC (pad) = mask_func;

  GST_CAT_DEBUG (GST_CAT_PADS, "eventmaskfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (mask_func));
}

/**
 * gst_pad_get_event_masks:
 * @pad: a #GstPad.
 *
 * Gets the array of eventmasks from the given pad.
 *
 * Returns: a zero-terminated array of #GstEventMask, or NULL if the pad does
 * not have an event mask function.
 */
const GstEventMask *
gst_pad_get_event_masks (GstPad * pad)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  g_return_val_if_fail (rpad, NULL);

  if (GST_RPAD_EVENTMASKFUNC (rpad))
    return GST_RPAD_EVENTMASKFUNC (rpad) (GST_PAD (pad));

  return NULL;
}

static gboolean
gst_pad_get_event_masks_dispatcher (GstPad * pad, const GstEventMask ** data)
{
  *data = gst_pad_get_event_masks (pad);

  return TRUE;
}

/**
 * gst_pad_get_event_masks_default:
 * @pad: a #GstPad.
 *
 * Invokes the default event masks dispatcher on the pad.
 *
 * Returns: a zero-terminated array of #GstEventMask, or NULL if none of the
 * internally-linked pads have an event mask function.
 */
const GstEventMask *
gst_pad_get_event_masks_default (GstPad * pad)
{
  GstEventMask *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_get_event_masks_dispatcher, &result);

  return result;
}

/**
 * gst_pad_set_convert_function:
 * @pad: a real #GstPad of either direction.
 * @convert: the #GstPadConvertFunction to set.
 *
 * Sets the given convert function for the pad.
 */
void
gst_pad_set_convert_function (GstPad * pad, GstPadConvertFunction convert)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_CONVERTFUNC (pad) = convert;

  GST_CAT_DEBUG (GST_CAT_PADS, "convertfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (convert));
}

/**
 * gst_pad_set_query_function:
 * @pad: a real #GstPad of either direction.
 * @query: the #GstPadQueryFunction to set.
 *
 * Set the given query function for the pad.
 */
void
gst_pad_set_query_function (GstPad * pad, GstPadQueryFunction query)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_QUERYFUNC (pad) = query;

  GST_CAT_DEBUG (GST_CAT_PADS, "queryfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (query));
}

/**
 * gst_pad_set_query_type_function:
 * @pad: a real #GstPad of either direction.
 * @type_func: the #GstPadQueryTypeFunction to set.
 *
 * Set the given query type function for the pad.
 */
void
gst_pad_set_query_type_function (GstPad * pad,
    GstPadQueryTypeFunction type_func)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_QUERYTYPEFUNC (pad) = type_func;

  GST_CAT_DEBUG (GST_CAT_PADS, "querytypefunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (type_func));
}

/**
 * gst_pad_get_query_types:
 * @pad: a #GstPad.
 *
 * Get an array of supported queries that can be performed
 * on this pad.
 *
 * Returns: a zero-terminated array of #GstQueryType.
 */
const GstQueryType *
gst_pad_get_query_types (GstPad * pad)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  g_return_val_if_fail (rpad, NULL);

  if (GST_RPAD_QUERYTYPEFUNC (rpad))
    return GST_RPAD_QUERYTYPEFUNC (rpad) (GST_PAD (pad));

  return NULL;
}

static gboolean
gst_pad_get_query_types_dispatcher (GstPad * pad, const GstQueryType ** data)
{
  *data = gst_pad_get_query_types (pad);

  return TRUE;
}

/**
 * gst_pad_get_query_types_default:
 * @pad: a #GstPad.
 *
 * Invoke the default dispatcher for the query types on
 * the pad.
 *
 * Returns: an zero-terminated array of #GstQueryType, or NULL if none of the
 * internally-linked pads has a query types function.
 */
const GstQueryType *
gst_pad_get_query_types_default (GstPad * pad)
{
  GstQueryType *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_get_query_types_dispatcher, &result);

  return result;
}

/**
 * gst_pad_set_internal_link_function:
 * @pad: a real #GstPad of either direction.
 * @intlink: the #GstPadIntLinkFunction to set.
 *
 * Sets the given internal link function for the pad.
 */
void
gst_pad_set_internal_link_function (GstPad * pad, GstPadIntLinkFunction intlink)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_INTLINKFUNC (pad) = intlink;
  GST_CAT_DEBUG (GST_CAT_PADS, "internal link for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (intlink));
}

/**
 * gst_pad_set_formats_function:
 * @pad: a real #GstPad of either direction.
 * @formats: the #GstPadFormatsFunction to set.
 *
 * Sets the given formats function for the pad.
 */
void
gst_pad_set_formats_function (GstPad * pad, GstPadFormatsFunction formats)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_FORMATSFUNC (pad) = formats;
  GST_CAT_DEBUG (GST_CAT_PADS, "formats function for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (formats));
}

/**
 * gst_pad_set_link_function:
 * @pad: a real #GstPad.
 * @link: the #GstPadLinkFunction to set.
 * 
 * Sets the given link function for the pad. It will be called when the pad is
 * linked or relinked with caps. The caps passed to the link function is
 * the filtered caps for the connnection. It can contain a non fixed caps.
 * 
 * The return value GST_PAD_LINK_OK should be used when the connection can be
 * made.
 * 
 * The return value GST_PAD_LINK_REFUSED should be used when the connection
 * cannot be made for some reason.
 */
void
gst_pad_set_link_function (GstPad * pad, GstPadLinkFunction link)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_LINKFUNC (pad) = link;
  GST_CAT_DEBUG (GST_CAT_PADS, "linkfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (link));
}

/**
 * gst_pad_set_unlink_function:
 * @pad: a real #GstPad.
 * @unlink: the #GstPadUnlinkFunction to set.
 *
 * Sets the given unlink function for the pad. It will be called
 * when the pad is unlinked.
 */
void
gst_pad_set_unlink_function (GstPad * pad, GstPadUnlinkFunction unlink)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_UNLINKFUNC (pad) = unlink;
  GST_CAT_DEBUG (GST_CAT_PADS, "unlinkfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (unlink));
}

/**
 * gst_pad_set_getcaps_function:
 * @pad: a real #GstPad.
 * @getcaps: the #GstPadGetCapsFunction to set.
 * 
 * Sets the given getcaps function for the pad. @getcaps should return the
 * allowable caps for a pad in the context of the element's state, its link to
 * other elements, and the devices or files it has opened. These caps must be a
 * subset of the pad template caps. In the NULL state with no links, @getcaps
 * should ideally return the same caps as the pad template. In rare
 * circumstances, an object property can affect the caps returned by @getcaps,
 * but this is discouraged.
 *
 * You do not need to call this function if @pad's allowed caps are always the
 * same as the pad template caps. This can only be true if the padtemplate 
 * has fixed simple caps.
 *
 * For most filters, the caps returned by @getcaps is directly affected by the
 * allowed caps on other pads. For demuxers and decoders, the caps returned by
 * the srcpad's getcaps function is directly related to the stream data. Again,
 * @getcaps should return the most specific caps it reasonably can, since this
 * helps with autoplugging. 
 *
 * Note that the return value from @getcaps is owned by the caller, so the caller
 * should unref the caps after usage.
 */
void
gst_pad_set_getcaps_function (GstPad * pad, GstPadGetCapsFunction getcaps)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_GETCAPSFUNC (pad) = getcaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "getcapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (getcaps));
}

/**
 * gst_pad_set_acceptcaps_function:
 * @pad: a real #GstPad.
 * @acceptcaps: the #GstPadAcceptCapsFunction to set.
 *
 * Sets the given acceptcaps function for the pad.  The acceptcaps function
 * will be called to check if the pad can accept the given caps.
 */
void
gst_pad_set_acceptcaps_function (GstPad * pad,
    GstPadAcceptCapsFunction acceptcaps)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_ACCEPTCAPSFUNC (pad) = acceptcaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "acceptcapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (acceptcaps));
}

/**
 * gst_pad_set_fixatecaps_function:
 * @pad: a real #GstPad.
 * @fixatecaps: the #GstPadFixateCapsFunction to set.
 *
 * Sets the given fixatecaps function for the pad.  The fixatecaps function
 * will be called whenever the default values for a GstCaps needs to be
 * filled in.
 */
void
gst_pad_set_fixatecaps_function (GstPad * pad,
    GstPadFixateCapsFunction fixatecaps)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_FIXATECAPSFUNC (pad) = fixatecaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "fixatecapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (fixatecaps));
}

/**
 * gst_pad_set_setcaps_function:
 * @pad: a real #GstPad.
 * @setcaps: the #GstPadSetCapsFunction to set.
 *
 * Sets the given setcaps function for the pad.  The setcaps function
 * will be called whenever a buffer with a new media type is pushed or
 * pulled from the pad. The pad/element needs to update it's internal
 * structures to process the new media type. If this new type is not
 * acceptable, the setcaps function should return FALSE.
 */
void
gst_pad_set_setcaps_function (GstPad * pad, GstPadSetCapsFunction setcaps)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_SETCAPSFUNC (pad) = setcaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "setcapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (setcaps));
}

/**
 * gst_pad_set_bufferalloc_function:
 * @pad: a real sink #GstPad.
 * @bufalloc: the #GstPadBufferAllocFunction to set.
 *
 * Sets the given bufferalloc function for the pad. Note that the
 * bufferalloc function can only be set on sinkpads.
 */
void
gst_pad_set_bufferalloc_function (GstPad * pad,
    GstPadBufferAllocFunction bufalloc)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_PAD_IS_SINK (pad));

  GST_RPAD_BUFFERALLOCFUNC (pad) = bufalloc;
  GST_CAT_DEBUG (GST_CAT_PADS, "bufferallocfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (bufalloc));
}

/**
 * gst_pad_unlink:
 * @srcpad: the source #GstPad to unlink.
 * @sinkpad: the sink #GstPad to unlink.
 *
 * Unlinks the source pad from the sink pad. Will emit the "unlinked" signal on
 * both pads.
 *
 * Returns: TRUE if the pads were unlinked. This function returns FALSE if
 * the pads were not linked together.
 *
 * MT safe.
 */
gboolean
gst_pad_unlink (GstPad * srcpad, GstPad * sinkpad)
{
  GstRealPad *realsrc, *realsink;

  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinking %s:%s(%p) and %s:%s(%p)",
      GST_DEBUG_PAD_NAME (srcpad), srcpad,
      GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

  GST_PAD_REALIZE_AND_LOCK (srcpad, realsrc, lost_src_ghostpad);

  if (G_UNLIKELY (GST_RPAD_DIRECTION (realsrc) != GST_PAD_SRC))
    goto not_srcpad;

  GST_PAD_REALIZE_AND_LOCK (sinkpad, realsink, lost_sink_ghostpad);

  if (G_UNLIKELY (GST_RPAD_DIRECTION (realsink) != GST_PAD_SINK))
    goto not_sinkpad;

  if (G_UNLIKELY (GST_RPAD_PEER (realsrc) != realsink))
    goto not_linked_together;

  if (GST_RPAD_UNLINKFUNC (realsrc)) {
    GST_RPAD_UNLINKFUNC (realsrc) (GST_PAD (realsrc));
  }
  if (GST_RPAD_UNLINKFUNC (realsink)) {
    GST_RPAD_UNLINKFUNC (realsink) (GST_PAD (realsink));
  }

  /* first clear peers */
  GST_RPAD_PEER (realsrc) = NULL;
  GST_RPAD_PEER (realsink) = NULL;

  /* clear filter, note that we leave the pad caps as they are */
  gst_caps_replace (&GST_RPAD_APPFILTER (realsrc), NULL);
  gst_caps_replace (&GST_RPAD_APPFILTER (realsink), NULL);

  GST_UNLOCK (realsink);
  GST_UNLOCK (realsrc);

  /* fire off a signal to each of the pads telling them 
   * that they've been unlinked */
  g_signal_emit (G_OBJECT (realsrc), gst_real_pad_signals[REAL_UNLINKED],
      0, realsink);
  g_signal_emit (G_OBJECT (realsink), gst_real_pad_signals[REAL_UNLINKED],
      0, realsrc);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinked %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));

  return TRUE;

lost_src_ghostpad:
  {
    return FALSE;
  }
not_srcpad:
  {
    g_critical ("pad %s is not a source pad", GST_PAD_NAME (realsrc));
    GST_UNLOCK (realsrc);
    return FALSE;
  }
lost_sink_ghostpad:
  {
    GST_UNLOCK (realsrc);
    return FALSE;
  }
not_sinkpad:
  {
    g_critical ("pad %s is not a sink pad", GST_PAD_NAME (realsink));
    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
    return FALSE;
  }
not_linked_together:
  {
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
    return FALSE;
  }
}

/**
 * gst_pad_is_linked:
 * @pad: pad to check
 *
 * Checks if a @pad is linked to another pad or not.
 *
 * Returns: TRUE if the pad is linked, FALSE otherwise.
 *
 * MT safe.
 */
gboolean
gst_pad_is_linked (GstPad * pad)
{
  gboolean result;
  GstRealPad *realpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);
  result = (GST_PAD_PEER (realpad) != NULL);
  GST_UNLOCK (realpad);
  return result;

lost_ghostpad:
  {
    return FALSE;
  }
}

/* FIXME leftover from an attempt at refactoring... */
static GstPadLinkReturn
gst_pad_link_prepare_filtered (GstPad * srcpad, GstPad * sinkpad,
    GstRealPad ** outrealsrc, GstRealPad ** outrealsink,
    const GstCaps * filtercaps)
{
  GstRealPad *realsrc, *realsink;

  /* generic checks */
  g_return_val_if_fail (GST_IS_PAD (srcpad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), GST_PAD_LINK_REFUSED);

  GST_CAT_INFO (GST_CAT_PADS, "trying to link %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* now we need to deal with the real/ghost stuff */
  GST_PAD_REALIZE_AND_LOCK (srcpad, realsrc, lost_src_ghostpad);

  if (G_UNLIKELY (GST_RPAD_DIRECTION (realsrc) != GST_PAD_SRC))
    goto not_srcpad;

  if (G_UNLIKELY (GST_RPAD_PEER (realsrc) != NULL))
    goto src_was_linked;

  GST_PAD_REALIZE_AND_LOCK (sinkpad, realsink, lost_sink_ghostpad);

  if (G_UNLIKELY (GST_RPAD_DIRECTION (realsink) != GST_PAD_SINK))
    goto not_sinkpad;

  if (G_UNLIKELY (GST_RPAD_PEER (realsink) != NULL))
    goto sink_was_linked;

  if ((GST_PAD (realsrc) != srcpad) || (GST_PAD (realsink) != sinkpad)) {
    GST_CAT_INFO (GST_CAT_PADS, "*actually* linking %s:%s and %s:%s",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  }
  *outrealsrc = realsrc;
  *outrealsink = realsink;

  /* check pad caps for non-empty intersection */
  {
    GstCaps *srccaps;
    GstCaps *sinkcaps;

    srccaps = gst_real_pad_get_caps_unlocked (realsrc);
    sinkcaps = gst_real_pad_get_caps_unlocked (realsink);
    GST_CAT_DEBUG (GST_CAT_CAPS, "got caps %p and %p", srccaps, sinkcaps);

    if (srccaps && sinkcaps) {
      GstCaps *caps;

      caps = gst_caps_intersect (srccaps, sinkcaps);
      GST_CAT_DEBUG (GST_CAT_CAPS,
          "intersection caps %p %" GST_PTR_FORMAT, caps, caps);

      if (filtercaps) {
        GstCaps *tmp;

        tmp = gst_caps_intersect (caps, filtercaps);
        gst_caps_unref (caps);
        caps = tmp;
      }
      if (!caps || gst_caps_is_empty (caps))
        goto no_format;
    }
  }

  /* FIXME check pad scheduling for non-empty intersection */

  /* update filter */
  if (filtercaps) {
    GstCaps *filtercopy;

    filtercopy = gst_caps_copy (filtercaps);
    filtercopy = gst_caps_ref (filtercopy);

    gst_caps_replace (&GST_PAD_APPFILTER (realsrc), filtercopy);
    gst_caps_replace (&GST_PAD_APPFILTER (realsink), filtercopy);
  } else {
    gst_caps_replace (&GST_PAD_APPFILTER (realsrc), NULL);
    gst_caps_replace (&GST_PAD_APPFILTER (realsink), NULL);
  }
  return GST_PAD_LINK_OK;

lost_src_ghostpad:
  {
    return GST_PAD_LINK_REFUSED;
  }
not_srcpad:
  {
    g_critical ("pad %s is not a source pad", GST_PAD_NAME (realsrc));
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
src_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "src %s:%s was linked",
        GST_DEBUG_PAD_NAME (realsrc));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_WAS_LINKED;
  }
lost_sink_ghostpad:
  {
    GST_DEBUG ("lost sink ghostpad");
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_REFUSED;
  }
not_sinkpad:
  {
    g_critical ("pad %s is not a sink pad", GST_PAD_NAME (realsink));
    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
sink_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "sink %s:%s was linked",
        GST_DEBUG_PAD_NAME (realsink));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_WAS_LINKED;
  }
no_format:
  {
    GST_CAT_INFO (GST_CAT_PADS, "caps are incompatible");
    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_NOFORMAT;
  }
}

/**
 * gst_pad_link_filtered:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 * @filtercaps: the filter #GstCaps.
 *
 * Links the source pad and the sink pad, constrained
 * by the given filter caps.
 *
 * The filtercaps will be copied and refcounted, so you should unref
 * it yourself after using this function.
 *
 * Returns: A result code indicating if the connection worked or
 *          what went wrong.
 *
 * MT Safe.
 */
GstPadLinkReturn
gst_pad_link_filtered (GstPad * srcpad, GstPad * sinkpad,
    const GstCaps * filtercaps)
{
  GstRealPad *realsrc, *realsink;
  GstPadLinkReturn result;

  result = gst_pad_link_prepare_filtered (srcpad, sinkpad, &realsrc, &realsink,
      filtercaps);

  if (result != GST_PAD_LINK_OK)
    goto prepare_failed;

  GST_UNLOCK (realsink);
  GST_UNLOCK (realsrc);

  /* FIXME released the locks here, concurrent thread might link
   * something else. */
  if (GST_RPAD_LINKFUNC (realsrc)) {
    /* this one will call the peer link function */
    result =
        GST_RPAD_LINKFUNC (realsrc) (GST_PAD (realsrc), GST_PAD (realsink));
  } else if (GST_RPAD_LINKFUNC (realsink)) {
    /* if no source link function, we need to call the sink link
     * function ourselves. */
    result =
        GST_RPAD_LINKFUNC (realsink) (GST_PAD (realsink), GST_PAD (realsrc));
  } else {
    result = GST_PAD_LINK_OK;
  }

  GST_LOCK (realsrc);
  GST_LOCK (realsink);
  if (result == GST_PAD_LINK_OK) {
    GST_RPAD_PEER (realsrc) = GST_REAL_PAD (realsink);
    GST_RPAD_PEER (realsink) = GST_REAL_PAD (realsrc);

    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);

    /* fire off a signal to each of the pads telling them 
     * that they've been linked */
    g_signal_emit (G_OBJECT (realsrc), gst_real_pad_signals[REAL_LINKED],
        0, realsink);
    g_signal_emit (G_OBJECT (realsink), gst_real_pad_signals[REAL_LINKED],
        0, realsrc);

    GST_CAT_INFO (GST_CAT_PADS, "linked %s:%s and %s:%s, successful",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  } else {
    GST_CAT_INFO (GST_CAT_PADS, "link between %s:%s and %s:%s failed",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));

    /* remove the filter again */
    if (filtercaps) {
      gst_caps_replace (&GST_RPAD_APPFILTER (realsrc), NULL);
      gst_caps_replace (&GST_RPAD_APPFILTER (realsink), NULL);
    }

    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
  }
  return result;

prepare_failed:
  {
    return result;
  }
}

/**
 * gst_pad_link:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 *
 * Links the source pad to the sink pad.
 *
 * Returns: A result code indicating if the connection worked or
 *          what went wrong.
 */
GstPadLinkReturn
gst_pad_link (GstPad * srcpad, GstPad * sinkpad)
{
  return gst_pad_link_filtered (srcpad, sinkpad, NULL);
}

static void
gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ)
{
  /* this function would need checks if it weren't static */

  GST_LOCK (pad);
  gst_object_replace ((GstObject **) & pad->padtemplate, (GstObject *) templ);
  GST_UNLOCK (pad);

  if (templ) {
    gst_object_sink (GST_OBJECT (templ));
    g_signal_emit (G_OBJECT (templ),
        gst_pad_template_signals[TEMPL_PAD_CREATED], 0, pad);
  }
}

/**
 * gst_pad_get_pad_template:
 * @pad: a #GstPad.
 *
 * Gets the template for @pad.
 *
 * Returns: the #GstPadTemplate from which this pad was instantiated, or %NULL
 * if this pad has no template.
 */
GstPadTemplate *
gst_pad_get_pad_template (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PAD_TEMPLATE (pad);
}


/**
 * gst_pad_get_real_parent:
 * @pad: a #GstPad to get the real parent of.
 *
 * Gets the real parent object of this pad. If the pad
 * is a ghost pad, the actual owner of the real pad is
 * returned, as opposed to #gst_pad_get_parent().
 * Unref the object after use.
 *
 * Returns: the parent #GstElement.
 *
 * MT safe.
 */
GstElement *
gst_pad_get_real_parent (GstPad * pad)
{
  GstRealPad *realpad;
  GstElement *element;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);
  element = GST_PAD_PARENT (realpad);
  if (element)
    gst_object_ref (GST_OBJECT (element));
  GST_UNLOCK (realpad);

  return element;

lost_ghostpad:
  {
    return NULL;
  }
}

/* FIXME not MT safe */
static void
gst_pad_add_ghost_pad (GstPad * pad, GstPad * ghostpad)
{
  GstRealPad *realpad;

  /* if we're ghosting a ghost pad, drill down to find the real pad */
  realpad = (GstRealPad *) pad;
  while (GST_IS_GHOST_PAD (realpad))
    realpad = GST_GPAD_REALPAD (realpad);
  g_return_if_fail (GST_IS_REAL_PAD (realpad));

  /* will ref the pad template */
  GST_GPAD_REALPAD (ghostpad) = realpad;
  realpad->ghostpads = g_list_prepend (realpad->ghostpads, ghostpad);
  gst_pad_set_pad_template (GST_PAD (ghostpad), GST_PAD_PAD_TEMPLATE (pad));
}

static void
gst_pad_remove_ghost_pad (GstPad * pad, GstPad * ghostpad)
{
  GstRealPad *realpad;

  realpad = GST_PAD_REALIZE (pad);
  g_return_if_fail (GST_GPAD_REALPAD (ghostpad) == realpad);

  gst_pad_set_pad_template (GST_PAD (ghostpad), NULL);
  realpad->ghostpads = g_list_remove (realpad->ghostpads, ghostpad);
  GST_GPAD_REALPAD (ghostpad) = NULL;
}

/**
 * gst_pad_relink_filtered:
 * @srcpad: the source #GstPad to relink.
 * @sinkpad: the sink #GstPad to relink.
 * @filtercaps: the #GstPad to use as a filter in the relink.
 *
 * Relinks the given source and sink pad, constrained by the given
 * capabilities.  If the relink fails, the pads are unlinked
 * and an error code is returned.
 *
 * Returns: The result code of the operation.
 *
 * MT safe
 */
GstPadLinkReturn
gst_pad_relink_filtered (GstPad * srcpad, GstPad * sinkpad,
    const GstCaps * filtercaps)
{
  GstRealPad *realsrc, *realsink;

  /* FIXME refactor and share code with link/unlink */

  /* generic checks */
  g_return_val_if_fail (GST_IS_PAD (srcpad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), GST_PAD_LINK_REFUSED);

  GST_CAT_INFO (GST_CAT_PADS, "trying to relink %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* now we need to deal with the real/ghost stuff */
  GST_PAD_REALIZE_AND_LOCK (srcpad, realsrc, lost_src_ghostpad);

  if (G_UNLIKELY (GST_RPAD_DIRECTION (realsrc) != GST_PAD_SRC))
    goto not_srcpad;

  GST_PAD_REALIZE_AND_LOCK (sinkpad, realsink, lost_sink_ghostpad);

  if (G_UNLIKELY (GST_RPAD_DIRECTION (realsink) != GST_PAD_SINK))
    goto not_sinkpad;

  if (G_UNLIKELY (GST_RPAD_PEER (realsink) != realsrc))
    goto not_linked_together;

  if ((GST_PAD (realsrc) != srcpad) || (GST_PAD (realsink) != sinkpad)) {
    GST_CAT_INFO (GST_CAT_PADS, "*actually* relinking %s:%s and %s:%s",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  }

  /* update filter */
  if (filtercaps) {
    GstCaps *filtercopy;

    filtercopy = gst_caps_copy (filtercaps);
    filtercopy = gst_caps_ref (filtercopy);

    gst_caps_replace (&GST_PAD_APPFILTER (realsrc), filtercopy);
    gst_caps_replace (&GST_PAD_APPFILTER (realsink), filtercopy);
  } else {
    gst_caps_replace (&GST_PAD_APPFILTER (realsrc), NULL);
    gst_caps_replace (&GST_PAD_APPFILTER (realsink), NULL);
  }
  /* clear caps to force renegotiation */
  gst_caps_replace (&GST_PAD_CAPS (realsrc), NULL);
  gst_caps_replace (&GST_PAD_CAPS (realsink), NULL);
  GST_UNLOCK (realsink);
  GST_UNLOCK (realsrc);

  GST_CAT_INFO (GST_CAT_PADS, "relinked %s:%s and %s:%s, successful",
      GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));

  return GST_PAD_LINK_OK;

lost_src_ghostpad:
  {
    return GST_PAD_LINK_REFUSED;
  }
not_srcpad:
  {
    g_critical ("pad %s is not a source pad", GST_PAD_NAME (realsrc));
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
lost_sink_ghostpad:
  {
    GST_DEBUG ("lost sink ghostpad");
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_REFUSED;
  }
not_sinkpad:
  {
    g_critical ("pad %s is not a sink pad", GST_PAD_NAME (realsink));
    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
not_linked_together:
  {
    GST_CAT_INFO (GST_CAT_PADS, "src %s:%s was not linked with sink %s:%s",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_UNLOCK (realsink);
    GST_UNLOCK (realsrc);
    return GST_PAD_LINK_REFUSED;
  }
}

/* should be called with the pad LOCK held */
static GstCaps *
gst_real_pad_get_caps_unlocked (GstRealPad * realpad)
{
  GstCaps *result = NULL, *filter;

  GST_CAT_DEBUG (GST_CAT_CAPS, "get pad caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (realpad), realpad);

  if (GST_RPAD_GETCAPSFUNC (realpad)) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "dispatching to pad getcaps function");

    GST_FLAG_SET (realpad, GST_PAD_IN_GETCAPS);
    GST_UNLOCK (realpad);
    result = GST_RPAD_GETCAPSFUNC (realpad) (GST_PAD (realpad));
    GST_LOCK (realpad);
    GST_FLAG_UNSET (realpad, GST_PAD_IN_GETCAPS);

    if (result == NULL) {
      g_critical ("pad %s:%s returned NULL caps from getcaps function\n",
          GST_DEBUG_PAD_NAME (realpad));
    } else {
#ifndef G_DISABLE_ASSERT
      /* check that the returned caps are a real subset of the template caps */
      if (GST_PAD_PAD_TEMPLATE (realpad)) {
        const GstCaps *templ_caps =
            GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (realpad));
        if (!gst_caps_is_subset (result, templ_caps)) {
          GstCaps *temp;

          GST_CAT_ERROR_OBJECT (GST_CAT_CAPS, realpad,
              "pad returned caps %" GST_PTR_FORMAT
              " which are not a real subset of its template caps %"
              GST_PTR_FORMAT, result, templ_caps);
          g_warning
              ("pad %s:%s returned caps that are not a real subset of its template caps",
              GST_DEBUG_PAD_NAME (realpad));
          temp = gst_caps_intersect (templ_caps, result);
          gst_caps_unref (result);
          result = temp;
        }
      }
#endif
      goto done;
    }
  }
  if (GST_PAD_PAD_TEMPLATE (realpad)) {
    GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (realpad);

    result = GST_PAD_TEMPLATE_CAPS (templ);
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "using pad template %p with caps %p %" GST_PTR_FORMAT, templ, result,
        result);

    result = gst_caps_ref (result);
    goto done;
  }
  if (GST_RPAD_CAPS (realpad)) {
    result = GST_RPAD_CAPS (realpad);

    GST_CAT_DEBUG (GST_CAT_CAPS,
        "using pad caps %p %" GST_PTR_FORMAT, result, result);

    result = gst_caps_ref (result);
    goto done;
  }

  GST_CAT_DEBUG (GST_CAT_CAPS, "pad has no caps");
  result = gst_caps_new_empty ();

done:
  filter = GST_RPAD_APPFILTER (realpad);

  if (filter) {
    GstCaps *temp = result;

    GST_CAT_DEBUG (GST_CAT_CAPS,
        "app filter %p %" GST_PTR_FORMAT, filter, filter);
    result = gst_caps_intersect (temp, filter);
    gst_caps_unref (temp);
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "caps after intersection with app filter %p %" GST_PTR_FORMAT, result,
        result);
  }
  return result;
}

/**
 * gst_pad_get_caps:
 * @pad: a  #GstPad to get the capabilities of.
 *
 * Gets the capabilities of this pad.
 *
 * Returns: the #GstCaps of this pad. This function returns a new caps, so use 
 * gst_caps_unref to get rid of it.
 *
 * MT safe.
 */
GstCaps *
gst_pad_get_caps (GstPad * pad)
{
  GstRealPad *realpad;
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  /* now we need to deal with the real/ghost stuff */
  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "get pad caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (realpad), realpad);

  if (G_UNLIKELY (GST_RPAD_IS_IN_GETCAPS (realpad)))
    goto was_dispatching;

  result = gst_real_pad_get_caps_unlocked (realpad);
  GST_UNLOCK (realpad);

  return result;

lost_ghostpad:
  {
    return NULL;
  }
was_dispatching:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "pad %s:%s is already dispatching!", GST_DEBUG_PAD_NAME (realpad));
    g_warning ("pad %s:%s recursively called getcaps!",
        GST_DEBUG_PAD_NAME (realpad));
    GST_UNLOCK (realpad);
    return NULL;
  }
}

/**
 * gst_pad_peer_get_caps:
 * @pad: a  #GstPad to get the peer capabilities of.
 *
 * Gets the capabilities of the peer connected to this pad.
 *
 * Returns: the #GstCaps of the peer pad. This function returns a new caps, so use 
 * gst_caps_unref to get rid of it.
 */
GstCaps *
gst_pad_peer_get_caps (GstPad * pad)
{
  GstRealPad *realpad, *peerpad;
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  /* now we need to deal with the real/ghost stuff */
  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "get peer caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (realpad), realpad);

  peerpad = GST_RPAD_PEER (realpad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  if (G_UNLIKELY (GST_RPAD_IS_IN_GETCAPS (peerpad)))
    goto was_dispatching;

  gst_object_ref (GST_OBJECT (peerpad));
  GST_UNLOCK (realpad);

  result = gst_pad_get_caps (GST_PAD_CAST (peerpad));

  gst_object_unref (GST_OBJECT (peerpad));

  return result;

lost_ghostpad:
  {
    return NULL;
  }
no_peer:
  {
    GST_UNLOCK (realpad);
    return gst_caps_new_any ();
  }
was_dispatching:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "pad %s:%s is already dispatching!", GST_DEBUG_PAD_NAME (realpad));
    g_warning ("pad %s:%s recursively called getcaps!",
        GST_DEBUG_PAD_NAME (realpad));
    GST_UNLOCK (realpad);
    return NULL;
  }
}

/**
 * gst_pad_fixate_caps:
 * @pad: a  #GstPad to fixate
 *
 * Fixate a caps on the given pad.
 *
 * Returns: a fixated #GstCaps.
 */
GstCaps *
gst_pad_fixate_caps (GstPad * pad, GstCaps * caps)
{
  /* FIXME, implement me, call the fixate function for the pad */
  return caps;
}

/**
 * gst_pad_accept_caps:
 * @pad: a  #GstPad to check
 *
 * Check if the given pad accepts the caps.
 *
 * Returns: TRUE if the pad can accept the caps.
 */
gboolean
gst_pad_accept_caps (GstPad * pad, GstCaps * caps)
{
  GstRealPad *realpad;
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* now we need to deal with the real/ghost stuff */
  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "pad accept caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (realpad), realpad);

  /* FIXME, call accept function */
  result = FALSE;
  GST_UNLOCK (realpad);

  return result;

lost_ghostpad:
  {
    return FALSE;
  }
}

/**
 * gst_pad_peer_accept_caps:
 * @pad: a  #GstPad to check
 *
 * Check if the given pad accepts the caps.
 *
 * Returns: TRUE if the pad can accept the caps.
 */
gboolean
gst_pad_peer_accept_caps (GstPad * pad, GstCaps * caps)
{
  GstRealPad *realpad, *peerpad;
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* now we need to deal with the real/ghost stuff */
  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "peer accept caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (realpad), realpad);

  peerpad = GST_RPAD_PEER (realpad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  result = gst_pad_accept_caps (GST_PAD_CAST (peerpad), caps);
  GST_UNLOCK (realpad);

  return result;

lost_ghostpad:
  {
    return FALSE;
  }
no_peer:
  {
    GST_UNLOCK (realpad);
    return TRUE;
  }
}

/**
 * gst_pad_set_caps:
 * @pad: a  #GstPad to set the capabilities of.
 * @caps: a #GstCaps to set.
 *
 * Sets the capabilities of this pad. The caps must be fixed. Any previous
 * caps on the pad will be unreffed. This function refs the caps so you should
 * unref if as soon as you don't need it anymore.
 * It is possible to set NULL caps, which will make the pad unnegotiated
 * again.
 *
 * Returns: TRUE if the caps could be set. FALSE if the caps were not fixed
 * or bad parameters were provided to this function.
 *
 * MT safe.
 */
gboolean
gst_pad_set_caps (GstPad * pad, GstCaps * caps)
{
  GstPadSetCapsFunction setcaps;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), FALSE);

  GST_LOCK (pad);
  setcaps = GST_RPAD_SETCAPSFUNC (pad);

  /* call setcaps function to configure the pad */
  if (setcaps != NULL) {
    if (!GST_RPAD_IS_IN_SETCAPS (pad)) {
      GST_FLAG_SET (pad, GST_PAD_IN_SETCAPS);
      GST_UNLOCK (pad);
      if (!setcaps (pad, caps))
        goto could_not_set;
      GST_LOCK (pad);
    } else {
      GST_CAT_DEBUG (GST_CAT_CAPS, "pad %s:%s was dispatching",
          GST_DEBUG_PAD_NAME (pad));
    }
  }

  if (GST_PAD_CAPS (pad))
    gst_caps_unref (GST_PAD_CAPS (pad));

  if (caps)
    caps = gst_caps_ref (caps);

  GST_PAD_CAPS (pad) = caps;
  GST_CAT_DEBUG (GST_CAT_CAPS, "%s:%s caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);
  GST_UNLOCK (pad);

  g_object_notify (G_OBJECT (pad), "caps");

  return TRUE;

could_not_set:
  {
    GST_LOCK (pad);
    GST_FLAG_UNSET (pad, GST_PAD_IN_SETCAPS);
    GST_UNLOCK (pad);
    GST_CAT_DEBUG (GST_CAT_CAPS, "caps %" GST_PTR_FORMAT " could not be set",
        caps);
    return FALSE;
  }
}

static gboolean
gst_pad_configure_sink (GstPad * pad, GstCaps * caps)
{
  GstPadAcceptCapsFunction acceptcaps;
  GstPadSetCapsFunction setcaps;

  acceptcaps = GST_RPAD_ACCEPTCAPSFUNC (pad);
  setcaps = GST_RPAD_SETCAPSFUNC (pad);

  /* See if pad accepts the caps, by calling acceptcaps, only
   * needed if no setcaps function */
  if (setcaps == NULL && acceptcaps != NULL) {
    if (!acceptcaps (pad, caps))
      goto not_accepted;
  }
  /* set caps on pad if call succeeds */
  gst_pad_set_caps (pad, caps);
  /* no need to unref the caps here, set_caps takes a ref and
   * our ref goes away when we leave this function. */

  return TRUE;

not_accepted:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS, "caps %" GST_PTR_FORMAT " not accepted", caps);
    return FALSE;
  }
}

static gboolean
gst_pad_configure_src (GstPad * pad, GstCaps * caps)
{
  GstPadAcceptCapsFunction acceptcaps;
  GstPadSetCapsFunction setcaps;

  acceptcaps = GST_RPAD_ACCEPTCAPSFUNC (pad);
  setcaps = GST_RPAD_SETCAPSFUNC (pad);

  /* See if pad accepts the caps, by calling acceptcaps, only
   * needed if no setcaps function */
  if (setcaps == NULL && acceptcaps != NULL) {
    if (!acceptcaps (pad, caps))
      goto not_accepted;
  }
  /* set caps on pad if call succeeds */
  gst_pad_set_caps (pad, caps);
  /* no need to unref the caps here, set_caps takes a ref and
   * our ref goes away when we leave this function. */

  return TRUE;

not_accepted:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS, "caps %" GST_PTR_FORMAT " not accepted", caps);
    return FALSE;
  }
}

/**
 * gst_pad_get_pad_template_caps:
 * @pad: a #GstPad to get the template capabilities from.
 *
 * Gets the capabilities for @pad's template.
 *
 * Returns: the #GstCaps of this pad template. If you intend to keep a reference
 * on the caps, make a copy (see gst_caps_copy ()).
 */
const GstCaps *
gst_pad_get_pad_template_caps (GstPad * pad)
{
  static GstStaticCaps anycaps = GST_STATIC_CAPS ("ANY");

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_PAD_TEMPLATE (pad))
    return GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad));

  return gst_static_caps_get (&anycaps);
}


/**
 * gst_pad_get_peer:
 * @pad: a #GstPad to get the peer of.
 *
 * Gets the peer of @pad. This function refs the peer pad so
 * you need to unref it after use.
 *
 * Returns: the peer #GstPad. Unref after usage.
 *
 * MT safe.
 */
GstPad *
gst_pad_get_peer (GstPad * pad)
{
  GstRealPad *realpad;
  GstRealPad *result;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);
  result = GST_RPAD_PEER (realpad);
  if (result)
    gst_object_ref (GST_OBJECT (result));
  GST_UNLOCK (realpad);

  return GST_PAD_CAST (result);

lost_ghostpad:
  {
    return NULL;
  }
}

/**
 * gst_pad_realize:
 * @pad: a #GstPad to realize
 *
 * If the pad is a #GstRealPad, it is simply returned, else
 * the #GstGhostPad will be dereffed to the real pad.
 *
 * After this function you always receive the real pad of
 * the provided pad.
 *
 * This function unrefs the input pad and refs the result so
 * that you can write constructs like:
 *
 *   pad = gst_pad_realize(pad)
 *
 * without having to unref the old pad.
 *
 * Returns: the real #GstPad or NULL when an old reference to a
 * ghostpad is used.
 *
 * MT safe.
 */
GstPad *
gst_pad_realize (GstPad * pad)
{
  GstRealPad *result;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_LOCK (pad);
  result = GST_PAD_REALIZE (pad);
  if (result && pad != GST_PAD_CAST (result)) {
    gst_object_ref (GST_OBJECT (result));
    GST_UNLOCK (pad);
    /* no other thread could dispose this since we
     * hold at least one ref */
    gst_object_unref (GST_OBJECT (pad));
  } else {
    GST_UNLOCK (pad);
  }

  return GST_PAD_CAST (result);
}

/**
 * gst_pad_get_allowed_caps:
 * @srcpad: a #GstPad, it must a a source pad.
 *
 * Gets the capabilities of the allowed media types that can flow through @pad
 * and its peer. The pad must be a source pad.
 * The caller must free the resulting caps.
 *
 * Returns: the allowed #GstCaps of the pad link.  Free the caps when
 * you no longer need it. This function returns NULL when the @pad has no
 * peer.
 *
 * MT safe.
 */
GstCaps *
gst_pad_get_allowed_caps (GstPad * srcpad)
{
  GstCaps *mycaps;
  GstCaps *caps;
  GstCaps *peercaps;
  GstRealPad *realpad, *peer;

  g_return_val_if_fail (GST_IS_PAD (srcpad), NULL);

  GST_PAD_REALIZE_AND_LOCK (srcpad, realpad, lost_ghostpad);

  if (G_UNLIKELY ((peer = GST_RPAD_PEER (realpad)) == NULL))
    goto no_peer;

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting allowed caps",
      GST_DEBUG_PAD_NAME (realpad));

  gst_object_ref (GST_OBJECT_CAST (peer));
  GST_UNLOCK (realpad);
  mycaps = gst_pad_get_caps (GST_PAD_CAST (realpad));

  peercaps = gst_pad_get_caps (GST_PAD_CAST (peer));
  gst_object_unref (GST_OBJECT_CAST (peer));

  caps = gst_caps_intersect (mycaps, peercaps);
  gst_caps_unref (peercaps);
  gst_caps_unref (mycaps);

  GST_CAT_DEBUG (GST_CAT_CAPS, "allowed caps %" GST_PTR_FORMAT, caps);

  return caps;

lost_ghostpad:
  {
    GST_UNLOCK (srcpad);
    return NULL;
  }
no_peer:
  {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: no peer",
        GST_DEBUG_PAD_NAME (realpad));
    GST_UNLOCK (realpad);

    return NULL;
  }
}

/**
 * gst_pad_get_negotiated_caps:
 * @pad: a #GstPad.
 *
 * Gets the capabilities of the media type that currently flows through @pad 
 * and its peer.
 *
 * This function can be used on both src and sinkpads. Note that srcpads are
 * always negotiated before sinkpads so it is possible that the negotiated caps 
 * on the srcpad do not match the negotiated caps of the peer.
 *
 * Returns: the negotiated #GstCaps of the pad link.  Free the caps when
 * you no longer need it. This function returns NULL when the @pad has no 
 * peer or is not negotiated yet.
 *
 * MT safe.
 */
GstCaps *
gst_pad_get_negotiated_caps (GstPad * pad)
{
  GstCaps *caps;
  GstRealPad *realpad, *peer;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);

  if (G_UNLIKELY ((peer = GST_RPAD_PEER (realpad)) == NULL))
    goto no_peer;

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting negotiated caps",
      GST_DEBUG_PAD_NAME (realpad));

  caps = GST_RPAD_CAPS (realpad);
  if (caps)
    gst_caps_ref (caps);
  GST_UNLOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "negotiated caps %" GST_PTR_FORMAT, caps);

  return caps;

lost_ghostpad:
  {
    GST_UNLOCK (pad);
    return NULL;
  }
no_peer:
  {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: no peer",
        GST_DEBUG_PAD_NAME (realpad));
    GST_UNLOCK (realpad);

    return NULL;
  }
}

/**
 * gst_pad_get_filter_caps:
 * @pad: a real #GstPad.
 *
 * Gets the capabilities of filter that currently configured on @pad 
 * and its peer.
 *
 * Returns: the filter #GstCaps of the pad link.  Free the caps when
 * you no longer need it. This function returns NULL when the @pad has no 
 * peer or there is no filter configured.
 *
 * MT safe.
 */
GstCaps *
gst_pad_get_filter_caps (GstPad * pad)
{
  GstCaps *caps;
  GstRealPad *realpad, *peer;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_PAD_REALIZE_AND_LOCK (pad, realpad, lost_ghostpad);

  if (G_UNLIKELY ((peer = GST_RPAD_PEER (realpad)) == NULL))
    goto no_peer;

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting filter caps",
      GST_DEBUG_PAD_NAME (realpad));

  if ((caps = GST_RPAD_APPFILTER (realpad)) != NULL)
    gst_caps_ref (caps);
  GST_UNLOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "filter caps %" GST_PTR_FORMAT, caps);

  return caps;

lost_ghostpad:
  {
    GST_UNLOCK (pad);
    return NULL;
  }
no_peer:
  {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: no peer",
        GST_DEBUG_PAD_NAME (realpad));
    GST_UNLOCK (realpad);

    return NULL;
  }
}

/**
 * gst_pad_alloc_buffer:
 * @pad: a source #GstPad
 * @offset: the offset of the new buffer in the stream
 * @size: the size of the new buffer
 * @caps: the caps of the new buffer
 *
 * Allocates a new, empty buffer optimized to push to pad @pad.  This
 * function only works if @pad is a source pad and a GST_REAL_PAD and
 * has a peer. 
 * You need to check the caps of the buffer after performing this 
 * function and renegotiate to the format if needed.
 *
 * Returns: a new, empty #GstBuffer, or NULL if wrong parameters
 * were provided or the peer pad is not able to provide a buffer
 * that can be handled by the caller.
 *
 * MT safe.
 */
GstBuffer *
gst_pad_alloc_buffer (GstPad * pad, guint64 offset, gint size, GstCaps * caps)
{
  GstRealPad *peer;
  GstBuffer *result = NULL;
  GstPadBufferAllocFunction bufferallocfunc;
  gboolean caps_changed;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), NULL);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), NULL);

  GST_LOCK (pad);
  if (G_UNLIKELY ((peer = GST_RPAD_PEER (pad)) == NULL))
    goto no_peer;

  if (G_LIKELY ((bufferallocfunc = peer->bufferallocfunc) == NULL)) {
    GST_UNLOCK (pad);
    goto fallback;
  }

  gst_object_ref (GST_OBJECT_CAST (peer));
  GST_UNLOCK (pad);

  GST_CAT_DEBUG (GST_CAT_PADS,
      "calling bufferallocfunc &%s (@%p) of peer pad %s:%s",
      GST_DEBUG_FUNCPTR_NAME (bufferallocfunc),
      &bufferallocfunc, GST_DEBUG_PAD_NAME (peer));

  result = bufferallocfunc (GST_PAD_CAST (peer), offset, size, caps);

  gst_object_unref (GST_OBJECT_CAST (peer));

  if (G_UNLIKELY (result == NULL)) {
    goto fallback;
  }

  /* FIXME, move capnego this into a base class? */
  caps = GST_BUFFER_CAPS (result);
  caps_changed = caps && caps != GST_RPAD_CAPS (pad);
  /* we got a new datatype on the pad, see if it can handle it */
  if (G_UNLIKELY (caps_changed)) {
    if (G_UNLIKELY (!gst_pad_configure_src (GST_PAD_CAST (pad), caps)))
      goto not_negotiated;
  }

  return result;

no_peer:
  {
    /* pad has no peer */
    GST_CAT_DEBUG (GST_CAT_PADS,
        "%s:%s called bufferallocfunc but had no peer, returning NULL",
        GST_DEBUG_PAD_NAME (pad));
    GST_UNLOCK (pad);
    return NULL;
  }
  /* fallback case, allocate a buffer of our own, add pad caps. */
fallback:
  {
    result = gst_buffer_new_and_alloc (size);
    gst_buffer_set_caps (result, caps);

    return result;
  }
not_negotiated:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "alloc function retured unacceptable buffer");
    return NULL;
  }
}

static void
gst_real_pad_dispose (GObject * object)
{
  GstPad *pad;
  GstRealPad *rpad;

  pad = GST_PAD (object);
  rpad = GST_REAL_PAD (object);

  /* No linked pad can ever be disposed.
   * It has to have a parent to be linked 
   * and a parent would hold a reference */
  /* FIXME: what about if g_object_dispose is explicitly called on the pad? Is
     that legal? otherwise we could assert GST_OBJECT_PARENT (pad) == NULL as
     well... */
  g_assert (GST_PAD_PEER (pad) == NULL);

  GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "dispose %s:%s",
      GST_DEBUG_PAD_NAME (pad));

  /* we destroy the ghostpads, because they are nothing without the real pad */
  if (rpad->ghostpads) {
    GList *orig, *ghostpads;

    orig = ghostpads = g_list_copy (rpad->ghostpads);

    while (ghostpads) {
      GstPad *ghostpad = GST_PAD (ghostpads->data);

      if (GST_IS_ELEMENT (GST_OBJECT_PARENT (ghostpad))) {
        GstElement *parent = GST_ELEMENT (GST_OBJECT_PARENT (ghostpad));

        GST_CAT_DEBUG (GST_CAT_REFCOUNTING,
            "removing ghost pad from element '%s'", GST_OBJECT_NAME (parent));
        gst_element_remove_pad (parent, ghostpad);
      } else {
        /* handle the case where we have some floating ghost pad that was never
           added to an element */
        g_object_set (ghostpad, "real-pad", NULL, NULL);
      }
      ghostpads = g_list_next (ghostpads);
    }
    g_list_free (orig);
    /* as the ghost pads are removed, they remove themselves from ->ghostpads.
       So it should be empty now. Let's assert that. */
    g_assert (rpad->ghostpads == NULL);
  }

  /* clear the caps */
  gst_caps_replace (&GST_RPAD_CAPS (pad), NULL);
  gst_caps_replace (&GST_RPAD_APPFILTER (pad), NULL);

  if (GST_IS_ELEMENT (GST_OBJECT_PARENT (pad))) {
    GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "removing pad from element '%s'",
        GST_OBJECT_NAME (GST_OBJECT (GST_ELEMENT (GST_OBJECT_PARENT (pad)))));

    gst_element_remove_pad (GST_ELEMENT (GST_OBJECT_PARENT (pad)), pad);
  }

  G_OBJECT_CLASS (real_pad_parent_class)->dispose (object);
}

static void
gst_real_pad_finalize (GObject * object)
{
  GstRealPad *rpad;

  rpad = GST_REAL_PAD (object);

  if (rpad->stream_rec_lock) {
    g_static_rec_mutex_free (rpad->stream_rec_lock);
    rpad->stream_rec_lock = NULL;
  }
  if (rpad->preroll_lock) {
    g_mutex_free (rpad->preroll_lock);
    g_cond_free (rpad->preroll_cond);
    rpad->preroll_lock = NULL;
    rpad->preroll_cond = NULL;
  }
  if (rpad->block_cond) {
    g_cond_free (rpad->block_cond);
    rpad->block_cond = NULL;
  }

  G_OBJECT_CLASS (real_pad_parent_class)->finalize (object);
}


#ifndef GST_DISABLE_LOADSAVE
/* FIXME: why isn't this on a GstElement ? */
/**
 * gst_pad_load_and_link:
 * @self: an #xmlNodePtr to read the description from.
 * @parent: the #GstObject element that owns the pad.
 *
 * Reads the pad definition from the XML node and links the given pad
 * in the element to a pad of an element up in the hierarchy.
 */
void
gst_pad_load_and_link (xmlNodePtr self, GstObject * parent)
{
  xmlNodePtr field = self->xmlChildrenNode;
  GstPad *pad = NULL, *targetpad;
  gchar *peer = NULL;
  gchar **split;
  GstElement *target;
  GstObject *grandparent;
  gchar *name = NULL;

  while (field) {
    if (!strcmp (field->name, "name")) {
      name = xmlNodeGetContent (field);
      pad = gst_element_get_pad (GST_ELEMENT (parent), name);
      g_free (name);
    } else if (!strcmp (field->name, "peer")) {
      peer = xmlNodeGetContent (field);
    }
    field = field->next;
  }
  g_return_if_fail (pad != NULL);

  if (peer == NULL)
    return;

  split = g_strsplit (peer, ".", 2);

  if (split[0] == NULL || split[1] == NULL) {
    GST_CAT_DEBUG (GST_CAT_XML,
        "Could not parse peer '%s' for pad %s:%s, leaving unlinked",
        peer, GST_DEBUG_PAD_NAME (pad));

    g_free (peer);
    return;
  }
  g_free (peer);

  g_return_if_fail (split[0] != NULL);
  g_return_if_fail (split[1] != NULL);

  grandparent = gst_object_get_parent (parent);

  if (grandparent && GST_IS_BIN (grandparent)) {
    target = gst_bin_get_by_name_recurse_up (GST_BIN (grandparent), split[0]);
  } else
    goto cleanup;

  if (target == NULL)
    goto cleanup;

  targetpad = gst_element_get_pad (target, split[1]);

  if (targetpad == NULL)
    goto cleanup;

  gst_pad_link (pad, targetpad);

cleanup:
  g_strfreev (split);
}

/**
 * gst_pad_save_thyself:
 * @pad: a #GstPad to save.
 * @parent: the parent #xmlNodePtr to save the description in.
 *
 * Saves the pad into an xml representation.
 *
 * Returns: the #xmlNodePtr representation of the pad.
 */
static xmlNodePtr
gst_pad_save_thyself (GstObject * object, xmlNodePtr parent)
{
  GstRealPad *realpad;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_REAL_PAD (object), NULL);

  realpad = GST_REAL_PAD (object);

  xmlNewChild (parent, NULL, "name", GST_PAD_NAME (realpad));
  if (GST_RPAD_PEER (realpad) != NULL) {
    gchar *content;

    peer = GST_PAD (GST_RPAD_PEER (realpad));
    /* first check to see if the peer's parent's parent is the same */
    /* we just save it off */
    content = g_strdup_printf ("%s.%s",
        GST_OBJECT_NAME (GST_PAD_PARENT (peer)), GST_PAD_NAME (peer));
    xmlNewChild (parent, NULL, "peer", content);
    g_free (content);
  } else
    xmlNewChild (parent, NULL, "peer", "");

  return parent;
}

/**
 * gst_ghost_pad_save_thyself:
 * @pad: a ghost #GstPad to save.
 * @parent: the parent #xmlNodePtr to save the description in.
 *
 * Saves the ghost pad into an xml representation.
 *
 * Returns: the #xmlNodePtr representation of the pad.
 */
xmlNodePtr
gst_ghost_pad_save_thyself (GstPad * pad, xmlNodePtr parent)
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

/* 
 * should be called with pad lock held 
 *
 * MT safe.
 */
static void
handle_pad_block (GstRealPad * pad)
{
  GstPadBlockCallback callback;
  gpointer user_data;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "signal block taken on pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* need to grab extra ref for the callbacks */
  gst_object_ref (GST_OBJECT (pad));

  callback = pad->block_callback;
  if (callback) {
    user_data = pad->block_data;
    GST_UNLOCK (pad);
    callback (GST_PAD_CAST (pad), TRUE, user_data);
    GST_LOCK (pad);
  } else {
    GST_PAD_BLOCK_SIGNAL (pad);
  }

  while (GST_RPAD_IS_BLOCKED (pad))
    GST_PAD_BLOCK_WAIT (pad);

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "got unblocked");

  callback = pad->block_callback;
  if (callback) {
    user_data = pad->block_data;
    GST_UNLOCK (pad);
    callback (GST_PAD_CAST (pad), FALSE, user_data);
    GST_LOCK (pad);
  } else {
    GST_PAD_BLOCK_SIGNAL (pad);
  }

  gst_object_unref (GST_OBJECT (pad));
}

/**
 * gst_pad_push:
 * @pad: a source #GstPad.
 * @buffer: the #GstBuffer to push.
 *
 * Pushes a buffer to the peer of @pad. @pad must be linked.
 *
 * Returns: a #GstFlowReturn from the peer pad.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_push (GstPad * pad, GstBuffer * buffer)
{
  GstRealPad *peer;
  GstFlowReturn ret;
  GstPadChainFunction chainfunc;
  GstCaps *caps;
  gboolean caps_changed;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_RPAD_DIRECTION (pad) == GST_PAD_SRC,
      GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);


  GST_LOCK (pad);
  while (G_UNLIKELY (GST_RPAD_IS_BLOCKED (pad)))
    handle_pad_block (GST_REAL_PAD_CAST (pad));

  if (G_UNLIKELY ((peer = GST_RPAD_PEER (pad)) == NULL))
    goto not_linked;

  if (G_UNLIKELY (!GST_RPAD_IS_ACTIVE (peer)))
    goto not_active;

  if (G_UNLIKELY (GST_RPAD_IS_FLUSHING (peer)))
    goto flushing;

  gst_object_ref (GST_OBJECT_CAST (peer));
  GST_UNLOCK (pad);

  /* FIXME, move capnego this into a base class? */
  caps = GST_BUFFER_CAPS (buffer);
  caps_changed = caps && caps != GST_RPAD_CAPS (peer);
  /* we got a new datatype on the peer pad, see if it can handle it */
  if (G_UNLIKELY (caps_changed)) {
    if (G_UNLIKELY (!gst_pad_configure_sink (GST_PAD_CAST (peer), caps)))
      goto not_negotiated;
  }

  /* NOTE: we read the peer chainfunc unlocked. 
   * we cannot hold the lock for the peer so we might send
   * the data to the wrong function. This is not really a
   * problem since functions are assigned at creation time
   * and don't change that often... */
  if (G_UNLIKELY ((chainfunc = peer->chainfunc) == NULL))
    goto no_function;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "calling chainfunction &%s of peer pad %s:%s",
      GST_DEBUG_FUNCPTR_NAME (chainfunc), GST_DEBUG_PAD_NAME (peer));

  ret = chainfunc (GST_PAD_CAST (peer), buffer);

  gst_object_unref (GST_OBJECT_CAST (peer));

  return ret;

  /* ERROR recovery here */
not_linked:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but it was not linked");
    GST_UNLOCK (pad);
    return GST_FLOW_NOT_CONNECTED;
  }
not_active:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but it was inactive");
    GST_UNLOCK (pad);
    return GST_FLOW_WRONG_STATE;
  }
flushing:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but pad was flushing");
    GST_UNLOCK (pad);
    return GST_FLOW_UNEXPECTED;
  }
not_negotiated:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing buffer but peer did not accept");
    return GST_FLOW_NOT_NEGOTIATED;
  }
no_function:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but not chainhandler");
    GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
        ("push on pad %s:%s but the peer pad %s:%s has no chainfunction",
            GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (peer)));
    gst_object_unref (GST_OBJECT (peer));
    return GST_FLOW_ERROR;
  }
}

/**
 * gst_pad_pull_range:
 * @pad: a sink #GstPad.
 * @buffer: a pointer to hold the #GstBuffer.
 * @offset: The start offset of the buffer
 * @length: The length of the buffer
 *
 * Pulls a buffer from the peer pad. @pad must be linked.
 *
 * Returns: a #GstFlowReturn from the peer pad.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_pull_range (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstRealPad *peer;
  GstFlowReturn ret;
  GstPadGetRangeFunction getrangefunc;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_RPAD_DIRECTION (pad) == GST_PAD_SINK,
      GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  GST_LOCK (pad);

  while (G_UNLIKELY (GST_RPAD_IS_BLOCKED (pad)))
    handle_pad_block (GST_REAL_PAD_CAST (pad));

  if (G_UNLIKELY ((peer = GST_RPAD_PEER (pad)) == NULL))
    goto not_connected;

  gst_object_ref (GST_OBJECT_CAST (peer));
  GST_UNLOCK (pad);

  /* see note in above function */
  if (G_UNLIKELY ((getrangefunc = peer->getrangefunc) == NULL))
    goto no_function;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "calling getrangefunc %s of peer pad %s:%s",
      GST_DEBUG_FUNCPTR_NAME (getrangefunc), GST_DEBUG_PAD_NAME (peer));

  ret = getrangefunc (GST_PAD_CAST (peer), offset, size, buffer);

  gst_object_unref (GST_OBJECT_CAST (peer));

  return ret;

  /* ERROR recovery here */
not_connected:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pulling range, but it was not linked");
    GST_UNLOCK (pad);
    return GST_FLOW_NOT_CONNECTED;
  }
no_function:
  {
    GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
        ("pullrange on pad %s:%s but the peer pad %s:%s has no getrangefunction",
            GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (peer)));
    gst_object_unref (GST_OBJECT (peer));
    return GST_FLOW_ERROR;
  }
}

/************************************************************************
 *
 * templates
 *
 */
static void gst_pad_template_class_init (GstPadTemplateClass * klass);
static void gst_pad_template_init (GstPadTemplate * templ);
static void gst_pad_template_dispose (GObject * object);

GType
gst_pad_template_get_type (void)
{
  static GType padtemplate_type = 0;

  if (!padtemplate_type) {
    static const GTypeInfo padtemplate_info = {
      sizeof (GstPadTemplateClass), NULL, NULL,
      (GClassInitFunc) gst_pad_template_class_init, NULL, NULL,
      sizeof (GstPadTemplate),
      0,
      (GInstanceInitFunc) gst_pad_template_init, NULL
    };

    padtemplate_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstPadTemplate",
        &padtemplate_info, 0);
  }
  return padtemplate_type;
}

static void
gst_pad_template_class_init (GstPadTemplateClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  padtemplate_parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gst_pad_template_signals[TEMPL_PAD_CREATED] =
      g_signal_new ("pad-created", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPadTemplateClass, pad_created),
      NULL, NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);

  gobject_class->dispose = gst_pad_template_dispose;

  gstobject_class->path_string_separator = "*";
}

static void
gst_pad_template_init (GstPadTemplate * templ)
{
}

static void
gst_pad_template_dispose (GObject * object)
{
  GstPadTemplate *templ = GST_PAD_TEMPLATE (object);

  g_free (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));
  if (GST_PAD_TEMPLATE_CAPS (templ)) {
    gst_caps_unref (GST_PAD_TEMPLATE_CAPS (templ));
  }

  G_OBJECT_CLASS (padtemplate_parent_class)->dispose (object);
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
name_is_valid (const gchar * name, GstPadPresence presence)
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
    if (str && (*(str + 1) != 's' && *(str + 1) != 'd')) {
      g_warning ("invalid name template %s: conversion specification must be of"
          " type '%%d' or '%%s' for GST_PAD_REQUEST padtemplate", name);
      return FALSE;
    }
    if (str && (*(str + 2) != '\0')) {
      g_warning ("invalid name template %s: conversion specification must"
          " appear at the end of the GST_PAD_REQUEST padtemplate name", name);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_static_pad_template_get:
 * @pad_template: the static pad template
 *
 * Converts a #GstStaticPadTemplate into a #GstPadTemplate.
 *
 * Returns: a new #GstPadTemplate.
 */
GstPadTemplate *
gst_static_pad_template_get (GstStaticPadTemplate * pad_template)
{
  GstPadTemplate *new;

  if (!name_is_valid (pad_template->name_template, pad_template->presence))
    return NULL;

  new = g_object_new (gst_pad_template_get_type (),
      "name", pad_template->name_template, NULL);

  GST_PAD_TEMPLATE_NAME_TEMPLATE (new) = g_strdup (pad_template->name_template);
  GST_PAD_TEMPLATE_DIRECTION (new) = pad_template->direction;
  GST_PAD_TEMPLATE_PRESENCE (new) = pad_template->presence;

  GST_PAD_TEMPLATE_CAPS (new) =
      gst_caps_copy (gst_static_caps_get (&pad_template->static_caps));

  return new;
}

/**
 * gst_pad_template_new:
 * @name_template: the name template.
 * @direction: the #GstPadDirection of the template.
 * @presence: the #GstPadPresence of the pad.
 * @caps: a #GstCaps set for the template. The caps are taken ownership of.
 *
 * Creates a new pad template with a name according to the given template
 * and with the given arguments. This functions takes ownership of the provided
 * caps, so be sure to not use them afterwards.
 *
 * Returns: a new #GstPadTemplate.
 */
GstPadTemplate *
gst_pad_template_new (const gchar * name_template,
    GstPadDirection direction, GstPadPresence presence, GstCaps * caps)
{
  GstPadTemplate *new;

  g_return_val_if_fail (name_template != NULL, NULL);
  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (direction == GST_PAD_SRC
      || direction == GST_PAD_SINK, NULL);
  g_return_val_if_fail (presence == GST_PAD_ALWAYS
      || presence == GST_PAD_SOMETIMES || presence == GST_PAD_REQUEST, NULL);

  if (!name_is_valid (name_template, presence))
    return NULL;

  new = g_object_new (gst_pad_template_get_type (),
      "name", name_template, NULL);

  GST_PAD_TEMPLATE_NAME_TEMPLATE (new) = g_strdup (name_template);
  GST_PAD_TEMPLATE_DIRECTION (new) = direction;
  GST_PAD_TEMPLATE_PRESENCE (new) = presence;
  GST_PAD_TEMPLATE_CAPS (new) = caps;

  return new;
}

/**
 * gst_pad_template_get_caps:
 * @templ: a #GstPadTemplate to get capabilities of.
 *
 * Gets the capabilities of the pad template.
 *
 * Returns: the #GstCaps of the pad template. If you need to keep a reference to
 * the caps, make a copy (see gst_caps_copy ()).
 */
const GstCaps *
gst_pad_template_get_caps (GstPadTemplate * templ)
{
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);

  return GST_PAD_TEMPLATE_CAPS (templ);
}

/**
 * gst_pad_set_element_private:
 * @pad: the #GstPad to set the private data of.
 * @priv: The private data to attach to the pad.
 *
 * Set the given private data gpointer on the pad. 
 * This function can only be used by the element that owns the pad.
 */
void
gst_pad_set_element_private (GstPad * pad, gpointer priv)
{
  pad->element_private = priv;
}

/**
 * gst_pad_get_element_private:
 * @pad: the #GstPad to get the private data of.
 *
 * Gets the private data of a pad.
 *
 * Returns: a #gpointer to the private data.
 */
gpointer
gst_pad_get_element_private (GstPad * pad)
{
  return pad->element_private;
}


/***** ghost pads *****/
GType _gst_ghost_pad_type = 0;

static void gst_ghost_pad_class_init (GstGhostPadClass * klass);
static void gst_ghost_pad_init (GstGhostPad * pad);
static void gst_ghost_pad_dispose (GObject * object);
static void gst_ghost_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ghost_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstPad *ghost_pad_parent_class = NULL;

/* static guint gst_ghost_pad_signals[LAST_SIGNAL] = { 0 }; */
enum
{
  GPAD_ARG_0,
  GPAD_ARG_REAL_PAD
      /* fill me */
};

GType
gst_ghost_pad_get_type (void)
{
  if (!_gst_ghost_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstGhostPadClass), NULL, NULL,
      (GClassInitFunc) gst_ghost_pad_class_init, NULL, NULL,
      sizeof (GstGhostPad),
      0,
      (GInstanceInitFunc) gst_ghost_pad_init,
      NULL
    };

    _gst_ghost_pad_type = g_type_register_static (GST_TYPE_PAD, "GstGhostPad",
        &pad_info, 0);
  }
  return _gst_ghost_pad_type;
}

static void
gst_ghost_pad_class_init (GstGhostPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  ghost_pad_parent_class = g_type_class_ref (GST_TYPE_PAD);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ghost_pad_dispose);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_ghost_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_ghost_pad_get_property);

  g_object_class_install_property (gobject_class, GPAD_ARG_REAL_PAD,
      g_param_spec_object ("real-pad", "Real pad",
          "The real pad for the ghost pad", GST_TYPE_PAD, G_PARAM_READWRITE));
}

static void
gst_ghost_pad_init (GstGhostPad * pad)
{
  /* zeroed by glib */
}

static void
gst_ghost_pad_dispose (GObject * object)
{
  g_object_set (object, "real-pad", NULL, NULL);

  G_OBJECT_CLASS (ghost_pad_parent_class)->dispose (object);
}

static void
gst_ghost_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPad *ghostpad = (GstPad *) object;
  GstPad *oldrealpad = (GstPad *) GST_GPAD_REALPAD (ghostpad);
  GstPad *realpad = NULL;

  switch (prop_id) {
    case GPAD_ARG_REAL_PAD:
      realpad = g_value_get_object (value);

      if (oldrealpad) {
        if (realpad == oldrealpad)
          return;
        else
          gst_pad_remove_ghost_pad (oldrealpad, ghostpad);
      }

      if (realpad)
        gst_pad_add_ghost_pad (realpad, ghostpad);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ghost_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case GPAD_ARG_REAL_PAD:
      g_value_set_object (value, GST_GPAD_REALPAD (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_ghost_pad_new:
 * @name: the name of the new ghost pad.
 * @pad: the #GstPad to create a ghost pad for.
 *
 * Creates a new ghost pad associated with @pad, and named @name. If @name is
 * %NULL, a guaranteed unique name (across all ghost pads) will be assigned.
 *
 * Returns: a new ghost #GstPad, or %NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new (const gchar * name, GstPad * pad)
{
  GstPad *gpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gpad = g_object_new (GST_TYPE_GHOST_PAD, "name", name, "real-pad", pad, NULL);

  GST_CAT_DEBUG (GST_CAT_PADS, "created ghost pad \"%s\" for pad %s:%s",
      GST_OBJECT_NAME (gpad), GST_DEBUG_PAD_NAME (pad));

  return gpad;
}

/**
 * gst_pad_get_internal_links_default:
 * @pad: the #GstPad to get the internal links of.
 *
 * Gets a list of pads to which the given pad is linked to
 * inside of the parent element.
 * This is the default handler, and thus returns a list of all of the
 * pads inside the parent element with opposite direction.
 * The caller must free this list after use.
 *
 * Returns: a newly allocated #GList of pads.
 */
GList *
gst_pad_get_internal_links_default (GstPad * pad)
{
  GList *res = NULL;
  GstElement *parent;
  GList *parent_pads;
  GstPadDirection direction;
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);
  direction = rpad->direction;

  parent = GST_PAD_PARENT (rpad);
  parent_pads = parent->pads;

  while (parent_pads) {
    GstRealPad *parent_pad = GST_PAD_REALIZE (parent_pads->data);

    if (parent_pad->direction != direction) {
      res = g_list_prepend (res, parent_pad);
    }

    parent_pads = g_list_next (parent_pads);
  }

  return res;
}

/**
 * gst_pad_get_internal_links:
 * @pad: the #GstPad to get the internal links of.
 *
 * Gets a list of pads to which the given pad is linked to
 * inside of the parent element.
 * The caller must free this list after use.
 *
 * Returns: a newly allocated #GList of pads.
 */
GList *
gst_pad_get_internal_links (GstPad * pad)
{
  GList *res = NULL;
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  if (GST_RPAD_INTLINKFUNC (rpad))
    res = GST_RPAD_INTLINKFUNC (rpad) (GST_PAD_CAST (rpad));

  return res;
}


static gboolean
gst_pad_event_default_dispatch (GstPad * pad, GstEvent * event)
{
  GList *orig, *pads;
  gboolean result;

  GST_INFO_OBJECT (pad, "Sending event %p to all internally linked pads",
      event);

  result = (GST_PAD_DIRECTION (pad) == GST_PAD_SINK);

  orig = pads = gst_pad_get_internal_links (pad);

  while (pads) {
    GstPad *eventpad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    /* for all of the internally-linked pads that are actually linked */
    if (GST_PAD_IS_LINKED (eventpad)) {
      if (GST_PAD_DIRECTION (eventpad) == GST_PAD_SRC) {
        /* for each pad we send to, we should ref the event; it's up
         * to downstream to unref again when handled. */
        GST_LOG_OBJECT (pad, "Reffing and sending event %p to %s:%s", event,
            GST_DEBUG_PAD_NAME (eventpad));
        gst_event_ref (event);
        gst_pad_push_event (eventpad, event);
      } else {
        /* we only send the event on one pad, multi-sinkpad elements
         * should implement a handler */
        GST_LOG_OBJECT (pad, "sending event %p to one sink pad %s:%s", event,
            GST_DEBUG_PAD_NAME (eventpad));
        result = gst_pad_push_event (eventpad, event);
        goto done;
      }
    }
  }
  /* we handled the incoming event so we unref once */
  GST_LOG_OBJECT (pad, "handled event %p, unreffing", event);
  gst_event_unref (event);

done:
  g_list_free (orig);

  return result;
}

/**
 * gst_pad_event_default:
 * @pad: a #GstPad to call the default event handler on.
 * @event: the #GstEvent to handle.
 *
 * Invokes the default event handler for the given pad. End-of-stream and
 * discontinuity events are handled specially, and then the event is sent to all
 * pads internally linked to @pad. Note that if there are many possible sink
 * pads that are internally linked to @pad, only one will be sent an event.
 * Multi-sinkpad elements should implement custom event handlers.
 *
 * Returns: TRUE if the event was sent succesfully.
 */

gboolean
gst_pad_event_default (GstPad * pad, GstEvent * event)
{
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:{
      GstElement *element = gst_pad_get_parent (pad);
      guint64 time;

      if (element && element->clock && GST_ELEMENT_MANAGER (element) &&
          gst_event_discont_get_value (event, GST_FORMAT_TIME, &time)) {
        GST_PIPELINE (GST_ELEMENT_MANAGER (element))->stream_time = time;
      }
      break;
    }
    case GST_EVENT_EOS:{
      GstRealPad *rpad = GST_PAD_REALIZE (pad);

      if (GST_RPAD_TASK (rpad)) {
        GST_DEBUG_OBJECT (rpad, "pausing task because of eos");
        gst_task_pause (GST_RPAD_TASK (rpad));
      }
    }
    default:
      break;
  }

  return gst_pad_event_default_dispatch (pad, event);
}

/**
 * gst_pad_dispatcher:
 * @pad: a #GstPad to dispatch.
 * @dispatch: the #GstDispatcherFunction to call.
 * @data: gpointer user data passed to the dispatcher function.
 *
 * Invokes the given dispatcher function on all pads that are 
 * internally linked to the given pad. 
 * The GstPadDispatcherFunction should return TRUE when no further pads 
 * need to be processed.
 *
 * Returns: TRUE if one of the dispatcher functions returned TRUE.
 */
gboolean
gst_pad_dispatcher (GstPad * pad, GstPadDispatcherFunction dispatch,
    gpointer data)
{
  gboolean res = FALSE;
  GList *int_pads, *orig;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (dispatch != NULL, FALSE);

  orig = int_pads = gst_pad_get_internal_links (pad);

  while (int_pads) {
    GstRealPad *int_rpad = GST_PAD_REALIZE (int_pads->data);
    GstRealPad *int_peer = GST_RPAD_PEER (int_rpad);

    if (int_peer) {
      res = dispatch (GST_PAD (int_peer), data);
      if (res)
        break;
    }
    int_pads = g_list_next (int_pads);
  }

  g_list_free (orig);

  return res;
}

/**
 * gst_pad_push_event:
 * @pad: a #GstPad to push the event to.
 * @event: the #GstEvent to send to the pad.
 *
 * Sends the event to the peer of the given pad.
 *
 * Returns: TRUE if the event was handled.
 *
 * MT safe.
 */
gboolean
gst_pad_push_event (GstPad * pad, GstEvent * event)
{
  GstRealPad *peerpad;
  gboolean result;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  GST_LOCK (pad);
  peerpad = GST_RPAD_PEER (pad);
  if (peerpad == NULL)
    goto not_linked;

  gst_object_ref (GST_OBJECT_CAST (peerpad));
  GST_UNLOCK (pad);

  result = gst_pad_send_event (GST_PAD_CAST (peerpad), event);

  gst_object_unref (GST_OBJECT_CAST (peerpad));

  return result;

  /* ERROR handling */
not_linked:
  {
    GST_UNLOCK (pad);
    return FALSE;
  }
}

/**
 * gst_pad_send_event:
 * @pad: a #GstPad to send the event to.
 * @event: the #GstEvent to send to the pad.
 *
 * Sends the event to the pad. This function can be used
 * by applications to send events in the pipeline.
 *
 * Returns: TRUE if the event was handled.
 */
gboolean
gst_pad_send_event (GstPad * pad, GstEvent * event)
{
  gboolean result = FALSE;
  GstRealPad *rpad;
  GstPadEventFunction eventfunc;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  rpad = GST_PAD_REALIZE (pad);

  if (GST_EVENT_SRC (event) == NULL)
    GST_EVENT_SRC (event) = gst_object_ref (GST_OBJECT (rpad));

  GST_CAT_DEBUG (GST_CAT_EVENT, "have event type %d on pad %s:%s",
      GST_EVENT_TYPE (event), GST_DEBUG_PAD_NAME (rpad));

  if (GST_PAD_IS_SINK (pad)) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH) {
      GST_CAT_DEBUG (GST_CAT_EVENT, "have flush event");
      GST_LOCK (pad);
      if (GST_EVENT_FLUSH_DONE (event)) {
        GST_CAT_DEBUG (GST_CAT_EVENT, "clear flush flag");
        GST_FLAG_UNSET (pad, GST_PAD_FLUSHING);
      } else {
        GST_CAT_DEBUG (GST_CAT_EVENT, "set flush flag");
        GST_FLAG_SET (pad, GST_PAD_FLUSHING);
      }
      GST_UNLOCK (pad);
    }
  }

  if ((eventfunc = GST_RPAD_EVENTFUNC (rpad)) == NULL)
    goto no_function;

  result = eventfunc (GST_PAD_CAST (rpad), event);

  return result;

  /* ERROR handling */
no_function:
  {
    g_warning ("pad %s:%s has no event handler, file a bug.",
        GST_DEBUG_PAD_NAME (rpad));
    gst_event_unref (event);
    return FALSE;
  }
}

typedef struct
{
  GstFormat src_format;
  gint64 src_value;
  GstFormat *dest_format;
  gint64 *dest_value;
}
GstPadConvertData;

static gboolean
gst_pad_convert_dispatcher (GstPad * pad, GstPadConvertData * data)
{
  return gst_pad_convert (pad, data->src_format, data->src_value,
      data->dest_format, data->dest_value);
}

/**
 * gst_pad_convert_default:
 * @pad: a #GstPad to invoke the default converter on.
 * @src_format: the source #GstFormat.
 * @src_value: the source value.
 * @dest_format: a pointer to the destination #GstFormat.
 * @dest_value: a pointer to the destination value.
 *
 * Invokes the default converter on a pad. 
 * This will forward the call to the pad obtained 
 * using the internal link of
 * the element.
 *
 * Returns: TRUE if the conversion could be performed.
 */
gboolean
gst_pad_convert_default (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstPadConvertData data;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  data.src_format = src_format;
  data.src_value = src_value;
  data.dest_format = dest_format;
  data.dest_value = dest_value;

  return gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_convert_dispatcher, &data);
}

/**
 * gst_pad_convert:
 * @pad: a #GstPad to invoke the default converter on.
 * @src_format: the source #GstFormat.
 * @src_value: the source value.
 * @dest_format: a pointer to the destination #GstFormat.
 * @dest_value: a pointer to the destination value.
 *
 * Invokes a conversion on the pad.
 *
 * Returns: TRUE if the conversion could be performed.
 */
gboolean
gst_pad_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  rpad = GST_PAD_REALIZE (pad);

  if (GST_RPAD_CONVERTFUNC (rpad)) {
    return GST_RPAD_CONVERTFUNC (rpad) (GST_PAD (rpad), src_format,
        src_value, dest_format, dest_value);
  }

  return FALSE;
}

typedef struct
{
  GstQueryType type;
  GstFormat *format;
  gint64 *value;
}
GstPadQueryData;

static gboolean
gst_pad_query_dispatcher (GstPad * pad, GstPadQueryData * data)
{
  return gst_pad_query (pad, data->type, data->format, data->value);
}

/**
 * gst_pad_query_default:
 * @pad: a #GstPad to invoke the default query on.
 * @type: the #GstQueryType of the query to perform.
 * @format: a pointer to the #GstFormat of the result.
 * @value: a pointer to the result.
 *
 * Invokes the default query function on a pad. 
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query_default (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstPadQueryData data;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  data.type = type;
  data.format = format;
  data.value = value;

  return gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_query_dispatcher, &data);
}

/**
 * gst_pad_query:
 * @pad: a #GstPad to invoke the default query on.
 * @type: the #GstQueryType of the query to perform.
 * @format: a pointer to the #GstFormat asked for.
 *          On return contains the #GstFormat used.
 * @value: a pointer to the result.
 *
 * Queries a pad for one of the available properties. The format will be
 * adjusted to the actual format used when specifying formats such as 
 * GST_FORMAT_DEFAULT.
 * FIXME: Tell if the format can be adjusted when specifying a definite format.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  rpad = GST_PAD_REALIZE (pad);

  g_return_val_if_fail (rpad, FALSE);

  if (GST_RPAD_QUERYFUNC (rpad))
    return GST_RPAD_QUERYFUNC (rpad) (GST_PAD_CAST (rpad), type, format, value);

  return FALSE;
}

static gboolean
gst_pad_get_formats_dispatcher (GstPad * pad, const GstFormat ** data)
{
  *data = gst_pad_get_formats (pad);

  return TRUE;
}

/**
 * gst_pad_get_formats_default:
 * @pad: a #GstPad to query
 *
 * Invoke the default format dispatcher for the pad.
 *
 * Returns: An array of GstFormats ended with a 0 value.
 */
const GstFormat *
gst_pad_get_formats_default (GstPad * pad)
{
  GstFormat *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_get_formats_dispatcher, &result);

  return result;
}

/**
 * gst_pad_get_formats:
 * @pad: a #GstPad to query
 *
 * Gets the list of supported formats from the pad.
 *
 * Returns: An array of GstFormats ended with a 0 value.
 */
const GstFormat *
gst_pad_get_formats (GstPad * pad)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  if (GST_RPAD_FORMATSFUNC (rpad))
    return GST_RPAD_FORMATSFUNC (rpad) (GST_PAD (pad));

  return NULL;
}
