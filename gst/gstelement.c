/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004 Wim Taymans <wim@fluendo.com>
 *
 * gstelement.c: The base element, all elements derive from this
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
#include <glib.h>
#include <stdarg.h>
#include <gobject/gvaluecollector.h>

#include "gstelement.h"
#include "gstbin.h"
#include "gstmarshal.h"
#include "gsterror.h"
#include "gstscheduler.h"
#include "gstevent.h"
#include "gstutils.h"
#include "gstinfo.h"
#include "gst-i18n-lib.h"

/* Element signals and args */
enum
{
  STATE_CHANGE,
  NEW_PAD,
  PAD_REMOVED,
  ERROR,
  EOS,
  FOUND_TAG,
  NO_MORE_PADS,
  /* add more above */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

extern void __gst_element_details_clear (GstElementDetails * dp);
extern void __gst_element_details_copy (GstElementDetails * dest,
    const GstElementDetails * src);

static void gst_element_class_init (GstElementClass * klass);
static void gst_element_init (GstElement * element);
static void gst_element_base_class_init (gpointer g_class);
static void gst_element_base_class_finalize (gpointer g_class);

static void gst_element_dispose (GObject * object);
static void gst_element_finalize (GObject * object);

static GstElementStateReturn gst_element_change_state (GstElement * element);
static void gst_element_error_func (GstElement * element, GstElement * source,
    GError * error, gchar * debug);
static void gst_element_found_tag_func (GstElement * element,
    GstElement * source, const GstTagList * tag_list);
static GstElementStateReturn gst_element_set_state_func (GstElement * element,
    GstElementState state);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_element_save_thyself (GstObject * object,
    xmlNodePtr parent);
static void gst_element_restore_thyself (GstObject * parent, xmlNodePtr self);
#endif

GType _gst_element_type = 0;

static GstObjectClass *parent_class = NULL;
static guint gst_element_signals[LAST_SIGNAL] = { 0 };

GType
gst_element_get_type (void)
{
  if (!_gst_element_type) {
    static const GTypeInfo element_info = {
      sizeof (GstElementClass),
      gst_element_base_class_init,
      gst_element_base_class_finalize,
      (GClassInitFunc) gst_element_class_init,
      NULL,
      NULL,
      sizeof (GstElement),
      0,
      (GInstanceInitFunc) gst_element_init,
      NULL
    };

    _gst_element_type = g_type_register_static (GST_TYPE_OBJECT, "GstElement",
        &element_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_element_type;
}

static void
gst_element_class_init (GstElementClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  /**
   * GstElement::state-change:
   * @gstelement: the object which received the signal
   * @int:
   * @int:
   *
   * the #GstElementState of the element has been changed
   */
  gst_element_signals[STATE_CHANGE] =
      g_signal_new ("state-change", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstElementClass, state_change), NULL,
      NULL, gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
  /**
   * GstElement::new-pad:
   * @gstelement: the object which received the signal
   * @object:
   *
   * a new #GstPad has been added to the element
   */
  gst_element_signals[NEW_PAD] =
      g_signal_new ("new-pad", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, new_pad), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
  /**
   * GstElement::pad-removed:
   * @gstelement: the object which received the signal
   * @object:
   *
   * a #GstPad has been removed from the element
   */
  gst_element_signals[PAD_REMOVED] =
      g_signal_new ("pad-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, pad_removed), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
  /**
   * GstElement::error:
   * @gstelement: the object which received the signal
   * @element:
   * @error:
   * @message:
   *
   * a #GstError has occured during data processing
   */
  gst_element_signals[ERROR] =
      g_signal_new ("error", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, error), NULL, NULL,
      gst_marshal_VOID__OBJECT_BOXED_STRING, G_TYPE_NONE, 3, GST_TYPE_ELEMENT,
      GST_TYPE_G_ERROR, G_TYPE_STRING);
  /**
   * GstElement::eos:
   * @gstelement: the object which received the signal
   *
   * the end of the stream has been reached
   */
  gst_element_signals[EOS] =
      g_signal_new ("eos", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, eos), NULL, NULL,
      gst_marshal_VOID__VOID, G_TYPE_NONE, 0);
  /**
   * GstElement::found-tag:
   * @gstelement: the object which received the signal
   * @element:
   * @tags:
   *
   * tags for the incomming stream have been received
   */
  gst_element_signals[FOUND_TAG] =
      g_signal_new ("found-tag", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, found_tag), NULL, NULL,
      gst_marshal_VOID__OBJECT_BOXED, G_TYPE_NONE, 2, GST_TYPE_ELEMENT,
      GST_TYPE_TAG_LIST);
  /**
   * GstElement::no-more-pads:
   * @gstelement: the object which received the signal
   *
   * ?
   */
  gst_element_signals[NO_MORE_PADS] =
      g_signal_new ("no-more-pads", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstElementClass, no_more_pads), NULL,
      NULL, gst_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_element_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_element_finalize);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_element_save_thyself);
  gstobject_class->restore_thyself =
      GST_DEBUG_FUNCPTR (gst_element_restore_thyself);
#endif

  klass->change_state = GST_DEBUG_FUNCPTR (gst_element_change_state);
  klass->error = GST_DEBUG_FUNCPTR (gst_element_error_func);
  klass->found_tag = GST_DEBUG_FUNCPTR (gst_element_found_tag_func);
  klass->numpadtemplates = 0;
  klass->set_state = GST_DEBUG_FUNCPTR (gst_element_set_state_func);

  klass->elementfactory = NULL;
}

static void
gst_element_base_class_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  memset (&element_class->details, 0, sizeof (GstElementDetails));
  element_class->padtemplates = NULL;
}

static void
gst_element_base_class_finalize (gpointer g_class)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (g_class);

  g_list_foreach (klass->padtemplates, (GFunc) gst_object_unref, NULL);
  g_list_free (klass->padtemplates);
  __gst_element_details_clear (&klass->details);
}

static void
gst_element_init (GstElement * element)
{
  element->current_state = GST_STATE_NULL;
  element->pending_state = GST_STATE_VOID_PENDING;
  element->numpads = 0;
  element->numsrcpads = 0;
  element->numsinkpads = 0;
  element->pads_cookie = 0;
  element->pads = NULL;
  element->srcpads = NULL;
  element->sinkpads = NULL;
  element->loopfunc = NULL;
  element->sched = NULL;
  element->clock = NULL;
  element->sched_private = NULL;
  element->state_lock = g_mutex_new ();
  element->state_cond = g_cond_new ();
}

/**
 * gst_element_default_error:
 * @object: a #GObject that signalled the error.
 * @orig: the #GstObject that initiated the error.
 * @error: the GError.
 * @debug: an additional debug information string, or NULL.
 *
 * A default error signal callback to attach to an element.
 * The user data passed to the g_signal_connect is ignored.
 *
 * The default handler will simply print the error string using g_print.
 *
 * MT safe.
 */
void
gst_element_default_error (GObject * object, GstObject * source, GError * error,
    gchar * debug)
{
  gchar *name = gst_object_get_path_string (source);

  g_print (_("ERROR: from element %s: %s\n"), name, error->message);
  if (debug)
    g_print (_("Additional debug info:\n%s\n"), debug);

  g_free (name);
}

/**
 * gst_element_release_request_pad:
 * @element: a #GstElement to release the request pad of.
 * @pad: the #GstPad to release.
 *
 * Makes the element free the previously requested pad as obtained
 * with gst_element_get_request_pad().
 *
 * MT safe.
 */
void
gst_element_release_request_pad (GstElement * element, GstPad * pad)
{
  GstElementClass *oclass;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (GST_IS_PAD (pad));

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->release_pad)
    (oclass->release_pad) (element, pad);
  else
    gst_element_remove_pad (element, pad);
}

/**
 * gst_element_requires_clock:
 * @element: a #GstElement to query
 *
 * Query if the element requires a clock.
 *
 * Returns: TRUE if the element requires a clock
 *
 * MT safe.
 */
gboolean
gst_element_requires_clock (GstElement * element)
{
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), result);

  result = (GST_ELEMENT_GET_CLASS (element)->set_clock != NULL);

  return result;
}

/**
 * gst_element_provides_clock:
 * @element: a #GstElement to query
 *
 * Query if the element provides a clock.
 *
 * Returns: TRUE if the element provides a clock
 *
 * MT safe.
 */
gboolean
gst_element_provides_clock (GstElement * element)
{
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  result = (GST_ELEMENT_GET_CLASS (element)->get_clock != NULL);

  return result;
}

/**
 * gst_element_set_clock:
 * @element: a #GstElement to set the clock for.
 * @clock: the #GstClock to set for the element.
 *
 * Sets the clock for the element. This function increases the
 * refcount on the clock. Any previously set clock on the object
 * is unreffed.
 *
 * MT safe.
 */
void
gst_element_set_clock (GstElement * element, GstClock * clock)
{
  GstElementClass *oclass;

  g_return_if_fail (GST_IS_ELEMENT (element));

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->set_clock)
    oclass->set_clock (element, clock);

  GST_LOCK (element);
  gst_object_replace ((GstObject **) & element->clock, (GstObject *) clock);
  GST_UNLOCK (element);
}

/**
 * gst_element_get_clock:
 * @element: a #GstElement to get the clock of.
 *
 * Gets the clock of the element. If the element provides a clock,
 * this function will return this clock. For elements that do not
 * provide a clock, this function returns NULL.
 *
 * Returns: the #GstClock of the element. unref after usage.
 *
 * MT safe.
 */
GstClock *
gst_element_get_clock (GstElement * element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_clock)
    return oclass->get_clock (element);

  return NULL;
}

/**
 * gst_element_clock_wait:
 * @element: a #GstElement.
 * @id: the #GstClock to use.
 * @jitter: the difference between requested time and actual time.
 *
 * Waits for a specific time on the clock.
 *
 * Returns: the #GstClockReturn result of the wait operation.
 */
GstClockReturn
gst_element_clock_wait (GstElement * element, GstClockID id,
    GstClockTimeDiff * jitter)
{
  GstClockReturn res;

  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_CLOCK_ERROR);

  if (GST_ELEMENT_SCHEDULER (element)) {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "waiting on scheduler clock with id %d");
    res =
        gst_scheduler_clock_wait (GST_ELEMENT_SCHEDULER (element), element, id,
        jitter);
  } else {
    GST_CAT_DEBUG (GST_CAT_CLOCK, "no scheduler, returning GST_CLOCK_OK");
    res = GST_CLOCK_OK;
  }

  return res;
}

#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT GST_CAT_CLOCK
/**
 * gst_element_get_time:
 * @element: element to query
 *
 * Query the element's time. FIXME: The element must use
 *
 * Returns: the current stream time in #GST_STATE_PLAYING,
 *          the element base time in #GST_STATE_PAUSED,
 *          or #GST_CLOCK_TIME_NONE otherwise.
 */
/* FIXME: this should always return time on the same scale.  Now it returns
 * the (absolute) base_time in PAUSED and the (current running) time in
 * PLAYING.
 * Solution: have a get_base_time and make the element subtract if it needs
 * to.  In PAUSED return the same as PLAYING, ie. the current timestamp where
 * the element is at according to the provided clock.
 */
GstClockTime
gst_element_get_time (GstElement * element)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_CLOCK_TIME_NONE);

  if (element->clock == NULL) {
    GST_WARNING_OBJECT (element, "element queries time but has no clock");
    return GST_CLOCK_TIME_NONE;
  }
  switch (element->current_state) {
    case GST_STATE_NULL:
    case GST_STATE_READY:
      return GST_CLOCK_TIME_NONE;
    case GST_STATE_PAUSED:
      return element->base_time;
    case GST_STATE_PLAYING:
      return gst_clock_get_time (element->clock) - element->base_time;
    default:
      g_assert_not_reached ();
      return GST_CLOCK_TIME_NONE;
  }
}

/**
 * gst_element_wait:
 * @element: element that should wait
 * @timestamp: what timestamp to wait on
 *
 * Waits until the given relative time stamp for the element has arrived.
 * When this function returns successfully, the relative time point specified
 * in the timestamp has passed for this element.
 * <note>This function can only be called on elements in
 * #GST_STATE_PLAYING</note>
 *
 * Returns: TRUE on success.
 */
gboolean
gst_element_wait (GstElement * element, GstClockTime timestamp)
{
  GstClockID id;
  GstClockReturn ret;
  GstClockTime time;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_CLOCK (element->clock), FALSE);
  g_return_val_if_fail (element->current_state == GST_STATE_PLAYING, FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);

  /* shortcut when we're already late... */
  time = gst_element_get_time (element);
  GST_CAT_LOG_OBJECT (GST_CAT_CLOCK, element, "element time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time));
  if (time >= timestamp) {
    GST_CAT_INFO_OBJECT (GST_CAT_CLOCK, element,
        "called gst_element_wait (% " GST_TIME_FORMAT ") and was late (%"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (gst_element_get_time (element)));
    return TRUE;
  }

  id = gst_clock_new_single_shot_id (element->clock,
      element->base_time + timestamp);
  ret = gst_element_clock_wait (element, id, NULL);
  gst_clock_id_unref (id);

  return ret == GST_CLOCK_OK;
}

/**
 * gst_element_set_time:
 * @element: element to set time on
 * @time: time to set
 *
 * Sets the current time of the element. This function can be used when handling
 * discont events. You can only call this function on an element with a clock in
 * #GST_STATE_PAUSED or #GST_STATE_PLAYING. You might want to have a look at
 * gst_element_adjust_time(), if you want to adjust by a difference as that is
 * more accurate.
 */
void
gst_element_set_time (GstElement * element, GstClockTime time)
{
  gst_element_set_time_delay (element, time, 0);
}

/**
 * gst_element_set_time_delay:
 * @element: element to set time on
 * @time: time to set
 * @delay: a delay to discount from the given time
 *
 * Sets the current time of the element to time - delay. This function can be
 * used when handling discont events in elements writing to an external buffer,
 * i. e., an audio sink that writes to a sound card that buffers the sound
 * before playing it. The delay should be the current buffering delay.
 *
 * You can only call this function on an element with a clock in
 * #GST_STATE_PAUSED or #GST_STATE_PLAYING. You might want to have a look at
 * gst_element_adjust_time(), if you want to adjust by a difference as that is
 * more accurate.
 */
void
gst_element_set_time_delay (GstElement * element, GstClockTime time,
    GstClockTime delay)
{
  GstClockTime event_time;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (GST_IS_CLOCK (element->clock));
  g_return_if_fail (element->current_state >= GST_STATE_PAUSED);
  g_return_if_fail (time >= delay);

  switch (element->current_state) {
    case GST_STATE_PAUSED:
      element->base_time = time - delay;
      break;
    case GST_STATE_PLAYING:
      event_time = GST_CLOCK_TIME_NONE;
      //gst_clock_get_event_time_delay (element->clock, delay);
      GST_CAT_LOG_OBJECT (GST_CAT_CLOCK, element,
          "clock time %" GST_TIME_FORMAT ": setting element time to %"
          GST_TIME_FORMAT, GST_TIME_ARGS (event_time), GST_TIME_ARGS (time));
      element->base_time = event_time - time;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

/**
 * gst_element_adjust_time:
 * @element: element to adjust time on
 * @diff: difference to adjust
 *
 * Adjusts the current time of the element by the specified difference. This
 * function can be used when handling discont events. You can only call this
 * function on an element with a clock in #GST_STATE_PAUSED or
 * #GST_STATE_PLAYING. It is more accurate than gst_element_set_time().
 */
void
gst_element_adjust_time (GstElement * element, GstClockTimeDiff diff)
{
  GstClockTime time;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (GST_IS_CLOCK (element->clock));
  g_return_if_fail (element->current_state >= GST_STATE_PAUSED);

  switch (element->current_state) {
    case GST_STATE_PAUSED:
      if (diff < 0 && element->base_time < abs (diff)) {
        g_warning ("attempted to set the current time of element %s below 0",
            GST_OBJECT_NAME (element));
        element->base_time = 0;
      } else {
        element->base_time += diff;
      }
      break;
    case GST_STATE_PLAYING:
      time = gst_clock_get_time (element->clock);
      if (time < element->base_time - diff) {
        g_warning ("attempted to set the current time of element %s below 0",
            GST_OBJECT_NAME (element));
        element->base_time = time;
      } else {
        element->base_time -= diff;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

#undef GST_CAT_DEFAULT

#ifndef GST_DISABLE_INDEX
/**
 * gst_element_is_indexable:
 * @element: a #GstElement.
 *
 * Queries if the element can be indexed.
 *
 * Returns: TRUE if the element can be indexed.
 *
 * MT safe.
 */
gboolean
gst_element_is_indexable (GstElement * element)
{
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), result);

  result = (GST_ELEMENT_GET_CLASS (element)->set_index != NULL);

  return result;
}

/**
 * gst_element_set_index:
 * @element: a #GstElement.
 * @index: a #GstIndex.
 *
 * Set the specified GstIndex on the element.
 *
 * MT safe.
 */
void
gst_element_set_index (GstElement * element, GstIndex * index)
{
  GstElementClass *oclass;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (GST_IS_INDEX (index));

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->set_index)
    oclass->set_index (element, index);
}

/**
 * gst_element_get_index:
 * @element: a #GstElement.
 *
 * Gets the index from the element.
 *
 * Returns: a #GstIndex or NULL when no index was set on the
 * element. unref after usage.
 *
 * MT safe.
 */
GstIndex *
gst_element_get_index (GstElement * element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_index)
    return oclass->get_index (element);

  return NULL;
}
#endif

/**
 * gst_element_release_locks:
 * @element: a #GstElement to release all locks on.
 *
 * Instruct the element to release all the locks it is holding, such as
 * blocking reads, waiting for the clock, ...
 *
 * Returns: TRUE if the locks could be released.
 */
gboolean
gst_element_release_locks (GstElement * element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->release_locks)
    return oclass->release_locks (element);

  return TRUE;
}

/**
 * gst_element_add_pad:
 * @element: a #GstElement to add the pad to.
 * @pad: the #GstPad to add to the element.
 *
 * Adds a pad (link point) to @element. @pad's parent will be set to @element;
 * see gst_object_set_parent() for refcounting information.
 *
 * Pads are automatically activated when the element is in state PLAYING.
 *
 * The pad and the element should be unlocked when calling this function.
 *
 * Returns: TRUE if the pad could be added. This function can fail when
 * passing bad arguments or when a pad with the same name already existed.
 *
 * MT safe.
 */
gboolean
gst_element_add_pad (GstElement * element, GstPad * pad)
{
  gchar *pad_name;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* locking pad to look at the name */
  GST_LOCK (pad);
  pad_name = g_strdup (GST_PAD_NAME (pad));
  GST_CAT_INFO_OBJECT (GST_CAT_ELEMENT_PADS, element, "adding pad '%s'",
      GST_STR_NULL (pad_name));
  GST_UNLOCK (pad);

  /* then check to see if there's already a pad by that name here */
  GST_LOCK (element);
  if (G_UNLIKELY (!gst_object_check_uniqueness (element->pads, pad_name)))
    goto name_exists;

  /* try to set the pad's parent */
  if (G_UNLIKELY (!gst_object_set_parent (GST_OBJECT_CAST (pad),
              GST_OBJECT_CAST (element))))
    goto had_parent;

  g_free (pad_name);

  /* add it to the list */
  switch (gst_pad_get_direction (pad)) {
    case GST_PAD_SRC:
      element->srcpads = g_list_prepend (element->srcpads, pad);
      element->numsrcpads++;
      break;
    case GST_PAD_SINK:
      element->sinkpads = g_list_prepend (element->sinkpads, pad);
      element->numsinkpads++;
      break;
    default:
      /* can happen for ghost pads */
      break;
  }
  element->pads = g_list_prepend (element->pads, pad);
  element->numpads++;
  element->pads_cookie++;
  GST_UNLOCK (element);

  /* emit the NEW_PAD signal */
  g_signal_emit (G_OBJECT (element), gst_element_signals[NEW_PAD], 0, pad);

  return TRUE;

  /* ERROR cases */
name_exists:
  {
    g_critical ("Padname %s is not unique in element %s, not adding",
        pad_name, GST_ELEMENT_NAME (element));
    GST_UNLOCK (element);
    g_free (pad_name);
    return FALSE;
  }
had_parent:
  {
    g_critical
        ("Pad %s already has parent when trying to add to element %s",
        pad_name, GST_ELEMENT_NAME (element));
    GST_UNLOCK (element);
    g_free (pad_name);
    return FALSE;
  }
}

/**
 * gst_element_add_ghost_pad:
 * @element: a #GstElement to add the ghost pad to.
 * @pad: the #GstPad from which the new ghost pad will be created.
 * @name: the name of the new ghost pad, or NULL to assign a unique name
 * automatically.
 *
 * Creates a ghost pad from @pad, and adds it to @element via
 * gst_element_add_pad().
 *
 * Returns: the added ghost #GstPad, or NULL on error.
 *
 * MT safe.
 */
GstPad *
gst_element_add_ghost_pad (GstElement * element, GstPad * pad,
    const gchar * name)
{
  GstPad *ghostpad;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  ghostpad = gst_ghost_pad_new (name, pad);

  if (!gst_element_add_pad (element, ghostpad)) {
    gst_object_unref (GST_OBJECT (ghostpad));
    ghostpad = NULL;
  }

  return ghostpad;
}

/**
 * gst_element_remove_pad:
 * @element: a #GstElement to remove pad from.
 * @pad: the #GstPad to remove from the element.
 *
 * Removes @pad from @element. @pad will be destroyed if it has not been
 * referenced elsewhere.
 *
 * Returns: TRUE if the pad could be removed. Can return FALSE if the
 * pad is not belonging to the provided element or when wrong parameters
 * are passed to this function.
 *
 * MT safe.
 */
gboolean
gst_element_remove_pad (GstElement * element, GstPad * pad)
{
  gchar *pad_name;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* locking pad to look at the name and parent */
  GST_LOCK (pad);
  pad_name = g_strdup (GST_PAD_NAME (pad));

  GST_CAT_INFO_OBJECT (GST_CAT_ELEMENT_PADS, element, "removing pad '%s'",
      GST_STR_NULL (pad_name));

  if (G_UNLIKELY (GST_PAD_PARENT (pad) != element))
    goto not_our_pad;
  GST_UNLOCK (pad);

  g_free (pad_name);

  /* FIXME, is this redundant with pad disposal? */
  if (GST_IS_REAL_PAD (pad)) {
    GstPad *peer = gst_pad_get_peer (pad);

    /* unlink */
    if (peer != NULL) {
      /* window for MT unsafeness, someone else could unlink here
       * and then we call unlink with wrong pads. The unlink
       * function would catch this and safely return failed. */
      if (GST_PAD_IS_SRC (pad))
        gst_pad_unlink (pad, GST_PAD_CAST (peer));
      else
        gst_pad_unlink (GST_PAD_CAST (peer), pad);

      gst_object_unref (GST_OBJECT (peer));
    }
  } else if (GST_IS_GHOST_PAD (pad)) {
    g_object_set (pad, "real-pad", NULL, NULL);
  }

  GST_LOCK (element);
  /* remove it from the list */
  switch (gst_pad_get_direction (pad)) {
    case GST_PAD_SRC:
      element->srcpads = g_list_remove (element->srcpads, pad);
      element->numsrcpads--;
      break;
    case GST_PAD_SINK:
      element->sinkpads = g_list_remove (element->sinkpads, pad);
      element->numsinkpads--;
      break;
    default:
      /* can happen for ghost pads */
      break;
  }
  element->pads = g_list_remove (element->pads, pad);
  element->numpads--;
  element->pads_cookie++;
  GST_UNLOCK (element);

  g_signal_emit (G_OBJECT (element), gst_element_signals[PAD_REMOVED], 0, pad);

  gst_object_unparent (GST_OBJECT (pad));

  return TRUE;

not_our_pad:
  {
    /* FIXME, locking order? */
    GST_LOCK (element);
    g_critical ("Padname %s:%s does not belong to element %s when removing",
        GST_ELEMENT_NAME (GST_PAD_PARENT (pad)), GST_PAD_NAME (pad),
        GST_ELEMENT_NAME (element));
    GST_UNLOCK (element);
    GST_UNLOCK (pad);
    g_free (pad_name);
    return FALSE;
  }
}

/**
 * gst_element_no_more_pads:
 * @element: a #GstElement
 *
 * Use this function to signal that the element does not expect any more pads
 * to show up in the current pipeline. This function should be called whenever
 * pads have been added by the element itself. Elements with GST_PAD_SOMETIMES
 * pad templates use this in combination with autopluggers to figure out that
 * the element is done initializing its pads.
 *
 * MT safe.
 */
void
gst_element_no_more_pads (GstElement * element)
{
  g_return_if_fail (GST_IS_ELEMENT (element));

  g_signal_emit (element, gst_element_signals[NO_MORE_PADS], 0);
}

static gint
pad_compare_name (GstPad * pad1, const gchar * name)
{
  gint result;

  GST_LOCK (pad1);
  result = strcmp (GST_PAD_NAME (pad1), name);
  GST_UNLOCK (pad1);

  return result;
}

/**
 * gst_element_get_static_pad:
 * @element: a #GstElement to find a static pad of.
 * @name: the name of the static #GstPad to retrieve.
 *
 * Retrieves a pad from @element by name. This version only retrieves
 * already-existing (i.e. 'static') pads.
 *
 * Returns: the requested #GstPad if found, otherwise NULL. unref after
 * usage.
 *
 * MT safe.
 */
GstPad *
gst_element_get_static_pad (GstElement * element, const gchar * name)
{
  GList *find;
  GstPad *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  GST_LOCK (element);
  find =
      g_list_find_custom (element->pads, name, (GCompareFunc) pad_compare_name);
  if (find) {
    result = GST_PAD_CAST (find->data);
    gst_object_ref (GST_OBJECT_CAST (result));
  }

  if (result == NULL) {
    GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "no such pad '%s' in element \"%s\"",
        name, GST_ELEMENT_NAME (element));
  } else {
    GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "found pad %s:%s",
        GST_ELEMENT_NAME (element), name);
  }
  GST_UNLOCK (element);

  return result;
}

static GstPad *
gst_element_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *newpad = NULL;
  GstElementClass *oclass;

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->request_new_pad)
    newpad = (oclass->request_new_pad) (element, templ, name);

  return newpad;
}

/**
 * gst_element_get_request_pad:
 * @element: a #GstElement to find a request pad of.
 * @name: the name of the request #GstPad to retrieve.
 *
 * Retrieves a pad from the element by name. This version only retrieves
 * request pads.
 *
 * Returns: requested #GstPad if found, otherwise NULL.
 */
GstPad *
gst_element_get_request_pad (GstElement * element, const gchar * name)
{
  GstPadTemplate *templ = NULL;
  GstPad *pad;
  const gchar *req_name = NULL;
  gboolean templ_found = FALSE;
  GList *list;
  gint n;
  const gchar *data;
  gchar *str, *endptr = NULL;
  GstElementClass *class;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  class = GST_ELEMENT_GET_CLASS (element);

  if (strstr (name, "%")) {
    templ = gst_element_class_get_pad_template (class, name);
    req_name = NULL;
    if (templ)
      templ_found = TRUE;
  } else {
    list = gst_element_class_get_pad_template_list (class);
    while (!templ_found && list) {
      templ = (GstPadTemplate *) list->data;
      if (templ->presence == GST_PAD_REQUEST) {
        /* Because of sanity checks in gst_pad_template_new(), we know that %s
           and %d, occurring at the end of the name_template, are the only
           possibilities. */
        GST_CAT_DEBUG (GST_CAT_PADS, "comparing %s to %s", name,
            templ->name_template);
        if ((str = strchr (templ->name_template, '%'))
            && strncmp (templ->name_template, name,
                str - templ->name_template) == 0
            && strlen (name) > str - templ->name_template) {
          data = name + (str - templ->name_template);
          if (*(str + 1) == 'd') {
            /* it's an int */
            n = (gint) strtol (data, &endptr, 10);
            if (endptr && *endptr == '\0') {
              templ_found = TRUE;
              req_name = name;
              break;
            }
          } else {
            /* it's a string */
            templ_found = TRUE;
            req_name = name;
            break;
          }
        }
      }
      list = list->next;
    }
  }

  if (!templ_found)
    return NULL;

  pad = gst_element_request_pad (element, templ, req_name);

  return pad;
}

/**
 * gst_element_get_pad:
 * @element: a #GstElement.
 * @name: the name of the pad to retrieve.
 *
 * Retrieves a pad from @element by name. Tries gst_element_get_static_pad()
 * first, then gst_element_get_request_pad().
 *
 * Returns: the #GstPad if found, otherwise %NULL. Unref after usage.
 */
GstPad *
gst_element_get_pad (GstElement * element, const gchar * name)
{
  GstPad *pad;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  pad = gst_element_get_static_pad (element, name);
  if (!pad)
    pad = gst_element_get_request_pad (element, name);

  return pad;
}

GstIteratorItem
iterate_pad (GstIterator * it, GstPad * pad)
{
  gst_object_ref (GST_OBJECT_CAST (pad));
  return GST_ITERATOR_ITEM_PASS;
}

/**
 * gst_element_iterate_pads:
 * @element: a #GstElement to iterate pads of.
 *
 * Retrieves an iterattor of @element's pads. 
 *
 * Returns: the #GstIterator of #GstPad. unref each pad after usage.
 *
 * MT safe.
 */
GstIterator *
gst_element_iterate_pads (GstElement * element)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  GST_LOCK (element);
  gst_object_ref (GST_OBJECT (element));
  result = gst_iterator_new_list (GST_GET_LOCK (element),
      &element->pads_cookie,
      &element->pads,
      element,
      (GstIteratorItemFunction) iterate_pad,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_UNLOCK (element);

  return result;
}


/**
 * gst_element_class_add_pad_template:
 * @klass: the #GstElementClass to add the pad template to.
 * @templ: a #GstPadTemplate to add to the element class.
 *
 * Adds a padtemplate to an element class. This is mainly used in the _base_init
 * functions of classes.
 */
void
gst_element_class_add_pad_template (GstElementClass * klass,
    GstPadTemplate * templ)
{
  g_return_if_fail (GST_IS_ELEMENT_CLASS (klass));
  g_return_if_fail (GST_IS_PAD_TEMPLATE (templ));

  /* avoid registering pad templates with the same name */
  g_return_if_fail (gst_element_class_get_pad_template (klass,
          templ->name_template) == NULL);

  klass->padtemplates = g_list_append (klass->padtemplates,
      gst_object_ref (GST_OBJECT (templ)));
  klass->numpadtemplates++;
}

/**
 * gst_element_class_set_details:
 * @klass: class to set details for
 * @details: details to set
 *
 * Sets the detailed information for a #GstElementClass.
 * <note>This function is for use in _base_init functions only.</note>
 */
void
gst_element_class_set_details (GstElementClass * klass,
    const GstElementDetails * details)
{
  g_return_if_fail (GST_IS_ELEMENT_CLASS (klass));
  g_return_if_fail (GST_IS_ELEMENT_DETAILS (details));

  __gst_element_details_copy (&klass->details, details);
}

/**
 * gst_element_class_get_pad_template_list:
 * @element_class: a #GstElementClass to get pad templates of.
 *
 * Retrieves a list of the pad templates associated with @element_class. The
 * list must not be modified by the calling code.
 * <note>If you use this function in the #GInstanceInitFunc of an object class
 * that has subclasses, make sure to pass the g_class parameter of the
 * #GInstanceInitFunc here.</note>
 *
 * Returns: the #GList of padtemplates.
 */
GList *
gst_element_class_get_pad_template_list (GstElementClass * element_class)
{
  g_return_val_if_fail (GST_IS_ELEMENT_CLASS (element_class), NULL);

  return element_class->padtemplates;
}

/**
 * gst_element_class_get_pad_template:
 * @element_class: a #GstElementClass to get the pad template of.
 * @name: the name of the #GstPadTemplate to get.
 *
 * Retrieves a padtemplate from @element_class with the given name.
 * <note>If you use this function in the #GInstanceInitFunc of an object class
 * that has subclasses, make sure to pass the g_class parameter of the
 * #GInstanceInitFunc here.</note>
 *
 * Returns: the #GstPadTemplate with the given name, or NULL if none was found.
 * No unreferencing is necessary.
 */
GstPadTemplate *
gst_element_class_get_pad_template (GstElementClass * element_class,
    const gchar * name)
{
  GList *padlist;

  g_return_val_if_fail (GST_IS_ELEMENT_CLASS (element_class), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  padlist = gst_element_class_get_pad_template_list (element_class);

  while (padlist) {
    GstPadTemplate *padtempl = (GstPadTemplate *) padlist->data;

    if (strcmp (padtempl->name_template, name) == 0)
      return padtempl;

    padlist = g_list_next (padlist);
  }

  return NULL;
}

static void
gst_element_error_func (GstElement * element, GstElement * source,
    GError * error, gchar * debug)
{
  GstObject *parent = GST_OBJECT_PARENT (element);

  /* tell the parent */
  if (parent) {
    gst_object_ref (GST_OBJECT (element));
    gst_object_ref (parent);
    GST_CAT_DEBUG (GST_CAT_ERROR_SYSTEM,
        "forwarding error \"%s\" from %s to %s", error->message,
        GST_ELEMENT_NAME (element), GST_OBJECT_NAME (parent));

    g_signal_emit (G_OBJECT (parent),
        gst_element_signals[ERROR], 0, source, error, debug);
    GST_CAT_DEBUG (GST_CAT_ERROR_SYSTEM, "forwarded error \"%s\" from %s to %s",
        error->message, GST_ELEMENT_NAME (element), GST_OBJECT_NAME (parent));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (parent));
  }
}

static GstPad *
gst_element_get_random_pad (GstElement * element, GstPadDirection dir)
{
  GstPad *result = NULL;
  GList *pads;

  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "getting a random pad");

  switch (dir) {
    case GST_PAD_SRC:
      GST_LOCK (element);
      pads = element->srcpads;
      break;
    case GST_PAD_SINK:
      GST_LOCK (element);
      pads = element->sinkpads;
      break;
    default:
      goto wrong_direction;
  }
  for (; pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD (pads->data);

    GST_LOCK (pad);
    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "checking pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    if (GST_PAD_IS_LINKED (pad)) {
      GST_UNLOCK (pad);
      result = pad;
      break;
    } else {
      GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "pad %s:%s is not linked",
          GST_DEBUG_PAD_NAME (pad));
    }
    GST_UNLOCK (pad);
  }
  if (result)
    gst_object_ref (GST_OBJECT (result));

  GST_UNLOCK (element);

  return result;

  /* ERROR handling */
wrong_direction:
  {
    g_warning ("unknown pad direction");
    return NULL;
  }
}

/**
 * gst_element_get_event_masks:
 * @element: a #GstElement to query
 *
 * Get an array of event masks from the element.
 * If the element doesn't implement an event masks function,
 * the query will be forwarded to a random linked sink pad.
 *
 * Returns: An array of #GstEventMask elements. The array 
 * cannot be modified or freed.
 *
 * MT safe.
 */
const GstEventMask *
gst_element_get_event_masks (GstElement * element)
{
  GstElementClass *oclass;
  const GstEventMask *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_event_masks) {
    result = oclass->get_event_masks (element);
  } else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad) {
      GstPad *peer = gst_pad_get_peer (pad);

      if (peer) {
        result = gst_pad_get_event_masks (peer);
        gst_object_unref (GST_OBJECT (peer));
      }
      gst_object_unref (GST_OBJECT (pad));
    }
  }

  return result;
}

/**
 * gst_element_send_event:
 * @element: a #GstElement to send the event to.
 * @event: the #GstEvent to send to the element.
 *
 * Sends an event to an element. If the element doesn't
 * implement an event handler, the event will be forwarded
 * to a random sink pad. This function takes owership of the
 * provided event so you should _ref it if you want to reuse
 * the event after this call.
 *
 * Returns: TRUE if the event was handled.
 *
 * MT safe.
 */
gboolean
gst_element_send_event (GstElement * element, GstEvent * event)
{
  GstElementClass *oclass;
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->send_event) {
    result = oclass->send_event (element, event);
  } else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad) {
      GstPad *peer = gst_pad_get_peer (pad);

      if (peer) {
        GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
            "sending event to random pad %s:%s", GST_DEBUG_PAD_NAME (pad));

        result = gst_pad_send_event (peer, event);
        gst_object_unref (GST_OBJECT (peer));
      }
      gst_object_unref (GST_OBJECT (pad));
    }
  }
  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "can't send event on element %s",
      GST_ELEMENT_NAME (element));

  return result;
}

/**
 * gst_element_seek:
 * @element: a #GstElement to send the event to.
 * @seek_type: the method to use for seeking.
 * @offset: the offset to seek to.
 *
 * Sends a seek event to an element.
 *
 * Returns: TRUE if the event was handled.
 *
 * MT safe.
 */
gboolean
gst_element_seek (GstElement * element, GstSeekType seek_type, guint64 offset)
{
  GstEvent *event = gst_event_new_seek (seek_type, offset);
  gboolean result;

  result = gst_element_send_event (element, event);

  return result;
}

/**
 * gst_element_get_query_types:
 * @element: a #GstElement to query
 *
 * Get an array of query types from the element.
 * If the element doesn't implement a query types function,
 * the query will be forwarded to a random sink pad.
 *
 * Returns: An array of #GstQueryType elements that should not
 * be freed or modified.
 *
 * MT safe.
 */
const GstQueryType *
gst_element_get_query_types (GstElement * element)
{
  GstElementClass *oclass;
  const GstQueryType *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_query_types) {
    result = oclass->get_query_types (element);
  } else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad) {
      GstPad *peer = gst_pad_get_peer (pad);

      if (peer) {
        result = gst_pad_get_query_types (peer);

        gst_object_unref (GST_OBJECT (peer));
      }
      gst_object_unref (GST_OBJECT (pad));
    }
  }
  return result;
}

/**
 * gst_element_query:
 * @element: a #GstElement to perform the query on.
 * @type: the #GstQueryType.
 * @format: the #GstFormat pointer to hold the format of the result.
 * @value: the pointer to the value of the result.
 *
 * Performs a query on the given element. If the format is set
 * to GST_FORMAT_DEFAULT and this function returns TRUE, the
 * format pointer will hold the default format.
 * For element that don't implement a query handler, this function
 * forwards the query to a random usable sinkpad of this element.
 *
 * Returns: TRUE if the query could be performed.
 *
 * MT safe.
 */
gboolean
gst_element_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstElementClass *oclass;
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->query) {
    result = oclass->query (element, type, format, value);
  } else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SRC);

    if (pad) {
      result = gst_pad_query (pad, type, format, value);

      gst_object_unref (GST_OBJECT (pad));
    } else {
      pad = gst_element_get_random_pad (element, GST_PAD_SINK);
      if (pad) {
        GstPad *peer = gst_pad_get_peer (pad);

        if (peer) {
          result = gst_pad_query (peer, type, format, value);

          gst_object_unref (GST_OBJECT (peer));
        }
        gst_object_unref (GST_OBJECT (pad));
      }
    }
  }
  return result;
}

/**
 * gst_element_get_formats:
 * @element: a #GstElement to query
 *
 * Get an array of formats from the element.
 * If the element doesn't implement a formats function,
 * the query will be forwarded to a random sink pad.
 *
 * Returns: An array of #GstFormat elements.
 *
 * MT safe.
 */
const GstFormat *
gst_element_get_formats (GstElement * element)
{
  GstElementClass *oclass;
  const GstFormat *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_formats) {
    result = oclass->get_formats (element);
  } else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad) {
      GstPad *peer = gst_pad_get_peer (pad);

      if (peer) {
        result = gst_pad_get_formats (peer);

        gst_object_unref (GST_OBJECT (peer));
      }
      gst_object_unref (GST_OBJECT (pad));
    }
  }

  return result;
}

/**
 * gst_element_convert:
 * @element: a #GstElement to invoke the converter on.
 * @src_format: the source #GstFormat.
 * @src_value: the source value.
 * @dest_format: a pointer to the destination #GstFormat.
 * @dest_value: a pointer to the destination value.
 *
 * Invokes a conversion on the element.
 * If the element doesn't implement a convert function,
 * the query will be forwarded to a random sink pad.
 *
 * Returns: TRUE if the conversion could be performed.
 *
 * MT safe.
 */
gboolean
gst_element_convert (GstElement * element,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstElementClass *oclass;
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->convert) {
    result = oclass->convert (element,
        src_format, src_value, dest_format, dest_value);
  } else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad) {
      GstPad *peer = gst_pad_get_peer (pad);

      if (peer) {
        result = gst_pad_convert (peer,
            src_format, src_value, dest_format, dest_value);

        gst_object_unref (GST_OBJECT (peer));
      }
      gst_object_unref (GST_OBJECT (pad));
    }
  }
  return result;
}

/**
 * _gst_element_error_printf:
 * @format: the printf-like format to use, or NULL
 *
 * This function is only used internally by the #gst_element_error macro.
 *
 * Returns: a newly allocated string, or NULL if the format was NULL or ""
 *
 * MT safe.
 */
gchar *
_gst_element_error_printf (const gchar * format, ...)
{
  va_list args;
  gchar *buffer;

  if (format == NULL)
    return NULL;
  if (format[0] == 0)
    return NULL;

  va_start (args, format);
  buffer = g_strdup_vprintf (format, args);
  va_end (args);
  return buffer;
}

/**
 * gst_element_error_full:
 * @element: a #GstElement with the error.
 * @domain: the GStreamer error domain this error belongs to.
 * @code: the error code belonging to the domain
 * @message: an allocated message to be used as a replacement for the default
 *           message connected to code, or NULL
 * @debug: an allocated debug message to be used as a replacement for the
 *         default debugging information, or NULL
 * @file: the source code file where the error was generated
 * @function: the source code function where the error was generated
 * @line: the source code line where the error was generated
 *
 * Signals an error condition on an element.
 * This function is used internally by elements.
 * It results in the "error" signal.
 */
void gst_element_error_full
    (GstElement * element, GQuark domain, gint code, gchar * message,
    gchar * debug, const gchar * file, const gchar * function, gint line)
{
  GError *error = NULL;
  gchar *name;
  gchar *sent_message;
  gchar *sent_debug;

  /* checks */
  g_return_if_fail (GST_IS_ELEMENT (element));

  /* check if we send the given message or the default error message */
  if ((message == NULL) || (message[0] == 0)) {
    /* we got this message from g_strdup_printf (""); */
    g_free (message);
    sent_message = gst_error_get_message (domain, code);
  } else
    sent_message = message;

  if ((debug == NULL) || (debug[0] == 0)) {
    /* we got this debug from g_strdup_printf (""); */
    g_free (debug);
    debug = NULL;
  }

  /* create error message */
  GST_CAT_INFO (GST_CAT_ERROR_SYSTEM, "signaling error in %s: %s",
      GST_ELEMENT_NAME (element), sent_message);
  error = g_error_new_literal (domain, code, sent_message);

  /* if the element was already in error, stop now */
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_IN_ERROR)) {
    GST_CAT_INFO (GST_CAT_ERROR_SYSTEM, "recursive ERROR detected in %s",
        GST_ELEMENT_NAME (element));
    g_free (sent_message);
    if (debug)
      g_free (debug);
    return;
  }

  GST_FLAG_SET (element, GST_ELEMENT_IN_ERROR);

  /* emit the signal, make sure the element stays available */
  gst_object_ref (GST_OBJECT (element));
  name = gst_object_get_path_string (GST_OBJECT (element));
  if (debug)
    sent_debug = g_strdup_printf ("%s(%d): %s: %s:\n%s",
        file, line, function, name, debug ? debug : "");
  else
    sent_debug = NULL;
  g_free (debug);
  g_free (name);
  g_signal_emit (G_OBJECT (element), gst_element_signals[ERROR], 0, element,
      error, sent_debug);
  GST_CAT_INFO (GST_CAT_ERROR_SYSTEM, "signalled error in %s: %s",
      GST_ELEMENT_NAME (element), sent_message);

  /* tell the scheduler */
  if (element->sched) {
    gst_scheduler_error (element->sched, element);
  }

  if (GST_STATE (element) == GST_STATE_PLAYING) {
    GstElementStateReturn ret;

    ret = gst_element_set_state (element, GST_STATE_PAUSED);
    if (ret != GST_STATE_SUCCESS) {
      g_warning ("could not PAUSE element \"%s\" after error, help!",
          GST_ELEMENT_NAME (element));
    }
  }

  GST_FLAG_UNSET (element, GST_ELEMENT_IN_ERROR);

  /* cleanup */
  gst_object_unref (GST_OBJECT (element));
  g_free (sent_message);
  g_free (sent_debug);
  g_error_free (error);
}

/**
 * gst_element_is_locked_state:
 * @element: a #GstElement.
 *
 * Checks if the state of an element is locked.
 * If the state of an element is locked, state changes of the parent don't
 * affect the element.
 * This way you can leave currently unused elements inside bins. Just lock their
 * state before changing the state from #GST_STATE_NULL.
 *
 * Returns: TRUE, if the element's state is locked.
 *
 * MT safe.
 */
gboolean
gst_element_is_locked_state (GstElement * element)
{
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  GST_LOCK (element);
  result = GST_FLAG_IS_SET (element, GST_ELEMENT_LOCKED_STATE);
  GST_UNLOCK (element);

  return result;
}

/**
 * gst_element_set_locked_state:
 * @element: a #GstElement
 * @locked_state: TRUE to lock the element's state
 *
 * Locks the state of an element, so state changes of the parent don't affect
 * this element anymore.
 *
 * Returns: TRUE if the state was changed, FALSE if bad params were given or
 * the element was already in the correct state.
 *
 * MT safe.
 */
gboolean
gst_element_set_locked_state (GstElement * element, gboolean locked_state)
{
  gboolean old;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  GST_LOCK (element);
  old = GST_FLAG_IS_SET (element, GST_ELEMENT_LOCKED_STATE);

  if (G_UNLIKELY (old == locked_state))
    goto was_ok;

  if (locked_state) {
    GST_CAT_DEBUG (GST_CAT_STATES, "locking state of element %s",
        GST_ELEMENT_NAME (element));
    GST_FLAG_SET (element, GST_ELEMENT_LOCKED_STATE);
  } else {
    GST_CAT_DEBUG (GST_CAT_STATES, "unlocking state of element %s",
        GST_ELEMENT_NAME (element));
    GST_FLAG_UNSET (element, GST_ELEMENT_LOCKED_STATE);
  }
  GST_UNLOCK (element);

  return TRUE;

was_ok:
  GST_UNLOCK (element);

  return FALSE;
}

/**
 * gst_element_sync_state_with_parent:
 * @element: a #GstElement.
 *
 * Tries to change the state of the element to the same as its parent.
 * If this function returns FALSE, the state of element is undefined.
 *
 * Returns: TRUE, if the element's state could be synced to the parent's state.
 */
gboolean
gst_element_sync_state_with_parent (GstElement * element)
{
  GstElement *parent;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  parent = GST_ELEMENT (GST_ELEMENT_PARENT (element));
  g_return_val_if_fail (GST_IS_BIN (parent), FALSE);

  GST_CAT_DEBUG (GST_CAT_STATES, "syncing state of element %s (%s) to %s (%s)",
      GST_ELEMENT_NAME (element),
      gst_element_state_get_name (GST_STATE (element)),
      GST_ELEMENT_NAME (parent),
      gst_element_state_get_name (GST_STATE (parent)));

  if (gst_element_set_state (element, GST_STATE (parent)) == GST_STATE_FAILURE) {
    return FALSE;
  }
  return TRUE;
}

/**
 * gst_element_get_state:
 * @element: a #GstElement to get the state of.
 *
 * Gets the state of the element.
 *
 * Returns: the #GstElementState of the element.
 */
GstElementState
gst_element_get_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_VOID_PENDING);

  return GST_STATE (element);
}

/**
 * gst_element_wait_state_change:
 * @element: a #GstElement to wait for a state change on.
 *
 * Waits and blocks until the element changed its state.
 */
void
gst_element_wait_state_change (GstElement * element)
{
  GST_STATE_LOCK (element);
  GST_STATE_WAIT (element);
  GST_STATE_UNLOCK (element);
}

/**
 * gst_element_set_state:
 * @element: a #GstElement to change state of.
 * @state: the element's new #GstElementState.
 *
 * Sets the state of the element. This function will try to set the
 * requested state by going through all the intermediary states and calling
 * the class's state change function for each.
 *
 * Returns: TRUE if the state was successfully set.
 * (using #GstElementStateReturn).
 */
GstElementStateReturn
gst_element_set_state (GstElement * element, GstElementState state)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstElementStateReturn ret;

  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);
  GST_DEBUG_OBJECT (element, "setting state to %s",
      gst_element_state_get_name (state));
  klass = GST_ELEMENT_GET_CLASS (element);
  g_return_val_if_fail (klass->set_state, GST_STATE_FAILURE);

  /* a set_state function is mandatory */
  gst_object_ref (GST_OBJECT (element));
  ret = klass->set_state (element, state);
  gst_object_unref (GST_OBJECT (element));

  return ret;
}

static GstElementStateReturn
gst_element_set_state_func (GstElement * element, GstElementState state)
{
  GstElementClass *oclass;
  GstElementState curpending;
  GstElementStateReturn return_val = GST_STATE_SUCCESS;

  oclass = GST_ELEMENT_GET_CLASS (element);

  /* start with the current state */
  curpending = GST_STATE (element);

  if (state == curpending) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
        "element is already in requested state %s, returning",
        gst_element_state_get_name (state));
    return GST_STATE_SUCCESS;
  }

  /* reentrancy issues with signals in change_state) */
  gst_object_ref (GST_OBJECT (element));
  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "setting state from %s to %s",
      gst_element_state_get_name (curpending),
      gst_element_state_get_name (state));

  /* loop until the final requested state is set */

  while (GST_STATE (element) != state
      && GST_STATE (element) != GST_STATE_VOID_PENDING) {
    /* move the curpending state in the correct direction */
    if (curpending < state)
      curpending <<= 1;
    else
      curpending >>= 1;

    /* set the pending state variable */
    GST_STATE_PENDING (element) = curpending;

    if (curpending != state) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
          "intermediate: setting state from %s to %s",
          gst_element_state_get_name (GST_STATE (element)),
          gst_element_state_get_name (curpending));
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
          "start: setting current state %s again",
          gst_element_state_get_name (GST_STATE (element)));
    }

    /* call the state change function so it can set the state */
    if (oclass->change_state)
      return_val = (oclass->change_state) (element);

    switch (return_val) {
      case GST_STATE_FAILURE:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "have failed change_state return");
        goto exit;
      case GST_STATE_ASYNC:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "element will change state async");
        goto exit;
      case GST_STATE_SUCCESS:
        /* Last thing we do is verify that a successful state change really
         * did change the state... */
        /* if it did not, this is an error - fix the element that does this */
        if (GST_STATE (element) != curpending) {
          g_warning ("element %s claimed state-change success,"
              "but state didn't change to %s. State is %s (%s pending), "
              "fix the element",
              GST_ELEMENT_NAME (element),
              gst_element_state_get_name (curpending),
              gst_element_state_get_name (GST_STATE (element)),
              gst_element_state_get_name (GST_STATE_PENDING (element)));
          return_val = GST_STATE_FAILURE;
          goto exit;
        }
        break;
      default:
        /* somebody added a GST_STATE_ and forgot to do stuff here ! */
        g_assert_not_reached ();
    }
  }

exit:
  gst_object_unref (GST_OBJECT (element));

  return return_val;
}

static gboolean
gst_element_negotiate_pads (GstElement * element)
{
  GList *pads;

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, element, "negotiating pads");

  for (pads = GST_ELEMENT_PADS (element); pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD (pads->data);

    if (!GST_IS_REAL_PAD (pad))
      continue;

    /* if we have a link on this pad and it doesn't have caps
     * allready, try to negotiate */
    if (!gst_pad_is_negotiated (pad)) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, element,
          "perform negotiate for %s:%s", GST_DEBUG_PAD_NAME (pad));
      if (gst_pad_renegotiate (pad) == GST_PAD_LINK_REFUSED)
        return FALSE;
    }
  }

  return TRUE;
}

static void
gst_element_clear_pad_caps (GstElement * element)
{
  GList *pads = GST_ELEMENT_PADS (element);

  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, element, "clearing pad caps");

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    gst_pad_unnegotiate (pad);
    if (GST_IS_REAL_PAD (pad)) {
      gst_caps_replace (&GST_RPAD_EXPLICIT_CAPS (pad), NULL);
    }

    pads = g_list_next (pads);
  }
}

static void
gst_element_pads_activate (GstElement * element, gboolean active)
{
  GList *pads = element->pads;

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    if (!GST_IS_REAL_PAD (pad))
      continue;

    gst_pad_set_active (pad, active);
  }
}

static GstElementStateReturn
gst_element_change_state (GstElement * element)
{
  GstElementState old_state, old_pending;
  GstObject *parent;
  gint old_transition;

  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);

  old_state = GST_STATE (element);
  old_pending = GST_STATE_PENDING (element);
  old_transition = GST_STATE_TRANSITION (element);

  /* if the element already is in the given state, we just return success */
  if (old_pending == GST_STATE_VOID_PENDING ||
      old_state == GST_STATE_PENDING (element)) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
        "element is already in the %s state",
        gst_element_state_get_name (old_state));
    return GST_STATE_SUCCESS;
  }

  /* we need to ref the object because of reentrancy issues with the signal
   * handlers (including those in pads and gst_bin_child_state_change */
  gst_object_ref (GST_OBJECT (element));
  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element,
      "default handler tries setting state from %s to %s (%04x)",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (old_pending), old_transition);

  /* we set the state change early for the negotiation functions */
  GST_STATE (element) = old_pending;
  GST_STATE_PENDING (element) = GST_STATE_VOID_PENDING;

  switch (old_transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      if (element->clock) {
        GstClockTimeDiff time = GST_CLOCK_TIME_NONE;    //gst_clock_get_event_time (element->clock);

        g_assert (time >= element->base_time);
        element->base_time = time - element->base_time;
        GST_CAT_LOG_OBJECT (GST_CAT_CLOCK, element, "setting base time to %"
            G_GINT64_FORMAT, element->base_time);
      }
      gst_element_pads_activate (element, FALSE);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      gst_element_pads_activate (element, TRUE);
      if (element->clock) {
        GstClockTime time = GST_CLOCK_TIME_NONE;        //gst_clock_get_event_time (element->clock);

        element->base_time = time - element->base_time;
        GST_CAT_LOG_OBJECT (GST_CAT_CLOCK, element, "setting base time to %"
            GST_TIME_FORMAT, GST_TIME_ARGS (element->base_time));
      }
      break;
      /* if we are going to paused, we try to negotiate the pads */
    case GST_STATE_READY_TO_PAUSED:
      g_assert (element->base_time == 0);
      if (!gst_element_negotiate_pads (element)) {
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "failed state change, could not negotiate pads");
        goto failure;
      }
      break;
      /* going to the READY state clears all pad caps */
      /* FIXME: Why doesn't this happen on READY => NULL? -- Company */
    case GST_STATE_PAUSED_TO_READY:
      element->base_time = 0;
      gst_element_clear_pad_caps (element);
      break;
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      /* this will catch real but unhandled state changes;
       * can only be caused by:
       * - a new state was added
       * - somehow the element was asked to jump across an intermediate state
       */
      g_warning ("Unhandled state change from %s to %s",
          gst_element_state_get_name (old_state),
          gst_element_state_get_name (old_pending));
      break;
  }

  parent = GST_ELEMENT_PARENT (element);

  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element,
      "signaling state change from %s to %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (GST_STATE (element)));

  /* tell the scheduler if we have one */
  if (element->sched) {
    if (gst_scheduler_state_transition (element->sched, element,
            old_transition) != GST_STATE_SUCCESS) {
      GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
          "scheduler could not change state");
      goto failure;
    }
  }

  /* tell our parent about the state change */
  if (parent && GST_IS_BIN (parent)) {
    gst_bin_child_state_change (GST_BIN (parent), old_state,
        GST_STATE (element), element);
  }
  /* at this point the state of the element could have changed again */

  g_signal_emit (G_OBJECT (element), gst_element_signals[STATE_CHANGE],
      0, old_state, GST_STATE (element));

  /* signal the state change in case somebody is waiting for us */
  GST_STATE_BROADCAST (element);

  gst_object_unref (GST_OBJECT (element));
  return GST_STATE_SUCCESS;

failure:
  /* undo the state change */
  GST_STATE (element) = old_state;
  GST_STATE_PENDING (element) = old_pending;
  gst_object_unref (GST_OBJECT (element));

  return GST_STATE_FAILURE;
}

/**
 * gst_element_get_factory:
 * @element: a #GstElement to request the element factory of.
 *
 * Retrieves the factory that was used to create this element.
 *
 * Returns: the #GstElementFactory used for creating this element.
 */
GstElementFactory *
gst_element_get_factory (GstElement * element)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  return GST_ELEMENT_GET_CLASS (element)->elementfactory;
}

static void
gst_element_dispose (GObject * object)
{
  GstElement *element = GST_ELEMENT (object);

  GST_CAT_INFO_OBJECT (GST_CAT_REFCOUNTING, element, "dispose");

  gst_element_set_state (element, GST_STATE_NULL);

  /* first we break all our links with the ouside */
  while (element->pads) {
    gst_element_remove_pad (element, GST_PAD (element->pads->data));
  }

  element->numsrcpads = 0;
  element->numsinkpads = 0;
  element->numpads = 0;

  gst_object_replace ((GstObject **) & element->sched, NULL);
  gst_object_replace ((GstObject **) & element->clock, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_element_finalize (GObject * object)
{
  GstElement *element = GST_ELEMENT (object);

  g_mutex_free (element->state_lock);
  g_cond_free (element->state_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#ifndef GST_DISABLE_LOADSAVE
/**
 * gst_element_save_thyself:
 * @element: a #GstElement to save.
 * @parent: the xml parent node.
 *
 * Saves the element as part of the given XML structure.
 *
 * Returns: the new #xmlNodePtr.
 */
static xmlNodePtr
gst_element_save_thyself (GstObject * object, xmlNodePtr parent)
{
  GList *pads;
  GstElementClass *oclass;
  GParamSpec **specs, *spec;
  gint nspecs, i;
  GValue value = { 0, };
  GstElement *element;

  g_return_val_if_fail (GST_IS_ELEMENT (object), parent);

  element = GST_ELEMENT (object);

  oclass = GST_ELEMENT_GET_CLASS (element);

  xmlNewChild (parent, NULL, "name", GST_ELEMENT_NAME (element));

  if (oclass->elementfactory != NULL) {
    GstElementFactory *factory = (GstElementFactory *) oclass->elementfactory;

    xmlNewChild (parent, NULL, "type", GST_PLUGIN_FEATURE (factory)->name);
  }

/* FIXME: what is this? */
/*  if (element->manager) */
/*    xmlNewChild(parent, NULL, "manager", GST_ELEMENT_NAME(element->manager)); */

  /* params */
  specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &nspecs);

  for (i = 0; i < nspecs; i++) {
    spec = specs[i];
    if (spec->flags & G_PARAM_READABLE) {
      xmlNodePtr param;
      char *contents;

      g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (spec));

      g_object_get_property (G_OBJECT (element), spec->name, &value);
      param = xmlNewChild (parent, NULL, "param", NULL);
      xmlNewChild (param, NULL, "name", spec->name);

      if (G_IS_PARAM_SPEC_STRING (spec))
        contents = g_value_dup_string (&value);
      else if (G_IS_PARAM_SPEC_ENUM (spec))
        contents = g_strdup_printf ("%d", g_value_get_enum (&value));
      else if (G_IS_PARAM_SPEC_INT64 (spec))
        contents = g_strdup_printf ("%" G_GINT64_FORMAT,
            g_value_get_int64 (&value));
      else
        contents = g_strdup_value_contents (&value);

      xmlNewChild (param, NULL, "value", contents);
      g_free (contents);

      g_value_unset (&value);
    }
  }

  pads = GST_ELEMENT_PADS (element);

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    /* figure out if it's a direct pad or a ghostpad */
    if (GST_ELEMENT (GST_OBJECT_PARENT (pad)) == element) {
      xmlNodePtr padtag = xmlNewChild (parent, NULL, "pad", NULL);

      gst_object_save_thyself (GST_OBJECT (pad), padtag);
    }
    pads = g_list_next (pads);
  }

  return parent;
}

static void
gst_element_restore_thyself (GstObject * object, xmlNodePtr self)
{
  xmlNodePtr children;
  GstElement *element;
  gchar *name = NULL;
  gchar *value = NULL;

  element = GST_ELEMENT (object);
  g_return_if_fail (element != NULL);

  /* parameters */
  children = self->xmlChildrenNode;
  while (children) {
    if (!strcmp (children->name, "param")) {
      xmlNodePtr child = children->xmlChildrenNode;

      while (child) {
        if (!strcmp (child->name, "name")) {
          name = xmlNodeGetContent (child);
        } else if (!strcmp (child->name, "value")) {
          value = xmlNodeGetContent (child);
        }
        child = child->next;
      }
      /* FIXME: can this just be g_object_set ? */
      gst_util_set_object_arg (G_OBJECT (element), name, value);
      /* g_object_set (G_OBJECT (element), name, value, NULL); */
      g_free (name);
      g_free (value);
    }
    children = children->next;
  }

  /* pads */
  children = self->xmlChildrenNode;
  while (children) {
    if (!strcmp (children->name, "pad")) {
      gst_pad_load_and_link (children, GST_OBJECT (element));
    }
    children = children->next;
  }

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    (GST_OBJECT_CLASS (parent_class)->restore_thyself) (object, self);
}
#endif /* GST_DISABLE_LOADSAVE */

/**
 * gst_element_yield:
 * @element: a #GstElement to yield.
 *
 * Requests a yield operation for the element. The scheduler will typically
 * give control to another element.
 */
void
gst_element_yield (GstElement * element)
{
  if (GST_ELEMENT_SCHEDULER (element)) {
    gst_scheduler_yield (GST_ELEMENT_SCHEDULER (element), element);
  }
}

/**
 * gst_element_interrupt:
 * @element: a #GstElement to interrupt.
 *
 * Requests the scheduler of this element to interrupt the execution of
 * this element and scheduler another one.
 *
 * Returns: TRUE if the element should exit its chain/loop/get
 * function ASAP, depending on the scheduler implementation.
 */
gboolean
gst_element_interrupt (GstElement * element)
{
  if (GST_ELEMENT_SCHEDULER (element)) {
    return gst_scheduler_interrupt (GST_ELEMENT_SCHEDULER (element), element);
  } else
    return TRUE;
}

/**
 * gst_element_set_scheduler:
 * @element: a #GstElement to set the scheduler of.
 * @scheduler: the #GstScheduler to set.
 *
 * Sets the scheduler of the element.  For internal use only, unless you're
 * testing elements.
 *
 * MT safe.
 */
void
gst_element_set_scheduler (GstElement * element, GstScheduler * scheduler)
{
  GstElementClass *oclass;

  g_return_if_fail (GST_IS_ELEMENT (element));

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->set_scheduler)
    oclass->set_scheduler (element, scheduler);
}

/**
 * gst_element_get_scheduler:
 * @element: a #GstElement to get the scheduler of.
 *
 * Returns the scheduler of the element.
 *
 * Returns: the element's #GstScheduler.
 *
 * MT safe.
 */
GstScheduler *
gst_element_get_scheduler (GstElement * element)
{
  GstScheduler *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), result);

  GST_LOCK (element);
  result = GST_ELEMENT_SCHEDULER (element);
  GST_UNLOCK (element);

  return result;
}

/**
 * gst_element_set_loop_function:
 * @element: a #GstElement to set the loop function of.
 * @loop: Pointer to #GstElementLoopFunction.
 *
 * This sets the loop function for the element.  The function pointed to
 * can deviate from the GstElementLoopFunction definition in type of
 * pointer only.
 *
 * NOTE: in order for this to take effect, the current loop function *must*
 * exit.  Assuming the loop function itself is the only one who will cause
 * a new loopfunc to be assigned, this should be no problem.
 */
void
gst_element_set_loop_function (GstElement * element,
    GstElementLoopFunction loop)
{
  gboolean need_notify = FALSE;

  g_return_if_fail (GST_IS_ELEMENT (element));

  /* if the element changed from loop based to chain/get based
   * or vice versa, we need to inform the scheduler about that */
  if ((element->loopfunc == NULL && loop != NULL) ||
      (element->loopfunc != NULL && loop == NULL)) {
    need_notify = TRUE;
  }

  /* set the loop function */
  element->loopfunc = loop;

  if (need_notify) {
    /* set the NEW_LOOPFUNC flag so everyone knows to go try again */
    GST_FLAG_SET (element, GST_ELEMENT_NEW_LOOPFUNC);

    if (GST_ELEMENT_SCHEDULER (element)) {
      gst_scheduler_scheduling_change (GST_ELEMENT_SCHEDULER (element),
          element);
    }
  }
}
static inline void
gst_element_emit_found_tag (GstElement * element, GstElement * source,
    const GstTagList * tag_list)
{
  gst_object_ref (GST_OBJECT (element));
  g_signal_emit (element, gst_element_signals[FOUND_TAG], 0, source, tag_list);
  gst_object_unref (GST_OBJECT (element));
}
static void
gst_element_found_tag_func (GstElement * element, GstElement * source,
    const GstTagList * tag_list)
{
  /* tell the parent */
  if (GST_OBJECT_PARENT (element)) {
    GST_CAT_LOG_OBJECT (GST_CAT_EVENT, element, "forwarding tag event to %s",
        GST_OBJECT_NAME (GST_OBJECT_PARENT (element)));
    gst_element_emit_found_tag (GST_ELEMENT (GST_OBJECT_PARENT (element)),
        source, tag_list);
  }
}

/**
 * gst_element_found_tags:
 * @element: the element that found the tags
 * @tag_list: the found tags
 *
 * This function emits the found_tags signal. This is a recursive signal, so
 * every parent will emit that signal, too, before this function returns.
 * Only emit this signal, when you extracted these tags out of the data stream,
 * not when you handle an event.
 */
void
gst_element_found_tags (GstElement * element, const GstTagList * tag_list)
{
  gst_element_emit_found_tag (element, element, tag_list);
}

/**
 * gst_element_found_tags_for_pad:
 * @element: element that found the tag
 * @pad: src pad the tags correspond to
 * @timestamp: time the tags were found
 * @list: the taglist
 *
 * This is a convenience routine for tag finding. Most of the time you only
 * want to push the found tags down one pad, in that case this function is for
 * you. It takes ownership of the taglist, emits the found-tag signal and
 * pushes a tag event down the pad.
 * <note>This function may not be used in a #GstPadGetFunction, because it calls
 * gst_pad_push(). In those functions, call gst_element_found_tags(), create a
 * tag event with gst_event_new_tag() and return that from your
 * #GstPadGetFunction.</note>
 */
void
gst_element_found_tags_for_pad (GstElement * element, GstPad * pad,
    GstClockTime timestamp, GstTagList * list)
{
  GstEvent *tag_event;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC);
  g_return_if_fail (element == GST_PAD_PARENT (pad));
  g_return_if_fail (list != NULL);

  tag_event = gst_event_new_tag (list);
  GST_EVENT_TIMESTAMP (tag_event) = timestamp;
  gst_element_found_tags (element, gst_event_tag_get_list (tag_event));
  if (GST_PAD_IS_USABLE (pad)) {
    gst_pad_push (pad, GST_DATA (tag_event));
  } else {
    gst_data_unref (GST_DATA (tag_event));
  }
}

static inline void
gst_element_set_eos_recursive (GstElement * element)
{
  /* this function is only called, when we were in PLAYING before. So every
     parent that's PAUSED was PLAYING before. That means it has reached EOS. */
  GstElement *parent;

  GST_CAT_DEBUG (GST_CAT_EVENT, "setting recursive EOS on %s",
      GST_OBJECT_NAME (element));
  g_signal_emit (G_OBJECT (element), gst_element_signals[EOS], 0);

  if (!GST_OBJECT_PARENT (element))
    return;

  parent = GST_ELEMENT (GST_OBJECT_PARENT (element));
  if (GST_STATE (parent) == GST_STATE_PAUSED)
    gst_element_set_eos_recursive (parent);
}

/**
 * gst_element_set_eos:
 * @element: a #GstElement to set to the EOS state.
 *
 * Perform the actions needed to bring the element in the EOS state.
 */
void
gst_element_set_eos (GstElement * element)
{
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_CAT_DEBUG (GST_CAT_EVENT, "setting EOS on element %s",
      GST_OBJECT_NAME (element));

  if (GST_STATE (element) == GST_STATE_PLAYING) {
    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_eos_recursive (element);
  } else {
    g_signal_emit (G_OBJECT (element), gst_element_signals[EOS], 0);
  }
}

/**
 * gst_element_get_managing_bin:
 * @element: a #GstElement to get the managing bin of.
 *
 * Gets the managing bin (a pipeline or a thread, for example) of an element.
 *
 * Returns: the #GstBin, or NULL on failure.
 **/
GstBin *
gst_element_get_managing_bin (GstElement * element)
{
  GstBin *bin;

  g_return_val_if_fail (element != NULL, NULL);

  bin = GST_BIN (gst_object_get_parent (GST_OBJECT (element)));

  while (bin && !GST_FLAG_IS_SET (GST_OBJECT (bin), GST_BIN_FLAG_MANAGER))
    bin = GST_BIN (gst_object_get_parent (GST_OBJECT (bin)));

  return bin;
}
