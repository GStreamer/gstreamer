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
#include "gstenumtypes.h"
#include "gstmarshal.h"
#include "gstutils.h"
#include "gstelement.h"
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

enum
{
  TEMPL_PAD_CREATED,
  /* FILL ME */
  TEMPL_LAST_SIGNAL
};

static GstObject *padtemplate_parent_class = NULL;
static guint gst_pad_template_signals[TEMPL_LAST_SIGNAL] = { 0 };

/* Pad signals and args */
enum
{
  PAD_LINKED,
  PAD_UNLINKED,
  PAD_REQUEST_LINK,
  PAD_HAVE_DATA,
  /* FILL ME */
  PAD_LAST_SIGNAL
};

enum
{
  PAD_PROP_0,
  PAD_PROP_CAPS,
  PAD_PROP_DIRECTION,
  PAD_PROP_TEMPLATE,
  /* FILL ME */
};

GType _gst_pad_type = 0;

static void gst_pad_class_init (GstPadClass * klass);
static void gst_pad_init (GstPad * pad);
static void gst_pad_dispose (GObject * object);
static void gst_pad_finalize (GObject * object);
static void gst_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_pad_get_caps_unlocked (GstPad * pad);
static void gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ);
static gboolean gst_pad_activate_default (GstPad * pad);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_pad_save_thyself (GstObject * object, xmlNodePtr parent);
#endif

static GstObjectClass *pad_parent_class = NULL;
static guint gst_pad_signals[PAD_LAST_SIGNAL] = { 0 };

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

static gboolean
_gst_do_pass_data_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  if (ihint->run_type == G_SIGNAL_RUN_FIRST) {
    gboolean ret = g_value_get_boolean (handler_return);

    g_value_set_boolean (return_accu, ret);
    return ret;
  }

  return TRUE;
}

static void
gst_pad_class_init (GstPadClass * klass)
{
  GObjectClass *gobject_class;


  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  pad_parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pad_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pad_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pad_get_property);

  gst_pad_signals[PAD_LINKED] =
      g_signal_new ("linked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPadClass, linked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  gst_pad_signals[PAD_UNLINKED] =
      g_signal_new ("unlinked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPadClass, unlinked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  gst_pad_signals[PAD_REQUEST_LINK] =
      g_signal_new ("request_link", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstPadClass, request_link), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 0);

  gst_pad_signals[PAD_HAVE_DATA] =
      g_signal_new ("have-data", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstPadClass, have_data),
      _gst_do_pass_data_accumulator,
      NULL, gst_marshal_BOOLEAN__POINTER, G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PAD_PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps", "The capabilities of the pad",
          GST_TYPE_CAPS, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PAD_PROP_DIRECTION,
      g_param_spec_enum ("direction", "Direction", "The direction of the pad",
          GST_TYPE_PAD_DIRECTION, GST_PAD_UNKNOWN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PAD_PROP_TEMPLATE,
      g_param_spec_object ("template", "Template",
          "The GstPadTemplate of this pad", GST_TYPE_PAD_TEMPLATE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_pad_save_thyself);
#endif
  gstobject_class->path_string_separator = ".";
}

static void
gst_pad_init (GstPad * pad)
{
  pad->direction = GST_PAD_UNKNOWN;
  pad->peer = NULL;

  pad->chainfunc = NULL;

  pad->caps = NULL;

  pad->linkfunc = NULL;
  pad->getcapsfunc = NULL;

  pad->activatefunc = gst_pad_activate_default;
  pad->eventfunc = gst_pad_event_default;
  pad->querytypefunc = gst_pad_get_query_types_default;
  pad->queryfunc = gst_pad_query_default;
  pad->intlinkfunc = gst_pad_get_internal_links_default;

  pad->emit_buffer_signals = pad->emit_event_signals = 0;

  GST_PAD_UNSET_FLUSHING (pad);

  pad->preroll_lock = g_mutex_new ();
  pad->preroll_cond = g_cond_new ();

  pad->stream_rec_lock = g_new (GStaticRecMutex, 1);
  g_static_rec_mutex_init (pad->stream_rec_lock);

  pad->block_cond = g_cond_new ();
}

static void
gst_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  gst_pad_set_pad_template (pad, NULL);
  /* FIXME, we have links to many other things like caps
   * and the peer pad... */

  /* No linked pad can ever be disposed.
   * It has to have a parent to be linked 
   * and a parent would hold a reference */
  /* FIXME: what about if g_object_dispose is explicitly called on the pad? Is
     that legal? otherwise we could assert GST_OBJECT_PARENT (pad) == NULL as
     well... */
  g_assert (GST_PAD_PEER (pad) == NULL);

  GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "dispose %s:%s",
      GST_DEBUG_PAD_NAME (pad));

  /* clear the caps */
  gst_caps_replace (&GST_PAD_CAPS (pad), NULL);

  if (GST_IS_ELEMENT (GST_OBJECT_PARENT (pad))) {
    GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "removing pad from element '%s'",
        GST_OBJECT_NAME (GST_OBJECT (GST_ELEMENT (GST_OBJECT_PARENT (pad)))));

    gst_element_remove_pad (GST_ELEMENT (GST_OBJECT_PARENT (pad)), pad);
  }

  G_OBJECT_CLASS (pad_parent_class)->dispose (object);
}

static void
gst_pad_finalize (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  if (pad->stream_rec_lock) {
    g_static_rec_mutex_free (pad->stream_rec_lock);
    pad->stream_rec_lock = NULL;
  }
  if (pad->preroll_lock) {
    g_mutex_free (pad->preroll_lock);
    g_cond_free (pad->preroll_cond);
    pad->preroll_lock = NULL;
    pad->preroll_cond = NULL;
  }
  if (pad->block_cond) {
    g_cond_free (pad->block_cond);
    pad->block_cond = NULL;
  }

  G_OBJECT_CLASS (pad_parent_class)->finalize (object);
}

static void
gst_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case PAD_PROP_DIRECTION:
      GST_PAD_DIRECTION (object) = g_value_get_enum (value);
      break;
    case PAD_PROP_TEMPLATE:
      gst_pad_set_pad_template (GST_PAD_CAST (object),
          (GstPadTemplate *) g_value_dup_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case PAD_PROP_CAPS:
      g_value_set_boxed (value, GST_PAD_CAPS (object));
      break;
    case PAD_PROP_DIRECTION:
      g_value_set_enum (value, GST_PAD_DIRECTION (object));
      break;
    case PAD_PROP_TEMPLATE:
      g_value_set_object (value, GST_PAD_TEMPLATE (object));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_pad_new:
 * @name: the name of the new pad.
 * @direction: the #GstPadDirection of the pad.
 *
 * Creates a new pad with the given name in the given direction.
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
  return g_object_new (GST_TYPE_PAD,
      "name", name, "direction", direction, NULL);
}

/**
 * gst_pad_new_from_template:
 * @templ: the pad template to use
 * @name: the name of the element
 *
 * Creates a new pad with the given name from the given template.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 * This function makes a copy of the name so you can safely free the name.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_new_from_template (GstPadTemplate * templ, const gchar * name)
{
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);

  return g_object_new (GST_TYPE_PAD,
      "name", name, "direction", templ->direction, "template", templ, NULL);
}

/**
 * gst_pad_get_parent:
 * @pad: a pad
 *
 * Gets the parent of @pad, cast to a #GstElement. If a @pad has no parent or
 * its parent is not an element, return NULL.
 *
 * Returns: The parent of the pad. The caller has a reference on the parent, so
 * unref when you're finished with it.
 *
 * MT safe.
 */
GstElement *
gst_pad_get_parent (GstPad * pad)
{
  GstObject *p;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  p = gst_object_get_parent (GST_OBJECT_CAST (pad));

  if (p && !GST_IS_ELEMENT (p)) {
    gst_object_unref (p);
    p = NULL;
  }

  return GST_ELEMENT_CAST (p);
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

  /* PAD_UNKNOWN is a little silly but we need some sort of
   * error return value */
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_UNKNOWN);

  GST_LOCK (pad);
  result = GST_PAD_DIRECTION (pad);
  GST_UNLOCK (pad);

  return result;
}

static gboolean
gst_pad_activate_default (GstPad * pad)
{
  return gst_pad_activate_push (pad, TRUE);
}

static void
pre_activate_switch (GstPad * pad, gboolean new_active)
{
  if (new_active) {
    return;
  } else {
    GST_LOCK (pad);
    GST_PAD_SET_FLUSHING (pad);
    /* unlock blocked pads so element can resume and stop */
    GST_PAD_BLOCK_SIGNAL (pad);
    GST_UNLOCK (pad);
  }
}

static void
post_activate_switch (GstPad * pad, gboolean new_active)
{
  if (new_active) {
    GST_LOCK (pad);
    GST_PAD_UNSET_FLUSHING (pad);
    GST_UNLOCK (pad);
  } else {
    /* make streaming stop */
    GST_STREAM_LOCK (pad);
    GST_STREAM_UNLOCK (pad);
  }
}

/**
 * gst_pad_set_active:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether or not the pad should be active.
 *
 * Activates or deactivates the given pad. Must be called with the STATE_LOCK.
 * Normally called from within core state change functions.
 *
 * If @active, makes sure the pad is active. If it is already active, either in
 * push or pull mode, just return. Otherwise dispatches to the pad's activate
 * function to perform the actual activation.
 *
 * If not @active, checks the pad's current mode and calls
 * gst_pad_activate_push() or gst_pad_activate_pull(), as appropriate, with a
 * FALSE argument.
 *
 * Returns: TRUE if the operation was successfull.
 *
 * MT safe. Must be called with STATE_LOCK.
 */
gboolean
gst_pad_set_active (GstPad * pad, gboolean active)
{
  GstActivateMode old;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  old = GST_PAD_ACTIVATE_MODE (pad);
  GST_UNLOCK (pad);

  if (active) {
    switch (old) {
      case GST_ACTIVATE_PUSH:
      case GST_ACTIVATE_PULL:
        ret = TRUE;
        break;
      case GST_ACTIVATE_NONE:
        ret = (GST_PAD_ACTIVATEFUNC (pad)) (pad);
        break;
    }
  } else {
    switch (old) {
      case GST_ACTIVATE_PUSH:
        ret = gst_pad_activate_push (pad, FALSE);
        break;
      case GST_ACTIVATE_PULL:
        ret = gst_pad_activate_pull (pad, FALSE);
        break;
      case GST_ACTIVATE_NONE:
        ret = TRUE;
        break;
    }
  }

  return ret;
}

/**
 * gst_pad_activate_pull:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether or not the pad should be active.
 *
 * Activates or deactivates the given pad in pull mode via dispatching to the
 * pad's activatepullfunc. For use from within pad activation functions only.
 * When called on sink pads, will first proxy the call to the peer pad, which is
 * expected to activate its internally linked pads from within its activate_pull
 * function.
 *
 * If you don't know what this is, you probably don't want to call it.
 *
 * Returns: TRUE if the operation was successfull.
 *
 * MT safe.
 */
gboolean
gst_pad_activate_pull (GstPad * pad, gboolean active)
{
  GstActivateMode old;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  old = GST_PAD_ACTIVATE_MODE (pad);
  GST_UNLOCK (pad);

  if ((active && old == GST_ACTIVATE_PULL)
      || (!active && old == GST_ACTIVATE_NONE))
    goto was_ok;

  if (active) {
    g_return_val_if_fail (old == GST_ACTIVATE_NONE, FALSE);
  } else {
    g_return_val_if_fail (old == GST_ACTIVATE_PULL, FALSE);
  }

  if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
    GstPad *peer = gst_pad_get_peer (pad);

    if (peer) {
      if (!gst_pad_activate_pull (peer, active)) {
        GST_LOCK (peer);
        GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad,
            "activate_pull on peer (%s:%s) failed", GST_DEBUG_PAD_NAME (peer));
        GST_UNLOCK (peer);
        gst_object_unref (peer);
        goto failure;
      }
    }
  }

  pre_activate_switch (pad, active);

  if (GST_PAD_ACTIVATEPULLFUNC (pad)) {
    if (GST_PAD_ACTIVATEPULLFUNC (pad) (pad, active)) {
      goto success;
    } else {
      goto failure;
    }
  } else {
    /* can happen for sinks of passthrough elements */
    goto success;
  }

was_ok:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "already %s in pull mode",
        active ? "activated" : "deactivated");
    return TRUE;
  }

success:
  {
    GST_LOCK (pad);
    GST_PAD_ACTIVATE_MODE (pad) =
        active ? GST_ACTIVATE_PULL : GST_ACTIVATE_NONE;
    GST_UNLOCK (pad);
    post_activate_switch (pad, active);

    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "%s in pull mode",
        active ? "activated" : "deactivated");
    return TRUE;
  }

failure:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_PADS, pad, "failed to %s in pull mode",
        active ? "activate" : "deactivate");
    return FALSE;
  }
}

/**
 * gst_pad_activate_push:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether or not the pad should be active.
 *
 * Activates or deactivates the given pad in push mode via dispatching to the
 * pad's activatepushfunc. For use from within pad activation functions only.
 *
 * If you don't know what this is, you probably don't want to call it.
 *
 * Returns: TRUE if the operation was successfull.
 *
 * MT safe.
 */
gboolean
gst_pad_activate_push (GstPad * pad, gboolean active)
{
  GstActivateMode old;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  old = GST_PAD_ACTIVATE_MODE (pad);
  GST_UNLOCK (pad);

  if ((active && old == GST_ACTIVATE_PUSH)
      || (!active && old == GST_ACTIVATE_NONE))
    goto was_ok;

  if (active) {
    g_return_val_if_fail (old == GST_ACTIVATE_NONE, FALSE);
  } else {
    g_return_val_if_fail (old == GST_ACTIVATE_PUSH, FALSE);
  }

  pre_activate_switch (pad, active);

  if (GST_PAD_ACTIVATEPUSHFUNC (pad)) {
    if (GST_PAD_ACTIVATEPUSHFUNC (pad) (pad, active)) {
      goto success;
    } else {
      goto failure;
    }
  } else {
    /* quite ok, element relies on state change func to prepare itself */
    goto success;
  }

was_ok:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "already %s in push mode",
        active ? "activated" : "deactivated");
    return TRUE;
  }

success:
  {
    GST_LOCK (pad);
    GST_PAD_ACTIVATE_MODE (pad) =
        active ? GST_ACTIVATE_PUSH : GST_ACTIVATE_NONE;
    GST_UNLOCK (pad);
    post_activate_switch (pad, active);

    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "%s in push mode",
        active ? "activated" : "deactivated");
    return TRUE;
  }

failure:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_PADS, pad, "failed to %s in push mode",
        active ? "activate" : "deactivate");
    return FALSE;
  }
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

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  result = GST_PAD_MODE_ACTIVATE (GST_PAD_ACTIVATE_MODE (pad));
  GST_UNLOCK (pad);

  return result;
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

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);

  was_blocked = GST_PAD_IS_BLOCKED (pad);

  if (G_UNLIKELY (was_blocked == blocked))
    goto had_right_state;

  if (blocked) {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "blocking pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    GST_FLAG_SET (pad, GST_PAD_BLOCKED);
    pad->block_callback = callback;
    pad->block_data = user_data;
    if (!callback) {
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "waiting for block");
      GST_PAD_BLOCK_WAIT (pad);
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "blocked");
    }
  } else {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "unblocking pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    GST_FLAG_UNSET (pad, GST_PAD_BLOCKED);

    pad->block_callback = callback;
    pad->block_data = user_data;

    if (callback) {
      GST_PAD_BLOCK_SIGNAL (pad);
    } else {
      GST_PAD_BLOCK_SIGNAL (pad);
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "waiting for unblock");
      GST_PAD_BLOCK_WAIT (pad);
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "unblocked");
    }
  }
  GST_UNLOCK (pad);

  return TRUE;

had_right_state:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pad %s:%s was in right state", GST_DEBUG_PAD_NAME (pad));
    GST_UNLOCK (pad);
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

  g_return_val_if_fail (GST_IS_PAD (pad), result);

  GST_LOCK (pad);
  result = GST_FLAG_IS_SET (pad, GST_PAD_BLOCKED);
  GST_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_set_activate_function:
 * @pad: a sink #GstPad.
 * @chain: the #GstPadActivateFunction to set.
 *
 * Sets the given activate function for the pad. The activate function will
 * dispatch to activate_push or activate_pull to perform the actual activation.
 * Only makes sense to set on sink pads.
 *
 * Call this function if your sink pad can start a pull-based task.
 */
void
gst_pad_set_activate_function (GstPad * pad, GstPadActivateFunction activate)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_ACTIVATEFUNC (pad) = activate;
  GST_CAT_DEBUG (GST_CAT_PADS, "activatefunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (activate));
}

/**
 * gst_pad_set_activatepull_function:
 * @pad: a sink #GstPad.
 * @chain: the #GstPadActivateModeFunction to set.
 *
 * Sets the given activate_pull function for the pad. An activate_pull function
 * prepares the element and any upstream connections for pulling. See XXX
 * part-activation.txt for details.
 */
void
gst_pad_set_activatepull_function (GstPad * pad,
    GstPadActivateModeFunction activatepull)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_ACTIVATEPULLFUNC (pad) = activatepull;
  GST_CAT_DEBUG (GST_CAT_PADS, "activatepullfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (activatepull));
}

/**
 * gst_pad_set_activatepush_function:
 * @pad: a sink #GstPad.
 * @chain: the #GstPadActivateModeFunction to set.
 *
 * Sets the given activate_push function for the pad. An activate_push function
 * prepares the element for pushing. See XXX part-activation.txt for details.
 */
void
gst_pad_set_activatepush_function (GstPad * pad,
    GstPadActivateModeFunction activatepush)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_ACTIVATEPUSHFUNC (pad) = activatepush;
  GST_CAT_DEBUG (GST_CAT_PADS, "activatepushfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (activatepush));
}

/**
 * gst_pad_set_chain_function:
 * @pad: a sink #GstPad.
 * @chain: the #GstPadChainFunction to set.
 *
 * Sets the given chain function for the pad. The chain function is called to
 * process a #GstBuffer input buffer.
 */
void
gst_pad_set_chain_function (GstPad * pad, GstPadChainFunction chain)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK);

  GST_PAD_CHAINFUNC (pad) = chain;
  GST_CAT_DEBUG (GST_CAT_PADS, "chainfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (chain));
}

/**
 * gst_pad_set_getrange_function:
 * @pad: a source #GstPad.
 * @get: the #GstPadGetRangeFunction to set.
 *
 * Sets the given getrange function for the pad. The getrange function is called to
 * produce a new #GstBuffer to start the processing pipeline. Getrange functions cannot
 * return %NULL.
 */
void
gst_pad_set_getrange_function (GstPad * pad, GstPadGetRangeFunction get)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC);

  GST_PAD_GETRANGEFUNC (pad) = get;

  GST_CAT_DEBUG (GST_CAT_PADS, "getrangefunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (get));
}

/**
 * gst_pad_set_checkgetrange_function:
 * @pad: a source #GstPad.
 * @check: the #GstPadCheckGetRangeFunction to set.
 *
 * Sets the given checkgetrange function for the pad. Implement this function on
 * a pad if you dynamically support getrange based scheduling on the pad.
 */
void
gst_pad_set_checkgetrange_function (GstPad * pad,
    GstPadCheckGetRangeFunction check)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC);

  GST_PAD_CHECKGETRANGEFUNC (pad) = check;

  GST_CAT_DEBUG (GST_CAT_PADS, "checkgetrangefunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (check));
}

/**
 * gst_pad_set_event_function:
 * @pad: a source #GstPad.
 * @event: the #GstPadEventFunction to set.
 *
 * Sets the given event handler for the pad.
 */
void
gst_pad_set_event_function (GstPad * pad, GstPadEventFunction event)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_EVENTFUNC (pad) = event;

  GST_CAT_DEBUG (GST_CAT_PADS, "eventfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (event));
}

/**
 * gst_pad_set_query_function:
 * @pad: a #GstPad of either direction.
 * @query: the #GstPadQueryFunction to set.
 *
 * Set the given query function for the pad.
 */
void
gst_pad_set_query_function (GstPad * pad, GstPadQueryFunction query)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_QUERYFUNC (pad) = query;

  GST_CAT_DEBUG (GST_CAT_PADS, "queryfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (query));
}

/**
 * gst_pad_set_query_type_function:
 * @pad: a #GstPad of either direction.
 * @type_func: the #GstPadQueryTypeFunction to set.
 *
 * Set the given query type function for the pad.
 */
void
gst_pad_set_query_type_function (GstPad * pad,
    GstPadQueryTypeFunction type_func)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_QUERYTYPEFUNC (pad) = type_func;

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
  GstPadQueryTypeFunction func;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (G_UNLIKELY ((func = GST_PAD_QUERYTYPEFUNC (pad)) == NULL))
    goto no_func;

  return func (pad);

no_func:
  {
    return NULL;
  }
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
 * @pad: a #GstPad of either direction.
 * @intlink: the #GstPadIntLinkFunction to set.
 *
 * Sets the given internal link function for the pad.
 */
void
gst_pad_set_internal_link_function (GstPad * pad, GstPadIntLinkFunction intlink)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_INTLINKFUNC (pad) = intlink;
  GST_CAT_DEBUG (GST_CAT_PADS, "internal link for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (intlink));
}

/**
 * gst_pad_set_link_function:
 * @pad: a #GstPad.
 * @link: the #GstPadLinkFunction to set.
 * 
 * Sets the given link function for the pad. It will be called when the pad is
 * linked or relinked with caps. The caps passed to the link function is
 * the caps for the connnection. It can contain a non fixed caps.
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
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_LINKFUNC (pad) = link;
  GST_CAT_DEBUG (GST_CAT_PADS, "linkfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (link));
}

/**
 * gst_pad_set_unlink_function:
 * @pad: a #GstPad.
 * @unlink: the #GstPadUnlinkFunction to set.
 *
 * Sets the given unlink function for the pad. It will be called
 * when the pad is unlinked.
 */
void
gst_pad_set_unlink_function (GstPad * pad, GstPadUnlinkFunction unlink)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_UNLINKFUNC (pad) = unlink;
  GST_CAT_DEBUG (GST_CAT_PADS, "unlinkfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (unlink));
}

/**
 * gst_pad_set_getcaps_function:
 * @pad: a #GstPad.
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
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_GETCAPSFUNC (pad) = getcaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "getcapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (getcaps));
}

/**
 * gst_pad_set_acceptcaps_function:
 * @pad: a #GstPad.
 * @acceptcaps: the #GstPadAcceptCapsFunction to set.
 *
 * Sets the given acceptcaps function for the pad.  The acceptcaps function
 * will be called to check if the pad can accept the given caps.
 */
void
gst_pad_set_acceptcaps_function (GstPad * pad,
    GstPadAcceptCapsFunction acceptcaps)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_ACCEPTCAPSFUNC (pad) = acceptcaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "acceptcapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (acceptcaps));
}

/**
 * gst_pad_set_fixatecaps_function:
 * @pad: a #GstPad.
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
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_FIXATECAPSFUNC (pad) = fixatecaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "fixatecapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (fixatecaps));
}

/**
 * gst_pad_set_setcaps_function:
 * @pad: a #GstPad.
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
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_SETCAPSFUNC (pad) = setcaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "setcapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (setcaps));
}

/**
 * gst_pad_set_bufferalloc_function:
 * @pad: a sink #GstPad.
 * @bufalloc: the #GstPadBufferAllocFunction to set.
 *
 * Sets the given bufferalloc function for the pad. Note that the
 * bufferalloc function can only be set on sinkpads.
 */
void
gst_pad_set_bufferalloc_function (GstPad * pad,
    GstPadBufferAllocFunction bufalloc)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_IS_SINK (pad));

  GST_PAD_BUFFERALLOCFUNC (pad) = bufalloc;
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
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinking %s:%s(%p) and %s:%s(%p)",
      GST_DEBUG_PAD_NAME (srcpad), srcpad,
      GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

  GST_LOCK (srcpad);

  if (G_UNLIKELY (GST_PAD_DIRECTION (srcpad) != GST_PAD_SRC))
    goto not_srcpad;

  GST_LOCK (sinkpad);

  if (G_UNLIKELY (GST_PAD_DIRECTION (sinkpad) != GST_PAD_SINK))
    goto not_sinkpad;

  if (G_UNLIKELY (GST_PAD_PEER (srcpad) != sinkpad))
    goto not_linked_together;

  if (GST_PAD_UNLINKFUNC (srcpad)) {
    GST_PAD_UNLINKFUNC (srcpad) (srcpad);
  }
  if (GST_PAD_UNLINKFUNC (sinkpad)) {
    GST_PAD_UNLINKFUNC (sinkpad) (sinkpad);
  }

  /* first clear peers */
  GST_PAD_PEER (srcpad) = NULL;
  GST_PAD_PEER (sinkpad) = NULL;

  GST_UNLOCK (sinkpad);
  GST_UNLOCK (srcpad);

  /* fire off a signal to each of the pads telling them 
   * that they've been unlinked */
  g_signal_emit (G_OBJECT (srcpad), gst_pad_signals[PAD_UNLINKED], 0, sinkpad);
  g_signal_emit (G_OBJECT (sinkpad), gst_pad_signals[PAD_UNLINKED], 0, srcpad);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinked %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  return TRUE;

not_srcpad:
  {
    g_critical ("pad %s is not a source pad", GST_PAD_NAME (srcpad));
    GST_UNLOCK (srcpad);
    return FALSE;
  }
not_sinkpad:
  {
    g_critical ("pad %s is not a sink pad", GST_PAD_NAME (sinkpad));
    GST_UNLOCK (sinkpad);
    GST_UNLOCK (srcpad);
    return FALSE;
  }
not_linked_together:
  {
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_UNLOCK (sinkpad);
    GST_UNLOCK (srcpad);
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

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  result = (GST_PAD_PEER (pad) != NULL);
  GST_UNLOCK (pad);

  return result;
}

static gboolean
gst_pad_link_check_compatible_unlocked (GstPad * src, GstPad * sink)
{
  GstCaps *srccaps;
  GstCaps *sinkcaps;

  srccaps = gst_pad_get_caps_unlocked (src);
  sinkcaps = gst_pad_get_caps_unlocked (sink);
  GST_CAT_DEBUG (GST_CAT_CAPS, "got caps %p and %p", srccaps, sinkcaps);

  if (srccaps && sinkcaps) {
    GstCaps *icaps;

    icaps = gst_caps_intersect (srccaps, sinkcaps);
    gst_caps_unref (srccaps);
    gst_caps_unref (sinkcaps);

    GST_CAT_DEBUG (GST_CAT_CAPS,
        "intersection caps %p %" GST_PTR_FORMAT, icaps, icaps);

    if (!icaps || gst_caps_is_empty (icaps))
      return FALSE;
  }

  return TRUE;
}


/* FIXME leftover from an attempt at refactoring... */
/* call with the two pads unlocked */
static GstPadLinkReturn
gst_pad_link_prepare (GstPad * srcpad, GstPad * sinkpad)
{
  /* generic checks */
  g_return_val_if_fail (GST_IS_PAD (srcpad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), GST_PAD_LINK_REFUSED);

  GST_CAT_INFO (GST_CAT_PADS, "trying to link %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  GST_LOCK (srcpad);

  if (G_UNLIKELY (GST_PAD_DIRECTION (srcpad) != GST_PAD_SRC))
    goto not_srcpad;

  if (G_UNLIKELY (GST_PAD_PEER (srcpad) != NULL))
    goto src_was_linked;

  GST_LOCK (sinkpad);

  if (G_UNLIKELY (GST_PAD_DIRECTION (sinkpad) != GST_PAD_SINK))
    goto not_sinkpad;

  if (G_UNLIKELY (GST_PAD_PEER (sinkpad) != NULL))
    goto sink_was_linked;

  /* check pad caps for non-empty intersection */
  if (!gst_pad_link_check_compatible_unlocked (srcpad, sinkpad)) {
    goto no_format;
  }

  /* FIXME check pad scheduling for non-empty intersection */

  return GST_PAD_LINK_OK;

not_srcpad:
  {
    g_critical ("pad %s is not a source pad", GST_PAD_NAME (srcpad));
    GST_UNLOCK (srcpad);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
src_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "src %s:%s was linked",
        GST_DEBUG_PAD_NAME (srcpad));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_UNLOCK (srcpad);
    return GST_PAD_LINK_WAS_LINKED;
  }
not_sinkpad:
  {
    g_critical ("pad %s is not a sink pad", GST_PAD_NAME (sinkpad));
    GST_UNLOCK (sinkpad);
    GST_UNLOCK (srcpad);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
sink_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "sink %s:%s was linked",
        GST_DEBUG_PAD_NAME (sinkpad));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_UNLOCK (sinkpad);
    GST_UNLOCK (srcpad);
    return GST_PAD_LINK_WAS_LINKED;
  }
no_format:
  {
    GST_CAT_INFO (GST_CAT_PADS, "caps are incompatible");
    GST_UNLOCK (sinkpad);
    GST_UNLOCK (srcpad);
    return GST_PAD_LINK_NOFORMAT;
  }
}

/**
 * gst_pad_link:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 *
 * Links the source pad and the sink pad.
 *
 * Returns: A result code indicating if the connection worked or
 *          what went wrong.
 *
 * MT Safe.
 */
GstPadLinkReturn
gst_pad_link (GstPad * srcpad, GstPad * sinkpad)
{
  GstPadLinkReturn result;

  /* prepare will also lock the two pads */
  result = gst_pad_link_prepare (srcpad, sinkpad);

  if (result != GST_PAD_LINK_OK)
    goto prepare_failed;

  GST_UNLOCK (sinkpad);
  GST_UNLOCK (srcpad);

  /* FIXME released the locks here, concurrent thread might link
   * something else. */
  if (GST_PAD_LINKFUNC (srcpad)) {
    /* this one will call the peer link function */
    result = GST_PAD_LINKFUNC (srcpad) (srcpad, sinkpad);
  } else if (GST_PAD_LINKFUNC (sinkpad)) {
    /* if no source link function, we need to call the sink link
     * function ourselves. */
    result = GST_PAD_LINKFUNC (sinkpad) (sinkpad, srcpad);
  } else {
    result = GST_PAD_LINK_OK;
  }

  GST_LOCK (srcpad);
  GST_LOCK (sinkpad);

  if (result == GST_PAD_LINK_OK) {
    GST_PAD_PEER (srcpad) = sinkpad;
    GST_PAD_PEER (sinkpad) = srcpad;

    GST_UNLOCK (sinkpad);
    GST_UNLOCK (srcpad);

    /* fire off a signal to each of the pads telling them
     * that they've been linked */
    g_signal_emit (G_OBJECT (srcpad), gst_pad_signals[PAD_LINKED], 0, sinkpad);
    g_signal_emit (G_OBJECT (sinkpad), gst_pad_signals[PAD_LINKED], 0, srcpad);

    GST_CAT_INFO (GST_CAT_PADS, "linked %s:%s and %s:%s, successful",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
  } else {
    GST_CAT_INFO (GST_CAT_PADS, "link between %s:%s and %s:%s failed",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

    GST_UNLOCK (sinkpad);
    GST_UNLOCK (srcpad);
  }
  return result;

prepare_failed:
  {
    return result;
  }
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
 *
 * FIXME: currently returns an unrefcounted padtemplate.
 */
GstPadTemplate *
gst_pad_get_pad_template (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PAD_TEMPLATE (pad);
}


/* should be called with the pad LOCK held */
/* refs the caps, so caller is responsible for getting it unreffed */
static GstCaps *
gst_pad_get_caps_unlocked (GstPad * pad)
{
  GstCaps *result = NULL;

  GST_CAT_DEBUG (GST_CAT_CAPS, "get pad caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  if (GST_PAD_GETCAPSFUNC (pad)) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "dispatching to pad getcaps function");

    GST_FLAG_SET (pad, GST_PAD_IN_GETCAPS);
    GST_UNLOCK (pad);
    result = GST_PAD_GETCAPSFUNC (pad) (pad);
    GST_LOCK (pad);
    GST_FLAG_UNSET (pad, GST_PAD_IN_GETCAPS);

    if (result == NULL) {
      g_critical ("pad %s:%s returned NULL caps from getcaps function",
          GST_DEBUG_PAD_NAME (pad));
    } else {
#ifndef G_DISABLE_ASSERT
      /* check that the returned caps are a real subset of the template caps */
      if (GST_PAD_PAD_TEMPLATE (pad)) {
        const GstCaps *templ_caps =
            GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad));
        if (!gst_caps_is_subset (result, templ_caps)) {
          GstCaps *temp;

          GST_CAT_ERROR_OBJECT (GST_CAT_CAPS, pad,
              "pad returned caps %" GST_PTR_FORMAT
              " which are not a real subset of its template caps %"
              GST_PTR_FORMAT, result, templ_caps);
          g_warning
              ("pad %s:%s returned caps that are not a real subset of its template caps",
              GST_DEBUG_PAD_NAME (pad));
          temp = gst_caps_intersect (templ_caps, result);
          gst_caps_unref (result);
          result = temp;
        }
      }
#endif
      goto done;
    }
  }
  if (GST_PAD_PAD_TEMPLATE (pad)) {
    GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (pad);

    result = GST_PAD_TEMPLATE_CAPS (templ);
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "using pad template %p with caps %p %" GST_PTR_FORMAT, templ, result,
        result);

    result = gst_caps_ref (result);
    goto done;
  }
  if (GST_PAD_CAPS (pad)) {
    result = GST_PAD_CAPS (pad);

    GST_CAT_DEBUG (GST_CAT_CAPS,
        "using pad caps %p %" GST_PTR_FORMAT, result, result);

    result = gst_caps_ref (result);
    goto done;
  }

  GST_CAT_DEBUG (GST_CAT_CAPS, "pad has no caps");
  result = gst_caps_new_empty ();

done:
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
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_LOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "get pad caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  if (G_UNLIKELY (GST_PAD_IS_IN_GETCAPS (pad)))
    goto was_dispatching;

  result = gst_pad_get_caps_unlocked (pad);
  GST_UNLOCK (pad);

  return result;

was_dispatching:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "pad %s:%s is already dispatching!", GST_DEBUG_PAD_NAME (pad));
    g_warning ("pad %s:%s recursively called getcaps!",
        GST_DEBUG_PAD_NAME (pad));
    GST_UNLOCK (pad);
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
 * gst_caps_unref to get rid of it. this function returns NULL if there is no
 * peer pad or when this function is called recursively from a getcaps function.
 */
GstCaps *
gst_pad_peer_get_caps (GstPad * pad)
{
  GstPad *peerpad;
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_LOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "get peer caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  peerpad = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  if (G_UNLIKELY (GST_PAD_IS_IN_GETCAPS (peerpad)))
    goto was_dispatching;

  gst_object_ref (peerpad);
  GST_UNLOCK (pad);

  result = gst_pad_get_caps (peerpad);

  gst_object_unref (peerpad);

  return result;

no_peer:
  {
    GST_UNLOCK (pad);
    return NULL;
  }
was_dispatching:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "pad %s:%s is already dispatching!", GST_DEBUG_PAD_NAME (pad));
    g_warning ("pad %s:%s recursively called getcaps!",
        GST_DEBUG_PAD_NAME (pad));
    GST_UNLOCK (pad);
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
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "pad accept caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  if (GST_PAD_ACCEPTCAPSFUNC (pad)) {
    /* we can call the function */
    result = GST_PAD_ACCEPTCAPSFUNC (pad) (pad, caps);
  } else {
    /* else see get the caps and see if it intersects to something
     * not empty */
    GstCaps *intersect;
    GstCaps *allowed;

    allowed = gst_pad_get_caps_unlocked (pad);
    intersect = gst_caps_intersect (allowed, caps);
    if (gst_caps_is_empty (intersect))
      result = FALSE;
    else
      result = TRUE;
  }
  GST_UNLOCK (pad);

  return result;
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
  GstPad *peerpad;
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "peer accept caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  peerpad = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  result = gst_pad_accept_caps (peerpad, caps);
  GST_UNLOCK (pad);

  return result;

no_peer:
  {
    GST_UNLOCK (pad);
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

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  setcaps = GST_PAD_SETCAPSFUNC (pad);

  /* call setcaps function to configure the pad */
  if (setcaps != NULL && caps) {
    if (!GST_PAD_IS_IN_SETCAPS (pad)) {
      GST_FLAG_SET (pad, GST_PAD_IN_SETCAPS);
      GST_UNLOCK (pad);
      if (!setcaps (pad, caps))
        goto could_not_set;
      GST_LOCK (pad);
      GST_FLAG_UNSET (pad, GST_PAD_IN_SETCAPS);
    } else {
      GST_CAT_DEBUG (GST_CAT_CAPS, "pad %s:%s was dispatching",
          GST_DEBUG_PAD_NAME (pad));
    }
  }

  gst_caps_replace (&GST_PAD_CAPS (pad), caps);
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
  gboolean res;

  acceptcaps = GST_PAD_ACCEPTCAPSFUNC (pad);
  setcaps = GST_PAD_SETCAPSFUNC (pad);

  /* See if pad accepts the caps, by calling acceptcaps, only
   * needed if no setcaps function */
  if (setcaps == NULL && acceptcaps != NULL) {
    if (!acceptcaps (pad, caps))
      goto not_accepted;
  }
  /* set caps on pad if call succeeds */
  res = gst_pad_set_caps (pad, caps);
  /* no need to unref the caps here, set_caps takes a ref and
   * our ref goes away when we leave this function. */

  return res;

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
  gboolean res;

  acceptcaps = GST_PAD_ACCEPTCAPSFUNC (pad);
  setcaps = GST_PAD_SETCAPSFUNC (pad);

  /* See if pad accepts the caps, by calling acceptcaps, only
   * needed if no setcaps function */
  if (setcaps == NULL && acceptcaps != NULL) {
    if (!acceptcaps (pad, caps))
      goto not_accepted;
  }
  /* set caps on pad if call succeeds */
  res = gst_pad_set_caps (pad, caps);
  /* no need to unref the caps here, set_caps takes a ref and
   * our ref goes away when we leave this function. */

  return res;

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
  GstPad *result;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_LOCK (pad);
  result = GST_PAD_PEER (pad);
  if (result)
    gst_object_ref (result);
  GST_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_get_allowed_caps:
 * @srcpad: a #GstPad, it must a a source pad.
 *
 * Gets the capabilities of the allowed media types that can flow through
 * @srcpad and its peer. The pad must be a source pad.
 * The caller must free the resulting caps.
 *
 * Returns: the allowed #GstCaps of the pad link.  Free the caps when
 * you no longer need it. This function returns NULL when the @srcpad has no
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
  GstPad *peer;

  g_return_val_if_fail (GST_IS_PAD (srcpad), NULL);
  g_return_val_if_fail (GST_PAD_IS_SRC (srcpad), NULL);

  GST_LOCK (srcpad);

  peer = GST_PAD_PEER (srcpad);
  if (G_UNLIKELY (peer == NULL))
    goto no_peer;

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting allowed caps",
      GST_DEBUG_PAD_NAME (srcpad));

  gst_object_ref (peer);
  GST_UNLOCK (srcpad);
  mycaps = gst_pad_get_caps (srcpad);

  peercaps = gst_pad_get_caps (peer);
  gst_object_unref (peer);

  caps = gst_caps_intersect (mycaps, peercaps);
  gst_caps_unref (peercaps);
  gst_caps_unref (mycaps);

  GST_CAT_DEBUG (GST_CAT_CAPS, "allowed caps %" GST_PTR_FORMAT, caps);

  return caps;

no_peer:
  {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: no peer",
        GST_DEBUG_PAD_NAME (srcpad));
    GST_UNLOCK (srcpad);

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
  GstPad *peer;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_LOCK (pad);

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto no_peer;

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting negotiated caps",
      GST_DEBUG_PAD_NAME (pad));

  caps = GST_PAD_CAPS (pad);
  if (caps)
    gst_caps_ref (caps);
  GST_UNLOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "negotiated caps %" GST_PTR_FORMAT, caps);

  return caps;

no_peer:
  {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: no peer",
        GST_DEBUG_PAD_NAME (pad));
    GST_UNLOCK (pad);

    return NULL;
  }
}

/**
 * gst_pad_alloc_buffer:
 * @pad: a source #GstPad
 * @offset: the offset of the new buffer in the stream
 * @size: the size of the new buffer
 * @caps: the caps of the new buffer
 * @buf: a newly allocated buffer
 *
 * Allocates a new, empty buffer optimized to push to pad @pad.  This
 * function only works if @pad is a source pad and has a peer. 
 *
 * You need to check the caps of the buffer after performing this 
 * function and renegotiate to the format if needed.
 *
 * A new, empty #GstBuffer will be put in the @buf argument.
 *
 * Returns: a result code indicating success of the operation. Any
 * result code other than GST_FLOW_OK is an error and @buf should
 * not be used.
 * An error can occur if the pad is not connected or when the downstream
 * peer elements cannot provide an acceptable buffer.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_alloc_buffer (GstPad * pad, guint64 offset, gint size, GstCaps * caps,
    GstBuffer ** buf)
{
  GstPad *peer;
  GstFlowReturn ret;
  GstPadBufferAllocFunction bufferallocfunc;
  gboolean caps_changed;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  GST_LOCK (pad);
  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto no_peer;

  gst_object_ref (peer);
  GST_UNLOCK (pad);

  if (G_LIKELY ((bufferallocfunc = peer->bufferallocfunc) == NULL))
    goto fallback;

  GST_LOCK (peer);
  /* when the peer is flushing we cannot give a buffer */
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (peer)))
    goto flushing;

  GST_CAT_DEBUG (GST_CAT_PADS,
      "calling bufferallocfunc &%s (@%p) of peer pad %s:%s",
      GST_DEBUG_FUNCPTR_NAME (bufferallocfunc),
      &bufferallocfunc, GST_DEBUG_PAD_NAME (peer));
  GST_UNLOCK (peer);

  ret = bufferallocfunc (peer, offset, size, caps, buf);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto peer_error;
  if (G_UNLIKELY (*buf == NULL))
    goto fallback;

do_caps:
  gst_object_unref (peer);

  /* FIXME, move capnego this into a base class? */
  caps = GST_BUFFER_CAPS (*buf);
  caps_changed = caps && caps != GST_PAD_CAPS (pad);
  /* we got a new datatype on the pad, see if it can handle it */
  if (G_UNLIKELY (caps_changed)) {
    GST_DEBUG ("caps changed to %" GST_PTR_FORMAT, caps);
    if (G_UNLIKELY (!gst_pad_configure_src (pad, caps)))
      goto not_negotiated;
  }
  return ret;

no_peer:
  {
    /* pad has no peer */
    GST_CAT_DEBUG (GST_CAT_PADS,
        "%s:%s called bufferallocfunc but had no peer",
        GST_DEBUG_PAD_NAME (pad));
    GST_UNLOCK (pad);
    return GST_FLOW_NOT_LINKED;
  }
flushing:
  {
    /* peer was flushing */
    GST_UNLOCK (peer);
    gst_object_unref (peer);
    GST_CAT_DEBUG (GST_CAT_PADS,
        "%s:%s called bufferallocfunc but peer was flushing",
        GST_DEBUG_PAD_NAME (pad));
    return GST_FLOW_WRONG_STATE;
  }
  /* fallback case, allocate a buffer of our own, add pad caps. */
fallback:
  {
    GST_CAT_DEBUG (GST_CAT_PADS,
        "%s:%s fallback buffer alloc", GST_DEBUG_PAD_NAME (pad));
    *buf = gst_buffer_new_and_alloc (size);
    gst_buffer_set_caps (*buf, caps);

    ret = GST_FLOW_OK;

    goto do_caps;
  }
not_negotiated:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "alloc function retured unacceptable buffer");
    return GST_FLOW_NOT_NEGOTIATED;
  }
peer_error:
  {
    gst_object_unref (peer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "alloc function retured error %d", ret);
    return ret;
  }
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
 *
 * Not MT safe.
 */
GList *
gst_pad_get_internal_links_default (GstPad * pad)
{
  GList *res = NULL;
  GstElement *parent;
  GList *parent_pads;
  GstPadDirection direction;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  direction = pad->direction;

  parent = GST_PAD_PARENT (pad);
  parent_pads = parent->pads;

  while (parent_pads) {
    GstPad *parent_pad = GST_PAD_CAST (parent_pads->data);

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
 *
 * Not MT safe.
 */
GList *
gst_pad_get_internal_links (GstPad * pad)
{
  GList *res = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_INTLINKFUNC (pad))
    res = GST_PAD_INTLINKFUNC (pad) (pad);

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
    GstPad *eventpad = GST_PAD_CAST (pads->data);

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
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (pad, "pausing task because of eos");
      gst_pad_pause_task (pad);
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
    GstPad *int_pad = GST_PAD_CAST (int_pads->data);
    GstPad *int_peer = GST_PAD_PEER (int_pad);

    if (int_peer) {
      res = dispatch (int_peer, data);
      if (res)
        break;
    }
    int_pads = g_list_next (int_pads);
  }

  g_list_free (orig);

  return res;
}

/**
 * gst_pad_query:
 * @pad: a #GstPad to invoke the default query on.
 * @query: the #GstQuery to perform.
 *
 * Dispatches a query to a pad. The query should have been allocated by the
 * caller via one of the type-specific allocation functions in gstquery.h. The
 * element is responsible for filling the query with an appropriate response,
 * which should then be parsed with a type-specific query parsing function.
 *
 * Again, the caller is responsible for both the allocation and deallocation of
 * the query structure.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query (GstPad * pad, GstQuery * query)
{
  GstPadQueryFunction func;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  GST_DEBUG ("sending query %p to pad %s:%s", query, GST_DEBUG_PAD_NAME (pad));

  if ((func = GST_PAD_QUERYFUNC (pad)) == NULL)
    goto no_func;

  return func (pad, query);

no_func:
  {
    GST_DEBUG ("pad had no query function");
    return FALSE;
  }
}

gboolean
gst_pad_query_default (GstPad * pad, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    case GST_QUERY_SEEKING:
    case GST_QUERY_FORMATS:
    case GST_QUERY_LATENCY:
    case GST_QUERY_JITTER:
    case GST_QUERY_RATE:
    case GST_QUERY_CONVERT:
    default:
      return gst_pad_dispatcher
          (pad, (GstPadDispatcherFunction) gst_pad_query, query);
  }
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
    if (!strcmp ((char *) field->name, "name")) {
      name = (gchar *) xmlNodeGetContent (field);
      pad = gst_element_get_pad (GST_ELEMENT (parent), name);
      g_free (name);
    } else if (!strcmp ((char *) field->name, "peer")) {
      peer = (gchar *) xmlNodeGetContent (field);
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
  GstPad *pad;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_PAD (object), NULL);

  pad = GST_PAD (object);

  xmlNewChild (parent, NULL, (xmlChar *) "name",
      (xmlChar *) GST_PAD_NAME (pad));
  if (GST_PAD_PEER (pad) != NULL) {
    gchar *content;

    peer = GST_PAD_PEER (pad);
    /* first check to see if the peer's parent's parent is the same */
    /* we just save it off */
    content = g_strdup_printf ("%s.%s",
        GST_OBJECT_NAME (GST_PAD_PARENT (peer)), GST_PAD_NAME (peer));
    xmlNewChild (parent, NULL, (xmlChar *) "peer", (xmlChar *) content);
    g_free (content);
  } else
    xmlNewChild (parent, NULL, (xmlChar *) "peer", NULL);

  return parent;
}

#if 0
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

  self = xmlNewChild (parent, NULL, (xmlChar *) "ghostpad", NULL);
  xmlNewChild (self, NULL, (xmlChar *) "name", (xmlChar *) GST_PAD_NAME (pad));
  xmlNewChild (self, NULL, (xmlChar *) "parent",
      (xmlChar *) GST_OBJECT_NAME (GST_PAD_PARENT (pad)));

  /* FIXME FIXME FIXME! */

  return self;
}
#endif /* 0 */
#endif /* GST_DISABLE_LOADSAVE */

/* 
 * should be called with pad lock held 
 *
 * MT safe.
 */
static void
handle_pad_block (GstPad * pad)
{
  GstPadBlockCallback callback;
  gpointer user_data;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "signal block taken on pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* need to grab extra ref for the callbacks */
  gst_object_ref (pad);

  callback = pad->block_callback;
  if (callback) {
    user_data = pad->block_data;
    GST_UNLOCK (pad);
    callback (pad, TRUE, user_data);
    GST_LOCK (pad);
  } else {
    GST_PAD_BLOCK_SIGNAL (pad);
  }

  while (GST_PAD_IS_BLOCKED (pad))
    GST_PAD_BLOCK_WAIT (pad);

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "got unblocked");

  callback = pad->block_callback;
  if (callback) {
    user_data = pad->block_data;
    GST_UNLOCK (pad);
    callback (pad, FALSE, user_data);
    GST_LOCK (pad);
  } else {
    GST_PAD_BLOCK_SIGNAL (pad);
  }

  gst_object_unref (pad);
}

/**********************************************************************
 * Data passing functions
 */

/**
 * gst_pad_chain:
 * @pad: a sink #GstPad.
 * @buffer: the #GstBuffer to send.
 *
 * Chain a buffer to @pad.
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_chain (GstPad * pad, GstBuffer * buffer)
{
  GstCaps *caps;
  gboolean caps_changed, do_pass = TRUE;
  GstPadChainFunction chainfunc;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK,
      GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  GST_STREAM_LOCK (pad);

  GST_LOCK (pad);
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
    goto flushing;

  caps = GST_BUFFER_CAPS (buffer);
  caps_changed = caps && caps != GST_PAD_CAPS (pad);
  GST_UNLOCK (pad);

  /* we got a new datatype on the pad, see if it can handle it */
  if (G_UNLIKELY (caps_changed)) {
    GST_DEBUG ("caps changed to %" GST_PTR_FORMAT, caps);
    if (G_UNLIKELY (!gst_pad_configure_sink (pad, caps)))
      goto not_negotiated;
  }

  /* NOTE: we read the chainfunc unlocked. 
   * we cannot hold the lock for the pad so we might send
   * the data to the wrong function. This is not really a
   * problem since functions are assigned at creation time
   * and don't change that often... */
  if (G_UNLIKELY ((chainfunc = GST_PAD_CHAINFUNC (pad)) == NULL))
    goto no_function;

  if (g_atomic_int_get (&pad->emit_buffer_signals) >= 1) {
    g_signal_emit (pad, gst_pad_signals[PAD_HAVE_DATA], 0, buffer, &do_pass);
  }

  if (do_pass) {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "calling chainfunction &%s of pad %s:%s",
        GST_DEBUG_FUNCPTR_NAME (chainfunc), GST_DEBUG_PAD_NAME (pad));

    ret = chainfunc (pad, buffer);
  } else {
    GST_DEBUG ("Dropping buffer due to FALSE probe return");
    gst_buffer_unref (buffer);
    ret = GST_FLOW_UNEXPECTED;
  }

  GST_STREAM_UNLOCK (pad);

  return ret;

  /* ERRORS */
flushing:
  {
    gst_buffer_unref (buffer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but pad was flushing");
    GST_UNLOCK (pad);
    GST_STREAM_UNLOCK (pad);
    return GST_FLOW_UNEXPECTED;
  }
not_negotiated:
  {
    gst_buffer_unref (buffer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing buffer but pad did not accept");
    GST_STREAM_UNLOCK (pad);
    return GST_FLOW_NOT_NEGOTIATED;
  }
no_function:
  {
    gst_buffer_unref (buffer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but not chainhandler");
    GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
        ("push on pad %s:%s but it has no chainfunction",
            GST_DEBUG_PAD_NAME (pad)));
    GST_STREAM_UNLOCK (pad);
    return GST_FLOW_ERROR;
  }
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
  GstPad *peer;
  GstFlowReturn ret;
  gboolean do_pass = TRUE;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC, GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  GST_LOCK (pad);
  while (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad)))
    handle_pad_block (pad);

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto not_linked;

  gst_object_ref (peer);
  GST_UNLOCK (pad);

  if (g_atomic_int_get (&pad->emit_buffer_signals) >= 1) {
    g_signal_emit (pad, gst_pad_signals[PAD_HAVE_DATA], 0, buffer, &do_pass);
  }

  if (do_pass) {
    ret = gst_pad_chain (peer, buffer);
  } else {
    GST_DEBUG ("Dropping buffer due to FALSE probe return");
    gst_buffer_unref (buffer);
    ret = GST_FLOW_UNEXPECTED;
  }

  gst_object_unref (peer);

  return ret;

  /* ERROR recovery here */
not_linked:
  {
    gst_buffer_unref (buffer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but it was not linked");
    GST_UNLOCK (pad);
    return GST_FLOW_NOT_LINKED;
  }
}

/**
 * gst_pad_check_pull_range:
 * @pad: a sink #GstPad.
 *
 * Checks if a #gst_pad_pull_range() can be performed on the peer
 * source pad. This function is used by plugins that want to check
 * if they can use random access on the peer source pad. 
 *
 * The peer sourcepad can implement a custom #GstPadCheckGetRangeFunction
 * if it needs to perform some logic to determine if pull_range is
 * possible.
 *
 * Returns: a gboolean with the result.
 *
 * MT safe.
 */
gboolean
gst_pad_check_pull_range (GstPad * pad)
{
  GstPad *peer;
  gboolean ret;
  GstPadCheckGetRangeFunction checkgetrangefunc;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  if (GST_PAD_DIRECTION (pad) != GST_PAD_SINK)
    goto wrong_direction;

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto not_connected;

  gst_object_ref (peer);
  GST_UNLOCK (pad);

  /* see note in above function */
  if (G_LIKELY ((checkgetrangefunc = peer->checkgetrangefunc) == NULL)) {
    ret = GST_PAD_GETRANGEFUNC (peer) != NULL;
  } else {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "calling checkgetrangefunc %s of peer pad %s:%s",
        GST_DEBUG_FUNCPTR_NAME (checkgetrangefunc), GST_DEBUG_PAD_NAME (peer));

    ret = checkgetrangefunc (peer);
  }

  gst_object_unref (peer);

  return ret;

  /* ERROR recovery here */
wrong_direction:
  {
    GST_UNLOCK (pad);
    return FALSE;
  }
not_connected:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "checking pull range, but it was not linked");
    GST_UNLOCK (pad);
    return FALSE;
  }
}

/**
 * gst_pad_get_range:
 * @pad: a src #GstPad.
 * @buffer: a pointer to hold the #GstBuffer.
 * @offset: The start offset of the buffer
 * @length: The length of the buffer
 *
 * Calls the getrange function of @pad. 
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_get_range (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstFlowReturn ret;
  GstPadGetRangeFunction getrangefunc;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC, GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  GST_STREAM_LOCK (pad);

  GST_LOCK (pad);
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
    goto flushing;
  GST_UNLOCK (pad);

  if (G_UNLIKELY ((getrangefunc = GST_PAD_GETRANGEFUNC (pad)) == NULL))
    goto no_function;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "calling getrangefunc %s of peer pad %s:%s, offset %"
      G_GUINT64_FORMAT ", size %u",
      GST_DEBUG_FUNCPTR_NAME (getrangefunc), GST_DEBUG_PAD_NAME (pad),
      offset, size);

  ret = getrangefunc (pad, offset, size, buffer);

  GST_STREAM_UNLOCK (pad);

  if (ret == GST_FLOW_OK && g_atomic_int_get (&pad->emit_buffer_signals) >= 1) {
    gboolean do_pass = TRUE;

    g_signal_emit (pad, gst_pad_signals[PAD_HAVE_DATA], 0, *buffer, &do_pass);
    if (!do_pass) {
      GST_DEBUG ("Dropping data after FALSE probe return");
      gst_buffer_unref (*buffer);
      *buffer = NULL;
      ret = GST_FLOW_UNEXPECTED;
    }
  }

  return ret;

  /* ERRORS */
flushing:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pulling range, but pad was flushing");
    GST_UNLOCK (pad);
    GST_STREAM_UNLOCK (pad);
    return GST_FLOW_UNEXPECTED;
  }
no_function:
  {
    GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
        ("pullrange on pad %s:%s but it has no getrangefunction",
            GST_DEBUG_PAD_NAME (pad)));
    GST_STREAM_UNLOCK (pad);
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
  GstPad *peer;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK,
      GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  GST_LOCK (pad);

  while (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad)))
    handle_pad_block (pad);

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto not_connected;

  gst_object_ref (peer);
  GST_UNLOCK (pad);

  ret = gst_pad_get_range (peer, offset, size, buffer);

  gst_object_unref (peer);

  if (ret == GST_FLOW_OK && g_atomic_int_get (&pad->emit_buffer_signals) >= 1) {
    gboolean do_pass = TRUE;

    g_signal_emit (pad, gst_pad_signals[PAD_HAVE_DATA], 0, *buffer, &do_pass);
    if (!do_pass) {
      GST_DEBUG ("Dropping data after FALSE probe return");
      gst_buffer_unref (*buffer);
      *buffer = NULL;
      ret = GST_FLOW_UNEXPECTED;
    }
  }

  return ret;

  /* ERROR recovery here */
not_connected:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pulling range, but it was not linked");
    GST_UNLOCK (pad);
    return GST_FLOW_NOT_LINKED;
  }
}

/**
 * gst_pad_push_event:
 * @pad: a #GstPad to push the event to.
 * @event: the #GstEvent to send to the pad.
 *
 * Sends the event to the peer of the given pad. This function is
 * mainly used by elements to send events to their peer
 * elements.
 *
 * Returns: TRUE if the event was handled.
 *
 * MT safe.
 */
gboolean
gst_pad_push_event (GstPad * pad, GstEvent * event)
{
  GstPad *peerpad;
  gboolean result, do_pass = TRUE;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  GST_LOCK (pad);
  peerpad = GST_PAD_PEER (pad);
  if (peerpad == NULL)
    goto not_linked;

  gst_object_ref (peerpad);
  GST_UNLOCK (pad);

  if (g_atomic_int_get (&pad->emit_event_signals) >= 1) {
    g_signal_emit (pad, gst_pad_signals[PAD_HAVE_DATA], 0, event, &do_pass);
  }

  if (do_pass) {
    result = gst_pad_send_event (peerpad, event);
  } else {
    GST_DEBUG ("Dropping event after FALSE probe return");
    gst_event_unref (event);
    result = FALSE;
  }

  gst_object_unref (peerpad);

  return result;

  /* ERROR handling */
not_linked:
  {
    gst_event_unref (event);
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
  gboolean result = FALSE, do_pass = TRUE;
  GstPadEventFunction eventfunc;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  GST_LOCK (pad);

  if (GST_EVENT_SRC (event) == NULL)
    GST_EVENT_SRC (event) = gst_object_ref (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      GST_CAT_DEBUG (GST_CAT_EVENT, "have event type %d (FLUSH) on pad %s:%s",
          GST_EVENT_TYPE (event), GST_DEBUG_PAD_NAME (pad));

      if (GST_EVENT_FLUSH_DONE (event)) {
        GST_PAD_UNSET_FLUSHING (pad);
        GST_CAT_DEBUG (GST_CAT_EVENT, "cleared flush flag");
      } else {
        /* can't even accept a flush begin event when flushing */
        if (GST_PAD_IS_FLUSHING (pad))
          goto flushing;
        GST_PAD_SET_FLUSHING (pad);
        GST_CAT_DEBUG (GST_CAT_EVENT, "set flush flag");
      }
      break;
    default:
      GST_CAT_DEBUG (GST_CAT_EVENT, "have event type %d on pad %s:%s",
          GST_EVENT_TYPE (event), GST_DEBUG_PAD_NAME (pad));

      if (GST_PAD_IS_FLUSHING (pad))
        goto flushing;
      break;
  }

  if ((eventfunc = GST_PAD_EVENTFUNC (pad)) == NULL)
    goto no_function;

  gst_object_ref (pad);
  GST_UNLOCK (pad);

  if (g_atomic_int_get (&pad->emit_event_signals) >= 1) {
    g_signal_emit (pad, gst_pad_signals[PAD_HAVE_DATA], 0, event, &do_pass);
  }

  if (do_pass) {
    result = eventfunc (GST_PAD_CAST (pad), event);
  } else {
    GST_DEBUG ("Dropping event after FALSE probe return");
    gst_event_unref (event);
    result = FALSE;
  }

  gst_object_unref (pad);

  return result;

  /* ERROR handling */
no_function:
  {
    g_warning ("pad %s:%s has no event handler, file a bug.",
        GST_DEBUG_PAD_NAME (pad));
    GST_UNLOCK (pad);
    gst_event_unref (event);
    return FALSE;
  }
flushing:
  {
    GST_UNLOCK (pad);
    GST_CAT_DEBUG (GST_CAT_EVENT, "Received event on flushing pad. Discarding");
    gst_event_unref (event);
    return FALSE;
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
 * gst_static_pad_template_get_caps:
 * @templ: a #GstStaticPadTemplate to get capabilities of.
 *
 * Gets the capabilities of the static pad template.
 *
 * Returns: the #GstCaps of the static pad template. If you need to keep a 
 * reference to the caps, take a ref (see gst_caps_ref ()).
 */
GstCaps *
gst_static_pad_template_get_caps (GstStaticPadTemplate * templ)
{
  g_return_val_if_fail (templ, NULL);

  return (GstCaps *) gst_static_caps_get (&templ->static_caps);
}

/**
 * gst_pad_template_get_caps:
 * @templ: a #GstPadTemplate to get capabilities of.
 *
 * Gets the capabilities of the pad template.
 *
 * Returns: the #GstCaps of the pad template. If you need to keep a reference to
 * the caps, take a ref (see gst_caps_ref ()).
 */
GstCaps *
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

/**
 * gst_pad_start_task:
 * @pad: the #GstPad to start the task of
 * @func: the task function to call
 * @data: data passed to the task function
 *
 * Starts a task that repeadedly calls @func with @data. This function
 * is nostly used in the pad activation function to start the
 * dataflow. This function will automatically acauire the STREAM_LOCK of
 * the pad before calling @func.
 *
 * Returns: a TRUE if the task could be started. FALSE when the pad has
 * no parent or the parent has no scheduler.
 */
gboolean
gst_pad_start_task (GstPad * pad, GstTaskFunction func, gpointer data)
{
  GstElement *parent;
  GstScheduler *sched;
  GstTask *task;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  GST_LOCK (pad);
  parent = GST_PAD_PARENT (pad);

  if (parent == NULL || !GST_IS_ELEMENT (parent))
    goto no_parent;

  sched = GST_ELEMENT_SCHEDULER (parent);
  if (sched == NULL)
    goto no_sched;

  task = GST_PAD_TASK (pad);
  if (task == NULL) {
    task = gst_scheduler_create_task (sched, func, data);
    gst_task_set_lock (task, GST_STREAM_GET_LOCK (pad));

    GST_PAD_TASK (pad) = task;
  }
  GST_UNLOCK (pad);

  gst_task_start (task);

  return TRUE;

  /* ERRORS */
no_parent:
  {
    GST_UNLOCK (pad);
    GST_DEBUG ("no parent");
    return FALSE;
  }
no_sched:
  {
    GST_UNLOCK (pad);
    GST_DEBUG ("no scheduler");
    return FALSE;
  }
}

/**
 * gst_pad_pause_task:
 * @pad: the #GstPad to pause the task of
 *
 * Pause the task of @pad. This function will also make sure that the 
 * function executed by the task will effectively stop.
 *
 * Returns: a TRUE if the task could be paused or FALSE when the pad
 * has no task.
 */
gboolean
gst_pad_pause_task (GstPad * pad)
{
  GstTask *task;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL)
    goto no_task;
  gst_task_pause (task);
  GST_UNLOCK (pad);

  GST_STREAM_LOCK (pad);
  GST_STREAM_UNLOCK (pad);

  return TRUE;

no_task:
  {
    GST_UNLOCK (pad);
    return TRUE;
  }
}

/**
 * gst_pad_stop_task:
 * @pad: the #GstPad to stop the task of
 *
 * Stop the task of @pad. This function will also make sure that the 
 * function executed by the task will effectively stop.
 *
 * Returns: a TRUE if the task could be stopped or FALSE when the pad
 * has no task.
 */
gboolean
gst_pad_stop_task (GstPad * pad)
{
  GstTask *task;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL)
    goto no_task;
  GST_PAD_TASK (pad) = NULL;
  GST_UNLOCK (pad);

  gst_task_stop (task);

  GST_STREAM_LOCK (pad);
  GST_STREAM_UNLOCK (pad);

  gst_object_unref (task);

  return TRUE;

no_task:
  {
    GST_UNLOCK (pad);
    return TRUE;
  }
}
