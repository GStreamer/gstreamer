/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstinfo.c: INFO, ERROR, and DEBUG systems
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
#include "gst.h"

extern gchar *_gst_progname;


/***** DEBUG system *****/
GHashTable *__gst_function_pointers = NULL;



/***** INFO system *****/
GstInfoHandler _gst_info_handler = gst_default_info_handler;
//guint32 _gst_info_categories = 0xffffffff;
guint32 _gst_info_categories = 0x00000001;

static gchar *_gst_info_category_strings[] = {
  "GST_INIT",
  "COTHREADS",
  "COTHREAD_SWITCH",
  "AUTOPLUG",
  "AUTOPLUG_ATTEMPT",
  "PARENTAGE",
  "STATES",
  "PLANING",
  "SCHEDULING",
  "OPERATION",
  "BUFFER",
  "CAPS",
  "CLOCK",
  "ELEMENT_PADS",
  "ELEMENTFACTORY",
  "PADS",
  "PIPELINE",
  "PLUGIN_LOADING",
  "PLUGIN_ERRORS",
  "PROPERTIES",
  "THREAD",
  "TYPES",
  "XML",
};

void
gst_default_info_handler (gint category, gchar *file, gchar *function,
                           gint line, gchar *debug_string,
                           void *element, gchar *string)
{
  gchar *empty = "";
  gchar *elementname = empty,*location = empty;

  if (debug_string == NULL) debug_string = "";
  if (category != GST_INFO_GST_INIT)
    location = g_strdup_printf("%s:%d%s:",function,line,debug_string);
  if (element && GST_IS_ELEMENT (element))
    elementname = g_strdup_printf (" [%s]",gst_element_get_name (element));

  fprintf(stderr,"INFO:%s%s %s\n",location,elementname,string);

  if (location != empty) g_free(location);
  if (elementname != empty) g_free(elementname);

  g_free(string);
}

void
gst_info_set_categories (guint32 categories) {
  _gst_info_categories = categories;
}

guint32
gst_info_get_categories () {
  return _gst_info_categories;
}

const gchar *
gst_info_get_category_name (gint category) {
  return _gst_info_category_strings[category];
}

void
gst_info_enable_category (gint category) {
  _gst_info_categories |= (1 << category);
}

void
gst_info_disable_category (gint category) {
  _gst_info_categories &= ~ (1 << category);
}



/***** ERROR system *****/
GstErrorHandler _gst_error_handler = gst_default_error_handler;

void
gst_default_error_handler (gchar *file, gchar *function,
                           gint line, gchar *debug_string,
                           void *element, void *object, gchar *string)
{
  int chars = 0;
  gchar *path;
  int i;

  // if there are NULL pointers, point them to null strings to clean up output
  if (!debug_string) debug_string = "";
  if (!string) string = "";

  // print out a preamble
  fprintf(stderr,"***** GStreamer ERROR ***** in file %s at %s:%d%s\n",
          file,function,line,debug_string);

  // if there's an element, print out the pertinent information
  if (element) {
    if (GST_IS_OBJECT(element)) {
      path = gst_object_get_path_string(element);
      fprintf(stderr,"Element: %s",path);
      chars = 9 + strlen(path);
      g_free(path);
    } else {
      fprintf(stderr,"Element ptr: %p",element);
      chars = 15 + sizeof(void*)*2;
    }
  }

  // if there's an object, print it out as well
  if (object) {
    // attempt to pad the line, or create a new one
    if (chars < 40)
      for (i=0;i<(40-chars)/8+1;i++) fprintf(stderr,"\t");
    else
      fprintf(stderr,"\n");

    if (GST_IS_OBJECT(object)) {
      path = gst_object_get_path_string(object);
      fprintf(stderr,"Object: %s",path);
      g_free(path);
    } else {
      fprintf(stderr,"Object ptr: %p",object);
    }
  }

  fprintf(stderr,"\n");
  fprintf(stderr,"Error: %s\n",string);

  g_free(string);

  fprintf(stderr,"***** attempting to stack trace.... *****\n");

  g_on_error_stack_trace (_gst_progname);

  exit(1);
}
