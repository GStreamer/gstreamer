/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * category.c: test the categories
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

GST_DEBUG_CATEGORY (cat);
#define GST_CAT_DEFAULT cat
GST_DEBUG_CATEGORY_STATIC (cat_static);

gint
main (gint argc, gchar * argv[])
{
  GSList *before, *after;

  unsetenv ("GST_DEBUG");
  gst_init (&argc, &argv);

  before = gst_debug_get_all_categories ();
  GST_DEBUG_CATEGORY_INIT (cat, "cat", GST_DEBUG_FG_GREEN,
      "default category for this test");
  GST_DEBUG_CATEGORY_INIT (cat_static, "cat_static",
      GST_DEBUG_BOLD | GST_DEBUG_FG_BLUE | GST_DEBUG_BG_RED,
      "static category for this test");
  after = gst_debug_get_all_categories ();

  g_print ("removing default log function\n");
  g_assert (gst_debug_remove_log_function (gst_debug_log_default) == 1);
  g_print
      ("checking, if the two new categories are put into the category list correctly...\n");
  g_assert (g_slist_length (after) - g_slist_length (before) == 2);
  /* check the _get stuff */
  g_print
      ("checking, if the gst_debug_category_get_* stuff works with the categories...\n");
  g_assert (strcmp (gst_debug_category_get_name (cat), "cat") == 0);
  g_assert (gst_debug_category_get_color (cat) == GST_DEBUG_FG_GREEN);
  g_assert (strcmp (gst_debug_category_get_description (cat),
	  "default category for this test") == 0);
  g_assert (gst_debug_category_get_threshold (cat) ==
      gst_debug_get_default_threshold ());
  g_assert (strcmp (gst_debug_category_get_name (cat_static),
	  "cat_static") == 0);
  g_assert (gst_debug_category_get_color (cat_static) | GST_DEBUG_FG_GREEN);
  g_assert (gst_debug_category_get_color (cat_static) | GST_DEBUG_BG_RED);
  g_assert (gst_debug_category_get_color (cat_static) | GST_DEBUG_BOLD);
  g_assert (strcmp (gst_debug_category_get_description (cat_static),
	  "static category for this test") == 0);
  g_assert (gst_debug_category_get_threshold (cat_static) ==
      gst_debug_get_default_threshold ());
  /* check if setting levels for names work */
  g_print
      ("checking if changing threshold for names affects existing categories...\n");
  gst_debug_set_threshold_for_name ("cat", GST_LEVEL_DEBUG);
  g_assert (gst_debug_category_get_threshold (cat) == GST_LEVEL_DEBUG);
  g_assert (gst_debug_category_get_threshold (cat_static) ==
      gst_debug_get_default_threshold ());
  gst_debug_set_threshold_for_name ("cat_static", GST_LEVEL_INFO);
  g_assert (gst_debug_category_get_threshold (cat) == GST_LEVEL_DEBUG);
  g_assert (gst_debug_category_get_threshold (cat_static) == GST_LEVEL_INFO);

  g_print ("everything ok.\n");
  return 0;
}
