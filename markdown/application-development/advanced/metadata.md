---
title: Metadata
...

# Metadata

GStreamer makes a clear distinction between the two types of metadata it
supports: Stream tags, which describe the content of a stream in a non-technical
way; and Stream-info, which is a somewhat technical description of the
properties of a stream. Stream tags examples include the author of a song, the
song's title or the album it belongs to. Examples for stream-info include video
size, audio samplerate, codecs used and so on.

Tags are handled using the GStreamer tagging system. Stream-info can be retrieved
from a `GstPad` by getting the current (negotiated) `GstCaps` for that pad.

## Metadata reading

Stream information can most easily be obtained by reading it from a `GstPad`.
Note that this requires access to all pads of which you want stream information.
This approach has already been discussed in [Using capabilities for metadata](application-development/basics/pads.md#using-capabilities-for-metadata).
Therefore, we will skip it here.

Tag reading is done through a bus in GStreamer. You can listen for
`GST_MESSAGE_TAG` messages and handle them as you wish. This has been discussed
previously in [Bus](application-development/basics/bus.md).

Note, however, that the `GST_MESSAGE_TAG` message may be emitted multiple
times and it is the application's responsibility to aggregate and display the tags
in a coherent way. This can be done using `gst_tag_list_merge ()` but make sure
to empty the cache when loading a new song, or after every few minutes when
listening to internet radio. Also, make sure you use `GST_TAG_MERGE_PREPEND` as
merging mode, so that a new title (which came in later) has precedence over
the old one.

The following example shows how to extract tags from a file and print them:

``` c
/* compile with:
 * gcc -o tags tags.c `pkg-config --cflags --libs gstreamer-1.0` */
#include <gst/gst.h>

static void
print_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  int i, num;

  num = gst_tag_list_get_tag_size (list, tag);
  for (i = 0; i < num; ++i) {
    const GValue *val;

    /* Note: when looking for specific tags, use the gst_tag_list_get_xyz() API,
     * we only use the GValue approach here because it is more generic */
    val = gst_tag_list_get_value_index (list, tag, i);
    if (G_VALUE_HOLDS_STRING (val)) {
      g_print ("\t%20s : %s\n", tag, g_value_get_string (val));
    } else if (G_VALUE_HOLDS_UINT (val)) {
      g_print ("\t%20s : %u\n", tag, g_value_get_uint (val));
    } else if (G_VALUE_HOLDS_DOUBLE (val)) {
      g_print ("\t%20s : %g\n", tag, g_value_get_double (val));
    } else if (G_VALUE_HOLDS_BOOLEAN (val)) {
      g_print ("\t%20s : %s\n", tag,
          (g_value_get_boolean (val)) ? "true" : "false");
    } else if (GST_VALUE_HOLDS_BUFFER (val)) {
      GstBuffer *buf = gst_value_get_buffer (val);
      guint buffer_size = gst_buffer_get_size (buf);

      g_print ("\t%20s : buffer of size %u\n", tag, buffer_size);
    } else if (GST_VALUE_HOLDS_DATE_TIME (val)) {
      GstDateTime *dt = g_value_get_boxed (val);
      gchar *dt_str = gst_date_time_to_iso8601_string (dt);

      g_print ("\t%20s : %s\n", tag, dt_str);
      g_free (dt_str);
    } else {
      g_print ("\t%20s : tag of type '%s'\n", tag, G_VALUE_TYPE_NAME (val));
    }
  }
}

static void
on_new_pad (GstElement * dec, GstPad * pad, GstElement * fakesink)
{
  GstPad *sinkpad;

  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  if (!gst_pad_is_linked (sinkpad)) {
    if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)
      g_error ("Failed to link pads!");
  }
  gst_object_unref (sinkpad);
}

int
main (int argc, char ** argv)
{
  GstElement *pipe, *dec, *sink;
  GstMessage *msg;
  gchar *uri;

  gst_init (&argc, &argv);

  if (argc < 2)
    g_error ("Usage: %s FILE or URI", argv[0]);

  if (gst_uri_is_valid (argv[1])) {
    uri = g_strdup (argv[1]);
  } else {
    uri = gst_filename_to_uri (argv[1], NULL);
  }

  pipe = gst_pipeline_new ("pipeline");

  dec = gst_element_factory_make ("uridecodebin", NULL);
  g_object_set (dec, "uri", uri, NULL);
  gst_bin_add (GST_BIN (pipe), dec);

  sink = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add (GST_BIN (pipe), sink);

  g_signal_connect (dec, "pad-added", G_CALLBACK (on_new_pad), sink);

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  while (TRUE) {
    GstTagList *tags = NULL;

    msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe),
        GST_CLOCK_TIME_NONE,
        GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_TAG | GST_MESSAGE_ERROR);

    if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_TAG) /* error or async_done */
      break;

    gst_message_parse_tag (msg, &tags);

    g_print ("Got tags from element %s:\n", GST_OBJECT_NAME (msg->src));
    gst_tag_list_foreach (tags, print_one_tag, NULL);
    g_print ("\n");
    gst_tag_list_unref (tags);

    gst_message_unref (msg);
  }

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;

    gst_message_parse_error (msg, &err, NULL);
    g_printerr ("Got error: %s\n", err->message);
    g_error_free (err);
  }

  gst_message_unref (msg);
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
  g_free (uri);
  return 0;
}

```

## Tag writing

Tag writing is done using the
[`GstTagSetter`](http://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gstreamer/html/GstTagSetter.html)
interface. All that's required is a tag-set-supporting element in your
pipeline.

In order to see if any of the elements in your pipeline supports tag writing,
you can use the function `gst_bin_iterate_all_by_interface (pipeline, GST_TYPE_TAG_SETTER)`.
On the resulting element, usually an encoder or muxer, you can use
`gst_tag_setter_merge_tags ()` with a taglist or `gst_tag_setter_add_tags ()`
with individual tags, to set tags on it.

A nice extra feature in GStreamer's tag support is that tags are preserved
in pipelines. This means that if you transcode one file containing tags
into another tag-supporting media type, then the tags will be handled as part of
the data stream and will be so merged into the newly written media file.
