/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * formats.c: Tests the different formats on alsasink
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

#include "sinesrc.h"

GstElement *pipeline;
gint channels = 1;
gboolean sign = FALSE;
gint endianness = G_LITTLE_ENDIAN;
gint depth = 8;
gint width = 8;

#define NUMBER_OF_INT_TESTS 28
#define NUMBER_OF_FLOAT_TESTS 2
#define NUMBER_OF_LAW_TESTS 2

gint last = 0;
gint counter = 0;

static void create_pipeline (void);


static void
pre_get_func (SineSrc * src)
{
  counter++;
};
static void
create_pipeline (void)
{
  GstElement *src;
  SineSrc *sinesrc;
  GstElement *alsasink;

  pipeline = gst_pipeline_new ("pipeline");
  src = sinesrc_new ();
  alsasink = gst_element_factory_make ("alsasink", "alsasink");

  gst_bin_add_many (GST_BIN (pipeline), src, alsasink, NULL);
  gst_element_link (src, alsasink);

  /* prepare our sinesrc */
  sinesrc = (SineSrc *) src;
  sinesrc->pre_get_func = pre_get_func;
  sinesrc->newcaps = TRUE;
  /* int tests */
  if (last < NUMBER_OF_INT_TESTS) {
    sinesrc->type = SINE_SRC_INT;
    sinesrc->sign = ((last % 2) == 0) ? TRUE : FALSE;
    sinesrc->endianness =
        ((last / 2) % 2 == 0) ? G_LITTLE_ENDIAN : G_BIG_ENDIAN;
    switch ((last / 4) % 8) {
      case 0:
        sinesrc->depth = 8;
        sinesrc->width = 8;
        break;
      case 1:
        sinesrc->depth = 16;
        sinesrc->width = 16;
        break;
      case 2:
        sinesrc->depth = 24;
        sinesrc->width = 32;
        break;
      case 3:
        sinesrc->depth = 32;
        sinesrc->width = 32;
        break;
        /* nomore tests below until i know what 24bit width means to alsa wrt endianness */
      case 4:
        sinesrc->depth = 24;
        sinesrc->width = 24;
        break;
      case 5:
        sinesrc->depth = 20;
        sinesrc->width = 24;
        break;
      case 6:
        sinesrc->depth = 18;
        sinesrc->width = 24;
        break;
      case 7:
        /* not used yet */
        sinesrc->depth = 8;
        sinesrc->width = 8;
        break;
      default:
        g_assert_not_reached ();
    }

    g_print ("Setting format to: format:     \"int\"\n"
        "                   sign:       %s\n"
        "                   endianness: %d\n"
        "                   width:      %d\n"
        "                   depth:      %d\n",
        sinesrc->sign ? "TRUE" : "FALSE", sinesrc->endianness,
        sinesrc->width, sinesrc->depth);
  } else if (last < NUMBER_OF_INT_TESTS + NUMBER_OF_FLOAT_TESTS) {
    gint temp = last - NUMBER_OF_INT_TESTS;

    sinesrc->type = SINE_SRC_FLOAT;
    switch (temp) {
      case 0:
        sinesrc->width = 32;
        break;
      case 1:
        sinesrc->width = 64;
        break;
      default:
        g_assert_not_reached ();
    }
    g_print ("Setting format to float width %d\n", sinesrc->width);
  } else if (last <
      NUMBER_OF_INT_TESTS + NUMBER_OF_FLOAT_TESTS + NUMBER_OF_LAW_TESTS) {
    gint temp = last - NUMBER_OF_INT_TESTS - NUMBER_OF_FLOAT_TESTS;
    GstElement *law;

    sinesrc->type = SINE_SRC_INT;
    sinesrc->sign = TRUE;
    sinesrc->endianness = G_BYTE_ORDER;
    sinesrc->depth = 16;
    sinesrc->width = 16;

    if (temp == 0) {
      law = gst_element_factory_make ("mulawenc", "mulaw");
    } else {
      law = gst_element_factory_make ("alawenc", "alaw");
    }
    g_assert (law);
    gst_element_unlink (src, alsasink);
    gst_bin_add (GST_BIN (pipeline), law);
    gst_element_link_many (src, law, alsasink, NULL);
    if (temp == 0) {
      g_print ("Setting format to: format:     \"MU law\"\n");
    } else {
      g_print ("Setting format to: format:     \"A law\"\n");
    }
  } else {
    g_print ("All formats work like a charm.\n");
    exit (0);
  }
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

gint
main (gint argc, gchar * argv[])
{
  gst_init (&argc, &argv);

  g_print ("\n"
      "This test will test the various formats ALSA and GStreamer support.\n"
      "You will hear a short sine tone on your default ALSA soundcard for every\n"
      "format tested. They should all sound the same (incl. volume).\n" "\n");
  create_pipeline ();

  while (pipeline) {
    gst_bin_iterate (GST_BIN (pipeline));
    if ((counter / 200) > last) {
      last = counter / 200;
      gst_object_unref (GST_OBJECT (pipeline));
      create_pipeline ();
    }
  }

  return 0;
}
