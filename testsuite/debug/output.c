/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * output.c: Test if the debugging output macros work
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

GST_DEBUG_CATEGORY_STATIC (cat_default);
#define GST_CAT_DEFAULT cat_default
GST_DEBUG_CATEGORY_STATIC (cat2);

static gint count = -1;
static GstElement *pipeline;

static void
check_message (GstDebugCategory *category, GstDebugLevel level, const gchar *file,
	       const gchar *function, gint line, GObject *object, GstDebugMessage *message,
	       gpointer unused)
{
  gint temp;
  
  /* these checks require count to be set right. So the order in the main
     funtion is actually important. */
  /* <0 means no checks */
  if (count < 0) return;
  
  g_print ("expecting \"%s\"...", (gchar *) message);
  /* level */
  temp = (count % 5) + 1;
  g_assert (level == temp);
  /* category */
  temp = (count % 10) / 5;
  g_assert (category == (temp ? cat2 : cat_default));
  /* object */
  temp = (count % 20) / 10;
  g_assert (object == (GObject *) (temp ? pipeline : NULL));
  g_print ("[OK]\n");
}

gint 
main (gint argc, gchar *argv[]) 
{

  gst_init (&argc, &argv);
  
  GST_DEBUG_CATEGORY_INIT (cat_default, "GST_Check_default", 0, "default category for this test");
  GST_DEBUG_CATEGORY_INIT (cat2, "GST_Check_2", 0, "second category for this test");
  g_assert (gst_debug_remove_log_function (gst_debug_log_default) == 1);
  gst_debug_add_log_function (check_message, NULL);	
  
  count = 0;
  GST_ERROR ("This is an error.");
  ++count;
  GST_WARNING ("This is a warning.");
  ++count;
  GST_INFO ("This is an info message.");
  ++count;
  GST_DEBUG ("This is a debug message.");
  ++count;
  GST_LOG ("This is a log message.");
  ++count;
  GST_CAT_ERROR (cat2, "This is an error with category.");
  ++count;
  GST_CAT_WARNING (cat2, "This is a warning with category.");
  ++count;
  GST_CAT_INFO (cat2, "This is an info message with category.");
  ++count;
  GST_CAT_DEBUG (cat2, "This is a debug message with category.");
  ++count;
  GST_CAT_LOG (cat2, "This is a log message with category.");
  count = -1;
  pipeline = gst_element_factory_make ("pipeline", "testelement");
  count = 10;
  GST_ERROR_OBJECT (pipeline, "This is an error with object.");
  ++count;
  GST_WARNING_OBJECT (pipeline, "This is a warning with object.");
  ++count;
  GST_INFO_OBJECT (pipeline, "This is an info message with object.");
  ++count;
  GST_DEBUG_OBJECT (pipeline, "This is a debug message with object.");
  ++count;
  GST_LOG_OBJECT (pipeline, "This is a log message with object.");
  ++count;
  GST_CAT_ERROR_OBJECT (cat2, pipeline, "This is an error with category and object.");
  ++count;
  GST_CAT_WARNING_OBJECT (cat2, pipeline, "This is a warning with category and object.");
  ++count;
  GST_CAT_INFO_OBJECT (cat2, pipeline, "This is an info message with category and object.");
  ++count;
  GST_CAT_DEBUG_OBJECT (cat2, pipeline, "This is a debug message with category and object.");
  ++count;
  GST_CAT_LOG_OBJECT (cat2, pipeline, "This is a log message with category and object.");
  count = -1;

  g_assert (gst_debug_remove_log_function (check_message) == 1);	
  
  return 0;
}
