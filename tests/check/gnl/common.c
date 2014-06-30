#include "common.h"

void
poll_the_bus (GstBus * bus)
{
  GstMessage *message;
  gboolean carry_on = TRUE;

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
          /* we should check if we really finished here */
          GST_DEBUG ("Got an EOS");
          carry_on = FALSE;
          break;
        case GST_MESSAGE_SEGMENT_START:
        case GST_MESSAGE_SEGMENT_DONE:
          /* We shouldn't see any segement messages, since we didn't do a segment seek */
          GST_WARNING ("Saw a Segment start/stop");
          fail_if (TRUE);
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }
}

GstElement *
gst_element_factory_make_or_warn (const gchar * factoryname, const gchar * name)
{
  GstElement *element;

  element = gst_element_factory_make (factoryname, name);
  fail_unless (element != NULL, "Failed to make element %s", factoryname);
  return element;
}

void
composition_pad_added_cb (GstElement * composition, GstPad * pad,
    CollectStructure * collect)
{
  fail_if (!(gst_element_link_pads_full (composition, GST_OBJECT_NAME (pad),
              collect->sink, "sink", GST_PAD_LINK_CHECK_NOTHING)));
}

/* return TRUE to discard the Segment */
static gboolean
compare_segments (CollectStructure * collect, Segment * segment,
    GstEvent * event)
{
  const GstSegment *orig;
  guint64 running_stop, running_start, running_duration;

  gst_event_parse_segment (event, &orig);

  GST_DEBUG ("Got Segment rate:%f, format:%s, start:%" GST_TIME_FORMAT
      ", stop:%" GST_TIME_FORMAT ", time:%" GST_TIME_FORMAT
      ", base:%" GST_TIME_FORMAT ", offset:%" GST_TIME_FORMAT,
      orig->rate, gst_format_get_name (orig->format),
      GST_TIME_ARGS (orig->start), GST_TIME_ARGS (orig->stop),
      GST_TIME_ARGS (orig->time), GST_TIME_ARGS (orig->base),
      GST_TIME_ARGS (orig->offset));
  GST_DEBUG ("[RUNNING] start:%" GST_TIME_FORMAT " [STREAM] start:%"
      GST_TIME_FORMAT, GST_TIME_ARGS (gst_segment_to_running_time (orig,
              GST_FORMAT_TIME, orig->start)),
      GST_TIME_ARGS (gst_segment_to_stream_time (orig, GST_FORMAT_TIME,
              orig->start)));

  GST_DEBUG ("Expecting rate:%f, format:%s, start:%" GST_TIME_FORMAT
      ", stop:%" GST_TIME_FORMAT ", position:%" GST_TIME_FORMAT ", base:%"
      GST_TIME_FORMAT, segment->rate, gst_format_get_name (segment->format),
      GST_TIME_ARGS (segment->start), GST_TIME_ARGS (segment->stop),
      GST_TIME_ARGS (segment->position),
      GST_TIME_ARGS (collect->expected_base));

  running_start =
      gst_segment_to_running_time (orig, GST_FORMAT_TIME, orig->start);
  running_stop =
      gst_segment_to_running_time (orig, GST_FORMAT_TIME, orig->stop);
  running_duration = running_stop - running_start;
  fail_if (orig->rate != segment->rate);
  fail_if (orig->format != segment->format);
  fail_unless_equals_int64 (orig->time, segment->position);
  fail_unless_equals_int64 (orig->base, collect->expected_base);
  fail_unless_equals_uint64 (orig->stop - orig->start,
      segment->stop - segment->start);

  collect->expected_base += running_duration;

  GST_DEBUG ("Segment was valid, discarding expected Segment");

  return TRUE;
}

static GstPadProbeReturn
sinkpad_event_probe (GstPad * sinkpad, GstEvent * event,
    CollectStructure * collect)
{
  Segment *segment;

  GST_DEBUG_OBJECT (sinkpad, "event:%p (%s seqnum:%d) , collect:%p", event,
      GST_EVENT_TYPE_NAME (event), GST_EVENT_SEQNUM (event), collect);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    fail_if (collect->expected_segments == NULL,
        "Received unexpected segment on pad: %s:%s",
        GST_DEBUG_PAD_NAME (sinkpad));

    if (!collect->gotsegment)
      collect->seen_segments =
          g_list_append (NULL, GINT_TO_POINTER (GST_EVENT_SEQNUM (event)));
    else {
      fail_if (g_list_find (collect->seen_segments,
              GINT_TO_POINTER (GST_EVENT_SEQNUM (event))),
          "Got a segment event we already saw before !");
      collect->seen_segments =
          g_list_append (collect->seen_segments,
          GINT_TO_POINTER (GST_EVENT_SEQNUM (event)));
    }

    segment = (Segment *) collect->expected_segments->data;

    if (compare_segments (collect, segment, event) &&
        collect->keep_expected_segments == FALSE) {
      collect->expected_segments =
          g_list_remove (collect->expected_segments, segment);
      g_free (segment);
    }

    collect->gotsegment = TRUE;
  }

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
sinkpad_buffer_probe (GstPad * sinkpad, GstBuffer * buffer,
    CollectStructure * collect)
{
  GST_DEBUG_OBJECT (sinkpad, "buffer:%p (%" GST_TIME_FORMAT ") , collect:%p",
      buffer, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), collect);
  fail_if (!collect->gotsegment,
      "Received a buffer without a preceding segment");
  return GST_PAD_PROBE_OK;
}

GstPadProbeReturn
sinkpad_probe (GstPad * sinkpad, GstPadProbeInfo * info,
    CollectStructure * collect)
{
  if (info->type & GST_PAD_PROBE_TYPE_BUFFER)
    return sinkpad_buffer_probe (sinkpad, (GstBuffer *) info->data, collect);
  if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
    return sinkpad_event_probe (sinkpad, (GstEvent *) info->data, collect);
  return GST_PAD_PROBE_OK;
}

static GstElement *
new_gnl_src (const gchar * name, guint64 start, gint64 duration, gint priority)
{
  GstElement *gnlsource = NULL;

  gnlsource = gst_element_factory_make_or_warn ("gnlsource", name);
  fail_if (gnlsource == NULL);

  g_object_set (G_OBJECT (gnlsource),
      "start", start,
      "duration", duration, "inpoint", start, "priority", priority, NULL);

  return gnlsource;
}

GstElement *
videotest_gnl_src (const gchar * name, guint64 start, gint64 duration,
    gint pattern, guint priority)
{
  GstElement *gnlsource = NULL;
  GstElement *videotestsrc = NULL;
  GstCaps *caps =
      gst_caps_from_string
      ("video/x-raw,format=(string)I420,framerate=(fraction)3/2");

  fail_if (caps == NULL);

  videotestsrc = gst_element_factory_make_or_warn ("videotestsrc", NULL);
  g_object_set (G_OBJECT (videotestsrc), "pattern", pattern, NULL);

  gnlsource = new_gnl_src (name, start, duration, priority);
  g_object_set (G_OBJECT (gnlsource), "caps", caps, NULL);
  gst_caps_unref (caps);

  gst_bin_add (GST_BIN (gnlsource), videotestsrc);

  return gnlsource;
}

GstElement *
videotest_gnl_src_full (const gchar * name, guint64 start, gint64 duration,
    guint64 inpoint, gint pattern, guint priority)
{
  GstElement *gnls;

  gnls = videotest_gnl_src (name, start, duration, pattern, priority);
  if (gnls) {
    g_object_set (G_OBJECT (gnls), "inpoint", inpoint, NULL);
  }


  return gnls;
}

GstElement *
videotest_in_bin_gnl_src (const gchar * name, guint64 start, gint64 duration,
    gint pattern, guint priority)
{
  GstElement *gnlsource = NULL;
  GstElement *videotestsrc = NULL;
  GstElement *bin = NULL;
  GstElement *alpha = NULL;
  GstPad *srcpad = NULL;

  alpha = gst_element_factory_make ("alpha", NULL);
  if (alpha == NULL)
    return NULL;

  videotestsrc = gst_element_factory_make_or_warn ("videotestsrc", NULL);
  g_object_set (G_OBJECT (videotestsrc), "pattern", pattern, NULL);
  bin = gst_bin_new (NULL);

  gnlsource = new_gnl_src (name, start, duration, priority);

  gst_bin_add (GST_BIN (bin), videotestsrc);
  gst_bin_add (GST_BIN (bin), alpha);

  gst_element_link_pads_full (videotestsrc, "src", alpha, "sink",
      GST_PAD_LINK_CHECK_NOTHING);

  gst_bin_add (GST_BIN (gnlsource), bin);

  srcpad = gst_element_get_static_pad (alpha, "src");

  gst_element_add_pad (bin, gst_ghost_pad_new ("src", srcpad));

  gst_object_unref (srcpad);

  return gnlsource;
}

GstElement *
audiotest_bin_src (const gchar * name, guint64 start,
    gint64 duration, guint priority, gboolean intaudio)
{
  GstElement *source = NULL;
  GstElement *identity = NULL;
  GstElement *audiotestsrc = NULL;
  GstElement *audioconvert = NULL;
  GstElement *bin = NULL;
  GstCaps *caps;
  GstPad *srcpad = NULL;

  audiotestsrc = gst_element_factory_make_or_warn ("audiotestsrc", NULL);
  identity = gst_element_factory_make_or_warn ("identity", NULL);
  bin = gst_bin_new (NULL);
  source = new_gnl_src (name, start, duration, priority);
  audioconvert = gst_element_factory_make_or_warn ("audioconvert", NULL);

  if (intaudio)
    caps = gst_caps_from_string ("audio/x-raw,format=(string)S16LE");
  else
    caps = gst_caps_from_string ("audio/x-raw,format=(string)F32LE");

  gst_bin_add_many (GST_BIN (bin), audiotestsrc, audioconvert, identity, NULL);
  gst_element_link_pads_full (audiotestsrc, "src", audioconvert, "sink",
      GST_PAD_LINK_CHECK_NOTHING);
  fail_if ((gst_element_link_filtered (audioconvert, identity, caps)) != TRUE);

  gst_caps_unref (caps);

  gst_bin_add (GST_BIN (source), bin);

  srcpad = gst_element_get_static_pad (identity, "src");

  gst_element_add_pad (bin, gst_ghost_pad_new ("src", srcpad));

  gst_object_unref (srcpad);

  return source;
}

GstElement *
new_operation (const gchar * name, const gchar * factory, guint64 start,
    gint64 duration, guint priority)
{
  GstElement *gnloperation = NULL;
  GstElement *operation = NULL;

  operation = gst_element_factory_make_or_warn (factory, NULL);
  gnloperation = gst_element_factory_make_or_warn ("gnloperation", name);

  g_object_set (G_OBJECT (gnloperation),
      "start", start, "duration", duration, "priority", priority, NULL);

  gst_bin_add (GST_BIN (gnloperation), operation);

  return gnloperation;
}


Segment *
segment_new (gdouble rate, GstFormat format, gint64 start, gint64 stop,
    gint64 position)
{
  Segment *segment;

  segment = g_new0 (Segment, 1);

  segment->rate = rate;
  segment->format = format;
  segment->start = start;
  segment->stop = stop;
  segment->position = position;

  return segment;
}

GList *
copy_segment_list (GList * list)
{
  GList *res = NULL;

  while (list) {
    Segment *pdata = (Segment *) list->data;

    res =
        g_list_append (res, segment_new (pdata->rate, pdata->format,
            pdata->start, pdata->stop, pdata->position));

    list = list->next;
  }

  return res;
}

static GMutex lock;
static GCond cond;
static void
commited_cb (GstElement * comp, gboolean changed)
{
  GST_ERROR ("commited !!");
  g_mutex_lock (&lock);
  g_cond_signal (&cond);
  g_mutex_unlock (&lock);
}

void
commit_and_wait (GstElement * comp, gboolean * ret)
{
  gulong handler_id =
      g_signal_connect (comp, "commited", (GCallback) commited_cb, NULL);
  g_mutex_lock (&lock);
  g_signal_emit_by_name (comp, "commit", TRUE, ret);
  g_cond_wait (&cond, &lock);
  g_mutex_unlock (&lock);
  g_signal_handler_disconnect (comp, handler_id);
}

gboolean
gnl_composition_remove (GstBin * comp, GstElement * object)
{
  gboolean ret;

  g_signal_emit_by_name (GST_BIN (comp), "remove-object", object, &ret);
  if (!ret)
    return ret;

  commit_and_wait ((GstElement *) comp, &ret);

  return ret;
}

gboolean
gnl_composition_add (GstBin * comp, GstElement * object)
{
  gboolean ret;

  g_signal_emit_by_name (comp, "add-object", object, &ret);

  return ret;
}
