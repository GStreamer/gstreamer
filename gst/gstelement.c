/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
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
#include "gstevent.h"
#include "gstutils.h"
#include "gstinfo.h"
#include "gst-i18n-lib.h"
#include "gstscheduler.h"
#include "gstpipeline.h"

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

static GstElementStateReturn gst_element_change_state (GstElement * element);
static gboolean gst_element_get_state_func (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout);
static void gst_element_set_manager_func (GstElement * element,
    GstPipeline * manager);

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

  gst_element_signals[STATE_CHANGE] =
      g_signal_new ("state-change", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstElementClass, state_change), NULL,
      NULL, gst_marshal_VOID__INT_INT, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
  gst_element_signals[NEW_PAD] =
      g_signal_new ("new-pad", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, new_pad), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
  gst_element_signals[PAD_REMOVED] =
      g_signal_new ("pad-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstElementClass, pad_removed), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
  gst_element_signals[NO_MORE_PADS] =
      g_signal_new ("no-more-pads", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstElementClass, no_more_pads), NULL,
      NULL, gst_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_element_dispose);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_element_save_thyself);
  gstobject_class->restore_thyself =
      GST_DEBUG_FUNCPTR (gst_element_restore_thyself);
#endif

  klass->change_state = GST_DEBUG_FUNCPTR (gst_element_change_state);
  klass->get_state = GST_DEBUG_FUNCPTR (gst_element_get_state_func);
  klass->set_manager = GST_DEBUG_FUNCPTR (gst_element_set_manager_func);
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
  element->numpads = 0;
  element->numsrcpads = 0;
  element->numsinkpads = 0;
  element->pads_cookie = 0;
  element->pads = NULL;
  element->srcpads = NULL;
  element->sinkpads = NULL;
  element->manager = NULL;
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
}

/**
 * gst_element_requires_clock:
 * @element: a #GstElement to query
 *
 * Query if the element requiresd a clock
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
  GstElement *old_parent;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  /* first check to make sure the pad hasn't already been added to another
   * element */
  old_parent = gst_pad_get_parent (pad);
  if (G_UNLIKELY (old_parent != NULL))
    goto had_parent;

  GST_LOCK (element);
  GST_LOCK (pad);
  /* then check to see if there's already a pad by that name here */
  if (G_UNLIKELY (!gst_object_check_uniqueness (element->pads,
              GST_PAD_NAME (pad))))
    goto name_exists;

  GST_CAT_INFO_OBJECT (GST_CAT_ELEMENT_PADS, element, "adding pad '%s'",
      GST_STR_NULL (GST_PAD_NAME (pad)));

  GST_UNLOCK (pad);

  /* set the pad's parent */
  gst_object_set_parent (GST_OBJECT_CAST (pad), GST_OBJECT_CAST (element));

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
      g_assert_not_reached ();
      break;
  }
  element->pads = g_list_prepend (element->pads, pad);
  element->numpads++;
  element->pads_cookie++;
  GST_UNLOCK (element);

  GST_STATE_LOCK (element);
  /* activate pad when we are playing */
  if (GST_STATE (element) == GST_STATE_PLAYING)
    gst_pad_set_active (pad, TRUE);
  GST_STATE_UNLOCK (element);

  /* emit the NEW_PAD signal */
  g_signal_emit (G_OBJECT (element), gst_element_signals[NEW_PAD], 0, pad);

  return TRUE;

had_parent:
  {
    gchar *parent_name = gst_element_get_name (old_parent);

    gst_object_unref (GST_OBJECT (old_parent));
    GST_LOCK (pad);
    GST_LOCK (element);
    g_critical
        ("Padname %s:%s already has parent when trying to add to element %s",
        parent_name, GST_PAD_NAME (pad), GST_ELEMENT_NAME (element));
    GST_UNLOCK (element);
    GST_UNLOCK (pad);
    g_free (parent_name);
    return FALSE;
  }

name_exists:
  g_critical ("Padname %s is not unique in element %s, not adding\n",
      GST_PAD_NAME (pad), GST_ELEMENT_NAME (element));

  GST_UNLOCK (pad);
  GST_UNLOCK (element);
  return FALSE;
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
 * MT safe.
 */
void
gst_element_remove_pad (GstElement * element, GstPad * pad)
{
  GstElement *current_parent;

  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (GST_IS_PAD (pad));

  current_parent = gst_pad_get_parent (pad);
  if (G_UNLIKELY (current_parent != element))
    goto not_our_pad;

  if (GST_IS_REAL_PAD (pad)) {
    GstPad *peer = gst_pad_get_peer (pad);

    /* unlink */
    if (peer != NULL) {
      /* window for MT unsafeness, someone else could unlink here
       * and then we call unlink with wrong pads. The unlink
       * function would catch this and safely return failed. */
      gst_pad_unlink (pad, GST_PAD_CAST (peer));
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

  return;

not_our_pad:
  {
    gchar *parent_name = gst_element_get_name (current_parent);

    gst_object_unref (GST_OBJECT (current_parent));
    GST_LOCK (pad);
    GST_LOCK (element);
    g_critical ("Padname %s:%s does not belong to element %s when removing",
        parent_name, GST_PAD_NAME (pad), GST_ELEMENT_NAME (element));
    GST_UNLOCK (element);
    GST_UNLOCK (pad);
    g_free (parent_name);
    return;
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

/**
 * gst_element_get_pad:
 * @element: a #GstElement.
 * @name: the name of the pad to retrieve.
 *
 * Retrieves a pad from @element by name. Tries gst_element_get_static_pad()
 * first, then gst_element_get_request_pad().
 *
 * Returns: the #GstPad if found, otherwise %NULL.
 */
GstPad *
gst_element_get_pad (GstElement * element, const gchar * name)
{
  GstPad *pad;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  pad = gst_element_get_static_pad (element, name);
  if (!pad)
    pad = gst_element_get_request_pad (element, name);

  return pad;
}

static gint
pad_compare_name (GstPad * pad1, const gchar * name)
{
  return strcmp (GST_PAD_NAME (pad1), name);
}

/**
 * gst_element_get_static_pad:
 * @element: a #GstElement to find a static pad of.
 * @name: the name of the static #GstPad to retrieve.
 *
 * Retrieves a pad from @element by name. This version only retrieves
 * already-existing (i.e. 'static') pads.
 *
 * Returns: the requested #GstPad if found, otherwise NULL.
 */
GstPad *
gst_element_get_static_pad (GstElement * element, const gchar * name)
{
  GList *find;
  GstPad *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), result);
  g_return_val_if_fail (name != NULL, result);

  GST_LOCK (element);
  find =
      g_list_find_custom (element->pads, name, (GCompareFunc) pad_compare_name);
  if (find) {
    result = GST_PAD (find->data);
  }
  GST_UNLOCK (element);

  if (result == NULL) {
    GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "no such pad '%s' in element \"%s\"",
        name, GST_OBJECT_NAME (element));
  } else {
    GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "found pad %s:%s",
        GST_DEBUG_PAD_NAME (result));
  }
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

  g_return_val_if_fail (element != NULL, NULL);
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
 * gst_element_get_pad_list:
 * @element: a #GstElement to get pads of.
 *
 * Retrieves a list of @element's pads. The list must not be modified by the
 * calling code.
 *
 * Returns: the #GList of pads.
 */
const GList *
gst_element_get_pad_list (GstElement * element)
{
  const GList *result = NULL;

  g_return_val_if_fail (element != NULL, result);
  g_return_val_if_fail (GST_IS_ELEMENT (element), result);

  g_warning ("calling gst_element_get_pad_list is MT unsafe!!");

  /* return the list of pads */
  result = element->pads;

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
  g_return_val_if_fail (element_class != NULL, NULL);
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

  g_return_val_if_fail (element_class != NULL, NULL);
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

  GST_LOCK (element);
  switch (dir) {
    case GST_PAD_SRC:
      pads = element->srcpads;
    case GST_PAD_SINK:
      pads = element->sinkpads;
    default:
      g_warning ("unknown pad direction");
      return NULL;
  }
  for (; pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD (pads->data);

    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "checking pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    if (GST_PAD_IS_LINKED (pad)) {
      result = pad;
      break;
    } else {
      GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "pad %s:%s is not linked",
          GST_DEBUG_PAD_NAME (pad));
    }
  }
  GST_UNLOCK (element);

  return result;
}

/**
 * gst_element_get_event_masks:
 * @element: a #GstElement to query
 *
 * Get an array of event masks from the element.
 * If the element doesn't implement an event masks function,
 * the query will be forwarded to a random linked sink pad.
 *
 * Returns: An array of #GstEventMask elements.
 */
const GstEventMask *
gst_element_get_event_masks (GstElement * element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_event_masks)
    return oclass->get_event_masks (element);
  else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad)
      return gst_pad_get_event_masks (GST_PAD_PEER (pad));
  }

  return NULL;
}

/**
 * gst_element_send_event:
 * @element: a #GstElement to send the event to.
 * @event: the #GstEvent to send to the element.
 *
 * Sends an event to an element. If the element doesn't
 * implement an event handler, the event will be forwarded
 * to a random sink pad.
 *
 * Returns: TRUE if the event was handled.
 */
gboolean
gst_element_send_event (GstElement * element, GstEvent * event)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->send_event)
    return oclass->send_event (element, event);
  else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad) {
      GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "sending event to random pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      return gst_pad_send_event (GST_PAD_PEER (pad), event);
    }
  }
  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "can't send event on element %s",
      GST_ELEMENT_NAME (element));
  return FALSE;
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
 */
gboolean
gst_element_seek (GstElement * element, GstSeekType seek_type, guint64 offset)
{
  GstEvent *event = gst_event_new_seek (seek_type, offset);

  return gst_element_send_event (element, event);
}

/**
 * gst_element_get_query_types:
 * @element: a #GstElement to query
 *
 * Get an array of query types from the element.
 * If the element doesn't implement a query types function,
 * the query will be forwarded to a random sink pad.
 *
 * Returns: An array of #GstQueryType elements.
 */
const GstQueryType *
gst_element_get_query_types (GstElement * element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_query_types)
    return oclass->get_query_types (element);
  else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad)
      return gst_pad_get_query_types (GST_PAD_PEER (pad));
  }

  return NULL;
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
 */
gboolean
gst_element_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->query)
    return oclass->query (element, type, format, value);
  else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SRC);

    if (pad)
      return gst_pad_query (pad, type, format, value);
    pad = gst_element_get_random_pad (element, GST_PAD_SINK);
    if (pad)
      return gst_pad_query (GST_PAD_PEER (pad), type, format, value);
  }

  return FALSE;
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
 */
const GstFormat *
gst_element_get_formats (GstElement * element)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_formats)
    return oclass->get_formats (element);
  else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad)
      return gst_pad_get_formats (GST_PAD_PEER (pad));
  }

  return NULL;
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
 */
gboolean
gst_element_convert (GstElement * element,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->convert)
    return oclass->convert (element,
        src_format, src_value, dest_format, dest_value);
  else {
    GstPad *pad = gst_element_get_random_pad (element, GST_PAD_SINK);

    if (pad)
      return gst_pad_convert (GST_PAD_PEER (pad),
          src_format, src_value, dest_format, dest_value);
  }

  return FALSE;
}

/* MT safe */
gboolean
gst_element_post_message (GstElement * element, GstMessage * message)
{
  GstPipeline *manager;
  gboolean result = FALSE;

  g_return_val_if_fail (GST_IS_ELEMENT (element), result);
  g_return_val_if_fail (message != NULL, result);

  GST_LOCK (element);
  manager = element->manager;
  if (manager == NULL) {
    GST_UNLOCK (element);
    gst_data_unref (GST_DATA (message));
    return result;
  }
  gst_object_ref (GST_OBJECT (manager));
  GST_UNLOCK (element);

  result = gst_pipeline_post_message (manager, message);
  gst_object_unref (GST_OBJECT (manager));

  return result;
}

/**
 * _gst_element_error_printf:
 * @format: the printf-like format to use, or NULL
 *
 * This function is only used internally by the #gst_element_error macro.
 *
 * Returns: a newly allocated string, or NULL if the format was NULL or ""
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
  gchar *elem_name;

  /* checks */
  g_return_if_fail (GST_IS_ELEMENT (element));

  elem_name = gst_element_get_name (element);

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
      elem_name, sent_message);
  error = g_error_new_literal (domain, code, sent_message);

  /* emit the signal, make sure the element stays available */
  name = gst_object_get_path_string (GST_OBJECT (element));
  if (debug)
    sent_debug = g_strdup_printf ("%s(%d): %s: %s:\n%s",
        file, line, function, name, debug ? debug : "");
  else
    sent_debug = NULL;
  g_free (debug);
  g_free (name);

  gst_element_post_message (element,
      gst_message_new_error (GST_OBJECT (element), error, sent_debug));

  GST_CAT_INFO (GST_CAT_ERROR_SYSTEM, "signalled error in %s: %s",
      elem_name, sent_message);

  /* cleanup */
  g_free (sent_message);
  g_free (elem_name);
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
  /* be careful with the flag tests */
  result = !!GST_FLAG_IS_SET (element, GST_ELEMENT_LOCKED_STATE);
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
 */
gboolean
gst_element_set_locked_state (GstElement * element, gboolean locked_state)
{
  gboolean old;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  GST_LOCK (element);
  old = !!GST_FLAG_IS_SET (element, GST_ELEMENT_LOCKED_STATE);

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

static gboolean
gst_element_get_state_func (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout)
{
  gboolean ret = FALSE;
  GstElementState old_pending;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  GST_STATE_LOCK (element);
  old_pending = GST_STATE_PENDING (element);
  if (old_pending != GST_STATE_VOID_PENDING) {
    if (!GST_STATE_TIMED_WAIT (element, timeout)) {
      /* timeout triggered */
      if (state)
        *state = GST_STATE (element);
      if (pending)
        *pending = GST_STATE_PENDING (element);
      ret = FALSE;
    } else {
      /* could be success or failure, we could check here if the
       * state is equal to the old pending state */
    }
  }
  /* if nothing is pending anymore we can return TRUE and
   * set the values of the current and panding state */
  if (GST_STATE_PENDING (element) == GST_STATE_VOID_PENDING) {
    if (state)
      *state = GST_STATE (element);
    if (pending)
      *pending = GST_STATE_VOID_PENDING;
    ret = TRUE;
  }
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
 *           state change.
 *
 * Gets the state of the element.
 *
 * Returns: TRUE if the element has no more pending state, FALSE
 *          if the element is still performing a state change.
 */
gboolean
gst_element_get_state (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout)
{
  GstElementClass *oclass;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->get_state)
    return (oclass->get_state) (element, state, pending, timeout);

  return FALSE;
}

/**
 * gst_element_abort_state:
 * @element: a #GstElement to abort the state of.
 *
 * Abort the state change of the element. This function is used
 * by elements that do asynchronous state changes and find out 
 * something is wrong.
 */
void
gst_element_abort_state (GstElement * element)
{
  GstElementState pending;

  g_return_if_fail (GST_IS_ELEMENT (element));

  pending = GST_STATE_PENDING (element);

  if (pending != GST_STATE_VOID_PENDING) {
    GstElementState old_state = GST_STATE (element);

    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
        "aborting state from %s to %s", gst_element_state_get_name (old_state),
        gst_element_state_get_name (pending));

    GST_STATE_PENDING (element) = GST_STATE_VOID_PENDING;

    g_cond_broadcast (GST_STATE_GET_COND (element));
  }
}

/**
 * gst_element_commit_state:
 * @element: a #GstElement to commit the state of.
 *
 * Commit the state change of the element. This function is used
 * by elements that do asynchronous state changes.
 */
void
gst_element_commit_state (GstElement * element)
{
  GstElementState pending;

  g_return_if_fail (GST_IS_ELEMENT (element));

  pending = GST_STATE_PENDING (element);

  if (pending != GST_STATE_VOID_PENDING) {
    GstElementState old_state = GST_STATE (element);

    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
        "commiting state from %s to %s", gst_element_state_get_name (old_state),
        gst_element_state_get_name (pending));

    GST_STATE (element) = pending;
    GST_STATE_PENDING (element) = GST_STATE_VOID_PENDING;

    g_signal_emit (G_OBJECT (element), gst_element_signals[STATE_CHANGE],
        0, old_state, pending);
    g_cond_broadcast (GST_STATE_GET_COND (element));
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
 * Returns: TRUE if the state was successfully set.
 * (using #GstElementStateReturn).
 */
GstElementStateReturn
gst_element_set_state (GstElement * element, GstElementState state)
{
  GstElementClass *oclass;
  GstElementState current;
  GstElementStateReturn return_val = GST_STATE_SUCCESS;

  oclass = GST_ELEMENT_GET_CLASS (element);

  /* reentrancy issues with signals in change_state) */
  gst_object_ref (GST_OBJECT (element));

  /* get the element state lock */
  GST_STATE_LOCK (element);

  /* start with the current state */
  current = GST_STATE (element);

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "setting state from %s to %s",
      gst_element_state_get_name (current), gst_element_state_get_name (state));

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

    if (pending != state) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
          "intermediate: setting state from %s to %s",
          gst_element_state_get_name (current),
          gst_element_state_get_name (pending));
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
          "final: setting state from %s to %s",
          gst_element_state_get_name (current),
          gst_element_state_get_name (pending));
    }

    /* call the state change function so it can set the state */
    if (oclass->change_state)
      return_val = (oclass->change_state) (element);
    else
      return_val = GST_STATE_FAILURE;

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
            "element changed state successfuly");
        /* we can commit the state now and proceed to the next state */
        gst_element_commit_state (element);
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "commited state");
        break;
      default:
        /* somebody added a GST_STATE_ and forgot to do stuff here ! */
        g_assert_not_reached ();
    }
    /* get the current state of the element and see if we need to do more
     * state changes */
    current = GST_STATE (element);
  }
  while (current != state);

exit:
  GST_STATE_UNLOCK (element);

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "exit state change");

  gst_object_unref (GST_OBJECT (element));

  return return_val;
}

static gboolean
gst_element_pads_activate (GstElement * element, gboolean active)
{
  GList *pads = element->pads;
  gboolean result = TRUE;

  while (pads && result) {
    GstPad *pad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    if (!GST_IS_REAL_PAD (pad))
      continue;

    result &= gst_pad_set_active (pad, active);
  }

  return result;
}

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
      if (GST_ELEMENT_MANAGER (element)) {
        element->base_time =
            GST_ELEMENT (GST_ELEMENT_MANAGER (element))->base_time;
      }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      element->base_time = 0;
      if (!gst_element_pads_activate (element, FALSE)) {
        result = GST_STATE_FAILURE;
      }
      break;
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

  gst_element_set_state (element, GST_STATE_NULL);

  /* first we break all our links with the ouside */
  while (element->pads) {
    gst_element_remove_pad (element, GST_PAD (element->pads->data));
  }

  element->numsrcpads = 0;
  element->numsinkpads = 0;
  element->numpads = 0;
  if (element->state_cond)
    g_cond_free (element->state_cond);
  element->state_cond = NULL;

  gst_object_replace ((GstObject **) & element->manager, NULL);
  gst_object_replace ((GstObject **) & element->clock, NULL);

  GST_CAT_INFO_OBJECT (GST_CAT_REFCOUNTING, element, "dispose parent");

  G_OBJECT_CLASS (parent_class)->dispose (object);
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

static void
gst_element_set_manager_func (GstElement * element, GstPipeline * manager)
{
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element, "setting manager to %p",
      manager);

  GST_LOCK (element);
  gst_object_replace ((GstObject **) & GST_ELEMENT_MANAGER (element),
      GST_OBJECT (manager));
  GST_UNLOCK (element);
}

/**
 * gst_element_set_manager:
 * @element: a #GstElement to set the manager of.
 * @manager: the #GstManager to set.
 *
 * Sets the manager of the element.  For internal use only, unless you're
 * writing a new bin subclass.
 */
void
gst_element_set_manager (GstElement * element, GstPipeline * manager)
{
  GstElementClass *oclass;

  g_return_if_fail (GST_IS_ELEMENT (element));

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->set_manager)
    oclass->set_manager (element, manager);
}

/**
 * gst_element_get_manager:
 * @element: a #GstElement to get the manager of.
 *
 * Returns the manager of the element.
 *
 * Returns: the element's #GstPipeline.
 *
 * MT safe.
 */
GstPipeline *
gst_element_get_manager (GstElement * element)
{
  GstPipeline *result = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), result);

  GST_LOCK (element);
  result = GST_ELEMENT_MANAGER (element);
  GST_UNLOCK (element);

  return result;
}

/**
 * gst_element_create_task:
 * @element: a #GstElement to get the manager of.
 * @func: the taskfunction to run
 * @data: user data passed to the taskfunction.
 *
 * Creates a new GstTask.
 *
 * Returns: the newly created #GstTask.
 *
 * MT safe.
 */
GstTask *
gst_element_create_task (GstElement * element, GstTaskFunction func,
    gpointer data)
{
  GstPipeline *pipeline;
  GstTask *result = NULL;

  GST_LOCK (element);
  pipeline = GST_ELEMENT_MANAGER (element);
  gst_object_ref (GST_OBJECT (pipeline));
  GST_UNLOCK (element);
  if (pipeline) {
    result = gst_scheduler_create_task (pipeline->scheduler, func, data);
    gst_object_unref (GST_OBJECT (pipeline));
  }

  return result;
}
