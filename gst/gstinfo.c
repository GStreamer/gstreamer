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
guint32 _gst_info_categories = 0x00000000;

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
  if (element) {
    if (debug_string)
      fprintf(stderr,"INFO:%s:%d%s: [%s] %s\n",
              function,line,debug_string,gst_element_get_name(element),string);
     else
      fprintf(stderr,"INFO:%s:%d: [%s] %s\n",
              function,line,gst_element_get_name(element),string);
  } else {
    if (debug_string)
      fprintf(stderr,"INFO:%s:%d%s: %s\n",
              function,line,debug_string,string);
    else
      fprintf(stderr,"INFO:%s:%d: %s\n",
              function,line,string);
  }

  g_free(string);
}


/***** ERROR system *****/
GstErrorHandler _gst_error_handler = gst_default_error_handler;

/*
gchar *gst_object_get_path_string(GstObject *object) {
  GSList *parentage = NULL;
  GSList *parents;
  void *parent;
  gchar *prevpath, *path = "";
  const char *component;
  gchar *separator = "";
  gboolean free_component;

  parentage = g_slist_prepend (NULL, object);

  // first walk the object hierarchy to build a list of the parents
  do {
    if (GST_IS_OBJECT(object)) {
      if (GST_IS_PAD(object)) {
        parent = GST_PAD(object)->parent;
//      } else if (GST_IS_ELEMENT(object)) {
//        parent = gst_element_get_parent(GST_ELEMENT(object));
      } else {
        parent = gst_object_get_parent (object);
      }
    } else {
      parentage = g_slist_prepend (parentage, NULL);
      parent = NULL;
    }

    if (parent != NULL) {
      parentage = g_slist_prepend (parentage, parent);
    }

    object = parent;
  } while (object != NULL);

  // then walk the parent list and print them out
  parents = parentage;
  while (parents) {
    if (GST_IS_OBJECT(parents->data)) {
      if (GST_IS_PAD(parents->data)) {
        component = gst_pad_get_name(GST_PAD(parents->data));
        separator = ".";
        free_component = FALSE;
      } else if (GST_IS_ELEMENT(parents->data)) {
        component = gst_element_get_name(GST_ELEMENT(parents->data));
        separator = "/";
        free_component = FALSE;
      } else {
//        component = g_strdup_printf("a %s",gtk_type_name(gtk_identifier_get_type(parents->data)));
        component = g_strdup_printf("unknown%p",parents->data);
        separator = "/";
        free_component = TRUE;
      }
    } else {
      component = g_strdup_printf("%p",parents->data);
      separator = "/";
      free_component = TRUE;
    }

    prevpath = path;
    path = g_strjoin(separator,prevpath,component,NULL);
    g_free(prevpath);
    if (free_component)
      g_free((gchar *)component);

    parents = g_slist_next(parents);
  }

  g_slist_free(parentage);

  return path;
}
*/

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
