/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttag.c: tag support (aka metadata)
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst_private.h"
#include "gsttag.h"
#include "gstinfo.h"
#include "gstvalue.h"

#include <gobject/gvaluecollector.h>
#include <string.h>

#define GST_TAG_IS_VALID(tag)		(gst_tag_get_info (tag) != NULL)

typedef struct {
  GType			type;		/* type the data is in */

  gchar *		nick;		/* translated name */
  gchar *		blurb;		/* translated description of type */

  GstTagMergeFunc	merge_func;	/* functions to merge the values */
} GstTagInfo;

#define TAGLIST "taglist"
static GQuark gst_tag_list_quark;
static GMutex *__tag_mutex;
static GHashTable *__tags;
#define TAG_LOCK g_mutex_lock (__tag_mutex)
#define TAG_UNLOCK g_mutex_unlock (__tag_mutex)

void
_gst_tag_initialize (void)
{
  gst_tag_list_quark = g_quark_from_static_string (TAGLIST);
  __tag_mutex = g_mutex_new ();
  __tags = g_hash_table_new (g_direct_hash, g_direct_equal);
  gst_tag_register (GST_TAG_TITLE,	      
		    G_TYPE_STRING,
		    _("title"),
		    _("commonly used title"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_ARTIST,
		    G_TYPE_STRING,
		    _("artist"),
		    _("person(s) resposible for the recording"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_ALBUM,
		    G_TYPE_STRING,
		    _("album"),
		    _("album containing this data"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_DATE,
		    G_TYPE_UINT, /* FIXME: own data type for dates? */
		    _("date"),
		    _("date the data was created in julien days"),
		    NULL);
  gst_tag_register (GST_TAG_GENRE,
		    G_TYPE_STRING,
		    _("genre"), 
		    _("genre this data belongs to"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_COMMENT,
		    G_TYPE_STRING,
		    _("comment"),
		    _("free text commenting the data"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_TRACK_NUMBER,
		    G_TYPE_UINT,
		    _("track number"),
		    _("track number inside a collection"),
		    gst_tag_merge_use_first);
  gst_tag_register (GST_TAG_TRACK_COUNT,
		    G_TYPE_STRING,
		    _("track count"),
		    _("count of tracks inside collection this track belongs to"), 
		    gst_tag_merge_use_first);
  gst_tag_register (GST_TAG_LOCATION,
		    G_TYPE_STRING,
		    _("loccation"),
		    _("original location of file as a URI"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_DESCRIPTION,
		    G_TYPE_STRING,
		    _("description"),
		    _("short text describing the content of the data"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_VERSION,
		    G_TYPE_STRING,
		    _("version"),
		    _("version of this data"),
		    NULL);
  gst_tag_register (GST_TAG_ISRC,
		    G_TYPE_STRING,
		    _("ISRC"),
		    _("International Standard Recording Code - see http://www.ifpi.org/isrc/"),
		    NULL);
  gst_tag_register (GST_TAG_ORGANIZATION,
		    G_TYPE_STRING,
		    _("organization"),
		    _("organization"), /* FIXME */
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_COPYRIGHT,
		    G_TYPE_STRING,
		    _("copyright"),
		    _("copyright notice of the data"),
		    NULL);
  gst_tag_register (GST_TAG_CONTACT,
		    G_TYPE_STRING,
		    _("contact"),
		    _("contact information"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_LICENSE,	
		    G_TYPE_STRING,
		    _("license"),
		    _("license of data"),
		    NULL);
  gst_tag_register (GST_TAG_PERFORMER,
		    G_TYPE_STRING,
		    _("performer"),
		    _("person(s) performing"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_DURATION,
		    G_TYPE_UINT64,
		    _("duration"),
		    _("length in GStreamer time units (nanoseconds)"),
		    NULL);
  gst_tag_register (GST_TAG_CODEC,
		    G_TYPE_STRING,
		    _("codec"),
		    _("codec the data is stored in"),
		    gst_tag_merge_strings_with_comma);
  gst_tag_register (GST_TAG_MINIMUM_BITRATE,
		    G_TYPE_UINT,
		    _("minimum bitrate"),
		    _("minimum bitrate in bits/s"),
		    NULL);
  gst_tag_register (GST_TAG_BITRATE,
		    G_TYPE_UINT,
		    _("bitrate"),
		    _("exact or average bitrate in bits/s"),
		    NULL);
  gst_tag_register (GST_TAG_MAXIMUM_BITRATE,
		    G_TYPE_UINT,
		    _("maximum bitrate"),
		    _("maximum bitrate in bits/s"),
		    NULL);
}
/**
 * gst_tag_merge_use_first:
 * @dest: uninitialized GValue to store result in
 * @src: GValue to copy from
 *
 * This is a convenience function for the func argument of gst_tag_register(). 
 * It creates a copy of the first value from the list.
 */
void
gst_tag_merge_use_first (GValue *dest, const GValue *src)
{
  const GValue *ret = gst_value_list_get_value (src, 0);

  g_value_init (dest, G_VALUE_TYPE (ret));
  g_value_copy (ret, dest);
}
/**
 * gst_tag_merge_strings_with_comma:
 * @dest: uninitialized GValue to store result in
 * @src: GValue to copy from
 * 
 * This is a convenience function for the func argument of gst_tag_register().
 * It concatenates all given strings using a comma. The tag must be registered
 * as a G_TYPE_STRING or this function will fail.
 */
void
gst_tag_merge_strings_with_comma (GValue *dest, const GValue *src)
{
  GString *str;
  gint i, count;

  count = gst_value_list_get_size (src);
  str = g_string_new (g_value_get_string (gst_value_list_get_value (src, 0)));
  for (i = 1; i < count; i++) {
    /* seperator between two string */
    str = g_string_append (str, _(", "));
    str = g_string_append (str, g_value_get_string (gst_value_list_get_value (src, 1)));
  }

  g_value_init (dest, G_TYPE_STRING);
  g_value_set_string_take_ownership (dest, str->str);
  g_string_free (str, FALSE);
}
static GstTagInfo *
gst_tag_lookup (GQuark entry)
{
  GstTagInfo *ret;
  
  TAG_LOCK;
  ret = g_hash_table_lookup (__tags, GUINT_TO_POINTER (entry));
  TAG_UNLOCK;

  return ret;
}
/**
 * gst_tag_register:
 * @name: the name or identifier string
 * @type: the type this data is in
 * @nick: human-readable name
 * @blurb: a human-readable description about this tag
 * @func: function for merging multiple values of this tag
 *
 * Registers a new tag type for the use with GStreamer's type system. If a type
 * with that name is already registered, that one is used.
 * The old registration may have used a different type however. So don't rely
 * on youre supplied values.
 * If you know the type is already registered, use gst_tag_lookup instead.
 * This function takes ownership of all supplied variables.
 */
void
gst_tag_register (gchar *name, GType type, gchar *nick, gchar *blurb,
		  GstTagMergeFunc func)
{
  GQuark key;
  GstTagInfo *info;

  g_return_if_fail (name != NULL);
  g_return_if_fail (nick != NULL);
  g_return_if_fail (blurb != NULL);
  g_return_if_fail (type != 0 && type != GST_TYPE_LIST);
  
  key = g_quark_from_string (name);
  info = gst_tag_lookup (key);
  g_return_if_fail (info == NULL);
  
  info = g_new (GstTagInfo, 1);
  info->type = type;
  info->nick = nick;
  info->blurb = blurb;
  info->merge_func = func;
    
  TAG_LOCK;
  g_hash_table_insert (__tags, GUINT_TO_POINTER (key), info);
  TAG_UNLOCK;
}
/**
 * gst_tag_exists:
 * @tag: name of the tag
 *
 * Checks if the given type is already registered.
 *
 * Returns: TRUE if the type is already registered
 */
gboolean
gst_tag_exists (const gchar *tag)
{
  g_return_val_if_fail (tag != NULL, FALSE);
  
  return gst_tag_lookup (g_quark_from_string (tag)) != NULL;
}
/**
 * gst_tag_get_type:
 * @tag: the tag
 *
 * Gets the #GType used for this tag.
 *
 * Returns: the #GType of this tag
 */
GType
gst_tag_get_type (const gchar *tag)
{
  GstTagInfo *info;
  
  g_return_val_if_fail (tag != NULL, 0);
  info = gst_tag_lookup (g_quark_from_string (tag));
  g_return_val_if_fail (info != NULL, 0);
  
  return info->type;
}
/**
 * gst_tag_get_nick
 * @tag: the tag
 *
 * Returns the human-readable name of this tag, You must not change or free 
 * this string.
 *
 * Returns: the human-readable name of this tag
 */
const gchar *
gst_tag_get_nick (const gchar *tag)
{
  GstTagInfo *info;
  
  g_return_val_if_fail (tag != NULL, NULL);
  info = gst_tag_lookup (g_quark_from_string (tag));
  g_return_val_if_fail (info != NULL, NULL);
  
  return info->nick;
}
/**
 * gst_tag_get_description:
 * @tag: the tag
 *
 * Returns the human-readable description of this tag, You must not change or 
 * free this string.
 *
 * Return the human-readable description of this tag
 */
const gchar *
gst_tag_get_description (const gchar *tag)
{
  GstTagInfo *info;
  
  g_return_val_if_fail (tag != NULL, NULL);
  info = gst_tag_lookup (g_quark_from_string (tag));
  g_return_val_if_fail (info != NULL, NULL);
  
  return info->blurb;
}
/**
 * gst_tag_list_is_fixed:
 * @tag: tag to check
 *
 * Checks if the given tag is fixed. A fixed tag can only contain one value.
 * Unfixed tags can contain lists of values.
 *
 * Returns: TRUE, if the given tag is fixed.
 */
gboolean
gst_tag_is_fixed (const gchar *tag)
{
  GstTagInfo *info;
  
  g_return_val_if_fail (tag != NULL, FALSE);
  info = gst_tag_lookup (g_quark_from_string (tag));
  g_return_val_if_fail (info != NULL, FALSE);
  
  return info->merge_func == NULL;
}
/**
 * gst_tag_list_new:
 *
 * Creates a new empty GstTagList.
 *
 * Returns: An empty tag list
 */
GstTagList *
gst_tag_list_new (void)
{
  return GST_TAG_LIST (gst_structure_new (TAGLIST, NULL));
}
/**
 * gst_is_tag_list:
 * @p: Object that might be a taglist
 *
 * Checks if the given pointer is a taglist.
 *
 * Returns: TRUE, if the given pointer is a taglist
 */
gboolean
gst_is_tag_list (gconstpointer p)
{
  g_return_val_if_fail (p != NULL, FALSE); 

  return ((GstStructure *) p)->name == gst_tag_list_quark;
}
typedef struct {
  GstStructure *	list;
  GstTagMergeMode	mode;
} GstTagCopyData;
static void
gst_tag_list_add_value_internal (GstStructure *list, GstTagMergeMode mode, GQuark tag, GValue *value)
{
  GstTagInfo *info = gst_tag_lookup (tag);
  GstStructureField *field;
  
  g_assert (info != NULL);

  if (info->merge_func && (field = gst_structure_id_get_field (list, tag)) != NULL) {
    GValue value2 = { 0, };
    switch (mode) {
      case GST_TAG_MERGE_REPLACE_ALL:
      case GST_TAG_MERGE_REPLACE:
	gst_structure_id_set_value (list, tag, value);
	break;
      case GST_TAG_MERGE_PREPEND:
	gst_value_list_concat (&value2, value, &field->value);
	gst_structure_id_set_value (list, tag, &value2);
	g_value_unset (&value2);
	break;
      case GST_TAG_MERGE_APPEND:
	gst_value_list_concat (&value2, &field->value, value);
	gst_structure_id_set_value (list, tag, &value2);
	g_value_unset (&value2);
	break;
      case GST_TAG_MERGE_KEEP:
      case GST_TAG_MERGE_KEEP_ALL:
	break;
      default:
	g_assert_not_reached ();
	break;
    }
  } else {
    switch (mode) {
      case GST_TAG_MERGE_APPEND:
      case GST_TAG_MERGE_KEEP:
	if (gst_structure_id_get_field (list, tag) != NULL)
	  break;
	/* fall through */
      case GST_TAG_MERGE_REPLACE_ALL:
      case GST_TAG_MERGE_REPLACE:
      case GST_TAG_MERGE_PREPEND:
	gst_structure_id_set_value (list, tag, value);
	break;
      case GST_TAG_MERGE_KEEP_ALL:
	break;
      default:
	g_assert_not_reached ();
	break;
    }
  }
}
static void
gst_tag_list_copy_foreach (GstStructure *structure, GQuark tag, GValue *value, gpointer user_data)
{
  GstTagCopyData *copy = (GstTagCopyData *) user_data;

  gst_tag_list_add_value_internal (copy->list, copy->mode, tag, value);
}
/**
 * gst_tag_list_insert:
 * @into: list to merge into
 * @from: list to merge from
 * @mode: the mode to use
 * 
 * Inserts the tags of the second list into the first list using the given mode.
 */
void
gst_tag_list_insert (GstTagList *into, const GstTagList *from, GstTagMergeMode mode)
{
  GstTagCopyData data;
  
  g_return_if_fail (GST_IS_TAG_LIST (into));
  g_return_if_fail (GST_IS_TAG_LIST (from));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));

  data.list = (GstStructure *) into;
  data.mode = mode;
  if (mode == GST_TAG_MERGE_REPLACE_ALL) {
    gst_structure_remove_all_fields (data.list);
  }
  gst_structure_field_foreach ((GstStructure *) from, gst_tag_list_copy_foreach, &data);
}
/**
 * gst_tag_list_copy:
 * @list: list to copy
 *
 * Copies a given #GstTagList.
 *
 * Returns: copy of the given list
 */
GstTagList *
gst_tag_list_copy (const GstTagList *list)
{
  g_return_val_if_fail (GST_IS_TAG_LIST (list), NULL);
  
  return GST_TAG_LIST (gst_structure_copy ((GstStructure *) list));
}
/**
 * gst_tag_list_merge:
 * @list1: first list to merge
 * @list2: second list to merge
 * @mode: the mode to use
 * 
 * Merges the two given lists into a new list. If one of the lists is NULL, a
 * copy of the other is returned. If both lists are NULL, NULL is returned.
 *
 * Returns: the new list
 */
GstTagList *
gst_tag_list_merge (const GstTagList *list1, const GstTagList *list2, GstTagMergeMode mode)
{
  g_return_val_if_fail (list1 == NULL || GST_IS_TAG_LIST (list1), NULL);
  g_return_val_if_fail (list2 == NULL || GST_IS_TAG_LIST (list2), NULL);
  g_return_val_if_fail (GST_TAG_MODE_IS_VALID (mode), NULL);

  if (!list1 && !list2) {
    return NULL;
  } else if (!list1) {
    return gst_tag_list_copy (list2);
  } else if (!list2) {
    return gst_tag_list_copy (list1);
  } else {
    GstTagList *ret;

    ret = gst_tag_list_copy (list1);
    gst_tag_list_insert (ret, list2, mode);
    return ret;
  }
}
/**
 * gst_tag_list_free:
 * @list: the list to free
 *
 * Frees the given list and all associated values.
 */
void
gst_tag_list_free (GstTagList *list)
{
  g_return_if_fail (GST_IS_TAG_LIST (list));
  gst_structure_free ((GstStructure *) list);
}
/**
 * gst_tag_list_get_tag_size:
 * @list: a taglist
 * @tag: the tag to query
 *
 * Checks how many value are stored in this tag list for the given tag.
 *
 * Returns: The number of tags stored
 */
guint
gst_tag_list_get_tag_size (const GstTagList *list, const gchar *tag)
{
  const GValue *value;

  g_return_val_if_fail (GST_IS_TAG_LIST (list), 0);

  value = gst_structure_get_value ((GstStructure *) list, tag);
  if (value == NULL)
    return 0;
  if (G_VALUE_TYPE (value) != GST_TYPE_LIST)
    return 1;

  return gst_value_list_get_size (value);
}
/**
 * gst_tag_list_add:
 * @list: list to set tags in
 * @mode: the mode to use
 * @tag: tag
 * @...: values to set
 *
 * Sets the values for the given tags using the specified mode.
 */
void
gst_tag_list_add (GstTagList *list, GstTagMergeMode mode, const gchar *tag, ...)
{
  va_list args;

  g_return_if_fail (GST_IS_TAG_LIST (list));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));
  g_return_if_fail (tag != NULL);
  
  va_start (args, tag);
  gst_tag_list_add_valist (list, mode, tag, args);
  va_end (args);
}
/**
 * gst_tag_list_add_valist:
 * @list: list to set tags in
 * @mode: the mode to use
 * @tag: tag
 * @var_args: tag / value pairs to set
 *
 * Sets the values for the given tags using the specified mode.
 */
void
gst_tag_list_add_valist (GstTagList *list, GstTagMergeMode mode, const gchar *tag, va_list var_args)
{
  GstTagInfo *info;
  GQuark quark;
  gchar *error = NULL;
  
  g_return_if_fail (GST_IS_TAG_LIST (list));
  g_return_if_fail (GST_TAG_MODE_IS_VALID (mode));
  g_return_if_fail (tag != NULL);
  
  while (tag != NULL) {
    GValue value = { 0, };
    quark = g_quark_from_string (tag);
    info = gst_tag_lookup (quark);
    g_return_if_fail (info != NULL);
    g_value_init (&value, info->type);
    G_VALUE_COLLECT (&value, var_args, 0, &error);
    if (error) {
      g_warning ("%s: %s", G_STRLOC, error);
      g_free (error);
      /* we purposely leak the value here, it might not be
       * in a sane state if an error condition occoured
       */
      return;
    }
    gst_tag_list_add_value_internal (list, mode, quark, &value);
    g_value_unset (&value);
    tag = va_arg (var_args, gchar *);
  }
}
/**
 * gst_tag_list_remove_tag:
 * @list: list to remove tag from
 * @tag: tag to remove
 *
 * Removes the goven tag from the taglist.
 */
void
gst_tag_list_remove_tag (GstTagList *list, const gchar *tag)
{
  g_return_if_fail (GST_IS_TAG_LIST (list));
  g_return_if_fail (tag != NULL);

  gst_structure_remove_field ((GstStructure *) list, tag);
}
typedef struct {
  GstTagForeachFunc	func;
  gpointer		data;
} TagForeachData;
static void 
structure_foreach_wrapper (GstStructure *structure, GQuark field_id, 
	GValue *value, gpointer user_data)
{
  TagForeachData *data = (TagForeachData *) user_data;
  data->func (GST_TAG_LIST (structure), g_quark_to_string (field_id), data->data);
}
/**
 * gst_tag_list_foreach:
 * @list: list to iterate over
 * @func: function to be called for each tag
 * @user_data: user specified data
 *
 * Calls the given function for each tag inside the tag list. Note that if there
 * is no tag, the function won't be called at all.
 */
void
gst_tag_list_foreach (GstTagList *list, GstTagForeachFunc func, gpointer user_data)
{
  TagForeachData data;

  g_return_if_fail (GST_IS_TAG_LIST (list));
  g_return_if_fail (func != NULL);
  
  data.func = func;
  data.data = user_data;
  gst_structure_field_foreach ((GstStructure *) list, structure_foreach_wrapper, &data);
}

/***** tag events *****/

/**
 * gst_event_new_tag:
 * @list: the tag list to put into the event or NULL for an empty list
 *
 * Creates a new tag event with the given list and takes ownership of it.
 *
 * Returns: a new tag event
 */
GstEvent *
gst_event_new_tag (GstTagList *list)
{
  GstEvent *ret;
  
  g_return_val_if_fail (list == NULL || GST_IS_TAG_LIST (list), NULL);

  ret = gst_event_new (GST_EVENT_TAG);
  if (!list)
    list = gst_tag_list_new ();
  ret->event_data.structure.structure = (GstStructure *) list;
  
  return ret;
}
/**
 * get_event_tag_get_list:
 * @tag_event: a tagging #GstEvent
 *
 * Gets the taglist from a given tagging event.
 * 
 * Returns: The #GstTagList of the event
 */
GstTagList *
gst_event_tag_get_list (GstEvent *tag_event)
{
  g_return_val_if_fail (GST_IS_EVENT (tag_event), NULL);
  g_return_val_if_fail (GST_EVENT_TYPE (tag_event) == GST_EVENT_TAG, NULL);

  return GST_TAG_LIST (tag_event->event_data.structure.structure);
}

/**
 * gst_tag_list_get_value_index:
 * @list: a #GStTagList
 * @tag: tag to read out
 * @index: number of entry to read out
 *
 * Gets the value that is at the given index for the given tag in the given 
 * list.
 * 
 * Returns: The GValue for the specified entry or NULL if the tag wasn't available
 *	    or the tag doesn't have as many entries
 */
G_CONST_RETURN GValue *
gst_tag_list_get_value_index (const GstTagList *list, const gchar *tag, guint index)
{
  const GValue *value;

  g_return_val_if_fail (GST_IS_TAG_LIST (list), NULL);
  g_return_val_if_fail (tag != NULL, NULL);
  
  value = gst_structure_get_value ((GstStructure *) list, tag);
  if (value == NULL) return  NULL;
  
  if (GST_VALUE_HOLDS_LIST (value)) {
    if (index >= gst_value_list_get_size (value)) return NULL;
    return gst_value_list_get_value (value, index);
  } else {
    if (index > 0) return NULL;
    return value;
  }
}

/**
 * gst_tag_list_copy_value:
 * @dest: uninitialized #GValue to copy into
 * @list: list to get the tag from
 * @tag: tag to read out
 *
 * Copies the contents for the given tag into the value, merging multiple values 
 * into one if multiple values are associated with the tag.
 * You must g_value_unset() the value after use.
 *
 * Returns: TRUE, if a value was copied, FALSE if the tag didn't exist in the 
 *	    given list.
 */
gboolean
gst_tag_list_copy_value (GValue *dest, const GstTagList *list, const gchar *tag)
{
  const GValue *src;
  
  g_return_val_if_fail (GST_IS_TAG_LIST (list), FALSE);
  g_return_val_if_fail (tag != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (dest) == 0, FALSE);
  
  src = gst_structure_get_value ((GstStructure *) list, tag);
  if (!src) return FALSE;
  
  if (G_VALUE_TYPE (src) == GST_TYPE_LIST) {    
    GstTagInfo *info = gst_tag_lookup (g_quark_from_string (tag));
    /* must be there or lists aren't allowed */
    g_assert (info->merge_func);
    info->merge_func (dest, src);
  } else {
    g_value_init (dest, G_VALUE_TYPE (src));
    g_value_copy (src, dest);
  }
  return TRUE;
}

/***** evil macros to get all the gst_tag_list_get_*() functions right *****/

#define TAG_MERGE_FUNCS(name,type)						\
gboolean									\
gst_tag_list_get_ ## name (const GstTagList *list, const gchar *tag,		\
			   type *value)						\
{										\
  GValue v = { 0, };								\
										\
  g_return_val_if_fail (GST_IS_TAG_LIST (list), FALSE);				\
  g_return_val_if_fail (tag != NULL, FALSE);					\
  g_return_val_if_fail (value != NULL, FALSE);					\
										\
  if (!gst_tag_list_copy_value (&v, list, tag))					\
      return FALSE;								\
  *value = COPY_FUNC (g_value_get_ ## name (&v));				\
  g_value_unset (&v);								\
  return TRUE;									\
}										\
										\
gboolean									\
gst_tag_list_get_ ## name ## _index (const GstTagList *list, const gchar *tag, 	\
			   guint index, type *value)			  	\
{										\
  const GValue *v;    								\
										\
  g_return_val_if_fail (GST_IS_TAG_LIST (list), FALSE);				\
  g_return_val_if_fail (tag != NULL, FALSE);					\
  g_return_val_if_fail (value != NULL, FALSE);					\
										\
  if ((v = gst_tag_list_get_value_index (list, tag, index)) == NULL)		\
      return FALSE;								\
  *value = COPY_FUNC (g_value_get_ ## name (v));			      	\
  return TRUE;									\
}

#define COPY_FUNC /**/
TAG_MERGE_FUNCS (char, gchar)
TAG_MERGE_FUNCS (uchar, guchar)
TAG_MERGE_FUNCS (boolean, gboolean)
TAG_MERGE_FUNCS (int, gint)
TAG_MERGE_FUNCS (uint, guint)
TAG_MERGE_FUNCS (long, glong)
TAG_MERGE_FUNCS (ulong, gulong)
TAG_MERGE_FUNCS (int64, gint64)
TAG_MERGE_FUNCS (uint64, guint64)
TAG_MERGE_FUNCS (float, gfloat)
TAG_MERGE_FUNCS (double, gdouble)
#undef COPY_FUNC
  
#define COPY_FUNC g_strdup
TAG_MERGE_FUNCS (string, gchar *)




