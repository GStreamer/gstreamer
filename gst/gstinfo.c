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

#include <dlfcn.h>
#include "gst_private.h"
#include "gstelement.h"
#include "gstpad.h"

extern gchar *_gst_progname;


/***** DEBUG system *****/
GHashTable *__gst_function_pointers = NULL;



/***** INFO system *****/
GstInfoHandler _gst_info_handler = gst_default_info_handler;
#ifdef GST_INFO_ENABLED_VERBOSE
guint32 _gst_info_categories = 0xffffffff;
#else
guint32 _gst_info_categories = 0x00000001;
#endif

static gchar *_gst_info_category_strings[] = {
  "GST_INIT",
  "COTHREADS",
  "COTHREAD_SWITCH",
  "AUTOPLUG",
  "AUTOPLUG_ATTEMPT",
  "PARENTAGE",
  "STATES",
  "PLANNING",
  "SCHEDULING",
  "DATAFLOW",
  "BUFFER",
  "CAPS",
  "CLOCK",
  "ELEMENT_PADS",
  "ELEMENTFACTORY",
  "PADS",
  "PIPELINE",
  "PLUGIN_LOADING",
  "PLUGIN_ERRORS",
  "PLUGIN_INFO",
  "PROPERTIES",
  "THREAD",
  "TYPES",
  "XML",
  "NEGOTIATION",
};

/*
 * Attribute codes:
 * 00=none 01=bold 04=underscore 05=blink 07=reverse 08=concealed
 * Text color codes:
 * 30=black 31=red 32=green 33=yellow 34=blue 35=magenta 36=cyan 37=white
 * Background color codes:
 * 40=black 41=red 42=green 43=yellow 44=blue 45=magenta 46=cyan 47=white
 */

const gchar *_gst_category_colors[32] = {
  [GST_CAT_GST_INIT]		= "07;37",
  [GST_CAT_COTHREADS]		= "00;32",
  [GST_CAT_COTHREAD_SWITCH]	= "00;32",
  [GST_CAT_AUTOPLUG]		= "00;34",
  [GST_CAT_AUTOPLUG_ATTEMPT]	= "00;36;44",
  [GST_CAT_PARENTAGE]		= "01;37;41",		// !!
  [GST_CAT_STATES]		= "00;31",
  [GST_CAT_PLANNING]		= "07;35",
  [GST_CAT_SCHEDULING]		= "00;35",
  [GST_CAT_DATAFLOW]		= "00;32",
  [GST_CAT_BUFFER]		= "00;32",
  [GST_CAT_CAPS]		= "04;34",
  [GST_CAT_CLOCK]		= "00;33",		// !!
  [GST_CAT_ELEMENT_PADS]	= "01;37;41",		// !!
  [GST_CAT_ELEMENTFACTORY]	= "01;37;41",		// !!
  [GST_CAT_PADS]		= "01;37;41",		// !!
  [GST_CAT_PIPELINE]		= "01;37;41",		// !!
  [GST_CAT_PLUGIN_LOADING]	= "00;36",
  [GST_CAT_PLUGIN_ERRORS]	= "05;31",
  [GST_CAT_PLUGIN_INFO]		= "00;36",
  [GST_CAT_PROPERTIES]		= "00;37;44",		// !!
  [GST_CAT_THREAD]		= "00;31",
  [GST_CAT_TYPES]		= "01;37;41",		// !!
  [GST_CAT_XML]			= "01;37;41",		// !!
  [GST_CAT_NEGOTIATION]		= "07;34",

  [31]				= "",
};


/* colorization hash */
inline gint _gst_debug_stringhash_color(gchar *file) {
  int filecolor = 0;
  while (file[0]) filecolor += *(char *)(file++);
  filecolor = (filecolor % 6) + 31;
  return filecolor;
}


/**
 * gst_default_info_handler:
 * @category: category of the INFO message
 * @file: the file the INFO occurs in
 * @function: the function the INFO occurs in
 * @line: the line number in the file
 * @debug_string: the current debug_string in the function, if any
 * @element: pointer to the #GstElement in question
 * @string: the actual INFO string
 *
 * Prints out the INFO mesage in a variant of the following form:
 *
 *   INFO:gst_function:542(args): [elementname] something neat happened
 */
void
gst_default_info_handler (gint category, gchar *file, gchar *function,
                           gint line, gchar *debug_string,
                           void *element, gchar *string)
{
  gchar *empty = "";
  gchar *elementname = empty,*location = empty;
  int pthread_id = getpid();
  int cothread_id = cothread_getcurrent();
#ifdef GST_DEBUG_COLOR
  int pthread_color = pthread_id%6 + 31;
  int cothread_color = (cothread_id < 0) ? 37 : (cothread_id%6 + 31);
#endif

  if (debug_string == NULL) debug_string = "";
  if (category != GST_CAT_GST_INIT)
    location = g_strdup_printf("%s:%d%s:",function,line,debug_string);
  if (element && GST_IS_ELEMENT (element))
    elementname = g_strdup_printf (" \033[04m[%s]\033[00m", GST_OBJECT_NAME (element));

#ifdef GST_DEBUG_ENABLED
  #ifdef GST_DEBUG_COLOR
    fprintf(stderr,"INFO (\033[00;%dm%5d\033[00m:\033[00;%dm%2d\033[00m)\033["
            GST_DEBUG_CHAR_MODE ";%sm%s%s\033[00m %s\n",
            pthread_color,pthread_id,cothread_color,cothread_id,
            _gst_category_colors[category],location,elementname,string);
  #else
    fprintf(stderr,"INFO (%5d:%2d)%s%s %s\n",
            getpid(),cothread_id,location,elementname,string);
  #endif /* GST_DEBUG_COLOR */
#else
  #ifdef GST_DEBUG_COLOR
    fprintf(stderr,"INFO:\033[" GST_DEBUG_CHAR_MODE ";%sm%s%s\033[00m %s\n",
            location,elementname,_gst_category_colors[category],string);
  #else
    fprintf(stderr,"INFO:%s%s %s\n",
            location,elementname,string);
  #endif /* GST_DEBUG_COLOR */
#endif

  if (location != empty) g_free(location);
  if (elementname != empty) g_free(elementname);

  g_free(string);
}

/**
 * gst_info_set_categories:
 * @categories: bitmask of INFO categories to enable
 *
 * Enable the output of INFO categories based on the given bitmask.
 * The bit for any given category is (1 << GST_CAT_...).
 */
void
gst_info_set_categories (guint32 categories) {
  _gst_info_categories = categories;
  if (categories)
    GST_INFO (0, "setting INFO categories to 0x%08X",categories);
}

/**
 * gst_info_get_categories:
 *
 * Returns: the current bitmask of enabled INFO categories
 * The bit for any given category is (1 << GST_CAT_...).
 */
guint32
gst_info_get_categories () {
  return _gst_info_categories;
}

/**
 * gst_info_enable_category:
 * @category: the category to enable
 *
 * Enables the given GST_CAT_... INFO category.
 */
void
gst_info_enable_category (gint category) {
  _gst_info_categories |= (1 << category);
  if (_gst_info_categories)
    GST_INFO (0, "setting INFO categories to 0x%08X",_gst_info_categories);
}

/**
 * gst_info_disable_category:
 * @category: the category to disable
 *
 * Disables the given GST_CAT_... INFO category.
 */
void
gst_info_disable_category (gint category) {
  _gst_info_categories &= ~ (1 << category);
  if (_gst_info_categories)
    GST_INFO (0, "setting INFO categories to 0x%08X",_gst_info_categories);
}



/***** DEBUG system *****/
guint32 _gst_debug_categories = 0x00000000;


/**
 * gst_debug_set_categories:
 * @categories: bitmask of DEBUG categories to enable
 *
 * Enable the output of DEBUG categories based on the given bitmask.
 * The bit for any given category is (1 << GST_CAT_...).
 */
void
gst_debug_set_categories (guint32 categories) {
  _gst_debug_categories = categories;
  if (categories)
    GST_INFO (0, "setting DEBUG categories to 0x%08X",categories);
}

/**
 * gst_debug_get_categories:
 *
 * Returns: the current bitmask of enabled DEBUG categories
 * The bit for any given category is (1 << GST_CAT_...).
 */
guint32
gst_debug_get_categories () {
  return _gst_debug_categories;
}

/**
 * gst_debug_enable_category:
 * @category: the category to enable
 *
 * Enables the given GST_CAT_... DEBUG category.
 */
void
gst_debug_enable_category (gint category) {
  _gst_debug_categories |= (1 << category);
  if (_gst_debug_categories)
    GST_INFO (0, "setting DEBUG categories to 0x%08X",_gst_debug_categories);
}

/**
 * gst_debug_disable_category:
 * @category: the category to disable
 *
 * Disables the given GST_CAT_... DEBUG category.
 */
void
gst_debug_disable_category (gint category) {
  _gst_debug_categories &= ~ (1 << category);
  if (_gst_debug_categories)
    GST_INFO (0, "setting DEBUG categories to 0x%08X",_gst_debug_categories);
}

/**
 * gst_get_category_name:
 * @category: the category to return the name of
 *
 * Returns: string containing the name of the category
 */
const gchar *
gst_get_category_name (gint category) {
  if ((category >= 0) && (category < GST_CAT_MAX_CATEGORY))
    return _gst_info_category_strings[category];
  else
    return NULL;
}



/***** ERROR system *****/
GstErrorHandler _gst_error_handler = gst_default_error_handler;

/**
 * gst_default_error_handler:
 * @file: the file the ERROR occurs in
 * @function: the function the INFO occurs in
 * @line: the line number in the file
 * @debug_string: the current debug_string in the function, if any
 * @element: pointer to the #GstElement in question
 * @object: pointer to a related object
 * @string: the actual ERROR string
 *
 * Prints out the given ERROR string in a variant of the following format:
 *
 * ***** GStreamer ERROR ***** in file gstsomething.c at gst_function:399(arg)
 * Element: /pipeline/thread/element.src
 * Error: peer is null!
 * ***** attempting to stack trace.... *****
 *
 * At the end, it attempts to print the stack trace via GDB.
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



#ifdef __USE_GNU
#warning __USE_GNU is defined
#endif

gchar *
_gst_debug_nameof_funcptr (void *ptr)
{
  gchar *ptrname;
  Dl_info dlinfo;
  if (__gst_function_pointers) {
    if (ptrname = g_hash_table_lookup(__gst_function_pointers,ptr))
      return g_strdup(ptrname);
  } else if (dladdr(ptr,&dlinfo)) {
    return g_strdup(dlinfo.dli_sname);
  } else {
    return g_strdup_printf("%p",ptr);
  }
}
