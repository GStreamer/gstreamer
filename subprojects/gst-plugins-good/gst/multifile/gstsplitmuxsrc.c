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
 * @title: splitmuxsrc
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
 * ## Example pipelines
 * |[
 * gst-launch-1.0 splitmuxsrc location=video*.mov ! decodebin ! xvimagesink
 * ]| Demux each file part and output the video stream as one continuous stream
 * |[
 * gst-launch-1.0 playbin uri="splitmux://path/to/foo.mp4.*"
 * ]| Play back a set of files created by splitmuxsink
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstsplitmuxsrc.h"
#include "gstsplitutils.h"

#include <glib/gi18n-lib.h>

GST_DEBUG_CATEGORY (splitmux_debug);
#define GST_CAT_DEFAULT splitmux_debug

#define FIXED_TS_OFFSET (1000*GST_SECOND)

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_NUM_OPEN_FRAGMENTS,
  PROP_NUM_LOOKAHEAD
};

#define DEFAULT_OPEN_FRAGMENTS 100
#define DEFAULT_LOOKAHEAD 1

enum
{
  SIGNAL_FORMAT_LOCATION,
  SIGNAL_ADD_FRAGMENT,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate video_aux_src_template =
GST_STATIC_PAD_TEMPLATE ("video_%u",
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


static void
gst_splitmux_part_measured_cb (GstSplitMuxPartReader * part,
    const gchar * filename, GstClockTime offset, GstClockTime duration,
    GstSplitMuxSrc * splitmux);
static void
gst_splitmux_part_loaded_cb (GstSplitMuxPartReader * part,
    GstSplitMuxSrc * splitmux);

static GstPad *gst_splitmux_find_output_pad (GstSplitMuxPartReader * part,
    GstPad * pad, GstSplitMuxSrc * splitmux);
static gboolean gst_splitmux_end_of_part (GstSplitMuxSrc * splitmux,
    SplitMuxSrcPad * pad);
static gboolean gst_splitmux_check_new_caps (SplitMuxSrcPad * splitpad,
    GstEvent * event);
static gboolean gst_splitmux_src_measure_next_part (GstSplitMuxSrc * splitmux);
static gboolean gst_splitmux_src_activate_part (GstSplitMuxSrc * splitmux,
    guint part, GstSeekFlags extra_flags);

static gboolean gst_splitmuxsrc_add_fragment (GstSplitMuxSrc * splitmux,
    const gchar * filename, GstClockTime offset, GstClockTime duration);

static void schedule_lookahead_check (GstSplitMuxSrc * src);

#define _do_init \
    G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, splitmux_src_uri_handler_init); \
    GST_DEBUG_CATEGORY_INIT (splitmux_debug, "splitmuxsrc", 0, "Split File Demuxing Source");
#define gst_splitmux_src_parent_class parent_class

G_DEFINE_TYPE_EXTENDED (GstSplitMuxSrc, gst_splitmux_src, GST_TYPE_BIN, 0,
    _do_init);
GST_ELEMENT_REGISTER_DEFINE (splitmuxsrc, "splitmuxsrc", GST_RANK_NONE,
    GST_TYPE_SPLITMUX_SRC);

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
      &video_aux_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &audio_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &subtitle_src_template);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_splitmux_src_change_state);

  /**
   * GstSplitMuxSrc:location:
   *
   * File glob pattern for the input file fragments. Files that match the glob will be
   * sorted and added to the set of fragments to play.
   *
   * This property is ignored if files are provided via the #GstSplitMuxSrc::format-location
   * signal, or #GstSplitMuxSrc::add-fragment signal
   *
   */
  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Input Pattern",
          "Glob pattern for the location of the files to read", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSplitMuxSrc:num-open-fragments:
   *
   * Upper target for the number of files the splitmuxsrc will try to keep open
   * simultaneously. This limits the number of file handles and threads that
   * will be active.
   *
   * If num-open-fragments is quite small, a few more files might be open
   * than requested, because of the way splitmuxsrc operates internally.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_NUM_OPEN_FRAGMENTS,
      g_param_spec_uint ("num-open-fragments", "Open files limit",
          "Number of files to keep open simultaneously. "
          "(0 = open all fragments at the start). "
          "May still use slightly more if set to less than the number of streams in the files",
          0, G_MAXUINT, DEFAULT_OPEN_FRAGMENTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSplitMuxSrc:num-lookahead:
   *
   * During playback, prepare / open the next N fragments in advance of the playback
   * position.
   *
   * When used in conjunction with a #GstSplitMuxSrc:num-open-fragments limit,
   * that closes fragments that haven't been used recently, lookahead can
   * re-prepare a fragment before it is used, by opening the file and reading
   * file headers and creating internal pads early.
   *
   * This can help when reading off very slow media by avoiding any data stall
   * at fragment transitions.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_NUM_LOOKAHEAD,
      g_param_spec_uint ("num-lookahead", "Fragment Lookahead",
          "When switching fragments, ensure the next N fragments are prepared. "
          "Useful on slow devices if opening/preparing a new fragment can cause playback stalls",
          0, G_MAXUINT, DEFAULT_LOOKAHEAD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  /**
   * GstSplitMuxSrc:format-location:
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

  /**
   * GstSplitMuxSrc::add-fragment:
   * @splitmux: the #GstSplitMuxSrc
   * @filename: The fragment filename to add
   * @offset:   Playback offset for the fragment (can be #GST_CLOCK_TIME_NONE)
   * @duration: Fragment nominal duration (can be #GST_CLOCK_TIME_NONE)
   *
   * Add a file fragment to the set of parts. If the offset and duration are provided,
   * the file will be placed in the set immediately without loading the file to measure
   * it.
   *
   * At least one fragment must be ready and available before starting
   * splitmuxsrc, either via this signal or via the #GstSplitMuxSrc:location property
   * or #GstSplitMuxSrc::format-location signal.
   *
   * Returns: A boolean. TRUE if the fragment was successfully appended.
   *   FALSE on failure.
   *
   * Since: 1.26
   */
  signals[SIGNAL_ADD_FRAGMENT] =
      g_signal_new_class_handler ("add-fragment",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_splitmuxsrc_add_fragment),
      NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 3, G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE,
      GST_TYPE_CLOCK_TIME, GST_TYPE_CLOCK_TIME);
}

static void
gst_splitmux_src_init (GstSplitMuxSrc * splitmux)
{
  g_mutex_init (&splitmux->lock);
  g_rw_lock_init (&splitmux->pads_rwlock);
  splitmux->total_duration = GST_CLOCK_TIME_NONE;
  gst_segment_init (&splitmux->play_segment, GST_FORMAT_TIME);
  splitmux->target_max_readers = DEFAULT_OPEN_FRAGMENTS;
  splitmux->num_lookahead = DEFAULT_LOOKAHEAD;
}

static void
gst_splitmux_src_dispose (GObject * object)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (object);
  GList *cur;

  SPLITMUX_SRC_PADS_WLOCK (splitmux);

  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    GstPad *pad = GST_PAD (cur->data);
    gst_element_remove_pad (GST_ELEMENT (splitmux), pad);
  }
  g_list_free (splitmux->pads);
  splitmux->n_pads = 0;
  splitmux->pads = NULL;
  SPLITMUX_SRC_PADS_WUNLOCK (splitmux);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_splitmux_src_finalize (GObject * object)
{
  GstSplitMuxSrc *splitmux = GST_SPLITMUX_SRC (object);
  g_mutex_clear (&splitmux->lock);
  g_rw_lock_clear (&splitmux->pads_rwlock);
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
    case PROP_NUM_OPEN_FRAGMENTS:
      GST_OBJECT_LOCK (splitmux);
      splitmux->target_max_readers = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_NUM_LOOKAHEAD:
      GST_OBJECT_LOCK (splitmux);
      splitmux->num_lookahead = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (splitmux);
      break;
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
    case PROP_NUM_OPEN_FRAGMENTS:
      GST_OBJECT_LOCK (splitmux);
      g_value_set_uint (value, splitmux->target_max_readers);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_NUM_LOOKAHEAD:
      GST_OBJECT_LOCK (splitmux);
      g_value_set_uint (value, splitmux->num_lookahead);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
do_async_start (GstSplitMuxSrc * splitmux)
{
  GstMessage *message;

  SPLITMUX_SRC_MSG_LOCK (splitmux);
  splitmux->async_pending = TRUE;

  message = gst_message_new_async_start (GST_OBJECT_CAST (splitmux));
  GST_BIN_CLASS (parent_class)->handle_message (GST_BIN_CAST (splitmux),
      message);
  SPLITMUX_SRC_MSG_UNLOCK (splitmux);
}

static void
do_async_done (GstSplitMuxSrc * splitmux)
{
  GstMessage *message;

  SPLITMUX_SRC_MSG_LOCK (splitmux);
  if (splitmux->async_pending) {
    message =
        gst_message_new_async_done (GST_OBJECT_CAST (splitmux),
        GST_CLOCK_TIME_NONE);
    GST_BIN_CLASS (parent_class)->handle_message (GST_BIN_CAST (splitmux),
        message);

    splitmux->async_pending = FALSE;
  }
  SPLITMUX_SRC_MSG_UNLOCK (splitmux);
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
      do_async_start (splitmux);

      if (!gst_splitmux_src_start (splitmux)) {
        do_async_done (splitmux);
        return GST_STATE_CHANGE_FAILURE;
      }
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
  if (ret == GST_STATE_CHANGE_FAILURE) {
    do_async_done (splitmux);
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ret = GST_STATE_CHANGE_ASYNC;
      break;
    default:
      break;
  }


  return ret;
}

static void
gst_splitmux_src_activate_first_part (GstSplitMuxSrc * splitmux)
{
  SPLITMUX_SRC_LOCK (splitmux);
  if (splitmux->running) {
    do_async_done (splitmux);

    if (!gst_splitmux_src_activate_part (splitmux, 0, GST_SEEK_FLAG_NONE)) {
      GST_ELEMENT_ERROR (splitmux, RESOURCE, OPEN_READ, (NULL),
          ("Failed to activate first part for playback"));
    }
    schedule_lookahead_check (splitmux);
  }
  SPLITMUX_SRC_UNLOCK (splitmux);
}

static void
gst_splitmux_part_measured_cb (GstSplitMuxPartReader * part,
    const gchar * filename, GstClockTime offset, GstClockTime duration,
    GstSplitMuxSrc * splitmux)
{
  guint idx = splitmux->num_measured_parts;
  gboolean need_no_more_pads;

  /* signal no-more-pads as we have all pads at this point now */
  SPLITMUX_SRC_LOCK (splitmux);
  need_no_more_pads = !splitmux->pads_complete;
  splitmux->pads_complete = TRUE;
  SPLITMUX_SRC_UNLOCK (splitmux);

  if (need_no_more_pads) {
    GST_DEBUG_OBJECT (splitmux, "Signalling no-more-pads");
    gst_element_no_more_pads (GST_ELEMENT_CAST (splitmux));
  }

  if (idx >= splitmux->num_parts) {
    return;
  }

  GST_DEBUG_OBJECT (splitmux, "Measured file part %s (%u)",
      splitmux->parts[idx]->path, idx);

  /* Post part measured info message */
  GstMessage *msg = gst_message_new_element (GST_OBJECT (splitmux),
      gst_structure_new ("splitmuxsrc-fragment-info",
          "fragment-id", G_TYPE_UINT, idx,
          "location", G_TYPE_STRING, filename,
          "fragment-offset", GST_TYPE_CLOCK_TIME, offset,
          "fragment-duration", GST_TYPE_CLOCK_TIME, duration,
          NULL));
  gst_element_post_message (GST_ELEMENT_CAST (splitmux), msg);

  /* Extend our total duration to cover this part */
  GST_OBJECT_LOCK (splitmux);
  splitmux->total_duration +=
      gst_splitmux_part_reader_get_duration (splitmux->parts[idx]);
  splitmux->play_segment.duration = splitmux->total_duration;
  splitmux->end_offset =
      gst_splitmux_part_reader_get_end_offset (splitmux->parts[idx]);
  GST_OBJECT_UNLOCK (splitmux);

  GST_DEBUG_OBJECT (splitmux,
      "Duration %" GST_TIME_FORMAT ", total duration now: %" GST_TIME_FORMAT
      " and end offset %" GST_TIME_FORMAT,
      GST_TIME_ARGS (gst_splitmux_part_reader_get_duration (splitmux->parts
              [idx])), GST_TIME_ARGS (splitmux->total_duration),
      GST_TIME_ARGS (splitmux->end_offset));

  SPLITMUX_SRC_LOCK (splitmux);
  splitmux->num_measured_parts++;

  /* If we're done or preparing the next part fails, finish here */
  if (splitmux->num_measured_parts >= splitmux->num_parts
      || !gst_splitmux_src_measure_next_part (splitmux)) {
    /* Store how many parts we actually prepared in the end */
    splitmux->num_parts = splitmux->num_measured_parts;

    if (!splitmux->did_initial_measuring) {
      /* All done preparing, activate the first part if this was the initial measurement phase */
      GST_INFO_OBJECT (splitmux,
          "All parts measured. Total duration %" GST_TIME_FORMAT
          " Activating first part", GST_TIME_ARGS (splitmux->total_duration));
      gst_element_call_async (GST_ELEMENT_CAST (splitmux),
          (GstElementCallAsyncFunc) gst_splitmux_src_activate_first_part,
          NULL, NULL);
    }
    splitmux->did_initial_measuring = TRUE;
  }
  SPLITMUX_SRC_UNLOCK (splitmux);
}

static void
gst_splitmux_part_loaded_cb (GstSplitMuxPartReader * part,
    GstSplitMuxSrc * splitmux)
{
  SPLITMUX_SRC_LOCK (splitmux);
  if (splitmux->did_initial_measuring) {
    /* If we've already moved to playing, do another lookahead check for each fragment
     * we load, to trigger loading another if needed */
    schedule_lookahead_check (splitmux);
  }
  SPLITMUX_SRC_UNLOCK (splitmux);
}

static GstBusSyncReply
gst_splitmux_part_bus_handler (GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstSplitMuxSrc *splitmux = user_data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ASYNC_DONE:{
      break;
    }
    case GST_MESSAGE_ERROR:{
      GST_ERROR_OBJECT (splitmux,
          "Got error message from part %" GST_PTR_FORMAT ": %" GST_PTR_FORMAT,
          GST_MESSAGE_SRC (msg), msg);
      SPLITMUX_SRC_LOCK (splitmux);
      if (splitmux->num_measured_parts < splitmux->num_parts) {
        guint idx = splitmux->num_measured_parts;

        if (idx == 0) {
          GST_ERROR_OBJECT (splitmux,
              "Failed to prepare first file part %s for playback",
              splitmux->parts[idx]->path);
          GST_ELEMENT_ERROR (splitmux, RESOURCE, OPEN_READ, (NULL),
              ("Failed to prepare first file part %s for playback",
                  splitmux->parts[idx]->path));
        } else {
          GST_WARNING_OBJECT (splitmux,
              "Failed to prepare file part %s. Cannot play past there.",
              splitmux->parts[idx]->path);
          GST_ELEMENT_WARNING (splitmux, RESOURCE, READ, (NULL),
              ("Failed to prepare file part %s. Cannot play past there.",
                  splitmux->parts[idx]->path));
        }

        /* Store how many parts we actually prepared in the end */
        splitmux->num_parts = splitmux->num_measured_parts;

        if (idx > 0 && !splitmux->did_initial_measuring) {
          /* All done preparing, activate the first part */
          GST_INFO_OBJECT (splitmux,
              "All parts prepared. Total duration %" GST_TIME_FORMAT
              " Activating first part",
              GST_TIME_ARGS (splitmux->total_duration));
          gst_element_call_async (GST_ELEMENT_CAST (splitmux),
              (GstElementCallAsyncFunc) gst_splitmux_src_activate_first_part,
              NULL, NULL);
        }
        splitmux->did_initial_measuring = TRUE;
        SPLITMUX_SRC_UNLOCK (splitmux);

        do_async_done (splitmux);
      } else {
        SPLITMUX_SRC_UNLOCK (splitmux);

        /* Need to update the message source so that it's part of the element
         * hierarchy the application would expect */
        msg = gst_message_copy (msg);
        gst_object_replace ((GstObject **) & msg->src, (GstObject *) splitmux);
        gst_element_post_message (GST_ELEMENT_CAST (splitmux), msg);
      }
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static GstSplitMuxPartReader *
gst_splitmux_part_reader_create (GstSplitMuxSrc * splitmux,
    const char *filename, gsize index)
{
  GstSplitMuxPartReader *r;
  GstBus *bus;

  r = g_object_new (GST_TYPE_SPLITMUX_PART_READER, NULL);

  gst_splitmux_part_reader_set_callbacks (r, splitmux,
      (GstSplitMuxPartReaderPadCb) gst_splitmux_find_output_pad,
      (GstSplitMuxPartReaderMeasuredCb) gst_splitmux_part_measured_cb,
      (GstSplitMuxPartReaderLoadedCb) gst_splitmux_part_loaded_cb);
  gst_splitmux_part_reader_set_location (r, filename);

  bus = gst_element_get_bus (GST_ELEMENT_CAST (r));
  gst_bus_set_sync_handler (bus, gst_splitmux_part_bus_handler, splitmux, NULL);
  gst_object_unref (bus);

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
      if (splitmux->segment_seqnum) {
        event = gst_event_make_writable (event);
        gst_event_set_seqnum (event, splitmux->segment_seqnum);
      }
      break;
    }
    case GST_EVENT_SEGMENT:{
      GstClockTime duration;
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
          seg.stop = splitmux->play_segment.stop + FIXED_TS_OFFSET;
        else
          seg.stop = -1;
      } else {
        /* Reverse playback from stop time to start time */
        /* See if an end point was requested in the seek */
        if (splitmux->play_segment.start != -1) {
          seg.start = splitmux->play_segment.start + FIXED_TS_OFFSET;
          seg.time = splitmux->play_segment.time;
        } else {
          seg.start = splitpad->segment.start;
          seg.time = splitpad->segment.time;
        }
      }

      GST_OBJECT_LOCK (splitmux);
      duration = splitmux->total_duration;
      GST_OBJECT_UNLOCK (splitmux);

      if (duration > 0)
        seg.duration = duration;
      else
        seg.duration = GST_CLOCK_TIME_NONE;

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

static guint
count_not_linked (GstSplitMuxSrc * splitmux)
{
  GList *cur;
  guint ret = 0;

  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    SplitMuxSrcPad *splitpad = (SplitMuxSrcPad *) (cur->data);
    if (GST_PAD_LAST_FLOW_RETURN (splitpad) == GST_FLOW_NOT_LINKED)
      ret++;
  }

  return ret;
}

static void
gst_splitmux_pad_loop (GstPad * pad)
{
  /* Get one event/buffer from the associated part and push */
  SplitMuxSrcPad *splitpad = (SplitMuxSrcPad *) (pad);
  GstSplitMuxSrc *splitmux = (GstSplitMuxSrc *) gst_pad_get_parent (pad);
  GstDataQueueItem *item = NULL;
  GstSplitMuxPartReader *reader = NULL;
  GstPad *part_pad;
  GstFlowReturn ret;

  GST_OBJECT_LOCK (splitpad);
  if (splitpad->part_pad == NULL) {
    GST_DEBUG_OBJECT (splitmux,
        "Pausing task because part reader is not present");
    GST_OBJECT_UNLOCK (splitpad);
    gst_pad_pause_task (pad);
    gst_object_unref (splitmux);
    return;
  }
  part_pad = gst_object_ref (splitpad->part_pad);
  GST_OBJECT_UNLOCK (splitpad);

  SPLITMUX_SRC_LOCK (splitmux);
  reader = splitpad->reader ? gst_object_ref (splitpad->reader) : NULL;
  SPLITMUX_SRC_UNLOCK (splitmux);

  if (reader == NULL)
    goto flushing;

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
      if (ret < GST_FLOW_EOS) {
        GST_ELEMENT_FLOW_ERROR (splitmux, ret);
      } else if (ret == GST_FLOW_NOT_LINKED) {
        gboolean post_error;
        guint n_notlinked;

        /* Only post not-linked if all pads are not-linked */
        SPLITMUX_SRC_PADS_RLOCK (splitmux);
        n_notlinked = count_not_linked (splitmux);
        post_error = (splitmux->pads_complete
            && n_notlinked == splitmux->n_pads);
        SPLITMUX_SRC_PADS_RUNLOCK (splitmux);

        if (post_error)
          GST_ELEMENT_FLOW_ERROR (splitmux, ret);
      }
    }
  }
  g_free (item);

  gst_object_unref (reader);
  gst_object_unref (part_pad);
  gst_object_unref (splitmux);
  return;

error:
  /* Fall through */
  GST_ELEMENT_ERROR (splitmux, RESOURCE, OPEN_READ, (NULL),
      ("Error reading part file %s", GST_STR_NULL (reader->path)));
flushing:
  gst_pad_pause_task (pad);
  if (reader != NULL)
    gst_object_unref (reader);
  gst_object_unref (part_pad);
  gst_object_unref (splitmux);
  return;
}

static void
reduce_active_readers (GstSplitMuxSrc * splitmux)
{
  /* Try and reduce the active reader count by deactivating the
   * oldest reader if it's no longer in use */
  if (splitmux->target_max_readers == 0) {
    return;
  }
  while (g_queue_get_length (splitmux->active_parts) >=
      splitmux->target_max_readers) {
    GstSplitMuxPartReader *oldest_reader =
        g_queue_peek_head (splitmux->active_parts);
    if (gst_splitmux_part_reader_is_playing (oldest_reader)) {
      /* This part is still playing on some pad(s). Keep it active */
      return;
    }

    GST_DEBUG_OBJECT (splitmux, "Stopping least recently used part %s",
        oldest_reader->path);
    oldest_reader = g_queue_pop_head (splitmux->active_parts);
    gst_splitmux_part_reader_stop (oldest_reader);
    g_object_unref (oldest_reader);
  }
}

static void
add_to_active_readers (GstSplitMuxSrc * splitmux,
    GstSplitMuxPartReader * reader, gboolean add_as_oldest)
{
  if (splitmux->target_max_readers != 0) {
    /* Check if it's already in the active reader pool, and move this reader
     * to the tail, or else add a ref and push it on the tail */
    if (gst_splitmux_part_reader_is_loaded (reader)) {
      /* Already in the queue, and reffed, move it to the end without
       * adding another ref */
      gboolean in_queue = g_queue_remove (splitmux->active_parts, reader);
      g_assert (in_queue == TRUE);
    } else {
      /* Putting it in the queue. Add a ref */
      g_object_ref (reader);
      /* When adding a new reader to the list, reduce active readers first */
      reduce_active_readers (splitmux);
    }
    if (add_as_oldest) {
      g_queue_push_head (splitmux->active_parts, reader);
    } else {
      g_queue_push_tail (splitmux->active_parts, reader);
    }
  }
}

/* Called with splitmuxsrc lock held */
static gboolean
gst_splitmux_src_activate_part (GstSplitMuxSrc * splitmux, guint part,
    GstSeekFlags extra_flags)
{
  GST_DEBUG_OBJECT (splitmux, "Activating part %d", part);
  GstSplitMuxPartReader *reader = gst_object_ref (splitmux->parts[part]);

  splitmux->cur_part = part;
  add_to_active_readers (splitmux, reader, FALSE);
  SPLITMUX_SRC_UNLOCK (splitmux);

  /* Drop lock around calling activate, as it might call back
   * into the splitmuxsrc when exposing pads */
  if (!gst_splitmux_part_reader_activate (reader,
          &splitmux->play_segment, extra_flags)) {
    gst_object_unref (reader);
    /* Re-take the lock before exiting */
    SPLITMUX_SRC_LOCK (splitmux);
    return FALSE;
  }
  gst_object_unref (reader);

  SPLITMUX_SRC_LOCK (splitmux);
  SPLITMUX_SRC_PADS_RLOCK (splitmux);
  GList *cur;

  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    SplitMuxSrcPad *splitpad = (SplitMuxSrcPad *) (cur->data);
    GST_OBJECT_LOCK (splitpad);
    splitpad->cur_part = part;
    splitpad->reader = splitmux->parts[splitpad->cur_part];
    if (splitpad->part_pad)
      gst_object_unref (splitpad->part_pad);
    splitpad->part_pad =
        gst_splitmux_part_reader_lookup_pad (splitpad->reader,
        (GstPad *) (splitpad));
    GST_OBJECT_UNLOCK (splitpad);

    /* Make sure we start with a DISCONT */
    splitpad->set_next_discont = TRUE;
    splitpad->clear_next_discont = FALSE;

    gst_pad_start_task (GST_PAD (splitpad),
        (GstTaskFunction) gst_splitmux_pad_loop, splitpad, NULL);
  }
  SPLITMUX_SRC_PADS_RUNLOCK (splitmux);

  return TRUE;
}

static gboolean
gst_splitmux_src_measure_next_part (GstSplitMuxSrc * splitmux)
{
  guint idx = splitmux->num_measured_parts;
  g_assert (idx < splitmux->num_parts);

  GstClockTime end_offset = 0;
  /* Take the end offset of the most recently measured part */
  if (splitmux->num_measured_parts > 0) {
    GstSplitMuxPartReader *reader =
        splitmux->parts[splitmux->num_measured_parts - 1];
    end_offset = gst_splitmux_part_reader_get_end_offset (reader);
  }

  for (guint idx = splitmux->num_measured_parts; idx < splitmux->num_parts;
      idx++) {
    /* Walk forward until we find a part that needs measuring */
    GstSplitMuxPartReader *reader = splitmux->parts[idx];

    GstClockTime start_offset =
        gst_splitmux_part_reader_get_start_offset (reader);
    if (start_offset == GST_CLOCK_TIME_NONE) {
      GST_DEBUG_OBJECT (splitmux,
          "Setting start offset for file part %s (%u) to %" GST_TIMEP_FORMAT,
          reader->path, idx, &end_offset);
      gst_splitmux_part_reader_set_start_offset (reader, end_offset,
          FIXED_TS_OFFSET);
    }

    if (gst_splitmux_part_reader_needs_measuring (reader)) {
      GST_DEBUG_OBJECT (splitmux, "Measuring file part %s (%u)",
          reader->path, idx);

      add_to_active_readers (splitmux, reader, TRUE);

      SPLITMUX_SRC_UNLOCK (splitmux);
      if (!gst_splitmux_part_reader_prepare (reader)) {
        GST_WARNING_OBJECT (splitmux,
            "Failed to prepare file part %s. Cannot play past there.",
            reader->path);
        GST_ELEMENT_WARNING (splitmux, RESOURCE, READ, (NULL),
            ("Failed to prepare file part %s. Cannot play past there.",
                reader->path));
        gst_splitmux_part_reader_unprepare (reader);
        g_object_unref (reader);

        SPLITMUX_SRC_LOCK (splitmux);
        splitmux->parts[idx] = NULL;

        splitmux->num_measured_parts = idx;
        return FALSE;
      }

      SPLITMUX_SRC_LOCK (splitmux);
      return TRUE;
    }

    GST_OBJECT_LOCK (splitmux);

    /* Get the end offset (start offset of the next piece) */
    end_offset = gst_splitmux_part_reader_get_end_offset (reader);
    splitmux->total_duration += gst_splitmux_part_reader_get_duration (reader);
    splitmux->num_measured_parts++;

    GST_OBJECT_UNLOCK (splitmux);
  }

  return TRUE;
}

static gboolean
gst_splitmux_src_start (GstSplitMuxSrc * splitmux)
{
  gboolean ret = FALSE;
  GError *err = NULL;
  gchar *basename = NULL;
  gchar *dirname = NULL;
  gchar **files = NULL;
  guint i;

  SPLITMUX_SRC_LOCK (splitmux);
  if (splitmux->running) {
    /* splitmux is still running / stopping. We can't start again yet */
    SPLITMUX_SRC_UNLOCK (splitmux);
    return FALSE;
  }

  GST_DEBUG_OBJECT (splitmux, "Starting");
  splitmux->active_parts = g_queue_new ();

  if (splitmux->num_parts == 0) {
    /* No parts were added via add-fragment signal, try via
     * format-location signal and location property glob */
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

      if (files == NULL || *files == NULL) {
        SPLITMUX_SRC_UNLOCK (splitmux);
        goto no_files;
      }
    }
  }

  splitmux->pads_complete = FALSE;
  splitmux->running = TRUE;

  if (files != NULL) {
    g_assert (splitmux->parts == NULL);

    splitmux->num_parts_alloced = g_strv_length (files);
    splitmux->parts =
        g_new0 (GstSplitMuxPartReader *, splitmux->num_parts_alloced);

    /* Create all part pipelines */
    for (i = 0; i < splitmux->num_parts_alloced; i++) {
      splitmux->parts[i] =
          gst_splitmux_part_reader_create (splitmux, files[i], i);
      if (splitmux->parts[i] == NULL)
        break;
    }

    /* Store how many parts we actually created */
    splitmux->num_parts = i;
  }
  splitmux->num_measured_parts = 0;

  /* Update total_duration state variable */
  GST_OBJECT_LOCK (splitmux);
  splitmux->total_duration = 0;
  splitmux->end_offset = 0;
  GST_OBJECT_UNLOCK (splitmux);

  /* Ensure all the parts we have are measured.
   * Start the first: it will asynchronously go to PAUSED
   * or error out and then we can proceed with the next one
   */
  if (!gst_splitmux_src_measure_next_part (splitmux) || splitmux->num_parts < 1) {
    SPLITMUX_SRC_UNLOCK (splitmux);
    goto failed_part;
  }
  if (splitmux->num_measured_parts >= splitmux->num_parts) {
    /* Nothing needed measuring, activate the first part */
    GST_INFO_OBJECT (splitmux,
        "All parts measured. Total duration %" GST_TIME_FORMAT
        " Activating first part", GST_TIME_ARGS (splitmux->total_duration));
    gst_element_call_async (GST_ELEMENT_CAST (splitmux),
        (GstElementCallAsyncFunc) gst_splitmux_src_activate_first_part,
        NULL, NULL);
    splitmux->did_initial_measuring = TRUE;
  }
  SPLITMUX_SRC_UNLOCK (splitmux);

  /* All good now: we have to wait for all parts to be asynchronously
   * prepared to know the total duration we can play */
  ret = TRUE;

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
  splitmux->running = FALSE;
  GST_DEBUG_OBJECT (splitmux, "Stopping");

  /* Stop all part readers. */
  for (i = 0; i < splitmux->num_parts; i++) {
    if (splitmux->parts[i] == NULL)
      continue;

    /* Take a ref so we can drop the lock around calling unprepare */
    GstSplitMuxPartReader *part = g_object_ref (splitmux->parts[i]);
    SPLITMUX_SRC_UNLOCK (splitmux);
    gst_splitmux_part_reader_unprepare (part);
    g_object_unref (part);
    SPLITMUX_SRC_LOCK (splitmux);
  }

  SPLITMUX_SRC_PADS_WLOCK (splitmux);
  pads_list = splitmux->pads;
  splitmux->pads = NULL;
  SPLITMUX_SRC_PADS_WUNLOCK (splitmux);

  SPLITMUX_SRC_UNLOCK (splitmux);
  for (cur = g_list_first (pads_list); cur != NULL; cur = g_list_next (cur)) {
    SplitMuxSrcPad *tmp = (SplitMuxSrcPad *) (cur->data);
    gst_pad_stop_task (GST_PAD (tmp));
    gst_element_remove_pad (GST_ELEMENT (splitmux), GST_PAD (tmp));
  }
  g_list_free (pads_list);
  SPLITMUX_SRC_LOCK (splitmux);

  /* Now the pad task is stopped we can destroy the readers */
  g_queue_free_full (splitmux->active_parts, g_object_unref);

  for (i = 0; i < splitmux->num_parts; i++) {
    if (splitmux->parts[i] == NULL)
      continue;
    g_object_unref (splitmux->parts[i]);
    splitmux->parts[i] = NULL;
  }

  g_free (splitmux->parts);
  splitmux->parts = NULL;
  splitmux->num_parts = 0;
  splitmux->num_measured_parts = 0;
  splitmux->did_initial_measuring = FALSE;
  splitmux->num_parts_alloced = 0;
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

  SPLITMUX_SRC_PADS_WLOCK (splitmux);
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
    splitmux->n_pads++;

    gst_pad_set_active (target, TRUE);

    splitmux_and_pad.splitmux = splitmux;
    splitmux_and_pad.splitpad = (SplitMuxSrcPad *) target;
    gst_pad_sticky_events_foreach (pad, handle_sticky_events,
        &splitmux_and_pad);
    is_new_pad = TRUE;
  }
  SPLITMUX_SRC_PADS_WUNLOCK (splitmux);

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
gst_splitmux_push_event (GstSplitMuxSrc * splitmux, GstEvent * e,
    guint32 seqnum)
{
  GList *cur;

  if (seqnum) {
    e = gst_event_make_writable (e);
    gst_event_set_seqnum (e, seqnum);
  }

  SPLITMUX_SRC_PADS_RLOCK (splitmux);
  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    GstPad *pad = GST_PAD_CAST (cur->data);
    gst_event_ref (e);
    gst_pad_push_event (pad, e);
  }
  SPLITMUX_SRC_PADS_RUNLOCK (splitmux);

  gst_event_unref (e);
}

static void
gst_splitmux_push_flush_stop (GstSplitMuxSrc * splitmux, guint32 seqnum)
{
  GstEvent *e = gst_event_new_flush_stop (TRUE);
  GList *cur;

  if (seqnum) {
    e = gst_event_make_writable (e);
    gst_event_set_seqnum (e, seqnum);
  }

  SPLITMUX_SRC_PADS_RLOCK (splitmux);
  for (cur = g_list_first (splitmux->pads);
      cur != NULL; cur = g_list_next (cur)) {
    SplitMuxSrcPad *target = (SplitMuxSrcPad *) (cur->data);

    gst_event_ref (e);
    gst_pad_push_event (GST_PAD_CAST (target), e);
    target->sent_caps = FALSE;
    target->sent_stream_start = FALSE;
    target->sent_segment = FALSE;
  }
  SPLITMUX_SRC_PADS_RUNLOCK (splitmux);

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
  if (gst_splitmux_part_is_eos (splitmux->parts[splitpad->cur_part])) {
    GST_DEBUG_OBJECT (splitmux, "All pads in part %d finished. Deactivating it",
        cur_part);
    gst_splitmux_part_reader_deactivate (splitmux->parts[cur_part]);
  }

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

    if (splitmux->cur_part != next_part) {
      if (!gst_splitmux_part_reader_is_playing (splitpad->reader)) {
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
        add_to_active_readers (splitmux, splitpad->reader, FALSE);

        if (!gst_splitmux_part_reader_activate (splitpad->reader, &tmp,
                GST_SEEK_FLAG_NONE)) {
          goto error;
        }
      }
      splitmux->cur_part = next_part;
      schedule_lookahead_check (splitmux);
    }

    if (splitpad->part_pad)
      gst_object_unref (splitpad->part_pad);
    splitpad->part_pad =
        gst_splitmux_part_reader_lookup_pad (splitpad->reader,
        (GstPad *) (splitpad));

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
      if (splitmux->segment_seqnum == seqnum) {
        GST_DEBUG_OBJECT (splitmux, "Ignoring duplicate seek event");
        SPLITMUX_SRC_UNLOCK (splitmux);
        ret = TRUE;
        goto done;
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
      SPLITMUX_SRC_PADS_RLOCK (splitmux);
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
      SPLITMUX_SRC_PADS_RUNLOCK (splitmux);
      SPLITMUX_SRC_LOCK (splitmux);

      /* Send flush stop */
      GST_DEBUG_OBJECT (splitmux, "Sending flush stop");
      gst_splitmux_push_flush_stop (splitmux, seqnum);

      /* Everything is stopped, so update the play_segment */
      gst_segment_copy_into (&tmp, &splitmux->play_segment);
      splitmux->segment_seqnum = seqnum;

      /* Work out where to start from now */
      for (i = 0; i < splitmux->num_parts - 1; i++) {
        GstSplitMuxPartReader *reader = splitmux->parts[i + 1];
        GstClockTime part_start =
            gst_splitmux_part_reader_get_start_offset (reader);

        GST_LOG_OBJECT (splitmux, "Part %d has start offset %" GST_TIMEP_FORMAT
            " (want position %" GST_TIMEP_FORMAT ")", i, &part_start,
            &position);
        if (position < part_start)
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
    case GST_EVENT_RECONFIGURE:{
      GST_DEBUG_OBJECT (splitmux, "reconfigure event on pad %" GST_PTR_FORMAT,
          pad);

      SPLITMUX_SRC_PADS_RLOCK (splitmux);
      /* Restart the task on this pad */
      gst_pad_start_task (GST_PAD (pad),
          (GstTaskFunction) gst_splitmux_pad_loop, pad, NULL);
      SPLITMUX_SRC_PADS_RUNLOCK (splitmux);
      break;
    }
    default:
      break;
  }

done:
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
    case GST_QUERY_POSITION:
    case GST_QUERY_LATENCY:{
      GstSplitMuxPartReader *part;
      SplitMuxSrcPad *anypad;

      SPLITMUX_SRC_LOCK (splitmux);
      SPLITMUX_SRC_PADS_RLOCK (splitmux);
      anypad = (SplitMuxSrcPad *) (splitmux->pads->data);
      part = splitmux->parts[anypad->cur_part];
      ret = gst_splitmux_part_reader_src_query (part, pad, query);
      SPLITMUX_SRC_PADS_RUNLOCK (splitmux);
      SPLITMUX_SRC_UNLOCK (splitmux);
      break;
    }
    case GST_QUERY_DURATION:{
      GstClockTime duration;
      GstFormat fmt;

      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt != GST_FORMAT_TIME)
        break;

      GST_OBJECT_LOCK (splitmux);
      duration = splitmux->total_duration;
      GST_OBJECT_UNLOCK (splitmux);

      if (duration > 0 && duration != GST_CLOCK_TIME_NONE) {
        gst_query_set_duration (query, GST_FORMAT_TIME, duration);
        ret = TRUE;
      }
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
    case GST_QUERY_SEGMENT:{
      GstFormat format;
      gint64 start, stop;

      SPLITMUX_SRC_LOCK (splitmux);
      format = splitmux->play_segment.format;

      start =
          gst_segment_to_stream_time (&splitmux->play_segment, format,
          splitmux->play_segment.start);
      if (splitmux->play_segment.stop == GST_CLOCK_TIME_NONE) {
        if (splitmux->play_segment.duration == GST_CLOCK_TIME_NONE)
          stop = GST_CLOCK_TIME_NONE;
        else
          stop = start + splitmux->play_segment.duration;
      } else {
        stop = gst_segment_to_stream_time (&splitmux->play_segment, format,
            splitmux->play_segment.stop);
      }

      gst_query_set_segment (query, splitmux->play_segment.rate, format, start,
          stop);
      ret = TRUE;

      SPLITMUX_SRC_UNLOCK (splitmux);
    }
    default:
      break;
  }
  return ret;
}

static gboolean
gst_splitmuxsrc_add_fragment (GstSplitMuxSrc * splitmux,
    const gchar * filename, GstClockTime offset, GstClockTime duration)
{
  SPLITMUX_SRC_LOCK (splitmux);
  /* Ensure we have enough space in the parts array, reallocating if necessary */
  if (splitmux->num_parts == splitmux->num_parts_alloced) {
    gsize to_alloc = splitmux->num_parts_alloced;
    to_alloc = MAX (to_alloc + 8, 3 * to_alloc / 2);

    splitmux->parts =
        g_renew (GstSplitMuxPartReader *, splitmux->parts, to_alloc);
    /* Zero newly allocated memory */
    for (gsize i = splitmux->num_parts_alloced; i < to_alloc; i++) {
      splitmux->parts[i] = NULL;
    }
    splitmux->num_parts_alloced = to_alloc;
  }

  GstSplitMuxPartReader *reader =
      gst_splitmux_part_reader_create (splitmux, filename, splitmux->num_parts);
  if (GST_CLOCK_TIME_IS_VALID (offset)) {
    gst_splitmux_part_reader_set_start_offset (reader, offset, FIXED_TS_OFFSET);
  }
  if (GST_CLOCK_TIME_IS_VALID (duration)) {
    gst_splitmux_part_reader_set_duration (reader, duration);
  }

  splitmux->parts[splitmux->num_parts] = reader;
  splitmux->num_parts++;

  /* If we already did the initial measuring, and we added a new first part here,
   * call 'measure_next_part' to get it measured / added to our duration */
  if (splitmux->did_initial_measuring
      && (splitmux->num_measured_parts + 1) == splitmux->num_parts) {
    if (!gst_splitmux_src_measure_next_part (splitmux)) {
    }
  }

  SPLITMUX_SRC_UNLOCK (splitmux);
  return TRUE;
}

static void
do_lookahead_check (GstSplitMuxSrc * splitmux)
{
  SPLITMUX_SRC_LOCK (splitmux);
  splitmux->lookahead_check_pending = FALSE;

  if (!splitmux->running) {
    goto done;
  }

  GST_OBJECT_LOCK (splitmux);
  guint lookahead = splitmux->num_lookahead;
  GST_OBJECT_UNLOCK (splitmux);

  if (splitmux->target_max_readers != 0 &&
      splitmux->target_max_readers <= lookahead) {
    /* Don't let lookahead activate more readers than the target */
    lookahead = splitmux->target_max_readers - 1;
  }
  if (lookahead == 0) {
    goto done;
  }
  if (splitmux->play_segment.rate > 0.0) {
    /* Walk forward */
    guint i;
    gsize limit = splitmux->cur_part + lookahead;
    if (limit >= splitmux->num_parts) {
      /* Don't check past the end */
      limit = splitmux->num_parts - 1;
    }

    for (i = splitmux->cur_part + 1; i <= limit; i++) {
      GstSplitMuxPartReader *reader = splitmux->parts[i];

      if (!gst_splitmux_part_reader_is_loaded (reader)) {
        GST_DEBUG_OBJECT (splitmux,
            "Loading part %u reader %" GST_PTR_FORMAT " for lookahead (cur %u)",
            i, reader, splitmux->cur_part);
        gst_object_ref (reader);
        add_to_active_readers (splitmux, reader, FALSE);

        /* Drop lock before calling activate, as it might call back
         * into the splitmuxsrc when exposing pads */
        SPLITMUX_SRC_UNLOCK (splitmux);

        gst_splitmux_part_reader_prepare (reader);
        gst_object_unref (reader);
        /* Only prepare one part at a time */
        return;
      }

      /* Already active, but promote it in the LRU list */
      add_to_active_readers (splitmux, reader, FALSE);
    }
  } else {
    /* playing backward */
    guint i;
    gsize limit = 0;
    if (splitmux->cur_part > lookahead) {
      limit = splitmux->cur_part - lookahead;
    }
    for (i = splitmux->cur_part; i > limit; i--) {
      GstSplitMuxPartReader *reader = splitmux->parts[i - 1];

      if (!gst_splitmux_part_reader_is_loaded (reader)) {
        GST_DEBUG_OBJECT (splitmux,
            "Loading part %u reader %" GST_PTR_FORMAT " for lookahead (cur %u)",
            i - 1, reader, splitmux->cur_part);
        gst_object_ref (reader);
        add_to_active_readers (splitmux, reader, FALSE);
        SPLITMUX_SRC_UNLOCK (splitmux);

        /* Drop lock before calling activate, as it might call back
         * into the splitmuxsrc when exposing pads */
        gst_splitmux_part_reader_prepare (reader);
        gst_object_unref (reader);
        /* Only prepare one part at a time */
        return;
      }
      /* Already active, but promote it in the LRU list */
      add_to_active_readers (splitmux, reader, FALSE);
    }
  }

done:
  SPLITMUX_SRC_UNLOCK (splitmux);
}

/* Called with SPLITMUX lock held */
static void
schedule_lookahead_check (GstSplitMuxSrc * splitmux)
{
  if (splitmux->lookahead_check_pending || splitmux->num_lookahead == 0
      || splitmux->target_max_readers == 0) {
    /* No need to do lookahead checks */
    return;
  }
  splitmux->lookahead_check_pending = TRUE;

  gst_element_call_async (GST_ELEMENT_CAST (splitmux),
      (GstElementCallAsyncFunc) do_lookahead_check, NULL, NULL);
}
