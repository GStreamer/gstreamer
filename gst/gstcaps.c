/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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
 * SECTION:gstcaps
 * @short_description: Structure describing sets of media formats
 * @see_also: #GstStructure
 *
 * Caps (capabilities) are lighweight refcounted objects describing media types.
 * They are composed of an array of #GstStructure.
 *
 * Caps are exposed on #GstPadTemplate to describe all possible types a
 * given pad can handle. They are also stored in the #GstRegistry along with
 * a description of the #GstElement.
 *
 * Caps are exposed on the element pads using the gst_pad_get_caps() pad
 * function. This function describes the possible types that the pad can
 * handle or produce at runtime.
 *
 * Caps are also attached to buffers to describe to content of the data
 * pointed to by the buffer with gst_buffer_set_caps(). Caps attached to
 * a #GstBuffer allow for format negotiation upstream and downstream.
 *
 * A #GstCaps can be constructed with the following code fragment:
 *
 * <example>
 *  <title>Creating caps</title>
 *  <programlisting>
 *  GstCaps *caps;
 *  caps = gst_caps_new_simple ("video/x-raw-yuv",
 *       "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
 *       "framerate", GST_TYPE_FRACTION, 25, 1,
 *       "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
 *       "width", G_TYPE_INT, 320,
 *       "height", G_TYPE_INT, 240,
 *       NULL);
 *  </programlisting>
 * </example>
 *
 * A #GstCaps is fixed when it has no properties with ranges or lists. Use
 * gst_caps_is_fixed() to test for fixed caps. Only fixed caps can be
 * set on a #GstPad or #GstBuffer.
 *
 * Various methods exist to work with the media types such as subtracting
 * or intersecting.
 *
 * Last reviewed on 2007-02-13 (0.10.10)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <signal.h>

#include "gst_private.h"
#include <gst/gst.h>
#include <gobject/gvaluecollector.h>

#define DEBUG_REFCOUNT


#define GST_CAPS_ARRAY(c) ((GPtrArray *)((c)->priv))

#define GST_CAPS_LEN(c)   (GST_CAPS_ARRAY(c)->len)

#define IS_WRITABLE(caps) \
  (GST_CAPS_REFCOUNT_VALUE (caps) == 1)

/* same as gst_caps_is_any () */
#define CAPS_IS_ANY(caps)				\
  (GST_CAPS_FLAGS(caps) & GST_CAPS_FLAGS_ANY)

/* same as gst_caps_is_empty () */
#define CAPS_IS_EMPTY(caps)				\
  (!CAPS_IS_ANY(caps) && CAPS_IS_EMPTY_SIMPLE(caps))

#define CAPS_IS_EMPTY_SIMPLE(caps)					\
  ((GST_CAPS_ARRAY (caps) == NULL) || (GST_CAPS_LEN (caps) == 0))

/* quick way to get a caps structure at an index without doing a type or array
 * length check */
#define gst_caps_get_structure_unchecked(caps, index) \
     ((GstStructure *)g_ptr_array_index (GST_CAPS_ARRAY (caps), (index)))
/* quick way to append a structure without checking the args */
#define gst_caps_append_structure_unchecked(caps, structure) G_STMT_START{\
  GstStructure *__s=structure;                                      \
  if (gst_structure_set_parent_refcount (__s, &GST_MINI_OBJECT_REFCOUNT(caps)))         \
    g_ptr_array_add (GST_CAPS_ARRAY (caps), __s);                             \
}G_STMT_END

/* lock to protect multiple invocations of static caps to caps conversion */
G_LOCK_DEFINE_STATIC (static_caps_lock);

static void gst_caps_transform_to_string (const GValue * src_value,
    GValue * dest_value);
static gboolean gst_caps_from_string_inplace (GstCaps * caps,
    const gchar * string);

GType _gst_caps_type = 0;

void
_gst_caps_initialize (void)
{
  _gst_caps_type = gst_mini_object_register ("GstCaps");

  g_value_register_transform_func (_gst_caps_type,
      G_TYPE_STRING, gst_caps_transform_to_string);
}

static GstCaps *
_gst_caps_copy (const GstCaps * caps)
{
  GstCaps *newcaps;
  GstStructure *structure;
  guint i, n;

  g_return_val_if_fail (GST_IS_CAPS (caps), NULL);

  newcaps = gst_caps_new_empty ();
  GST_CAPS_FLAGS (newcaps) = GST_CAPS_FLAGS (caps);
  n = GST_CAPS_LEN (caps);

  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure_unchecked (caps, i);
    gst_caps_append_structure (newcaps, gst_structure_copy (structure));
  }

  return newcaps;
}

/* creation/deletion */
static void
_gst_caps_free (GstCaps * caps)
{
  GstStructure *structure;
  guint i, len;

  /* The refcount must be 0, but since we're only called by gst_caps_unref,
   * don't bother testing. */
  len = GST_CAPS_LEN (caps);
  /* This can be used to get statistics about caps sizes */
  /*GST_CAT_INFO (GST_CAT_CAPS, "caps size: %d", len); */
  for (i = 0; i < len; i++) {
    structure = (GstStructure *) gst_caps_get_structure_unchecked (caps, i);
    gst_structure_set_parent_refcount (structure, NULL);
    gst_structure_free (structure);
  }
  g_ptr_array_free (GST_CAPS_ARRAY (caps), TRUE);

#ifdef DEBUG_REFCOUNT
  GST_CAT_LOG (GST_CAT_CAPS, "freeing caps %p", caps);
#endif
  g_slice_free1 (GST_MINI_OBJECT_SIZE (caps), caps);
}

static void
gst_caps_init (GstCaps * caps, gsize size)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (caps), _gst_caps_type, size);

  caps->mini_object.copy = (GstMiniObjectCopyFunction) _gst_caps_copy;
  caps->mini_object.dispose = NULL;
  caps->mini_object.free = (GstMiniObjectFreeFunction) _gst_caps_free;

  /* the 32 has been determined by logging caps sizes in _gst_caps_free
   * but g_ptr_array uses 16 anyway if it expands once, so this does not help
   * in practise
   * GST_CAPS_ARRAY (caps) = g_ptr_array_sized_new (32);
   */
  caps->priv = g_ptr_array_new ();
}

/**
 * gst_caps_new_empty:
 *
 * Creates a new #GstCaps that is empty.  That is, the returned
 * #GstCaps contains no media formats.
 * Caller is responsible for unreffing the returned caps.
 *
 * Returns: (transfer full): the new #GstCaps
 */
GstCaps *
gst_caps_new_empty (void)
{
  GstCaps *caps;

  caps = g_slice_new (GstCaps);

  gst_caps_init (caps, sizeof (GstCaps));

#ifdef DEBUG_REFCOUNT
  GST_CAT_LOG (GST_CAT_CAPS, "created caps %p", caps);
#endif

  return caps;
}

/**
 * gst_caps_new_any:
 *
 * Creates a new #GstCaps that indicates that it is compatible with
 * any media format.
 *
 * Returns: (transfer full): the new #GstCaps
 */
GstCaps *
gst_caps_new_any (void)
{
  GstCaps *caps = gst_caps_new_empty ();

  GST_CAPS_FLAG_SET (caps, GST_CAPS_FLAGS_ANY);

  return caps;
}

/**
 * gst_caps_new_simple:
 * @media_type: the media type of the structure
 * @fieldname: first field to set
 * @...: additional arguments
 *
 * Creates a new #GstCaps that contains one #GstStructure.  The
 * structure is defined by the arguments, which have the same format
 * as gst_structure_new().
 * Caller is responsible for unreffing the returned caps.
 *
 * Returns: (transfer full): the new #GstCaps
 */
GstCaps *
gst_caps_new_simple (const char *media_type, const char *fieldname, ...)
{
  GstCaps *caps;
  GstStructure *structure;
  va_list var_args;

  caps = gst_caps_new_empty ();

  va_start (var_args, fieldname);
  structure = gst_structure_new_valist (media_type, fieldname, var_args);
  va_end (var_args);

  if (structure)
    gst_caps_append_structure_unchecked (caps, structure);
  else
    gst_caps_replace (&caps, NULL);

  return caps;
}

/**
 * gst_caps_new_full:
 * @struct1: the first structure to add
 * @...: additional structures to add
 *
 * Creates a new #GstCaps and adds all the structures listed as
 * arguments.  The list must be NULL-terminated.  The structures
 * are not copied; the returned #GstCaps owns the structures.
 *
 * Returns: (transfer full): the new #GstCaps
 */
GstCaps *
gst_caps_new_full (GstStructure * struct1, ...)
{
  GstCaps *caps;
  va_list var_args;

  va_start (var_args, struct1);
  caps = gst_caps_new_full_valist (struct1, var_args);
  va_end (var_args);

  return caps;
}

/**
 * gst_caps_new_full_valist:
 * @structure: the first structure to add
 * @var_args: additional structures to add
 *
 * Creates a new #GstCaps and adds all the structures listed as
 * arguments.  The list must be NULL-terminated.  The structures
 * are not copied; the returned #GstCaps owns the structures.
 *
 * Returns: (transfer full): the new #GstCaps
 */
GstCaps *
gst_caps_new_full_valist (GstStructure * structure, va_list var_args)
{
  GstCaps *caps;

  caps = gst_caps_new_empty ();

  while (structure) {
    gst_caps_append_structure_unchecked (caps, structure);
    structure = va_arg (var_args, GstStructure *);
  }

  return caps;
}

GType
gst_static_caps_get_type (void)
{
  static GType staticcaps_type = 0;

  if (G_UNLIKELY (staticcaps_type == 0)) {
    staticcaps_type = g_pointer_type_register_static ("GstStaticCaps");
  }
  return staticcaps_type;
}


/**
 * gst_static_caps_get:
 * @static_caps: the #GstStaticCaps to convert
 *
 * Converts a #GstStaticCaps to a #GstCaps.
 *
 * Returns: (transfer full): a pointer to the #GstCaps. Unref after usage.
 *     Since the core holds an additional ref to the returned caps,
 *     use gst_caps_make_writable() on the returned caps to modify it.
 */
GstCaps *
gst_static_caps_get (GstStaticCaps * static_caps)
{
  GstCaps *caps;

  g_return_val_if_fail (static_caps != NULL, NULL);

  caps = (GstCaps *) static_caps;

  /* refcount is 0 when we need to convert */
  if (G_UNLIKELY (GST_CAPS_REFCOUNT_VALUE (caps) == 0)) {
    const char *string;
    GstCaps temp;

    G_LOCK (static_caps_lock);
    /* check if other thread already updated */
    if (G_UNLIKELY (GST_CAPS_REFCOUNT_VALUE (caps) > 0))
      goto done;

    string = static_caps->string;

    if (G_UNLIKELY (string == NULL))
      goto no_string;

    GST_CAT_LOG (GST_CAT_CAPS, "creating %p", static_caps);

    /* we construct the caps on the stack, then copy over the struct into our
     * real caps, refcount last. We do this because we must leave the refcount
     * of the result caps to 0 so that other threads don't run away with the
     * caps while we are constructing it. */
    gst_caps_init (&temp, sizeof (GstCaps));

    /* convert to string */
    if (G_UNLIKELY (!gst_caps_from_string_inplace (&temp, string)))
      g_critical ("Could not convert static caps \"%s\"", string);

    gst_caps_init (caps, sizeof (GstCaps));
    /* now copy stuff over to the real caps. */
    GST_CAPS_FLAGS (caps) = GST_CAPS_FLAGS (&temp);
    caps->priv = GST_CAPS_ARRAY (&temp);

    GST_CAT_LOG (GST_CAT_CAPS, "created %p", static_caps);
  done:
    G_UNLOCK (static_caps_lock);
  }
  /* ref the caps, makes it not writable */
  gst_caps_ref (caps);

  return caps;

  /* ERRORS */
no_string:
  {
    G_UNLOCK (static_caps_lock);
    g_warning ("static caps %p string is NULL", static_caps);
    return NULL;
  }
}

/**
 * gst_static_caps_cleanup:
 * @static_caps: the #GstStaticCaps to convert
 *
 * Clean up the caps contained in @static_caps when the refcount is 0.
 */
void
gst_static_caps_cleanup (GstStaticCaps * static_caps)
{
  GstCaps *caps = (GstCaps *) static_caps;

  /* FIXME: this is not threadsafe */
  if (GST_CAPS_REFCOUNT_VALUE (caps) == 1) {
    GstStructure *structure;
    guint i, clen;

    clen = GST_CAPS_LEN (caps);

    for (i = 0; i < clen; i++) {
      structure = (GstStructure *) gst_caps_get_structure (caps, i);
      gst_structure_set_parent_refcount (structure, NULL);
      gst_structure_free (structure);
    }
    g_ptr_array_free (GST_CAPS_ARRAY (caps), TRUE);
    GST_CAPS_REFCOUNT (caps) = 0;
  }
}

/* manipulation */

static GstStructure *
gst_caps_remove_and_get_structure (GstCaps * caps, guint idx)
{
  /* don't use index_fast, gst_caps_do_simplify relies on the order */
  GstStructure *s = g_ptr_array_remove_index (GST_CAPS_ARRAY (caps), idx);

  gst_structure_set_parent_refcount (s, NULL);
  return s;
}

/**
 * gst_caps_steal_structure:
 * @caps: the #GstCaps to retrieve from
 * @index: Index of the structure to retrieve
 *
 * Retrieves the stucture with the given index from the list of structures
 * contained in @caps. The caller becomes the owner of the returned structure.
 *
 * Returns: (transfer full): a pointer to the #GstStructure corresponding
 *     to @index.
 *
 * Since: 0.10.30
 */
GstStructure *
gst_caps_steal_structure (GstCaps * caps, guint index)
{
  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (IS_WRITABLE (caps), NULL);

  if (G_UNLIKELY (index >= GST_CAPS_LEN (caps)))
    return NULL;

  return gst_caps_remove_and_get_structure (caps, index);
}

/**
 * gst_caps_append:
 * @caps1: the #GstCaps that will be appended to
 * @caps2: (transfer full): the #GstCaps to append
 *
 * Appends the structures contained in @caps2 to @caps1. The structures in
 * @caps2 are not copied -- they are transferred to @caps1, and then @caps2 is
 * freed. If either caps is ANY, the resulting caps will be ANY.
 */
void
gst_caps_append (GstCaps * caps1, GstCaps * caps2)
{
  GstStructure *structure;
  int i;

  g_return_if_fail (GST_IS_CAPS (caps1));
  g_return_if_fail (GST_IS_CAPS (caps2));
  g_return_if_fail (IS_WRITABLE (caps1));
  g_return_if_fail (IS_WRITABLE (caps2));

  if (G_UNLIKELY (CAPS_IS_ANY (caps1) || CAPS_IS_ANY (caps2))) {
    /* FIXME: this leaks */
    GST_CAPS_FLAGS (caps1) |= GST_CAPS_FLAGS_ANY;
    for (i = GST_CAPS_LEN (caps2) - 1; i >= 0; i--) {
      structure = gst_caps_remove_and_get_structure (caps2, i);
      gst_structure_free (structure);
    }
  } else {
    for (i = GST_CAPS_LEN (caps2); i; i--) {
      structure = gst_caps_remove_and_get_structure (caps2, 0);
      gst_caps_append_structure_unchecked (caps1, structure);
    }
  }
  gst_caps_unref (caps2);       /* guaranteed to free it */
}

/**
 * gst_caps_merge:
 * @caps1: the #GstCaps that will take the new entries
 * @caps2: (transfer full): the #GstCaps to merge in
 *
 * Appends the structures contained in @caps2 to @caps1 if they are not yet
 * expressed by @caps1. The structures in @caps2 are not copied -- they are
 * transferred to @caps1, and then @caps2 is freed.
 * If either caps is ANY, the resulting caps will be ANY.
 *
 * Since: 0.10.10
 */
void
gst_caps_merge (GstCaps * caps1, GstCaps * caps2)
{
  GstStructure *structure;
  int i;

  g_return_if_fail (GST_IS_CAPS (caps1));
  g_return_if_fail (GST_IS_CAPS (caps2));
  g_return_if_fail (IS_WRITABLE (caps1));
  g_return_if_fail (IS_WRITABLE (caps2));

  if (G_UNLIKELY (CAPS_IS_ANY (caps1))) {
    for (i = GST_CAPS_LEN (caps2) - 1; i >= 0; i--) {
      structure = gst_caps_remove_and_get_structure (caps2, i);
      gst_structure_free (structure);
    }
  } else if (G_UNLIKELY (CAPS_IS_ANY (caps2))) {
    GST_CAPS_FLAGS (caps1) |= GST_CAPS_FLAGS_ANY;
    for (i = GST_CAPS_LEN (caps1) - 1; i >= 0; i--) {
      structure = gst_caps_remove_and_get_structure (caps1, i);
      gst_structure_free (structure);
    }
  } else {
    for (i = GST_CAPS_LEN (caps2); i; i--) {
      structure = gst_caps_remove_and_get_structure (caps2, 0);
      gst_caps_merge_structure (caps1, structure);
    }
    /* this is too naive
       GstCaps *com = gst_caps_intersect (caps1, caps2);
       GstCaps *add = gst_caps_subtract (caps2, com);

       GST_DEBUG ("common : %d", gst_caps_get_size (com));
       GST_DEBUG ("adding : %d", gst_caps_get_size (add));
       gst_caps_append (caps1, add);
       gst_caps_unref (com);
     */
  }
  gst_caps_unref (caps2);       /* guaranteed to free it */
}

/**
 * gst_caps_append_structure:
 * @caps: the #GstCaps that will be appended to
 * @structure: (transfer full): the #GstStructure to append
 *
 * Appends @structure to @caps.  The structure is not copied; @caps
 * becomes the owner of @structure.
 */
void
gst_caps_append_structure (GstCaps * caps, GstStructure * structure)
{
  g_return_if_fail (GST_IS_CAPS (caps));
  g_return_if_fail (IS_WRITABLE (caps));

  if (G_LIKELY (structure)) {
    gst_caps_append_structure_unchecked (caps, structure);
  }
}

/**
 * gst_caps_remove_structure:
 * @caps: the #GstCaps to remove from
 * @idx: Index of the structure to remove
 *
 * removes the stucture with the given index from the list of structures
 * contained in @caps.
 */
void
gst_caps_remove_structure (GstCaps * caps, guint idx)
{
  GstStructure *structure;

  g_return_if_fail (caps != NULL);
  g_return_if_fail (idx <= gst_caps_get_size (caps));
  g_return_if_fail (IS_WRITABLE (caps));

  structure = gst_caps_remove_and_get_structure (caps, idx);
  gst_structure_free (structure);
}

/**
 * gst_caps_merge_structure:
 * @caps: the #GstCaps that will the the new structure
 * @structure: (transfer full): the #GstStructure to merge
 *
 * Appends @structure to @caps if its not already expressed by @caps.  The
 * structure is not copied; @caps becomes the owner of @structure.
 */
void
gst_caps_merge_structure (GstCaps * caps, GstStructure * structure)
{
  g_return_if_fail (GST_IS_CAPS (caps));
  g_return_if_fail (IS_WRITABLE (caps));

  if (G_LIKELY (structure)) {
    GstStructure *structure1;
    int i;
    gboolean unique = TRUE;

    /* check each structure */
    for (i = GST_CAPS_LEN (caps) - 1; i >= 0; i--) {
      structure1 = gst_caps_get_structure_unchecked (caps, i);
      /* if structure is a subset of structure1, then skip it */
      if (gst_structure_is_subset (structure, structure1)) {
        unique = FALSE;
        break;
      }
    }
    if (unique) {
      gst_caps_append_structure_unchecked (caps, structure);
    } else {
      gst_structure_free (structure);
    }
  }
}

/**
 * gst_caps_get_size:
 * @caps: a #GstCaps
 *
 * Gets the number of structures contained in @caps.
 *
 * Returns: the number of structures that @caps contains
 */
guint
gst_caps_get_size (const GstCaps * caps)
{
  g_return_val_if_fail (GST_IS_CAPS (caps), 0);

  return GST_CAPS_LEN (caps);
}

/**
 * gst_caps_get_structure:
 * @caps: a #GstCaps
 * @index: the index of the structure
 *
 * Finds the structure in @caps that has the index @index, and
 * returns it.
 *
 * WARNING: This function takes a const GstCaps *, but returns a
 * non-const GstStructure *.  This is for programming convenience --
 * the caller should be aware that structures inside a constant
 * #GstCaps should not be modified. However, if you know the caps
 * are writable, either because you have just copied them or made
 * them writable with gst_caps_make_writable(), you may modify the
 * structure returned in the usual way, e.g. with functions like
 * gst_structure_set().
 *
 * You do not need to free or unref the structure returned, it
 * belongs to the #GstCaps.
 *
 * Returns: (transfer none): a pointer to the #GstStructure corresponding
 *     to @index
 */
GstStructure *
gst_caps_get_structure (const GstCaps * caps, guint index)
{
  g_return_val_if_fail (GST_IS_CAPS (caps), NULL);
  g_return_val_if_fail (index < GST_CAPS_LEN (caps), NULL);

  return gst_caps_get_structure_unchecked (caps, index);
}

/**
 * gst_caps_copy_nth:
 * @caps: the #GstCaps to copy
 * @nth: the nth structure to copy
 *
 * Creates a new #GstCaps and appends a copy of the nth structure
 * contained in @caps.
 *
 * Returns: (transfer full): the new #GstCaps
 */
GstCaps *
gst_caps_copy_nth (const GstCaps * caps, guint nth)
{
  GstCaps *newcaps;
  GstStructure *structure;

  g_return_val_if_fail (GST_IS_CAPS (caps), NULL);

  newcaps = gst_caps_new_empty ();
  GST_CAPS_FLAGS (newcaps) = GST_CAPS_FLAGS (caps);

  if (G_LIKELY (GST_CAPS_LEN (caps) > nth)) {
    structure = gst_caps_get_structure_unchecked (caps, nth);
    gst_caps_append_structure_unchecked (newcaps,
        gst_structure_copy (structure));
  }

  return newcaps;
}

/**
 * gst_caps_truncate:
 * @caps: the #GstCaps to truncate
 *
 * Destructively discard all but the first structure from @caps. Useful when
 * fixating. @caps must be writable.
 */
void
gst_caps_truncate (GstCaps * caps)
{
  gint i;

  g_return_if_fail (GST_IS_CAPS (caps));
  g_return_if_fail (IS_WRITABLE (caps));

  i = GST_CAPS_LEN (caps) - 1;

  while (i > 0)
    gst_caps_remove_structure (caps, i--);
}

/**
 * gst_caps_set_value:
 * @caps: a writable caps
 * @field: name of the field to set
 * @value: value to set the field to
 *
 * Sets the given @field on all structures of @caps to the given @value.
 * This is a convenience function for calling gst_structure_set_value() on
 * all structures of @caps.
 *
 * Since: 0.10.26
 **/
void
gst_caps_set_value (GstCaps * caps, const char *field, const GValue * value)
{
  guint i, len;

  g_return_if_fail (GST_IS_CAPS (caps));
  g_return_if_fail (IS_WRITABLE (caps));
  g_return_if_fail (field != NULL);
  g_return_if_fail (G_IS_VALUE (value));

  len = GST_CAPS_LEN (caps);
  for (i = 0; i < len; i++) {
    GstStructure *structure = gst_caps_get_structure_unchecked (caps, i);
    gst_structure_set_value (structure, field, value);
  }
}

/**
 * gst_caps_set_simple_valist:
 * @caps: the #GstCaps to set
 * @field: first field to set
 * @varargs: additional parameters
 *
 * Sets fields in a #GstCaps.  The arguments must be passed in the same
 * manner as gst_structure_set(), and be NULL-terminated.
 * <note>Prior to GStreamer version 0.10.26, this function failed when
 * @caps was not simple. If your code needs to work with those versions
 * of GStreamer, you may only call this function when GST_CAPS_IS_SIMPLE()
 * is %TRUE for @caps.</note>
 */
void
gst_caps_set_simple_valist (GstCaps * caps, const char *field, va_list varargs)
{
  GValue value = { 0, };

  g_return_if_fail (GST_IS_CAPS (caps));
  g_return_if_fail (IS_WRITABLE (caps));

  while (field) {
    GType type;
    char *err;

    type = va_arg (varargs, GType);

    if (G_UNLIKELY (type == G_TYPE_DATE)) {
      g_warning ("Don't use G_TYPE_DATE, use GST_TYPE_DATE instead\n");
      type = GST_TYPE_DATE;
    }
    G_VALUE_COLLECT_INIT (&value, type, varargs, 0, &err);
    if (G_UNLIKELY (err)) {
      g_critical ("%s", err);
      return;
    }

    gst_caps_set_value (caps, field, &value);

    g_value_unset (&value);

    field = va_arg (varargs, const gchar *);
  }
}

/**
 * gst_caps_set_simple:
 * @caps: the #GstCaps to set
 * @field: first field to set
 * @...: additional parameters
 *
 * Sets fields in a #GstCaps.  The arguments must be passed in the same
 * manner as gst_structure_set(), and be NULL-terminated.
 * <note>Prior to GStreamer version 0.10.26, this function failed when
 * @caps was not simple. If your code needs to work with those versions
 * of GStreamer, you may only call this function when GST_CAPS_IS_SIMPLE()
 * is %TRUE for @caps.</note>
 */
void
gst_caps_set_simple (GstCaps * caps, const char *field, ...)
{
  va_list var_args;

  g_return_if_fail (GST_IS_CAPS (caps));
  g_return_if_fail (IS_WRITABLE (caps));

  va_start (var_args, field);
  gst_caps_set_simple_valist (caps, field, var_args);
  va_end (var_args);
}

/* tests */

/**
 * gst_caps_is_any:
 * @caps: the #GstCaps to test
 *
 * Determines if @caps represents any media format.
 *
 * Returns: TRUE if @caps represents any format.
 */
gboolean
gst_caps_is_any (const GstCaps * caps)
{
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);

  return (CAPS_IS_ANY (caps));
}

/**
 * gst_caps_is_empty:
 * @caps: the #GstCaps to test
 *
 * Determines if @caps represents no media formats.
 *
 * Returns: TRUE if @caps represents no formats.
 */
gboolean
gst_caps_is_empty (const GstCaps * caps)
{
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);

  if (CAPS_IS_ANY (caps))
    return FALSE;

  return CAPS_IS_EMPTY_SIMPLE (caps);
}

static gboolean
gst_caps_is_fixed_foreach (GQuark field_id, const GValue * value,
    gpointer unused)
{
  return gst_value_is_fixed (value);
}

/**
 * gst_caps_is_fixed:
 * @caps: the #GstCaps to test
 *
 * Fixed #GstCaps describe exactly one format, that is, they have exactly
 * one structure, and each field in the structure describes a fixed type.
 * Examples of non-fixed types are GST_TYPE_INT_RANGE and GST_TYPE_LIST.
 *
 * Returns: TRUE if @caps is fixed
 */
gboolean
gst_caps_is_fixed (const GstCaps * caps)
{
  GstStructure *structure;

  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);

  if (GST_CAPS_LEN (caps) != 1)
    return FALSE;

  structure = gst_caps_get_structure_unchecked (caps, 0);

  return gst_structure_foreach (structure, gst_caps_is_fixed_foreach, NULL);
}

/**
 * gst_caps_is_equal_fixed:
 * @caps1: the #GstCaps to test
 * @caps2: the #GstCaps to test
 *
 * Tests if two #GstCaps are equal.  This function only works on fixed
 * #GstCaps.
 *
 * Returns: TRUE if the arguments represent the same format
 */
gboolean
gst_caps_is_equal_fixed (const GstCaps * caps1, const GstCaps * caps2)
{
  GstStructure *struct1, *struct2;

  g_return_val_if_fail (gst_caps_is_fixed (caps1), FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps2), FALSE);

  struct1 = gst_caps_get_structure_unchecked (caps1, 0);
  struct2 = gst_caps_get_structure_unchecked (caps2, 0);

  return gst_structure_is_equal (struct1, struct2);
}

/**
 * gst_caps_is_always_compatible:
 * @caps1: the #GstCaps to test
 * @caps2: the #GstCaps to test
 *
 * A given #GstCaps structure is always compatible with another if
 * every media format that is in the first is also contained in the
 * second.  That is, @caps1 is a subset of @caps2.
 *
 * Returns: TRUE if @caps1 is a subset of @caps2.
 */
gboolean
gst_caps_is_always_compatible (const GstCaps * caps1, const GstCaps * caps2)
{
  g_return_val_if_fail (GST_IS_CAPS (caps1), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps2), FALSE);

  return gst_caps_is_subset (caps1, caps2);
}

/**
 * gst_caps_is_subset:
 * @subset: a #GstCaps
 * @superset: a potentially greater #GstCaps
 *
 * Checks if all caps represented by @subset are also represented by @superset.
 * <note>This function does not work reliably if optional properties for caps
 * are included on one caps and omitted on the other.</note>
 *
 * Returns: %TRUE if @subset is a subset of @superset
 */
gboolean
gst_caps_is_subset (const GstCaps * subset, const GstCaps * superset)
{
  GstStructure *s1, *s2;
  gboolean ret = TRUE;
  gint i, j;

  g_return_val_if_fail (subset != NULL, FALSE);
  g_return_val_if_fail (superset != NULL, FALSE);

  if (CAPS_IS_EMPTY (subset) || CAPS_IS_ANY (superset))
    return TRUE;
  if (CAPS_IS_ANY (subset) || CAPS_IS_EMPTY (superset))
    return FALSE;

  for (i = GST_CAPS_LEN (subset) - 1; i >= 0; i--) {
    for (j = GST_CAPS_LEN (superset) - 1; j >= 0; j--) {
      s1 = gst_caps_get_structure_unchecked (subset, i);
      s2 = gst_caps_get_structure_unchecked (superset, j);
      if (gst_structure_is_subset (s1, s2)) {
        /* If we found a superset, continue with the next
         * subset structure */
        break;
      }
    }
    /* If we found no superset for this subset structure
     * we return FALSE immediately */
    if (j == -1) {
      ret = FALSE;
      break;
    }
  }

  return ret;
}

/**
 * gst_caps_is_subset_structure:
 * @caps: a #GstCaps
 * @structure: a potential #GstStructure subset of @caps
 *
 * Checks if @structure is a subset of @caps. See gst_caps_is_subset()
 * for more information.
 *
 * Returns: %TRUE if @structure is a subset of @caps
 *
 * Since: 0.10.35
 */
gboolean
gst_caps_is_subset_structure (const GstCaps * caps,
    const GstStructure * structure)
{
  GstStructure *s;
  gint i;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (structure != NULL, FALSE);

  if (CAPS_IS_ANY (caps))
    return TRUE;
  if (CAPS_IS_EMPTY (caps))
    return FALSE;

  for (i = GST_CAPS_LEN (caps) - 1; i >= 0; i--) {
    s = gst_caps_get_structure_unchecked (caps, i);
    if (gst_structure_is_subset (structure, s)) {
      /* If we found a superset return TRUE */
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * gst_caps_is_equal:
 * @caps1: a #GstCaps
 * @caps2: another #GstCaps
 *
 * Checks if the given caps represent the same set of caps.
 * <note>This function does not work reliably if optional properties for caps
 * are included on one caps and omitted on the other.</note>
 *
 * This function deals correctly with passing NULL for any of the caps.
 *
 * Returns: TRUE if both caps are equal.
 */
gboolean
gst_caps_is_equal (const GstCaps * caps1, const GstCaps * caps2)
{
  /* FIXME 0.11: NULL pointers are no valid Caps but indicate an error
   * So there should be an assertion that caps1 and caps2 != NULL */

  /* NULL <-> NULL is allowed here */
  if (G_UNLIKELY (caps1 == caps2))
    return TRUE;

  /* one of them NULL => they are different (can't be both NULL because
   * we checked that above) */
  if (G_UNLIKELY (caps1 == NULL || caps2 == NULL))
    return FALSE;

  if (G_UNLIKELY (gst_caps_is_fixed (caps1) && gst_caps_is_fixed (caps2)))
    return gst_caps_is_equal_fixed (caps1, caps2);

  return gst_caps_is_subset (caps1, caps2) && gst_caps_is_subset (caps2, caps1);
}

/* intersect operation */

/**
 * gst_caps_can_intersect:
 * @caps1: a #GstCaps to intersect
 * @caps2: a #GstCaps to intersect
 *
 * Tries intersecting @caps1 and @caps2 and reports whether the result would not
 * be empty
 *
 * Returns: %TRUE if intersection would be not empty
 *
 * Since: 0.10.25
 */
gboolean
gst_caps_can_intersect (const GstCaps * caps1, const GstCaps * caps2)
{
  guint64 i;                    /* index can be up to 2 * G_MAX_UINT */
  guint j, k, len1, len2;
  GstStructure *struct1;
  GstStructure *struct2;

  g_return_val_if_fail (GST_IS_CAPS (caps1), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps2), FALSE);

  /* caps are exactly the same pointers */
  if (G_UNLIKELY (caps1 == caps2))
    return TRUE;

  /* empty caps on either side, return empty */
  if (G_UNLIKELY (CAPS_IS_EMPTY (caps1) || CAPS_IS_EMPTY (caps2)))
    return FALSE;

  /* one of the caps is any */
  if (G_UNLIKELY (CAPS_IS_ANY (caps1) || CAPS_IS_ANY (caps2)))
    return TRUE;

  /* run zigzag on top line then right line, this preserves the caps order
   * much better than a simple loop.
   *
   * This algorithm zigzags over the caps structures as demonstrated in
   * the folowing matrix:
   *
   *          caps1                              0  1  2  3
   *       +-------------     total distance:  +-------------
   *       | 1  2  4  7                      0 | 0  1  2  3
   * caps2 | 3  5  8 10                      1 | 1  2  3  4
   *       | 6  9 11 12                      2 | 2  3  4  5
   *
   * First we iterate over the caps1 structures (top line) intersecting
   * the structures diagonally down, then we iterate over the caps2
   * structures. The result is that the intersections are ordered based on the
   * sum of the indexes in the list.
   */
  len1 = GST_CAPS_LEN (caps1);
  len2 = GST_CAPS_LEN (caps2);
  for (i = 0; i < len1 + len2 - 1; i++) {
    /* superset index goes from 0 to sgst_caps_structure_intersectuperset->structs->len-1 */
    j = MIN (i, len1 - 1);
    /* subset index stays 0 until i reaches superset->structs->len, then it
     * counts up from 1 to subset->structs->len - 1 */
    k = MAX (0, i - j);

    /* now run the diagonal line, end condition is the left or bottom
     * border */
    while (k < len2) {
      struct1 = gst_caps_get_structure_unchecked (caps1, j);
      struct2 = gst_caps_get_structure_unchecked (caps2, k);

      if (gst_structure_can_intersect (struct1, struct2)) {
        return TRUE;
      }
      /* move down left */
      k++;
      if (G_UNLIKELY (j == 0))
        break;                  /* so we don't roll back to G_MAXUINT */
      j--;
    }
  }
  return FALSE;
}

static GstCaps *
gst_caps_intersect_zig_zag (const GstCaps * caps1, const GstCaps * caps2)
{
  guint64 i;                    /* index can be up to 2 * G_MAX_UINT */
  guint j, k, len1, len2;

  GstStructure *struct1;
  GstStructure *struct2;
  GstCaps *dest;
  GstStructure *istruct;

  /* caps are exactly the same pointers, just copy one caps */
  if (G_UNLIKELY (caps1 == caps2))
    return _gst_caps_copy (caps1);

  /* empty caps on either side, return empty */
  if (G_UNLIKELY (CAPS_IS_EMPTY (caps1) || CAPS_IS_EMPTY (caps2)))
    return gst_caps_new_empty ();

  /* one of the caps is any, just copy the other caps */
  if (G_UNLIKELY (CAPS_IS_ANY (caps1)))
    return _gst_caps_copy (caps2);
  if (G_UNLIKELY (CAPS_IS_ANY (caps2)))
    return _gst_caps_copy (caps1);

  dest = gst_caps_new_empty ();

  /* run zigzag on top line then right line, this preserves the caps order
   * much better than a simple loop.
   *
   * This algorithm zigzags over the caps structures as demonstrated in
   * the folowing matrix:
   *
   *          caps1
   *       +-------------
   *       | 1  2  4  7
   * caps2 | 3  5  8 10
   *       | 6  9 11 12
   *
   * First we iterate over the caps1 structures (top line) intersecting
   * the structures diagonally down, then we iterate over the caps2
   * structures.
   */
  len1 = GST_CAPS_LEN (caps1);
  len2 = GST_CAPS_LEN (caps2);
  for (i = 0; i < len1 + len2 - 1; i++) {
    /* caps1 index goes from 0 to GST_CAPS_LEN (caps1)-1 */
    j = MIN (i, len1 - 1);
    /* caps2 index stays 0 until i reaches GST_CAPS_LEN (caps1), then it counts
     * up from 1 to GST_CAPS_LEN (caps2) - 1 */
    k = MAX (0, i - j);

    /* now run the diagonal line, end condition is the left or bottom
     * border */
    while (k < len2) {
      struct1 = gst_caps_get_structure_unchecked (caps1, j);
      struct2 = gst_caps_get_structure_unchecked (caps2, k);

      istruct = gst_structure_intersect (struct1, struct2);

      gst_caps_merge_structure (dest, istruct);
      /* move down left */
      k++;
      if (G_UNLIKELY (j == 0))
        break;                  /* so we don't roll back to G_MAXUINT */
      j--;
    }
  }
  return dest;
}

/**
 * gst_caps_intersect_first:
 * @caps1: a #GstCaps to intersect
 * @caps2: a #GstCaps to intersect
 *
 * Creates a new #GstCaps that contains all the formats that are common
 * to both @caps1 and @caps2.
 *
 * Unlike @gst_caps_intersect, the returned caps will be ordered in a similar
 * fashion as @caps1.
 *
 * Returns: the new #GstCaps
 */
static GstCaps *
gst_caps_intersect_first (const GstCaps * caps1, const GstCaps * caps2)
{
  guint64 i;                    /* index can be up to 2 * G_MAX_UINT */
  guint j, len1, len2;

  GstStructure *struct1;
  GstStructure *struct2;
  GstCaps *dest;
  GstStructure *istruct;

  /* caps are exactly the same pointers, just copy one caps */
  if (G_UNLIKELY (caps1 == caps2))
    return gst_caps_copy (caps1);

  /* empty caps on either side, return empty */
  if (G_UNLIKELY (CAPS_IS_EMPTY (caps1) || CAPS_IS_EMPTY (caps2)))
    return gst_caps_new_empty ();

  /* one of the caps is any, just copy the other caps */
  if (G_UNLIKELY (CAPS_IS_ANY (caps1)))
    return gst_caps_copy (caps2);
  if (G_UNLIKELY (CAPS_IS_ANY (caps2)))
    return gst_caps_copy (caps1);

  dest = gst_caps_new_empty ();

  len1 = GST_CAPS_LEN (caps1);
  len2 = GST_CAPS_LEN (caps2);
  for (i = 0; i < len1; i++) {
    struct1 = gst_caps_get_structure_unchecked (caps1, i);
    for (j = 0; j < len2; j++) {
      struct2 = gst_caps_get_structure_unchecked (caps2, j);
      istruct = gst_structure_intersect (struct1, struct2);
      if (istruct)
        gst_caps_merge_structure (dest, istruct);
    }
  }

  return dest;
}

/**
 * gst_caps_intersect_full:
 * @caps1: a #GstCaps to intersect
 * @caps2: a #GstCaps to intersect
 * @mode: The intersection algorithm/mode to use
 *
 * Creates a new #GstCaps that contains all the formats that are common
 * to both @caps1 and @caps2, the order is defined by the #GstCapsIntersectMode
 * used.
 *
 * Returns: the new #GstCaps
 * Since: 0.10.33
 */
GstCaps *
gst_caps_intersect_full (const GstCaps * caps1, const GstCaps * caps2,
    GstCapsIntersectMode mode)
{
  g_return_val_if_fail (GST_IS_CAPS (caps1), NULL);
  g_return_val_if_fail (GST_IS_CAPS (caps2), NULL);

  switch (mode) {
    case GST_CAPS_INTERSECT_FIRST:
      return gst_caps_intersect_first (caps1, caps2);
    default:
      g_warning ("Unknown caps intersect mode: %d", mode);
      /* fallthrough */
    case GST_CAPS_INTERSECT_ZIG_ZAG:
      return gst_caps_intersect_zig_zag (caps1, caps2);
  }
}

/**
 * gst_caps_intersect:
 * @caps1: a #GstCaps to intersect
 * @caps2: a #GstCaps to intersect
 *
 * Creates a new #GstCaps that contains all the formats that are common
 * to both @caps1 and @caps2. Defaults to %GST_CAPS_INTERSECT_ZIG_ZAG mode.
 *
 * Returns: the new #GstCaps
 */
GstCaps *
gst_caps_intersect (const GstCaps * caps1, const GstCaps * caps2)
{
  return gst_caps_intersect_full (caps1, caps2, GST_CAPS_INTERSECT_ZIG_ZAG);
}


/* subtract operation */

typedef struct
{
  const GstStructure *subtract_from;
  GSList *put_into;
}
SubtractionEntry;

static gboolean
gst_caps_structure_subtract_field (GQuark field_id, const GValue * value,
    gpointer user_data)
{
  SubtractionEntry *e = user_data;
  GValue subtraction = { 0, };
  const GValue *other;
  GstStructure *structure;

  other = gst_structure_id_get_value (e->subtract_from, field_id);
  if (!other) {
    return FALSE;
  }
  if (!gst_value_subtract (&subtraction, other, value))
    return TRUE;
  if (gst_value_compare (&subtraction, other) == GST_VALUE_EQUAL) {
    g_value_unset (&subtraction);
    return FALSE;
  } else {
    structure = gst_structure_copy (e->subtract_from);
    gst_structure_id_set_value (structure, field_id, &subtraction);
    g_value_unset (&subtraction);
    e->put_into = g_slist_prepend (e->put_into, structure);
    return TRUE;
  }
}

static gboolean
gst_caps_structure_subtract (GSList ** into, const GstStructure * minuend,
    const GstStructure * subtrahend)
{
  SubtractionEntry e;
  gboolean ret;

  e.subtract_from = minuend;
  e.put_into = NULL;

  ret = gst_structure_foreach ((GstStructure *) subtrahend,
      gst_caps_structure_subtract_field, &e);
  if (ret) {
    *into = e.put_into;
  } else {
    GSList *walk;

    for (walk = e.put_into; walk; walk = g_slist_next (walk)) {
      gst_structure_free (walk->data);
    }
    g_slist_free (e.put_into);
  }
  return ret;
}

/**
 * gst_caps_subtract:
 * @minuend: #GstCaps to substract from
 * @subtrahend: #GstCaps to substract
 *
 * Subtracts the @subtrahend from the @minuend.
 * <note>This function does not work reliably if optional properties for caps
 * are included on one caps and omitted on the other.</note>
 *
 * Returns: the resulting caps
 */
GstCaps *
gst_caps_subtract (const GstCaps * minuend, const GstCaps * subtrahend)
{
  guint i, j, sublen;
  GstStructure *min;
  GstStructure *sub;
  GstCaps *dest = NULL, *src;

  g_return_val_if_fail (minuend != NULL, NULL);
  g_return_val_if_fail (subtrahend != NULL, NULL);

  if (CAPS_IS_EMPTY (minuend) || CAPS_IS_ANY (subtrahend)) {
    return gst_caps_new_empty ();
  }
  if (CAPS_IS_EMPTY_SIMPLE (subtrahend))
    return _gst_caps_copy (minuend);

  /* FIXME: Do we want this here or above?
     The reason we need this is that there is no definition about what
     ANY means for specific types, so it's not possible to reduce ANY partially
     You can only remove everything or nothing and that is done above.
     Note: there's a test that checks this behaviour. */
  g_return_val_if_fail (!CAPS_IS_ANY (minuend), NULL);
  sublen = GST_CAPS_LEN (subtrahend);
  g_assert (sublen > 0);

  src = _gst_caps_copy (minuend);
  for (i = 0; i < sublen; i++) {
    guint srclen;

    sub = gst_caps_get_structure_unchecked (subtrahend, i);
    if (dest) {
      gst_caps_unref (src);
      src = dest;
    }
    dest = gst_caps_new_empty ();
    srclen = GST_CAPS_LEN (src);
    for (j = 0; j < srclen; j++) {
      min = gst_caps_get_structure_unchecked (src, j);
      if (gst_structure_get_name_id (min) == gst_structure_get_name_id (sub)) {
        GSList *list;

        if (gst_caps_structure_subtract (&list, min, sub)) {
          GSList *walk;

          for (walk = list; walk; walk = g_slist_next (walk)) {
            gst_caps_append_structure_unchecked (dest,
                (GstStructure *) walk->data);
          }
          g_slist_free (list);
        } else {
          gst_caps_append_structure_unchecked (dest, gst_structure_copy (min));
        }
      } else {
        gst_caps_append_structure_unchecked (dest, gst_structure_copy (min));
      }
    }
    if (CAPS_IS_EMPTY_SIMPLE (dest)) {
      gst_caps_unref (src);
      return dest;
    }
  }

  gst_caps_unref (src);
  gst_caps_do_simplify (dest);
  return dest;
}

/* union operation */

#if 0
static GstStructure *
gst_caps_structure_union (const GstStructure * struct1,
    const GstStructure * struct2)
{
  int i;
  GstStructure *dest;
  const GstStructureField *field1;
  const GstStructureField *field2;
  int ret;

  /* FIXME this doesn't actually work */

  if (struct1->name != struct2->name)
    return NULL;

  dest = gst_structure_id_empty_new (struct1->name);

  for (i = 0; i < struct1->fields->len; i++) {
    GValue dest_value = { 0 };

    field1 = GST_STRUCTURE_FIELD (struct1, i);
    field2 = gst_structure_id_get_field (struct2, field1->name);

    if (field2 == NULL) {
      continue;
    } else {
      if (gst_value_union (&dest_value, &field1->value, &field2->value)) {
        gst_structure_set_value (dest, g_quark_to_string (field1->name),
            &dest_value);
      } else {
        ret = gst_value_compare (&field1->value, &field2->value);
      }
    }
  }

  return dest;
}
#endif

/**
 * gst_caps_union:
 * @caps1: a #GstCaps to union
 * @caps2: a #GstCaps to union
 *
 * Creates a new #GstCaps that contains all the formats that are in
 * either @caps1 and @caps2.
 *
 * Returns: the new #GstCaps
 */
GstCaps *
gst_caps_union (const GstCaps * caps1, const GstCaps * caps2)
{
  GstCaps *dest1;
  GstCaps *dest2;

  /* NULL pointers are no correct GstCaps */
  g_return_val_if_fail (caps1 != NULL, NULL);
  g_return_val_if_fail (caps2 != NULL, NULL);

  if (CAPS_IS_EMPTY (caps1))
    return _gst_caps_copy (caps2);

  if (CAPS_IS_EMPTY (caps2))
    return _gst_caps_copy (caps1);

  if (CAPS_IS_ANY (caps1) || CAPS_IS_ANY (caps2))
    return gst_caps_new_any ();

  dest1 = _gst_caps_copy (caps1);
  dest2 = _gst_caps_copy (caps2);
  gst_caps_append (dest1, dest2);

  gst_caps_do_simplify (dest1);
  return dest1;
}

/* normalize/simplify operations */

typedef struct _NormalizeForeach
{
  GstCaps *caps;
  GstStructure *structure;
}
NormalizeForeach;

static gboolean
gst_caps_normalize_foreach (GQuark field_id, const GValue * value, gpointer ptr)
{
  NormalizeForeach *nf = (NormalizeForeach *) ptr;
  GValue val = { 0 };
  guint i;

  if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    guint len = gst_value_list_get_size (value);
    for (i = 1; i < len; i++) {
      const GValue *v = gst_value_list_get_value (value, i);
      GstStructure *structure = gst_structure_copy (nf->structure);

      gst_structure_id_set_value (structure, field_id, v);
      gst_caps_append_structure_unchecked (nf->caps, structure);
    }

    gst_value_init_and_copy (&val, gst_value_list_get_value (value, 0));
    gst_structure_id_set_value (nf->structure, field_id, &val);
    g_value_unset (&val);

    return FALSE;
  }
  return TRUE;
}

/**
 * gst_caps_normalize:
 * @caps: a #GstCaps to normalize
 *
 * Creates a new #GstCaps that represents the same set of formats as
 * @caps, but contains no lists.  Each list is expanded into separate
 * @GstStructures.
 *
 * Returns: the new #GstCaps
 */
GstCaps *
gst_caps_normalize (const GstCaps * caps)
{
  NormalizeForeach nf;
  GstCaps *newcaps;
  guint i;

  g_return_val_if_fail (GST_IS_CAPS (caps), NULL);

  newcaps = _gst_caps_copy (caps);
  nf.caps = newcaps;

  for (i = 0; i < gst_caps_get_size (newcaps); i++) {
    nf.structure = gst_caps_get_structure_unchecked (newcaps, i);

    while (!gst_structure_foreach (nf.structure,
            gst_caps_normalize_foreach, &nf));
  }

  return newcaps;
}

static gint
gst_caps_compare_structures (gconstpointer one, gconstpointer two)
{
  gint ret;
  const GstStructure *struct1 = *((const GstStructure **) one);
  const GstStructure *struct2 = *((const GstStructure **) two);

  /* FIXME: this orders alphabetically, but ordering the quarks might be faster
     So what's the best way? */
  ret = strcmp (gst_structure_get_name (struct1),
      gst_structure_get_name (struct2));
  if (ret)
    return ret;

  return gst_structure_n_fields (struct2) - gst_structure_n_fields (struct1);
}

typedef struct
{
  GQuark name;
  GValue value;
  GstStructure *compare;
}
UnionField;

static gboolean
gst_caps_structure_figure_out_union (GQuark field_id, const GValue * value,
    gpointer user_data)
{
  UnionField *u = user_data;
  const GValue *val = gst_structure_id_get_value (u->compare, field_id);

  if (!val) {
    if (u->name)
      g_value_unset (&u->value);
    return FALSE;
  }
  if (gst_value_compare (val, value) == GST_VALUE_EQUAL)
    return TRUE;
  if (u->name) {
    g_value_unset (&u->value);
    return FALSE;
  }
  u->name = field_id;
  gst_value_union (&u->value, val, value);
  return TRUE;
}

static gboolean
gst_caps_structure_simplify (GstStructure ** result,
    const GstStructure * simplify, GstStructure * compare)
{
  GSList *list;
  UnionField field = { 0, {0,}, NULL };

  /* try to subtract to get a real subset */
  if (gst_caps_structure_subtract (&list, simplify, compare)) {
    if (list == NULL) {         /* no result */
      *result = NULL;
      return TRUE;
    } else if (list->next == NULL) {    /* one result */
      *result = list->data;
      g_slist_free (list);
      return TRUE;
    } else {                    /* multiple results */
      g_slist_foreach (list, (GFunc) gst_structure_free, NULL);
      g_slist_free (list);
      list = NULL;
    }
  }

  /* try to union both structs */
  field.compare = compare;
  if (gst_structure_foreach ((GstStructure *) simplify,
          gst_caps_structure_figure_out_union, &field)) {
    gboolean ret = FALSE;

    /* now we know all of simplify's fields are the same in compare
     * but at most one field: field.name */
    if (G_IS_VALUE (&field.value)) {
      if (gst_structure_n_fields (simplify) == gst_structure_n_fields (compare)) {
        gst_structure_id_set_value (compare, field.name, &field.value);
        *result = NULL;
        ret = TRUE;
      }
      g_value_unset (&field.value);
    } else if (gst_structure_n_fields (simplify) <=
        gst_structure_n_fields (compare)) {
      /* compare is just more specific, will be optimized away later */
      /* FIXME: do this here? */
      GST_LOG ("found a case that will be optimized later.");
    } else {
      gchar *one = gst_structure_to_string (simplify);
      gchar *two = gst_structure_to_string (compare);

      GST_ERROR
          ("caps mismatch: structures %s and %s claim to be possible to unify, but aren't",
          one, two);
      g_free (one);
      g_free (two);
    }
    return ret;
  }

  return FALSE;
}

static void
gst_caps_switch_structures (GstCaps * caps, GstStructure * old,
    GstStructure * new, gint i)
{
  gst_structure_set_parent_refcount (old, NULL);
  gst_structure_free (old);
  gst_structure_set_parent_refcount (new, &GST_CAPS_REFCOUNT (caps));
  g_ptr_array_index (GST_CAPS_ARRAY (caps), i) = new;
}

/**
 * gst_caps_do_simplify:
 * @caps: a #GstCaps to simplify
 *
 * Modifies the given @caps inplace into a representation that represents the
 * same set of formats, but in a simpler form.  Component structures that are
 * identical are merged.  Component structures that have values that can be
 * merged are also merged.
 *
 * Returns: TRUE, if the caps could be simplified
 */
gboolean
gst_caps_do_simplify (GstCaps * caps)
{
  GstStructure *simplify, *compare, *result = NULL;
  gint i, j, start;
  gboolean changed = FALSE;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (IS_WRITABLE (caps), FALSE);

  if (gst_caps_get_size (caps) < 2)
    return FALSE;

  g_ptr_array_sort (GST_CAPS_ARRAY (caps), gst_caps_compare_structures);

  start = GST_CAPS_LEN (caps) - 1;
  for (i = GST_CAPS_LEN (caps) - 1; i >= 0; i--) {
    simplify = gst_caps_get_structure_unchecked (caps, i);
    if (gst_structure_get_name_id (simplify) !=
        gst_structure_get_name_id (gst_caps_get_structure_unchecked (caps,
                start)))
      start = i;
    for (j = start; j >= 0; j--) {
      if (j == i)
        continue;
      compare = gst_caps_get_structure_unchecked (caps, j);
      if (gst_structure_get_name_id (simplify) !=
          gst_structure_get_name_id (compare)) {
        break;
      }
      if (gst_caps_structure_simplify (&result, simplify, compare)) {
        if (result) {
          gst_caps_switch_structures (caps, simplify, result, i);
          simplify = result;
        } else {
          gst_caps_remove_structure (caps, i);
          start--;
          break;
        }
        changed = TRUE;
      }
    }
  }

  if (!changed)
    return FALSE;

  /* gst_caps_do_simplify (caps); */
  return TRUE;
}

/* utility */

/**
 * gst_caps_to_string:
 * @caps: a #GstCaps
 *
 * Converts @caps to a string representation.  This string representation
 * can be converted back to a #GstCaps by gst_caps_from_string().
 *
 * For debugging purposes its easier to do something like this:
 * |[
 * GST_LOG ("caps are %" GST_PTR_FORMAT, caps);
 * ]|
 * This prints the caps in human readble form.
 *
 * Returns: (transfer full): a newly allocated string representing @caps.
 */
gchar *
gst_caps_to_string (const GstCaps * caps)
{
  guint i, slen, clen;
  GString *s;

  /* NOTE:  This function is potentially called by the debug system,
   * so any calls to gst_log() (and GST_DEBUG(), GST_LOG(), etc.)
   * should be careful to avoid recursion.  This includes any functions
   * called by gst_caps_to_string.  In particular, calls should
   * not use the GST_PTR_FORMAT extension.  */

  if (caps == NULL) {
    return g_strdup ("NULL");
  }
  if (CAPS_IS_ANY (caps)) {
    return g_strdup ("ANY");
  }
  if (CAPS_IS_EMPTY_SIMPLE (caps)) {
    return g_strdup ("EMPTY");
  }

  /* estimate a rough string length to avoid unnecessary reallocs in GString */
  slen = 0;
  clen = GST_CAPS_LEN (caps);
  for (i = 0; i < clen; i++) {
    slen +=
        STRUCTURE_ESTIMATED_STRING_LEN (gst_caps_get_structure_unchecked (caps,
            i));
  }

  s = g_string_sized_new (slen);
  for (i = 0; i < clen; i++) {
    GstStructure *structure;

    if (i > 0) {
      /* ';' is now added by gst_structure_to_string */
      g_string_append_c (s, ' ');
    }

    structure = gst_caps_get_structure_unchecked (caps, i);
    priv_gst_structure_append_to_gstring (structure, s);
  }
  if (s->len && s->str[s->len - 1] == ';') {
    /* remove latest ';' */
    s->str[--s->len] = '\0';
  }
  return g_string_free (s, FALSE);
}

static gboolean
gst_caps_from_string_inplace (GstCaps * caps, const gchar * string)
{
  GstStructure *structure;
  gchar *s;

  if (strcmp ("ANY", string) == 0) {
    GST_CAPS_FLAGS (caps) = GST_CAPS_FLAGS_ANY;
    return TRUE;
  }
  if (strcmp ("EMPTY", string) == 0) {
    return TRUE;
  }

  structure = gst_structure_from_string (string, &s);
  if (structure == NULL) {
    return FALSE;
  }
  gst_caps_append_structure_unchecked (caps, structure);

  do {

    while (g_ascii_isspace (*s))
      s++;
    if (*s == '\0') {
      break;
    }
    structure = gst_structure_from_string (s, &s);
    if (structure == NULL) {
      return FALSE;
    }
    gst_caps_append_structure_unchecked (caps, structure);

  } while (TRUE);

  return TRUE;
}

/**
 * gst_caps_from_string:
 * @string: a string to convert to #GstCaps
 *
 * Converts @caps from a string representation.
 *
 * Returns: (transfer full): a newly allocated #GstCaps
 */
GstCaps *
gst_caps_from_string (const gchar * string)
{
  GstCaps *caps;

  g_return_val_if_fail (string, FALSE);

  caps = gst_caps_new_empty ();
  if (gst_caps_from_string_inplace (caps, string)) {
    return caps;
  } else {
    gst_caps_unref (caps);
    return NULL;
  }
}

static void
gst_caps_transform_to_string (const GValue * src_value, GValue * dest_value)
{
  g_return_if_fail (G_IS_VALUE (src_value));
  g_return_if_fail (G_IS_VALUE (dest_value));
  g_return_if_fail (G_VALUE_HOLDS (src_value, GST_TYPE_CAPS));
  g_return_if_fail (G_VALUE_HOLDS (dest_value, G_TYPE_STRING)
      || G_VALUE_HOLDS (dest_value, G_TYPE_POINTER));

  dest_value->data[0].v_pointer =
      gst_caps_to_string (src_value->data[0].v_pointer);
}
