/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2002 Andy Wingo <wingo@pobox.com>
 *
 * gstparse.c: get a pipeline from a text pipeline description
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

#include <string.h>

#include "gst_private.h"

#include "gstparse.h"
#include "gstinfo.h"

extern GstElement *_gst_parse_launch (const gchar *, GError **);

GQuark
gst_parse_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gst_parse_error");
  return quark;
}

static gchar *
_gst_parse_escape (const gchar * str)
{
  GString *gstr = NULL;
  gchar *newstr = NULL;

  g_return_val_if_fail (str != NULL, NULL);

  gstr = g_string_sized_new (strlen (str));

  while (*str) {
    if (*str == ' ')
      g_string_append_c (gstr, '\\');
    g_string_append_c (gstr, *str);
    str++;
  }

  newstr = gstr->str;
  g_string_free (gstr, FALSE);

  return newstr;
}

/**
 * gst_parse_launchv:
 * @argv: null-terminated array of arguments
 * @error: pointer to a #GError
 *
 * Create a new element based on command line syntax.
 * #error will contain an error message if an erroneuos pipeline is specified.
 * An error does not mean that the pipeline could not be constructed.
 *
 * Returns: a new element on success and NULL on failure.
 */
GstElement *
gst_parse_launchv (const gchar ** argv, GError ** error)
{
  GstElement *element;
  GString *str;
  const gchar **argvp, *arg;
  gchar *tmp;

  g_return_val_if_fail (argv != NULL, NULL);

  /* let's give it a nice size. */
  str = g_string_sized_new (1024);

  argvp = argv;
  while (*argvp) {
    arg = *argvp;
    tmp = _gst_parse_escape (arg);
    g_string_append (str, tmp);
    g_free (tmp);
    g_string_append (str, " ");
    argvp++;
  }

  element = gst_parse_launch (str->str, error);

  g_string_free (str, TRUE);

  return element;
}

/**
 * gst_parse_launch:
 * @pipeline_description: the command line describing the pipeline
 * @error: the error message in case of an erroneous pipeline.
 *
 * Create a new pipeline based on command line syntax.
 * Please note that you might get a return value that is not NULL even though
 * the error is set. In this case there was a recoverable parsing error and you
 * can try to play the pipeline.
 *
 * Returns: a new element on success, NULL on failure. If more than one toplevel
 * element is specified by the pipeline_description, all elements are put into
 * a #GstPipeline ant that is returned.
 */
GstElement *
gst_parse_launch (const gchar * pipeline_description, GError ** error)
{
  GstElement *element;
  static GStaticMutex flex_lock = G_STATIC_MUTEX_INIT;

  g_return_val_if_fail (pipeline_description != NULL, NULL);

  GST_CAT_INFO (GST_CAT_PIPELINE, "parsing pipeline description %s",
      pipeline_description);

  /* the need for the mutex will go away with flex 2.5.6 */
  g_static_mutex_lock (&flex_lock);
  element = _gst_parse_launch (pipeline_description, error);
  g_static_mutex_unlock (&flex_lock);

  return element;
}
