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
 * To send a #GstEvent on a pad, use gst_pad_send_event() and
 * gst_pad_push_event().
 *
 * Last reviewed on 2006-07-06 (0.10.9)
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

#define GST_PAD_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_PAD, GstPadPrivate))

/* we have a pending and an active event on the pad. On source pads only the
 * active event is used. On sinkpads, events are copied to the pending entry and
 * moved to the active event when the eventfunc returned TRUE. */
typedef struct
{
  GstEvent *pending;
  GstEvent *event;
} PadEvent;

struct _GstPadPrivate
{
  PadEvent events[GST_EVENT_MAX_STICKY];

  gint using;
};

static void gst_pad_dispose (GObject * object);
static void gst_pad_finalize (GObject * object);
static void gst_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn handle_pad_block (GstPad * pad);
static GstCaps *gst_pad_get_caps_unlocked (GstPad * pad, GstCaps * filter);
static void gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ);
static gboolean gst_pad_activate_default (GstPad * pad);
static gboolean gst_pad_acceptcaps_default (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_pad_chain_list_default (GstPad * pad,
    GstBufferList * list);

static GstObjectClass *parent_class = NULL;
static guint gst_pad_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *pspec_caps = NULL;

/* quarks for probe signals */
static GQuark buffer_quark;
static GQuark buffer_list_quark;
static GQuark event_quark;

typedef struct
{
  const gint ret;
  const gchar *name;
  GQuark quark;
} GstFlowQuarks;

static GstFlowQuarks flow_quarks[] = {
  {GST_FLOW_CUSTOM_SUCCESS, "custom-success", 0},
  {GST_FLOW_RESEND, "resend", 0},
  {GST_FLOW_OK, "ok", 0},
  {GST_FLOW_NOT_LINKED, "not-linked", 0},
  {GST_FLOW_WRONG_STATE, "wrong-state", 0},
  {GST_FLOW_UNEXPECTED, "unexpected", 0},
  {GST_FLOW_NOT_NEGOTIATED, "not-negotiated", 0},
  {GST_FLOW_ERROR, "error", 0},
  {GST_FLOW_NOT_SUPPORTED, "not-supported", 0},
  {GST_FLOW_CUSTOM_ERROR, "custom-error", 0}
};

/**
 * gst_flow_get_name:
 * @ret: a #GstFlowReturn to get the name of.
 *
 * Gets a string representing the given flow return.
 *
 * Returns: a static string with the name of the flow return.
 */
G_CONST_RETURN gchar *
gst_flow_get_name (GstFlowReturn ret)
{
  gint i;

  ret = CLAMP (ret, GST_FLOW_CUSTOM_ERROR, GST_FLOW_CUSTOM_SUCCESS);

  for (i = 0; i < G_N_ELEMENTS (flow_quarks); i++) {
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

  ret = CLAMP (ret, GST_FLOW_CUSTOM_ERROR, GST_FLOW_CUSTOM_SUCCESS);

  for (i = 0; i < G_N_ELEMENTS (flow_quarks); i++) {
    if (ret == flow_quarks[i].ret)
      return flow_quarks[i].quark;
  }
  return 0;
}

#define _do_init \
{ \
  gint i; \
  \
  buffer_quark = g_quark_from_static_string ("buffer"); \
  buffer_list_quark = g_quark_from_static_string ("bufferlist"); \
  event_quark = g_quark_from_static_string ("event"); \
  \
  for (i = 0; i < G_N_ELEMENTS (flow_quarks); i++) {			\
    flow_quarks[i].quark = g_quark_from_static_string (flow_quarks[i].name); \
  } \
  \
  GST_DEBUG_CATEGORY_INIT (debug_dataflow, "GST_DATAFLOW", \
      GST_DEBUG_BOLD | GST_DEBUG_FG_GREEN, "dataflow inside pads"); \
}

G_DEFINE_TYPE_WITH_CODE (GstPad, gst_pad, GST_TYPE_OBJECT, _do_init);

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

  gobject_class = G_OBJECT_CLASS (klass);
  gstobject_class = GST_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstPadPrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_pad_dispose;
  gobject_class->finalize = gst_pad_finalize;
  gobject_class->set_property = gst_pad_set_property;
  gobject_class->get_property = gst_pad_get_property;

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
      NULL, gst_marshal_BOOLEAN__POINTER, G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);

  pspec_caps = g_param_spec_boxed ("caps", "Caps",
      "The capabilities of the pad", GST_TYPE_CAPS,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PAD_PROP_CAPS, pspec_caps);

  g_object_class_install_property (gobject_class, PAD_PROP_DIRECTION,
      g_param_spec_enum ("direction", "Direction", "The direction of the pad",
          GST_TYPE_PAD_DIRECTION, GST_PAD_UNKNOWN,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  /* FIXME, Make G_PARAM_CONSTRUCT_ONLY when we fix ghostpads. */
  g_object_class_install_property (gobject_class, PAD_PROP_TEMPLATE,
      g_param_spec_object ("template", "Template",
          "The GstPadTemplate of this pad", GST_TYPE_PAD_TEMPLATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstobject_class->path_string_separator = ".";

  /* Register common function pointer descriptions */
  GST_DEBUG_REGISTER_FUNCPTR (gst_pad_activate_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_pad_event_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_pad_get_query_types_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_pad_query_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_pad_iterate_internal_links_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_pad_acceptcaps_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_pad_chain_list_default);

  klass->have_data = default_have_data;
}

static void
gst_pad_init (GstPad * pad)
{
  pad->priv = GST_PAD_GET_PRIVATE (pad);

  GST_PAD_DIRECTION (pad) = GST_PAD_UNKNOWN;

  GST_PAD_ACTIVATEFUNC (pad) = gst_pad_activate_default;
  GST_PAD_EVENTFUNC (pad) = gst_pad_event_default;
  GST_PAD_QUERYTYPEFUNC (pad) = gst_pad_get_query_types_default;
  GST_PAD_QUERYFUNC (pad) = gst_pad_query_default;
  GST_PAD_ITERINTLINKFUNC (pad) = gst_pad_iterate_internal_links_default;
  GST_PAD_ACCEPTCAPSFUNC (pad) = gst_pad_acceptcaps_default;
  GST_PAD_CHAINLISTFUNC (pad) = gst_pad_chain_list_default;

  GST_PAD_SET_FLUSHING (pad);

  /* FIXME 0.11: Store this directly in the instance struct */
  pad->stream_rec_lock = g_slice_new (GStaticRecMutex);
  g_static_rec_mutex_init (pad->stream_rec_lock);

  pad->block_cond = g_cond_new ();
}

/* called when setting the pad inactive. It removes all sticky events from
 * the pad */
static void
clear_events (PadEvent events[])
{
  guint i;

  for (i = 0; i < GST_EVENT_MAX_STICKY; i++) {
    gst_event_replace (&events[i].event, NULL);
    gst_event_replace (&events[i].pending, NULL);
  }
}

/* The sticky event with @idx from the srcpad is copied to the
 * pending event on the sinkpad (when different).
 * This function applies the pad offsets in case of segment events.
 * This will make sure that we send the event to the sinkpad event
 * function when the next buffer of event arrives.
 * Should be called with the OBJECT lock of both pads.
 * This function returns TRUE when there is a pending event on the
 * sinkpad */
static gboolean
replace_event (GstPad * srcpad, GstPad * sinkpad, guint idx)
{
  PadEvent *srcev, *sinkev;
  GstEvent *event;
  gboolean pending = FALSE;

  srcev = &srcpad->priv->events[idx];

  if ((event = srcev->event)) {
    sinkev = &sinkpad->priv->events[idx];

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_SEGMENT:
      {
        GstSegment segment;
        gint64 offset;

        offset = srcpad->offset + sinkpad->offset;
        if (offset != 0) {
          gst_event_copy_segment (event, &segment);
          /* adjust the base time. FIXME, check negative times, try to tweak the
           * start to do clipping on negative times */
          segment.base += offset;
          /* make a new event from the updated segment */
          event = gst_event_new_segment (&segment);
        }
        break;
      }
      default:
        break;
    }
    if (sinkev->event != event) {
      /* put in the pending entry when different */
      gst_event_replace (&sinkev->pending, event);
      pending = TRUE;
    }
  }
  return pending;
}

/* should be called with the OBJECT_LOCK */
static GstCaps *
get_pad_caps (GstPad * pad)
{
  GstCaps *caps = NULL;
  GstEvent *event;
  guint idx;

  idx = GST_EVENT_STICKY_IDX_TYPE (GST_EVENT_CAPS);
  /* we can only use the caps when we have successfully send the caps
   * event to the event function and is thus in the active entry */
  if ((event = pad->priv->events[idx].event))
    gst_event_parse_caps (event, &caps);

  return caps;
}

static void
gst_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD_CAST (object);
  GstPad *peer;

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, pad, "dispose");

  /* unlink the peer pad */
  if ((peer = gst_pad_get_peer (pad))) {
    /* window for MT unsafeness, someone else could unlink here
     * and then we call unlink with wrong pads. The unlink
     * function would catch this and safely return failed. */
    if (GST_PAD_IS_SRC (pad))
      gst_pad_unlink (pad, peer);
    else
      gst_pad_unlink (peer, pad);

    gst_object_unref (peer);
  }

  gst_pad_set_pad_template (pad, NULL);

  if (pad->block_destroy_data && pad->block_data) {
    pad->block_destroy_data (pad->block_data);
    pad->block_data = NULL;
  }

  clear_events (pad->priv->events);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pad_finalize (GObject * object)
{
  GstPad *pad = GST_PAD_CAST (object);
  GstTask *task;

  /* in case the task is still around, clean it up */
  if ((task = GST_PAD_TASK (pad))) {
    gst_task_join (task);
    GST_PAD_TASK (pad) = NULL;
    gst_object_unref (task);
  }

  if (pad->stream_rec_lock) {
    g_static_rec_mutex_free (pad->stream_rec_lock);
    g_slice_free (GStaticRecMutex, pad->stream_rec_lock);
    pad->stream_rec_lock = NULL;
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
          (GstPadTemplate *) g_value_get_object (value));
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
      GST_OBJECT_LOCK (object);
      g_value_set_boxed (value, get_pad_caps (GST_PAD_CAST (object)));
      GST_OBJECT_UNLOCK (object);
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
 * Returns: (transfer full): a new #GstPad, or NULL in case of an error.
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
 * Returns: (transfer full): a new #GstPad, or NULL in case of an error.
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
 * Returns: (transfer full): a new #GstPad, or NULL in case of an error.
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

  result = GST_PAD_DIRECTION (pad);

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
      GST_DEBUG_OBJECT (pad, "setting ACTIVATE_MODE NONE, set flushing");
      GST_PAD_SET_FLUSHING (pad);
      GST_PAD_ACTIVATE_MODE (pad) = new_mode;
      /* unlock blocked pads so element can resume and stop */
      GST_PAD_BLOCK_BROADCAST (pad);
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
      GST_DEBUG_OBJECT (pad, "stopped streaming");
      GST_OBJECT_LOCK (pad);
      clear_events (pad->priv->events);
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
        GST_DEBUG_OBJECT (pad, "activating pad from push");
        ret = TRUE;
        break;
      case GST_ACTIVATE_PULL:
        GST_DEBUG_OBJECT (pad, "activating pad from pull");
        ret = TRUE;
        break;
      case GST_ACTIVATE_NONE:
        GST_DEBUG_OBJECT (pad, "activating pad from none");
        ret = (GST_PAD_ACTIVATEFUNC (pad)) (pad);
        break;
      default:
        GST_DEBUG_OBJECT (pad, "unknown activation mode!");
        break;
    }
  } else {
    switch (old) {
      case GST_ACTIVATE_PUSH:
        GST_DEBUG_OBJECT (pad, "deactivating pad from push");
        ret = gst_pad_activate_push (pad, FALSE);
        break;
      case GST_ACTIVATE_PULL:
        GST_DEBUG_OBJECT (pad, "deactivating pad from pull");
        ret = gst_pad_activate_pull (pad, FALSE);
        break;
      case GST_ACTIVATE_NONE:
        GST_DEBUG_OBJECT (pad, "deactivating pad from none");
        ret = TRUE;
        break;
      default:
        GST_DEBUG_OBJECT (pad, "unknown activation mode!");
        break;
    }
  }

  if (!ret) {
    GST_OBJECT_LOCK (pad);
    if (!active) {
      g_critical ("Failed to deactivate pad %s:%s, very bad",
          GST_DEBUG_PAD_NAME (pad));
    } else {
      GST_WARNING_OBJECT (pad, "Failed to activate pad");
    }
    GST_OBJECT_UNLOCK (pad);
  } else {
    if (!active) {
      GST_OBJECT_LOCK (pad);
      GST_OBJECT_FLAG_UNSET (pad, GST_PAD_NEED_RECONFIGURE);
      GST_OBJECT_UNLOCK (pad);
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
 * When called on sink pads, will first proxy the call to the peer pad, which
 * is expected to activate its internally linked pads from within its
 * activate_pull function.
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

  if (active) {
    switch (old) {
      case GST_ACTIVATE_PULL:
        GST_DEBUG_OBJECT (pad, "activating pad from pull, was ok");
        goto was_ok;
      case GST_ACTIVATE_PUSH:
        GST_DEBUG_OBJECT (pad,
            "activating pad from push, deactivate push first");
        /* pad was activate in the wrong direction, deactivate it
         * and reactivate it in pull mode */
        if (G_UNLIKELY (!gst_pad_activate_push (pad, FALSE)))
          goto deactivate_failed;
        /* fallthrough, pad is deactivated now. */
      case GST_ACTIVATE_NONE:
        GST_DEBUG_OBJECT (pad, "activating pad from none");
        break;
    }
  } else {
    switch (old) {
      case GST_ACTIVATE_NONE:
        GST_DEBUG_OBJECT (pad, "deactivating pad from none, was ok");
        goto was_ok;
      case GST_ACTIVATE_PUSH:
        GST_DEBUG_OBJECT (pad, "deactivating pad from push, weird");
        /* pad was activated in the other direction, deactivate it
         * in push mode, this should not happen... */
        if (G_UNLIKELY (!gst_pad_activate_push (pad, FALSE)))
          goto deactivate_failed;
        /* everything is fine now */
        goto was_ok;
      case GST_ACTIVATE_PULL:
        GST_DEBUG_OBJECT (pad, "deactivating pad from pull");
        break;
    }
  }

  if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
    if ((peer = gst_pad_get_peer (pad))) {
      GST_DEBUG_OBJECT (pad, "calling peer");
      if (G_UNLIKELY (!gst_pad_activate_pull (peer, active)))
        goto peer_failed;
      gst_object_unref (peer);
    } else {
      /* there is no peer, this is only fatal when we activate. When we
       * deactivate, we must assume the application has unlinked the peer and
       * will deactivate it eventually. */
      if (active)
        goto not_linked;
      else
        GST_DEBUG_OBJECT (pad, "deactivating unlinked pad");
    }
  } else {
    if (G_UNLIKELY (GST_PAD_GETRANGEFUNC (pad) == NULL))
      goto failure;             /* Can't activate pull on a src without a
                                   getrange function */
  }

  new = active ? GST_ACTIVATE_PULL : GST_ACTIVATE_NONE;
  pre_activate (pad, new);

  if (GST_PAD_ACTIVATEPULLFUNC (pad)) {
    if (G_UNLIKELY (!GST_PAD_ACTIVATEPULLFUNC (pad) (pad, active)))
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
deactivate_failed:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad,
        "failed to %s in switch to pull from mode %d",
        (active ? "activate" : "deactivate"), old);
    return FALSE;
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
not_linked:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_PADS, pad, "can't activate unlinked sink "
        "pad in pull mode");
    return FALSE;
  }
failure:
  {
    GST_OBJECT_LOCK (pad);
    GST_CAT_INFO_OBJECT (GST_CAT_PADS, pad, "failed to %s in pull mode",
        active ? "activate" : "deactivate");
    GST_PAD_SET_FLUSHING (pad);
    GST_PAD_ACTIVATE_MODE (pad) = old;
    GST_OBJECT_UNLOCK (pad);
    return FALSE;
  }
}

/**
 * gst_pad_activate_push:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether the pad should be active or not.
 *
 * Activates or deactivates the given pad in push mode via dispatching to the
 * pad's activatepushfunc. For use from within pad activation functions only.
 *
 * If you don't know what this is, you probably don't want to call it.
 *
 * Returns: %TRUE if the operation was successful.
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

  if (active) {
    switch (old) {
      case GST_ACTIVATE_PUSH:
        GST_DEBUG_OBJECT (pad, "activating pad from push, was ok");
        goto was_ok;
      case GST_ACTIVATE_PULL:
        GST_DEBUG_OBJECT (pad,
            "activating pad from push, deactivating pull first");
        /* pad was activate in the wrong direction, deactivate it
         * an reactivate it in push mode */
        if (G_UNLIKELY (!gst_pad_activate_pull (pad, FALSE)))
          goto deactivate_failed;
        /* fallthrough, pad is deactivated now. */
      case GST_ACTIVATE_NONE:
        GST_DEBUG_OBJECT (pad, "activating pad from none");
        break;
    }
  } else {
    switch (old) {
      case GST_ACTIVATE_NONE:
        GST_DEBUG_OBJECT (pad, "deactivating pad from none, was ok");
        goto was_ok;
      case GST_ACTIVATE_PULL:
        GST_DEBUG_OBJECT (pad, "deactivating pad from pull, weird");
        /* pad was activated in the other direction, deactivate it
         * in pull mode, this should not happen... */
        if (G_UNLIKELY (!gst_pad_activate_pull (pad, FALSE)))
          goto deactivate_failed;
        /* everything is fine now */
        goto was_ok;
      case GST_ACTIVATE_PUSH:
        GST_DEBUG_OBJECT (pad, "deactivating pad from push");
        break;
    }
  }

  new = active ? GST_ACTIVATE_PUSH : GST_ACTIVATE_NONE;
  pre_activate (pad, new);

  if (GST_PAD_ACTIVATEPUSHFUNC (pad)) {
    if (G_UNLIKELY (!GST_PAD_ACTIVATEPUSHFUNC (pad) (pad, active))) {
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
deactivate_failed:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad,
        "failed to %s in switch to push from mode %d",
        (active ? "activate" : "deactivate"), old);
    return FALSE;
  }
failure:
  {
    GST_OBJECT_LOCK (pad);
    GST_CAT_INFO_OBJECT (GST_CAT_PADS, pad, "failed to %s in push mode",
        active ? "activate" : "deactivate");
    GST_PAD_SET_FLUSHING (pad);
    GST_PAD_ACTIVATE_MODE (pad) = old;
    GST_OBJECT_UNLOCK (pad);
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
 * gst_pad_set_blocked:
 * @pad: the #GstPad to block or unblock
 * @blocked: boolean indicating whether the pad should be blocked or unblocked
 * @callback: #GstPadBlockCallback that will be called when the
 *            operation succeeds
 * @user_data: (closure): user data passed to the callback
 * @destroy_data: #GDestroyNotify for user_data
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
 * <note>
 *  Pad block handlers are only called for source pads in push mode
 *  and sink pads in pull mode.
 * </note>
 *
 * Returns: TRUE if the pad could be blocked. This function can fail if the
 * wrong parameters were passed or the pad was already in the requested state.
 *
 * MT safe.
 */
gboolean
gst_pad_set_blocked (GstPad * pad, gboolean blocked,
    GstPadBlockCallback callback, gpointer user_data,
    GDestroyNotify destroy_data)
{
  gboolean was_blocked = FALSE;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_OBJECT_LOCK (pad);

  was_blocked = GST_PAD_IS_BLOCKED (pad);

  if (G_UNLIKELY (was_blocked == blocked))
    goto had_right_state;

  if (blocked) {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "blocking pad");

    GST_OBJECT_FLAG_SET (pad, GST_PAD_BLOCKED);

    if (pad->block_destroy_data && pad->block_data)
      pad->block_destroy_data (pad->block_data);

    pad->block_callback = callback;
    pad->block_data = user_data;
    pad->block_destroy_data = destroy_data;
    pad->block_callback_called = FALSE;
    if (!callback) {
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "waiting for block");
      GST_PAD_BLOCK_WAIT (pad);
      GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "blocked");
    }
  } else {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "unblocking pad");

    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_BLOCKED);

    if (pad->block_destroy_data && pad->block_data)
      pad->block_destroy_data (pad->block_data);

    pad->block_callback = callback;
    pad->block_data = user_data;
    pad->block_destroy_data = destroy_data;
    pad->block_callback_called = FALSE;

    GST_PAD_BLOCK_BROADCAST (pad);
    if (!callback) {
      /* no callback, wait for the unblock to happen */
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
        "pad was in right state (%d)", was_blocked);
    GST_OBJECT_UNLOCK (pad);

    return FALSE;
  }
}

/**
 * gst_pad_is_blocked:
 * @pad: the #GstPad to query
 *
 * Checks if the pad is blocked or not. This function returns the
 * last requested state of the pad. It is not certain that the pad
 * is actually blocking at this point (see gst_pad_is_blocking()).
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
 * gst_pad_is_blocking:
 * @pad: the #GstPad to query
 *
 * Checks if the pad is blocking or not. This is a guaranteed state
 * of whether the pad is actually blocking on a #GstBuffer or a #GstEvent.
 *
 * Returns: TRUE if the pad is blocking.
 *
 * MT safe.
 *
 * Since: 0.10.11
 */
gboolean
gst_pad_is_blocking (GstPad * pad)
{
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_PAD (pad), result);

  GST_OBJECT_LOCK (pad);
  /* the blocking flag is only valid if the pad is not flushing */
  result = GST_OBJECT_FLAG_IS_SET (pad, GST_PAD_BLOCKING) &&
      !GST_OBJECT_FLAG_IS_SET (pad, GST_PAD_FLUSHING);
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_set_activate_function:
 * @pad: a #GstPad.
 * @activate: the #GstPadActivateFunction to set.
 *
 * Sets the given activate function for @pad. The activate function will
 * dispatch to gst_pad_activate_push() or gst_pad_activate_pull() to perform
 * the actual activation. Only makes sense to set on sink pads.
 *
 * Call this function if your sink pad can start a pull-based task.
 */
void
gst_pad_set_activate_function (GstPad * pad, GstPadActivateFunction activate)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_ACTIVATEFUNC (pad) = activate;
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "activatefunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (activate));
}

/**
 * gst_pad_set_activatepull_function:
 * @pad: a #GstPad.
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
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "activatepullfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (activatepull));
}

/**
 * gst_pad_set_activatepush_function:
 * @pad: a #GstPad.
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
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "activatepushfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (activatepush));
}

/**
 * gst_pad_set_chain_function:
 * @pad: a sink #GstPad.
 * @chain: the #GstPadChainFunction to set.
 *
 * Sets the given chain function for the pad. The chain function is called to
 * process a #GstBuffer input buffer. see #GstPadChainFunction for more details.
 */
void
gst_pad_set_chain_function (GstPad * pad, GstPadChainFunction chain)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_IS_SINK (pad));

  GST_PAD_CHAINFUNC (pad) = chain;
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "chainfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (chain));
}

/**
 * gst_pad_set_chain_list_function:
 * @pad: a sink #GstPad.
 * @chainlist: the #GstPadChainListFunction to set.
 *
 * Sets the given chain list function for the pad. The chainlist function is
 * called to process a #GstBufferList input buffer list. See
 * #GstPadChainListFunction for more details.
 *
 * Since: 0.10.24
 */
void
gst_pad_set_chain_list_function (GstPad * pad,
    GstPadChainListFunction chainlist)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_IS_SINK (pad));

  GST_PAD_CHAINLISTFUNC (pad) = chainlist;
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "chainlistfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (chainlist));
}

/**
 * gst_pad_set_getrange_function:
 * @pad: a source #GstPad.
 * @get: the #GstPadGetRangeFunction to set.
 *
 * Sets the given getrange function for the pad. The getrange function is
 * called to produce a new #GstBuffer to start the processing pipeline. see
 * #GstPadGetRangeFunction for a description of the getrange function.
 */
void
gst_pad_set_getrange_function (GstPad * pad, GstPadGetRangeFunction get)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_IS_SRC (pad));

  GST_PAD_GETRANGEFUNC (pad) = get;

  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "getrangefunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (get));
}

/**
 * gst_pad_set_event_function:
 * @pad: a #GstPad of either direction.
 * @event: the #GstPadEventFunction to set.
 *
 * Sets the given event handler for the pad.
 */
void
gst_pad_set_event_function (GstPad * pad, GstPadEventFunction event)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_EVENTFUNC (pad) = event;

  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "eventfunc for set to %s",
      GST_DEBUG_FUNCPTR_NAME (event));
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

  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "queryfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (query));
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

  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "querytypefunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (type_func));
}

/**
 * gst_pad_get_query_types:
 * @pad: a #GstPad.
 *
 * Get an array of supported queries that can be performed
 * on this pad.
 *
 * Returns: (transfer none) (array zero-terminated=1): a zero-terminated array
 *     of #GstQueryType.
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
 * Returns: (transfer none) (array zero-terminated=1): a zero-terminated array
 *     of #GstQueryType, or NULL if none of the internally-linked pads has a
 *     query types function.
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
 * gst_pad_set_iterate_internal_links_function:
 * @pad: a #GstPad of either direction.
 * @iterintlink: the #GstPadIterIntLinkFunction to set.
 *
 * Sets the given internal link iterator function for the pad.
 *
 * Since: 0.10.21
 */
void
gst_pad_set_iterate_internal_links_function (GstPad * pad,
    GstPadIterIntLinkFunction iterintlink)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_ITERINTLINKFUNC (pad) = iterintlink;
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "internal link iterator set to %s",
      GST_DEBUG_FUNCPTR_NAME (iterintlink));
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
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "linkfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (link));
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
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "unlinkfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (unlink));
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
 * Note that the return value from @getcaps is owned by the caller, so the
 * caller should unref the caps after usage.
 */
void
gst_pad_set_getcaps_function (GstPad * pad, GstPadGetCapsFunction getcaps)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_GETCAPSFUNC (pad) = getcaps;
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "getcapsfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (getcaps));
}

/**
 * gst_pad_set_acceptcaps_function:
 * @pad: a #GstPad.
 * @acceptcaps: the #GstPadAcceptCapsFunction to set.
 *
 * Sets the given acceptcaps function for the pad.  The acceptcaps function
 * will be called to check if the pad can accept the given caps. Setting the
 * acceptcaps function to NULL restores the default behaviour of allowing
 * any caps that matches the caps from gst_pad_get_caps().
 */
void
gst_pad_set_acceptcaps_function (GstPad * pad,
    GstPadAcceptCapsFunction acceptcaps)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_ACCEPTCAPSFUNC (pad) = acceptcaps;
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "acceptcapsfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (acceptcaps));
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
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "fixatecapsfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (fixatecaps));
}

/**
 * gst_pad_set_setcaps_function:
 * @pad: a #GstPad.
 * @setcaps: the #GstPadSetCapsFunction to set.
 *
 * Sets the given setcaps function for the pad.  The setcaps function
 * will be called whenever a buffer with a new media type is pushed or
 * pulled from the pad. The pad/element needs to update its internal
 * structures to process the new media type. If this new type is not
 * acceptable, the setcaps function should return FALSE.
 */
void
gst_pad_set_setcaps_function (GstPad * pad, GstPadSetCapsFunction setcaps)
{
  g_return_if_fail (GST_IS_PAD (pad));

  GST_PAD_SETCAPSFUNC (pad) = setcaps;
  GST_CAT_DEBUG_OBJECT (GST_CAT_PADS, pad, "setcapsfunc set to %s",
      GST_DEBUG_FUNCPTR_NAME (setcaps));
}

/**
 * gst_pad_unlink:
 * @srcpad: the source #GstPad to unlink.
 * @sinkpad: the sink #GstPad to unlink.
 *
 * Unlinks the source pad from the sink pad. Will emit the #GstPad::unlinked
 * signal on both pads.
 *
 * Returns: TRUE if the pads were unlinked. This function returns FALSE if
 * the pads were not linked together.
 *
 * MT safe.
 */
gboolean
gst_pad_unlink (GstPad * srcpad, GstPad * sinkpad)
{
  gboolean result = FALSE;
  GstElement *parent = NULL;
  gint i;

  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (GST_PAD_IS_SRC (srcpad), FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);
  g_return_val_if_fail (GST_PAD_IS_SINK (sinkpad), FALSE);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinking %s:%s(%p) and %s:%s(%p)",
      GST_DEBUG_PAD_NAME (srcpad), srcpad,
      GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

  /* We need to notify the parent before taking any pad locks as the bin in
   * question might be waiting for a lock on the pad while holding its lock
   * that our message will try to take. */
  if ((parent = GST_ELEMENT_CAST (gst_pad_get_parent (srcpad)))) {
    if (GST_IS_ELEMENT (parent)) {
      gst_element_post_message (parent,
          gst_message_new_structure_change (GST_OBJECT_CAST (sinkpad),
              GST_STRUCTURE_CHANGE_TYPE_PAD_UNLINK, parent, TRUE));
    } else {
      gst_object_unref (parent);
      parent = NULL;
    }
  }

  GST_OBJECT_LOCK (srcpad);

  GST_OBJECT_LOCK (sinkpad);

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

  /* clear pending caps if any */
  for (i = 0; i < GST_EVENT_MAX_STICKY; i++)
    gst_event_replace (&sinkpad->priv->events[i].pending, NULL);

  GST_OBJECT_UNLOCK (sinkpad);
  GST_OBJECT_UNLOCK (srcpad);

  /* fire off a signal to each of the pads telling them
   * that they've been unlinked */
  g_signal_emit (srcpad, gst_pad_signals[PAD_UNLINKED], 0, sinkpad);
  g_signal_emit (sinkpad, gst_pad_signals[PAD_UNLINKED], 0, srcpad);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinked %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  result = TRUE;

done:
  if (parent != NULL) {
    gst_element_post_message (parent,
        gst_message_new_structure_change (GST_OBJECT_CAST (sinkpad),
            GST_STRUCTURE_CHANGE_TYPE_PAD_UNLINK, parent, FALSE));
    gst_object_unref (parent);
  }
  return result;

  /* ERRORS */
not_linked_together:
  {
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
    goto done;
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
gst_pad_link_check_compatible_unlocked (GstPad * src, GstPad * sink,
    GstPadLinkCheck flags)
{
  GstCaps *srccaps = NULL;
  GstCaps *sinkcaps = NULL;
  gboolean compatible = FALSE;

  if (!(flags & (GST_PAD_LINK_CHECK_CAPS | GST_PAD_LINK_CHECK_TEMPLATE_CAPS)))
    return TRUE;

  /* Doing the expensive caps checking takes priority over only checking the template caps */
  if (flags & GST_PAD_LINK_CHECK_CAPS) {
    srccaps = gst_pad_get_caps_unlocked (src, NULL);
    sinkcaps = gst_pad_get_caps_unlocked (sink, NULL);
  } else {
    /* If one of the two pads doesn't have a template, consider the intersection
     * as valid.*/
    if (G_UNLIKELY ((GST_PAD_PAD_TEMPLATE (src) == NULL)
            || (GST_PAD_PAD_TEMPLATE (sink) == NULL))) {
      compatible = TRUE;
      goto done;
    }
    srccaps = gst_caps_ref (GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (src)));
    sinkcaps =
        gst_caps_ref (GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (sink)));
  }

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, src, "src caps %" GST_PTR_FORMAT,
      srccaps);
  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, sink, "sink caps %" GST_PTR_FORMAT,
      sinkcaps);

  /* if we have caps on both pads we can check the intersection. If one
   * of the caps is NULL, we return TRUE. */
  if (G_UNLIKELY (srccaps == NULL || sinkcaps == NULL)) {
    if (srccaps)
      gst_caps_unref (srccaps);
    if (sinkcaps)
      gst_caps_unref (sinkcaps);
    goto done;
  }

  compatible = gst_caps_can_intersect (srccaps, sinkcaps);
  gst_caps_unref (srccaps);
  gst_caps_unref (sinkcaps);

done:
  GST_CAT_DEBUG (GST_CAT_CAPS, "caps are %scompatible",
      (compatible ? "" : "not"));

  return compatible;
}

/* check if the grandparents of both pads are the same.
 * This check is required so that we don't try to link
 * pads from elements in different bins without ghostpads.
 *
 * The LOCK should be held on both pads
 */
static gboolean
gst_pad_link_check_hierarchy (GstPad * src, GstPad * sink)
{
  GstObject *psrc, *psink;

  psrc = GST_OBJECT_PARENT (src);
  psink = GST_OBJECT_PARENT (sink);

  /* if one of the pads has no parent, we allow the link */
  if (G_UNLIKELY (psrc == NULL || psink == NULL))
    goto no_parent;

  /* only care about parents that are elements */
  if (G_UNLIKELY (!GST_IS_ELEMENT (psrc) || !GST_IS_ELEMENT (psink)))
    goto no_element_parent;

  /* if the parents are the same, we have a loop */
  if (G_UNLIKELY (psrc == psink))
    goto same_parents;

  /* if they both have a parent, we check the grandparents. We can not lock
   * the parent because we hold on the child (pad) and the locking order is
   * parent >> child. */
  psrc = GST_OBJECT_PARENT (psrc);
  psink = GST_OBJECT_PARENT (psink);

  /* if they have grandparents but they are not the same */
  if (G_UNLIKELY (psrc != psink))
    goto wrong_grandparents;

  return TRUE;

  /* ERRORS */
no_parent:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "one of the pads has no parent %" GST_PTR_FORMAT " and %"
        GST_PTR_FORMAT, psrc, psink);
    return TRUE;
  }
no_element_parent:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "one of the pads has no element parent %" GST_PTR_FORMAT " and %"
        GST_PTR_FORMAT, psrc, psink);
    return TRUE;
  }
same_parents:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS, "pads have same parent %" GST_PTR_FORMAT,
        psrc);
    return FALSE;
  }
wrong_grandparents:
  {
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "pads have different grandparents %" GST_PTR_FORMAT " and %"
        GST_PTR_FORMAT, psrc, psink);
    return FALSE;
  }
}

/* FIXME leftover from an attempt at refactoring... */
/* call with the two pads unlocked, when this function returns GST_PAD_LINK_OK,
 * the two pads will be locked in the srcpad, sinkpad order. */
static GstPadLinkReturn
gst_pad_link_prepare (GstPad * srcpad, GstPad * sinkpad, GstPadLinkCheck flags)
{
  GST_CAT_INFO (GST_CAT_PADS, "trying to link %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  GST_OBJECT_LOCK (srcpad);

  if (G_UNLIKELY (GST_PAD_PEER (srcpad) != NULL))
    goto src_was_linked;

  GST_OBJECT_LOCK (sinkpad);

  if (G_UNLIKELY (GST_PAD_PEER (sinkpad) != NULL))
    goto sink_was_linked;

  /* check hierarchy, pads can only be linked if the grandparents
   * are the same. */
  if ((flags & GST_PAD_LINK_CHECK_HIERARCHY)
      && !gst_pad_link_check_hierarchy (srcpad, sinkpad))
    goto wrong_hierarchy;

  /* check pad caps for non-empty intersection */
  if (!gst_pad_link_check_compatible_unlocked (srcpad, sinkpad, flags))
    goto no_format;

  /* FIXME check pad scheduling for non-empty intersection */

  return GST_PAD_LINK_OK;

src_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "src %s:%s was already linked to %s:%s",
        GST_DEBUG_PAD_NAME (srcpad),
        GST_DEBUG_PAD_NAME (GST_PAD_PEER (srcpad)));
    /* we do not emit a warning in this case because unlinking cannot
     * be made MT safe.*/
    GST_OBJECT_UNLOCK (srcpad);
    return GST_PAD_LINK_WAS_LINKED;
  }
sink_was_linked:
  {
    GST_CAT_INFO (GST_CAT_PADS, "sink %s:%s was already linked to %s:%s",
        GST_DEBUG_PAD_NAME (sinkpad),
        GST_DEBUG_PAD_NAME (GST_PAD_PEER (sinkpad)));
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
 * gst_pad_can_link:
 * @srcpad: the source #GstPad.
 * @sinkpad: the sink #GstPad.
 *
 * Checks if the source pad and the sink pad are compatible so they can be
 * linked.
 *
 * Returns: TRUE if the pads can be linked.
 */
gboolean
gst_pad_can_link (GstPad * srcpad, GstPad * sinkpad)
{
  GstPadLinkReturn result;

  /* generic checks */
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  GST_CAT_INFO (GST_CAT_PADS, "check if %s:%s can link with %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* gst_pad_link_prepare does everything for us, we only release the locks
   * on the pads that it gets us. If this function returns !OK the locks are not
   * taken anymore. */
  result = gst_pad_link_prepare (srcpad, sinkpad, GST_PAD_LINK_CHECK_DEFAULT);
  if (result != GST_PAD_LINK_OK)
    goto done;

  GST_OBJECT_UNLOCK (srcpad);
  GST_OBJECT_UNLOCK (sinkpad);

done:
  return result == GST_PAD_LINK_OK;
}

/**
 * gst_pad_link_full:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 * @flags: the checks to validate when linking
 *
 * Links the source pad and the sink pad.
 *
 * This variant of #gst_pad_link provides a more granular control on the
 * checks being done when linking. While providing some considerable speedups
 * the caller of this method must be aware that wrong usage of those flags
 * can cause severe issues. Refer to the documentation of #GstPadLinkCheck
 * for more information.
 *
 * MT Safe.
 *
 * Returns: A result code indicating if the connection worked or
 *          what went wrong.
 *
 * Since: 0.10.30
 */
GstPadLinkReturn
gst_pad_link_full (GstPad * srcpad, GstPad * sinkpad, GstPadLinkCheck flags)
{
  GstPadLinkReturn result;
  GstElement *parent;
  guint i;
  gboolean pending;

  g_return_val_if_fail (GST_IS_PAD (srcpad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_PAD_IS_SRC (srcpad), GST_PAD_LINK_WRONG_DIRECTION);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_PAD_IS_SINK (sinkpad),
      GST_PAD_LINK_WRONG_DIRECTION);

  /* Notify the parent early. See gst_pad_unlink for details. */
  if ((parent = GST_ELEMENT_CAST (gst_pad_get_parent (srcpad)))) {
    if (GST_IS_ELEMENT (parent)) {
      gst_element_post_message (parent,
          gst_message_new_structure_change (GST_OBJECT_CAST (sinkpad),
              GST_STRUCTURE_CHANGE_TYPE_PAD_LINK, parent, TRUE));
    } else {
      gst_object_unref (parent);
      parent = NULL;
    }
  }

  /* prepare will also lock the two pads */
  result = gst_pad_link_prepare (srcpad, sinkpad, flags);

  if (result != GST_PAD_LINK_OK)
    goto done;

  /* must set peers before calling the link function */
  GST_PAD_PEER (srcpad) = sinkpad;
  GST_PAD_PEER (sinkpad) = srcpad;

  /* make sure we push the events from the source to this new peer, for this we
   * copy the events on the sinkpad and mark EVENTS_PENDING */
  pending = FALSE;
  for (i = 0; i < GST_EVENT_MAX_STICKY; i++)
    pending |= replace_event (srcpad, sinkpad, i);

  /* we had some new pending events, set our flag */
  if (pending)
    GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_NEED_EVENTS);

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
    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);

    /* fire off a signal to each of the pads telling them
     * that they've been linked */
    g_signal_emit (srcpad, gst_pad_signals[PAD_LINKED], 0, sinkpad);
    g_signal_emit (sinkpad, gst_pad_signals[PAD_LINKED], 0, srcpad);

    GST_CAT_INFO (GST_CAT_PADS, "linked %s:%s and %s:%s, successful",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

    gst_pad_send_event (srcpad, gst_event_new_reconfigure ());
  } else {
    GST_CAT_INFO (GST_CAT_PADS, "link between %s:%s and %s:%s failed",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

    GST_PAD_PEER (srcpad) = NULL;
    GST_PAD_PEER (sinkpad) = NULL;

    GST_OBJECT_UNLOCK (sinkpad);
    GST_OBJECT_UNLOCK (srcpad);
  }

done:
  if (parent) {
    gst_element_post_message (parent,
        gst_message_new_structure_change (GST_OBJECT_CAST (sinkpad),
            GST_STRUCTURE_CHANGE_TYPE_PAD_LINK, parent, FALSE));
    gst_object_unref (parent);
  }

  return result;
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
  return gst_pad_link_full (srcpad, sinkpad, GST_PAD_LINK_CHECK_DEFAULT);
}

static void
gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ)
{
  GstPadTemplate **template_p;

  /* this function would need checks if it weren't static */

  GST_OBJECT_LOCK (pad);
  template_p = &pad->padtemplate;
  gst_object_replace ((GstObject **) template_p, (GstObject *) templ);
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
 * Returns: (transfer full): the #GstPadTemplate from which this pad was
 *     instantiated, or %NULL if this pad has no template. Unref after
 *     usage.
 */
GstPadTemplate *
gst_pad_get_pad_template (GstPad * pad)
{
  GstPadTemplate *templ;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  templ = GST_PAD_PAD_TEMPLATE (pad);

  return (templ ? gst_object_ref (templ) : NULL);
}

/* should be called with the pad LOCK held */
/* refs the caps, so caller is responsible for getting it unreffed */
static GstCaps *
gst_pad_get_caps_unlocked (GstPad * pad, GstCaps * filter)
{
  GstCaps *result = NULL;
  GstPadTemplate *templ;
  gboolean fixed_caps;

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "get pad caps");

  fixed_caps = GST_PAD_IS_FIXED_CAPS (pad);

  if (!fixed_caps && GST_PAD_GETCAPSFUNC (pad)) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
        "dispatching to pad getcaps function with "
        "filter %" GST_PTR_FORMAT, filter);

    GST_OBJECT_FLAG_SET (pad, GST_PAD_IN_GETCAPS);
    GST_OBJECT_UNLOCK (pad);
    result = GST_PAD_GETCAPSFUNC (pad) (pad, filter);
    GST_OBJECT_LOCK (pad);
    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_IN_GETCAPS);

    if (result == NULL) {
      g_critical ("pad %s:%s returned NULL caps from getcaps function",
          GST_DEBUG_PAD_NAME (pad));
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
          "pad getcaps returned %" GST_PTR_FORMAT, result);
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
              ("pad %s:%s returned caps which are not a real "
              "subset of its template caps", GST_DEBUG_PAD_NAME (pad));
          temp = gst_caps_intersect (templ_caps, result);
          gst_caps_unref (result);
          result = temp;
        }
      }
      if (filter) {
        if (!gst_caps_is_subset (result, filter)) {
          GstCaps *temp;

          GST_CAT_ERROR_OBJECT (GST_CAT_CAPS, pad,
              "pad returned caps %" GST_PTR_FORMAT
              " which are not a real subset of the filter caps %"
              GST_PTR_FORMAT, result, filter);
          g_warning ("pad %s:%s returned caps which are not a real "
              "subset of the filter caps", GST_DEBUG_PAD_NAME (pad));
          /* FIXME: Order? But shouldn't happen anyway... */
          temp =
              gst_caps_intersect_full (filter, result,
              GST_CAPS_INTERSECT_FIRST);
          gst_caps_unref (result);
          result = temp;
        }
      }
#endif
      goto done;
    }
  }
  if (fixed_caps && (result = get_pad_caps (pad))) {
    if (filter) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
          "using pad caps %p %" GST_PTR_FORMAT " with filter %p %"
          GST_PTR_FORMAT, result, result, filter, filter);
      result =
          gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "result %p %" GST_PTR_FORMAT,
          result);
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
          "using pad caps %p %" GST_PTR_FORMAT, result, result);
      result = gst_caps_ref (result);
    }
    goto done;
  }
  if ((templ = GST_PAD_PAD_TEMPLATE (pad))) {
    result = GST_PAD_TEMPLATE_CAPS (templ);

    if (filter) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
          "using pad template %p with caps %p %" GST_PTR_FORMAT
          " and filter %p %" GST_PTR_FORMAT, templ, result, result, filter,
          filter);
      result =
          gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "result %p %" GST_PTR_FORMAT,
          result);
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
          "using pad template %p with caps %p %" GST_PTR_FORMAT, templ, result,
          result);
      result = gst_caps_ref (result);
    }

    goto done;
  }
  if (!fixed_caps && (result = get_pad_caps (pad))) {
    if (filter) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
          "using pad caps %p %" GST_PTR_FORMAT " with filter %p %"
          GST_PTR_FORMAT, result, result, filter, filter);
      result =
          gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "result %p %" GST_PTR_FORMAT,
          result);
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
          "using pad caps %p %" GST_PTR_FORMAT, result, result);
      result = gst_caps_ref (result);
    }

    goto done;
  }

  /* this almost never happens */
  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "pad has no caps");
  result = gst_caps_new_empty ();

done:
  return result;
}

/**
 * gst_pad_has_current_caps:
 * @pad: a  #GstPad to check
 *
 * Check if @pad has caps set on it with gst_pad_set_caps().
 *
 * Returns: TRUE when @pad has caps associated with it.
 */
gboolean
gst_pad_has_current_caps (GstPad * pad)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_OBJECT_LOCK (pad);
  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "check current pad caps");
  result = (get_pad_caps (pad) != NULL);
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_get_current_caps:
 * @pad: a  #GstPad to get the current capabilities of.
 *
 * Gets the capabilities currently configured on @pad with the last call to
 * gst_pad_set_caps().
 *
 * Returns: the current caps of the pad with incremented ref-count.
 */
GstCaps *
gst_pad_get_current_caps (GstPad * pad)
{
  GstCaps *result;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_OBJECT_LOCK (pad);
  if ((result = get_pad_caps (pad)))
    gst_caps_ref (result);
  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
      "get current pad caps %" GST_PTR_FORMAT, result);
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_get_caps:
 * @pad: a  #GstPad to get the capabilities of.
 * @filter: suggested #GstCaps.
 *
 * Gets the capabilities this pad can produce or consume.
 * Note that this method doesn't necessarily return the caps set by
 * gst_pad_set_caps() - use gst_pad_get_current_caps() for that instead.
 * gst_pad_get_caps returns all possible caps a pad can operate with, using
 * the pad's get_caps function;
 * this returns the pad template caps if not explicitly set.
 *
 * When called on sinkpads @filter contains the caps that
 * upstream could produce in the order preferred by upstream. When
 * called on srcpads @filter contains the caps accepted by
 * downstream in the preffered order. @filter might be %NULL but
 * if it is not %NULL the returned caps will be a subset of @filter.
 *
 * Note that this function does not return writable #GstCaps, use
 * gst_caps_make_writable() before modifying the caps.
 *
 * Returns: the caps of the pad with incremented ref-count.
 */
GstCaps *
gst_pad_get_caps (GstPad * pad, GstCaps * filter)
{
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail (filter == NULL || GST_IS_CAPS (filter), NULL);

  GST_OBJECT_LOCK (pad);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "get pad caps");

  result = gst_pad_get_caps_unlocked (pad, filter);

  GST_OBJECT_UNLOCK (pad);

  return result;
}


/**
 * gst_pad_peer_get_caps:
 * @pad: a  #GstPad to get the capabilities of.
 * @filter: a #GstCaps filter.
 *
 * Gets the capabilities of the peer connected to this pad. Similar to
 * gst_pad_get_caps().
 *
 * When called on srcpads @filter contains the caps that
 * upstream could produce in the order preferred by upstream. When
 * called on sinkpads @filter contains the caps accepted by
 * downstream in the preffered order. @filter might be %NULL but
 * if it is not %NULL the returned caps will be a subset of @filter.
 *
 * Returns: the caps of the peer pad with incremented ref-count. This function
 * returns %NULL when there is no peer pad.
 */
GstCaps *
gst_pad_peer_get_caps (GstPad * pad, GstCaps * filter)
{
  GstPad *peerpad;
  GstCaps *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail (filter == NULL || GST_IS_CAPS (filter), NULL);

  GST_OBJECT_LOCK (pad);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "get peer caps");

  peerpad = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  gst_object_ref (peerpad);
  GST_OBJECT_UNLOCK (pad);

  result = gst_pad_get_caps (peerpad, filter);

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
  } else if (G_VALUE_TYPE (src) == GST_TYPE_FRACTION_RANGE) {
    gst_value_init_and_copy (dest, gst_value_get_fraction_range_min (src));
  } else if (G_VALUE_TYPE (src) == GST_TYPE_LIST) {
    GValue temp = { 0 };

    /* list could be empty */
    if (gst_value_list_get_size (src) <= 0)
      return FALSE;

    gst_value_init_and_copy (&temp, gst_value_list_get_value (src, 0));

    if (!fixate_value (dest, &temp))
      gst_value_init_and_copy (dest, &temp);
    g_value_unset (&temp);
  } else if (G_VALUE_TYPE (src) == GST_TYPE_ARRAY) {
    gboolean res = FALSE;
    guint n, len;

    len = gst_value_array_get_size (src);
    g_value_init (dest, GST_TYPE_ARRAY);
    for (n = 0; n < len; n++) {
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
  GstStructure *s;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (caps != NULL);
  g_return_if_fail (!gst_caps_is_empty (caps));
  g_return_if_fail (!gst_caps_is_any (caps));

  if (gst_caps_is_fixed (caps) || gst_caps_is_any (caps))
    return;

  fixatefunc = GST_PAD_FIXATECAPSFUNC (pad);
  if (fixatefunc) {
    fixatefunc (pad, caps);
  }

  /* default fixation */
  gst_caps_truncate (caps);
  s = gst_caps_get_structure (caps, 0);
  gst_structure_foreach (s, gst_pad_default_fixate, s);
}

/* Default accept caps implementation just checks against
 * against the allowed caps for the pad */
static gboolean
gst_pad_acceptcaps_default (GstPad * pad, GstCaps * caps)
{
  /* get the caps and see if it intersects to something not empty */
  GstCaps *allowed;
  gboolean result = FALSE;

  GST_DEBUG_OBJECT (pad, "caps %" GST_PTR_FORMAT, caps);

  allowed = gst_pad_get_caps (pad, NULL);
  if (!allowed)
    goto nothing_allowed;

  GST_DEBUG_OBJECT (pad, "allowed caps %" GST_PTR_FORMAT, allowed);

  result = gst_caps_can_intersect (allowed, caps);

  gst_caps_unref (allowed);

  return result;

  /* ERRORS */
nothing_allowed:
  {
    GST_DEBUG_OBJECT (pad, "no caps allowed on the pad");
    return FALSE;
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
#if 0
  GstCaps *existing = NULL;
#endif

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* any pad can be unnegotiated */
  if (caps == NULL)
    return TRUE;

  /* lock for checking the existing caps */
  GST_OBJECT_LOCK (pad);
  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "accept caps of %p", caps);
#if 0
  /* The current caps on a pad are trivially acceptable */
  if (G_LIKELY ((existing = GST_PAD_CAPS (pad)))) {
    if (caps == existing || gst_caps_is_equal (caps, existing))
      goto is_same_caps;
  }
#endif
  acceptfunc = GST_PAD_ACCEPTCAPSFUNC (pad);
  GST_OBJECT_UNLOCK (pad);

  if (G_LIKELY (acceptfunc)) {
    /* we can call the function */
    result = acceptfunc (pad, caps);
    GST_DEBUG_OBJECT (pad, "acceptfunc returned %d", result);
  } else {
    /* Only null if the element explicitly unset it */
    result = gst_pad_acceptcaps_default (pad, caps);
    GST_DEBUG_OBJECT (pad, "default acceptcaps returned %d", result);
  }
  return result;

#if 0
is_same_caps:
  {
    GST_DEBUG_OBJECT (pad, "pad had same caps");
    GST_OBJECT_UNLOCK (pad);
    return TRUE;
  }
#endif
}

/**
 * gst_pad_peer_accept_caps:
 * @pad: a  #GstPad to check the peer of
 * @caps: a #GstCaps to check on the pad
 *
 * Check if the peer of @pad accepts @caps. If @pad has no peer, this function
 * returns TRUE.
 *
 * Returns: TRUE if the peer of @pad can accept the caps or @pad has no peer.
 */
gboolean
gst_pad_peer_accept_caps (GstPad * pad, GstCaps * caps)
{
  GstPad *peerpad;
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_OBJECT_LOCK (pad);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "peer accept caps of (%p)", pad);

  peerpad = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  gst_object_ref (peerpad);
  /* release lock before calling external methods but keep ref to pad */
  GST_OBJECT_UNLOCK (pad);

  result = gst_pad_accept_caps (peerpad, caps);

  gst_object_unref (peerpad);

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
 * @caps: (transfer none): a #GstCaps to set.
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
  GstEvent *event;
  gboolean res = TRUE;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (caps != NULL && gst_caps_is_fixed (caps), FALSE);

  event = gst_event_new_caps (caps);

  if (GST_PAD_IS_SRC (pad))
    gst_pad_push_event (pad, event);
  else
    res = gst_pad_send_event (pad, event);

  return res;
}

static gboolean
gst_pad_call_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPadSetCapsFunction setcaps;

  GST_OBJECT_LOCK (pad);
  setcaps = GST_PAD_SETCAPSFUNC (pad);

  /* call setcaps function to configure the pad only if the
   * caps is not NULL */
  if (setcaps != NULL) {
    if (!GST_PAD_IS_IN_SETCAPS (pad)) {
      GST_OBJECT_FLAG_SET (pad, GST_PAD_IN_SETCAPS);
      GST_OBJECT_UNLOCK (pad);
      if (!setcaps (pad, caps))
        goto setcaps_failed;
      GST_OBJECT_LOCK (pad);
      GST_OBJECT_FLAG_UNSET (pad, GST_PAD_IN_SETCAPS);
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "pad was dispatching");
    }
  }
  GST_OBJECT_UNLOCK (pad);

  g_object_notify_by_pspec ((GObject *) pad, pspec_caps);

  return TRUE;

  /* ERRORS */
setcaps_failed:
  {
    return FALSE;
  }
}

static gboolean
do_event_function (GstPad * pad, GstEvent * event,
    GstPadEventFunction eventfunc)
{
  gboolean result = TRUE;
  GstCaps *caps, *templ;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      /* backwards compatibility mode for caps */
      gst_event_parse_caps (event, &caps);

      /* See if pad accepts the caps */
      templ = gst_pad_get_pad_template_caps (pad);
      if (!gst_caps_can_intersect (caps, templ))
        goto not_accepted;

      if (!gst_pad_call_setcaps (pad, caps))
        goto not_accepted;

      gst_caps_unref (templ);
      break;
    }
    default:
      break;
  }

  GST_DEBUG_OBJECT (pad, "calling event function with event %p", event);
  result = eventfunc (pad, event);

  return result;

  /* ERRORS */
not_accepted:
  {
    gst_caps_unref (templ);
    gst_event_unref (event);
    GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
        "caps %" GST_PTR_FORMAT " not accepted", caps);
    return FALSE;
  }
}

/* function to send all pending events on the sinkpad to the event
 * function and collect the results. This function should be called with
 * the object lock. The object lock might be released by this function.
 */
static GstFlowReturn
gst_pad_update_events (GstPad * pad)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;
  GstPadEventFunction eventfunc;
  GstEvent *event;

  if (G_UNLIKELY ((eventfunc = GST_PAD_EVENTFUNC (pad)) == NULL))
    goto no_function;

restart:
  for (i = 0; i < GST_EVENT_MAX_STICKY; i++) {
    gboolean res;
    PadEvent *ev;

    ev = &pad->priv->events[i];

    /* skip without pending event */
    if ((event = ev->pending) == NULL)
      continue;

    gst_event_ref (event);
    GST_OBJECT_UNLOCK (pad);

    res = do_event_function (pad, event, eventfunc);

    GST_OBJECT_LOCK (pad);
    /* things could have changed while we release the lock, check if we still
     * are handling the same event, if we don't something changed and we have
     * to try again. FIXME. we need a cookie here. */
    if (event != ev->pending) {
      GST_DEBUG_OBJECT (pad, "events changed, restarting");
      goto restart;
    }

    /* remove the event from the pending entry in all cases */
    ev->pending = NULL;

    if (res) {
      /* make the event active */
      if (ev->event)
        gst_event_unref (ev->event);
      ev->event = event;
    } else {
      gst_event_unref (event);
      ret = GST_FLOW_ERROR;
    }
  }
  /* when we get here all events were successfully updated. */

  return ret;

  /* ERRORS */
no_function:
  {
    g_warning ("pad %s:%s has no event handler, file a bug.",
        GST_DEBUG_PAD_NAME (pad));
    return GST_FLOW_NOT_SUPPORTED;
  }
}

/**
 * gst_pad_get_pad_template_caps:
 * @pad: a #GstPad to get the template capabilities from.
 *
 * Gets the capabilities for @pad's template.
 *
 * Returns: (transfer full): the #GstCaps of this pad template.
 * Unref after usage.
 */
GstCaps *
gst_pad_get_pad_template_caps (GstPad * pad)
{
  static GstStaticCaps anycaps = GST_STATIC_CAPS ("ANY");

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_PAD_TEMPLATE (pad))
    return gst_pad_template_get_caps (GST_PAD_PAD_TEMPLATE (pad));

  return gst_static_caps_get (&anycaps);
}

/**
 * gst_pad_get_peer:
 * @pad: a #GstPad to get the peer of.
 *
 * Gets the peer of @pad. This function refs the peer pad so
 * you need to unref it after use.
 *
 * Returns: (transfer full): the peer #GstPad. Unref after usage.
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
 * @pad: a #GstPad.
 *
 * Gets the capabilities of the allowed media types that can flow through
 * @pad and its peer.
 *
 * The allowed capabilities is calculated as the intersection of the results of
 * calling gst_pad_get_caps() on @pad and its peer. The caller owns a reference
 * on the resulting caps.
 *
 * Returns: (transfer full): the allowed #GstCaps of the pad link. Unref the
 *     caps when you no longer need it. This function returns NULL when @pad
 *     has no peer.
 *
 * MT safe.
 */
GstCaps *
gst_pad_get_allowed_caps (GstPad * pad)
{
  GstCaps *mycaps;
  GstCaps *caps;
  GstCaps *peercaps;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_OBJECT_LOCK (pad);
  peer = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peer == NULL))
    goto no_peer;

  GST_CAT_DEBUG_OBJECT (GST_CAT_PROPERTIES, pad, "getting allowed caps");

  gst_object_ref (peer);
  GST_OBJECT_UNLOCK (pad);
  mycaps = gst_pad_get_caps (pad, NULL);

  peercaps = gst_pad_get_caps (peer, NULL);
  gst_object_unref (peer);

  caps = gst_caps_intersect (mycaps, peercaps);
  gst_caps_unref (peercaps);
  gst_caps_unref (mycaps);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "allowed caps %" GST_PTR_FORMAT,
      caps);

  return caps;

no_peer:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PROPERTIES, pad, "no peer");
    GST_OBJECT_UNLOCK (pad);

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
 * Returns: (transfer full): the negotiated #GstCaps of the pad link. Unref
 *     the caps when you no longer need it. This function returns NULL when
 *     the @pad has no peer or is not negotiated yet.
 *
 * MT safe.
 */
GstCaps *
gst_pad_get_negotiated_caps (GstPad * pad)
{
  GstCaps *caps = NULL;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_OBJECT_LOCK (pad);

  if (G_UNLIKELY ((peer = GST_PAD_PEER (pad)) == NULL))
    goto no_peer;

  GST_CAT_DEBUG_OBJECT (GST_CAT_PROPERTIES, pad, "getting negotiated caps");

  if ((caps = get_pad_caps (pad)))
    gst_caps_ref (caps);

  GST_OBJECT_UNLOCK (pad);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "negotiated caps %" GST_PTR_FORMAT,
      caps);

  return caps;

no_peer:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PROPERTIES, pad, "no peer");
    GST_OBJECT_UNLOCK (pad);
    return NULL;
  }
}

/**
 * gst_pad_iterate_internal_links_default:
 * @pad: the #GstPad to get the internal links of.
 *
 * Iterate the list of pads to which the given pad is linked to inside of
 * the parent element.
 * This is the default handler, and thus returns an iterator of all of the
 * pads inside the parent element with opposite direction.
 *
 * The caller must free this iterator after use with gst_iterator_free().
 *
 * Returns: a #GstIterator of #GstPad, or NULL if @pad has no parent. Unref each
 * returned pad with gst_object_unref().
 *
 * Since: 0.10.21
 */
GstIterator *
gst_pad_iterate_internal_links_default (GstPad * pad)
{
  GstIterator *res;
  GList **padlist;
  guint32 *cookie;
  GMutex *lock;
  gpointer owner;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  {
    GstElement *parent;

    GST_OBJECT_LOCK (pad);
    parent = GST_PAD_PARENT (pad);
    if (!parent || !GST_IS_ELEMENT (parent))
      goto no_parent;

    gst_object_ref (parent);
    GST_OBJECT_UNLOCK (pad);

    if (pad->direction == GST_PAD_SRC)
      padlist = &parent->sinkpads;
    else
      padlist = &parent->srcpads;

    GST_DEBUG_OBJECT (pad, "Making iterator");

    cookie = &parent->pads_cookie;
    owner = parent;
    lock = GST_OBJECT_GET_LOCK (parent);
  }

  res = gst_iterator_new_list (GST_TYPE_PAD,
      lock, cookie, padlist, (GObject *) owner, NULL);

  gst_object_unref (owner);

  return res;

  /* ERRORS */
no_parent:
  {
    GST_OBJECT_UNLOCK (pad);
    GST_DEBUG_OBJECT (pad, "no parent element");
    return NULL;
  }
}

/**
 * gst_pad_iterate_internal_links:
 * @pad: the GstPad to get the internal links of.
 *
 * Gets an iterator for the pads to which the given pad is linked to inside
 * of the parent element.
 *
 * Each #GstPad element yielded by the iterator will have its refcount increased,
 * so unref after use.
 *
 * Free-function: gst_iterator_free
 *
 * Returns: (transfer full): a new #GstIterator of #GstPad or %NULL when the
 *     pad does not have an iterator function configured. Use
 *     gst_iterator_free() after usage.
 *
 * Since: 0.10.21
 */
GstIterator *
gst_pad_iterate_internal_links (GstPad * pad)
{
  GstIterator *res = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_ITERINTLINKFUNC (pad))
    res = GST_PAD_ITERINTLINKFUNC (pad) (pad);

  return res;
}

static gboolean
gst_pad_event_default_dispatch (GstPad * pad, GstEvent * event)
{
  gboolean result = FALSE;
  GstIterator *iter;
  gboolean done = FALSE;
  GValue item = { 0, };
  GstPad *eventpad;
  GList *pushed_pads = NULL;

  GST_INFO_OBJECT (pad, "Sending event %p (%s) to all internally linked pads",
      event, GST_EVENT_TYPE_NAME (event));

  iter = gst_pad_iterate_internal_links (pad);

  if (!iter)
    goto no_iter;

  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
        eventpad = g_value_get_object (&item);

        /* if already pushed,  skip */
        if (g_list_find (pushed_pads, eventpad)) {
          g_value_reset (&item);
          break;
        }

        if (GST_PAD_IS_SRC (eventpad)) {
          /* for each pad we send to, we should ref the event; it's up
           * to downstream to unref again when handled. */
          GST_LOG_OBJECT (pad, "Reffing and sending event %p (%s) to %s:%s",
              event, GST_EVENT_TYPE_NAME (event),
              GST_DEBUG_PAD_NAME (eventpad));
          gst_event_ref (event);
          result |= gst_pad_push_event (eventpad, event);
        } else {
          /* we only send the event on one pad, multi-sinkpad elements
           * should implement a handler */
          GST_LOG_OBJECT (pad, "sending event %p (%s) to one sink pad %s:%s",
              event, GST_EVENT_TYPE_NAME (event),
              GST_DEBUG_PAD_NAME (eventpad));
          result = gst_pad_push_event (eventpad, event);
          done = TRUE;
          event = NULL;
        }

        pushed_pads = g_list_prepend (pushed_pads, eventpad);

        g_value_reset (&item);
        break;
      case GST_ITERATOR_RESYNC:
        /* We don't reset the result here because we don't push the event
         * again on pads that got the event already and because we need
         * to consider the result of the previous pushes */
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_ERROR_OBJECT (pad, "Could not iterate over internally linked pads");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (iter);

no_iter:

  /* If this is a sinkpad and we don't have pads to send the event to, we
   * return TRUE. This is so that when using the default handler on a sink
   * element, we don't fail to push it. */
  if (!pushed_pads)
    result = GST_PAD_IS_SINK (pad);

  g_list_free (pushed_pads);

  /* we handled the incoming event so we unref once */
  if (event) {
    GST_LOG_OBJECT (pad, "handled event %p, unreffing", event);
    gst_event_unref (event);
  }

  return result;
}

/**
 * gst_pad_event_default:
 * @pad: a #GstPad to call the default event handler on.
 * @event: (transfer full): the #GstEvent to handle.
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
  gboolean result = TRUE, forward = TRUE;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  GST_LOG_OBJECT (pad, "default event handler");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (pad, "pausing task because of eos");
      gst_pad_pause_task (pad);
      break;
    }
    case GST_EVENT_CAPS:
    {
      /* don't forward by default */
      forward = FALSE;
      break;
    }
    default:
      break;
  }

  if (forward)
    result = gst_pad_event_default_dispatch (pad, event);
  else
    gst_event_unref (event);

  return result;
}

/**
 * gst_pad_dispatcher:
 * @pad: a #GstPad to dispatch.
 * @dispatch: the #GstPadDispatcherFunction to call.
 * @data: (closure): gpointer user data passed to the dispatcher function.
 *
 * Invokes the given dispatcher function on each respective peer of
 * all pads that are internally linked to the given pad.
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
  GstIterator *iter = NULL;
  gboolean done = FALSE;
  GValue item = { 0, };

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (dispatch != NULL, FALSE);

  iter = gst_pad_iterate_internal_links (pad);

  if (!iter)
    goto no_iter;

  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
      {
        GstPad *int_pad = g_value_get_object (&item);
        GstPad *int_peer = gst_pad_get_peer (int_pad);

        if (int_peer) {
          GST_DEBUG_OBJECT (int_pad, "dispatching to peer %s:%s",
              GST_DEBUG_PAD_NAME (int_peer));
          done = res = dispatch (int_peer, data);
          gst_object_unref (int_peer);
        } else {
          GST_DEBUG_OBJECT (int_pad, "no peer");
        }
        g_value_reset (&item);
      }
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        GST_ERROR_OBJECT (pad, "Could not iterate internally linked pads");
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (iter);

  GST_DEBUG_OBJECT (pad, "done, result %d", res);

no_iter:

  return res;
}

/**
 * gst_pad_query:
 * @pad: a #GstPad to invoke the default query on.
 * @query: (transfer none): the #GstQuery to perform.
 *
 * Dispatches a query to a pad. The query should have been allocated by the
 * caller via one of the type-specific allocation functions. The element that
 * the pad belongs to is responsible for filling the query with an appropriate
 * response, which should then be parsed with a type-specific query parsing
 * function.
 *
 * Again, the caller is responsible for both the allocation and deallocation of
 * the query structure.
 *
 * Please also note that some queries might need a running pipeline to work.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query (GstPad * pad, GstQuery * query)
{
  gboolean res;
  GstPadQueryFunction func;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  GST_DEBUG_OBJECT (pad, "sending query %p (%s)", query,
      GST_QUERY_TYPE_NAME (query));

  if ((func = GST_PAD_QUERYFUNC (pad)) == NULL)
    goto no_func;

  res = func (pad, query);

  GST_DEBUG_OBJECT (pad, "sent query %p (%s), result %d", query,
      GST_QUERY_TYPE_NAME (query), res);

  return res;

no_func:
  {
    GST_DEBUG_OBJECT (pad, "had no query function");
    return FALSE;
  }
}

/**
 * gst_pad_peer_query:
 * @pad: a #GstPad to invoke the peer query on.
 * @query: (transfer none): the #GstQuery to perform.
 *
 * Performs gst_pad_query() on the peer of @pad.
 *
 * The caller is responsible for both the allocation and deallocation of
 * the query structure.
 *
 * Returns: TRUE if the query could be performed. This function returns %FALSE
 * if @pad has no peer.
 *
 * Since: 0.10.15
 */
gboolean
gst_pad_peer_query (GstPad * pad, GstQuery * query)
{
  GstPad *peerpad;
  gboolean result;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  GST_OBJECT_LOCK (pad);

  GST_DEBUG_OBJECT (pad, "peer query %p (%s)", query,
      GST_QUERY_TYPE_NAME (query));

  peerpad = GST_PAD_PEER (pad);
  if (G_UNLIKELY (peerpad == NULL))
    goto no_peer;

  gst_object_ref (peerpad);
  GST_OBJECT_UNLOCK (pad);

  result = gst_pad_query (peerpad, query);

  gst_object_unref (peerpad);

  return result;

  /* ERRORS */
no_peer:
  {
    GST_WARNING_OBJECT (pad, "pad has no peer");
    GST_OBJECT_UNLOCK (pad);
    return FALSE;
  }
}

/**
 * gst_pad_query_default:
 * @pad: a #GstPad to call the default query handler on.
 * @query: (transfer none): the #GstQuery to handle.
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

/*
 * should be called with pad OBJECT_LOCK and STREAM_LOCK held.
 * GST_PAD_IS_BLOCKED (pad) == TRUE when this function is
 * called.
 *
 * This function performs the pad blocking when an event, buffer push
 * is performed on a _SRC_ pad. It blocks the streaming thread after
 * informing the pad has been blocked.
 *
 * An application can with this method wait and block any streaming
 * thread and perform operations such as seeking or linking.
 *
 * Two methods are available for notifying the application of the
 * block:
 * - the callback method, which happens in the STREAMING thread with
 *   the STREAM_LOCK held. With this method, the most useful way of
 *   dealing with the callback is to post a message to the main thread
 *   where the pad block can then be handled outside of the streaming
 *   thread. With the last method one can perform all operations such
 *   as doing a state change, linking, unblocking, seeking etc on the
 *   pad.
 * - the GCond signal method, which makes any thread unblock when
 *   the pad block happens.
 *
 * During the actual blocking state, the GST_PAD_BLOCKING flag is set.
 * The GST_PAD_BLOCKING flag is unset when the pad was unblocked.
 *
 * MT safe.
 */
static GstFlowReturn
handle_pad_block (GstPad * pad)
{
  GstPadBlockCallback callback;
  gpointer user_data;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "signal block taken");

  /* flushing, don't bother trying to block and return WRONG_STATE
   * right away */
  if (GST_PAD_IS_FLUSHING (pad))
    goto flushingnonref;

  /* we grab an extra ref for the callbacks, this is probably not
   * needed (callback code does not have a ref and cannot unref). I
   * think this was done to make it possible to unref the element in
   * the callback, which is in the end totally impossible as it
   * requires grabbing the STREAM_LOCK and OBJECT_LOCK which are
   * all taken when calling this function. */
  gst_object_ref (pad);

  while (GST_PAD_IS_BLOCKED (pad)) {
    do {
      /* we either have a callback installed to notify the block or
       * some other thread is doing a GCond wait. */
      callback = pad->block_callback;
      pad->block_callback_called = TRUE;
      if (callback) {
        /* there is a callback installed, call it. We release the
         * lock so that the callback can do something usefull with the
         * pad */
        user_data = pad->block_data;
        GST_OBJECT_UNLOCK (pad);
        callback (pad, TRUE, user_data);
        GST_OBJECT_LOCK (pad);

        /* we released the lock, recheck flushing */
        if (GST_PAD_IS_FLUSHING (pad))
          goto flushing;
      } else {
        /* no callback, signal the thread that is doing a GCond wait
         * if any. */
        GST_PAD_BLOCK_BROADCAST (pad);
      }
    } while (pad->block_callback_called == FALSE && GST_PAD_IS_BLOCKED (pad));

    /* OBJECT_LOCK could have been released when we did the callback, which
     * then could have made the pad unblock so we need to check the blocking
     * condition again.   */
    if (!GST_PAD_IS_BLOCKED (pad))
      break;

    /* now we block the streaming thread. It can be unlocked when we
     * deactivate the pad (which will also set the FLUSHING flag) or
     * when the pad is unblocked. A flushing event will also unblock
     * the pad after setting the FLUSHING flag. */
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "Waiting to be unblocked or set flushing");
    GST_OBJECT_FLAG_SET (pad, GST_PAD_BLOCKING);
    GST_PAD_BLOCK_WAIT (pad);
    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_BLOCKING);

    /* see if we got unblocked by a flush or not */
    if (GST_PAD_IS_FLUSHING (pad))
      goto flushing;
  }

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "got unblocked");

  /* when we get here, the pad is unblocked again and we perform
   * the needed unblock code. */
  callback = pad->block_callback;
  if (callback) {
    /* we need to call the callback */
    user_data = pad->block_data;
    GST_OBJECT_UNLOCK (pad);
    callback (pad, FALSE, user_data);
    GST_OBJECT_LOCK (pad);
  } else {
    /* we need to signal the thread waiting on the GCond */
    GST_PAD_BLOCK_BROADCAST (pad);
  }

  gst_object_unref (pad);

  return ret;

flushingnonref:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "pad was flushing");
    return GST_FLOW_WRONG_STATE;
  }
flushing:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "pad became flushing");
    gst_object_unref (pad);
    return GST_FLOW_WRONG_STATE;
  }
}

/* pad offsets */

/**
 * gst_pad_get_offset:
 * @pad: a #GstPad
 *
 * Get the offset applied to the running time of @pad. @pad has to be a source
 * pad.
 *
 * Returns: the offset.
 */
gint64
gst_pad_get_offset (GstPad * pad)
{
  gint64 result;

  g_return_val_if_fail (GST_IS_PAD (pad), 0);

  GST_OBJECT_LOCK (pad);
  result = pad->offset;
  GST_OBJECT_UNLOCK (pad);

  return result;
}

/**
 * gst_pad_set_offset:
 * @pad: a #GstPad
 * @offset: the offset
 *
 * Set the offset that will be applied to the running time of @pad.
 */
void
gst_pad_set_offset (GstPad * pad, gint64 offset)
{
  guint idx;
  GstPad *peer;
  GstPad *tmp = NULL;

  g_return_if_fail (GST_IS_PAD (pad));

  GST_OBJECT_LOCK (pad);
  /* if nothing changed, do nothing */
  if (pad->offset == offset)
    goto done;

  pad->offset = offset;

  /* if no peer, we just updated the offset */
  if ((peer = GST_PAD_PEER (pad)) == NULL)
    goto done;

  /* switch pads around when dealing with a sinkpad */
  if (GST_PAD_IS_SINK (pad)) {
    /* ref the peer so it doesn't go away when we release the lock */
    tmp = gst_object_ref (peer);
    /* make sure we get the peer (the srcpad) */
    GST_OBJECT_UNLOCK (pad);

    /* swap pads */
    peer = pad;
    pad = tmp;

    GST_OBJECT_LOCK (pad);
    /* check if the pad didn't get relinked */
    if (GST_PAD_PEER (pad) != peer)
      goto done;

    /* we can release the ref now */
    gst_object_unref (peer);
  }

  /* the index of the segment event in the array */
  idx = GST_EVENT_STICKY_IDX_TYPE (GST_EVENT_SEGMENT);

  /* lock order is srcpad >> sinkpad */
  GST_OBJECT_LOCK (peer);
  /* take the current segment event, adjust it and then place
   * it on the sinkpad. events on the srcpad are always active. */
  if (replace_event (pad, peer, idx))
    GST_OBJECT_FLAG_SET (peer, GST_PAD_NEED_EVENTS);

  GST_OBJECT_UNLOCK (peer);

done:
  GST_OBJECT_UNLOCK (pad);
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
  g_value_init (&args[1], GST_MINI_OBJECT_TYPE (obj));
  g_value_set_boxed (&args[1], obj);

  if (GST_IS_EVENT (obj))
    detail = event_quark;
  else if (GST_IS_BUFFER (obj))
    detail = buffer_quark;
  else if (GST_IS_BUFFER_LIST (obj))
    detail = buffer_list_quark;
  else
    detail = 0;

  /* actually emit */
  g_signal_emitv (args, gst_pad_signals[PAD_HAVE_DATA], detail, &ret);
  res = g_value_get_boolean (&ret);

  /* clean up */
  g_value_unset (&ret);
  g_value_unset (&args[0]);
  g_value_unset (&args[1]);

  return res;
}

static void
gst_pad_data_unref (gboolean is_buffer, void *data)
{
  if (G_LIKELY (is_buffer)) {
    gst_buffer_unref (data);
  } else {
    gst_buffer_list_unref (data);
  }
}

static GstFlowReturn
pad_pre_chain (GstPad * pad, gpointer data)
{
  GstFlowReturn ret;
  gboolean needs_events;
  gboolean emit_signal;

  GST_PAD_STREAM_LOCK (pad);

  GST_OBJECT_LOCK (pad);
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
    goto flushing;

  needs_events = GST_PAD_NEEDS_EVENTS (pad);
  if (G_UNLIKELY (needs_events)) {
    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_NEED_EVENTS);

    GST_DEBUG_OBJECT (pad, "need to update all events");
    ret = gst_pad_update_events (pad);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto events_error;
  }
  emit_signal = GST_PAD_DO_BUFFER_SIGNALS (pad) > 0;
  GST_OBJECT_UNLOCK (pad);

  /* see if the signal should be emited */
  if (G_UNLIKELY (emit_signal)) {
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT_CAST (data)))
      goto dropping;
  }
  return GST_FLOW_OK;

  /* ERRORS */
flushing:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but pad was flushing");
    GST_OBJECT_UNLOCK (pad);
    GST_PAD_STREAM_UNLOCK (pad);
    return GST_FLOW_WRONG_STATE;
  }
events_error:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad, "events were not accepted");
    GST_OBJECT_UNLOCK (pad);
    GST_PAD_STREAM_UNLOCK (pad);
    return ret;
  }
dropping:
  {
    GST_DEBUG_OBJECT (pad, "Dropping buffer due to FALSE probe return");
    GST_PAD_STREAM_UNLOCK (pad);
    return GST_FLOW_CUSTOM_SUCCESS;
  }
}

static void
pad_post_chain (GstPad * pad)
{
  GST_PAD_STREAM_UNLOCK (pad);
}

/* this is the chain function that does not perform the additional argument
 * checking for that little extra speed.
 */
static inline GstFlowReturn
gst_pad_chain_data_unchecked (GstPad * pad, gboolean is_buffer, void *data)
{
  GstFlowReturn ret;

  if (G_UNLIKELY ((ret = pad_pre_chain (pad, data)) != GST_FLOW_OK))
    goto error;

  /* NOTE: we read the chainfunc unlocked.
   * we cannot hold the lock for the pad so we might send
   * the data to the wrong function. This is not really a
   * problem since functions are assigned at creation time
   * and don't change that often... */
  if (G_LIKELY (is_buffer)) {
    GstPadChainFunction chainfunc;

    if (G_UNLIKELY ((chainfunc = GST_PAD_CHAINFUNC (pad)) == NULL))
      goto no_function;

    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "calling chainfunction &%s with buffer %p",
        GST_DEBUG_FUNCPTR_NAME (chainfunc), data);

    ret = chainfunc (pad, GST_BUFFER_CAST (data));

    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "called chainfunction &%s with buffer %p, returned %s",
        GST_DEBUG_FUNCPTR_NAME (chainfunc), data, gst_flow_get_name (ret));
  } else {
    GstPadChainListFunction chainlistfunc;

    if (G_UNLIKELY ((chainlistfunc = GST_PAD_CHAINLISTFUNC (pad)) == NULL))
      goto no_function;

    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "calling chainlistfunction &%s",
        GST_DEBUG_FUNCPTR_NAME (chainlistfunc));

    ret = chainlistfunc (pad, GST_BUFFER_LIST_CAST (data));

    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "called chainlistfunction &%s, returned %s",
        GST_DEBUG_FUNCPTR_NAME (chainlistfunc), gst_flow_get_name (ret));
  }

  pad_post_chain (pad);

  return ret;

  /* ERRORS */
error:
  {
    gst_pad_data_unref (is_buffer, data);

    switch (ret) {
      case GST_FLOW_CUSTOM_SUCCESS:
        GST_DEBUG_OBJECT (pad, "dropped buffer");
        ret = GST_FLOW_OK;
        break;
      default:
        GST_DEBUG_OBJECT (pad, "en error occured %s", gst_flow_get_name (ret));
        break;
    }
    return ret;
  }
no_function:
  {
    gst_pad_data_unref (is_buffer, data);
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but not chainhandler");
    GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
        ("push on pad %s:%s but it has no chainfunction",
            GST_DEBUG_PAD_NAME (pad)));
    pad_post_chain (pad);
    return GST_FLOW_NOT_SUPPORTED;
  }
}

/**
 * gst_pad_chain:
 * @pad: a sink #GstPad, returns GST_FLOW_ERROR if not.
 * @buffer: (transfer full): the #GstBuffer to send, return GST_FLOW_ERROR
 *     if not.
 *
 * Chain a buffer to @pad.
 *
 * The function returns #GST_FLOW_WRONG_STATE if the pad was flushing.
 *
 * If the caps on @buffer are different from the current caps on @pad, this
 * function will call any setcaps function (see gst_pad_set_setcaps_function())
 * installed on @pad. If the new caps are not acceptable for @pad, this
 * function returns #GST_FLOW_NOT_NEGOTIATED.
 *
 * The function proceeds calling the chain function installed on @pad (see
 * gst_pad_set_chain_function()) and the return value of that function is
 * returned to the caller. #GST_FLOW_NOT_SUPPORTED is returned if @pad has no
 * chain function.
 *
 * In all cases, success or failure, the caller loses its reference to @buffer
 * after calling this function.
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_chain (GstPad * pad, GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SINK (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  return gst_pad_chain_data_unchecked (pad, TRUE, buffer);
}

static GstFlowReturn
gst_pad_chain_list_default (GstPad * pad, GstBufferList * list)
{
  guint i, len;
  GstBuffer *buffer;
  GstFlowReturn ret;

  GST_INFO_OBJECT (pad, "chaining each group in list as a merged buffer");

  len = gst_buffer_list_len (list);

  ret = GST_FLOW_OK;
  for (i = 0; i < len; i++) {
    buffer = gst_buffer_list_get (list, i);
    ret = gst_pad_chain_data_unchecked (pad, TRUE, gst_buffer_ref (buffer));
    if (ret != GST_FLOW_OK)
      break;
  }
  gst_buffer_list_unref (list);

  return ret;
}

/**
 * gst_pad_chain_list:
 * @pad: a sink #GstPad, returns GST_FLOW_ERROR if not.
 * @list: (transfer full): the #GstBufferList to send, return GST_FLOW_ERROR
 *     if not.
 *
 * Chain a bufferlist to @pad.
 *
 * The function returns #GST_FLOW_WRONG_STATE if the pad was flushing.
 *
 * If the caps on the first buffer of @list are different from the current
 * caps on @pad, this function will call any setcaps function
 * (see gst_pad_set_setcaps_function()) installed on @pad. If the new caps
 * are not acceptable for @pad, this function returns #GST_FLOW_NOT_NEGOTIATED.
 *
 * The function proceeds calling the chainlist function installed on @pad (see
 * gst_pad_set_chain_list_function()) and the return value of that function is
 * returned to the caller. #GST_FLOW_NOT_SUPPORTED is returned if @pad has no
 * chainlist function.
 *
 * In all cases, success or failure, the caller loses its reference to @list
 * after calling this function.
 *
 * MT safe.
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * Since: 0.10.24
 */
GstFlowReturn
gst_pad_chain_list (GstPad * pad, GstBufferList * list)
{
  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SINK (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER_LIST (list), GST_FLOW_ERROR);

  return gst_pad_chain_data_unchecked (pad, FALSE, list);
}

static GstFlowReturn
pad_pre_push (GstPad * pad, GstPad ** peer, gpointer data)
{
  GstFlowReturn ret;
  gboolean need_probes, did_probes = FALSE;

again:
  GST_OBJECT_LOCK (pad);
  /* FIXME: this check can go away; pad_set_blocked could be implemented with
   * probes completely or probes with an extended pad block. */
  while (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad)))
    if ((ret = handle_pad_block (pad)) != GST_FLOW_OK)
      goto flushed;

  need_probes = GST_PAD_DO_BUFFER_SIGNALS (pad) > 0;

  /* we emit signals on the pad arg, the peer will have a chance to
   * emit in the _chain() function */
  if (G_UNLIKELY (need_probes && !did_probes)) {
    did_probes = TRUE;
    /* unlock before emitting */
    GST_OBJECT_UNLOCK (pad);

    /* if the signal handler returned FALSE, it means we should just drop the
     * buffer */
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT_CAST (data)))
      goto dropped;

    goto again;
  }

  if (G_UNLIKELY ((*peer = GST_PAD_PEER (pad)) == NULL))
    goto not_linked;

  /* take ref to peer pad before releasing the lock */
  gst_object_ref (*peer);
  pad->priv->using++;
  GST_OBJECT_UNLOCK (pad);

  return GST_FLOW_OK;

  /* ERRORS */
flushed:
  {
    GST_DEBUG_OBJECT (pad, "pad block stopped by flush");
    GST_OBJECT_UNLOCK (pad);
    return ret;
  }
dropped:
  {
    GST_DEBUG_OBJECT (pad, "Dropping buffer due to FALSE probe return");
    return GST_FLOW_CUSTOM_SUCCESS;
  }
not_linked:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pushing, but it was not linked");
    GST_OBJECT_UNLOCK (pad);
    return GST_FLOW_NOT_LINKED;
  }
}

static void
pad_post_push (GstPad * pad)
{
  GST_OBJECT_LOCK (pad);
  pad->priv->using--;
  GST_OBJECT_UNLOCK (pad);
}

static GstFlowReturn
gst_pad_push_data (GstPad * pad, gboolean is_buffer, void *data)
{
  GstPad *peer;
  GstFlowReturn ret;

  if ((ret = pad_pre_push (pad, &peer, data)) != GST_FLOW_OK)
    goto error;

  ret = gst_pad_chain_data_unchecked (peer, is_buffer, data);

  gst_object_unref (peer);

  pad_post_push (pad);

  return ret;

  /* ERROR recovery here */
error:
  {
    gst_pad_data_unref (is_buffer, data);

    switch (ret) {
      case GST_FLOW_CUSTOM_SUCCESS:
        GST_DEBUG_OBJECT (pad, "dropped buffer");
        ret = GST_FLOW_OK;
        break;
      default:
        GST_DEBUG_OBJECT (pad, "en error occured %s", gst_flow_get_name (ret));
        break;
    }
    return ret;
  }
}

/**
 * gst_pad_push:
 * @pad: a source #GstPad, returns #GST_FLOW_ERROR if not.
 * @buffer: (transfer full): the #GstBuffer to push returns GST_FLOW_ERROR
 *     if not.
 *
 * Pushes a buffer to the peer of @pad.
 *
 * This function will call an installed pad block before triggering any
 * installed pad probes.
 *
 * If the caps on @buffer are different from the currently configured caps on
 * @pad, this function will call any installed setcaps function on @pad (see
 * gst_pad_set_setcaps_function()). In case of failure to renegotiate the new
 * format, this function returns #GST_FLOW_NOT_NEGOTIATED.
 *
 * The function proceeds calling gst_pad_chain() on the peer pad and returns
 * the value from that function. If @pad has no peer, #GST_FLOW_NOT_LINKED will
 * be returned.
 *
 * In all cases, success or failure, the caller loses its reference to @buffer
 * after calling this function.
 *
 * Returns: a #GstFlowReturn from the peer pad.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_push (GstPad * pad, GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  return gst_pad_push_data (pad, TRUE, buffer);
}

/**
 * gst_pad_push_list:
 * @pad: a source #GstPad, returns #GST_FLOW_ERROR if not.
 * @list: (transfer full): the #GstBufferList to push returns GST_FLOW_ERROR
 *     if not.
 *
 * Pushes a buffer list to the peer of @pad.
 *
 * This function will call an installed pad block before triggering any
 * installed pad probes.
 *
 * If the caps on the first buffer in the first group of @list are different
 * from the currently configured caps on @pad, this function will call any
 * installed setcaps function on @pad (see gst_pad_set_setcaps_function()). In
 * case of failure to renegotiate the new format, this function returns
 * #GST_FLOW_NOT_NEGOTIATED.
 *
 * If there are any probes installed on @pad every group of the buffer list
 * will be merged into a normal #GstBuffer and pushed via gst_pad_push and the
 * buffer list will be unreffed.
 *
 * The function proceeds calling the chain function on the peer pad and returns
 * the value from that function. If @pad has no peer, #GST_FLOW_NOT_LINKED will
 * be returned. If the peer pad does not have any installed chainlist function
 * every group buffer of the list will be merged into a normal #GstBuffer and
 * chained via gst_pad_chain().
 *
 * In all cases, success or failure, the caller loses its reference to @list
 * after calling this function.
 *
 * Returns: a #GstFlowReturn from the peer pad.
 *
 * MT safe.
 *
 * Since: 0.10.24
 */
GstFlowReturn
gst_pad_push_list (GstPad * pad, GstBufferList * list)
{
  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER_LIST (list), GST_FLOW_ERROR);

  return gst_pad_push_data (pad, FALSE, list);
}

static GstFlowReturn
gst_pad_get_range_unchecked (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstFlowReturn ret;
  GstPadGetRangeFunction getrangefunc;
  gboolean emit_signal;

  GST_PAD_STREAM_LOCK (pad);

  GST_OBJECT_LOCK (pad);
  if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
    goto flushing;

  emit_signal = GST_PAD_DO_BUFFER_SIGNALS (pad) > 0;
  GST_OBJECT_UNLOCK (pad);

  if (G_UNLIKELY ((getrangefunc = GST_PAD_GETRANGEFUNC (pad)) == NULL))
    goto no_function;

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
      "calling getrangefunc %s, offset %"
      G_GUINT64_FORMAT ", size %u",
      GST_DEBUG_FUNCPTR_NAME (getrangefunc), offset, size);

  ret = getrangefunc (pad, offset, size, buffer);

  /* can only fire the signal if we have a valid buffer */
  if (G_UNLIKELY (emit_signal) && (ret == GST_FLOW_OK)) {
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT_CAST (*buffer)))
      goto dropping;
  }
  GST_PAD_STREAM_UNLOCK (pad);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto get_range_failed;

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
    return GST_FLOW_NOT_SUPPORTED;
  }
dropping:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "Dropping data after FALSE probe return");
    GST_PAD_STREAM_UNLOCK (pad);
    gst_buffer_unref (*buffer);
    *buffer = NULL;
    return GST_FLOW_UNEXPECTED;
  }
get_range_failed:
  {
    *buffer = NULL;
    GST_CAT_LEVEL_LOG (GST_CAT_SCHEDULING,
        (ret >= GST_FLOW_UNEXPECTED) ? GST_LEVEL_INFO : GST_LEVEL_WARNING,
        pad, "getrange failed, flow: %s", gst_flow_get_name (ret));
    return ret;
  }
}

/**
 * gst_pad_get_range:
 * @pad: a src #GstPad, returns #GST_FLOW_ERROR if not.
 * @offset: The start offset of the buffer
 * @size: The length of the buffer
 * @buffer: (out callee-allocates): a pointer to hold the #GstBuffer,
 *     returns #GST_FLOW_ERROR if %NULL.
 *
 * When @pad is flushing this function returns #GST_FLOW_WRONG_STATE
 * immediatly and @buffer is %NULL.
 *
 * Calls the getrange function of @pad, see #GstPadGetRangeFunction for a
 * description of a getrange function. If @pad has no getrange function
 * installed (see gst_pad_set_getrange_function()) this function returns
 * #GST_FLOW_NOT_SUPPORTED.
 *
 * This is a lowlevel function. Usualy gst_pad_pull_range() is used.
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * MT safe.
 */
GstFlowReturn
gst_pad_get_range (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  return gst_pad_get_range_unchecked (pad, offset, size, buffer);
}

/**
 * gst_pad_pull_range:
 * @pad: a sink #GstPad, returns GST_FLOW_ERROR if not.
 * @offset: The start offset of the buffer
 * @size: The length of the buffer
 * @buffer: (out callee-allocates): a pointer to hold the #GstBuffer, returns
 *     GST_FLOW_ERROR if %NULL.
 *
 * Pulls a @buffer from the peer pad.
 *
 * This function will first trigger the pad block signal if it was
 * installed.
 *
 * When @pad is not linked #GST_FLOW_NOT_LINKED is returned else this
 * function returns the result of gst_pad_get_range() on the peer pad.
 * See gst_pad_get_range() for a list of return values and for the
 * semantics of the arguments of this function.
 *
 * @buffer's caps must either be unset or the same as what is already
 * configured on @pad. Renegotiation within a running pull-mode pipeline is not
 * supported.
 *
 * Returns: a #GstFlowReturn from the peer pad.
 * When this function returns #GST_FLOW_OK, @buffer will contain a valid
 * #GstBuffer that should be freed with gst_buffer_unref() after usage.
 * @buffer may not be used or freed when any other return value than
 * #GST_FLOW_OK is returned.
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
  gboolean needs_events;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_SINK (pad), GST_FLOW_ERROR);
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

  ret = gst_pad_get_range_unchecked (peer, offset, size, buffer);

  gst_object_unref (peer);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pull_range_failed;

  /* can only fire the signal if we have a valid buffer */
  if (G_UNLIKELY (emit_signal)) {
    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT_CAST (*buffer)))
      goto dropping;
  }

  GST_OBJECT_LOCK (pad);

  needs_events = GST_PAD_NEEDS_EVENTS (pad);
  if (G_UNLIKELY (needs_events)) {
    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_NEED_EVENTS);

    GST_DEBUG_OBJECT (pad, "we need to update the events");
    ret = gst_pad_update_events (pad);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto events_error;
  }
  GST_OBJECT_UNLOCK (pad);

  return ret;

  /* ERROR recovery here */
not_connected:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "pulling range, but it was not linked");
    GST_OBJECT_UNLOCK (pad);
    return GST_FLOW_NOT_LINKED;
  }
pull_range_failed:
  {
    *buffer = NULL;
    GST_CAT_LEVEL_LOG (GST_CAT_SCHEDULING,
        (ret >= GST_FLOW_UNEXPECTED) ? GST_LEVEL_INFO : GST_LEVEL_WARNING,
        pad, "pullrange failed, flow: %s", gst_flow_get_name (ret));
    return ret;
  }
dropping:
  {
    GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, pad,
        "Dropping data after FALSE probe return");
    gst_buffer_unref (*buffer);
    *buffer = NULL;
    return GST_FLOW_UNEXPECTED;
  }
events_error:
  {
    GST_OBJECT_UNLOCK (pad);
    gst_buffer_unref (*buffer);
    *buffer = NULL;
    GST_CAT_WARNING_OBJECT (GST_CAT_SCHEDULING, pad,
        "pullrange returned events that were not accepted");
    return ret;
  }
}

/**
 * gst_pad_push_event:
 * @pad: a #GstPad to push the event to.
 * @event: (transfer full): the #GstEvent to send to the pad.
 *
 * Sends the event to the peer of the given pad. This function is
 * mainly used by elements to send events to their peer
 * elements.
 *
 * This function takes owership of the provided event so you should
 * gst_event_ref() it if you want to reuse the event after this call.
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
  gint64 offset;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (GST_IS_EVENT (event), FALSE);

  GST_LOG_OBJECT (pad, "event: %s", GST_EVENT_TYPE_NAME (event));

  GST_OBJECT_LOCK (pad);

  /* Two checks to be made:
   * . (un)set the FLUSHING flag for flushing events,
   * . handle pad blocking */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_PAD_SET_FLUSHING (pad);

      if (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad))) {
        /* flush start will have set the FLUSHING flag and will then
         * unlock all threads doing a GCond wait on the blocking pad. This
         * will typically unblock the STREAMING thread blocked on a pad. */
        GST_LOG_OBJECT (pad, "Pad is blocked, not forwarding flush-start, "
            "doing block signal.");
        GST_PAD_BLOCK_BROADCAST (pad);
        goto flushed;
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_PAD_UNSET_FLUSHING (pad);

      /* if we are blocked, flush away the FLUSH_STOP event */
      if (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad))) {
        GST_LOG_OBJECT (pad, "Pad is blocked, not forwarding flush-stop");
        goto flushed;
      }
      break;
    case GST_EVENT_RECONFIGURE:
      if (GST_PAD_IS_SINK (pad))
        GST_OBJECT_FLAG_SET (pad, GST_PAD_NEED_RECONFIGURE);
    default:
      while (G_UNLIKELY (GST_PAD_IS_BLOCKED (pad))) {
        /* block the event as long as the pad is blocked */
        if (handle_pad_block (pad) != GST_FLOW_OK)
          goto flushed;
      }
      break;
  }

  if (G_UNLIKELY (GST_PAD_DO_EVENT_SIGNALS (pad) > 0)) {
    GST_OBJECT_UNLOCK (pad);

    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT_CAST (event)))
      goto dropping;

    GST_OBJECT_LOCK (pad);
  }

  /* store the event on the pad, but only on srcpads */
  if (GST_PAD_IS_SRC (pad) && GST_EVENT_IS_STICKY (event)) {
    guint idx;

    idx = GST_EVENT_STICKY_IDX (event);
    GST_LOG_OBJECT (pad, "storing sticky event %s at index %u",
        GST_EVENT_TYPE_NAME (event), idx);

    /* srcpad sticky events always become active immediately */
    gst_event_replace (&pad->priv->events[idx].event, event);
  }

  if ((peerpad = GST_PAD_PEER (pad)))
    gst_object_ref (peerpad);

  offset = pad->offset;
  GST_OBJECT_UNLOCK (pad);

  /* backwards compatibility mode for caps */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      /* FIXME, this is awkward because we don't check flushing here which means
       * that we can call the setcaps functions on flushing pads, this is not
       * quite what we want, otoh, this code should just go away and elements
       * that set caps on their sinkpad should just setup stuff themselves. */
      gst_pad_call_setcaps (pad, caps);
      break;
    }
    case GST_EVENT_SEGMENT:
      /* check if we need to adjust the segment */
      if (offset != 0 && peerpad != NULL) {
        GstSegment segment;

        /* copy segment values */
        gst_event_copy_segment (event, &segment);
        gst_event_unref (event);

        /* adjust and make a new event with the offset applied */
        segment.base += offset;
        event = gst_event_new_segment (&segment);
      }
      break;
    default:
      break;
  }

  /* now check the peer pad */
  if (peerpad == NULL)
    goto not_linked;

  GST_LOG_OBJECT (pad, "sending event %s to peerpad %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), peerpad);

  result = gst_pad_send_event (peerpad, event);

  /* Note: we gave away ownership of the event at this point */
  GST_LOG_OBJECT (pad, "sent event to peerpad %" GST_PTR_FORMAT ", result %d",
      peerpad, result);

  gst_object_unref (peerpad);

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
    GST_DEBUG_OBJECT (pad, "Dropping event because pad is not linked");
    gst_event_unref (event);
    return FALSE;
  }
flushed:
  {
    GST_DEBUG_OBJECT (pad,
        "Not forwarding event since we're flushing and blocking");
    gst_event_unref (event);
    GST_OBJECT_UNLOCK (pad);
    return TRUE;
  }
}

/**
 * gst_pad_send_event:
 * @pad: a #GstPad to send the event to.
 * @event: (transfer full): the #GstEvent to send to the pad.
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
 * #GST_EVENT_IS_SERIALIZED. Note that in practice that an application or
 * plugin doesn't need to bother itself with this information; the core handles
 * all necessary locks and checks.
 *
 * This function takes owership of the provided event so you should
 * gst_event_ref() it if you want to reuse the event after this call.
 *
 * Returns: TRUE if the event was handled.
 */
gboolean
gst_pad_send_event (GstPad * pad, GstEvent * event)
{
  gboolean result = FALSE;
  gboolean serialized, need_unlock = FALSE, needs_events, sticky;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  GST_OBJECT_LOCK (pad);
  if (GST_PAD_IS_SINK (pad)) {
    if (G_UNLIKELY (!GST_EVENT_IS_DOWNSTREAM (event)))
      goto wrong_direction;
    serialized = GST_EVENT_IS_SERIALIZED (event);
    sticky = GST_EVENT_IS_STICKY (event);
  } else if (GST_PAD_IS_SRC (pad)) {
    if (G_UNLIKELY (!GST_EVENT_IS_UPSTREAM (event)))
      goto wrong_direction;
    /* events on srcpad never are serialized and sticky */
    serialized = sticky = FALSE;
  } else
    goto unknown_direction;

  /* pad signals */
  if (G_UNLIKELY (GST_PAD_DO_EVENT_SIGNALS (pad) > 0)) {
    GST_OBJECT_UNLOCK (pad);

    if (!gst_pad_emit_have_data_signal (pad, GST_MINI_OBJECT_CAST (event)))
      goto dropping;

    GST_OBJECT_LOCK (pad);
  }
  /* get the flag first, we clear it when we have a FLUSH or a non-serialized
   * event. */
  needs_events = GST_PAD_NEEDS_EVENTS (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_CAT_DEBUG_OBJECT (GST_CAT_EVENT, pad,
          "have event type %d (FLUSH_START)", GST_EVENT_TYPE (event));

      /* can't even accept a flush begin event when flushing */
      if (GST_PAD_IS_FLUSHING (pad))
        goto flushing;

      GST_PAD_SET_FLUSHING (pad);
      GST_CAT_DEBUG_OBJECT (GST_CAT_EVENT, pad, "set flush flag");
      needs_events = FALSE;
      break;
    case GST_EVENT_FLUSH_STOP:
      if (G_LIKELY (GST_PAD_ACTIVATE_MODE (pad) != GST_ACTIVATE_NONE)) {
        GST_PAD_UNSET_FLUSHING (pad);
        GST_CAT_DEBUG_OBJECT (GST_CAT_EVENT, pad, "cleared flush flag");
      }
      GST_OBJECT_UNLOCK (pad);
      /* grab stream lock */
      GST_PAD_STREAM_LOCK (pad);
      need_unlock = TRUE;
      GST_OBJECT_LOCK (pad);
      needs_events = FALSE;
      break;
    case GST_EVENT_RECONFIGURE:
      if (GST_PAD_IS_SRC (pad))
        GST_OBJECT_FLAG_SET (pad, GST_PAD_NEED_RECONFIGURE);
    default:
      GST_CAT_DEBUG_OBJECT (GST_CAT_EVENT, pad, "have event type %s",
          GST_EVENT_TYPE_NAME (event));

      if (serialized) {
        /* lock order: STREAM_LOCK, LOCK, recheck flushing. */
        GST_OBJECT_UNLOCK (pad);
        GST_PAD_STREAM_LOCK (pad);
        need_unlock = TRUE;
        GST_OBJECT_LOCK (pad);
      } else {
        /* don't forward events on non-serialized events */
        needs_events = FALSE;
      }

      /* store the event on the pad, but only on srcpads. We need to store the
       * event before checking the flushing flag. */
      if (sticky) {
        guint idx;
        PadEvent *ev;

        switch (GST_EVENT_TYPE (event)) {
          case GST_EVENT_SEGMENT:
            if (pad->offset != 0) {
              GstSegment segment;

              /* copy segment values */
              gst_event_copy_segment (event, &segment);
              gst_event_unref (event);

              /* adjust and make a new event with the offset applied */
              segment.base += pad->offset;
              event = gst_event_new_segment (&segment);
            }
            break;
          default:
            break;
        }

        idx = GST_EVENT_STICKY_IDX (event);
        ev = &pad->priv->events[idx];

        GST_LOG_OBJECT (pad, "storing sticky event %s at index %u",
            GST_EVENT_TYPE_NAME (event), idx);

        if (ev->event != event) {
          gst_event_replace (&ev->pending, event);
          /* set the flag so that we update the events next time. We would
           * usually update below but we might be flushing too. */
          GST_OBJECT_FLAG_SET (pad, GST_PAD_NEED_EVENTS);
          needs_events = TRUE;
        }
      }
      /* now check the flushing flag */
      if (G_UNLIKELY (GST_PAD_IS_FLUSHING (pad)))
        goto flushing;

      break;
  }

  if (G_UNLIKELY (needs_events)) {
    GstFlowReturn ret;

    GST_OBJECT_FLAG_UNSET (pad, GST_PAD_NEED_EVENTS);

    GST_DEBUG_OBJECT (pad, "need to update all events");
    ret = gst_pad_update_events (pad);
    if (ret != GST_FLOW_OK)
      goto update_failed;
    GST_OBJECT_UNLOCK (pad);

    gst_event_unref (event);

    result = TRUE;
  } else {
    GstPadEventFunction eventfunc;

    if (G_UNLIKELY ((eventfunc = GST_PAD_EVENTFUNC (pad)) == NULL))
      goto no_function;

    GST_OBJECT_UNLOCK (pad);

    result = do_event_function (pad, event, eventfunc);
  }

  if (need_unlock)
    GST_PAD_STREAM_UNLOCK (pad);

  GST_DEBUG_OBJECT (pad, "sent event, result %d", result);

  return result;

  /* ERROR handling */
wrong_direction:
  {
    g_warning ("pad %s:%s sending %s event in wrong direction",
        GST_DEBUG_PAD_NAME (pad), GST_EVENT_TYPE_NAME (event));
    GST_OBJECT_UNLOCK (pad);
    gst_event_unref (event);
    return FALSE;
  }
unknown_direction:
  {
    g_warning ("pad %s:%s has invalid direction", GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    gst_event_unref (event);
    return FALSE;
  }
no_function:
  {
    g_warning ("pad %s:%s has no event handler, file a bug.",
        GST_DEBUG_PAD_NAME (pad));
    GST_OBJECT_UNLOCK (pad);
    if (need_unlock)
      GST_PAD_STREAM_UNLOCK (pad);
    gst_event_unref (event);
    return FALSE;
  }
flushing:
  {
    GST_OBJECT_UNLOCK (pad);
    if (need_unlock)
      GST_PAD_STREAM_UNLOCK (pad);
    GST_CAT_INFO_OBJECT (GST_CAT_EVENT, pad,
        "Received event on flushing pad. Discarding");
    gst_event_unref (event);
    return FALSE;
  }
dropping:
  {
    GST_DEBUG_OBJECT (pad, "Dropping event after FALSE probe return");
    gst_event_unref (event);
    return FALSE;
  }
update_failed:
  {
    GST_OBJECT_UNLOCK (pad);
    if (need_unlock)
      GST_PAD_STREAM_UNLOCK (pad);
    GST_CAT_INFO_OBJECT (GST_CAT_EVENT, pad, "Update events failed");
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
 * Returns: (transfer none): a #gpointer to the private data.
 */
gpointer
gst_pad_get_element_private (GstPad * pad)
{
  return pad->element_private;
}

/**
 * gst_pad_get_sticky_event:
 * @pad: the #GstPad to get the event from.
 * @event_type: the #GstEventType that should be retrieved.
 *
 * Returns a new reference of the sticky event of type @event_type
 * from the event.
 *
 * Returns: (transfer full): a #GstEvent of type @event_type. Unref after usage.
 */
GstEvent *
gst_pad_get_sticky_event (GstPad * pad, GstEventType event_type)
{
  GstEvent *event = NULL;
  guint idx;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail ((event_type & GST_EVENT_TYPE_STICKY) != 0, NULL);

  idx = GST_EVENT_STICKY_IDX_TYPE (event_type);

  GST_OBJECT_LOCK (pad);
  if ((event = pad->priv->events[idx].event)) {
    gst_event_ref (event);
  }
  GST_OBJECT_UNLOCK (pad);

  return event;
}

/**
 * gst_pad_sticky_events_foreach:
 * @pad: the #GstPad that should be used for iteration.
 * @foreach_func: (scope call): the #GstPadStickyEventsForeachFunction that should be called for every event.
 * @user_data: (closure): the optional user data.
 *
 * Iterates all active sticky events on @pad and calls @foreach_func for every
 * event. If @foreach_func returns something else than GST_FLOW_OK the iteration
 * is immediately stopped.
 *
 * Returns: GST_FLOW_OK if iteration was successful
 */
GstFlowReturn
gst_pad_sticky_events_foreach (GstPad * pad,
    GstPadStickyEventsForeachFunction foreach_func, gpointer user_data)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;
  GstEvent *event;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (foreach_func != NULL, GST_FLOW_ERROR);

  GST_OBJECT_LOCK (pad);

restart:
  for (i = 0; i < GST_EVENT_MAX_STICKY; i++) {
    gboolean res;
    PadEvent *ev;

    ev = &pad->priv->events[i];

    /* skip without active event */
    if ((event = ev->event) == NULL)
      continue;

    gst_event_ref (event);
    GST_OBJECT_UNLOCK (pad);

    res = foreach_func (pad, event, user_data);

    GST_OBJECT_LOCK (pad);
    gst_event_unref (event);

    if (res != GST_FLOW_OK) {
      ret = res;
      break;
    }

    /* things could have changed while we release the lock, check if we still
     * are handling the same event, if we don't something changed and we have
     * to try again. FIXME. we need a cookie here. */
    if (event != ev->event) {
      GST_DEBUG_OBJECT (pad, "events changed, restarting");
      goto restart;
    }
  }
  GST_OBJECT_UNLOCK (pad);

  return ret;
}

static void
do_stream_status (GstPad * pad, GstStreamStatusType type,
    GThread * thread, GstTask * task)
{
  GstElement *parent;

  GST_DEBUG_OBJECT (pad, "doing stream-status %d", type);

  if ((parent = GST_ELEMENT_CAST (gst_pad_get_parent (pad)))) {
    if (GST_IS_ELEMENT (parent)) {
      GstMessage *message;
      GValue value = { 0 };

      if (type == GST_STREAM_STATUS_TYPE_ENTER) {
        gchar *tname, *ename, *pname;

        /* create a good task name */
        ename = gst_element_get_name (parent);
        pname = gst_pad_get_name (pad);
        tname = g_strdup_printf ("%s:%s", ename, pname);
        g_free (ename);
        g_free (pname);

        gst_object_set_name (GST_OBJECT_CAST (task), tname);
        g_free (tname);
      }

      message = gst_message_new_stream_status (GST_OBJECT_CAST (pad),
          type, parent);

      g_value_init (&value, GST_TYPE_TASK);
      g_value_set_object (&value, task);
      gst_message_set_stream_status_object (message, &value);
      g_value_unset (&value);

      GST_DEBUG_OBJECT (pad, "posting stream-status %d", type);
      gst_element_post_message (parent, message);
    }
    gst_object_unref (parent);
  }
}

static void
pad_enter_thread (GstTask * task, GThread * thread, gpointer user_data)
{
  do_stream_status (GST_PAD_CAST (user_data), GST_STREAM_STATUS_TYPE_ENTER,
      thread, task);
}

static void
pad_leave_thread (GstTask * task, GThread * thread, gpointer user_data)
{
  do_stream_status (GST_PAD_CAST (user_data), GST_STREAM_STATUS_TYPE_LEAVE,
      thread, task);
}

static GstTaskThreadCallbacks thr_callbacks = {
  pad_enter_thread,
  pad_leave_thread,
};

/**
 * gst_pad_start_task:
 * @pad: the #GstPad to start the task of
 * @func: the task function to call
 * @data: data passed to the task function
 *
 * Starts a task that repeatedly calls @func with @data. This function
 * is mostly used in pad activation functions to start the dataflow.
 * The #GST_PAD_STREAM_LOCK of @pad will automatically be acquired
 * before @func is called.
 *
 * Returns: a %TRUE if the task could be started.
 */
gboolean
gst_pad_start_task (GstPad * pad, GstTaskFunction func, gpointer data)
{
  GstTask *task;
  gboolean res;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  GST_DEBUG_OBJECT (pad, "start task");

  GST_OBJECT_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL) {
    task = gst_task_create (func, data);
    gst_task_set_lock (task, GST_PAD_GET_STREAM_LOCK (pad));
    gst_task_set_thread_callbacks (task, &thr_callbacks, pad, NULL);
    GST_DEBUG_OBJECT (pad, "created task");
    GST_PAD_TASK (pad) = task;
    gst_object_ref (task);
    /* release lock to post the message */
    GST_OBJECT_UNLOCK (pad);

    do_stream_status (pad, GST_STREAM_STATUS_TYPE_CREATE, NULL, task);

    gst_object_unref (task);

    GST_OBJECT_LOCK (pad);
    /* nobody else is supposed to have changed the pad now */
    if (GST_PAD_TASK (pad) != task)
      goto concurrent_stop;
  }
  res = gst_task_set_state (task, GST_TASK_STARTED);
  GST_OBJECT_UNLOCK (pad);

  return res;

  /* ERRORS */
concurrent_stop:
  {
    GST_OBJECT_UNLOCK (pad);
    return TRUE;
  }
}

/**
 * gst_pad_pause_task:
 * @pad: the #GstPad to pause the task of
 *
 * Pause the task of @pad. This function will also wait until the
 * function executed by the task is finished if this function is not
 * called from the task function.
 *
 * Returns: a TRUE if the task could be paused or FALSE when the pad
 * has no task.
 */
gboolean
gst_pad_pause_task (GstPad * pad)
{
  GstTask *task;
  gboolean res;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_DEBUG_OBJECT (pad, "pause task");

  GST_OBJECT_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL)
    goto no_task;
  res = gst_task_set_state (task, GST_TASK_PAUSED);
  GST_OBJECT_UNLOCK (pad);

  /* wait for task function to finish, this lock is recursive so it does nothing
   * when the pause is called from the task itself */
  GST_PAD_STREAM_LOCK (pad);
  GST_PAD_STREAM_UNLOCK (pad);

  return res;

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
 * the task. Use gst_task_pause() instead.
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
  gboolean res;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_DEBUG_OBJECT (pad, "stop task");

  GST_OBJECT_LOCK (pad);
  task = GST_PAD_TASK (pad);
  if (task == NULL)
    goto no_task;
  GST_PAD_TASK (pad) = NULL;
  res = gst_task_set_state (task, GST_TASK_STOPPED);
  GST_OBJECT_UNLOCK (pad);

  GST_PAD_STREAM_LOCK (pad);
  GST_PAD_STREAM_UNLOCK (pad);

  if (!gst_task_join (task))
    goto join_failed;

  gst_object_unref (task);

  return res;

no_task:
  {
    GST_DEBUG_OBJECT (pad, "no task");
    GST_OBJECT_UNLOCK (pad);

    GST_PAD_STREAM_LOCK (pad);
    GST_PAD_STREAM_UNLOCK (pad);

    /* this is not an error */
    return TRUE;
  }
join_failed:
  {
    /* this is bad, possibly the application tried to join the task from
     * the task's thread. We install the task again so that it will be stopped
     * again from the right thread next time hopefully. */
    GST_OBJECT_LOCK (pad);
    GST_DEBUG_OBJECT (pad, "join failed");
    /* we can only install this task if there was no other task */
    if (GST_PAD_TASK (pad) == NULL)
      GST_PAD_TASK (pad) = task;
    GST_OBJECT_UNLOCK (pad);

    return FALSE;
  }
}
