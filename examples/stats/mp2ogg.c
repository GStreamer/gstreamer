/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>

/* This example app demonstartes the use of pad query and convert to
 * get usefull statistics about a plugin. In this case we monitor the
 * compression status of mpeg audio to ogg vorbis transcoding.
 */

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GError *error = NULL;
  gchar *description;
  GstElement *encoder, *decoder;
  GstPad *dec_sink, *enc_src;

  gst_init (&argc, &argv);

  if (argc < 3) {
    g_print ("usage: %s <inputfile> <outputfile>\n", argv[0]);
    return -1;
  }

  description = g_strdup_printf ("filesrc location=\"%s\" ! mad name=decoder ! "
      "vorbisenc name=encoder ! filesink location=\"%s\"", argv[1], argv[2]);

  pipeline = GST_ELEMENT (gst_parse_launch (description, &error));
  if (!pipeline) {
    if (error)
      g_print ("ERROR: pipeline could not be constructed: %s\n",
          error->message);
    else
      g_print ("ERROR: pipeline could not be constructed\n");
    return -1;
  }

  decoder = gst_bin_get_by_name (GST_BIN (pipeline), "decoder");
  encoder = gst_bin_get_by_name (GST_BIN (pipeline), "encoder");

  dec_sink = gst_element_get_pad (decoder, "sink");
  enc_src = gst_element_get_pad (encoder, "src");

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS) {
    g_print ("pipeline doesn't want to play\n");
    return -1;
  }

  while (gst_bin_iterate (GST_BIN (pipeline))) {
    gint64 position;
    gint64 duration;
    gint64 bitrate_enc, bitrate_dec;
    GstFormat format;

    format = GST_FORMAT_TIME;
    /* get the position */
    gst_pad_query (enc_src, GST_QUERY_POSITION, &format, &position);

    /* get the total duration */
    gst_pad_query (enc_src, GST_QUERY_TOTAL, &format, &duration);

    format = GST_FORMAT_BYTES;
    /* see how many bytes are genereated per 8 seconds (== bitrate) */
    gst_pad_convert (enc_src, GST_FORMAT_TIME, 8 * GST_SECOND,
        &format, &bitrate_enc);

    gst_pad_convert (dec_sink, GST_FORMAT_TIME, 8 * GST_SECOND,
        &format, &bitrate_dec);

    g_print ("[%2dm %.2ds] of [%2dm %.2ds], "
        "src avg bitrate: %lld, dest avg birate: %lld, ratio [%02.2f]    \r",
        (gint) (position / (GST_SECOND * 60)),
        (gint) (position / (GST_SECOND)) % 60,
        (gint) (duration / (GST_SECOND * 60)),
        (gint) (duration / (GST_SECOND)) % 60,
        bitrate_dec, bitrate_enc, (gfloat) bitrate_dec / bitrate_enc);
  }

  g_print ("\n");

  return 0;
}
