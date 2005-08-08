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
#include "gstbus.h"
#include "gstmarshal.h"
#include "gsterror.h"
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
static GstElementStateReturn gst_element_get_state_func (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout);
static void gst_element_set_bus_func (GstElement * element, GstBus * bus);

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
   * GstElement::state-changed:
   * @gstelement: the object which received the signal
   * @int:
   * @int:
   *
   * the #GstElementState of the element has been changed
   */
  gst_element_signals[STATE_CHANGE] =
      g_signal_new ("state-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstElementClass, state_changed), NULL,
      NULL, gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
  /**
   * GstElement::pad-added:
   * @gstelement: the object which received the signal
   * @object:
   *
   * a new #GstPad has been added to the element
   */
  gst_element_signals[NEW_PAD] =
      g_signal_new ("pad-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, pad_added), NULL, NULL,
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
  klass->get_state = GST_DEBUG_FUNCPTR (gst_element_get_state_func);
  klass->set_bus = GST_DEBUG_FUNCPTR (gst_element_set_bus_func);
  klass->numpadtemplates = 0;

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
 * gst_element_set_base_time:
 * @element: a #GstElement.
 * @time: the base time to set.
 *
 * Set the base time of an element. See @gst_element_get_base_time().
 *
 * MT safe.
 */
void
gst_element_set_base_time (GstElement * element, GstClockTime time)
{
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_LOCK (element);
  element->base_time = time;
  GST_UNLOCK (element);
}

/**
 * gst_element_get_base_time:
 * @element: a #GstElement.
 *
 * Returns the base time of the element. The base time is the
 * absolute time of the clock when this element was last put to
 * PLAYING. Substracting the base time from the clock time gives
 * the stream time of the element.
 *
 * Returns: the base time of the element.
 *
 * MT safe.
 */
GstClockTime
gst_element_get_base_time (GstElement * element)
{
  GstClockTime result;

  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_CLOCK_TIME_NONE);

  GST_LOCK (element);
  result = element->base_time;
  GST_UNLOCK (element);

  return result;
}

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
 * gst_element_add_pad:
 * @element: a #GstElement to add the pad to.
 * @pad: the #GstPad to add to the element.
 *
 * Adds a pad (link point) to @element. @pad's parent will be set to @element;
 * see gst_object_set_parent() for refcounting information.
 *
 * Pads are not automatically activated so elements should perform the needed
 * steps to activate the pad. 
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
      goto no_direction;
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
no_direction:
  {
    GST_LOCK (pad);
    g_critical
        ("Trying to add pad %s to element %s, but it has no direction",
        GST_OBJECT_NAME (pad), GST_ELEMENT_NAME (element));
    GST_UNLOCK (pad);
    GST_UNLOCK (element);
    return FALSE;
  }
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
  GstPad *peer;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* locking pad to look at the name and parent */
  GST_LOCK (pad);
  GST_CAT_INFO_OBJECT (GST_CAT_ELEMENT_PADS, element, "removing pad '%s'",
      GST_STR_NULL (GST_PAD_NAME (pad)));

  if (G_UNLIKELY (GST_PAD_PARENT (pad) != element))
    goto not_our_pad;
  GST_UNLOCK (pad);

  /* unlink */
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
      g_critical ("Removing pad without direction???");
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
    gst_object_ref (result);
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

  if (newpad)
    gst_object_ref (newpad);

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
 * Returns: requested #GstPad if found, otherwise NULL. Unref after usage.
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
  gst_object_ref (pad);
  return GST_ITERATOR_ITEM_PASS;
}

/**
 * gst_element_iterate_pads:
 * @element: a #GstElement to iterate pads of.
 *
 * Retrieves an iterattor of @element's pads. 
 *
 * Returns: the #GstIterator of #GstPad. Unref each pad after use.
 *
 * MT safe.
 */
GstIterator *
gst_element_iterate_pads (GstElement * element)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  GST_LOCK (element);
  gst_object_ref (element);
  result = gst_iterator_new_list (GST_GET_LOCK (element),
      &element->pads_cookie,
      &element->pads,
      element,
      (GstIteratorItemFunction) iterate_pad,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_UNLOCK (element);

  return result;
}

static gint
direction_filter (gconstpointer pad, gconstpointer direction)
{
  if (GST_PAD_DIRECTION (pad) == GPOINTER_TO_INT (direction)) {
    /* pass the ref through */
    return 0;
  } else {
    /* unref */
    /* FIXME: this is very stupid */
    gst_object_unref (GST_OBJECT_CAST (pad));
    return 1;
  }
}

/**
 * gst_element_iterate_src_pads:
 * @element: a #GstElement.
 *
 * Retrieves an iterator of @element's source pads. 
 *
 * Returns: the #GstIterator of #GstPad. Unref each pad after use.
 *
 * MT safe.
 */
GstIterator *
gst_element_iterate_src_pads (GstElement * element)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  return gst_iterator_filter (gst_element_iterate_pads (element),
      direction_filter, GINT_TO_POINTER (GST_PAD_SRC));
}

/**
 * gst_element_iterate_sink_pads:
 * @element: a #GstElement.
 *
 * Retrieves an iterator of @element's sink pads. 
 *
 * Returns: the #GstIterator of #GstPad. Unref each pad after use.
 *
 * MT safe.
 */
GstIterator *
gst_element_iterate_sink_pads (GstElement * element)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  return gst_iterator_filter (gst_element_iterate_pads (element),
      direction_filter, GINT_TO_POINTER (GST_PAD_SINK));
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
      gst_object_ref (templ));
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
    gst_object_ref (result);

  GST_UNLOCK (element);

  return result;

  /* ERROR handling */
wrong_direction:
  {
    g_warning ("unknown pad direction %d", dir);
    return NULL;
  }
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
        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }
  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "can't send event on element %s",
      GST_ELEMENT_NAME (element));

  return result;
}

/**
 * gst_element_seek:
 * @element: a #GstElement to send the event to.
 * @seek_method: the method to use for seeking (GST_SEEK_METHOD_*).
 * @seek_format: the #GstFormat to use for seeking (GST_FORMAT_*).
 * @seek_flags: the flags to use for seeking (GST_SEEK_FLAG_*).
 * @offset: the offset to seek to (in the given seek_format).
 *
 * Sends a seek event to an element.
 *
 * Returns: TRUE if the event was handled.
 *
 * MT safe.
 */
gboolean
gst_element_seek (GstElement * element, gdouble rate, GstFormat format,
    GstSeekFlags flags, GstSeekType cur_type, gint64 cur,
    GstSeekType stop_type, gint64 stop)
{
  GstEvent *event;
  gboolean result;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  event =
      gst_event_new_seek (rate, format, flags, cur_type, cur, stop_type, stop);
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

        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }
  return result;
}

/**
 * gst_element_query:
 * @element: a #GstElement to perform the query on.
 * @query: the #GstQuery.
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
gst_element_query (GstElement * element, GstQuery * query)
{
  GstElementClass *oclass;
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (query != NULL, FALSE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->query) {
    result = oclass->query (element, query);
  } else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SRC);

    if (pad) {
      result = gst_pad_query (pad, query);

      gst_object_unref (pad);
    } else {
      pad = gst_element_get_random_pad (element, GST_PAD_SINK);
      if (pad) {
        GstPad *peer = gst_pad_get_peer (pad);

        if (peer) {
          result = gst_pad_query (peer, query);

          gst_object_unref (peer);
        }
        gst_object_unref (pad);
      }
    }
  }
  return result;
}

/**
 * gst_element_post_message:
 * @element: a #GstElement posting the message
 * @message: a #GstMessage to post
 *
 * Post a message on the element's #GstBus.
 *
 * Returns: TRUE if the message was successfully posted.
 *
 * MT safe.
 */
gboolean
gst_element_post_message (GstElement * element, GstMessage * message)
{
  GstBus *bus;
  gboolean result = FALSE;

  GST_DEBUG ("posting message %p ...", message);

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (message != NULL, FALSE);

  GST_LOCK (element);
  bus = element->bus;

  if (G_UNLIKELY (bus == NULL)) {
    GST_DEBUG ("... but I won't because I have no bus");
    GST_UNLOCK (element);
    gst_message_unref (message);
    return FALSE;
  }
  gst_object_ref (bus);
  GST_DEBUG ("... on bus %" GST_PTR_FORMAT, bus);
  GST_UNLOCK (element);

  result = gst_bus_post (bus, message);
  gst_object_unref (bus);

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
 * gst_element_message_full:
 * @element:  a #GstElement to send message from
 * @type:     the #GstMessageType
 * @domain:   the GStreamer GError domain this message belongs to
 * @code:     the GError code belonging to the domain
 * @text:     an allocated text string to be used as a replacement for the
 *            default message connected to code, or NULL
 * @debug:    an allocated debug message to be used as a replacement for the
 *            default debugging information, or NULL
 * @file:     the source code file where the error was generated
 * @function: the source code function where the error was generated
 * @line:     the source code line where the error was generated
 *
 * Post an error or warning message on the bus from inside an element.
 *
 * MT safe.
 */
void gst_element_message_full
    (GstElement * element, GstMessageType type,
    GQuark domain, gint code, gchar * text,
    gchar * debug, const gchar * file, const gchar * function, gint line)
{
  GError *gerror = NULL;
  gchar *name;
  gchar *sent_text;
  gchar *sent_debug;
  GstMessage *message = NULL;

  /* checks */
  GST_DEBUG ("start");
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail ((type == GST_MESSAGE_ERROR) ||
      (type == GST_MESSAGE_WARNING));

  /* check if we send the given text or the default error text */
  if ((text == NULL) || (text[0] == 0)) {
    /* text could have come from g_strdup_printf (""); */
    g_free (text);
    sent_text = gst_error_get_message (domain, code);
  } else
    sent_text = text;

  /* construct a sent_debug with extra information from source */
  if ((debug == NULL) || (debug[0] == 0)) {
    /* debug could have come from g_strdup_printf (""); */
    sent_debug = NULL;
  } else {
    name = gst_object_get_path_string (GST_OBJECT (element));
    sent_debug = g_strdup_printf ("%s(%d): %s: %s:\n%s",
        file, line, function, name, debug ? debug : "");
    g_free (name);
  }
  g_free (debug);

  /* create gerror and post message */
  GST_CAT_INFO_OBJECT (GST_CAT_ERROR_SYSTEM, element, "posting message: %s",
      sent_text);
  gerror = g_error_new_literal (domain, code, sent_text);

  if (type == GST_MESSAGE_ERROR) {
    message = gst_message_new_error (GST_OBJECT (element), gerror, sent_debug);
  } else if (type == GST_MESSAGE_WARNING) {
    message = gst_message_new_warning (GST_OBJECT (element), gerror,
        sent_debug);
  } else {
    g_assert_not_reached ();
  }
  gst_element_post_message (element, message);

  GST_CAT_INFO_OBJECT (GST_CAT_ERROR_SYSTEM, element, "posted message: %s",
      sent_text);

  /* cleanup */
  g_error_free (gerror);
  g_free (sent_debug);
  g_free (sent_text);
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

/* MT safe */
static GstElementStateReturn
gst_element_get_state_func (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout)
{
  GstElementStateReturn ret = GST_STATE_FAILURE;
  GstElementState old_pending;

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "getting state");

  GST_STATE_LOCK (element);
  /* we got an error, report immediatly */
  if (GST_STATE_NO_PREROLL (element)) {
    ret = GST_STATE_NO_PREROLL;
    goto done;
  }

  /* we got an error, report immediatly */
  if (GST_STATE_ERROR (element)) {
    ret = GST_STATE_FAILURE;
    goto done;
  }

  old_pending = GST_STATE_PENDING (element);
  if (old_pending != GST_STATE_VOID_PENDING) {
    GTimeVal *timeval, abstimeout;

    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "wait for pending");
    if (timeout) {
      /* make timeout absolute */
      g_get_current_time (&abstimeout);
      g_time_val_add (&abstimeout,
          timeout->tv_sec * G_USEC_PER_SEC + timeout->tv_usec);
      timeval = &abstimeout;
    } else {
      timeval = NULL;
    }
    /* we have a pending state change, wait for it to complete */
    if (!GST_STATE_TIMED_WAIT (element, timeval)) {
      GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "timeout");
      /* timeout triggered */
      ret = GST_STATE_ASYNC;
    } else {
      /* could be success or failure */
      if (old_pending == GST_STATE (element)) {
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "got success");
        ret = GST_STATE_SUCCESS;
      } else {
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "got failure");
        ret = GST_STATE_FAILURE;
      }
    }
  }
  /* if nothing is pending anymore we can return SUCCESS */
  if (GST_STATE_PENDING (element) == GST_STATE_VOID_PENDING) {
    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "nothing pending");
    ret = GST_STATE_SUCCESS;
  }

done:
  if (state)
    *state = GST_STATE (element);
  if (pending)
    *pending = GST_STATE_PENDING (element);

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
      "state current: %s, pending: %s, error: %d, no_preroll: %d, result: %d",
      gst_element_state_get_name (GST_STATE (element)),
      gst_element_state_get_name (GST_STATE_PENDING (element)),
      GST_STATE_ERROR (element), GST_STATE_NO_PREROLL (element), ret);

  GST_STATE_UNLOCK (element);

  return ret;
}

/**
 * gst_element_get_state:
 * @element: a #GstElement to get the state of.
 * @state: a pointer to #GstElementState to hold the state. Can be NULL.
 * @pending: a pointer to #GstElementState to hold the pending state.
 *           Can be NULL.
 * @timeout: a #GTimeVal to specify the timeout for an async
 *           state change or NULL for infinite timeout.
 *
 * Gets the state of the element. 
 *
 * For elements that performed an ASYNC state change, as reported by 
 * #gst_element_set_state(), this function will block up to the 
 * specified timeout value for the state change to complete. 
 * If the element completes the state change or goes into
 * an error, this function returns immediatly with a return value of
 * GST_STATE_SUCCESS or GST_STATE_FAILURE respectively. 
 *
 * Returns: GST_STATE_SUCCESS if the element has no more pending state and
 *          the last state change succeeded, GST_STATE_ASYNC
 *          if the element is still performing a state change or 
 *          GST_STATE_FAILURE if the last state change failed.
 *
 * MT safe.
 */
GstElementStateReturn
gst_element_get_state (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout)
{
  GstElementClass *oclass;
  GstElementStateReturn result = GST_STATE_FAILURE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_state)
    result = (oclass->get_state) (element, state, pending, timeout);

  return result;
}

/**
 * gst_element_abort_state:
 * @element: a #GstElement to abort the state of.
 *
 * Abort the state change of the element. This function is used
 * by elements that do asynchronous state changes and find out 
 * something is wrong.
 *
 * This function should be called with the STATE_LOCK held.
 *
 * MT safe.
 */
void
gst_element_abort_state (GstElement * element)
{
  GstElementState pending;

  g_return_if_fail (GST_IS_ELEMENT (element));

  pending = GST_STATE_PENDING (element);

  if (pending != GST_STATE_VOID_PENDING && !GST_STATE_ERROR (element)) {
#ifndef GST_DISABLE_GST_DEBUG
    GstElementState old_state = GST_STATE (element);
#endif

    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
        "aborting state from %s to %s", gst_element_state_get_name (old_state),
        gst_element_state_get_name (pending));

    /* flag error */
    GST_STATE_ERROR (element) = TRUE;

    GST_STATE_BROADCAST (element);
  }
}

/**
 * gst_element_commit_state:
 * @element: a #GstElement to commit the state of.
 *
 * Commit the state change of the element. This function is used
 * by elements that do asynchronous state changes.
 *
 * This function can only be called with the STATE_LOCK held.
 *
 * MT safe.
 */
void
gst_element_commit_state (GstElement * element)
{
  GstElementState pending;
  GstMessage *message;

  g_return_if_fail (GST_IS_ELEMENT (element));

  pending = GST_STATE_PENDING (element);

  if (pending != GST_STATE_VOID_PENDING) {
    GstElementState old_state = GST_STATE (element);

    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
        "commiting state from %s to %s", gst_element_state_get_name (old_state),
        gst_element_state_get_name (pending));

    GST_STATE (element) = pending;
    GST_STATE_PENDING (element) = GST_STATE_VOID_PENDING;
    GST_STATE_ERROR (element) = FALSE;

    g_signal_emit (G_OBJECT (element), gst_element_signals[STATE_CHANGE],
        0, old_state, pending);
    message = gst_message_new_state_changed (GST_OBJECT (element),
        old_state, pending);
    gst_element_post_message (element, message);
    GST_STATE_BROADCAST (element);
  }
}

/**
 * gst_element_lost_state:
 * @element: a #GstElement the state is lost of
 *
 * Brings the element to the lost state. The current state of the
 * element is copied to the pending state so that any call to
 * #gst_element_get_state() will return ASYNC.
 * This is mostly used for elements that lost their preroll buffer
 * in the PAUSED state after a flush, they become PAUSED again
 * if a new preroll buffer is queued.
 * This function can only be called when the element is currently
 * not in error or an async state change.
 *
 * This function can only be called with the STATE_LOCK held.
 *
 * MT safe.
 */
void
gst_element_lost_state (GstElement * element)
{
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (GST_STATE_PENDING (element) == GST_STATE_VOID_PENDING &&
      !GST_STATE_ERROR (element)) {
    GstElementState current_state = GST_STATE (element);

    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
        "lost state of %s", gst_element_state_get_name (current_state));

    GST_STATE_PENDING (element) = current_state;
    GST_STATE_ERROR (element) = FALSE;
  }
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
 * Returns: Result of the state change using #GstElementStateReturn.
 *
 * MT safe.
 */
GstElementStateReturn
gst_element_set_state (GstElement * element, GstElementState state)
{
  GstElementClass *oclass;
  GstElementState current;
  GstElementStateReturn return_val = GST_STATE_SUCCESS;
  GstElementStateReturn ret;
  GstElementState pending;
  GTimeVal tv;


  /* get current element state,  need to call the method so that
   * we call the virtual method and subclasses can implement their
   * own algorithms */
  GST_TIME_TO_TIMEVAL (0, tv);
  ret = gst_element_get_state (element, &current, &pending, &tv);

  /* get the element state lock */
  GST_STATE_LOCK (element);
  /* this is the state we should go to */
  GST_STATE_FINAL (element) = state;
  if (ret == GST_STATE_ASYNC) {
    gst_element_commit_state (element);
  }

  /* start with the current state */
  current = GST_STATE (element);

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "setting state from %s to %s",
      gst_element_state_get_name (current), gst_element_state_get_name (state));

  oclass = GST_ELEMENT_GET_CLASS (element);

  /* We always perform at least one state change, even if the 
   * current state is equal to the required state. This is needed
   * for bins that sync their children. */
  do {
    GstElementState pending;

    /* calculate the pending state */
    if (current < state)
      pending = current << 1;
    else if (current > state)
      pending = current >> 1;
    else
      pending = current;

    /* set the pending state variable */
    GST_STATE_PENDING (element) = pending;

    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
        "%s: setting state from %s to %s",
        (pending != state ? "intermediate" : "final"),
        gst_element_state_get_name (current),
        gst_element_state_get_name (pending));

    /* call the state change function so it can set the state */
    if (oclass->change_state)
      return_val = (oclass->change_state) (element);
    else
      return_val = GST_STATE_FAILURE;

    /* clear the error and preroll flag, we need to do that after
     * calling the virtual change_state function so that it can use the
     * old previous value. */
    GST_STATE_ERROR (element) = FALSE;
    GST_STATE_NO_PREROLL (element) = FALSE;

    switch (return_val) {
      case GST_STATE_FAILURE:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "have failed change_state return");
        /* state change failure exits the loop */
        gst_element_abort_state (element);
        goto exit;
      case GST_STATE_ASYNC:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "element will change state async");
        /* an async state change exits the loop, we can only
         * go to the next state change when this one completes. */
        goto exit;
      case GST_STATE_SUCCESS:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "element changed state successfully");
        /* we can commit the state now and proceed to the next state */
        gst_element_commit_state (element);
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "commited state");
        break;
      case GST_STATE_NO_PREROLL:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "element changed state successfully and can't preroll");
        /* we can commit the state now and proceed to the next state */
        gst_element_commit_state (element);
        GST_STATE_NO_PREROLL (element) = TRUE;
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "commited state");
        break;
      default:
        goto invalid_return;
    }
    /* get the current state of the element and see if we need to do more
     * state changes */
    current = GST_STATE (element);
  }
  while (current != state);

exit:
  GST_STATE_FINAL (element) = GST_STATE_VOID_PENDING;
  GST_STATE_UNLOCK (element);

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "exit state change");

  return return_val;

  /* ERROR */
invalid_return:
  {
    GST_STATE_FINAL (element) = GST_STATE_VOID_PENDING;
    GST_STATE_UNLOCK (element);
    /* somebody added a GST_STATE_ and forgot to do stuff here ! */
    g_critical ("unknown return value %d from a state change function",
        return_val);
    return GST_STATE_FAILURE;
  }
}

/* gst_iterator_fold functions for pads_activate */

static gboolean
activate_pads (GstPad * pad, GValue * ret, gboolean * active)
{
  if (!gst_pad_set_active (pad, *active))
    g_value_set_boolean (ret, FALSE);
  else if (!*active)
    gst_pad_set_caps (pad, NULL);

  gst_object_unref (pad);
  return TRUE;
}

/* returns false on error or early cutout of the fold, true otherwise */
static gboolean
iterator_fold_with_resync (GstIterator * iter, GstIteratorFoldFunction func,
    GValue * ret, gpointer user_data)
{
  GstIteratorResult ires;
  gboolean res = TRUE;

  while (1) {
    ires = gst_iterator_fold (iter, func, ret, user_data);

    switch (ires) {
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_DONE:
        res = TRUE;
        goto done;
      default:
        res = FALSE;
        goto done;
    }
  }

done:
  return res;
}

/* is called with STATE_LOCK
 */
static gboolean
gst_element_pads_activate (GstElement * element, gboolean active)
{
  GValue ret = { 0, };
  GstIterator *iter;
  gboolean fold_ok;

  /* no need to unset this later, it's just a boolean */
  g_value_init (&ret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&ret, TRUE);

  iter = gst_element_iterate_src_pads (element);
  fold_ok = iterator_fold_with_resync
      (iter, (GstIteratorFoldFunction) activate_pads, &ret, &active);
  gst_iterator_free (iter);
  if (!fold_ok || !g_value_get_boolean (&ret))
    return FALSE;

  iter = gst_element_iterate_sink_pads (element);
  fold_ok = iterator_fold_with_resync
      (iter, (GstIteratorFoldFunction) activate_pads, &ret, &active);
  gst_iterator_free (iter);
  if (!fold_ok || !g_value_get_boolean (&ret))
    return FALSE;

  return TRUE;
}

/* is called with STATE_LOCK */
static GstElementStateReturn
gst_element_change_state (GstElement * element)
{
  GstElementState old_state;
  gint old_pending, old_transition;
  GstElementStateReturn result = GST_STATE_SUCCESS;

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
    if (GST_STATE_NO_PREROLL (element))
      return GST_STATE_NO_PREROLL;
    else
      return GST_STATE_SUCCESS;
  }

  GST_CAT_LOG_OBJECT (GST_CAT_STATES, element,
      "default handler tries setting state from %s to %s (%04x)",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (old_pending), old_transition);

  switch (old_transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (!gst_element_pads_activate (element, TRUE)) {
        result = GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
      /* deactivate pads in both cases, since they are activated on
         ready->paused but the element might not have made it to paused */
      if (!gst_element_pads_activate (element, FALSE)) {
        result = GST_STATE_FAILURE;
      } else {
        GST_LOCK (element);
        element->base_time = 0;
        GST_UNLOCK (element);
      }
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

  return result;
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

  /* ref so we don't hit 0 again */
  gst_object_ref (object);

  /* first we break all our links with the outside */
  while (element->pads) {
    gst_element_remove_pad (element, GST_PAD_CAST (element->pads->data));
  }
  if (G_UNLIKELY (element->pads != 0)) {
    g_critical ("could not remove pads from element %s",
        GST_STR_NULL (GST_OBJECT_NAME (object)));
  }

  GST_LOCK (element);
  gst_object_replace ((GstObject **) & element->clock, NULL);
  GST_UNLOCK (element);

  GST_CAT_INFO_OBJECT (GST_CAT_REFCOUNTING, element, "dispose parent");

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_element_finalize (GObject * object)
{
  GstElement *element = GST_ELEMENT (object);

  GST_CAT_INFO_OBJECT (GST_CAT_REFCOUNTING, element, "finalize");

  GST_STATE_LOCK (element);
  if (element->state_cond)
    g_cond_free (element->state_cond);
  element->state_cond = NULL;
  GST_STATE_UNLOCK (element);
  g_mutex_free (element->state_lock);

  GST_CAT_INFO_OBJECT (GST_CAT_REFCOUNTING, element, "finalize parent");

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
  guint nspecs;
  gint i;
  GValue value = { 0, };
  GstElement *element;

  g_return_val_if_fail (GST_IS_ELEMENT (object), parent);

  element = GST_ELEMENT (object);

  oclass = GST_ELEMENT_GET_CLASS (element);

  xmlNewChild (parent, NULL, (xmlChar *) "name",
      (xmlChar *) GST_ELEMENT_NAME (element));

  if (oclass->elementfactory != NULL) {
    GstElementFactory *factory = (GstElementFactory *) oclass->elementfactory;

    xmlNewChild (parent, NULL, (xmlChar *) "type",
        (xmlChar *) GST_PLUGIN_FEATURE (factory)->name);
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
      param = xmlNewChild (parent, NULL, (xmlChar *) "param", NULL);
      xmlNewChild (param, NULL, (xmlChar *) "name", (xmlChar *) spec->name);

      if (G_IS_PARAM_SPEC_STRING (spec))
        contents = g_value_dup_string (&value);
      else if (G_IS_PARAM_SPEC_ENUM (spec))
        contents = g_strdup_printf ("%d", g_value_get_enum (&value));
      else if (G_IS_PARAM_SPEC_INT64 (spec))
        contents = g_strdup_printf ("%" G_GINT64_FORMAT,
            g_value_get_int64 (&value));
      else
        contents = g_strdup_value_contents (&value);

      xmlNewChild (param, NULL, (xmlChar *) "value", (xmlChar *) contents);
      g_free (contents);

      g_value_unset (&value);
    }
  }

  pads = GST_ELEMENT_PADS (element);

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    /* figure out if it's a direct pad or a ghostpad */
    if (GST_ELEMENT (GST_OBJECT_PARENT (pad)) == element) {
      xmlNodePtr padtag = xmlNewChild (parent, NULL, (xmlChar *) "pad", NULL);

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
    if (!strcmp ((char *) children->name, "param")) {
      xmlNodePtr child = children->xmlChildrenNode;

      while (child) {
        if (!strcmp ((char *) child->name, "name")) {
          name = (gchar *) xmlNodeGetContent (child);
        } else if (!strcmp ((char *) child->name, "value")) {
          value = (gchar *) xmlNodeGetContent (child);
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
    if (!strcmp ((char *) children->name, "pad")) {
      gst_pad_load_and_link (children, GST_OBJECT (element));
    }
    children = children->next;
  }

  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    (GST_OBJECT_CLASS (parent_class)->restore_thyself) (object, self);
}
#endif /* GST_DISABLE_LOADSAVE */

static void
gst_element_set_bus_func (GstElement * element, GstBus * bus)
{
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element, "setting bus to %p", bus);

  GST_LOCK (element);
  gst_object_replace ((GstObject **) & GST_ELEMENT_BUS (element),
      GST_OBJECT (bus));
  GST_UNLOCK (element);
}

/**
 * gst_element_set_bus:
 * @element: a #GstElement to set the bus of.
 * @bus: the #GstBus to set.
 *
 * Sets the bus of the element.  For internal use only, unless you're
 * testing elements.
 *
 * MT safe.
 */
void
gst_element_set_bus (GstElement * element, GstBus * bus)
{
  GstElementClass *oclass;

  g_return_if_fail (GST_IS_ELEMENT (element));

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->set_bus)
    oclass->set_bus (element, bus);
}

/**
 * gst_element_get_bus:
 * @element: a #GstElement to get the bus of.
 *
 * Returns the bus of the element.
 *
 * Returns: the element's #GstBus. unref after usage.
 *
 * MT safe.
 */
GstBus *
gst_element_get_bus (GstElement * element)
{
  GstBus *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), result);

  GST_LOCK (element);
  result = GST_ELEMENT_BUS (element);
  gst_object_ref (result);
  GST_UNLOCK (element);

  GST_DEBUG_OBJECT (element, "got bus %" GST_PTR_FORMAT, result);

  return result;
}
