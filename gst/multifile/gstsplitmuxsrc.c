/* GStreamer Split Demuxer bin that recombines files created by
 * the splitmuxsink element.
 *
 * Copyright (C) <2014> Jan Schmidt <jan@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-splitmuxsrc
 * @short_description: Split Demuxer bin that recombines files created by
 * the splitmuxsink element.
 *
 * This element reads a set of input files created by the splitmuxsink element
 * containing contiguous elementary streams split across multiple files.
 *
 * This element is similar to splitfilesrc, except that it recombines the
 * streams in each file part at the demuxed elementary level, rather than
 * as a single larger bytestream.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 splitmuxsrc location=video*.mov ! decodebin ! xvimagesink
 * ]| Demux each file part and output the video stream as one continuous stream
 * |[
 * gst-launch-1.0 playbin uri="splitmux://path/to/foo.mp4.*"
 * ]| Play back a set of files created by splitmuxsink
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstsplitmuxsrc.h"
#include "gstsplitutils.h"

#include "../../gst-libs/gst/gst-i18n-plugin.h"

GST_DEBUG_CATEGORY (splitmux_debug);
#define GST_CAT_DEFAULT splitmux_debug

enum
{
  PROP_0,
  PROP_LOCATION
};

enum
{
  SIGNAL_FORMAT_LOCATION,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate subtitle_src_template =
GST_STATIC_PAD_TEMPLATE ("subtitle_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStateChangeReturn gst_splitmux_src_change_state (GstElement *
    element, GstStateChange transition);
static void gst_splitmux_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_splitmux_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_splitmux_src_dispose (GObject * object);
static void gst_splitmux_src_finalize (GObject * object);
static gboolean gst_splitmux_src_start (GstSplitMuxSrc * splitmux);
static gboolean gst_splitmux_src_stop (GstSplitMuxSrc * splitmux);
static void splitmux_src_pad_constructed (GObject * pad);
static gboolean splitmux_src_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean splitmux_src_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static void splitmux_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);


static GstPad *gst_splitmux_find_output_pad (GstSplitMuxPartReader * part,
    GstPad * pad, GstSplitMuxSrc * splitmux);
static void gst_splitmux_part_prepared (GstSplitMuxPartReader * reader,
    GstSplitMuxSrc * splitmux);
static gboolean gst_splitmux_end_of_part (GstSplitMuxSrc * splitmux,
    SplitMuxSrcPad * pad);
static gboolean gst_splitmux_check_new_caps (SplitMuxSrcPad * splitpad,
    GstEvent * event);

#define _do_init \
    G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, splitmux_src_uri_handler_init);
#define gst_splitmux_src_parent_class parent_class

G_DEFINE_TYPE_EXTENDED (GstSplitMuxSrc, gst_splitmux_src, GST_TYPE_BIN, 0,
    _do_init);

static GstURIType
splitmux_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
splitmux_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "splitmux", NULL };

  return protocols;
}

static gchar *
splitmux_src_uri_get_uri (GstURIHandler * handler)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (handler);
  gchar *ret = NULL;

  GST_OBJECT_LOCK (splitmux);
  if (splitmux->location)
    ret = g_strdup_printf ("splitmux://%s", splitmux->location);
  GST_OBJECT_UNLOCK (splitmux);
  return ret;
}

static gboolean
splitmux_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** err)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (handler);
  gchar *protocol, *location;

  protocol = gst_uri_get_protocol (uri);
  if (protocol == NULL || !g_str_equal (protocol, "splitmux"))
    goto wrong_uri;
  g_free (protocol);

  location = gst_uri_get_location (uri);
  GST_OBJECT_LOCK (splitmux);
  g_free (splitmux->location);
  splitmux->location = location;
  GST_OBJECT_UNLOCK (splitmux);

  return TRUE;

wrong_uri:
  g_free (protocol);
  GST_ELEMENT_ERROR (splitmux, RESOURCE, READ, (NULL),
      ("Error parsing uri %s", uri));
  g_set_error_literal (err, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
      "Could not parse splitmux URI");
  return FALSE;
}

static void
splitmux_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) (g_iface);

  iface->get_type = splitmux_src_uri_get_type;
  iface->get_protocols = splitmux_src_uri_get_protocols;
  iface->set_uri = splitmux_src_uri_set_uri;
  iface->get_uri = splitmux_src_uri_get_uri;
}


static void
gst_splitmux_src_class_init (GstSplitMuxSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_splitmux_src_set_property;
  gobject_class->get_property = gst_splitmux_src_get_property;
  gobject_class->dispose = gst_splitmux_src_dispose;
  gobject_class->finalize = gst_splitmux_src_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "Split File Demuxing Bin", "Generic/Bin/Demuxer",
      "Source that reads a set of files created by splitmuxsink",
      "Jan Schmidt <jan@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &video_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &audio_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &subtitle_src_template);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_splitmux_src_change_state);

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Input Pattern",
          "Glob pattern for the location of the files to read", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSplitMuxSrc::format-location:
   * @splitmux: the #GstSplitMuxSrc
   *
   * Returns: A NULL-terminated sorted array of strings containing the
   *   filenames of the input files. The array will be freed internally
   *   using g_strfreev()
   *
   * Since: 1.8
   */
  signals[SIGNAL_FORMAT_LOCATION] =
      g_signal_new ("format-location", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_STRV, 0);
}

static void
gst_splitmux_src_init (GstSplitMuxSrc * splitmux)
{
  g_mutex_init (&splitmux->lock);
  g_mutex_init (&splitmux->pads_lock);
  splitmux->total_duration = GST_CLOCK_TIME_NONE;
  gst_segment_init (&splitmux->play_segment, GST_FORMAT_TIME);
}

static void
gst_splitmux_src_dispose (GObject * object)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (object);
  GList *cur;

  SPLITMUX_SRC_PADS_LOCK (splitmux);

  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    GstPad *pad = GST_PAD (cur->data);
    gst_element_remove_pad (GST_ELEMENT (splitmux), pad);
  }
  g_list_free (splitmux->pads);
  splitmux->pads = NULL;
  SPLITMUX_SRC_PADS_UNLOCK (splitmux);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_splitmux_src_finalize (GObject * object)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (object);
  g_mutex_clear (&splitmux->lock);
  g_mutex_clear (&splitmux->pads_lock);
  g_free (splitmux->location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_splitmux_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:{
      GST_OBJECT_LOCK (splitmux);
      g_free (splitmux->location);
      splitmux->location = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_splitmux_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      GST_OBJECT_LOCK (splitmux);
      g_value_set_string (value, splitmux->location);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_splitmux_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSplitMuxSrc *splitmux = (GstSplitMuxSrc *) element;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      if (!gst_splitmux_src_start (splitmux))
        return GST_STATE_CHANGE_FAILURE;
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* Make sure the element will shut down */
      if (!gst_splitmux_src_stop (splitmux))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static GstSplitMuxPartReader *
gst_splitmux_part_create (GstSplitMuxSrc * splitmux, char *filename)
{
  GstSplitMuxPartReader *r;

  r = g_object_new (GST_TYPE_SPLITMUX_PART_READER, NULL);

  g_signal_connect (r, "prepared", (GCallback) gst_splitmux_part_prepared,
      splitmux);

  gst_splitmux_part_reader_set_callbacks (r, splitmux,
      (GstSplitMuxPartReaderPadCb) gst_splitmux_find_output_pad);
  gst_splitmux_part_reader_set_location (r, filename);

  return r;
}

static gboolean
gst_splitmux_check_new_caps (SplitMuxSrcPad * splitpad, GstEvent * event)
{
  GstCaps *curcaps = gst_pad_get_current_caps ((GstPad *) (splitpad));
  GstCaps *newcaps;
  GstCaps *tmpcaps;
  GstCaps *tmpcurcaps;

  GstStructure *s;
  gboolean res = TRUE;

  gst_event_parse_caps (event, &newcaps);

  GST_LOG_OBJECT (splitpad, "Comparing caps %" GST_PTR_FORMAT
      " and %" GST_PTR_FORMAT, curcaps, newcaps);

  if (curcaps == NULL)
    return TRUE;

  /* If caps are exactly equal exit early */
  if (gst_caps_is_equal (curcaps, newcaps)) {
    gst_caps_unref (curcaps);
    return FALSE;
  }

  /* More extensive check, ignore changes in framerate, because
   * demuxers get that wrong */
  tmpcaps = gst_caps_copy (newcaps);
  s = gst_caps_get_structure (tmpcaps, 0);
  gst_structure_remove_field (s, "framerate");

  tmpcurcaps = gst_caps_copy (curcaps);
  gst_caps_unref (curcaps);
  s = gst_caps_get_structure (tmpcurcaps, 0);
  gst_structure_remove_field (s, "framerate");

  /* Now check if these filtered caps are equal */
  if (gst_caps_is_equal (tmpcurcaps, tmpcaps)) {
    GST_INFO_OBJECT (splitpad, "Ignoring framerate-only caps change");
    res = FALSE;
  }

  gst_caps_unref (tmpcaps);
  gst_caps_unref (tmpcurcaps);
  return res;
}

static void
gst_splitmux_handle_event (GstSplitMuxSrc * splitmux,
    SplitMuxSrcPad * splitpad, GstPad * part_pad, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:{
      if (splitpad->sent_stream_start)
        goto drop_event;
      splitpad->sent_stream_start = TRUE;
      break;
    }
    case GST_EVENT_EOS:{
      if (gst_splitmux_end_of_part (splitmux, splitpad))
        // Continuing to next part, drop the EOS
        goto drop_event;
      if (splitmux->segment_seqnum)
        gst_event_set_seqnum (event, splitmux->segment_seqnum);
      break;
    }
    case GST_EVENT_SEGMENT:{
      GstSegment seg;

      gst_event_copy_segment (event, &seg);

      splitpad->segment.position = seg.position;

      if (splitpad->sent_segment)
        goto drop_event;        /* We already forwarded a segment event */

      /* Calculate output segment */
      GST_LOG_OBJECT (splitpad, "Pad seg %" GST_SEGMENT_FORMAT
          " got seg %" GST_SEGMENT_FORMAT
          " play seg %" GST_SEGMENT_FORMAT,
          &splitpad->segment, &seg, &splitmux->play_segment);

      /* If playing forward, take the stop time from the overall
       * seg or play_segment */
      if (splitmux->play_segment.rate > 0.0) {
        if (splitmux->play_segment.stop != -1)
          seg.stop = splitmux->play_segment.stop;
        else
          seg.stop = splitpad->segment.stop;
      } else {
        /* Reverse playback from stop time to start time */
        /* See if an end point was requested in the seek */
        if (splitmux->play_segment.start != -1) {
          seg.start = splitmux->play_segment.start;
          seg.time = splitmux->play_segment.time;
        } else {
          seg.start = splitpad->segment.start;
          seg.time = splitpad->segment.time;
        }
      }

      GST_INFO_OBJECT (splitpad,
          "Forwarding segment %" GST_SEGMENT_FORMAT, &seg);

      gst_event_unref (event);
      event = gst_event_new_segment (&seg);
      if (splitmux->segment_seqnum)
        gst_event_set_seqnum (event, splitmux->segment_seqnum);
      splitpad->sent_segment = TRUE;
      break;
    }
    case GST_EVENT_CAPS:{
      if (!gst_splitmux_check_new_caps (splitpad, event))
        goto drop_event;
      splitpad->sent_caps = TRUE;
      break;
    }
    default:
      break;
  }

  gst_pad_push_event ((GstPad *) (splitpad), event);
  return;
drop_event:
  gst_event_unref (event);
  return;
}

static GstFlowReturn
gst_splitmux_handle_buffer (GstSplitMuxSrc * splitmux,
    SplitMuxSrcPad * splitpad, GstBuffer * buf)
{
  GstFlowReturn ret;

  if (splitpad->clear_next_discont) {
    GST_LOG_OBJECT (splitpad, "Clearing discont flag on buffer");
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
    splitpad->clear_next_discont = FALSE;
  }
  if (splitpad->set_next_discont) {
    GST_LOG_OBJECT (splitpad, "Setting discont flag on buffer");
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    splitpad->set_next_discont = FALSE;
  }

  ret = gst_pad_push (GST_PAD_CAST (splitpad), buf);

  GST_LOG_OBJECT (splitpad, "Pad push returned %d", ret);
  return ret;
}

static void
gst_splitmux_pad_loop (GstPad * pad)
{
  /* Get one event/buffer from the associated part and push */
  SplitMuxSrcPad *splitpad = (SplitMuxSrcPad *) (pad);
  GstSplitMuxSrc *splitmux = (GstSplitMuxSrc *) gst_pad_get_parent (pad);
  GstDataQueueItem *item = NULL;
  GstSplitMuxPartReader *reader = splitpad->reader;
  GstPad *part_pad;
  GstFlowReturn ret;

  GST_OBJECT_LOCK (splitpad);
  if (splitpad->part_pad == NULL) {
    GST_OBJECT_UNLOCK (splitpad);
    return;
  }
  part_pad = gst_object_ref (splitpad->part_pad);
  GST_OBJECT_UNLOCK (splitpad);

  GST_LOG_OBJECT (splitpad, "Popping data queue item from %" GST_PTR_FORMAT
      " pad %" GST_PTR_FORMAT, reader, part_pad);
  ret = gst_splitmux_part_reader_pop (reader, part_pad, &item);
  if (ret == GST_FLOW_ERROR)
    goto error;
  if (ret == GST_FLOW_FLUSHING || item == NULL)
    goto flushing;

  GST_DEBUG_OBJECT (splitpad, "Got data queue item %" GST_PTR_FORMAT,
      item->object);

  if (GST_IS_EVENT (item->object)) {
    GstEvent *event = (GstEvent *) (item->object);
    gst_splitmux_handle_event (splitmux, splitpad, part_pad, event);
  } else {
    GstBuffer *buf = (GstBuffer *) (item->object);
    GstFlowReturn ret = gst_splitmux_handle_buffer (splitmux, splitpad, buf);
    if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_EOS)) {
      /* Stop immediately on error or flushing */
      GST_INFO_OBJECT (splitpad, "Stopping due to pad_push() result %d", ret);
      gst_pad_pause_task (pad);
      if (ret < GST_FLOW_EOS || ret == GST_FLOW_NOT_LINKED) {
        GST_ELEMENT_FLOW_ERROR (splitmux, ret);
      }
    }
  }
  g_slice_free (GstDataQueueItem, item);

  gst_object_unref (part_pad);
  gst_object_unref (splitmux);
  return;

error:
  /* Fall through */
  GST_ELEMENT_ERROR (splitmux, RESOURCE, OPEN_READ, (NULL),
      ("Error reading part file %s", GST_STR_NULL (reader->path)));
flushing:
  gst_pad_pause_task (pad);
  gst_object_unref (part_pad);
  gst_object_unref (splitmux);
  return;
}

static gboolean
gst_splitmux_src_activate_part (GstSplitMuxSrc * splitmux, guint part,
    GstSeekFlags extra_flags)
{
  GList *cur;

  GST_DEBUG_OBJECT (splitmux, "Activating part %d", part);

  splitmux->cur_part = part;
  if (!gst_splitmux_part_reader_activate (splitmux->parts[part],
          &splitmux->play_segment, extra_flags))
    return FALSE;

  SPLITMUX_SRC_PADS_LOCK (splitmux);
  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    SplitMuxSrcPad *splitpad = (SplitMuxSrcPad *) (cur->data);
    splitpad->cur_part = part;
    splitpad->reader = splitmux->parts[splitpad->cur_part];
    if (splitpad->part_pad)
      gst_object_unref (splitpad->part_pad);
    splitpad->part_pad =
        gst_splitmux_part_reader_lookup_pad (splitpad->reader,
        (GstPad *) (splitpad));

    /* Make sure we start with a DISCONT */
    splitpad->set_next_discont = TRUE;
    splitpad->clear_next_discont = FALSE;

    gst_pad_start_task (GST_PAD (splitpad),
        (GstTaskFunction) gst_splitmux_pad_loop, splitpad, NULL);
  }
  SPLITMUX_SRC_PADS_UNLOCK (splitmux);

  return TRUE;
}

static gboolean
gst_splitmux_src_start (GstSplitMuxSrc * splitmux)
{
  gboolean ret = FALSE;
  GError *err = NULL;
  gchar *basename = NULL;
  gchar *dirname = NULL;
  gchar **files;
  GstClockTime next_offset = 0;
  guint i;
  GstClockTime total_duration = 0;

  GST_DEBUG_OBJECT (splitmux, "Starting");

  g_signal_emit (splitmux, signals[SIGNAL_FORMAT_LOCATION], 0, &files);

  if (files == NULL || *files == NULL) {
    GST_OBJECT_LOCK (splitmux);
    if (splitmux->location != NULL && splitmux->location[0] != '\0') {
      basename = g_path_get_basename (splitmux->location);
      dirname = g_path_get_dirname (splitmux->location);
    }
    GST_OBJECT_UNLOCK (splitmux);

    g_strfreev (files);
    files = gst_split_util_find_files (dirname, basename, &err);

    if (files == NULL || *files == NULL)
      goto no_files;
  }

  SPLITMUX_SRC_LOCK (splitmux);
  splitmux->pads_complete = FALSE;
  splitmux->running = TRUE;
  SPLITMUX_SRC_UNLOCK (splitmux);

  splitmux->num_parts = g_strv_length (files);

  splitmux->parts = g_new0 (GstSplitMuxPartReader *, splitmux->num_parts);

  for (i = 0; i < splitmux->num_parts; i++) {
    splitmux->parts[i] = gst_splitmux_part_create (splitmux, files[i]);
    if (splitmux->parts[i] == NULL)
      break;

    /* Figure out the next offset - the smallest one */
    gst_splitmux_part_reader_set_start_offset (splitmux->parts[i], next_offset);
    if (!gst_splitmux_part_reader_prepare (splitmux->parts[i])) {
      GST_WARNING_OBJECT (splitmux,
          "Failed to prepare file part %s. Cannot play past there.", files[i]);
      GST_ELEMENT_WARNING (splitmux, RESOURCE, READ, (NULL),
          ("Failed to prepare file part %s. Cannot play past there.",
              files[i]));
      gst_splitmux_part_reader_unprepare (splitmux->parts[i]);
      g_object_unref (splitmux->parts[i]);
      splitmux->parts[i] = NULL;
      break;
    }

    /* Extend our total duration to cover this part */
    total_duration =
        next_offset +
        gst_splitmux_part_reader_get_duration (splitmux->parts[i]);
    splitmux->play_segment.duration = total_duration;

    next_offset = gst_splitmux_part_reader_get_end_offset (splitmux->parts[i]);
  }

  /* Update total_duration state variable */
  GST_OBJECT_LOCK (splitmux);
  splitmux->total_duration = total_duration;
  GST_OBJECT_UNLOCK (splitmux);

  /* Store how many parts we actually created */
  splitmux->num_parts = i;

  if (splitmux->num_parts < 1)
    goto failed_part;

  /* All done preparing, activate the first part */
  GST_INFO_OBJECT (splitmux,
      "All parts prepared. Total duration %" GST_TIME_FORMAT
      " Activating first part", GST_TIME_ARGS (total_duration));
  ret = gst_splitmux_src_activate_part (splitmux, 0, GST_SEEK_FLAG_NONE);
  if (ret == FALSE)
    goto failed_first_part;
done:
  if (err != NULL)
    g_error_free (err);
  g_strfreev (files);
  g_free (basename);
  g_free (dirname);

  return ret;

/* ERRORS */
no_files:
  {
    GST_ELEMENT_ERROR (splitmux, RESOURCE, OPEN_READ, ("%s", err->message),
        ("Failed to find files in '%s' for pattern '%s'",
            GST_STR_NULL (dirname), GST_STR_NULL (basename)));
    goto done;
  }
failed_part:
  {
    GST_ELEMENT_ERROR (splitmux, RESOURCE, OPEN_READ, (NULL),
        ("Failed to open any files for reading"));
    goto done;
  }
failed_first_part:
  {
    GST_ELEMENT_ERROR (splitmux, RESOURCE, OPEN_READ, (NULL),
        ("Failed to activate first part for playback"));
    goto done;
  }
}

static gboolean
gst_splitmux_src_stop (GstSplitMuxSrc * splitmux)
{
  gboolean ret = TRUE;
  guint i;
  GList *cur, *pads_list;

  SPLITMUX_SRC_LOCK (splitmux);
  if (!splitmux->running)
    goto out;

  GST_DEBUG_OBJECT (splitmux, "Stopping");

  /* Stop and destroy all parts  */
  for (i = 0; i < splitmux->num_parts; i++) {
    if (splitmux->parts[i] == NULL)
      continue;
    gst_splitmux_part_reader_unprepare (splitmux->parts[i]);
    g_object_unref (splitmux->parts[i]);
    splitmux->parts[i] = NULL;
  }

  SPLITMUX_SRC_PADS_LOCK (splitmux);
  pads_list = splitmux->pads;
  splitmux->pads = NULL;
  SPLITMUX_SRC_PADS_UNLOCK (splitmux);

  SPLITMUX_SRC_UNLOCK (splitmux);
  for (cur = g_list_first (pads_list); cur != NULL; cur = g_list_next (cur)) {
    SplitMuxSrcPad *tmp = (SplitMuxSrcPad *) (cur->data);
    gst_pad_stop_task (GST_PAD (tmp));
    gst_element_remove_pad (GST_ELEMENT (splitmux), GST_PAD (tmp));
  }
  g_list_free (pads_list);
  SPLITMUX_SRC_LOCK (splitmux);

  g_free (splitmux->parts);
  splitmux->parts = NULL;
  splitmux->num_parts = 0;
  splitmux->running = FALSE;
  splitmux->total_duration = GST_CLOCK_TIME_NONE;
  /* Reset playback segment */
  gst_segment_init (&splitmux->play_segment, GST_FORMAT_TIME);
out:
  SPLITMUX_SRC_UNLOCK (splitmux);
  return ret;
}

typedef struct
{
  GstSplitMuxSrc *splitmux;
  SplitMuxSrcPad *splitpad;
} SplitMuxAndPad;

static gboolean
handle_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  SplitMuxAndPad *splitmux_and_pad;
  GstSplitMuxSrc *splitmux;
  SplitMuxSrcPad *splitpad;

  splitmux_and_pad = user_data;
  splitmux = splitmux_and_pad->splitmux;
  splitpad = splitmux_and_pad->splitpad;

  GST_DEBUG_OBJECT (splitpad, "handle sticky event %" GST_PTR_FORMAT, *event);
  gst_event_ref (*event);
  gst_splitmux_handle_event (splitmux, splitpad, pad, *event);

  return TRUE;
}

static GstPad *
gst_splitmux_find_output_pad (GstSplitMuxPartReader * part, GstPad * pad,
    GstSplitMuxSrc * splitmux)
{
  GList *cur;
  gchar *pad_name = gst_pad_get_name (pad);
  GstPad *target = NULL;
  gboolean is_new_pad = FALSE;

  SPLITMUX_SRC_LOCK (splitmux);
  SPLITMUX_SRC_PADS_LOCK (splitmux);
  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    GstPad *tmp = (GstPad *) (cur->data);
    if (g_str_equal (GST_PAD_NAME (tmp), pad_name)) {
      target = tmp;
      break;
    }
  }

  if (target == NULL && !splitmux->pads_complete) {
    SplitMuxAndPad splitmux_and_pad;

    /* No pad found, create one */
    target = g_object_new (SPLITMUX_TYPE_SRC_PAD,
        "name", pad_name, "direction", GST_PAD_SRC, NULL);
    splitmux->pads = g_list_prepend (splitmux->pads, target);

    gst_pad_set_active (target, TRUE);

    splitmux_and_pad.splitmux = splitmux;
    splitmux_and_pad.splitpad = (SplitMuxSrcPad *) target;
    gst_pad_sticky_events_foreach (pad, handle_sticky_events,
        &splitmux_and_pad);
    is_new_pad = TRUE;
  }
  SPLITMUX_SRC_PADS_UNLOCK (splitmux);
  SPLITMUX_SRC_UNLOCK (splitmux);

  g_free (pad_name);

  if (target == NULL)
    goto pad_not_found;

  if (is_new_pad)
    gst_element_add_pad (GST_ELEMENT_CAST (splitmux), target);

  return target;

pad_not_found:
  GST_ELEMENT_ERROR (splitmux, STREAM, FAILED, (NULL),
      ("Stream part %s contains extra unknown pad %" GST_PTR_FORMAT,
          part->path, pad));
  return NULL;
}

static void
gst_splitmux_part_prepared (GstSplitMuxPartReader * reader,
    GstSplitMuxSrc * splitmux)
{
  gboolean need_no_more_pads;

  GST_LOG_OBJECT (splitmux, "Part %" GST_PTR_FORMAT " prepared", reader);
  SPLITMUX_SRC_LOCK (splitmux);
  need_no_more_pads = !splitmux->pads_complete;
  splitmux->pads_complete = TRUE;
  SPLITMUX_SRC_UNLOCK (splitmux);

  if (need_no_more_pads) {
    GST_DEBUG_OBJECT (splitmux, "Signalling no-more-pads");
    gst_element_no_more_pads (GST_ELEMENT_CAST (splitmux));
  }
}

static void
gst_splitmux_push_event (GstSplitMuxSrc * splitmux, GstEvent * e,
    guint32 seqnum)
{
  GList *cur;

  if (seqnum)
    gst_event_set_seqnum (e, seqnum);

  SPLITMUX_SRC_PADS_LOCK (splitmux);
  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    GstPad *pad = GST_PAD_CAST (cur->data);
    gst_event_ref (e);
    gst_pad_push_event (pad, e);
  }
  SPLITMUX_SRC_PADS_UNLOCK (splitmux);

  gst_event_unref (e);
}

static void
gst_splitmux_push_flush_stop (GstSplitMuxSrc * splitmux, guint32 seqnum)
{
  GstEvent *e = gst_event_new_flush_stop (TRUE);
  GList *cur;

  if (seqnum)
    gst_event_set_seqnum (e, seqnum);

  SPLITMUX_SRC_PADS_LOCK (splitmux);
  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    SplitMuxSrcPad *target = (SplitMuxSrcPad *) (cur->data);

    gst_event_ref (e);
    gst_pad_push_event (GST_PAD_CAST (target), e);
    target->sent_caps = FALSE;
    target->sent_stream_start = FALSE;
    target->sent_segment = FALSE;
  }
  SPLITMUX_SRC_PADS_UNLOCK (splitmux);

  gst_event_unref (e);
}

/* Callback for when a part finishes and we need to move to the next */
static gboolean
gst_splitmux_end_of_part (GstSplitMuxSrc * splitmux, SplitMuxSrcPad * splitpad)
{
  gint next_part = -1;
  gint cur_part = splitpad->cur_part;
  gboolean res = FALSE;

  if (splitmux->play_segment.rate >= 0.0) {
    if (cur_part + 1 < splitmux->num_parts)
      next_part = cur_part + 1;
    /* Make sure the transition is seamless */
    splitpad->set_next_discont = FALSE;
    splitpad->clear_next_discont = TRUE;
  } else {
    /* Reverse play - move to previous segment */
    if (cur_part > 0) {
      next_part = cur_part - 1;
      /* Non-seamless transition in reverse */
      splitpad->set_next_discont = TRUE;
      splitpad->clear_next_discont = FALSE;
    }
  }

  SPLITMUX_SRC_LOCK (splitmux);

  /* If all pads are done with this part, deactivate it */
  if (gst_splitmux_part_is_eos (splitmux->parts[splitpad->cur_part]))
    gst_splitmux_part_reader_deactivate (splitmux->parts[cur_part]);

  if (splitmux->play_segment.rate >= 0.0) {
    if (splitmux->play_segment.stop != -1) {
      GstClockTime part_end =
          gst_splitmux_part_reader_get_end_offset (splitmux->parts[cur_part]);
      if (part_end >= splitmux->play_segment.stop) {
        GST_DEBUG_OBJECT (splitmux,
            "Stop position was within that part. Finishing");
        next_part = -1;
      }
    }
  } else {
    if (splitmux->play_segment.start != -1) {
      GstClockTime part_start =
          gst_splitmux_part_reader_get_start_offset (splitmux->parts[cur_part]);
      if (part_start <= splitmux->play_segment.start) {
        GST_DEBUG_OBJECT (splitmux,
            "Start position %" GST_TIME_FORMAT
            " was within that part. Finishing",
            GST_TIME_ARGS (splitmux->play_segment.start));
        next_part = -1;
      }
    }
  }

  if (next_part != -1) {
    GST_DEBUG_OBJECT (splitmux, "At EOS on pad %" GST_PTR_FORMAT
        " moving to part %d", splitpad, next_part);
    splitpad->cur_part = next_part;
    splitpad->reader = splitmux->parts[splitpad->cur_part];
    if (splitpad->part_pad)
      gst_object_unref (splitpad->part_pad);
    splitpad->part_pad =
        gst_splitmux_part_reader_lookup_pad (splitpad->reader,
        (GstPad *) (splitpad));

    if (splitmux->cur_part != next_part) {
      if (!gst_splitmux_part_reader_is_active (splitpad->reader)) {
        GstSegment tmp;
        /* If moving backward into a new part, set stop
         * to -1 to ensure we play the entire file - workaround
         * a bug in qtdemux that misses bits at the end */
        gst_segment_copy_into (&splitmux->play_segment, &tmp);
        if (tmp.rate < 0)
          tmp.stop = -1;

        /* This is the first pad to move to the new part, activate it */
        GST_DEBUG_OBJECT (splitpad,
            "First pad to change part. Activating part %d with seg %"
            GST_SEGMENT_FORMAT, next_part, &tmp);
        if (!gst_splitmux_part_reader_activate (splitpad->reader, &tmp,
                GST_SEEK_FLAG_NONE))
          goto error;
      }
      splitmux->cur_part = next_part;
    }
    res = TRUE;
  }

  SPLITMUX_SRC_UNLOCK (splitmux);
  return res;
error:
  SPLITMUX_SRC_UNLOCK (splitmux);
  GST_ELEMENT_ERROR (splitmux, RESOURCE, READ, (NULL),
      ("Failed to activate part %d", splitmux->cur_part));
  return FALSE;
}

G_DEFINE_TYPE (SplitMuxSrcPad, splitmux_src_pad, GST_TYPE_PAD);

static void
splitmux_src_pad_constructed (GObject * pad)
{
  gst_pad_set_event_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (splitmux_src_pad_event));
  gst_pad_set_query_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (splitmux_src_pad_query));

  G_OBJECT_CLASS (splitmux_src_pad_parent_class)->constructed (pad);
}

static void
gst_splitmux_src_pad_dispose (GObject * object)
{
  SplitMuxSrcPad *pad = (SplitMuxSrcPad *) (object);

  GST_OBJECT_LOCK (pad);
  if (pad->part_pad) {
    gst_object_unref (pad->part_pad);
    pad->part_pad = NULL;
  }
  GST_OBJECT_UNLOCK (pad);

  G_OBJECT_CLASS (splitmux_src_pad_parent_class)->dispose (object);
}

static void
splitmux_src_pad_class_init (SplitMuxSrcPadClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) (klass);

  gobject_klass->constructed = splitmux_src_pad_constructed;
  gobject_klass->dispose = gst_splitmux_src_pad_dispose;
}

static void
splitmux_src_pad_init (SplitMuxSrcPad * pad)
{
}

/* Event handler for source pads. Proxy events into the child
 * parts as needed
 */
static gboolean
splitmux_src_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (parent);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (parent, "event %" GST_PTR_FORMAT
      " on %" GST_PTR_FORMAT, event, pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstFormat format;
      gdouble rate;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      guint32 seqnum;
      gint i;
      GstClockTime part_start, position;
      GList *cur;
      GstSegment tmp;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &start_type, &start, &stop_type, &stop);

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (splitmux, "can only seek on TIME");
        goto error;
      }
      /* FIXME: Support non-flushing seeks, which might never wake up */
      if (!(flags & GST_SEEK_FLAG_FLUSH)) {
        GST_DEBUG_OBJECT (splitmux, "Only flushing seeks supported");
        goto error;
      }
      seqnum = gst_event_get_seqnum (event);

      SPLITMUX_SRC_LOCK (splitmux);
      if (!splitmux->running || splitmux->num_parts < 1) {
        /* Not started yet */
        SPLITMUX_SRC_UNLOCK (splitmux);
        goto error;
      }

      gst_segment_copy_into (&splitmux->play_segment, &tmp);

      if (!gst_segment_do_seek (&tmp, rate,
              format, flags, start_type, start, stop_type, stop, NULL)) {
        /* Invalid seek requested, ignore it */
        SPLITMUX_SRC_UNLOCK (splitmux);
        goto error;
      }
      position = tmp.position;

      GST_DEBUG_OBJECT (splitmux, "Performing seek with seg %"
          GST_SEGMENT_FORMAT, &tmp);

      GST_DEBUG_OBJECT (splitmux,
          "Handling flushing seek. Sending flush start");

      /* Send flush_start */
      gst_splitmux_push_event (splitmux, gst_event_new_flush_start (), seqnum);

      /* Stop all parts, which will work because of the flush */
      SPLITMUX_SRC_PADS_LOCK (splitmux);
      SPLITMUX_SRC_UNLOCK (splitmux);
      for (cur = g_list_first (splitmux->pads);
          cur != NULL; cur = g_list_next (cur)) {
        SplitMuxSrcPad *target = (SplitMuxSrcPad *) (cur->data);
        GstSplitMuxPartReader *reader = splitmux->parts[target->cur_part];
        gst_splitmux_part_reader_deactivate (reader);
      }

      /* Shut down pad tasks */
      GST_DEBUG_OBJECT (splitmux, "Pausing pad tasks");
      for (cur = g_list_first (splitmux->pads);
          cur != NULL; cur = g_list_next (cur)) {
        GstPad *splitpad = (GstPad *) (cur->data);
        gst_pad_pause_task (GST_PAD (splitpad));
      }
      SPLITMUX_SRC_PADS_UNLOCK (splitmux);
      SPLITMUX_SRC_LOCK (splitmux);

      /* Send flush stop */
      GST_DEBUG_OBJECT (splitmux, "Sending flush stop");
      gst_splitmux_push_flush_stop (splitmux, seqnum);

      /* Everything is stopped, so update the play_segment */
      gst_segment_copy_into (&tmp, &splitmux->play_segment);
      splitmux->segment_seqnum = seqnum;

      /* Work out where to start from now */
      for (i = 0; i < splitmux->num_parts; i++) {
        GstSplitMuxPartReader *reader = splitmux->parts[i];
        GstClockTime part_end =
            gst_splitmux_part_reader_get_end_offset (reader);

        if (part_end > position)
          break;
      }
      if (i == splitmux->num_parts)
        i = splitmux->num_parts - 1;

      part_start =
          gst_splitmux_part_reader_get_start_offset (splitmux->parts[i]);

      GST_DEBUG_OBJECT (splitmux,
          "Seek to time %" GST_TIME_FORMAT " landed in part %d offset %"
          GST_TIME_FORMAT, GST_TIME_ARGS (position),
          i, GST_TIME_ARGS (position - part_start));

      ret = gst_splitmux_src_activate_part (splitmux, i, flags);
      SPLITMUX_SRC_UNLOCK (splitmux);
    }
    default:
      break;
  }

  gst_event_unref (event);
error:
  return ret;
}

static gboolean
splitmux_src_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  /* Query handler for source pads. Proxy queries into the child
   * parts as needed
   */
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (parent);
  gboolean ret = FALSE;

  GST_LOG_OBJECT (parent, "query %" GST_PTR_FORMAT
      " on %" GST_PTR_FORMAT, query, pad);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    case GST_QUERY_POSITION:{
      GstSplitMuxPartReader *part;
      SplitMuxSrcPad *anypad;

      SPLITMUX_SRC_LOCK (splitmux);
      SPLITMUX_SRC_PADS_LOCK (splitmux);
      anypad = (SplitMuxSrcPad *) (splitmux->pads->data);
      part = splitmux->parts[anypad->cur_part];
      ret = gst_splitmux_part_reader_src_query (part, pad, query);
      SPLITMUX_SRC_PADS_UNLOCK (splitmux);
      SPLITMUX_SRC_UNLOCK (splitmux);
      break;
    }
    case GST_QUERY_DURATION:{
      GstFormat fmt;
      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt != GST_FORMAT_TIME)
        break;

      GST_OBJECT_LOCK (splitmux);
      if (splitmux->total_duration > 0) {
        gst_query_set_duration (query, GST_FORMAT_TIME,
            splitmux->total_duration);
        ret = TRUE;
      }
      GST_OBJECT_UNLOCK (splitmux);
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat format;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      if (format != GST_FORMAT_TIME)
        break;

      GST_OBJECT_LOCK (splitmux);
      gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0,
          splitmux->total_duration);
      ret = TRUE;
      GST_OBJECT_UNLOCK (splitmux);

      break;
    }
    default:
      break;
  }
  return ret;
}


gboolean
register_splitmuxsrc (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (splitmux_debug, "splitmuxsrc", 0,
      "Split File Demuxing Source");

  return gst_element_register (plugin, "splitmuxsrc", GST_RANK_NONE,
      GST_TYPE_SPLITMUX_SRC);
}
