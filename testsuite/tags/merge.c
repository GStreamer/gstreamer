/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * parse1.c: Test various parsing stuff
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
 
#include <gst/gst.h>

#include <string.h>


/* multiple artists are possible */
#define UTAG GST_TAG_ARTIST
#define UNFIXED1 "Britney Spears"
#define UNFIXED2 "Evanescene"
#define UNFIXED3 "AC/DC"
#define UNFIXED4 "The Prodigy"

/* license is fixed */
#define FTAG GST_TAG_LICENSE
#define FIXED1 "Lesser General Public License"
#define FIXED2 "Microsoft End User License Agreement"
#define FIXED3 "Mozilla Public License"
#define FIXED4 "Public Domain"

/* checks that a tag contains the given values and not more values */
static void
check (const GstTagList *list, const gchar *tag, gchar *value, ...)
{
  va_list args;
  gchar *str;
  guint i = 0;

  va_start (args, value);
  while (value != NULL) {
    g_assert (gst_tag_list_get_string_index (list, tag, i, &str));
    g_assert (strcmp (value, str) == 0);
    g_free (str);
    
    value = va_arg (args, gchar *);
    i++;
  }
  g_assert (i == gst_tag_list_get_tag_size (list, tag));
  va_end (args);
}
#define NEW_LIST_FIXED(mode) G_STMT_START{ \
  if (list) gst_tag_list_free (list);\
  list = gst_tag_list_new (); \
  gst_tag_list_add (list, mode, FTAG, FIXED1, FTAG, FIXED2, FTAG, FIXED3, FTAG, FIXED4, NULL); \
}G_STMT_END
#define NEW_LIST_UNFIXED(mode) G_STMT_START{ \
  if (list) gst_tag_list_free (list);\
  list = gst_tag_list_new (); \
  gst_tag_list_add (list, mode, UTAG, UNFIXED1, UTAG, UNFIXED2, UTAG, UNFIXED3, UTAG, UNFIXED4, NULL);\
}G_STMT_END
#define NEW_LISTS_FIXED(mode) G_STMT_START{ \
  if (list) gst_tag_list_free (list);\
  list = gst_tag_list_new (); \
  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, FTAG, FIXED1, FTAG, FIXED2, NULL); \
  if (list2) gst_tag_list_free (list2);\
  list2 = gst_tag_list_new (); \
  gst_tag_list_add (list2, GST_TAG_MERGE_APPEND, FTAG, FIXED3, FTAG, FIXED4, NULL); \
  if (merge) gst_tag_list_free (merge);\
  merge = gst_tag_list_merge (list, list2, mode); \
}G_STMT_END
#define NEW_LISTS_UNFIXED(mode) G_STMT_START{ \
  if (list) gst_tag_list_free (list);\
  list = gst_tag_list_new (); \
  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, UTAG, UNFIXED1, UTAG, UNFIXED2, NULL); \
  if (list2) gst_tag_list_free (list2);\
  list2 = gst_tag_list_new (); \
  gst_tag_list_add (list2, GST_TAG_MERGE_APPEND, UTAG, UNFIXED3, UTAG, UNFIXED4, NULL); \
  if (merge) gst_tag_list_free (merge);\
  merge = gst_tag_list_merge (list, list2, mode); \
}G_STMT_END
gint 
main (gint argc, gchar *argv[]) 
{
  GstTagList *list = NULL, *list2 = NULL, *merge = NULL;
  
  gst_init (&argc, &argv);

  /* make sure the assumptions work */
  g_assert (gst_tag_is_fixed (FTAG));
  g_assert (!gst_tag_is_fixed (UTAG));
  /* we check string here only */
  g_assert (gst_tag_get_type (FTAG) == G_TYPE_STRING);
  g_assert (gst_tag_get_type (UTAG) == G_TYPE_STRING);
  
  /* check additions */
  /* unfixed */
  NEW_LIST_UNFIXED (GST_TAG_MERGE_REPLACE_ALL);
  check (list, UTAG, UNFIXED4, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_REPLACE);
  check (list, UTAG, UNFIXED4, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_PREPEND);
  check (list, UTAG, UNFIXED4, UNFIXED3, UNFIXED2, UNFIXED1, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_APPEND);
  check (list, UTAG, UNFIXED1, UNFIXED2, UNFIXED3, UNFIXED4, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_KEEP);
  check (list, UTAG, UNFIXED1, NULL);
  NEW_LIST_UNFIXED (GST_TAG_MERGE_KEEP_ALL);
  check (list, UTAG, NULL);
  /* fixed */
  NEW_LIST_FIXED (GST_TAG_MERGE_REPLACE_ALL);
  check (list, FTAG, FIXED4, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_REPLACE);
  check (list, FTAG, FIXED4, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_PREPEND);
  check (list, FTAG, FIXED4, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_APPEND);
  check (list, FTAG, FIXED1, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_KEEP);
  check (list, FTAG, FIXED1, NULL);
  NEW_LIST_FIXED (GST_TAG_MERGE_KEEP_ALL);
  check (list, FTAG, NULL);
  
  /* check merging */
  /* unfixed */ 
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_REPLACE_ALL);
  check (merge, UTAG, UNFIXED3, UNFIXED4, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_REPLACE);
  check (merge, UTAG, UNFIXED3, UNFIXED4, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_PREPEND);
  check (merge, UTAG, UNFIXED3, UNFIXED4, UNFIXED1, UNFIXED2, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_APPEND);
  check (merge, UTAG, UNFIXED1, UNFIXED2, UNFIXED3, UNFIXED4, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_KEEP);
  check (merge, UTAG, UNFIXED1, UNFIXED2, NULL);
  NEW_LISTS_UNFIXED (GST_TAG_MERGE_KEEP_ALL);
  check (merge, UTAG, UNFIXED1, UNFIXED2, NULL);
  /* fixed */ 
  NEW_LISTS_FIXED (GST_TAG_MERGE_REPLACE_ALL);
  check (merge, FTAG, FIXED3, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_REPLACE);
  check (merge, FTAG, FIXED3, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_PREPEND);
  check (merge, FTAG, FIXED3, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_APPEND);
  check (merge, FTAG, FIXED1, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_KEEP);
  check (merge, FTAG, FIXED1, NULL);
  NEW_LISTS_FIXED (GST_TAG_MERGE_KEEP_ALL);
  check (merge, FTAG, FIXED1, NULL);
  
  return 0;
}
