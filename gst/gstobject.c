/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstobject.c: Fundamental class used for all of GStreamer
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
 * SECTION:gstobject
 * @short_description: Base class for the GStreamer object hierarchy
 *
 * #GstObject provides a root for the object hierarchy tree filed in by the
 * GStreamer library.  It is currently a thin wrapper on top of
 * #GObject. It is an abstract class that is not very usable on its own.
 *
 * #GstObject gives us basic refcounting, parenting functionality and locking.
 * Most of the function are just extended for special GStreamer needs and can be
 * found under the same name in the base class of #GstObject which is #GObject
 * (e.g. g_object_ref() becomes gst_object_ref()).
 *
 * The most interesting difference between #GstObject and #GObject is the
 * "floating" reference count. A #GObject is created with a reference count of
 * 1, owned by the creator of the #GObject. (The owner of a reference is the
 * code section that has the right to call gst_object_unref() in order to
 * remove that reference.) A #GstObject is created with a reference count of 1
 * also, but it isn't owned by anyone; Instead, the initial reference count
 * of a #GstObject is "floating". The floating reference can be removed by
 * anyone at any time, by calling gst_object_sink().  gst_object_sink() does
 * nothing if an object is already sunk (has no floating reference).
 *
 * When you add a #GstElement to its parent container, the parent container will
 * do this:
 * <informalexample>
 * <programlisting>
 *   gst_object_ref (GST_OBJECT (child_element));
 *   gst_object_sink (GST_OBJECT (child_element));
 * </programlisting>
 * </informalexample>
 * This means that the container now owns a reference to the child element
 * (since it called gst_object_ref()), and the child element has no floating
 * reference.
 *
 * The purpose of the floating reference is to keep the child element alive
 * until you add it to a parent container, which then manages the lifetime of
 * the object itself:
 * <informalexample>
 * <programlisting>
 *    element = gst_element_factory_make (factoryname, name);
 *    // element has one floating reference to keep it alive
 *    gst_bin_add (GST_BIN (bin), element);
 *    // element has one non-floating reference owned by the container
 * </programlisting>
 * </informalexample>
 *
 * Another effect of this is, that calling gst_object_unref() on a bin object,
 * will also destoy all the #GstElement objects in it. The same is true for
 * calling gst_bin_remove().
 *
 * Special care has to be taken for all methods that gst_object_sink() an object
 * since if the caller of those functions had a floating reference to the object,
 * the object reference is now invalid.
 *
 * In contrast to #GObject instances, #GstObject adds a name property. The functions
 * gst_object_set_name() and gst_object_get_name() are used to set/get the name
 * of the object.
 *
 * Last reviewed on 2005-11-09 (0.9.4)
 */

#include "gst_private.h"

#include "gstobject.h"
#include "gstmarshal.h"
#include "gstinfo.h"
#include "gstutils.h"

#ifndef GST_DISABLE_TRACE
#include "gsttrace.h"
static GstAllocTrace *_gst_object_trace;
#endif

#define DEBUG_REFCOUNT

/* Object signals and args */
enum
{
  DEEP_NOTIFY,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_NAME,
  PROP_PARENT,
  PROP_LAST
};

enum
{
  SO_OBJECT_LOADED,
  SO_LAST_SIGNAL
};

/* maps type name quark => count */
static GData *object_name_counts = NULL;

G_LOCK_DEFINE_STATIC (object_name_mutex);

static void gst_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_object_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_object_dispatch_properties_changed (GObject * object,
    guint n_pspecs, GParamSpec ** pspecs);

static void gst_object_dispose (GObject * object);
static void gst_object_finalize (GObject * object);

static gboolean gst_object_set_name_default (GstObject * object);

static guint gst_object_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[PROP_LAST];

G_DEFINE_ABSTRACT_TYPE (GstObject, gst_object, G_TYPE_INITIALLY_UNOWNED);

static void
gst_object_class_init (GstObjectClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

#ifndef GST_DISABLE_TRACE
  _gst_object_trace = gst_alloc_trace_register (g_type_name (GST_TYPE_OBJECT));
#endif

  gobject_class->set_property = gst_object_set_property;
  gobject_class->get_property = gst_object_get_property;

  properties[PROP_NAME] =
      g_param_spec_string ("name", "Name", "The name of the object", NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_NAME,
      properties[PROP_NAME]);

  properties[PROP_PARENT] =
      g_param_spec_object ("parent", "Parent", "The parent of the object",
      GST_TYPE_OBJECT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_PARENT,
      properties[PROP_PARENT]);

  /**
   * GstObject::deep-notify:
   * @gstobject: a #GstObject
   * @prop_object: the object that originated the signal
   * @prop: the property that changed
   *
   * The deep notify signal is used to be notified of property changes. It is
   * typically attached to the toplevel bin to receive notifications from all
   * the elements contained in that bin.
   */
  gst_object_signals[DEEP_NOTIFY] =
      g_signal_new ("deep-notify", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED |
      G_SIGNAL_NO_HOOKS, G_STRUCT_OFFSET (GstObjectClass, deep_notify), NULL,
      NULL, gst_marshal_VOID__OBJECT_PARAM, G_TYPE_NONE, 2, GST_TYPE_OBJECT,
      G_TYPE_PARAM);

  klass->path_string_separator = "/";

  /* see the comments at gst_object_dispatch_properties_changed */
  gobject_class->dispatch_properties_changed
      = GST_DEBUG_FUNCPTR (gst_object_dispatch_properties_changed);

  gobject_class->dispose = gst_object_dispose;
  gobject_class->finalize = gst_object_finalize;
}

static void
gst_object_init (GstObject * object)
{
  object->lock = g_mutex_new ();
  object->parent = NULL;
  object->name = NULL;
  GST_CAT_TRACE_OBJECT (GST_CAT_REFCOUNTING, object, "%p new", object);

#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_object_trace, object);
#endif

  object->flags = 0;
}

/**
 * gst_object_ref:
 * @object: a #GstObject to reference
 *
 * Increments the reference count on @object. This function
 * does not take the lock on @object because it relies on
 * atomic refcounting.
 *
 * This object returns the input parameter to ease writing
 * constructs like :
 *  result = gst_object_ref (object->parent);
 *
 * Returns: (transfer full): A pointer to @object
 */
gpointer
gst_object_ref (gpointer object)
{
  g_return_val_if_fail (object != NULL, NULL);

#ifdef DEBUG_REFCOUNT
  GST_CAT_TRACE_OBJECT (GST_CAT_REFCOUNTING, object, "%p ref %d->%d", object,
      ((GObject *) object)->ref_count, ((GObject *) object)->ref_count + 1);
#endif
  g_object_ref (object);

  return object;
}

/**
 * gst_object_unref:
 * @object: a #GstObject to unreference
 *
 * Decrements the reference count on @object.  If reference count hits
 * zero, destroy @object. This function does not take the lock
 * on @object as it relies on atomic refcounting.
 *
 * The unref method should never be called with the LOCK held since
 * this might deadlock the dispose function.
 */
void
gst_object_unref (gpointer object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (((GObject *) object)->ref_count > 0);

#ifdef DEBUG_REFCOUNT
  GST_CAT_TRACE_OBJECT (GST_CAT_REFCOUNTING, object, "%p unref %d->%d", object,
      ((GObject *) object)->ref_count, ((GObject *) object)->ref_count - 1);
#endif
  g_object_unref (object);
}

/**
 * gst_object_ref_sink:
 * @object: a #GstObject to sink
 *
 * Increase the reference count of @object, and possibly remove the floating
 * reference, if @object has a floating reference.
 *
 * In other words, if the object is floating, then this call "assumes ownership"
 * of the floating reference, converting it to a normal reference by clearing
 * the floating flag while leaving the reference count unchanged. If the object
 * is not floating, then this call adds a new normal reference increasing the
 * reference count by one.
 */
gpointer
gst_object_ref_sink (gpointer object)
{
  g_return_val_if_fail (object != NULL, NULL);

#ifdef DEBUG_REFCOUNT
  GST_CAT_TRACE_OBJECT (GST_CAT_REFCOUNTING, object, "%p ref_sink %d->%d",
      object, ((GObject *) object)->ref_count,
      ((GObject *) object)->ref_count + 1);
#endif
  return g_object_ref_sink (object);
}

/**
 * gst_object_replace:
 * @oldobj: (inout) (transfer full): pointer to a place of a #GstObject to
 *     replace
 * @newobj: (transfer none): a new #GstObject
 *
 * Atomically modifies a pointer to point to a new object.
 * The reference count of @oldobj is decreased and the reference count of
 * @newobj is increased.
 *
 * Either @newobj and the value pointed to by @oldobj may be NULL.
 *
 * Returns: TRUE if @newobj was different from @oldobj
 */
gboolean
gst_object_replace (GstObject ** oldobj, GstObject * newobj)
{
  GstObject *oldptr;

  g_return_val_if_fail (oldobj != NULL, FALSE);

#ifdef DEBUG_REFCOUNT
  GST_CAT_TRACE (GST_CAT_REFCOUNTING, "replace %p %s (%d) with %p %s (%d)",
      *oldobj, *oldobj ? GST_STR_NULL (GST_OBJECT_NAME (*oldobj)) : "(NONE)",
      *oldobj ? G_OBJECT (*oldobj)->ref_count : 0,
      newobj, newobj ? GST_STR_NULL (GST_OBJECT_NAME (newobj)) : "(NONE)",
      newobj ? G_OBJECT (newobj)->ref_count : 0);
#endif

  oldptr = g_atomic_pointer_get ((gpointer *) oldobj);

  if (G_UNLIKELY (oldptr == newobj))
    return FALSE;

  if (newobj)
    g_object_ref (newobj);

  while (G_UNLIKELY (!g_atomic_pointer_compare_and_exchange ((gpointer *)
              oldobj, oldptr, newobj))) {
    oldptr = g_atomic_pointer_get ((gpointer *) oldobj);
    if (G_UNLIKELY (oldptr == newobj))
      break;
  }

  if (oldptr)
    g_object_unref (oldptr);

  return oldptr != newobj;
}

/* dispose is called when the object has to release all links
 * to other objects */
static void
gst_object_dispose (GObject * object)
{
  GstObject *parent;

  GST_CAT_TRACE_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  GST_OBJECT_LOCK (object);
  if ((parent = GST_OBJECT_PARENT (object)))
    goto have_parent;
  GST_OBJECT_PARENT (object) = NULL;
  GST_OBJECT_UNLOCK (object);

  ((GObjectClass *) gst_object_parent_class)->dispose (object);

  return;

  /* ERRORS */
have_parent:
  {
    g_critical ("\nTrying to dispose object \"%s\", but it still has a "
        "parent \"%s\".\nYou need to let the parent manage the "
        "object instead of unreffing the object directly.\n",
        GST_OBJECT_NAME (object), GST_OBJECT_NAME (parent));
    GST_OBJECT_UNLOCK (object);
    /* ref the object again to revive it in this error case */
    gst_object_ref (object);
    return;
  }
}

/* finalize is called when the object has to free its resources */
static void
gst_object_finalize (GObject * object)
{
  GstObject *gstobject = GST_OBJECT_CAST (object);

  GST_CAT_TRACE_OBJECT (GST_CAT_REFCOUNTING, object, "finalize");

  g_signal_handlers_destroy (object);

  g_free (gstobject->name);
  g_mutex_free (gstobject->lock);

#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_gst_object_trace, object);
#endif

  ((GObjectClass *) gst_object_parent_class)->finalize (object);
}

/* Changing a GObject property of a GstObject will result in "deep-notify"
 * signals being emitted by the object itself, as well as in each parent
 * object. This is so that an application can connect a listener to the
 * top-level bin to catch property-change notifications for all contained
 * elements.
 *
 * MT safe.
 */
static void
gst_object_dispatch_properties_changed (GObject * object,
    guint n_pspecs, GParamSpec ** pspecs)
{
  GstObject *gst_object, *parent, *old_parent;
  guint i;
#ifndef GST_DISABLE_GST_DEBUG
  gchar *name = NULL;
  const gchar *debug_name;
#endif

  /* do the standard dispatching */
  ((GObjectClass *)
      gst_object_parent_class)->dispatch_properties_changed (object, n_pspecs,
      pspecs);

  gst_object = GST_OBJECT_CAST (object);
#ifndef GST_DISABLE_GST_DEBUG
  if (G_UNLIKELY (__gst_debug_min >= GST_LEVEL_LOG)) {
    name = gst_object_get_name (gst_object);
    debug_name = GST_STR_NULL (name);
  } else
    debug_name = "";
#endif

  /* now let the parent dispatch those, too */
  parent = gst_object_get_parent (gst_object);
  while (parent) {
    for (i = 0; i < n_pspecs; i++) {
      GST_CAT_LOG_OBJECT (GST_CAT_PROPERTIES, parent,
          "deep notification from %s (%s)", debug_name, pspecs[i]->name);

      g_signal_emit (parent, gst_object_signals[DEEP_NOTIFY],
          g_quark_from_string (pspecs[i]->name), gst_object, pspecs[i]);
    }

    old_parent = parent;
    parent = gst_object_get_parent (old_parent);
    gst_object_unref (old_parent);
  }
#ifndef GST_DISABLE_GST_DEBUG
  g_free (name);
#endif
}

/**
 * gst_object_default_deep_notify:
 * @object: the #GObject that signalled the notify.
 * @orig: a #GstObject that initiated the notify.
 * @pspec: a #GParamSpec of the property.
 * @excluded_props: (array zero-terminated=1) (element-type gchar*)
 *     (allow-none):a set of user-specified properties to exclude or
 *     NULL to show all changes.
 *
 * A default deep_notify signal callback for an object. The user data
 * should contain a pointer to an array of strings that should be excluded
 * from the notify. The default handler will print the new value of the property
 * using g_print.
 *
 * MT safe. This function grabs and releases @object's LOCK for getting its
 *          path string.
 */
void
gst_object_default_deep_notify (GObject * object, GstObject * orig,
    GParamSpec * pspec, gchar ** excluded_props)
{
  GValue value = { 0, };        /* the important thing is that value.type = 0 */
  gchar *str = NULL;
  gchar *name = NULL;

  if (pspec->flags & G_PARAM_READABLE) {
    /* let's not print these out for excluded properties... */
    while (excluded_props != NULL && *excluded_props != NULL) {
      if (strcmp (pspec->name, *excluded_props) == 0)
        return;
      excluded_props++;
    }
    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);

    /* FIXME: handle flags */
    if (G_IS_PARAM_SPEC_ENUM (pspec)) {
      GEnumValue *enum_value;
      GEnumClass *klass = G_ENUM_CLASS (g_type_class_ref (pspec->value_type));

      enum_value = g_enum_get_value (klass, g_value_get_enum (&value));

      str = g_strdup_printf ("%s (%d)", enum_value->value_nick,
          enum_value->value);
      g_type_class_unref (klass);
    } else {
      str = g_strdup_value_contents (&value);
    }
    name = gst_object_get_path_string (orig);
    g_print ("%s: %s = %s\n", name, pspec->name, str);
    g_free (name);
    g_free (str);
    g_value_unset (&value);
  } else {
    name = gst_object_get_path_string (orig);
    g_warning ("Parameter %s not readable in %s.", pspec->name, name);
    g_free (name);
  }
}

static gboolean
gst_object_set_name_default (GstObject * object)
{
  const gchar *type_name;
  gint count;
  gchar *name;
  GQuark q;
  guint i, l;

  /* to ensure guaranteed uniqueness across threads, only one thread
   * may ever assign a name */
  G_LOCK (object_name_mutex);

  if (!object_name_counts) {
    g_datalist_init (&object_name_counts);
  }

  q = g_type_qname (G_OBJECT_TYPE (object));
  count = GPOINTER_TO_INT (g_datalist_id_get_data (&object_name_counts, q));
  g_datalist_id_set_data (&object_name_counts, q, GINT_TO_POINTER (count + 1));

  G_UNLOCK (object_name_mutex);

  /* GstFooSink -> foosink<N> */
  type_name = g_quark_to_string (q);
  if (strncmp (type_name, "Gst", 3) == 0)
    type_name += 3;
  name = g_strdup_printf ("%s%d", type_name, count);
  l = strlen (name);
  for (i = 0; i < l; i++)
    name[i] = g_ascii_tolower (name[i]);

  GST_OBJECT_LOCK (object);
  if (G_UNLIKELY (object->parent != NULL))
    goto had_parent;

  g_free (object->name);
  object->name = name;

  GST_OBJECT_UNLOCK (object);

  return TRUE;

had_parent:
  {
    g_free (name);
    GST_WARNING ("parented objects can't be renamed");
    GST_OBJECT_UNLOCK (object);
    return FALSE;
  }
}

/**
 * gst_object_set_name:
 * @object: a #GstObject
 * @name:   new name of object
 *
 * Sets the name of @object, or gives @object a guaranteed unique
 * name (if @name is NULL).
 * This function makes a copy of the provided name, so the caller
 * retains ownership of the name it sent.
 *
 * Returns: TRUE if the name could be set. Since Objects that have
 * a parent cannot be renamed, this function returns FALSE in those
 * cases.
 *
 * MT safe.  This function grabs and releases @object's LOCK.
 */
gboolean
gst_object_set_name (GstObject * object, const gchar * name)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_OBJECT (object), FALSE);

  GST_OBJECT_LOCK (object);

  /* parented objects cannot be renamed */
  if (G_UNLIKELY (object->parent != NULL))
    goto had_parent;

  if (name != NULL) {
    g_free (object->name);
    object->name = g_strdup (name);
    GST_OBJECT_UNLOCK (object);
    result = TRUE;
  } else {
    GST_OBJECT_UNLOCK (object);
    result = gst_object_set_name_default (object);
  }
  /* FIXME-0.11: this misses a g_object_notify (object, "name"); unless called
   * from gst_object_set_property.
   * Ideally remove such custom setters (or make it static).
   */
  return result;

  /* error */
had_parent:
  {
    GST_WARNING ("parented objects can't be renamed");
    GST_OBJECT_UNLOCK (object);
    return FALSE;
  }
}

/**
 * gst_object_get_name:
 * @object: a #GstObject
 *
 * Returns a copy of the name of @object.
 * Caller should g_free() the return value after usage.
 * For a nameless object, this returns NULL, which you can safely g_free()
 * as well.
 *
 * Free-function: g_free
 *
 * Returns: (transfer full): the name of @object. g_free() after usage.
 *
 * MT safe. This function grabs and releases @object's LOCK.
 */
gchar *
gst_object_get_name (GstObject * object)
{
  gchar *result = NULL;

  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

  GST_OBJECT_LOCK (object);
  result = g_strdup (object->name);
  GST_OBJECT_UNLOCK (object);

  return result;
}

/**
 * gst_object_set_parent:
 * @object: a #GstObject
 * @parent: new parent of object
 *
 * Sets the parent of @object to @parent. The object's reference count will
 * be incremented, and any floating reference will be removed (see gst_object_ref_sink()).
 *
 * Returns: TRUE if @parent could be set or FALSE when @object
 * already had a parent or @object and @parent are the same.
 *
 * MT safe. Grabs and releases @object's LOCK.
 */
gboolean
gst_object_set_parent (GstObject * object, GstObject * parent)
{
  g_return_val_if_fail (GST_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (GST_IS_OBJECT (parent), FALSE);
  g_return_val_if_fail (object != parent, FALSE);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object,
      "set parent (ref and sink)");

  GST_OBJECT_LOCK (object);
  if (G_UNLIKELY (object->parent != NULL))
    goto had_parent;

  object->parent = parent;
  g_object_ref_sink (object);
  GST_OBJECT_UNLOCK (object);

  /* FIXME, this does not work, the deep notify takes the lock from the parent
   * object and deadlocks when the parent holds its lock when calling this
   * function (like _element_add_pad()) */
  /* g_object_notify_by_pspec ((GObject *)object, properties[PROP_PARENT]); */

  return TRUE;

  /* ERROR handling */
had_parent:
  {
    GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object,
        "set parent failed, object already had a parent");
    GST_OBJECT_UNLOCK (object);
    return FALSE;
  }
}

/**
 * gst_object_get_parent:
 * @object: a #GstObject
 *
 * Returns the parent of @object. This function increases the refcount
 * of the parent object so you should gst_object_unref() it after usage.
 *
 * Returns: (transfer full): parent of @object, this can be NULL if @object
 *   has no parent. unref after usage.
 *
 * MT safe. Grabs and releases @object's LOCK.
 */
GstObject *
gst_object_get_parent (GstObject * object)
{
  GstObject *result = NULL;

  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

  GST_OBJECT_LOCK (object);
  result = object->parent;
  if (G_LIKELY (result))
    gst_object_ref (result);
  GST_OBJECT_UNLOCK (object);

  return result;
}

/**
 * gst_object_unparent:
 * @object: a #GstObject to unparent
 *
 * Clear the parent of @object, removing the associated reference.
 * This function decreases the refcount of @object.
 *
 * MT safe. Grabs and releases @object's lock.
 */
void
gst_object_unparent (GstObject * object)
{
  GstObject *parent;

  g_return_if_fail (GST_IS_OBJECT (object));

  GST_OBJECT_LOCK (object);
  parent = object->parent;

  if (G_LIKELY (parent != NULL)) {
    GST_CAT_TRACE_OBJECT (GST_CAT_REFCOUNTING, object, "unparent");
    object->parent = NULL;
    GST_OBJECT_UNLOCK (object);

    /* g_object_notify_by_pspec ((GObject *)object, properties[PROP_PARENT]); */

    gst_object_unref (object);
  } else {
    GST_OBJECT_UNLOCK (object);
  }
}

/**
 * gst_object_has_ancestor:
 * @object: a #GstObject to check
 * @ancestor: a #GstObject to check as ancestor
 *
 * Check if @object has an ancestor @ancestor somewhere up in
 * the hierarchy. One can e.g. check if a #GstElement is inside a #GstPipeline.
 *
 * Returns: TRUE if @ancestor is an ancestor of @object.
 *
 * MT safe. Grabs and releases @object's locks.
 */
gboolean
gst_object_has_ancestor (GstObject * object, GstObject * ancestor)
{
  GstObject *parent, *tmp;

  if (!ancestor || !object)
    return FALSE;

  parent = gst_object_ref (object);
  do {
    if (parent == ancestor) {
      gst_object_unref (parent);
      return TRUE;
    }

    tmp = gst_object_get_parent (parent);
    gst_object_unref (parent);
    parent = tmp;
  } while (parent);

  return FALSE;
}

/**
 * gst_object_check_uniqueness:
 * @list: (transfer none) (element-type Gst.Object): a list of #GstObject to
 *      check through
 * @name: the name to search for
 *
 * Checks to see if there is any object named @name in @list. This function
 * does not do any locking of any kind. You might want to protect the
 * provided list with the lock of the owner of the list. This function
 * will lock each #GstObject in the list to compare the name, so be
 * carefull when passing a list with a locked object.
 *
 * Returns: TRUE if a #GstObject named @name does not appear in @list,
 * FALSE if it does.
 *
 * MT safe. Grabs and releases the LOCK of each object in the list.
 */
gboolean
gst_object_check_uniqueness (GList * list, const gchar * name)
{
  gboolean result = TRUE;

  g_return_val_if_fail (name != NULL, FALSE);

  for (; list; list = g_list_next (list)) {
    GstObject *child;
    gboolean eq;

    child = GST_OBJECT_CAST (list->data);

    GST_OBJECT_LOCK (child);
    eq = strcmp (GST_OBJECT_NAME (child), name) == 0;
    GST_OBJECT_UNLOCK (child);

    if (G_UNLIKELY (eq)) {
      result = FALSE;
      break;
    }
  }
  return result;
}


static void
gst_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstObject *gstobject;

  gstobject = GST_OBJECT_CAST (object);

  switch (prop_id) {
    case PROP_NAME:
      gst_object_set_name (gstobject, g_value_get_string (value));
      break;
    case PROP_PARENT:
      gst_object_set_parent (gstobject, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_object_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstObject *gstobject;

  gstobject = GST_OBJECT_CAST (object);

  switch (prop_id) {
    case PROP_NAME:
      g_value_take_string (value, gst_object_get_name (gstobject));
      break;
    case PROP_PARENT:
      g_value_take_object (value, gst_object_get_parent (gstobject));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_object_get_path_string:
 * @object: a #GstObject
 *
 * Generates a string describing the path of @object in
 * the object hierarchy. Only useful (or used) for debugging.
 *
 * Free-function: g_free
 *
 * Returns: (transfer full): a string describing the path of @object. You must
 *          g_free() the string after usage.
 *
 * MT safe. Grabs and releases the #GstObject's LOCK for all objects
 *          in the hierarchy.
 */
gchar *
gst_object_get_path_string (GstObject * object)
{
  GSList *parentage;
  GSList *parents;
  void *parent;
  gchar *prevpath, *path;
  const gchar *typename;
  gchar *component;
  const gchar *separator;

  /* ref object before adding to list */
  gst_object_ref (object);
  parentage = g_slist_prepend (NULL, object);

  path = g_strdup ("");

  /* first walk the object hierarchy to build a list of the parents,
   * be carefull here with refcounting. */
  do {
    if (GST_IS_OBJECT (object)) {
      parent = gst_object_get_parent (object);
      /* add parents to list, refcount remains increased while
       * we handle the object */
      if (parent)
        parentage = g_slist_prepend (parentage, parent);
    } else {
      break;
    }
    object = parent;
  } while (object != NULL);

  /* then walk the parent list and print them out. we need to
   * decrease the refcounting on each element after we handled
   * it. */
  for (parents = parentage; parents; parents = g_slist_next (parents)) {
    if (G_IS_OBJECT (parents->data)) {
      typename = G_OBJECT_TYPE_NAME (parents->data);
    } else {
      typename = NULL;
    }
    if (GST_IS_OBJECT (parents->data)) {
      GstObject *item = GST_OBJECT_CAST (parents->data);
      GstObjectClass *oclass = GST_OBJECT_GET_CLASS (item);
      gchar *objname = gst_object_get_name (item);

      component = g_strdup_printf ("%s:%s", typename, objname);
      separator = oclass->path_string_separator;
      /* and unref now */
      gst_object_unref (item);
      g_free (objname);
    } else {
      if (typename) {
        component = g_strdup_printf ("%s:%p", typename, parents->data);
      } else {
        component = g_strdup_printf ("%p", parents->data);
      }
      separator = "/";
    }

    prevpath = path;
    path = g_strjoin (separator, prevpath, component, NULL);
    g_free (prevpath);
    g_free (component);
  }

  g_slist_free (parentage);

  return path;
}
