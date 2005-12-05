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
/**
 * SECTION:gstpad
 * @short_description: Object contained by elements that allows links to
 *                     other elements
 * @see_also: #GstPadTemplate, #GstElement, #GstEvent
 *
 * A #GstElement is linked to other elements via "pads", which are extremely
 * light-weight generic link points.
 * After two pads are retrieved from an element with gst_element_get_pad(),
 * the pads can be link with gst_pad_link(). (For quick links,
 * you can also use gst_element_link(), which will make the obvious
 * link for you if it's straightforward.)
 *
 * Pads are typically created from a #GstPadTemplate with
 * gst_pad_new_from_template().
 *
 * Pads have #GstCaps attached to it to describe the media type they are
 * capable of dealing with.  gst_pad_get_caps() and gst_pad_set_caps() are
 * used to manipulate the caps of the pads.
 * Pads created from a pad template cannot set capabilities that are
 * incompatible with the pad template capabilities.
 *
 * Pads without pad templates can be created with gst_pad_new(),
 * which takes a direction and a name as an argument.  If the name is NULL,
 * then a guaranteed unique name will be assigned to it.
 *
 * gst_pad_get_parent() will retrieve the #GstElement that owns the pad.
 *
 * A #GstElement creating a pad will typically use the various
 * gst_pad_set_*_function() calls to register callbacks for various events
 * on the pads.
 *
 * GstElements will use gst_pad_push() and gst_pad_pull_range() to push out
 * or pull in a buffer.
 *
 * To send a #GstEvent on a pad, use gst_pad_send_event().
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 *
 */

#include "gst_private.h"

#include "gstpad.h"
#include "gstpadtemplate.h"
#include "gstenumtypes.h"
#include "gstmarshal.h"
#include "gstutils.h"
#include "gstinfo.h"
#include "gsterror.h"
#include "gstvalue.h"
#include "glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (debug_dataflow);
#define GST_CAT_DEFAULT GST_CAT_PADS

/* Pad signals and args */
enum
{
  PAD_LINKED,
  PAD_UNLINKED,
  PAD_REQUEST_LINK,
  PAD_HAVE_DATA,
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PAD_PROP_0,
  PAD_PROP_CAPS,
  PAD_PROP_DIRECTION,
  PAD_PROP_TEMPLATE,
  /* FILL ME */
};

static void gst_pad_class_init (GstPadClass * klass);
static void gst_pad_init (GstPad * pad);
static void gst_pad_dispose (GObject * object);
static void gst_pad_finalize (GObject * object);
static void gst_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn handle_pad_block (GstPad * pad);
static GstCaps *gst_pad_get_caps_unlocked (GstPad * pad);
static void gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ);
static gboolean gst_pad_activate_default (GstPad * pad);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_pad_save_thyself (GstObject * object, xmlNodePtr parent);
#endif

static GstObjectClass *parent_class = NULL;
static guint gst_pad_signals[LAST_SIGNAL] = { 0 };

/* quarks for probe signals */
static GQuark buffer_quark;
static GQuark event_quark;

typedef struct
{
  gint ret;
  gchar *name;
  GQuark quark;
} GstFlowQuarks;

static GstFlowQuarks flow_quarks[] = {
  {GST_FLOW_RESEND, "resend", 0},
  {GST_FLOW_OK, "ok", 0},
  {GST_FLOW_NOT_LINKED, "not-linked", 0},
  {GST_FLOW_WRONG_STATE, "wrong-state", 0},
  {GST_FLOW_UNEXPECTED, "unexpected", 0},
  {GST_FLOW_NOT_NEGOTIATED, "not-negotiated", 0},
  {GST_FLOW_ERROR, "error", 0},
  {GST_FLOW_NOT_SUPPORTED, "not-supported", 0},

  {0, NULL, 0}
};

/**
 * gst_flow_get_name:
 * @ret: a #GstFlowReturn to get the name of.
 *
 * Gets a string representing the given flow return.
 *
 * Returns: a string with the name of the flow return.
 */
G_CONST_RETURN gchar *
gst_flow_get_name (GstFlowReturn ret)
{
  gint i;

  for (i = 0; flow_quarks[i].name; i++) {
    if (ret == flow_quarks[i].ret)
      return flow_quarks[i].name;
  }
  return "unknown";
}

/**
 * gst_flow_to_quark:
 * @ret: a #GstFlowReturn to get the quark of.
 *
 * Get the unique quark for the given GstFlowReturn.
 *
 * Returns: the quark associated with the flow return or 0 if an
 * invalid return was specified.
 */
GQuark
gst_flow_to_quark (GstFlowReturn ret)
{
  gint i;

  for (i = 0; flow_quarks[i].name; i++) {
    if (ret == flow_quarks[i].ret)
      return flow_quarks[i].quark;
  }
  return 0;
}

GType
gst_pad_get_type (void)
{
  static GType gst_pad_type = 0;

  if (!gst_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstPadClass), NULL, NULL,
      (GClassInitFunc) gst_pad_class_init, NULL, NULL,
      sizeof (GstPad),
      0,
      (GInstanceInitFunc) gst_pad_init, NULL
    };
    gint i;

    gst_pad_type = g_type_register_static (GST_TYPE_OBJECT, "GstPad",
        &pad_info, 0);

    buffer_quark = g_quark_from_static_string ("buffer");
    event_quark = g_quark_from_static_string ("event");

    for (i = 0; flow_quarks[i].name; i++) {
      flow_quarks[i].quark = g_quark_from_static_string (flow_quarks[i].name);
    }

    GST_DEBUG_CATEGORY_INIT (debug_dataflow, "GST_DATAFLOW",
        GST_DEBUG_BOLD | GST_DEBUG_FG_GREEN, "dataflow inside pads");
  }
  return gst_pad_type;
}

static gboolean
_gst_do_pass_data_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gboolean ret = g_value_get_boolean (handler_return);

  GST_DEBUG ("accumulated %d", ret);
  g_value_set_boolean (return_accu, ret);

  return ret;
}

static gboolean
default_have_data (GstPad * pad, GstMiniObject * o)
{
  return TRUE;
}

static void
gst_pad_class_init (GstPadClass * klass)
{
  GObjectClass *gobject_class;


  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pad_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pad_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pad_get_property);

  /**
   * GstPad::linked:
   * @pad: the pad that emitted the signal
   * @peer: the peer pad that has been connected
   *
   * Signals that a pad has been linked to the peer pad.
   */
  gst_pad_signals[PAD_LINKED] =
      g_signal_new ("linked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPadClass, linked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  /**
   * GstPad::unlinked:
   * @pad: the pad that emitted the signal
   * @peer: the peer pad that has been disconnected
   *
   * Signals that a pad has been unlinked from the peer pad.
   */
  gst_pad_signals[PAD_UNLINKED] =
      g_signal_new ("unlinked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPadClass, unlinked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  /**
   * GstPad::request-link:
   * @pad: the pad that emitted the signal
   * @peer: the peer pad for which a connection is requested
   *
   * Signals that a pad connection has been requested.
   */
  gst_pad_signals[PAD_REQUEST_LINK] =
      g_signal_new ("request-link", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstPadClass, request_link), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 0);

  /**
   * GstPad::have-data:
   * @pad: the pad that emitted the signal
   * @mini_obj: new data
   *
   * Signals that new data is available on the pad. This signal is used
   * internally for implementing pad probes.
   * See gst_pad_add_*_probe functions.
   *
   * Returns: %TRUE to keep the data, %FALSE to drop it
   */
  gst_pad_signals[PAD_HAVE_DATA] =
      g_signal_new ("have-data", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET (GstPadClass, have_data),
      _gst_do_pass_data_accumulator,
      NULL, gst_marshal_BOOLEAN__POINTER, G_TYPE_BOOLEAN, 1,
      GST_TYPE_MINI_OBJECT);

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
          G_PARAM_READWRITE));

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_pad_save_thyself);
#endif
  gstobject_class->path_string_separator = ".";

  klass->have_data = default_have_data;
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

  pad->activatefunc = GST_DEBUG_FUNCPTR (gst_pad_activate_default);
  pad->eventfunc = GST_DEBUG_FUNCPTR (gst_pad_event_default);
  pad->querytypefunc = GST_DEBUG_FUNCPTR (gst_pad_get_query_types_default);
  pad->queryfunc = GST_DEBUG_FUNCPTR (gst_pad_query_default);
  pad->intlinkfunc = GST_DEBUG_FUNCPTR (gst_pad_get_internal_links_default);

  pad->do_buffer_signals = 0;
  pad->do_event_signals = 0;

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

  GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "dispose %s:%s",
      GST_DEBUG_PAD_NAME (pad));

  /* we don't hold a ref to the peer so we can just set the
   * peer to NULL. */
  GST_PAD_PEER (pad) = NULL;

  /* clear the caps */
  gst_caps_replace (&GST_PAD_CAPS (pad), NULL);

  gst_pad_set_pad_template (pad, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pad_finalize (GObject * object)
{
  GstPad *pad = GST_PAD (object);
  GstTask *task;

  /* in case the task is still around, clean it up */
  if ((task = GST_PAD_TASK (pad))) {
    gst_task_join (task);
    GST_PAD_TASK (pad) = NULL;
    gst_object_unref (task);
  }

  if (pad->stream_rec_lock) {
    g_static_rec_mutex_free (pad->stream_rec_lock);
    g_free (pad->stream_rec_lock);
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

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
          (GstPadTemplate *) g_value_dup_gst_object (value));
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
      g_value_set_object (value, GST_PAD_PAD_TEMPLATE (object));
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
 * gst_pad_new_from_static_template:
 * @templ: the #GstStaticPadTemplate to use
 * @name: the name of the element
 *
 * Creates a new pad with the given name from the given static template.
 * If name is NULL, a guaranteed unique name (across all pads)
 * will be assigned.
 * This function makes a copy of the name so you can safely free the name.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_new_from_static_template (GstStaticPadTemplate * templ,
    const gchar * name)
{
  GstPad *pad;
  GstPadTemplate *template;

  template = gst_static_pad_template_get (templ);
  pad = gst_pad_new_from_template (template, name);
  gst_object_unref (template);
  return pad;
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

  GST_OBJECT_LOCK (pad);
  result = GST_PAD_DIRECTION (pad);
  GST_OBJECT_UNLOCK (pad);

  return result;
}

static gboolean
gst_pad_activate_default (GstPad * pad)
{
  return gst_pad_activate_push (pad, TRUE);
}

static void
pre_activate (GstPad * pad, GstActivateMode new_mode)
{
  switch (new_mode) {
    case GST_ACTIVATE_PUSH:
    case GST_ACTIVATE_PULL:
      GST_OBJECT_LOCK (pad);
      GST_DEBUG_OBJECT (pad, "setting ACTIVATE_MODE %d, unset flushing",
          new_mode);
      GST_PAD_UNSET_FLUSHING (pad);
      GST_PAD_ACTIVATE_MODE (pad) = new_mode;
      GST_OBJECT_UNLOCK (pad);
      break;
    case GST_ACTIVATE_NONE:
      GST_OBJECT_LOCK (pad);
      GST_DEBUG_OBJECT (pad, "setting ACTIVATE_MODE NONE, set flushing",
          new_mode);
      GST_PAD_SET_FLUSHING (pad);
      /* unlock blocked pads so element can resume and stop */
      GST_PAD_BLOCK_SIGNAL (pad);
      GST_OBJECT_UNLOCK (pad);
      break;
  }
}

static void
post_activate (GstPad * pad, GstActivateMode new_mode)
{
  switch (new_mode) {
    case GST_ACTIVATE_PUSH:
    case GST_ACTIVATE_PULL:
      /* nop */
      break;
    case GST_ACTIVATE_NONE:
      /* ensures that streaming stops */
      GST_PAD_STREAM_LOCK (pad);
      /* while we're at it set activation mode */
      GST_OBJECT_LOCK (pad);
      GST_DEBUG_OBJECT (pad, "setting ACTIVATE_MODE %d", new_mode);
      GST_PAD_ACTIVATE_MODE (pad) = new_mode;
      GST_OBJECT_UNLOCK (pad);
      GST_PAD_STREAM_UNLOCK (pad);
      break;
  }
}

/**
 * gst_pad_set_active:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether or not the pad should be active.
 *
 * Activates or deactivates the given pad.
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
 * Returns: #TRUE if the operation was successful.
 *
 * MT safe.
 */
gboolean
gst_pad_set_active (GstPad * pad, gboolean active)
{
  GstActivateMode old;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_OBJECT_LOCK (pad);
  old = GST_PAD_ACTIVATE_MODE (pad);
  GST_OBJECT_UNLOCK (pad);

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

  if (!active && !ret) {
    GST_OBJECT_LOCK (pad);
    g_critical ("Failed to deactivate pad %s:%s, very bad",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
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
 * Returns: TRUE if the operation was successful.
 *
 * MT safe.
 */
gboolean
gst_pad_activate_pull (GstPad * pad, gboolean active)
{
  GstActivateMode old, new;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_OBJECT_LOCK (pad);
  old = GST_PAD_ACTIVATE_MODE (pad);
  GST_OBJECT_UNLOCK (pad);

  if ((active && old == GST_ACTIVATE_PULL)
      || (!active && old == GST_ACTIVATE_NONE))
    goto was_ok;

  if (active) {
    g_return_val_if_fail (old == GST_ACTIVATE_NONE, FALSE);
  } else {
    g_return_val_if_fail (old == GST_ACTIVATE_PULL, FALSE);
  }

  if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
    if ((peer = gst_pad_get_peer (pad))) {
      if (!gst_pad_activate_pull (peer, active))
        goto peer_failed;
      gst_object_unref (peer);
    }
  }

  new = active ? GST_ACTIVATE_PULL : GST_ACTIVATE_NONE;
  pre_activate (pad, new);

  if (GST_PAD_ACTIVATEPULLFUNC (pad)) {
    if (!GST_PAD_ACTIVATEPULLFUNC (pad) (pad, active))
      goto failure;
  } else {
    /* can happen for sinks of passthrough elements */
  }

  post_activate (pad, new);

  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "%s in pull mode",
      active ? "activated" : "deactivated");
  return TRUE;

was_ok:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "already %s in pull mode",
        active ? "activated" : "deactivated");
    return TRUE;
  }
peer_failed:
  {
    GST_OBJECT_LOCK (peer);
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad,
        "activate_pull on peer (%s:%s) failed", GST_DEBUG_PAD_NAME (peer));
    GST_OBJECT_UNLOCK (peer);
    gst_object_unref (peer);
    return FALSE;
  }
failure:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_PADS, pad, "failed to %s in pull mode",
        active ? "activate" : "deactivate");
    pre_activate (pad, GST_ACTIVATE_NONE);
    post_activate (pad, GST_ACTIVATE_NONE);
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
  GstActivateMode old, new;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "trying to set %s in push mode",
      active ? "activated" : "deactivated");

  GST_OBJECT_LOCK (pad);
  old = GST_PAD_ACTIVATE_MODE (pad);
  GST_OBJECT_UNLOCK (pad);

  if ((active && old == GST_ACTIVATE_PUSH)
      || (!active && old == GST_ACTIVATE_NONE))
    goto was_ok;

  if (active) {
    g_return_val_if_fail (old == GST_ACTIVATE_NONE, FALSE);
  } else {
    g_return_val_if_fail (old == GST_ACTIVATE_PUSH, FALSE);
  }

  new = active ? GST_ACTIVATE_PUSH : GST_ACTIVATE_NONE;
  pre_activate (pad, new);

  if (GST_PAD_ACTIVATEPUSHFUNC (pad)) {
    if (!GST_PAD_ACTIVATEPUSHFUNC (pad) (pad, active)) {
      goto failure;
    }
  } else {
    /* quite ok, element relies on state change func to prepare itself */
  }

  post_activate (pad, new);

  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "%s in push mode",
      active ? "activated" : "deactivated");
  return TRUE;

was_ok:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "already %s in push mode",
        active ? "activated" : "deactivated");
    return TRUE;
  }
failure:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_PADS, pad, "failed to %s in push mode",
        active ? "activate" : "deactivate");
    pre_activate (pad, GST_ACTIVATE_NONE);
    post_activate (pad, GST_ACTIVATE_NONE);
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

  GST_OBJECT_LOCK (pad);
  result = GST_PAD_MODE_ACTIVATE (GST_PAD_ACTIVATE_MODE (pad));
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_set_blocked_async:
 * @pad: the #GstPad to block or unblock
 * @blocked: boolean indicating whether the pad should be blocked or unblocked
 * @callback: #GstPadBlockCallback that will be called when the
 *            operation succeeds
 * @user_data: user data passed to the callback
 *
 * Blocks or unblocks the dataflow on a pad. The provided callback
 * is called when the operation succeeds; this happens right before the next
 * attempt at pushing a buffer on the pad.
 *
 * This can take a while as the pad can only become blocked when real dataflow
 * is happening.
 * When the pipeline is stalled, for example in PAUSED, this can
 * take an indeterminate amount of time.
 * You can pass NULL as the callback to make this call block. Be careful with
 * this blocking call as it might not return for reasons stated above.
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

  GST_OBJECT_LOCK (pad);

  was_blocked = GST_PAD_IS_BLOCKED (pad);

  if (G_UNLIKELY (was_blocked == blocked))
    goto had_right_state;

  if (blocked) {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "blocking pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    GST_OBJECT_FLAG_SET (pad, GST_PAD_BLOCKED);
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

    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_BLOCKED);

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
  GST_OBJECT_UNLOCK (pad);

  return TRUE;

had_right_state:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pad %s:%s was in right state", GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    return FALSE;
  }
}

/**
 * gst_pad_set_blocked:
 * @pad: the #GstPad to block or unblock
 * @blocked: boolean indicating we should block or unblock
 *
 * Blocks or unblocks the dataflow on a pad. This function is
 * a shortcut for gst_pad_set_blocked_async() with a NULL
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

  GST_OBJECT_LOCK (pad);
  result = GST_OBJECT_FLAG_IS_SET (pad, GST_PAD_BLOCKED);
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_set_activate_function:
 * @pad: a sink #GstPad.
 * @activate: the #GstPadActivateFunction to set.
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
 * @activatepull: the #GstPadActivateModeFunction to set.
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
 * @activatepush: the #GstPadActivateModeFunction to set.
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
 * Sets the given link function for the pad. It will be called when
 * the pad is linked with another pad.
 *
 * The return value #GST_PAD_LINK_OK should be used when the connection can be
 * made.
 *
 * The return value #GST_PAD_LINK_REFUSED should be used when the connection
 * cannot be made for some reason.
 *
 * If @link is installed on a source pad, it should call the #GstPadLinkFunction
 * of the peer sink pad, if present.
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

  GST_OBJECT_LOCK (srcpad);

  if (G_UNLIKELY (GST_PAD_DIRECTION (srcpad) != GST_PAD_SRC))
    goto not_srcpad;

  GST_OBJECT_LOCK (sinkpad);

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

  GST_OBJECT_UNLOCK (sinkpad);
  GST_OBJECT_UNLOCK (srcpad);

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
    GST_OBJECT_UNLOCK (srcpad);
    return FALSE;
  }
not_sinkpad:
  {
    g_critical ("pad %s is not a sink pad", GST_PAD_NAME (sinkpad));
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
    return FALSE;
  }
not_linked_together:
  {
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
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

  GST_OBJECT_LOCK (pad);
  result = (GST_PAD_PEER (pad) != NULL);
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/* get the caps from both pads and see if the intersection
 * is not empty.
 *
 * This function should be called with the pad LOCK on both
 * pads
 */
static gboolean
gst_pad_link_check_compatible_unlocked (GstPad * src, GstPad * sink)
{
  GstCaps *srccaps;
  GstCaps *sinkcaps;

  srccaps = gst_pad_get_caps_unlocked (src);
  sinkcaps = gst_pad_get_caps_unlocked (sink);
  GST_CAT_DEBUG (GST_CAT_CAPS, "src caps %" GST_PTR_FORMAT, srccaps);
  GST_CAT_DEBUG (GST_CAT_CAPS, "sink caps %" GST_PTR_FORMAT, sinkcaps);

  /* if we have caps on both pads we can check the intersection */
  if (srccaps && sinkcaps) {
    GstCaps *icaps;

    icaps = gst_caps_intersect (srccaps, sinkcaps);
    gst_caps_unref (srccaps);
    gst_caps_unref (sinkcaps);

    GST_CAT_DEBUG (GST_CAT_CAPS,
        "intersection caps %p %" GST_PTR_FORMAT, icaps, icaps);

    if (!icaps || gst_caps_is_empty (icaps)) {
      GST_CAT_DEBUG (GST_CAT_CAPS, "intersection is empty");
      gst_caps_unref (icaps);
      return FALSE;
    }
    gst_caps_unref (icaps);
  }

  return TRUE;
}

/* check if the grandparents of both pads are the same.
 * This check is required so that we don't try to link
 * pads from elements in different bins without ghostpads.
 *
 * The LOCK should be helt on both pads
 */
static gboolean
gst_pad_link_check_hierarchy (GstPad * src, GstPad * sink)
{
  GstObject *psrc, *psink;
  gboolean res = TRUE;

  psrc = GST_OBJECT_PARENT (src);
  psink = GST_OBJECT_PARENT (sink);

  /* if one of the pads has no parent, we allow the link */
  if (psrc && psink) {
    /* if the parents are the same, we have a loop */
    if (psrc == psink) {
      GST_CAT_DEBUG (GST_CAT_CAPS, "pads have same parent %" GST_PTR_FORMAT,
          psrc);
      res = FALSE;
      goto done;
    }
    /* if they both have a parent, we check the grandparents */
    psrc = gst_object_get_parent (psrc);
    psink = gst_object_get_parent (psink);

    if (psrc != psink) {
      /* if they have grandparents but they are not the same */
      GST_CAT_DEBUG (GST_CAT_CAPS,
          "pads have different grandparents %" GST_PTR_FORMAT " and %"
          GST_PTR_FORMAT, psrc, psink);
      res = FALSE;
    }
    if (psrc)
      gst_object_unref (psrc);
    if (psink)
      gst_object_unref (psink);
  }
done:
  return res;
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

  GST_OBJECT_LOCK (srcpad);

  if (G_UNLIKELY (GST_PAD_DIRECTION (srcpad) != GST_PAD_SRC))
    goto not_srcpad;

  if (G_UNLIKELY (GST_PAD_PEER (srcpad) != NULL))
    goto src_was_linked;

  GST_OBJECT_LOCK (sinkpad);

  if (G_UNLIKELY (GST_PAD_DIRECTION (sinkpad) != GST_PAD_SINK))
    goto not_sinkpad;

  if (G_UNLIKELY (GST_PAD_PEER (sinkpad) != NULL))
    goto sink_was_linked;

  /* check hierarchy, pads can only be linked if the grandparents
   * are the same. */
  if (!gst_pad_link_check_hierarchy (srcpad, sinkpad))
    goto wrong_hierarchy;

  /* check pad caps for non-empty intersection */
  if (!gst_pad_link_check_compatible_unlocked (srcpad, sinkpad))
    goto no_format;

  /* FIXME check pad scheduling for non-empty intersection */

  return GST_PAD_LINK_OK;

not_srcpad:
  {
    g_critical ("pad %s is not a source pad", GST_PAD_NAME (srcpad));
    GST_OBJECT_UNLOCK (srcpad);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
src_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "src %s:%s was linked",
        GST_DEBUG_PAD_NAME (srcpad));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_OBJECT_UNLOCK (srcpad);
    return GST_PAD_LINK_WAS_LINKED;
  }
not_sinkpad:
  {
    g_critical ("pad %s is not a sink pad", GST_PAD_NAME (sinkpad));
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
    return GST_PAD_LINK_WRONG_DIRECTION;
  }
sink_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "sink %s:%s was linked",
        GST_DEBUG_PAD_NAME (sinkpad));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
    return GST_PAD_LINK_WAS_LINKED;
  }
wrong_hierarchy:
  {
    GST_CAT_INFO (GST_CAT_PADS, "pads have wrong hierarchy");
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
    return GST_PAD_LINK_WRONG_HIERARCHY;
  }
no_format:
  {
    GST_CAT_INFO (GST_CAT_PADS, "caps are incompatible");
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
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

  GST_OBJECT_UNLOCK (sinkpad);
  GST_OBJECT_UNLOCK (srcpad);

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

  GST_OBJECT_LOCK (srcpad);
  GST_OBJECT_LOCK (sinkpad);

  if (result == GST_PAD_LINK_OK) {
    GST_PAD_PEER (srcpad) = sinkpad;
    GST_PAD_PEER (sinkpad) = srcpad;

    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);

    /* fire off a signal to each of the pads telling them
     * that they've been linked */
    g_signal_emit (G_OBJECT (srcpad), gst_pad_signals[PAD_LINKED], 0, sinkpad);
    g_signal_emit (G_OBJECT (sinkpad), gst_pad_signals[PAD_LINKED], 0, srcpad);

    GST_CAT_INFO (GST_CAT_PADS, "linked %s:%s and %s:%s, successful",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
  } else {
    GST_CAT_INFO (GST_CAT_PADS, "link between %s:%s and %s:%s failed",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
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

  GST_OBJECT_LOCK (pad);
  gst_object_replace ((GstObject **) & pad->padtemplate, (GstObject *) templ);
  GST_OBJECT_UNLOCK (pad);

  if (templ)
    gst_pad_template_pad_created (templ, pad);
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

    GST_OBJECT_FLAG_SET (pad, GST_PAD_IN_GETCAPS);
    GST_OBJECT_UNLOCK (pad);
    result = GST_PAD_GETCAPSFUNC (pad) (pad);
    GST_OBJECT_LOCK (pad);
    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_IN_GETCAPS);

    if (result == NULL) {
      g_critical ("pad %s:%s returned NULL caps from getcaps function",
          GST_DEBUG_PAD_NAME (pad));
    } else {
      GST_CAT_DEBUG (GST_CAT_CAPS,
          "pad getcaps %s:%s returned %" GST_PTR_FORMAT,
          GST_DEBUG_PAD_NAME (pad), result);
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
 * Gets the capabilities this pad can produce or consume.
 * Note that this method doesn't necessarily returns the caps set by
 * #gst_pad_set_caps - use #GST_PAD_CAPS for that instead.
 * gst_pad_get_caps returns all possible caps a pad can operate with, using
 * the pad's get_caps function;
 * this returns the pad template caps if not explicitly set.
 *
 * Returns: a newly allocated copy of the #GstCaps of this pad.
 *
 * MT safe.
 */
GstCaps *
gst_pad_get_caps (GstPad * pad)
{
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_OBJECT_LOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "get pad caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  result = gst_pad_get_caps_unlocked (pad);
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_peer_get_caps:
 * @pad: a  #GstPad to get the peer capabilities of.
 *
 * Gets the capabilities of the peer connected to this pad.
 *
 * Returns: the #GstCaps of the peer pad. This function returns a new caps, so use
 * gst_caps_unref to get rid of it. this function returns NULL if there is no
 * peer pad.
 */
GstCaps *
gst_pad_peer_get_caps (GstPad * pad)
{
  GstPad *peerpad;
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_OBJECT_LOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "get peer caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  peerpad = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  gst_object_ref (peerpad);
  GST_OBJECT_UNLOCK (pad);

  result = gst_pad_get_caps (peerpad);

  gst_object_unref (peerpad);

  return result;

no_peer:
  {
    GST_OBJECT_UNLOCK (pad);
    return NULL;
  }
}

static gboolean
fixate_value (GValue * dest, const GValue * src)
{
  if (G_VALUE_TYPE (src) == GST_TYPE_INT_RANGE) {
    g_value_init (dest, G_TYPE_INT);
    g_value_set_int (dest, gst_value_get_int_range_min (src));
  } else if (G_VALUE_TYPE (src) == GST_TYPE_DOUBLE_RANGE) {
    g_value_init (dest, G_TYPE_DOUBLE);
    g_value_set_double (dest, gst_value_get_double_range_min (src));
  } else if (G_VALUE_TYPE (src) == GST_TYPE_LIST) {
    GValue temp = { 0 };

    gst_value_init_and_copy (&temp, gst_value_list_get_value (src, 0));
    if (!fixate_value (dest, &temp))
      gst_value_init_and_copy (dest, &temp);
    g_value_unset (&temp);
  } else if (G_VALUE_TYPE (src) == GST_TYPE_ARRAY) {
    gboolean res = FALSE;
    guint n;

    g_value_init (dest, GST_TYPE_ARRAY);
    for (n = 0; n < gst_value_array_get_size (src); n++) {
      GValue kid = { 0 };
      const GValue *orig_kid = gst_value_array_get_value (src, n);

      if (!fixate_value (&kid, orig_kid))
        gst_value_init_and_copy (&kid, orig_kid);
      else
        res = TRUE;
      gst_value_array_append_value (dest, &kid);
      g_value_unset (&kid);
    }

    if (!res)
      g_value_unset (dest);

    return res;
  } else {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_pad_default_fixate (GQuark field_id, const GValue * value, gpointer data)
{
  GstStructure *s = data;
  GValue v = { 0 };

  if (fixate_value (&v, value)) {
    gst_structure_id_set_value (s, field_id, &v);
    g_value_unset (&v);
  }

  return TRUE;
}

/**
 * gst_pad_fixate_caps:
 * @pad: a  #GstPad to fixate
 * @caps: the  #GstCaps to fixate
 *
 * Fixate a caps on the given pad. Modifies the caps in place, so you should
 * make sure that the caps are actually writable (see gst_caps_make_writable()).
 */
void
gst_pad_fixate_caps (GstPad * pad, GstCaps * caps)
{
  GstPadFixateCapsFunction fixatefunc;
  guint n;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (caps != NULL);

  if (gst_caps_is_fixed (caps))
    return;

  fixatefunc = GST_PAD_FIXATECAPSFUNC (pad);
  if (fixatefunc) {
    fixatefunc (pad, caps);
  }

  /* default fixation */
  for (n = 0; n < gst_caps_get_size (caps); n++) {
    GstStructure *s = gst_caps_get_structure (caps, n);

    gst_structure_foreach (s, gst_pad_default_fixate, s);
  }
}

/**
 * gst_pad_accept_caps:
 * @pad: a #GstPad to check
 * @caps: a #GstCaps to check on the pad
 *
 * Check if the given pad accepts the caps.
 *
 * Returns: TRUE if the pad can accept the caps.
 */
gboolean
gst_pad_accept_caps (GstPad * pad, GstCaps * caps)
{
  gboolean result;
  GstPadAcceptCapsFunction acceptfunc;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* any pad can be unnegotiated */
  if (caps == NULL)
    return TRUE;

  GST_OBJECT_LOCK (pad);
  acceptfunc = GST_PAD_ACCEPTCAPSFUNC (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "pad accept caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);
  GST_OBJECT_UNLOCK (pad);

  if (acceptfunc) {
    /* we can call the function */
    result = acceptfunc (pad, caps);
  } else {
    /* else see get the caps and see if it intersects to something
     * not empty */
    GstCaps *intersect;
    GstCaps *allowed;

    allowed = gst_pad_get_caps (pad);
    if (allowed) {
      intersect = gst_caps_intersect (allowed, caps);

      result = !gst_caps_is_empty (intersect);

      gst_caps_unref (allowed);
      gst_caps_unref (intersect);
    } else {
      result = FALSE;
    }
  }
  return result;
}

/**
 * gst_pad_peer_accept_caps:
 * @pad: a  #GstPad to check
 * @caps: a #GstCaps to check on the pad
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

  GST_OBJECT_LOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "peer accept caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (pad), pad);

  peerpad = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  result = gst_pad_accept_caps (peerpad, caps);
  GST_OBJECT_UNLOCK (pad);

  return result;

no_peer:
  {
    GST_OBJECT_UNLOCK (pad);
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
  GstCaps *existing;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (caps == NULL || gst_caps_is_fixed (caps), FALSE);

  GST_OBJECT_LOCK (pad);
  setcaps = GST_PAD_SETCAPSFUNC (pad);

  existing = GST_PAD_CAPS (pad);
  if (caps == existing)
    goto setting_same_caps;
  else if (caps && existing && gst_caps_is_equal (caps, existing))
    goto setting_same_caps;

  /* call setcaps function to configure the pad */
  if (setcaps != NULL && caps) {
    if (!GST_PAD_IS_IN_SETCAPS (pad)) {
      GST_OBJECT_FLAG_SET (pad, GST_PAD_IN_SETCAPS);
      GST_OBJECT_UNLOCK (pad);
      if (!setcaps (pad, caps))
        goto could_not_set;
      GST_OBJECT_LOCK (pad);
      GST_OBJECT_FLAG_UNSET (pad, GST_PAD_IN_SETCAPS);
    } else {
      GST_CAT_DEBUG (GST_CAT_CAPS, "pad %s:%s was dispatching",
          GST_DEBUG_PAD_NAME (pad));
    }
  }

  gst_caps_replace (&GST_PAD_CAPS (pad), caps);
  GST_CAT_DEBUG (GST_CAT_CAPS, "%s:%s caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);
  GST_OBJECT_UNLOCK (pad);

  g_object_notify (G_OBJECT (pad), "caps");

  return TRUE;

setting_same_caps:
  {
    GST_OBJECT_UNLOCK (pad);
    gst_caps_replace (&GST_PAD_CAPS (pad), caps);
    GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
        "caps %" GST_PTR_FORMAT " same as existing, updating ptr only", caps);
    return TRUE;
  }
/* errors */
could_not_set:
  {
    GST_OBJECT_LOCK (pad);
    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_IN_SETCAPS);
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "pad %s:%s, caps %" GST_PTR_FORMAT " could not be set",
        GST_DEBUG_PAD_NAME (pad), caps);
    GST_OBJECT_UNLOCK (pad);

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

/* returns TRUE if the src pad could be configured to accept the given caps */
static gboolean
gst_pad_configure_src (GstPad * pad, GstCaps * caps, gboolean dosetcaps)
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
  if (dosetcaps)
    res = gst_pad_set_caps (pad, caps);
  else
    res = TRUE;

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

  GST_OBJECT_LOCK (pad);
  result = GST_PAD_PEER (pad);
  if (result)
    gst_object_ref (result);
  GST_OBJECT_UNLOCK (pad);

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

  GST_OBJECT_LOCK (srcpad);

  peer = GST_PAD_PEER (srcpad);
  if (G_UNLIKELY (peer == NULL))
    goto no_peer;

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting allowed caps",
      GST_DEBUG_PAD_NAME (srcpad));

  gst_object_ref (peer);
  GST_OBJECT_UNLOCK (srcpad);
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
    GST_OBJECT_UNLOCK (srcpad);

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

  GST_OBJECT_LOCK (pad);

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto no_peer;

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting negotiated caps",
      GST_DEBUG_PAD_NAME (pad));

  caps = GST_PAD_CAPS (pad);
  if (caps)
    gst_caps_ref (caps);
  GST_OBJECT_UNLOCK (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "negotiated caps %" GST_PTR_FORMAT, caps);

  return caps;

no_peer:
  {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: no peer",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);

    return NULL;
  }
}

static GstFlowReturn
gst_pad_alloc_buffer_full (GstPad * pad, guint64 offset, gint size,
    GstCaps * caps, GstBuffer ** buf, gboolean setcaps)
{
  GstPad *peer;
  GstFlowReturn ret;
  GstPadBufferAllocFunction bufferallocfunc;
  gboolean caps_changed;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  GST_OBJECT_LOCK (pad);
  while (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad)))
    if ((ret = handle_pad_block (pad)) != GST_FLOW_OK)
      goto flushed;

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto no_peer;

  gst_object_ref (peer);
  GST_OBJECT_UNLOCK (pad);

  if (G_LIKELY ((bufferallocfunc = peer->bufferallocfunc) == NULL))
    goto fallback;

  GST_OBJECT_LOCK (peer);
  /* when the peer is flushing we cannot give a buffer */
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (peer)))
    goto flushing;

  if (offset == GST_BUFFER_OFFSET_NONE) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad,
        "calling bufferallocfunc &%s (@%p) of peer pad %s:%s for size %d offset NONE",
        GST_DEBUG_FUNCPTR_NAME (bufferallocfunc),
        &bufferallocfunc, GST_DEBUG_PAD_NAME (peer), size);
  } else {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad,
        "calling bufferallocfunc &%s (@%p) of peer pad %s:%s for size %d offset %"
        G_GUINT64_FORMAT, GST_DEBUG_FUNCPTR_NAME (bufferallocfunc),
        &bufferallocfunc, GST_DEBUG_PAD_NAME (peer), size, offset);
  }
  GST_OBJECT_UNLOCK (peer);

  ret = bufferallocfunc (peer, offset, size, caps, buf);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto peer_error;
  if (G_UNLIKELY (*buf == NULL))
    goto fallback;

  /* If the buffer alloc function didn't set up the caps like it should,
   * do it for it */
  if (caps && (GST_BUFFER_CAPS (*buf) == NULL)) {
    GST_WARNING ("Buffer allocation function for pad % " GST_PTR_FORMAT
        " did not set up caps. Setting", peer);

    gst_buffer_set_caps (*buf, caps);
  }

do_caps:
  gst_object_unref (peer);

  /* FIXME, move capnego this into a base class? */
  caps = GST_BUFFER_CAPS (*buf);
  caps_changed = caps && caps != GST_PAD_CAPS (pad);
  /* we got a new datatype on the pad, see if it can handle it */
  if (G_UNLIKELY (caps_changed)) {
    GST_DEBUG ("caps changed to %" GST_PTR_FORMAT, caps);
    if (G_UNLIKELY (!gst_pad_configure_src (pad, caps, setcaps)))
      goto not_negotiated;
  }
  return ret;

flushed:
  {
    GST_CAT_DEBUG (GST_CAT_PADS, "%s:%s pad block stopped by flush",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    return ret;
  }
no_peer:
  {
    /* pad has no peer */
    GST_CAT_DEBUG (GST_CAT_PADS,
        "%s:%s called bufferallocfunc but had no peer",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    return GST_FLOW_NOT_LINKED;
  }
flushing:
  {
    /* peer was flushing */
    GST_OBJECT_UNLOCK (peer);
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
    GST_BUFFER_OFFSET (*buf) = offset;
    gst_buffer_set_caps (*buf, caps);

    ret = GST_FLOW_OK;

    goto do_caps;
  }
not_negotiated:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "alloc function returned unacceptable buffer");
    return GST_FLOW_NOT_NEGOTIATED;
  }
peer_error:
  {
    gst_object_unref (peer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "alloc function returned error %s", gst_flow_get_name (ret));
    return ret;
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
  return gst_pad_alloc_buffer_full (pad, offset, size, caps, buf, FALSE);
}

/**
 * gst_pad_alloc_buffer_and_set_caps:
 * @pad: a source #GstPad
 * @offset: the offset of the new buffer in the stream
 * @size: the size of the new buffer
 * @caps: the caps of the new buffer
 * @buf: a newly allocated buffer
 *
 * In addition to the function gst_pad_alloc_buffer(), this function
 * automatically calls gst_pad_set_caps() when the caps of the
 * newly allocated buffer are different from the @pad caps.
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
gst_pad_alloc_buffer_and_set_caps (GstPad * pad, guint64 offset, gint size,
    GstCaps * caps, GstBuffer ** buf)
{
  return gst_pad_alloc_buffer_full (pad, offset, size, caps, buf, TRUE);
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
 * Returns: a newly allocated #GList of pads, or NULL if the pad has no parent.
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
  if (!parent)
    return NULL;

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

    if (GST_PAD_DIRECTION (eventpad) == GST_PAD_SRC) {
      /* for each pad we send to, we should ref the event; it's up
       * to downstream to unref again when handled. */
      GST_LOG_OBJECT (pad, "Reffing and sending event %p (%s) to %s:%s",
          event, gst_event_type_get_name (GST_EVENT_TYPE (event)),
          GST_DEBUG_PAD_NAME (eventpad));
      gst_event_ref (event);
      gst_pad_push_event (eventpad, event);
    } else {
      /* we only send the event on one pad, multi-sinkpad elements
       * should implement a handler */
      GST_LOG_OBJECT (pad, "sending event %p (%s) to one sink pad %s:%s",
          event, gst_event_type_get_name (GST_EVENT_TYPE (event)),
          GST_DEBUG_PAD_NAME (eventpad));
      result = gst_pad_push_event (eventpad, event);
      goto done;
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

/**
 * gst_pad_query_default:
 * @pad: a #GstPad to call the default query handler on.
 * @query: the #GstQuery to handle.
 *
 * Invokes the default query handler for the given pad. 
 * The query is sent to all pads internally linked to @pad. Note that 
 * if there are many possible sink pads that are internally linked to 
 * @pad, only one will be sent the query.
 * Multi-sinkpad elements should implement custom query handlers.
 *
 * Returns: TRUE if the query was performed succesfully.
 */
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
static GstFlowReturn
handle_pad_block (GstPad * pad)
{
  GstPadBlockCallback callback;
  gpointer user_data;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "signal block taken on pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* need to grab extra ref for the callbacks */
  gst_object_ref (pad);

  callback = pad->block_callback;
  if (callback) {
    user_data = pad->block_data;
    GST_OBJECT_UNLOCK (pad);
    callback (pad, TRUE, user_data);
    GST_OBJECT_LOCK (pad);
  } else {
    GST_PAD_BLOCK_SIGNAL (pad);
  }

  while (GST_PAD_IS_BLOCKED (pad)) {
    if (GST_PAD_IS_FLUSHING (pad))
      goto flushing;
    GST_PAD_BLOCK_WAIT (pad);
    if (GST_PAD_IS_FLUSHING (pad))
      goto flushing;
  }

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "got unblocked");

  callback = pad->block_callback;
  if (callback) {
    user_data = pad->block_data;
    GST_OBJECT_UNLOCK (pad);
    callback (pad, FALSE, user_data);
    GST_OBJECT_LOCK (pad);
  } else {
    GST_PAD_BLOCK_SIGNAL (pad);
  }

  gst_object_unref (pad);

  return ret;

flushing:
  {
    gst_object_unref (pad);
    return GST_FLOW_WRONG_STATE;
  }
}

/**********************************************************************
 * Data passing functions
 */

static gboolean
gst_pad_emit_have_data_signal (GstPad * pad, GstMiniObject * obj)
{
  GValue ret = { 0 };
  GValue args[2] = { {0}, {0} };
  gboolean res;
  GQuark detail;

  /* init */
  g_value_init (&ret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&ret, TRUE);
  g_value_init (&args[0], GST_TYPE_PAD);
  g_value_set_object (&args[0], pad);
  g_value_init (&args[1], GST_TYPE_MINI_OBJECT);        // G_TYPE_POINTER);
  gst_value_set_mini_object (&args[1], obj);

  if (GST_IS_EVENT (obj))
    detail = event_quark;
  else
    detail = buffer_quark;

  /* actually emit */
  g_signal_emitv (args, gst_pad_signals[PAD_HAVE_DATA], detail, &ret);
  res = g_value_get_boolean (&ret);

  /* clean up */
  g_value_unset (&ret);
  g_value_unset (&args[0]);
  g_value_unset (&args[1]);

  return res;
}

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
  gboolean caps_changed;
  GstPadChainFunction chainfunc;
  GstFlowReturn ret;
  gboolean emit_signal;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK,
      GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  GST_PAD_STREAM_LOCK (pad);

  GST_OBJECT_LOCK (pad);
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
    goto flushing;

  caps = GST_BUFFER_CAPS (buffer);
  caps_changed = caps && caps != GST_PAD_CAPS (pad);

  emit_signal = GST_PAD_DO_BUFFER_SIGNALS (pad) > 0;
  GST_OBJECT_UNLOCK (pad);

  /* see if the signal should be emited, we emit before caps nego as
   * we might drop the buffer and do capsnego for nothing. */
  if (G_UNLIKELY (emit_signal)) {
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT (buffer)))
      goto dropping;
  }

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

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "calling chainfunction &%s of pad %s:%s",
      GST_DEBUG_FUNCPTR_NAME (chainfunc), GST_DEBUG_PAD_NAME (pad));

  ret = chainfunc (pad, buffer);

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "called chainfunction &%s of pad %s:%s, returned %s",
      GST_DEBUG_FUNCPTR_NAME (chainfunc), GST_DEBUG_PAD_NAME (pad),
      gst_flow_get_name (ret));

  GST_PAD_STREAM_UNLOCK (pad);

  return ret;

  /* ERRORS */
flushing:
  {
    gst_buffer_unref (buffer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but pad was flushing");
    GST_OBJECT_UNLOCK (pad);
    GST_PAD_STREAM_UNLOCK (pad);
    return GST_FLOW_WRONG_STATE;
  }
dropping:
  {
    gst_buffer_unref (buffer);
    GST_DEBUG_OBJECT (pad, "Dropping buffer due to FALSE probe return");
    GST_PAD_STREAM_UNLOCK (pad);
    return GST_FLOW_OK;
  }
not_negotiated:
  {
    gst_buffer_unref (buffer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing buffer but pad did not accept");
    GST_PAD_STREAM_UNLOCK (pad);
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
    GST_PAD_STREAM_UNLOCK (pad);
    return GST_FLOW_ERROR;
  }
}

/**
 * gst_pad_push:
 * @pad: a source #GstPad.
 * @buffer: the #GstBuffer to push.
 *
 * Pushes a buffer to the peer of @pad.
 * buffer probes will be triggered before the buffer gets pushed.
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

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC, GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  GST_OBJECT_LOCK (pad);

  /* FIXME: this check can go away; pad_set_blocked could be implemented with
   * probes completely */
  while (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad)))
    if ((ret = handle_pad_block (pad)) != GST_FLOW_OK)
      goto flushed;

  /* we emit signals on the pad arg, the peer will have a chance to
   * emit in the _chain() function */
  if (G_UNLIKELY (GST_PAD_DO_BUFFER_SIGNALS (pad) > 0)) {
    /* unlock before emitting */
    GST_OBJECT_UNLOCK (pad);

    /* if the signal handler returned FALSE, it means we should just drop the
     * buffer */
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT (buffer)))
      goto dropped;

    GST_OBJECT_LOCK (pad);
  }

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto not_linked;
  gst_object_ref (peer);
  GST_OBJECT_UNLOCK (pad);

  ret = gst_pad_chain (peer, buffer);

  gst_object_unref (peer);

  return ret;

  /* ERROR recovery here */
flushed:
  {
    gst_buffer_unref (buffer);
    GST_DEBUG_OBJECT (pad, "pad block stopped by flush");
    GST_OBJECT_UNLOCK (pad);
    return ret;
  }
dropped:
  {
    gst_buffer_unref (buffer);
    GST_DEBUG_OBJECT (pad, "Dropping buffer due to FALSE probe return");
    return GST_FLOW_OK;
  }
not_linked:
  {
    gst_buffer_unref (buffer);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but it was not linked");
    GST_OBJECT_UNLOCK (pad);
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

  GST_OBJECT_LOCK (pad);
  if (GST_PAD_DIRECTION (pad) != GST_PAD_SINK)
    goto wrong_direction;

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto not_connected;

  gst_object_ref (peer);
  GST_OBJECT_UNLOCK (pad);

  /* see note in above function */
  if (G_LIKELY ((checkgetrangefunc = peer->checkgetrangefunc) == NULL)) {
    /* FIXME, kindoff ghetto */
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
    GST_OBJECT_UNLOCK (pad);
    return FALSE;
  }
not_connected:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "checking pull range, but it was not linked");
    GST_OBJECT_UNLOCK (pad);
    return FALSE;
  }
}

/**
 * gst_pad_get_range:
 * @pad: a src #GstPad.
 * @offset: The start offset of the buffer
 * @size: The length of the buffer
 * @buffer: a pointer to hold the #GstBuffer.
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
  gboolean emit_signal;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC, GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  GST_PAD_STREAM_LOCK (pad);

  GST_OBJECT_LOCK (pad);
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
    goto flushing;

  emit_signal = GST_PAD_DO_BUFFER_SIGNALS (pad) > 0;
  GST_OBJECT_UNLOCK (pad);

  if (G_UNLIKELY ((getrangefunc = GST_PAD_GETRANGEFUNC (pad)) == NULL))
    goto no_function;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "calling getrangefunc %s of peer pad %s:%s, offset %"
      G_GUINT64_FORMAT ", size %u",
      GST_DEBUG_FUNCPTR_NAME (getrangefunc), GST_DEBUG_PAD_NAME (pad),
      offset, size);

  ret = getrangefunc (pad, offset, size, buffer);

  /* can only fire the signal if we have a valid buffer */
  if (G_UNLIKELY (emit_signal) && (ret == GST_FLOW_OK)) {
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT (*buffer)))
      goto dropping;
  }

  GST_PAD_STREAM_UNLOCK (pad);

  return ret;

  /* ERRORS */
flushing:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pulling range, but pad was flushing");
    GST_OBJECT_UNLOCK (pad);
    GST_PAD_STREAM_UNLOCK (pad);
    return GST_FLOW_WRONG_STATE;
  }
no_function:
  {
    GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
        ("pullrange on pad %s:%s but it has no getrangefunction",
            GST_DEBUG_PAD_NAME (pad)));
    GST_PAD_STREAM_UNLOCK (pad);
    return GST_FLOW_ERROR;
  }
dropping:
  {
    GST_DEBUG ("Dropping data after FALSE probe return");
    GST_PAD_STREAM_UNLOCK (pad);
    gst_buffer_unref (*buffer);
    *buffer = NULL;
    return GST_FLOW_UNEXPECTED;
  }
}


/**
 * gst_pad_pull_range:
 * @pad: a sink #GstPad.
 * @offset: The start offset of the buffer
 * @size: The length of the buffer
 * @buffer: a pointer to hold the #GstBuffer.
 *
 * Pulls a buffer from the peer pad. @pad must be a linked
 * sinkpad.
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
  gboolean emit_signal;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK,
      GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  GST_OBJECT_LOCK (pad);

  while (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad)))
    handle_pad_block (pad);

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto not_connected;

  /* signal emision for the pad, peer has chance to emit when
   * we call _get_range() */
  emit_signal = GST_PAD_DO_BUFFER_SIGNALS (pad) > 0;

  gst_object_ref (peer);
  GST_OBJECT_UNLOCK (pad);

  ret = gst_pad_get_range (peer, offset, size, buffer);

  gst_object_unref (peer);

  /* can only fire the signal if we have a valid buffer */
  if (G_UNLIKELY (emit_signal) && (ret == GST_FLOW_OK)) {
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT (*buffer)))
      goto dropping;
  }
  return ret;

  /* ERROR recovery here */
not_connected:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pulling range, but it was not linked");
    GST_OBJECT_UNLOCK (pad);
    return GST_FLOW_NOT_LINKED;
  }
dropping:
  {
    GST_DEBUG ("Dropping data after FALSE probe return");
    gst_buffer_unref (*buffer);
    *buffer = NULL;
    return GST_FLOW_UNEXPECTED;
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
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (GST_IS_EVENT (event), FALSE);

  GST_OBJECT_LOCK (pad);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_PAD_SET_FLUSHING (pad);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_PAD_UNSET_FLUSHING (pad);
      break;
    default:
      break;
  }

  if (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad))) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START) {
      GST_PAD_BLOCK_SIGNAL (pad);
    }
  }

  if (G_UNLIKELY (GST_PAD_DO_EVENT_SIGNALS (pad) > 0)) {
    GST_OBJECT_UNLOCK (pad);

    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT (event)))
      goto dropping;

    GST_OBJECT_LOCK (pad);
  }
  peerpad = GST_PAD_PEER (pad);
  if (peerpad == NULL)
    goto not_linked;

  GST_LOG_OBJECT (peerpad, "sending event on peerpad");
  gst_object_ref (peerpad);
  GST_OBJECT_UNLOCK (pad);

  result = gst_pad_send_event (peerpad, event);

  gst_object_unref (peerpad);
  GST_LOG_OBJECT (peerpad, "sent event on peerpad");

  return result;

  /* ERROR handling */
dropping:
  {
    GST_DEBUG_OBJECT (pad, "Dropping event after FALSE probe return");
    gst_event_unref (event);
    return FALSE;
  }
not_linked:
  {
    gst_event_unref (event);
    GST_OBJECT_UNLOCK (pad);
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
 * If @pad is a source pad, @event should be an upstream event. If @pad is a
 * sink pad, @event should be a downstream event. For example, you would not
 * send a #GST_EVENT_EOS on a src pad; EOS events only propagate downstream.
 * Furthermore, some downstream events have to be serialized with data flow,
 * like EOS, while some can travel out-of-band, like #GST_EVENT_FLUSH_START. If
 * the event needs to be serialized with data flow, this function will take the
 * pad's stream lock while calling its event function.
 *
 * To find out whether an event type is upstream, downstream, or downstream and
 * serialized, see #GstEventTypeFlags, gst_event_type_get_flags(),
 * #GST_EVENT_IS_UPSTREAM, #GST_EVENT_IS_DOWNSTREAM, and
 * #GST_EVENT_IS_SERIALIZED. Note that in practice that an application or plugin
 * doesn't need to bother itself with this information; the core handles all
 * necessary locks and checks.
 *
 * Returns: TRUE if the event was handled.
 */
gboolean
gst_pad_send_event (GstPad * pad, GstEvent * event)
{
  gboolean result = FALSE;
  GstPadEventFunction eventfunc;
  gboolean emit_signal, serialized;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  GST_OBJECT_LOCK (pad);
  if (GST_PAD_IS_SINK (pad) && !GST_EVENT_IS_DOWNSTREAM (event))
    goto wrong_direction;
  if (GST_PAD_IS_SRC (pad) && !GST_EVENT_IS_UPSTREAM (event))
    goto wrong_direction;

  if (GST_EVENT_SRC (event) == NULL) {
    GST_LOG_OBJECT (pad, "event had no source, setting pad as event source");
    GST_EVENT_SRC (event) = gst_object_ref (pad);
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_CAT_DEBUG (GST_CAT_EVENT,
          "have event type %d (FLUSH_START) on pad %s:%s",
          GST_EVENT_TYPE (event), GST_DEBUG_PAD_NAME (pad));

      /* can't even accept a flush begin event when flushing */
      if (GST_PAD_IS_FLUSHING (pad))
        goto flushing;
      GST_PAD_SET_FLUSHING (pad);
      GST_CAT_DEBUG (GST_CAT_EVENT, "set flush flag");
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_PAD_UNSET_FLUSHING (pad);
      GST_CAT_DEBUG (GST_CAT_EVENT, "cleared flush flag");
      break;
    default:
      GST_CAT_DEBUG (GST_CAT_EVENT, "have event type %s on pad %s:%s",
          gst_event_type_get_name (GST_EVENT_TYPE (event)),
          GST_DEBUG_PAD_NAME (pad));

      if (GST_PAD_IS_FLUSHING (pad))
        goto flushing;
      break;
  }

  if ((eventfunc = GST_PAD_EVENTFUNC (pad)) == NULL)
    goto no_function;

  emit_signal = GST_PAD_DO_EVENT_SIGNALS (pad) > 0;
  GST_OBJECT_UNLOCK (pad);

  /* have to check if it's a sink pad, because e.g. CUSTOM_BOTH is serialized
     when going down but not when going up */
  serialized = GST_EVENT_IS_SERIALIZED (event) && GST_PAD_IS_SINK (pad);

  if (G_UNLIKELY (emit_signal)) {
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT (event)))
      goto dropping;
  }

  if (serialized)
    GST_PAD_STREAM_LOCK (pad);

  result = eventfunc (GST_PAD_CAST (pad), event);

  if (serialized)
    GST_PAD_STREAM_UNLOCK (pad);

  return result;

  /* ERROR handling */
wrong_direction:
  {
    g_warning ("pad %s:%s sending event in wrong direction",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    gst_event_unref (event);
    return FALSE;
  }
no_function:
  {
    g_warning ("pad %s:%s has no event handler, file a bug.",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    gst_event_unref (event);
    return FALSE;
  }
flushing:
  {
    GST_OBJECT_UNLOCK (pad);
    GST_CAT_INFO (GST_CAT_EVENT, "Received event on flushing pad. Discarding");
    gst_event_unref (event);
    return FALSE;
  }
dropping:
  {
    GST_DEBUG ("Dropping event after FALSE probe return");
    gst_event_unref (event);
    return FALSE;
  }
}

/**
 * gst_pad_set_element_private:
 * @pad: the #GstPad to set the private data of.
 * @priv: The private data to attach to the pad.
 *
 * Set the given private data gpointer on the pad.
 * This function can only be used by the element that owns the pad.
 * No locking is performed in this function.
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
 * No locking is performed in this function.
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
 * Returns: a TRUE if the task could be started.
 */
gboolean
gst_pad_start_task (GstPad * pad, GstTaskFunction func, gpointer data)
{
  GstTask *task;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  GST_OBJECT_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL) {
    task = gst_task_create (func, data);
    gst_task_set_lock (task, GST_PAD_GET_STREAM_LOCK (pad));
    GST_PAD_TASK (pad) = task;
  }
  gst_task_start (task);
  GST_OBJECT_UNLOCK (pad);

  return TRUE;
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

  GST_OBJECT_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL)
    goto no_task;
  gst_task_pause (task);
  GST_OBJECT_UNLOCK (pad);

  GST_PAD_STREAM_LOCK (pad);
  GST_PAD_STREAM_UNLOCK (pad);

  return TRUE;

no_task:
  {
    GST_DEBUG_OBJECT (pad, "pad has no task");
    GST_OBJECT_UNLOCK (pad);
    return FALSE;
  }
}

/**
 * gst_pad_stop_task:
 * @pad: the #GstPad to stop the task of
 *
 * Stop the task of @pad. This function will also make sure that the
 * function executed by the task will effectively stop if not called
 * from the GstTaskFunction.
 *
 * This function will deadlock if called from the GstTaskFunction of
 * the task. Use #gst_task_pause() instead.
 *
 * Regardless of whether the pad has a task, the stream lock is acquired and
 * released so as to ensure that streaming through this pad has finished.
 *
 * Returns: a TRUE if the task could be stopped or FALSE on error.
 */
gboolean
gst_pad_stop_task (GstPad * pad)
{
  GstTask *task;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_OBJECT_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL)
    goto no_task;
  GST_PAD_TASK (pad) = NULL;
  gst_task_stop (task);
  GST_OBJECT_UNLOCK (pad);

  GST_PAD_STREAM_LOCK (pad);
  GST_PAD_STREAM_UNLOCK (pad);

  gst_task_join (task);

  gst_object_unref (task);

  return TRUE;

no_task:
  {
    GST_OBJECT_UNLOCK (pad);

    GST_PAD_STREAM_LOCK (pad);
    GST_PAD_STREAM_UNLOCK (pad);

    return TRUE;
  }
}
