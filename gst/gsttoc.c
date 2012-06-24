/* GStreamer
 * (c) 2010, 2012 Alexander Saprykin <xelfium@gmail.com>
 *
 * gsttoc.c: GstToc initialization and parsing/creation
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
 * SECTION:gsttoc
 * @short_description: Generic table of contents support
 * @see_also: #GstStructure, #GstEvent, #GstMessage, #GstQuery, #GstPad
 *
 * #GstToc functions are used to create/free #GstToc and #GstTocEntry structures.
 * Also they are used to convert #GstToc into #GstStructure and vice versa.
 *
 * #GstToc lets you to inform other elements in pipeline or application that playing
 * source has some kind of table of contents (TOC). These may be chapters, editions,
 * angles or other types. For example: DVD chapters, Matroska chapters or cue sheet
 * TOC. Such TOC will be useful for applications to display instead of just a
 * playlist.
 *
 * Using TOC is very easy. Firstly, create #GstToc structure which represents root
 * contents of the source. You can also attach TOC-specific tags to it. Then fill
 * it with #GstTocEntry entries by appending them to #GstToc.entries #GstTocEntry.subentries
 * lists. You should use GST_TOC_ENTRY_TYPE_CHAPTER for generic TOC entry and
 * GST_TOC_ENTRY_TYPE_EDITION for the entries which are considered to be alternatives
 * (like DVD angles, Matroska editions and so on).
 *
 * Note that root level of the TOC can contain only either editions or chapters. You
 * should not mix them together at the same level. Otherwise you will get serialization
 * /deserialization errors. Make sure that no one of the entries has negative start and
 *  stop values.
 *
 * Please, use #GstToc.info and #GstTocEntry.info fields in that way: create a #GstStructure,
 * put all info related to your element there and put this structure into the info field under
 * the name of your element. Some fields in the info structure can be used for internal purposes,
 * so you should use it in the way described above to not to overwrite already existent fields.
 *
 * Use gst_event_new_toc() to create a new TOC #GstEvent, and gst_event_parse_toc() to
 * parse received TOC event. Use gst_event_new_toc_select() to create a new TOC select #GstEvent,
 * and gst_event_parse_toc_select() to parse received TOC select event. The same rule for
 * the #GstMessage: gst_message_new_toc() to create new TOC #GstMessage, and
 * gst_message_parse_toc() to parse received TOC message. Also you can create a new TOC query
 * with gst_query_new_toc(), set it with gst_query_set_toc() and parse it with
 * gst_query_parse_toc().
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst_private.h"
#include "gstenumtypes.h"
#include "gsttaglist.h"
#include "gststructure.h"
#include "gstvalue.h"
#include "gsttoc.h"
#include "gstpad.h"
#include "gstquark.h"

#undef gst_toc_copy
static GstToc *gst_toc_copy (const GstToc * toc);
static void gst_toc_free (GstToc * toc);
#undef gst_toc_entry_copy
static GstTocEntry *gst_toc_entry_copy (const GstTocEntry * toc);
static void gst_toc_entry_free (GstTocEntry * toc);

GST_DEFINE_MINI_OBJECT_TYPE (GstToc, gst_toc);
GST_DEFINE_MINI_OBJECT_TYPE (GstTocEntry, gst_toc_entry);

/**
 * gst_toc_new:
 *
 * Create a new #GstToc structure.
 *
 * Returns: (transfer full): newly allocated #GstToc structure, free it
 *     with gst_toc_unref().
 *
 * Since: 0.10.37
 */
GstToc *
gst_toc_new (void)
{
  GstToc *toc;

  toc = g_slice_new0 (GstToc);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (toc), GST_TYPE_TOC,
      (GstMiniObjectCopyFunction) gst_toc_copy, NULL,
      (GstMiniObjectFreeFunction) gst_toc_free);

  toc->tags = gst_tag_list_new_empty ();
  toc->info = gst_structure_new_id_empty (GST_QUARK (INFO_STRUCTURE));

  return toc;
}

static GstTocEntry *
gst_toc_entry_new_internal (GstTocEntryType type, const gchar * uid,
    GstPad * pad)
{
  GstTocEntry *entry;

  entry = g_slice_new0 (GstTocEntry);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (entry), GST_TYPE_TOC_ENTRY,
      (GstMiniObjectCopyFunction) gst_toc_entry_copy, NULL,
      (GstMiniObjectFreeFunction) gst_toc_entry_free);

  entry->uid = g_strdup (uid);
  entry->type = type;
  entry->tags = gst_tag_list_new_empty ();
  entry->info = gst_structure_new_id_empty (GST_QUARK (INFO_STRUCTURE));

  if (pad != NULL && GST_IS_PAD (pad))
    entry->pads = g_list_append (entry->pads, gst_object_ref (pad));

  return entry;
}

/**
 * gst_toc_entry_new:
 * @type: entry type.
 * @uid: unique ID (UID) in the whole TOC.
 *
 * Create new #GstTocEntry structure.
 *
 * Returns: newly allocated #GstTocEntry structure, free it with gst_toc_entry_unref().
 *
 * Since: 0.10.37
 */
GstTocEntry *
gst_toc_entry_new (GstTocEntryType type, const gchar * uid)
{
  g_return_val_if_fail (uid != NULL, NULL);

  return gst_toc_entry_new_internal (type, uid, NULL);
}

/**
 * gst_toc_entry_new_with_pad:
 * @type: entry type.
 * @uid: unique ID (UID) in the whole TOC.
 * @pad: #GstPad related to this entry.
 *
 * Create new #GstTocEntry structure with #GstPad related.
 *
 * Returns: newly allocated #GstTocEntry structure, free it with gst_toc_entry_unref()
 * when done.
 *
 * Since: 0.10.37
 */
GstTocEntry *
gst_toc_entry_new_with_pad (GstTocEntryType type, const gchar * uid,
    GstPad * pad)
{
  g_return_val_if_fail (uid != NULL, NULL);

  return gst_toc_entry_new_internal (type, uid, pad);
}

static void
gst_toc_free (GstToc * toc)
{
  g_list_foreach (toc->entries, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (toc->entries);

  if (toc->tags != NULL)
    gst_tag_list_unref (toc->tags);

  if (toc->info != NULL)
    gst_structure_free (toc->info);

  g_slice_free (GstToc, toc);
}

static void
gst_toc_entry_free (GstTocEntry * entry)
{
  GList *cur;

  g_return_if_fail (entry != NULL);

  g_list_foreach (entry->subentries, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (entry->subentries);

  g_free (entry->uid);

  if (entry->tags != NULL)
    gst_tag_list_unref (entry->tags);

  if (entry->info != NULL)
    gst_structure_free (entry->info);

  cur = entry->pads;
  while (cur != NULL) {
    if (GST_IS_PAD (cur->data))
      gst_object_unref (cur->data);
    cur = cur->next;
  }

  g_list_free (entry->pads);

  g_slice_free (GstTocEntry, entry);
}

static GstStructure *
gst_toc_structure_new (GstTagList * tags, GstStructure * info)
{
  GstStructure *ret;

  ret = gst_structure_new_id_empty (GST_QUARK (TOC));

  if (tags != NULL) {
    gst_structure_id_set (ret, GST_QUARK (TAGS), GST_TYPE_TAG_LIST, tags, NULL);
  }

  if (info != NULL) {
    gst_structure_id_set (ret, GST_QUARK (INFO), GST_TYPE_STRUCTURE, info,
        NULL);
  }

  return ret;
}

static GstStructure *
gst_toc_entry_structure_new (GstTocEntryType type, const gchar * uid,
    GstTagList * tags, GstStructure * info)
{
  GstStructure *ret;

  ret = gst_structure_new_id_empty (GST_QUARK (TOC_ENTRY));

  gst_structure_id_set (ret, GST_QUARK (TYPE), GST_TYPE_TOC_ENTRY_TYPE, type,
      NULL);
  gst_structure_id_set (ret, GST_QUARK (UID), G_TYPE_STRING, uid, NULL);

  if (tags != NULL) {
    gst_structure_id_set (ret, GST_QUARK (TAGS), GST_TYPE_TAG_LIST, tags, NULL);
  }

  if (info != NULL) {
    gst_structure_id_set (ret, GST_QUARK (INFO), GST_TYPE_STRUCTURE, info,
        NULL);
  }

  return ret;
}

static guint
gst_toc_entry_structure_n_subentries (const GstStructure * entry)
{
  if (G_UNLIKELY (!gst_structure_id_has_field_typed (entry,
              GST_QUARK (SUB_ENTRIES), GST_TYPE_ARRAY)))
    return 0;
  else
    return gst_value_array_get_size ((gst_structure_id_get_value (entry,
                GST_QUARK (SUB_ENTRIES))));
}

static const GstStructure *
gst_toc_entry_structure_nth_subentry (const GstStructure * entry, guint nth)
{
  guint count;
  const GValue *array;

  count = gst_toc_entry_structure_n_subentries (entry);

  if (count < nth)
    return NULL;

  if (G_UNLIKELY (!gst_structure_id_has_field_typed (entry,
              GST_QUARK (SUB_ENTRIES), GST_TYPE_ARRAY)))
    return NULL;
  else {
    array =
        gst_value_array_get_value (gst_structure_id_get_value (entry,
            GST_QUARK (SUB_ENTRIES)), nth);
    return gst_value_get_structure (array);
  }
}

static GstTocEntry *
gst_toc_entry_from_structure (const GstStructure * entry, guint level)
{
  GstTocEntry *ret, *subentry;
  const GValue *val;
  const GstStructure *subentry_struct;
  GstTagList *list;
  GstStructure *st;
  gint count, i;
  const gchar *uid;
  guint chapters_count = 0, editions_count = 0;

  g_return_val_if_fail (entry != NULL, NULL);
  g_return_val_if_fail (gst_structure_id_has_field_typed (entry,
          GST_QUARK (UID), G_TYPE_STRING), NULL);
  g_return_val_if_fail (gst_structure_id_has_field_typed (entry,
          GST_QUARK (TYPE), GST_TYPE_TOC_ENTRY_TYPE), NULL);

  val = gst_structure_id_get_value (entry, GST_QUARK (UID));
  uid = g_value_get_string (val);

  ret = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, uid);

  gst_structure_get_enum (entry, g_quark_to_string (GST_QUARK (TYPE)),
      GST_TYPE_TOC_ENTRY_TYPE, (gint *) & (ret->type));

  if (gst_structure_id_has_field_typed (entry,
          GST_QUARK (SUB_ENTRIES), GST_TYPE_ARRAY)) {
    count = gst_toc_entry_structure_n_subentries (entry);

    for (i = 0; i < count; ++i) {
      subentry_struct = gst_toc_entry_structure_nth_subentry (entry, i);
      subentry = gst_toc_entry_from_structure (subentry_struct, level + 1);

      /* skip empty editions */
      if (G_UNLIKELY (subentry->type == GST_TOC_ENTRY_TYPE_EDITION
              && subentry->subentries == NULL)) {
        g_warning
            ("Empty edition found while deserializing TOC from GstStructure, skipping");
        continue;
      }

      if (subentry->type == GST_TOC_ENTRY_TYPE_EDITION)
        ++editions_count;
      else
        ++chapters_count;

      /* check for mixed content */
      if (G_UNLIKELY (chapters_count > 0 && editions_count > 0)) {
        g_critical
            ("Mixed editions and chapters in the TOC contents, the TOC is broken");
        gst_toc_entry_unref (subentry);
        gst_toc_entry_unref (ret);
        return NULL;
      }

      if (G_UNLIKELY (subentry == NULL)) {
        gst_toc_entry_unref (ret);
        return NULL;
      }

      ret->subentries = g_list_prepend (ret->subentries, subentry);
    }

    ret->subentries = g_list_reverse (ret->subentries);
  }

  if (gst_structure_id_has_field_typed (entry, GST_QUARK (TAGS),
          GST_TYPE_TAG_LIST)) {
    val = gst_structure_id_get_value (entry, GST_QUARK (TAGS));

    if (G_LIKELY (GST_IS_TAG_LIST (g_value_get_boxed (val)))) {
      list = gst_tag_list_copy (GST_TAG_LIST (g_value_get_boxed (val)));
      gst_tag_list_unref (ret->tags);
      ret->tags = list;
    }
  }

  if (gst_structure_id_has_field_typed (entry,
          GST_QUARK (INFO), GST_TYPE_STRUCTURE)) {
    val = gst_structure_id_get_value (entry, GST_QUARK (INFO));

    if (G_LIKELY (GST_IS_STRUCTURE (gst_value_get_structure (val)))) {
      st = gst_structure_copy (gst_value_get_structure (val));
      gst_structure_free (ret->info);
      ret->info = st;
    }
  }

  return ret;
}

GstToc *
__gst_toc_from_structure (const GstStructure * toc)
{
  GstToc *ret;
  GstTocEntry *subentry;
  const GstStructure *subentry_struct;
  const GValue *val;
  GstTagList *list;
  GstStructure *st;
  guint count, i;
  guint editions_count = 0, chapters_count = 0;

  g_return_val_if_fail (toc != NULL, NULL);

  ret = gst_toc_new ();

  if (gst_structure_id_has_field_typed (toc,
          GST_QUARK (SUB_ENTRIES), GST_TYPE_ARRAY)) {
    count = gst_toc_entry_structure_n_subentries (toc);

    for (i = 0; i < count; ++i) {
      subentry_struct = gst_toc_entry_structure_nth_subentry (toc, i);
      subentry = gst_toc_entry_from_structure (subentry_struct, 0);

      /* skip empty editions */
      if (G_UNLIKELY (subentry->type == GST_TOC_ENTRY_TYPE_EDITION
              && subentry->subentries == NULL)) {
        g_warning
            ("Empty edition found while deserializing TOC from GstStructure, skipping");
        continue;
      }

      /* check for success */
      if (G_UNLIKELY (subentry == NULL)) {
        g_critical ("Couldn't serialize deserializing TOC from GstStructure");
        gst_toc_free (ret);
        return NULL;
      }

      if (subentry->type == GST_TOC_ENTRY_TYPE_EDITION)
        ++editions_count;
      else
        ++chapters_count;

      /* check for mixed content */
      if (G_UNLIKELY (chapters_count > 0 && editions_count > 0)) {
        g_critical
            ("Mixed editions and chapters in the TOC contents, the TOC is broken");
        gst_toc_entry_unref (subentry);
        gst_toc_free (ret);
        return NULL;
      }

      ret->entries = g_list_prepend (ret->entries, subentry);
    }

    ret->entries = g_list_reverse (ret->entries);
  }

  if (gst_structure_id_has_field_typed (toc, GST_QUARK (TAGS),
          GST_TYPE_TAG_LIST)) {
    val = gst_structure_id_get_value (toc, GST_QUARK (TAGS));

    if (G_LIKELY (GST_IS_TAG_LIST (g_value_get_boxed (val)))) {
      list = gst_tag_list_copy (GST_TAG_LIST (g_value_get_boxed (val)));
      gst_tag_list_unref (ret->tags);
      ret->tags = list;
    }
  }

  if (gst_structure_id_has_field_typed (toc,
          GST_QUARK (INFO), GST_TYPE_STRUCTURE)) {
    val = gst_structure_id_get_value (toc, GST_QUARK (INFO));

    if (G_LIKELY (GST_IS_STRUCTURE (gst_value_get_structure (val)))) {
      st = gst_structure_copy (gst_value_get_structure (val));
      gst_structure_free (ret->info);
      ret->info = st;
    }
  }

  if (G_UNLIKELY (ret->entries == NULL)) {
    gst_toc_free (ret);
    return NULL;
  }

  return ret;
}

static GstStructure *
gst_toc_entry_to_structure (const GstTocEntry * entry, guint level)
{
  GstStructure *ret, *subentry_struct;
  GstTocEntry *subentry;
  GList *cur;
  GValue subentries_val = { 0 };
  GValue entry_val = { 0 };
  guint chapters_count = 0, editions_count = 0;

  g_return_val_if_fail (entry != NULL, NULL);

  ret =
      gst_toc_entry_structure_new (entry->type, entry->uid, entry->tags,
      entry->info);

  g_value_init (&subentries_val, GST_TYPE_ARRAY);
  g_value_init (&entry_val, GST_TYPE_STRUCTURE);

  cur = entry->subentries;
  while (cur != NULL) {
    subentry = cur->data;

    if (subentry->type == GST_TOC_ENTRY_TYPE_EDITION)
      ++editions_count;
    else
      ++chapters_count;

    /* check for mixed content */
    if (G_UNLIKELY (chapters_count > 0 && editions_count > 0)) {
      g_critical
          ("Mixed editions and chapters in the TOC contents, the TOC is broken");
      gst_structure_free (ret);
      g_value_unset (&entry_val);
      g_value_unset (&subentries_val);
      return NULL;
    }

    /* skip empty editions */
    if (G_UNLIKELY (subentry->type == GST_TOC_ENTRY_TYPE_EDITION
            && subentry->subentries == NULL)) {
      g_warning
          ("Empty edition found while serializing TOC to GstStructure, skipping");
      cur = cur->next;
      continue;
    }

    subentry_struct = gst_toc_entry_to_structure (subentry, level + 1);

    /* check for success */
    if (G_UNLIKELY (subentry_struct == NULL)) {
      gst_structure_free (ret);
      g_value_unset (&subentries_val);
      g_value_unset (&entry_val);
      return NULL;
    }

    /* skip empty editions */
    if (G_UNLIKELY (subentry->type == GST_TOC_ENTRY_TYPE_EDITION
            && subentry->subentries == NULL)) {
      g_warning
          ("Empty edition found while serializing TOC to GstStructure, skipping");
      cur = cur->next;
      continue;
    }

    gst_value_set_structure (&entry_val, subentry_struct);
    gst_value_array_append_value (&subentries_val, &entry_val);
    gst_structure_free (subentry_struct);

    cur = cur->next;
  }

  gst_structure_id_set_value (ret, GST_QUARK (SUB_ENTRIES), &subentries_val);

  g_value_unset (&subentries_val);
  g_value_unset (&entry_val);
  return ret;
}

GstStructure *
__gst_toc_to_structure (const GstToc * toc)
{
  GValue val = { 0 };
  GValue subentries_val = { 0 };
  GstStructure *ret, *subentry_struct;
  GstTocEntry *subentry;
  GList *cur;
  guint editions_count = 0, chapters_count = 0;

  g_return_val_if_fail (toc != NULL, NULL);
  g_return_val_if_fail (toc->entries != NULL, NULL);

  ret = gst_toc_structure_new (toc->tags, toc->info);

  g_value_init (&val, GST_TYPE_STRUCTURE);
  g_value_init (&subentries_val, GST_TYPE_ARRAY);
  cur = toc->entries;

  while (cur != NULL) {
    subentry = cur->data;

    if (subentry->type == GST_TOC_ENTRY_TYPE_EDITION)
      ++editions_count;
    else
      ++chapters_count;

    /* check for mixed content */
    if (G_UNLIKELY (chapters_count > 0 && editions_count > 0)) {
      g_critical
          ("Mixed editions and chapters in the TOC contents, the TOC is broken");
      gst_structure_free (ret);
      g_value_unset (&val);
      g_value_unset (&subentries_val);
      return NULL;
    }

    /* skip empty editions */
    if (G_UNLIKELY (subentry->type == GST_TOC_ENTRY_TYPE_EDITION
            && subentry->subentries == NULL)) {
      g_warning
          ("Empty edition found while serializing TOC to GstStructure, skipping");
      cur = cur->next;
      continue;
    }

    subentry_struct = gst_toc_entry_to_structure (subentry, 0);

    /* check for success */
    if (G_UNLIKELY (subentry_struct == NULL)) {
      g_critical ("Couldn't serialize TOC to GstStructure");
      gst_structure_free (ret);
      g_value_unset (&val);
      g_value_unset (&subentries_val);
      return NULL;
    }

    gst_value_set_structure (&val, subentry_struct);
    gst_value_array_append_value (&subentries_val, &val);
    gst_structure_free (subentry_struct);

    cur = cur->next;
  }

  gst_structure_id_set_value (ret, GST_QUARK (SUB_ENTRIES), &subentries_val);

  g_value_unset (&val);
  g_value_unset (&subentries_val);
  return ret;
}

static gboolean
gst_toc_check_entry_for_uid (const GstTocEntry * entry, const gchar * uid)
{
  GList *cur;

  g_return_val_if_fail (entry != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  if (g_strcmp0 (entry->uid, uid) == 0)
    return TRUE;

  cur = entry->subentries;
  while (cur != NULL) {
    if (gst_toc_check_entry_for_uid (cur->data, uid))
      return TRUE;
    cur = cur->next;
  }

  return FALSE;
}

/**
 * gst_toc_find_entry:
 * @toc: #GstToc to search in.
 * @uid: UID to find #GstTocEntry with.
 *
 * Find #GstTocEntry with given @uid in the @toc.
 *
 * Returns: #GstTocEntry with specified @uid from the @toc, or NULL if not found.
 *
 * Since: 0.10.37
 */
GstTocEntry *
gst_toc_find_entry (const GstToc * toc, const gchar * uid)
{
  GList *cur;

  g_return_val_if_fail (toc != NULL, NULL);
  g_return_val_if_fail (uid != NULL, NULL);

  cur = toc->entries;
  while (cur != NULL) {
    if (gst_toc_check_entry_for_uid (cur->data, uid))
      return cur->data;
    cur = cur->next;
  }

  return NULL;
}

/**
 * gst_toc_entry_copy:
 * @entry: #GstTocEntry to copy.
 *
 * Copy #GstTocEntry with all subentries (deep copy).
 *
 * Returns: newly allocated #GstTocEntry in case of success, NULL otherwise;
 * free it when done with gst_toc_entry_unref().
 *
 * Since: 0.10.37
 */
static GstTocEntry *
gst_toc_entry_copy (const GstTocEntry * entry)
{
  GstTocEntry *ret, *sub;
  GList *cur;
  GstTagList *list;
  GstStructure *st;

  g_return_val_if_fail (entry != NULL, NULL);

  ret = gst_toc_entry_new (entry->type, entry->uid);

  if (GST_IS_STRUCTURE (entry->info)) {
    st = gst_structure_copy (entry->info);
    gst_structure_free (ret->info);
    ret->info = st;
  }

  if (GST_IS_TAG_LIST (entry->tags)) {
    list = gst_tag_list_copy (entry->tags);
    gst_tag_list_unref (ret->tags);
    ret->tags = list;
  }

  cur = entry->pads;
  while (cur != NULL) {
    if (GST_IS_PAD (cur->data))
      ret->pads = g_list_prepend (ret->pads, gst_object_ref (cur->data));
    cur = cur->next;
  }
  ret->pads = g_list_reverse (ret->pads);

  cur = entry->subentries;
  while (cur != NULL) {
    sub = gst_toc_entry_copy (cur->data);

    if (sub != NULL)
      ret->subentries = g_list_prepend (ret->subentries, sub);

    cur = cur->next;
  }
  ret->subentries = g_list_reverse (ret->subentries);

  return ret;
}

/**
 * gst_toc_copy:
 * @toc: #GstToc to copy.
 *
 * Copy #GstToc with all subentries (deep copy).
 *
 * Returns: newly allocated #GstToc in case of success, NULL otherwise;
 * free it when done with gst_toc_free().
 *
 * Since: 0.10.37
 */
static GstToc *
gst_toc_copy (const GstToc * toc)
{
  GstToc *ret;
  GstTocEntry *entry;
  GList *cur;
  GstTagList *list;
  GstStructure *st;

  g_return_val_if_fail (toc != NULL, NULL);

  ret = gst_toc_new ();

  if (GST_IS_STRUCTURE (toc->info)) {
    st = gst_structure_copy (toc->info);
    gst_structure_free (ret->info);
    ret->info = st;
  }

  if (GST_IS_TAG_LIST (toc->tags)) {
    list = gst_tag_list_copy (toc->tags);
    gst_tag_list_unref (ret->tags);
    ret->tags = list;
  }

  cur = toc->entries;
  while (cur != NULL) {
    entry = gst_toc_entry_copy (cur->data);

    if (entry != NULL)
      ret->entries = g_list_prepend (ret->entries, entry);

    cur = cur->next;
  }
  ret->entries = g_list_reverse (ret->entries);

  return ret;
}

/**
 * gst_toc_entry_set_start_stop:
 * @entry: #GstTocEntry to set values.
 * @start: start value to set.
 * @stop: stop value to set.
 *
 * Set @start and @stop values for the @entry.
 *
 * Since: 0.10.37
 */
void
gst_toc_entry_set_start_stop (GstTocEntry * entry, gint64 start, gint64 stop)
{
  const GValue *val;
  GstStructure *structure = NULL;

  g_return_if_fail (entry != NULL);
  g_return_if_fail (GST_IS_STRUCTURE (entry->info));

  if (gst_structure_id_has_field_typed (entry->info, GST_QUARK (TIME),
          GST_TYPE_STRUCTURE)) {
    val = gst_structure_id_get_value (entry->info, GST_QUARK (TIME));
    structure = gst_structure_copy (gst_value_get_structure (val));
  }

  if (structure == NULL)
    structure = gst_structure_new_id_empty (GST_QUARK (TIME_STRUCTURE));

  gst_structure_id_set (structure, GST_QUARK (START),
      G_TYPE_INT64, start, GST_QUARK (STOP), G_TYPE_INT64, stop, NULL);

  gst_structure_id_set (entry->info, GST_QUARK (TIME),
      GST_TYPE_STRUCTURE, structure, NULL);

  gst_structure_free (structure);
}

/**
 * gst_toc_entry_get_start_stop:
 * @entry: #GstTocEntry to get values from.
 * @start: (out): the storage for the start value, leave #NULL if not need.
 * @stop: (out): the storage for the stop value, leave #NULL if not need.
 *
 * Get start and stop values from the @entry and write them into appropriate storages.
 *
 * Returns: TRUE if all non-NULL storage pointers were filled with appropriate values,
 * FALSE otherwise.
 *
 * Since: 0.10.37
 */
gboolean
gst_toc_entry_get_start_stop (const GstTocEntry * entry, gint64 * start,
    gint64 * stop)
{
  gboolean ret = TRUE;
  const GValue *val;
  const GstStructure *structure;

  g_return_val_if_fail (entry != NULL, FALSE);
  g_return_val_if_fail (GST_IS_STRUCTURE (entry->info), FALSE);

  if (!gst_structure_id_has_field_typed (entry->info,
          GST_QUARK (TIME), GST_TYPE_STRUCTURE))
    return FALSE;

  val = gst_structure_id_get_value (entry->info, GST_QUARK (TIME));
  structure = gst_value_get_structure (val);

  if (start != NULL) {
    if (gst_structure_id_has_field_typed (structure,
            GST_QUARK (START), G_TYPE_INT64))
      *start =
          g_value_get_int64 (gst_structure_id_get_value (structure,
              GST_QUARK (START)));
    else
      ret = FALSE;
  }

  if (stop != NULL) {
    if (gst_structure_id_has_field_typed (structure,
            GST_QUARK (STOP), G_TYPE_INT64))
      *stop =
          g_value_get_int64 (gst_structure_id_get_value (structure,
              GST_QUARK (STOP)));
    else
      ret = FALSE;
  }

  return ret;
}

/**
 * gst_toc_entry_type_get_nick:
 * @type: a #GstTocEntryType.
 *
 * Converts @type to a string representation.
 *
 * Returns: Returns the human-readable @type. Can be NULL if an error occurred.
 * Since: 0.11.92
 */
const gchar *
gst_toc_entry_type_get_nick (GstTocEntryType type)
{
  const gchar *entry_types[] = { "chapter", "edition" };

  g_return_val_if_fail ((gint) type >= 0
      && (gint) type < G_N_ELEMENTS (entry_types), NULL);
  return entry_types[type];
}

gboolean
__gst_toc_structure_get_updated (const GstStructure * toc)
{
  const GValue *val;

  g_return_val_if_fail (GST_IS_STRUCTURE (toc), FALSE);

  if (G_LIKELY (gst_structure_id_has_field_typed (toc,
              GST_QUARK (UPDATED), G_TYPE_BOOLEAN))) {
    val = gst_structure_id_get_value (toc, GST_QUARK (UPDATED));
    return g_value_get_boolean (val);
  }

  return FALSE;
}

void
__gst_toc_structure_set_updated (GstStructure * toc, gboolean updated)
{
  g_return_if_fail (toc != NULL);

  gst_structure_id_set (toc, GST_QUARK (UPDATED), G_TYPE_BOOLEAN, updated,
      NULL);
}

gchar *
__gst_toc_structure_get_extend_uid (const GstStructure * toc)
{
  const GValue *val;

  g_return_val_if_fail (GST_IS_STRUCTURE (toc), NULL);

  if (G_LIKELY (gst_structure_id_has_field_typed (toc,
              GST_QUARK (EXTEND_UID), G_TYPE_STRING))) {
    val = gst_structure_id_get_value (toc, GST_QUARK (EXTEND_UID));
    return g_strdup (g_value_get_string (val));
  }

  return NULL;
}

void
__gst_toc_structure_set_extend_uid (GstStructure * toc,
    const gchar * extend_uid)
{
  g_return_if_fail (toc != NULL);
  g_return_if_fail (extend_uid != NULL);

  gst_structure_id_set (toc, GST_QUARK (EXTEND_UID), G_TYPE_STRING, extend_uid,
      NULL);
}
