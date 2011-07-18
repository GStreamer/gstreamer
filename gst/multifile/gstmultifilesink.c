/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2006 Wim Taymans <wim@fluendo.com>
 *                    2006 David A. Schleef <ds@schleef.org>
 *
 * gstmultifilesink.c:
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
/**
 * SECTION:element-multifilesink
 * @see_also: #GstFileSrc
 *
 * Write incoming data to a series of sequentially-named files.
 *
 * The filename property should contain a string with a \%d placeholder that will
 * be substituted with the index for each filename.
 *
 * If the #GstMultiFileSink:post-messages property is #TRUE, it sends an application
 * message named
 * <classname>&quot;GstMultiFileSink&quot;</classname> after writing each
 * buffer.
 *
 * The message's structure contains these fields:
 * <itemizedlist>
 * <listitem>
 *   <para>
 *   #gchar *
 *   <classname>&quot;filename&quot;</classname>:
 *   the filename where the buffer was written.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #gint
 *   <classname>&quot;index&quot;</classname>:
 *   the index of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;timestamp&quot;</classname>:
 *   the timestamp of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;stream-time&quot;</classname>:
 *   the stream time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;running-time&quot;</classname>:
 *   the running_time of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #GstClockTime
 *   <classname>&quot;duration&quot;</classname>:
 *   the duration of the buffer.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #guint64
 *   <classname>&quot;offset&quot;</classname>:
 *   the offset of the buffer that triggered the message.
 *   </para>
 * </listitem>
 * <listitem>
 *   <para>
 *   #guint64
 *   <classname>&quot;offset-end&quot;</classname>:
 *   the offset-end of the buffer that triggered the message.
 *   </para>
 * </listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc ! multifilesink
 * gst-launch videotestsrc ! multifilesink post-messages=true filename="frame%d"
 * ]|
 * </refsect2>
 *
 * Last reviewed on 2009-09-11 (0.10.17)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include <gst/base/gstbasetransform.h>
#include <glib/gstdio.h>
#include "gstmultifilesink.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_multi_file_sink_debug);
#define GST_CAT_DEFAULT gst_multi_file_sink_debug

#define DEFAULT_LOCATION "%05d"
#define DEFAULT_INDEX 0
#define DEFAULT_POST_MESSAGES FALSE
#define DEFAULT_NEXT_FILE GST_MULTI_FILE_SINK_NEXT_BUFFER
#define DEFAULT_MAX_FILES 0

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_INDEX,
  PROP_POST_MESSAGES,
  PROP_NEXT_FILE,
  PROP_MAX_FILES,
  PROP_LAST
};

static void gst_multi_file_sink_finalize (GObject * object);

static void gst_multi_file_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_file_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_multi_file_sink_stop (GstBaseSink * sink);
static GstFlowReturn gst_multi_file_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static gboolean gst_multi_file_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static gboolean gst_multi_file_sink_open_next_file (GstMultiFileSink *
    multifilesink);
static void gst_multi_file_sink_close_file (GstMultiFileSink * multifilesink,
    GstBuffer * buffer);
static void gst_multi_file_sink_ensure_max_files (GstMultiFileSink *
    multifilesink);

#define GST_TYPE_MULTI_FILE_SINK_NEXT (gst_multi_file_sink_next_get_type ())
static GType
gst_multi_file_sink_next_get_type (void)
{
  static GType multi_file_sync_next_type = 0;
  static const GEnumValue next_types[] = {
    {GST_MULTI_FILE_SINK_NEXT_BUFFER, "New file for each buffer", "buffer"},
    {GST_MULTI_FILE_SINK_NEXT_DISCONT, "New file after each discontinuity",
        "discont"},
    {GST_MULTI_FILE_SINK_NEXT_KEY_FRAME, "New file at each key frame "
          "(Useful for MPEG-TS segmenting)", "key-frame"},
    {0, NULL, NULL}
  };

  if (!multi_file_sync_next_type) {
    multi_file_sync_next_type =
        g_enum_register_static ("GstMultiFileSinkNext", next_types);
  }

  return multi_file_sync_next_type;
}

GST_BOILERPLATE (GstMultiFileSink, gst_multi_file_sink, GstBaseSink,
    GST_TYPE_BASE_SINK);

static void
gst_multi_file_sink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_multi_file_sink_debug, "multifilesink", 0,
      "multifilesink element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details_simple (gstelement_class, "Multi-File Sink",
      "Sink/File",
      "Write buffers to a sequentially named set of files",
      "David Schleef <ds@schleef.org>");
}

static void
gst_multi_file_sink_class_init (GstMultiFileSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_multi_file_sink_set_property;
  gobject_class->get_property = gst_multi_file_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to write", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INDEX,
      g_param_spec_int ("index", "Index",
          "Index to use with location property to create file names.  The "
          "index is incremented by one for each buffer written.",
          0, G_MAXINT, DEFAULT_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiFileSink:post-messages
   *
   * Post a message on the GstBus for each file.
   *
   * Since: 0.10.17
   */
  g_object_class_install_property (gobject_class, PROP_POST_MESSAGES,
      g_param_spec_boolean ("post-messages", "Post Messages",
          "Post a message for each file with information of the buffer",
          DEFAULT_POST_MESSAGES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiFileSink:next-file
   *
   * When to start a new file.
   *
   * Since: 0.10.17
   */
  g_object_class_install_property (gobject_class, PROP_NEXT_FILE,
      g_param_spec_enum ("next-file", "Next File",
          "When to start a new file",
          GST_TYPE_MULTI_FILE_SINK_NEXT, DEFAULT_NEXT_FILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_STATIC_STRINGS));


  /**
   * GstMultiFileSink:max-files
   *
   * Maximum number of files to keep on disk. Once the maximum is reached, old
   * files start to be deleted to make room for new ones.
   *
   * Since: 0.10.31
   */
  g_object_class_install_property (gobject_class, PROP_MAX_FILES,
      g_param_spec_uint ("max-files", "Max files",
          "Maximum number of files to keep on disk. Once the maximum is reached,"
          "old files start to be deleted to make room for new ones.",
          0, G_MAXUINT, DEFAULT_MAX_FILES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_multi_file_sink_finalize;

  gstbasesink_class->get_times = NULL;
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_multi_file_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_multi_file_sink_render);
  gstbasesink_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_multi_file_sink_set_caps);
}

static void
gst_multi_file_sink_init (GstMultiFileSink * multifilesink,
    GstMultiFileSinkClass * g_class)
{
  multifilesink->filename = g_strdup (DEFAULT_LOCATION);
  multifilesink->index = DEFAULT_INDEX;
  multifilesink->post_messages = DEFAULT_POST_MESSAGES;
  multifilesink->max_files = DEFAULT_MAX_FILES;
  multifilesink->files = NULL;
  multifilesink->n_files = 0;

  gst_base_sink_set_sync (GST_BASE_SINK (multifilesink), FALSE);

  multifilesink->next_segment = GST_CLOCK_TIME_NONE;
}

static void
gst_multi_file_sink_finalize (GObject * object)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  g_free (sink->filename);
  g_slist_foreach (sink->files, (GFunc) g_free, NULL);
  g_slist_free (sink->files);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_multi_file_sink_set_location (GstMultiFileSink * sink,
    const gchar * location)
{
  g_free (sink->filename);
  /* FIXME: validate location to have just one %d */
  sink->filename = g_strdup (location);

  return TRUE;
}

static void
gst_multi_file_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_multi_file_sink_set_location (sink, g_value_get_string (value));
      break;
    case PROP_INDEX:
      sink->index = g_value_get_int (value);
      break;
    case PROP_POST_MESSAGES:
      sink->post_messages = g_value_get_boolean (value);
      break;
    case PROP_NEXT_FILE:
      sink->next_file = g_value_get_enum (value);
      break;
    case PROP_MAX_FILES:
      sink->max_files = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_file_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, sink->filename);
      break;
    case PROP_INDEX:
      g_value_set_int (value, sink->index);
      break;
    case PROP_POST_MESSAGES:
      g_value_set_boolean (value, sink->post_messages);
      break;
    case PROP_NEXT_FILE:
      g_value_set_enum (value, sink->next_file);
      break;
    case PROP_MAX_FILES:
      g_value_set_uint (value, sink->max_files);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_multi_file_sink_stop (GstBaseSink * sink)
{
  GstMultiFileSink *multifilesink;
  int i;

  multifilesink = GST_MULTI_FILE_SINK (sink);

  if (multifilesink->file != NULL) {
    fclose (multifilesink->file);
    multifilesink->file = NULL;
  }

  if (multifilesink->streamheaders) {
    for (i = 0; i < multifilesink->n_streamheaders; i++) {
      gst_buffer_unref (multifilesink->streamheaders[i]);
    }
    g_free (multifilesink->streamheaders);
  }

  return TRUE;
}


static void
gst_multi_file_sink_post_message (GstMultiFileSink * multifilesink,
    GstBuffer * buffer, const char *filename)
{
  if (multifilesink->post_messages) {
    GstClockTime duration, timestamp;
    GstClockTime running_time, stream_time;
    guint64 offset, offset_end;
    GstStructure *s;
    GstSegment *segment;
    GstFormat format;

    segment = &GST_BASE_SINK (multifilesink)->segment;
    format = segment->format;

    timestamp = GST_BUFFER_TIMESTAMP (buffer);
    duration = GST_BUFFER_DURATION (buffer);
    offset = GST_BUFFER_OFFSET (buffer);
    offset_end = GST_BUFFER_OFFSET_END (buffer);

    running_time = gst_segment_to_running_time (segment, format, timestamp);
    stream_time = gst_segment_to_stream_time (segment, format, timestamp);

    s = gst_structure_new ("GstMultiFileSink",
        "filename", G_TYPE_STRING, filename,
        "index", G_TYPE_INT, multifilesink->index,
        "timestamp", G_TYPE_UINT64, timestamp,
        "stream-time", G_TYPE_UINT64, stream_time,
        "running-time", G_TYPE_UINT64, running_time,
        "duration", G_TYPE_UINT64, duration,
        "offset", G_TYPE_UINT64, offset,
        "offset-end", G_TYPE_UINT64, offset_end, NULL);

    gst_element_post_message (GST_ELEMENT_CAST (multifilesink),
        gst_message_new_element (GST_OBJECT_CAST (multifilesink), s));
  }
}

static GstFlowReturn
gst_multi_file_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstMultiFileSink *multifilesink;
  guint size;
  guint8 *data;
  gchar *filename;
  gboolean ret;
  GError *error = NULL;

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  multifilesink = GST_MULTI_FILE_SINK (sink);

  switch (multifilesink->next_file) {
    case GST_MULTI_FILE_SINK_NEXT_BUFFER:
      gst_multi_file_sink_ensure_max_files (multifilesink);

      filename = g_strdup_printf (multifilesink->filename,
          multifilesink->index);
      ret = g_file_set_contents (filename, (char *) data, size, &error);
      if (!ret)
        goto write_error;

      multifilesink->files = g_slist_append (multifilesink->files, filename);
      multifilesink->n_files += 1;

      gst_multi_file_sink_post_message (multifilesink, buffer, filename);
      multifilesink->index++;

      break;
    case GST_MULTI_FILE_SINK_NEXT_DISCONT:
      if (GST_BUFFER_IS_DISCONT (buffer)) {
        if (multifilesink->file)
          gst_multi_file_sink_close_file (multifilesink, buffer);
      }

      if (multifilesink->file == NULL) {
        if (!gst_multi_file_sink_open_next_file (multifilesink))
          goto stdio_write_error;
      }

      ret = fwrite (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), 1,
          multifilesink->file);
      if (ret != 1)
        goto stdio_write_error;

      break;
    case GST_MULTI_FILE_SINK_NEXT_KEY_FRAME:
      if (multifilesink->next_segment == GST_CLOCK_TIME_NONE) {
        if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
          multifilesink->next_segment = GST_BUFFER_TIMESTAMP (buffer) +
              10 * GST_SECOND;
        }
      }

      if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
          GST_BUFFER_TIMESTAMP (buffer) >= multifilesink->next_segment &&
          !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
        if (multifilesink->file)
          gst_multi_file_sink_close_file (multifilesink, buffer);

        multifilesink->next_segment += 10 * GST_SECOND;
      }

      if (multifilesink->file == NULL) {
        int i;

        if (!gst_multi_file_sink_open_next_file (multifilesink))
          goto stdio_write_error;

        if (multifilesink->streamheaders) {
          for (i = 0; i < multifilesink->n_streamheaders; i++) {
            ret = fwrite (GST_BUFFER_DATA (multifilesink->streamheaders[i]),
                GST_BUFFER_SIZE (multifilesink->streamheaders[i]), 1,
                multifilesink->file);
            if (ret != 1)
              goto stdio_write_error;
          }
        }
      }

      ret = fwrite (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), 1,
          multifilesink->file);
      if (ret != 1)
        goto stdio_write_error;

      break;
    default:
      g_assert_not_reached ();
  }

  return GST_FLOW_OK;

  /* ERRORS */
write_error:
  {
    switch (error->code) {
      case G_FILE_ERROR_NOSPC:{
        GST_ELEMENT_ERROR (multifilesink, RESOURCE, NO_SPACE_LEFT, (NULL),
            (NULL));
        break;
      }
      default:{
        GST_ELEMENT_ERROR (multifilesink, RESOURCE, WRITE,
            ("Error while writing to file \"%s\".", filename),
            ("%s", g_strerror (errno)));
      }
    }
    g_error_free (error);
    g_free (filename);

    return GST_FLOW_ERROR;
  }
stdio_write_error:
  GST_ELEMENT_ERROR (multifilesink, RESOURCE, WRITE,
      ("Error while writing to file."), (NULL));
  return GST_FLOW_ERROR;
}

static gboolean
gst_multi_file_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstMultiFileSink *multifilesink;
  GstStructure *structure;

  multifilesink = GST_MULTI_FILE_SINK (sink);

  structure = gst_caps_get_structure (caps, 0);
  if (structure) {
    const GValue *value;

    value = gst_structure_get_value (structure, "streamheader");

    if (GST_VALUE_HOLDS_ARRAY (value)) {
      int i;

      if (multifilesink->streamheaders) {
        for (i = 0; i < multifilesink->n_streamheaders; i++) {
          gst_buffer_unref (multifilesink->streamheaders[i]);
        }
        g_free (multifilesink->streamheaders);
      }

      multifilesink->n_streamheaders = gst_value_array_get_size (value);
      multifilesink->streamheaders =
          g_malloc (sizeof (GstBuffer *) * multifilesink->n_streamheaders);

      for (i = 0; i < multifilesink->n_streamheaders; i++) {
        multifilesink->streamheaders[i] =
            gst_buffer_ref (gst_value_get_buffer (gst_value_array_get_value
                (value, i)));
      }
    }
  }

  return TRUE;
}

static void
gst_multi_file_sink_ensure_max_files (GstMultiFileSink * multifilesink)
{
  char *filename;

  while (multifilesink->max_files &&
      multifilesink->n_files >= multifilesink->max_files) {
    filename = multifilesink->files->data;
    g_remove (filename);
    multifilesink->files = g_slist_delete_link (multifilesink->files,
        multifilesink->files);
    multifilesink->n_files -= 1;
  }
}

static gboolean
gst_multi_file_sink_open_next_file (GstMultiFileSink * multifilesink)
{
  char *filename;

  g_return_val_if_fail (multifilesink->file == NULL, FALSE);

  gst_multi_file_sink_ensure_max_files (multifilesink);
  filename = g_strdup_printf (multifilesink->filename, multifilesink->index);
  multifilesink->file = g_fopen (filename, "wb");
  if (multifilesink->file == NULL) {
    g_free (filename);
    return FALSE;
  }

  multifilesink->files = g_slist_append (multifilesink->files, filename);
  multifilesink->n_files += 1;

  return TRUE;
}

static void
gst_multi_file_sink_close_file (GstMultiFileSink * multifilesink,
    GstBuffer * buffer)
{
  char *filename;

  fclose (multifilesink->file);
  multifilesink->file = NULL;

  if (buffer) {
    filename = g_strdup_printf (multifilesink->filename, multifilesink->index);
    gst_multi_file_sink_post_message (multifilesink, buffer, filename);
    g_free (filename);
  }

  multifilesink->index++;
}
