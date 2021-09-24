/* GStreamer
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
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


#include <gst/gst.h>

#include <gst/rtsp-server/rtsp-server.h>

#include "test-onvif-server.h"

GST_DEBUG_CATEGORY_STATIC (onvif_server_debug);
#define GST_CAT_DEFAULT (onvif_server_debug)

#define MAKE_AND_ADD(var, pipe, name, label, elem_name) \
G_STMT_START { \
  if (G_UNLIKELY (!(var = (gst_element_factory_make (name, elem_name))))) { \
    GST_ERROR ("Could not create element %s", name); \
    goto label; \
  } \
  if (G_UNLIKELY (!gst_bin_add (GST_BIN_CAST (pipe), var))) { \
    GST_ERROR ("Could not add element %s", name); \
    goto label; \
  } \
} G_STMT_END

/* This simulates an archive of recordings running from 01-01-1900 to 01-01-2000.
 *
 * This is implemented by repeating the file provided at the command line, with
 * an empty interval of 5 seconds in-between. We intercept relevant events to
 * translate them, and update the timestamps on the output buffers.
 */

#define INTERVAL (5 * GST_SECOND)

/* January the first, 2000 */
#define END_DATE 3155673600 * GST_SECOND

static gchar *filename;

struct _ReplayBin
{
  GstBin parent;

  GstEvent *incoming_seek;
  GstEvent *outgoing_seek;
  GstClockTime trickmode_interval;

  GstSegment segment;
  const GstSegment *incoming_segment;
  gboolean sent_segment;
  GstClockTime ts_offset;
  gint64 remainder;
  GstClockTime min_pts;
};

G_DEFINE_TYPE (ReplayBin, replay_bin, GST_TYPE_BIN);

static void
replay_bin_init (ReplayBin * self)
{
  self->incoming_seek = NULL;
  self->outgoing_seek = NULL;
  self->trickmode_interval = 0;
  self->ts_offset = 0;
  self->sent_segment = FALSE;
  self->min_pts = GST_CLOCK_TIME_NONE;
}

static void
replay_bin_class_init (ReplayBinClass * klass)
{
}

static GstElement *
replay_bin_new (void)
{
  return GST_ELEMENT (g_object_new (replay_bin_get_type (), NULL));
}

static void
demux_pad_added_cb (GstElement * demux, GstPad * pad, GstGhostPad * ghost)
{
  GstCaps *caps = gst_pad_get_current_caps (pad);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (s, "video/x-h264")) {
    gst_ghost_pad_set_target (ghost, pad);
  }

  gst_caps_unref (caps);
}

static void
query_seekable (GstPad * ghost, gint64 * start, gint64 * stop)
{
  GstPad *target;
  GstQuery *query;
  GstFormat format;
  gboolean seekable;

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (ghost));

  query = gst_query_new_seeking (GST_FORMAT_TIME);

  gst_pad_query (target, query);

  gst_query_parse_seeking (query, &format, &seekable, start, stop);

  g_assert (seekable);

  gst_object_unref (target);
}

static GstEvent *
translate_seek (ReplayBin * self, GstPad * pad, GstEvent * ievent)
{
  GstEvent *oevent = NULL;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gint64 istart, istop;         /* Incoming */
  gint64 ustart, ustop;         /* Upstream */
  gint64 ostart, ostop;         /* Outgoing */
  guint32 seqnum = gst_event_get_seqnum (ievent);

  gst_event_parse_seek (ievent, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (!GST_CLOCK_TIME_IS_VALID (stop))
    stop = END_DATE;

  gst_event_parse_seek_trickmode_interval (ievent, &self->trickmode_interval);

  istart = start;
  istop = stop;

  query_seekable (pad, &ustart, &ustop);

  if (rate > 0) {
    /* First, from where we should seek the file */
    ostart = istart % (ustop + INTERVAL);

    /* This may end up in our empty interval */
    if (ostart > ustop) {
      istart += ostart - ustop;
      ostart = 0;
    }

    /* Then, up to where we should seek it */
    ostop = MIN (ustop, ostart + (istop - istart));
  } else {
    /* First up to where we should seek the file */
    ostop = istop % (ustop + INTERVAL);

    /* This may end up in our empty interval */
    if (ostop > ustop) {
      istop -= ostop - ustop;
      ostop = ustop;
    }

    ostart = MAX (0, ostop - (istop - istart));
  }

  /* We may be left with nothing to actually play, in this
   * case we won't seek upstream, and emit the expected events
   * ourselves */
  if (istart > istop) {
    GstSegment segment;
    GstEvent *event;
    gboolean update;

    event = gst_event_new_flush_start ();
    gst_event_set_seqnum (event, seqnum);
    gst_pad_push_event (pad, event);

    event = gst_event_new_flush_stop (TRUE);
    gst_event_set_seqnum (event, seqnum);
    gst_pad_push_event (pad, event);

    gst_segment_init (&segment, format);
    gst_segment_do_seek (&segment, rate, format, flags, start_type, start,
        stop_type, stop, &update);

    event = gst_event_new_segment (&segment);
    gst_event_set_seqnum (event, seqnum);
    gst_pad_push_event (pad, event);

    event = gst_event_new_eos ();
    gst_event_set_seqnum (event, seqnum);
    gst_pad_push_event (pad, event);

    goto done;
  }

  /* Lastly, how much will remain to play back (this remainder includes the interval) */
  if (stop - start > ostop - ostart)
    self->remainder = (stop - start) - (ostop - ostart);

  flags |= GST_SEEK_FLAG_SEGMENT;

  oevent =
      gst_event_new_seek (rate, format, flags, start_type, ostart, stop_type,
      ostop);
  gst_event_set_seek_trickmode_interval (oevent, self->trickmode_interval);
  gst_event_set_seqnum (oevent, seqnum);

  GST_DEBUG ("Translated event to %" GST_PTR_FORMAT
      " (remainder: %" G_GINT64_FORMAT ")", oevent, self->remainder);

done:
  return oevent;
}

static gboolean
replay_bin_event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  ReplayBin *self = REPLAY_BIN (parent);
  gboolean ret = TRUE;
  gboolean forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GST_DEBUG ("Processing seek event %" GST_PTR_FORMAT, event);

      self->incoming_seek = event;

      gst_event_replace (&self->outgoing_seek, NULL);
      self->sent_segment = FALSE;

      event = translate_seek (self, pad, event);

      if (!event)
        forward = FALSE;
      else
        self->outgoing_seek = gst_event_ref (event);
      break;
    }
    default:
      break;
  }

  if (forward)
    return gst_pad_event_default (pad, parent, event);
  else
    return ret;
}

static gboolean
replay_bin_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  ReplayBin *self = REPLAY_BIN (parent);
  gboolean ret = TRUE;
  gboolean forward = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SEEKING:
      /* We are seekable from the beginning till the end of time */
      gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0,
          GST_CLOCK_TIME_NONE);
      forward = FALSE;
      break;
    case GST_QUERY_SEGMENT:
      gst_query_set_segment (query, self->segment.rate, self->segment.format,
          self->segment.start, self->segment.stop);
      forward = FALSE;
    default:
      break;
  }

  GST_DEBUG ("Processed query %" GST_PTR_FORMAT, query);

  if (forward)
    return gst_pad_query_default (pad, parent, query);
  else
    return ret;
}

static GstEvent *
translate_segment (GstPad * pad, GstEvent * ievent)
{
  ReplayBin *self = REPLAY_BIN (GST_OBJECT_PARENT (pad));
  GstEvent *ret;
  gdouble irate, orate;
  GstFormat iformat, oformat;
  GstSeekFlags iflags, oflags;
  GstSeekType istart_type, ostart_type, istop_type, ostop_type;
  gint64 istart, ostart, istop, ostop;
  gboolean update;

  gst_event_parse_segment (ievent, &self->incoming_segment);

  if (!self->outgoing_seek) {
    GstSegment segment;
    gboolean update;

    gst_segment_init (&segment, GST_FORMAT_TIME);

    gst_segment_do_seek (&segment, 1.0, GST_FORMAT_TIME, 0, GST_SEEK_TYPE_SET,
        0, GST_SEEK_TYPE_SET, END_DATE, &update);

    ret = gst_event_new_segment (&segment);
    gst_event_unref (ievent);
    goto done;
  }

  if (!self->sent_segment) {
    gst_event_parse_seek (self->incoming_seek, &irate, &iformat, &iflags,
        &istart_type, &istart, &istop_type, &istop);
    gst_event_parse_seek (self->outgoing_seek, &orate, &oformat, &oflags,
        &ostart_type, &ostart, &ostop_type, &ostop);

    if (istop == -1)
      istop = END_DATE;

    if (self->incoming_segment->rate > 0)
      self->ts_offset = istart - ostart;
    else
      self->ts_offset = istop - ostop;

    istart += self->incoming_segment->start - ostart;
    istop += self->incoming_segment->stop - ostop;

    gst_segment_init (&self->segment, self->incoming_segment->format);

    gst_segment_do_seek (&self->segment, self->incoming_segment->rate,
        self->incoming_segment->format,
        (GstSeekFlags) self->incoming_segment->flags, GST_SEEK_TYPE_SET,
        (guint64) istart, GST_SEEK_TYPE_SET, (guint64) istop, &update);

    self->min_pts = istart;

    ret = gst_event_new_segment (&self->segment);

    self->sent_segment = TRUE;

    gst_event_unref (ievent);

    GST_DEBUG ("Translated segment: %" GST_PTR_FORMAT ", "
        "ts_offset: %" G_GUINT64_FORMAT, ret, self->ts_offset);
  } else {
    ret = NULL;
  }

done:
  return ret;
}

static void
handle_segment_done (ReplayBin * self, GstPad * pad)
{
  GstEvent *event;

  if (self->remainder < INTERVAL) {
    self->remainder = 0;
    event = gst_event_new_eos ();
    gst_event_set_seqnum (event, gst_event_get_seqnum (self->incoming_seek));
    gst_pad_push_event (pad, event);
  } else {
    gint64 ustart, ustop;
    gint64 ostart, ostop;
    GstPad *target;
    GstStructure *s;

    /* Signify the end of a contiguous section of recording */
    s = gst_structure_new ("GstNtpOffset",
        "ntp-offset", G_TYPE_UINT64, 0, "discont", G_TYPE_BOOLEAN, TRUE, NULL);

    event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

    gst_pad_push_event (pad, event);

    query_seekable (pad, &ustart, &ustop);

    self->remainder -= INTERVAL;

    if (self->incoming_segment->rate > 0) {
      ostart = 0;
      ostop = MIN (ustop, self->remainder);
    } else {
      ostart = MAX (ustop - self->remainder, 0);
      ostop = ustop;
    }

    self->remainder = MAX (self->remainder - ostop - ostart, 0);

    event =
        gst_event_new_seek (self->segment.rate, self->segment.format,
        self->segment.flags & ~GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, ostart,
        GST_SEEK_TYPE_SET, ostop);
    gst_event_set_seek_trickmode_interval (event, self->trickmode_interval);

    if (self->incoming_segment->rate > 0)
      self->ts_offset += INTERVAL + ustop;
    else
      self->ts_offset -= INTERVAL + ustop;

    GST_DEBUG ("New offset: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (self->ts_offset));

    GST_DEBUG ("Seeking to %" GST_PTR_FORMAT, event);
    target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
    gst_pad_send_event (target, event);
    gst_object_unref (target);
  }
}

static GstPadProbeReturn
replay_bin_event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer unused)
{
  ReplayBin *self = REPLAY_BIN (GST_OBJECT_PARENT (pad));
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  GST_DEBUG ("Probed %" GST_PTR_FORMAT, info->data);

  switch (GST_EVENT_TYPE (info->data)) {
    case GST_EVENT_SEGMENT:
    {
      GstEvent *translated;

      GST_DEBUG ("Probed segment %" GST_PTR_FORMAT, info->data);

      translated = translate_segment (pad, GST_EVENT (info->data));
      if (translated)
        info->data = translated;
      else
        ret = GST_PAD_PROBE_HANDLED;

      break;
    }
    case GST_EVENT_SEGMENT_DONE:
    {
      handle_segment_done (self, pad);
      ret = GST_PAD_PROBE_HANDLED;
      break;
    }
    default:
      break;
  }

  return ret;
}

static GstPadProbeReturn
replay_bin_buffer_probe (GstPad * pad, GstPadProbeInfo * info, gpointer unused)
{
  ReplayBin *self = REPLAY_BIN (GST_OBJECT_PARENT (pad));
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (GST_BUFFER_PTS (info->data) > self->incoming_segment->stop) {
    ret = GST_PAD_PROBE_DROP;
    goto done;
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (info->data)))
    GST_BUFFER_PTS (info->data) += self->ts_offset;
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (info->data)))
    GST_BUFFER_DTS (info->data) += self->ts_offset;

  GST_LOG ("Pushing buffer %" GST_PTR_FORMAT, info->data);

done:
  return ret;
}

static GstElement *
create_replay_bin (GstElement * parent)
{
  GstElement *ret, *src, *demux;
  GstPad *ghost;

  ret = replay_bin_new ();
  if (!gst_bin_add (GST_BIN (parent), ret)) {
    gst_object_unref (ret);
    goto fail;
  }

  MAKE_AND_ADD (src, ret, "filesrc", fail, NULL);
  MAKE_AND_ADD (demux, ret, "qtdemux", fail, NULL);

  ghost = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  gst_element_add_pad (ret, ghost);

  gst_pad_set_event_function (ghost, replay_bin_event_func);
  gst_pad_add_probe (ghost, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      replay_bin_event_probe, NULL, NULL);
  gst_pad_add_probe (ghost, GST_PAD_PROBE_TYPE_BUFFER, replay_bin_buffer_probe,
      NULL, NULL);
  gst_pad_set_query_function (ghost, replay_bin_query_func);

  if (!gst_element_link (src, demux))
    goto fail;

  g_object_set (src, "location", filename, NULL);
  g_signal_connect (demux, "pad-added", G_CALLBACK (demux_pad_added_cb), ghost);

done:
  return ret;

fail:
  ret = NULL;
  goto done;
}

/* A simple factory to set up our replay bin */

struct _OnvifFactory
{
  GstRTSPOnvifMediaFactory parent;
};

G_DEFINE_TYPE (OnvifFactory, onvif_factory, GST_TYPE_RTSP_MEDIA_FACTORY);

static void
onvif_factory_init (OnvifFactory * factory)
{
}

static GstElement *
onvif_factory_create_element (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstElement *replay_bin, *q1, *parse, *pay, *onvifts, *q2;
  GstElement *ret = gst_bin_new (NULL);
  GstElement *pbin = gst_bin_new ("pay0");
  GstPad *sinkpad, *srcpad;

  if (!(replay_bin = create_replay_bin (ret)))
    goto fail;

  MAKE_AND_ADD (q1, pbin, "queue", fail, NULL);
  MAKE_AND_ADD (parse, pbin, "h264parse", fail, NULL);
  MAKE_AND_ADD (pay, pbin, "rtph264pay", fail, NULL);
  MAKE_AND_ADD (onvifts, pbin, "rtponviftimestamp", fail, NULL);
  MAKE_AND_ADD (q2, pbin, "queue", fail, NULL);

  gst_bin_add (GST_BIN (ret), pbin);

  if (!gst_element_link_many (q1, parse, pay, onvifts, q2, NULL))
    goto fail;

  sinkpad = gst_element_get_static_pad (q1, "sink");
  gst_element_add_pad (pbin, gst_ghost_pad_new ("sink", sinkpad));
  gst_object_unref (sinkpad);

  if (!gst_element_link (replay_bin, pbin))
    goto fail;

  srcpad = gst_element_get_static_pad (q2, "src");
  gst_element_add_pad (pbin, gst_ghost_pad_new ("src", srcpad));
  gst_object_unref (srcpad);

  g_object_set (onvifts, "set-t-bit", TRUE, "set-e-bit", TRUE, "ntp-offset",
      G_GUINT64_CONSTANT (0), "drop-out-of-segment", FALSE, NULL);

  gst_element_set_clock (onvifts, gst_system_clock_obtain ());

done:
  return ret;

fail:
  gst_object_unref (ret);
  ret = NULL;
  goto done;
}

static void
onvif_factory_class_init (OnvifFactoryClass * klass)
{
  GstRTSPMediaFactoryClass *mf_class = GST_RTSP_MEDIA_FACTORY_CLASS (klass);

  mf_class->create_element = onvif_factory_create_element;
}

static GstRTSPMediaFactory *
onvif_factory_new (void)
{
  GstRTSPMediaFactory *result;

  result =
      GST_RTSP_MEDIA_FACTORY (g_object_new (onvif_factory_get_type (), NULL));

  return result;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  GOptionContext *optctx;
  GError *error = NULL;
  gchar *service;

  optctx = g_option_context_new ("<filename.mp4> - ONVIF RTSP Server, MP4");
  g_option_context_add_group (optctx, gst_init_get_option_group ());
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s\n", error->message);
    g_option_context_free (optctx);
    g_clear_error (&error);
    return -1;
  }
  if (argc < 2) {
    g_print ("%s\n", g_option_context_get_help (optctx, TRUE, NULL));
    return 1;
  }
  filename = argv[1];
  g_option_context_free (optctx);

  GST_DEBUG_CATEGORY_INIT (onvif_server_debug, "onvif-server", 0,
      "ONVIF server");

  loop = g_main_loop_new (NULL, FALSE);

  server = gst_rtsp_onvif_server_new ();

  mounts = gst_rtsp_server_get_mount_points (server);

  factory = onvif_factory_new ();
  gst_rtsp_media_factory_set_media_gtype (factory, GST_TYPE_RTSP_ONVIF_MEDIA);

  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  g_object_unref (mounts);

  gst_rtsp_server_attach (server, NULL);

  service = gst_rtsp_server_get_service (server);
  g_print ("stream ready at rtsp://127.0.0.1:%s/test\n", service);
  g_free (service);
  g_main_loop_run (loop);

  return 0;
}
