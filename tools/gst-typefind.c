/* GStreamer
 * Copyright (C) 2003 Thomas Vander Stichele <thomas@apestaart.org>
 *               2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *               2005 Andy Wingo <wingo@pobox.com>
 *
 * gst-typefind.c: Use GStreamer to find the type of a file
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

#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <gst/gst.h>


char *filename = NULL;


void
have_type_handler (GstElement * typefind, guint probability,
    const GstCaps * caps, gpointer unused)
{
  gchar *caps_str;

  caps_str = gst_caps_to_string (caps);
  g_print ("%s - %s\n", filename, caps_str);
  g_free (caps_str);
}

int
main (int argc, char *argv[])
{
  guint i = 1;
  GstElement *pipeline;
  GstElement *source, *typefind, *fakesink;

  setlocale (LC_ALL, "");

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("Please give a filename to typefind\n\n");
    return 1;
  }

  pipeline = gst_pipeline_new (NULL);

  /* don't hang too long trying to preroll */
  g_object_set (pipeline, "play-timeout", (gint64) 0.25 * GST_SECOND, NULL);

  source = gst_element_factory_make ("filesrc", "source");
  g_assert (GST_IS_ELEMENT (source));
  typefind = gst_element_factory_make ("typefind", "typefind");
  g_assert (GST_IS_ELEMENT (typefind));
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (GST_IS_ELEMENT (typefind));

  gst_bin_add_many (GST_BIN (pipeline), source, typefind, fakesink, NULL);
  gst_element_link_many (source, typefind, fakesink, NULL);

  g_signal_connect (G_OBJECT (typefind), "have-type",
      G_CALLBACK (have_type_handler), NULL);

  while (i < argc) {
    GstElementStateReturn sret;

    filename = argv[i];
    g_object_set (source, "location", filename, NULL);

    /* typefind will only commit to PAUSED if it actually finds a type;
     * otherwise the state change fails */
    sret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

    if (sret != GST_STATE_SUCCESS)
      g_print ("%s - No type found\n", argv[i]);

    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

    i++;
  }

  gst_object_unref (pipeline);
  return 0;
}
