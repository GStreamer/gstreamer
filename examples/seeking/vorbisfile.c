#include <stdlib.h>
#include <gst/gst.h>
#include <string.h>

static gboolean ready = FALSE;

struct probe_context {
  GstElement *pipeline;
  GstElement *element;
  GstPad     *pad;
  GstFormat   ls_format;

  gint        total_ls;

  GstCaps    *metadata;
  GstCaps    *streaminfo;
  GstCaps    *caps;
};

static void
print_caps (GstCaps *caps)
{
  if (caps == NULL) return;
  if (!strcmp (gst_caps_get_mime (caps), "application/x-gst-metadata") ||
      !strcmp (gst_caps_get_mime (caps), "application/x-gst-streaminfo"))
  {
    GstProps *props = caps->properties;
    GList *walk;
    /* ugly hack, but ok for now.  If needed, fix by individual strcmp */
    g_print ("  %s:\n", gst_caps_get_mime (caps) + 18);
    if (props == NULL) {
      g_print ("    none\n");
      return;
    }
    walk = props->properties;

    while (walk) {
      GstPropsEntry *entry = (GstPropsEntry *) walk->data;
      const gchar *name;
      const gchar *str_val;
      gint int_val;
      GstPropsType type;

      name = gst_props_entry_get_name (entry);
      type = gst_props_entry_get_type (entry);
      switch (type) {
	case GST_PROPS_STRING_TYPE:
          gst_props_entry_get_string (entry, &str_val);
          g_print ("    %s='%s'\n", name, str_val);
          break;
	case GST_PROPS_INT_TYPE:
          gst_props_entry_get_int (entry, &int_val);
          g_print ("    %s=%d\n", name, int_val);
          break;
	default:
          break;
      }
      walk = g_list_next (walk);
    }
  }
  else {
    g_print (" unkown caps type\n");
  }
}

static void
print_format (GstCaps *caps)
{
  g_print ("  format:\n");
  if (!caps || caps->properties == NULL) {
    g_print ("    unkown\n");
    return;
  }
  if (!strcmp (gst_caps_get_mime (caps), "audio/raw")) {
    gint channels;
    gint rate;

    gst_caps_get_int (caps, "channels", &channels);
    gst_caps_get_int (caps, "rate", &rate);

    g_print ("    channels: %d\n", channels);
    g_print ("    rate: %d\n", rate);
  }
  else {
    g_print (" unkown format\n");
  }
}

static void
print_lbs_info (struct probe_context *context, gint stream)
{
  const GstFormat *formats;

  /* FIXME: need a better name here */
  g_print ("  stream info:\n");

  /* report info in all supported formats */
  formats = gst_pad_get_formats (context->pad);
  while (*formats) {
    const GstFormatDefinition *definition;
    gint64 value_start, value_end;
    gboolean res;
    GstFormat format;

    format = *formats;
    formats++;

    if (format == context->ls_format) {
      continue;
    }

    definition = gst_format_get_details (format);

    /* get start and end position of this stream */
    res = gst_pad_convert  (context->pad,
		            context->ls_format, stream,
		            &format, &value_start);
    res &= gst_pad_convert (context->pad,
		            context->ls_format, stream + 1,
		            &format, &value_end);

    if (res) {
      /* substract to get the length */
      value_end -= value_start;

      if (format == GST_FORMAT_TIME) {
	value_end /= (GST_SECOND/100);
        g_print ("    %s: %lld:%02lld.%02lld\n", definition->nick,
			value_end/6000, (value_end/100)%60, (value_end%100));
      }
      else {
        g_print ("    %s: %lld\n", definition->nick, value_end);
      }
    }
    else
      g_print ("    could not get logical stream %s\n", definition->nick);

  }
}

static void
deep_notify (GObject *object, GstObject *origin,
	     GParamSpec *pspec, gpointer data)
{
  struct probe_context *context = (struct probe_context *) data;
  GValue value = { 0, };

  if (!strcmp (pspec->name, "metadata")) {

    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (origin), pspec->name, &value);
    context->metadata = g_value_peek_pointer (&value);
  }
  else if (!strcmp (pspec->name, "streaminfo")) {

    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (origin), pspec->name, &value);
    context->streaminfo = g_value_peek_pointer (&value);
  } else if (!strcmp (pspec->name, "caps")) {
    if (GST_IS_PAD (origin) && GST_PAD (origin) == context->pad) {
      g_value_init (&value, pspec->value_type);
      g_object_get_property (G_OBJECT (origin), pspec->name, &value);
      context->caps = g_value_peek_pointer (&value);

      ready = TRUE;
    }
  }
}

static gboolean
collect_logical_stream_properties (struct probe_context *context, gint stream)
{
  GstEvent *event;
  gboolean res;
  gint count;

  g_print ("info for logical stream %d:\n", stream);

  /* seek to stream */
  event = gst_event_new_seek (context->ls_format |
		              GST_SEEK_METHOD_SET |
		              GST_SEEK_FLAG_FLUSH,
			      stream);
  res = gst_pad_send_event (context->pad, event);
  if (!res) {
    g_warning ("seek to logical track failed");
    return FALSE;
  }

  /* run the pipeline to get the info */
  count = 0;
  ready = FALSE;
  while (gst_bin_iterate (GST_BIN (context->pipeline)) && !ready) {
    count++;
    if (count > 10) break;
  }

  print_caps (context->metadata);
  print_caps (context->streaminfo);
  print_format (context->caps);
  print_lbs_info (context, stream);

  g_print ("\n");

  return TRUE;
}

static void
collect_stream_properties (struct probe_context *context)
{
  const GstFormat *formats;

  ready = FALSE;
  while (gst_bin_iterate (GST_BIN (context->pipeline)) && !ready);

  g_print ("stream info:\n");

  context->total_ls = -1;

  /* report info in all supported formats */
  formats = gst_pad_get_formats (context->pad);
  while (*formats) {
    const GstFormatDefinition *definition;
    gint64 value;
    gboolean res;
    GstFormat format;

    format = *formats;
    formats++;

    res = gst_pad_query (context->pad, GST_QUERY_TOTAL,
		         &format, &value);

    definition = gst_format_get_details (format);

    if (res) {
      if (format == GST_FORMAT_TIME) {
	value /= (GST_SECOND/100);
        g_print ("  total %s: %lld:%02lld.%02lld\n", definition->nick,
			value/6000, (value/100)%60, (value%100));
      }
      else {
        if (format == context->ls_format)
          context->total_ls = value;
        g_print ("  total %s: %lld\n", definition->nick, value);
      }
    }
  }

  if (context->total_ls == -1) {
    g_warning ("  could not get number of logical streams");
  }
  g_print ("\n");
}

int
main (int argc, char **argv)
{
  GstElement *pipeline;
  GstElement *filesrc;
  GstElement *vorbisfile;
  GstPad *pad;
  GstFormat logical_stream_format;
  struct probe_context *context;
  gint stream;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <oggfile>\n", argv[0]);
    return (-1);
  }

  pipeline = gst_pipeline_new ("pipeline");

  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  g_assert (filesrc);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  vorbisfile = gst_element_factory_make ("vorbisfile", "vorbisfile");
  //vorbisfile = gst_element_factory_make ("mad", "vorbisfile");
  g_assert (vorbisfile);

  gst_bin_add (GST_BIN (pipeline), filesrc);
  gst_bin_add (GST_BIN (pipeline), vorbisfile);

  gst_element_link_pads (filesrc, "src", vorbisfile, "sink");

  pad = gst_element_get_pad (vorbisfile, "src");
  g_assert (pad);

  logical_stream_format = gst_format_get_by_nick ("logical_stream");
  g_assert (logical_stream_format != 0);

  context = g_new0 (struct probe_context, 1);
  context->pipeline = pipeline;
  context->element = vorbisfile;
  context->pad = pad;
  context->ls_format = logical_stream_format;

  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
		  G_CALLBACK (deep_notify), context);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* at this point we can inspect the stream */
  collect_stream_properties (context);

  /* loop over all logical streams to get info */
  stream = 0;
  while (stream < context->total_ls) {
    collect_logical_stream_properties (context, stream);
    stream++;
  }

  /* stop probe */
  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
