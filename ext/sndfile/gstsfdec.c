/* GStreamer libsndfile plugin
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
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
 * License along with self library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include "gstsfdec.h"
#include <gst/audio/audio.h>

enum
{
  PROP_0,
  PROP_LOCATION
};

#define FORMATS \
    "{ "GST_AUDIO_NE (F32)", "GST_AUDIO_NE (S32)", "GST_AUDIO_NE (S16)" }"

static GstStaticPadTemplate sf_dec_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " FORMATS ", "
        "layout = (string) interleaved, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]"));

GST_DEBUG_CATEGORY_STATIC (gst_sf_dec_debug);
#define GST_CAT_DEFAULT gst_sf_dec_debug

#define DEFAULT_BUFFER_FRAMES	(256)

static gboolean gst_sf_dec_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstStateChangeReturn gst_sf_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_sf_dec_sink_activate (GstPad * pad, GstObject * parent);
static gboolean gst_sf_dec_sink_activate_mode (GstPad * sinkpad,
    GstObject * parent, GstPadMode mode, gboolean active);
static void gst_sf_dec_loop (GstPad * pad);

static gboolean gst_sf_dec_start (GstSFDec * bsrc);
static gboolean gst_sf_dec_stop (GstSFDec * bsrc);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_sf_dec_debug, "sfdec", 0, "sfdec element");
#define gst_sf_dec_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSFDec, gst_sf_dec, GST_TYPE_ELEMENT, _do_init);

/* sf virtual io */

static sf_count_t
gst_sf_vio_get_filelen (void *user_data)
{
  GstSFDec *self = GST_SF_DEC (user_data);
  gint64 dur;

  if (gst_pad_peer_query_duration (self->sinkpad, GST_FORMAT_BYTES, &dur)) {
    return (sf_count_t) dur;
  }
  GST_WARNING_OBJECT (self, "query_duration failed");
  return -1;
}

static sf_count_t
gst_sf_vio_tell (void *user_data)
{
  GstSFDec *self = GST_SF_DEC (user_data);
  return self->pos;
}

static sf_count_t
gst_sf_vio_seek (sf_count_t offset, int whence, void *user_data)
{
  GstSFDec *self = GST_SF_DEC (user_data);

  switch (whence) {
    case SEEK_CUR:
      self->pos += offset;
      break;
    case SEEK_SET:
      self->pos = offset;
      break;
    case SEEK_END:
      self->pos = gst_sf_vio_get_filelen (user_data) - offset;
      break;
  }
  return (sf_count_t) self->pos;
}

static sf_count_t
gst_sf_vio_read (void *ptr, sf_count_t count, void *user_data)
{
  GstSFDec *self = GST_SF_DEC (user_data);
  GstBuffer *buffer = gst_buffer_new_wrapped_full (0, ptr, count, 0, count,
      ptr, NULL);

  if (gst_pad_pull_range (self->sinkpad, self->pos, count, &buffer) ==
      GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self, "read %d bytes @ pos %" G_GUINT64_FORMAT,
        (gint) count, self->pos);
    self->pos += count;
    return count;
  }
  GST_WARNING_OBJECT (self, "read failed");
  return 0;
}

static sf_count_t
gst_sf_vio_write (const void *ptr, sf_count_t count, void *user_data)
{
  GstSFDec *self = GST_SF_DEC (user_data);
  GstBuffer *buffer = gst_buffer_new_wrapped (g_memdup (ptr, count), count);

  if (gst_pad_push (self->srcpad, buffer) == GST_FLOW_OK) {
    return count;
  }
  GST_WARNING_OBJECT (self, "write failed");
  return 0;
}

SF_VIRTUAL_IO gst_sf_vio = {
  &gst_sf_vio_get_filelen,
  &gst_sf_vio_seek,
  &gst_sf_vio_read,
  &gst_sf_vio_write,
  &gst_sf_vio_tell,
};


static void
gst_sf_dec_class_init (GstSFDecClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_sf_dec_change_state);

  gst_element_class_set_static_metadata (gstelement_class, "Sndfile decoder",
      "Demuxer/Audio",
      "Read audio streams using libsndfile",
      "Stefan Sauer <ensonic@user.sf.net>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sf_dec_src_factory));

  gst_element_class_add_pad_template (gstelement_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_sf_create_audio_template_caps ()));

}

static void
gst_sf_dec_init (GstSFDec * self)
{
  self->sinkpad = gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (self), "sink"), "sink");
  gst_pad_set_activate_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sf_dec_sink_activate));
  gst_pad_set_activatemode_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sf_dec_sink_activate_mode));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&sf_dec_src_factory, "src");
  /* TODO(ensonic): event function */
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_sf_dec_src_query));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static gboolean
gst_sf_dec_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstSFDec *self = GST_SF_DEC (parent);
  GstFormat format;
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (self, "query %s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      if (!self->file)
        goto done;
      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        gst_query_set_duration (query, format,
            gst_util_uint64_scale_int (self->duration, GST_SECOND, self->rate));
        res = TRUE;
      }
      break;
    case GST_QUERY_POSITION:
      if (!self->file)
        goto done;
      gst_query_parse_position (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        gst_query_set_position (query, format,
            gst_util_uint64_scale_int (self->pos, GST_SECOND, self->rate));
        res = TRUE;
      }
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

done:
  GST_DEBUG_OBJECT (self, "query %s: %d", GST_QUERY_TYPE_NAME (query), res);
  return res;
}

static GstStateChangeReturn
gst_sf_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSFDec *self = GST_SF_DEC (element);

  GST_INFO_OBJECT (self, "transition: %s -> %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_sf_dec_start (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_sf_dec_stop (self);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_sf_dec_start (GstSFDec * self)
{
  return TRUE;
}

static gboolean
gst_sf_dec_stop (GstSFDec * self)
{
  int err = 0;

  g_return_val_if_fail (self->file != NULL, FALSE);

  GST_INFO_OBJECT (self, "Closing sndfile stream");

  if ((err = sf_close (self->file)))
    goto close_failed;

  self->file = NULL;
  self->offset = 0;
  self->channels = 0;
  self->rate = 0;

  self->pos = 0;
  self->duration = 0;

  return TRUE;

close_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, CLOSE,
        ("Could not close sndfile stream."),
        ("soundfile error: %s", sf_error_number (err)));
    return FALSE;
  }
}

static gboolean
gst_sf_dec_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
  gst_query_unref (query);

  if (!pull_mode)
    goto activate_push;

  GST_DEBUG_OBJECT (sinkpad, "activating pull");
  return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PULL, TRUE);

activate_push:
  {
    GST_DEBUG_OBJECT (sinkpad, "activating push");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, TRUE);
  }
}

static gboolean
gst_sf_dec_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      res = FALSE;              /* no push support */
      break;
    case GST_PAD_MODE_PULL:
      if (active) {
        /* if we have a scheduler we can start the task */
        GST_DEBUG_OBJECT (sinkpad, "start task");
        res = gst_pad_start_task (sinkpad, (GstTaskFunction) gst_sf_dec_loop,
            sinkpad, NULL);
      } else {
        res = gst_pad_stop_task (sinkpad);
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_sf_dec_open_file (GstSFDec * self)
{
  SF_INFO info = { 0, };
  GstCaps *caps;
  GstStructure *s;
  GstSegment seg;
  gint width;
  const gchar *format;
  gchar *stream_id;

  GST_DEBUG_OBJECT (self, "opening the stream");
  if (!(self->file = sf_open_virtual (&gst_sf_vio, SFM_READ, &info, self)))
    goto open_failed;

  stream_id =
      gst_pad_create_stream_id (self->srcpad, GST_ELEMENT_CAST (self), NULL);
  gst_pad_push_event (self->srcpad, gst_event_new_stream_start (stream_id));
  g_free (stream_id);

  self->channels = info.channels;
  self->rate = info.samplerate;
  self->duration = info.frames;
  /* TODO(ensonic): do something with info.seekable? */

  /* negotiate srcpad caps */
  if ((caps = gst_pad_get_allowed_caps (self->srcpad)) == NULL) {
    caps = gst_pad_get_pad_template_caps (self->srcpad);
  }
  caps = gst_caps_make_writable (caps);
  GST_DEBUG_OBJECT (self, "allowed caps %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);
  gst_structure_set (s,
      "channels", G_TYPE_INT, self->channels,
      "rate", G_TYPE_INT, self->rate, NULL);

  if (!gst_structure_fixate_field_string (s, "format", GST_AUDIO_NE (S16)))
    GST_WARNING_OBJECT (self, "Failed to fixate format to S16NE");

  caps = gst_caps_fixate (caps);

  GST_DEBUG_OBJECT (self, "fixated caps %" GST_PTR_FORMAT, caps);

  /* configure to output the negotiated format */
  s = gst_caps_get_structure (caps, 0);
  format = gst_structure_get_string (s, "format");
  if (g_str_equal (format, GST_AUDIO_NE (S32))) {
    self->reader = (GstSFReader) sf_readf_int;
    width = 32;
  } else if (g_str_equal (format, GST_AUDIO_NE (S16))) {
    self->reader = (GstSFReader) sf_readf_short;
    width = 16;
  } else {
    self->reader = (GstSFReader) sf_readf_float;
    width = 32;
  }
  self->bytes_per_frame = width * self->channels / 8;

  gst_pad_set_caps (self->srcpad, caps);
  gst_caps_unref (caps);

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale_int (self->duration, GST_SECOND, self->rate);
  gst_pad_push_event (self->srcpad, gst_event_new_segment (&seg));

  return TRUE;

open_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        (_("Could not open sndfile stream for reading.")),
        ("soundfile error: %s", sf_strerror (NULL)));
    return FALSE;
  }
}

static void
gst_sf_dec_loop (GstPad * pad)
{
  GstSFDec *self = GST_SF_DEC (GST_PAD_PARENT (pad));
  GstBuffer *buf;
  GstMapInfo map;
  GstFlowReturn flow;
  sf_count_t bytes_read;
  guint num_frames = 1024;      /* arbitrary */

  if (G_UNLIKELY (!self->file)) {
    /* not started yet */
    if (!gst_sf_dec_open_file (self))
      goto pause;
  }

  buf = gst_buffer_new_and_alloc (self->bytes_per_frame * num_frames);
  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  bytes_read = self->reader (self->file, map.data, num_frames);
  GST_DEBUG_OBJECT (self, "read %d / %d bytes = %d frames of audio",
      (gint) bytes_read, (gint) map.size, num_frames);
  gst_buffer_unmap (buf, &map);

  if (G_UNLIKELY (bytes_read < 0))
    goto could_not_read;

  if (G_UNLIKELY (bytes_read == 0))
    goto eos;

  num_frames = bytes_read / self->bytes_per_frame;

  GST_BUFFER_OFFSET (buf) = self->offset;
  GST_BUFFER_TIMESTAMP (buf) = gst_util_uint64_scale_int (self->offset,
      GST_SECOND, self->rate);
  self->offset += num_frames;
  GST_BUFFER_DURATION (buf) = gst_util_uint64_scale_int (self->offset,
      GST_SECOND, self->rate) - GST_BUFFER_TIMESTAMP (buf);

  flow = gst_pad_push (self->srcpad, buf);
  if (flow != GST_FLOW_OK) {
    GST_LOG_OBJECT (self, "pad push flow: %s", gst_flow_get_name (flow));
    goto pause;
  }

  return;

  /* ERROR */
could_not_read:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    gst_buffer_unref (buf);
    goto pause;
  }
eos:
  {
    GST_DEBUG_OBJECT (self, "EOS");
    gst_buffer_unref (buf);
    gst_pad_push_event (self->srcpad, gst_event_new_eos ());
    goto pause;
  }
pause:
  {
    GST_INFO_OBJECT (self, "Pausing");
    gst_pad_pause_task (self->sinkpad);
  }
}
