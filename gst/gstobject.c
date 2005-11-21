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
#ifndef GST_HAVE_GLIB_2_8
#define REFCOUNT_HACK
#endif

/* Refcount hack: since glib < 2.8 is not threadsafe, the glib refcounter can be
 * screwed up and the object can be freed unexpectedly. We use an evil hack
 * to work around this problem. We set the glib refcount to a high value so
 * that glib will never unref the object under realistic circumstances. Then
 * we use our own atomic refcounting to do proper MT safe refcounting.
 *
 * The hack has several side-effect. At first you should use
 * gst_object_ref/unref() whenever you can. Next when using
 * g_value_set/get_object(); you need to manually fix the refcount.
 *
 * A proper fix is of course to upgrade to glib 2.8
 */
#ifdef REFCOUNT_HACK
#define PATCH_REFCOUNT(obj)    ((GObject*)(obj))->ref_count = 100000;
#define PATCH_REFCOUNT1(obj)    ((GObject*)(obj))->ref_count = 1;
#else
#define PATCH_REFCOUNT(obj)
#define PATCH_REFCOUNT1(obj)
#endif

/* Object signals and args */
enum
{
  PARENT_SET,
  PARENT_UNSET,
#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  OBJECT_SAVED,
#endif
  DEEP_NOTIFY,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_NAME
      /* FILL ME */
};

enum
{
  SO_OBJECT_LOADED,
  SO_LAST_SIGNAL
};

static GHashTable *object_name_counts = NULL;

G_LOCK_DEFINE_STATIC (object_name_mutex);

typedef struct _GstSignalObject GstSignalObject;
typedef struct _GstSignalObjectClass GstSignalObjectClass;

static GType gst_signal_object_get_type (void);
static void gst_signal_object_class_init (GstSignalObjectClass * klass);
static void gst_signal_object_init (GstSignalObject * object);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
static guint gst_signal_object_signals[SO_LAST_SIGNAL] = { 0 };
#endif

static void gst_object_class_init (GstObjectClass * klass);
static void gst_object_init (GTypeInstance * instance, gpointer g_class);

static void gst_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_object_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_object_dispatch_properties_changed (GObject * object,
    guint n_pspecs, GParamSpec ** pspecs);

static void gst_object_dispose (GObject * object);
static void gst_object_finalize (GObject * object);

static gboolean gst_object_set_name_default (GstObject * object,
    const gchar * type_name);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
static void gst_object_real_restore_thyself (GstObject * object,
    xmlNodePtr self);
#endif

static GObjectClass *parent_class = NULL;
static guint gst_object_signals[LAST_SIGNAL] = { 0 };

GType
gst_object_get_type (void)
{
  static GType gst_object_type = 0;

  if (!gst_object_type) {
    static const GTypeInfo object_info = {
      sizeof (GstObjectClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_object_class_init,
      NULL,
      NULL,
      sizeof (GstObject),
      0,
      gst_object_init,
      NULL
    };

    gst_object_type =
        g_type_register_static (G_TYPE_OBJECT, "GstObject", &object_info,
        G_TYPE_FLAG_ABSTRACT);
  }
  return gst_object_type;
}

static void
gst_object_class_init (GstObjectClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

#ifndef GST_DISABLE_TRACE
  _gst_object_trace = gst_alloc_trace_register (g_type_name (GST_TYPE_OBJECT));
#endif

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_object_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_object_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NAME,
      g_param_spec_string ("name", "Name", "The name of the object",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GstObject::parent-set:
   * @gstobject: a #GstObject
   * @parent: the new parent
   *
   * Emitted when the parent of an object is set.
   */
  gst_object_signals[PARENT_SET] =
      g_signal_new ("parent-set", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstObjectClass, parent_set), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  /**
   * GstObject::parent-unset:
   * @gstobject: a #GstObject
   * @parent: the old parent
   *
   * Emitted when the parent of an object is unset.
   */
  gst_object_signals[PARENT_UNSET] =
      g_signal_new ("parent-unset", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstObjectClass, parent_unset), NULL,
      NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  /**
   * GstObject::object-saved:
   * @gstobject: a #GstObject
   * @xml_node: the xmlNodePtr of the parent node
   *
   * Trigered whenever a new object is saved to XML. You can connect to this
   * signal to insert custom XML tags into the core XML.
   */
  /* FIXME This should be the GType of xmlNodePtr instead of G_TYPE_POINTER
   *       (if libxml would use GObject)
   */
  gst_object_signals[OBJECT_SAVED] =
      g_signal_new ("object-saved", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstObjectClass, object_saved), NULL,
      NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

  klass->restore_thyself = gst_object_real_restore_thyself;
#endif

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
      NULL, gst_marshal_VOID__OBJECT_PARAM, G_TYPE_NONE, 2, G_TYPE_OBJECT,
      G_TYPE_PARAM);

  klass->path_string_separator = "/";
  klass->lock = g_new0 (GStaticRecMutex, 1);
  g_static_rec_mutex_init (klass->lock);

  klass->signal_object = g_object_new (gst_signal_object_get_type (), NULL);

  /* see the comments at gst_object_dispatch_properties_changed */
  gobject_class->dispatch_properties_changed
      = GST_DEBUG_FUNCPTR (gst_object_dispatch_properties_changed);

  gobject_class->dispose = gst_object_dispose;
  gobject_class->finalize = gst_object_finalize;
}

static void
gst_object_init (GTypeInstance * instance, gpointer g_class)
{
  GstObject *object = GST_OBJECT (instance);

  object->lock = g_mutex_new ();
  object->parent = NULL;
  object->name = NULL;
  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "%p new", object);
#ifdef REFCOUNT_HACK
  gst_atomic_int_set (&object->refcount, 1);
#endif
  PATCH_REFCOUNT (object);

#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_object_trace, object);
#endif

  object->flags = 0;
  GST_OBJECT_FLAG_SET (object, GST_OBJECT_FLOATING);
}

/**
 * gst_object_ref:
 * @object: a #GstObject to reference
 *
 * Increments the refence count on @object. This function
 * does not take the lock on @object because it relies on
 * atomic refcounting.
 *
 * This object returns the input parameter to ease writing
 * constructs like :
 *  result = gst_object_ref (object->parent);
 *
 * Returns: A pointer to @object
 */
gpointer
gst_object_ref (gpointer object)
{
#ifdef REFCOUNT_HACK
  gint old;
#endif

  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

#ifdef REFCOUNT_HACK
  old = g_atomic_int_exchange_and_add (&((GstObject *) object)->refcount, 1);
#  ifdef DEBUG_REFCOUNT
  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "%p ref %d->%d",
      object, old, old + 1);
#  endif
  PATCH_REFCOUNT (object);
#else
#  ifdef DEBUG_REFCOUNT
  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "%p ref %d->%d",
      object,
      ((GObject *) object)->ref_count, ((GObject *) object)->ref_count + 1);
#  endif
  g_object_ref (object);
#endif

  return object;
}

/**
 * gst_object_unref:
 * @object: a #GstObject to unreference
 *
 * Decrements the refence count on @object.  If reference count hits
 * zero, destroy @object. This function does not take the lock
 * on @object as it relies on atomic refcounting.
 *
 * The unref method should never be called with the LOCK held since
 * this might deadlock the dispose function.
 */
void
gst_object_unref (gpointer object)
{
#ifdef REFCOUNT_HACK
  gint old;
#endif
  g_return_if_fail (GST_IS_OBJECT (object));

#ifdef REFCOUNT_HACK
  g_return_if_fail (GST_OBJECT_REFCOUNT_VALUE (object) > 0);

  old = g_atomic_int_exchange_and_add (&((GstObject *) object)->refcount, -1);

#  ifdef DEBUG_REFCOUNT
  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "%p unref %d->%d",
      object, old, old - 1);
#endif
  if (G_UNLIKELY (old == 1)) {
    PATCH_REFCOUNT1 (object);
    g_object_unref (object);
  } else {
    PATCH_REFCOUNT (object);
  }
#else
  g_return_if_fail (((GObject *) object)->ref_count > 0);

#ifdef DEBUG_REFCOUNT
  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "%p unref %d->%d",
      object,
      ((GObject *) object)->ref_count, ((GObject *) object)->ref_count - 1);
#endif
  g_object_unref (object);
#endif
}

/**
 * gst_object_sink:
 * @object: a #GstObject to sink
 *
 * If @object was floating, the #GST_OBJECT_FLOATING flag is removed 
 * and @object is unreffed. When @object was not floating,
 * this function does nothing.
 *
 * Any newly created object has a refcount of 1 and is floating. 
 * This function should be used when creating a new object to 
 * symbolically 'take ownership' of @object. This done by first doing a
 * gst_object_ref() to keep a reference to @object and then gst_object_sink()
 * to remove and unref any floating references to @object.
 * Use gst_object_set_parent() to have this done for you.
 *
 * MT safe. This function grabs and releases @object lock.
 */
void
gst_object_sink (gpointer object)
{
  g_return_if_fail (GST_IS_OBJECT (object));

  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "sink");

  GST_OBJECT_LOCK (object);
  if (G_LIKELY (GST_OBJECT_IS_FLOATING (object))) {
    GST_OBJECT_FLAG_UNSET (object, GST_OBJECT_FLOATING);
    GST_OBJECT_UNLOCK (object);
    gst_object_unref (object);
  } else {
    GST_OBJECT_UNLOCK (object);
  }
}

/**
 * gst_object_replace:
 * @oldobj: pointer to a place of a #GstObject to replace
 * @newobj: a new #GstObject
 *
 * Unrefs the #GstObject pointed to by @oldobj, refs @newobj and
 * puts @newobj in *@oldobj. Be carefull when calling this
 * function, it does not take any locks. You might want to lock
 * the object owning @oldobj pointer before calling this
 * function.
 *
 * Make sure not to LOCK @oldobj because it might be unreffed
 * which could cause a deadlock when it is disposed.
 */
void
gst_object_replace (GstObject ** oldobj, GstObject * newobj)
{
  g_return_if_fail (oldobj != NULL);
  g_return_if_fail (*oldobj == NULL || GST_IS_OBJECT (*oldobj));
  g_return_if_fail (newobj == NULL || GST_IS_OBJECT (newobj));

#ifdef DEBUG_REFCOUNT
#ifdef REFCOUNT_HACK
  GST_CAT_LOG (GST_CAT_REFCOUNTING, "replace %s (%d) with %s (%d)",
      *oldobj ? GST_STR_NULL (GST_OBJECT_NAME (*oldobj)) : "(NONE)",
      *oldobj ? GST_OBJECT_REFCOUNT_VALUE (*oldobj) : 0,
      newobj ? GST_STR_NULL (GST_OBJECT_NAME (newobj)) : "(NONE)",
      newobj ? GST_OBJECT_REFCOUNT_VALUE (newobj) : 0);
#else
  GST_CAT_LOG (GST_CAT_REFCOUNTING, "replace %s (%d) with %s (%d)",
      *oldobj ? GST_STR_NULL (GST_OBJECT_NAME (*oldobj)) : "(NONE)",
      *oldobj ? G_OBJECT (*oldobj)->ref_count : 0,
      newobj ? GST_STR_NULL (GST_OBJECT_NAME (newobj)) : "(NONE)",
      newobj ? G_OBJECT (newobj)->ref_count : 0);
#endif
#endif

  if (G_LIKELY (*oldobj != newobj)) {
    if (newobj)
      gst_object_ref (newobj);
    if (*oldobj)
      gst_object_unref (*oldobj);

    *oldobj = newobj;
  }
}

/* dispose is called when the object has to release all links
 * to other objects */
static void
gst_object_dispose (GObject * object)
{
  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  GST_OBJECT_LOCK (object);
  GST_OBJECT_PARENT (object) = NULL;
  GST_OBJECT_UNLOCK (object);

  /* need to patch refcount so it is finalized */
  PATCH_REFCOUNT1 (object);

  parent_class->dispose (object);
}

/* finalize is called when the object has to free its resources */
static void
gst_object_finalize (GObject * object)
{
  GstObject *gstobject = GST_OBJECT (object);

  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "finalize");

  g_signal_handlers_destroy (object);

  g_free (gstobject->name);
  g_mutex_free (gstobject->lock);

#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_gst_object_trace, object);
#endif

  parent_class->finalize (object);
}

/* Changing a GObject property of a GstObject will result in "deep_notify"
 * signals being emitted by the object itself, as well as in each parent
 * object. This is so that an application can connect a listener to the
 * top-level bin to catch property-change notifications for all contained
 * elements.
 *
 * This function is not MT safe in glib < 2.8 so we need to lock it with a
 * classwide mutex in that case.
 *
 * MT safe.
 */
static void
gst_object_dispatch_properties_changed (GObject * object,
    guint n_pspecs, GParamSpec ** pspecs)
{
  GstObject *gst_object, *parent, *old_parent;
  guint i;
  gchar *name, *debug_name;
  GstObjectClass *klass;

  /* we fail when this is not a GstObject */
  g_return_if_fail (GST_IS_OBJECT (object));

  klass = GST_OBJECT_GET_CLASS (object);

#ifndef GST_HAVE_GLIB_2_8
  GST_CLASS_LOCK (klass);
#endif

  /* do the standard dispatching */
  PATCH_REFCOUNT (object);
  G_OBJECT_CLASS (parent_class)->dispatch_properties_changed (object, n_pspecs,
      pspecs);
  PATCH_REFCOUNT (object);

  gst_object = GST_OBJECT_CAST (object);
  name = gst_object_get_name (gst_object);
  debug_name = GST_STR_NULL (name);

  /* now let the parent dispatch those, too */
  parent = gst_object_get_parent (gst_object);
  while (parent) {
    /* for debugging ... */
    gchar *parent_name = gst_object_get_name (parent);

#ifndef GST_DISABLE_GST_DEBUG
    gchar *debug_parent_name = GST_STR_NULL (parent_name);
#endif

    /* need own category? */
    for (i = 0; i < n_pspecs; i++) {
      GST_CAT_LOG (GST_CAT_EVENT, "deep notification from %s to %s (%s)",
          debug_name, debug_parent_name, pspecs[i]->name);

      /* not MT safe because of glib, fixed by taking class lock higher up */
      PATCH_REFCOUNT (parent);
      PATCH_REFCOUNT (object);
      g_signal_emit (parent, gst_object_signals[DEEP_NOTIFY],
          g_quark_from_string (pspecs[i]->name), GST_OBJECT_CAST (object),
          pspecs[i]);
      PATCH_REFCOUNT (parent);
      PATCH_REFCOUNT (object);
    }
    g_free (parent_name);

    old_parent = parent;
    parent = gst_object_get_parent (old_parent);
    gst_object_unref (old_parent);
  }
  g_free (name);

#ifndef GST_HAVE_GLIB_2_8
  GST_CLASS_UNLOCK (klass);
#endif
}

/**
 * gst_object_default_deep_notify:
 * @object: the #GObject that signalled the notify.
 * @orig: a #GstObject that initiated the notify.
 * @pspec: a #GParamSpec of the property.
 * @excluded_props: a set of user-specified properties to exclude or
 *  NULL to show all changes.
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
    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);

    if (G_IS_PARAM_SPEC_ENUM (pspec)) {
      GEnumValue *enum_value;

      enum_value =
          g_enum_get_value (G_ENUM_CLASS (g_type_class_ref (pspec->value_type)),
          g_value_get_enum (&value));

      str = g_strdup_printf ("%s (%d)", enum_value->value_nick,
          enum_value->value);
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
gst_object_set_name_default (GstObject * object, const gchar * type_name)
{
  gint count;
  gchar *name, *tmp;
  gboolean result;

  /* to ensure guaranteed uniqueness across threads, only one thread
   * may ever assign a name */
  G_LOCK (object_name_mutex);

  if (!object_name_counts) {
    object_name_counts = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, NULL);
  }

  count = GPOINTER_TO_INT (g_hash_table_lookup (object_name_counts, type_name));
  g_hash_table_insert (object_name_counts, g_strdup (type_name),
      GINT_TO_POINTER (count + 1));

  G_UNLOCK (object_name_mutex);

  /* GstFooSink -> foosinkN */
  if (strncmp (type_name, "Gst", 3) == 0)
    type_name += 3;
  tmp = g_strdup_printf ("%s%d", type_name, count);
  name = g_ascii_strdown (tmp, strlen (tmp));
  g_free (tmp);

  result = gst_object_set_name (object, name);
  g_free (name);

  return result;
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
    result = gst_object_set_name_default (object, G_OBJECT_TYPE_NAME (object));
  }
  return result;

  /* error */
had_parent:
  {
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
 * Returns: the name of @object. g_free() after usage.
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
 * gst_object_set_name_prefix:
 * @object:      a #GstObject 
 * @name_prefix: new name prefix of @object
 *
 * Sets the name prefix of @object to @name_prefix.
 * This function makes a copy of the provided name prefix, so the caller
 * retains ownership of the name prefix it sent.
 *
 * MT safe.  This function grabs and releases @object's LOCK.
 */
void
gst_object_set_name_prefix (GstObject * object, const gchar * name_prefix)
{
  g_return_if_fail (GST_IS_OBJECT (object));

  GST_OBJECT_LOCK (object);
  g_free (object->name_prefix);
  object->name_prefix = g_strdup (name_prefix); /* NULL gives NULL */
  GST_OBJECT_UNLOCK (object);
}

/**
 * gst_object_get_name_prefix:
 * @object: a #GstObject 
 *
 * Returns a copy of the name prefix of @object.
 * Caller should g_free() the return value after usage.
 * For a prefixless object, this returns NULL, which you can safely g_free()
 * as well.
 *
 * Returns: the name prefix of @object. g_free() after usage.
 *
 * MT safe. This function grabs and releases @object's LOCK.
 */
gchar *
gst_object_get_name_prefix (GstObject * object)
{
  gchar *result = NULL;

  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

  GST_OBJECT_LOCK (object);
  result = g_strdup (object->name_prefix);
  GST_OBJECT_UNLOCK (object);

  return result;
}

/**
 * gst_object_set_parent:
 * @object: a #GstObject 
 * @parent: new parent of object
 *
 * Sets the parent of @object to @parent. The object's reference count will 
 * be incremented, and any floating reference will be removed (see gst_object_sink()).
 *
 * This function causes the parent-set signal to be emitted when the parent
 * was successfully set.
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

  /* sink object, we don't call our own function because we don't
   * need to release/acquire the lock needlessly or touch the refcount
   * in the floating case. */
  object->parent = parent;
  if (G_LIKELY (GST_OBJECT_IS_FLOATING (object))) {
    GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "unsetting floating flag");
    GST_OBJECT_FLAG_UNSET (object, GST_OBJECT_FLOATING);
    GST_OBJECT_UNLOCK (object);
  } else {
    GST_OBJECT_UNLOCK (object);
    gst_object_ref (object);
  }

  g_signal_emit (G_OBJECT (object), gst_object_signals[PARENT_SET], 0, parent);

  return TRUE;

  /* ERROR handling */
had_parent:
  {
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
 * Returns: parent of @object, this can be NULL if @object has no
 *   parent. unref after usage.
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
    GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "unparent");
    object->parent = NULL;
    GST_OBJECT_UNLOCK (object);

    g_signal_emit (G_OBJECT (object), gst_object_signals[PARENT_UNSET], 0,
        parent);

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
 * the hierarchy.
 *
 * Returns: TRUE if @ancestor is an ancestor of @object.
 *
 * MT safe. Grabs and releases @object's locks.
 */
gboolean
gst_object_has_ancestor (GstObject * object, GstObject * ancestor)
{
  GstObject *parent;
  gboolean result = FALSE;

  if (object == NULL)
    return FALSE;

  if (object == ancestor)
    return TRUE;

  parent = gst_object_get_parent (object);
  result = gst_object_has_ancestor (parent, ancestor);
  if (parent)
    gst_object_unref (parent);

  return result;
}

/**
 * gst_object_check_uniqueness:
 * @list: a list of #GstObject to check through
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

    child = GST_OBJECT (list->data);

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


#ifndef GST_DISABLE_LOADSAVE_REGISTRY
/**
 * gst_object_save_thyself:
 * @object: a #GstObject to save
 * @parent: The parent XML node to save @object into
 *
 * Saves @object into the parent XML node.
 *
 * Returns: the new xmlNodePtr with the saved object
 */
xmlNodePtr
gst_object_save_thyself (GstObject * object, xmlNodePtr parent)
{
  GstObjectClass *oclass;

  g_return_val_if_fail (GST_IS_OBJECT (object), parent);
  g_return_val_if_fail (parent != NULL, parent);

  oclass = GST_OBJECT_GET_CLASS (object);

  if (oclass->save_thyself)
    oclass->save_thyself (object, parent);

  g_signal_emit (G_OBJECT (object), gst_object_signals[OBJECT_SAVED], 0,
      parent);

  return parent;
}

/**
 * gst_object_restore_thyself:
 * @object: a #GstObject to load into
 * @self: The XML node to load @object from
 *
 * Restores @object with the data from the parent XML node.
 */
void
gst_object_restore_thyself (GstObject * object, xmlNodePtr self)
{
  GstObjectClass *oclass;

  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (self != NULL);

  oclass = GST_OBJECT_GET_CLASS (object);

  if (oclass->restore_thyself)
    oclass->restore_thyself (object, self);
}

static void
gst_object_real_restore_thyself (GstObject * object, xmlNodePtr self)
{
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (self != NULL);

  gst_class_signal_emit_by_name (object, "object_loaded", self);
}
#endif /* GST_DISABLE_LOADSAVE_REGISTRY */

static void
gst_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstObject *gstobject;

  gstobject = GST_OBJECT (object);

  switch (prop_id) {
    case ARG_NAME:
      gst_object_set_name (gstobject, g_value_get_string (value));
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

  gstobject = GST_OBJECT (object);

  switch (prop_id) {
    case ARG_NAME:
      g_value_take_string (value, gst_object_get_name (gstobject));
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
 * Returns: a string describing the path of @object. You must
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
  gchar *component;
  gchar *separator;

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
    if (GST_IS_OBJECT (parents->data)) {
      GstObject *item = GST_OBJECT_CAST (parents->data);
      GstObjectClass *oclass = GST_OBJECT_GET_CLASS (item);

      component = gst_object_get_name (item);
      separator = oclass->path_string_separator;
      /* and unref now */
      gst_object_unref (item);
    } else {
      component = g_strdup_printf ("%p", parents->data);
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

struct _GstSignalObject
{
  GObject object;
};

struct _GstSignalObjectClass
{
  GObjectClass parent_class;

  /* signals */
#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  void (*object_loaded) (GstSignalObject * object, GstObject * new,
      xmlNodePtr self);
#endif                          /* GST_DISABLE_LOADSAVE_REGISTRY */
};

static GType
gst_signal_object_get_type (void)
{
  static GType signal_object_type = 0;

  if (!signal_object_type) {
    static const GTypeInfo signal_object_info = {
      sizeof (GstSignalObjectClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_signal_object_class_init,
      NULL,
      NULL,
      sizeof (GstSignalObject),
      0,
      (GInstanceInitFunc) gst_signal_object_init,
      NULL
    };

    signal_object_type =
        g_type_register_static (G_TYPE_OBJECT, "GstSignalObject",
        &signal_object_info, 0);
  }
  return signal_object_type;
}

static void
gst_signal_object_class_init (GstSignalObjectClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  gst_signal_object_signals[SO_OBJECT_LOADED] =
      g_signal_new ("object-loaded", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSignalObjectClass, object_loaded),
      NULL, NULL, gst_marshal_VOID__OBJECT_POINTER, G_TYPE_NONE, 2,
      G_TYPE_OBJECT, G_TYPE_POINTER);
#endif
}

static void
gst_signal_object_init (GstSignalObject * object)
{
}

/**
 * gst_class_signal_connect
 * @klass: a #GstObjectClass to attach the signal to
 * @name: the name of the signal to attach to
 * @func: the signal function
 * @func_data: a pointer to user data
 *
 * Connect to a class signal.
 *
 * Returns: the signal id.
 */
guint
gst_class_signal_connect (GstObjectClass * klass,
    const gchar * name, gpointer func, gpointer func_data)
{
  return g_signal_connect (klass->signal_object, name, func, func_data);
}

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
/**
 * gst_class_signal_emit_by_name:
 * @object: a #GstObject that emits the signal
 * @name: the name of the signal to emit
 * @self: data for the signal
 *
 * emits the named class signal.
 */
void
gst_class_signal_emit_by_name (GstObject * object,
    const gchar * name, xmlNodePtr self)
{
  GstObjectClass *oclass;

  oclass = GST_OBJECT_GET_CLASS (object);

  g_signal_emit_by_name (oclass->signal_object, name, object, self);
}

#endif /* GST_DISABLE_LOADSAVE_REGISTRY */
