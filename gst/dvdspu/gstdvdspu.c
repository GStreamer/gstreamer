/* GStreamer DVD Sub-Picture Unit
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
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
/*
 * SECTION:element-dvdspu
 *
 * <refsect2>
 * <para>
 * DVD sub picture overlay element.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * FIXME: gst-launch ...
 * </programlisting>
 * FIXME: description for the sample launch pipeline
 * </para>
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstdvdspu.h"

extern void gstgst_dvdspu_render_spu (GstDVDSpu * dvdspu, GstBuffer * buf);

GST_DEBUG_CATEGORY (gst_dvdspu_debug);
#define GST_CAT_DEFAULT gst_dvdspu_debug

/* Convert an STM offset in the SPU sequence to a GStreamer timestamp */
#define STM_TO_GST(stm) ((GST_MSECOND * 1024 * (stm)) / 90)

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, " "format = (fourcc) { I420 }, "
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ]")
    /* FIXME: Can support YV12 one day too */
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, " "format = (fourcc) { I420 }, "
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ]")
    /* FIXME: Can support YV12 one day too */
    );

static GstStaticPadTemplate subpic_sink_factory =
GST_STATIC_PAD_TEMPLATE ("subpicture",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dvd-subpicture")
    );

GST_BOILERPLATE (GstDVDSpu, gst_dvdspu, GstElement, GST_TYPE_ELEMENT);

static void gst_dvdspu_dispose (GObject * object);
static void gst_dvdspu_finalize (GObject * object);
static GstStateChangeReturn gst_dvdspu_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_dvdspu_src_event (GstPad * pad, GstEvent * event);

static GstCaps *gst_dvdspu_video_proxy_getcaps (GstPad * pad);
static gboolean gst_dvdspu_video_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_dvdspu_video_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_dvdspu_video_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_dvdspu_buffer_alloc (GstPad * sinkpad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static void gst_dvdspu_redraw_still (GstDVDSpu * dvdspu);

static void gst_dvdspu_check_still_updates (GstDVDSpu * dvdspu);
static GstFlowReturn gst_dvdspu_subpic_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_dvdspu_subpic_event (GstPad * pad, GstEvent * event);

static void gst_dvdspu_clear (GstDVDSpu * dvdspu);
static void gst_dvdspu_flush_spu_info (GstDVDSpu * dvdspu);
static void gst_dvdspu_advance_spu (GstDVDSpu * dvdspu, GstClockTime new_ts);
static GstFlowReturn
dvspu_handle_vid_buffer (GstDVDSpu * dvdspu, GstBuffer * buf);

static void
gst_dvdspu_base_init (gpointer gclass)
{
  static GstElementDetails element_details =
      GST_ELEMENT_DETAILS ("Fluendo DVD Player Sub-picture Overlay",
      "Mixer/Video/Overlay/DVD",
      "Parses the DVD Sub-Picture command stream and renders the SPU overlay "
      "onto the video as it passes through",
      "Jan Schmidt <jan@fluendo.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subpic_sink_factory));
  gst_element_class_set_details (element_class, &element_details);

  element_class->change_state = gst_dvdspu_change_state;
}

static void
gst_dvdspu_class_init (GstDVDSpuClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = (GObjectFinalizeFunc) gst_dvdspu_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_dvdspu_finalize;
}

static void
gst_dvdspu_init (GstDVDSpu * dvdspu, GstDVDSpuClass * gclass)
{
  dvdspu->videosinkpad =
      gst_pad_new_from_static_template (&video_sink_factory, "video");
  gst_pad_set_setcaps_function (dvdspu->videosinkpad,
      gst_dvdspu_video_set_caps);
  gst_pad_set_getcaps_function (dvdspu->videosinkpad,
      gst_dvdspu_video_proxy_getcaps);
  gst_pad_set_chain_function (dvdspu->videosinkpad, gst_dvdspu_video_chain);
  gst_pad_set_event_function (dvdspu->videosinkpad, gst_dvdspu_video_event);
  gst_pad_set_bufferalloc_function (dvdspu->videosinkpad,
      gst_dvdspu_buffer_alloc);

  dvdspu->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (dvdspu->srcpad, gst_dvdspu_src_event);
  gst_pad_set_getcaps_function (dvdspu->srcpad, gst_dvdspu_video_proxy_getcaps);

  dvdspu->subpic_sinkpad =
      gst_pad_new_from_static_template (&subpic_sink_factory, "subpicture");
  gst_pad_set_chain_function (dvdspu->subpic_sinkpad, gst_dvdspu_subpic_chain);
  gst_pad_set_event_function (dvdspu->subpic_sinkpad, gst_dvdspu_subpic_event);
  gst_pad_use_fixed_caps (dvdspu->subpic_sinkpad);

  gst_element_add_pad (GST_ELEMENT (dvdspu), dvdspu->videosinkpad);
  gst_element_add_pad (GST_ELEMENT (dvdspu), dvdspu->subpic_sinkpad);
  gst_element_add_pad (GST_ELEMENT (dvdspu), dvdspu->srcpad);

  dvdspu->spu_lock = g_mutex_new ();
  dvdspu->pending_spus = g_queue_new ();

  gst_dvdspu_clear (dvdspu);
}

static void
gst_dvdspu_clear (GstDVDSpu * dvdspu)
{
  gst_dvdspu_flush_spu_info (dvdspu);

  gst_buffer_replace (&dvdspu->ref_frame, NULL);
  gst_buffer_replace (&dvdspu->pending_frame, NULL);

  dvdspu->spu_state.fps_n = 25;
  dvdspu->spu_state.fps_d = 1;

  gst_segment_init (&dvdspu->video_seg, GST_FORMAT_UNDEFINED);
}

static void
gst_dvdspu_dispose (GObject * object)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (object);

  /* need to hold the SPU lock in case other stuff is still running... */
  DVD_SPU_LOCK (dvdspu);
  gst_dvdspu_clear (dvdspu);
  DVD_SPU_UNLOCK (dvdspu);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dvdspu_finalize (GObject * object)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (object);
  gint i;

  for (i = 0; i < 3; i++) {
    if (dvdspu->spu_state.comp_bufs[i] != NULL) {
      g_free (dvdspu->spu_state.comp_bufs[i]);
      dvdspu->spu_state.comp_bufs[i] = NULL;
    }
  }
  g_queue_free (dvdspu->pending_spus);
  g_mutex_free (dvdspu->spu_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* With SPU lock held, clear the queue of SPU packets */
static void
gst_dvdspu_flush_spu_info (GstDVDSpu * dvdspu)
{
  SpuPacket *packet;
  SpuState *state = &dvdspu->spu_state;

  GST_INFO_OBJECT (dvdspu, "Flushing SPU information");

  gst_segment_init (&dvdspu->subp_seg, GST_FORMAT_UNDEFINED);

  if (dvdspu->partial_spu) {
    gst_buffer_unref (dvdspu->partial_spu);
    dvdspu->partial_spu = NULL;
  }

  packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);
  while (packet != NULL) {
    if (packet->buf)
      gst_buffer_unref (packet->buf);
    if (packet->event)
      gst_event_unref (packet->event);
    g_free (packet);
    packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);
  }

  if (state->buf) {
    gst_buffer_unref (state->buf);
    state->buf = NULL;
  }
  if (state->pix_buf) {
    gst_buffer_unref (state->pix_buf);
    state->pix_buf = NULL;
  }

  state->base_ts = state->next_ts = GST_CLOCK_TIME_NONE;
  state->flags &= ~(SPU_STATE_FLAGS_MASK);
  state->pix_data[0] = 0;
  state->pix_data[1] = 0;

  state->hl_rect.top = -1;
  state->hl_rect.bottom = -1;

  state->disp_rect.top = -1;
  state->disp_rect.bottom = -1;

  state->n_line_ctrl_i = 0;
  if (state->line_ctrl_i != NULL) {
    g_free (state->line_ctrl_i);
    state->line_ctrl_i = NULL;
  }
}

/* Proxy buffer allocations on the video sink pad downstream */
static GstFlowReturn
gst_dvdspu_buffer_alloc (GstPad * sinkpad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (gst_pad_get_parent (sinkpad));
  GstFlowReturn ret = GST_FLOW_OK;

  ret = gst_pad_alloc_buffer (dvdspu->srcpad, offset, size, caps, buf);

  gst_object_unref (dvdspu);

  return ret;
}

static gboolean
gst_dvdspu_src_event (GstPad * pad, GstEvent * event)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (gst_pad_get_parent (pad));
  GstPad *peer;
  gboolean res = TRUE;

  peer = gst_pad_get_peer (dvdspu->videosinkpad);
  if (peer) {
    res = gst_pad_send_event (peer, event);
    gst_object_unref (peer);
  }

  gst_object_unref (dvdspu);
  return res;
}

static gboolean
gst_dvdspu_video_set_caps (GstPad * pad, GstCaps * caps)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (gst_pad_get_parent (pad));
  gboolean res = FALSE;
  GstStructure *s;
  gint w, h;
  gint i;
  gint fps_n, fps_d;
  SpuState *state;

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "width", &w) ||
      !gst_structure_get_int (s, "height", &h) ||
      !gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d)) {
    goto done;
  }

  DVD_SPU_LOCK (dvdspu);

  state = &dvdspu->spu_state;

  state->fps_n = fps_n;
  state->fps_d = fps_d;

  state->vid_height = h;
  state->Y_height = GST_ROUND_UP_2 (h);
  state->UV_height = state->Y_height / 2;

  if (state->vid_width != w) {
    state->vid_width = w;
    state->Y_stride = GST_ROUND_UP_4 (w);
    state->UV_stride = GST_ROUND_UP_4 (state->Y_stride / 2);
    for (i = 0; i < 3; i++) {
      state->comp_bufs[i] = g_realloc (state->comp_bufs[i],
          sizeof (guint16) * state->UV_stride);
    }
  }
  DVD_SPU_UNLOCK (dvdspu);

  res = TRUE;
done:
  gst_object_unref (dvdspu);
  return res;
}

static GstCaps *
gst_dvdspu_video_proxy_getcaps (GstPad * pad)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (gst_pad_get_parent (pad));
  GstCaps *caps;
  GstPad *otherpad;

  /* Proxy the getcaps between videosink and the srcpad, ignoring the 
   * subpicture sink pad */
  otherpad = (pad == dvdspu->srcpad) ? dvdspu->videosinkpad : dvdspu->srcpad;

  caps = gst_pad_peer_get_caps (otherpad);
  if (caps) {
    GstCaps *temp;
    const GstCaps *templ;

    templ = gst_pad_get_pad_template_caps (otherpad);
    temp = gst_caps_intersect (caps, templ);
    gst_caps_unref (caps);
    caps = temp;
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  gst_object_unref (dvdspu);
  return caps;
}

static gboolean
gst_dvdspu_video_event (GstPad * pad, GstEvent * event)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) (gst_object_get_parent (GST_OBJECT (pad)));
  SpuState *state = &dvdspu->spu_state;
  gboolean res = TRUE;

  g_return_val_if_fail (dvdspu != NULL, FALSE);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      const GstStructure *structure = gst_event_get_structure (event);
      const char *event_type;

      if (!gst_structure_has_name (structure, "application/x-gst-dvd")) {
        res = gst_pad_event_default (pad, event);
        break;
      }

      event_type = gst_structure_get_string (structure, "event");
      GST_DEBUG_OBJECT (dvdspu,
          "DVD event of type %s on video pad", event_type);

      if (strcmp (event_type, "dvd-still") == 0) {
        gboolean in_still;

        if (gst_structure_get_boolean (structure, "still-state", &in_still)) {
          DVD_SPU_LOCK (dvdspu);
          if (in_still) {
            state->flags |= SPU_STATE_STILL_FRAME;
            /* Entering still. Advance the SPU to make sure the state is 
             * up to date */
            gst_dvdspu_check_still_updates (dvdspu);
            /* And re-draw the still frame to make sure it appears on
             * screen, otherwise the last frame  might have been discarded 
             * by QoS */
            gst_dvdspu_redraw_still (dvdspu);
          } else
            state->flags &= ~(SPU_STATE_STILL_FRAME);
          DVD_SPU_UNLOCK (dvdspu);
        }
        gst_event_unref (event);
        res = TRUE;
      } else
        res = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, arate;
      GstFormat format;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      /* Only print updates if they have an end time (don't print start_time
       * updates */
      GST_DEBUG_OBJECT (dvdspu, "video pad NewSegment:"
          " Update %d, rate %g arate %g format %d start %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT " position %" GST_TIME_FORMAT,
          update, rate, arate, format, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop), GST_TIME_ARGS (time));

      DVD_SPU_LOCK (dvdspu);

      if (update && start > dvdspu->video_seg.last_stop) {
#if 0
        g_print ("Segment update for video. Advancing from %" GST_TIME_FORMAT
            " to %" GST_TIME_FORMAT "\n",
            GST_TIME_ARGS (dvdspu->video_seg.last_stop), GST_TIME_ARGS (start));
#endif
        while (dvdspu->video_seg.last_stop < start &&
            !(state->flags & SPU_STATE_STILL_FRAME)) {
          DVD_SPU_UNLOCK (dvdspu);
          if (dvspu_handle_vid_buffer (dvdspu, NULL) != GST_FLOW_OK) {
            DVD_SPU_LOCK (dvdspu);
            break;
          }
          DVD_SPU_LOCK (dvdspu);
        }
      }

      gst_segment_set_newsegment_full (&dvdspu->video_seg, update, rate, arate,
          format, start, stop, time);

      DVD_SPU_UNLOCK (dvdspu);

      res = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      res = gst_pad_event_default (pad, event);
      goto done;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_event_default (pad, event);

      DVD_SPU_LOCK (dvdspu);
      gst_segment_init (&dvdspu->video_seg, GST_FORMAT_UNDEFINED);
      gst_buffer_replace (&dvdspu->ref_frame, NULL);
      gst_buffer_replace (&dvdspu->pending_frame, NULL);

      DVD_SPU_UNLOCK (dvdspu);
      goto done;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

done:
  gst_object_unref (dvdspu);
  return res;
#if 0
error:
  gst_event_unref (event);
  return FALSE;
#endif
}

static GstFlowReturn
gst_dvdspu_video_chain (GstPad * pad, GstBuffer * buf)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) (gst_object_get_parent (GST_OBJECT (pad)));
  GstFlowReturn ret;

  g_return_val_if_fail (dvdspu != NULL, GST_FLOW_ERROR);

  GST_LOG_OBJECT (dvdspu, "video buffer %p with TS %" GST_TIME_FORMAT,
      buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  ret = dvspu_handle_vid_buffer (dvdspu, buf);

  gst_object_unref (dvdspu);

  return ret;
}

static GstFlowReturn
dvspu_handle_vid_buffer (GstDVDSpu * dvdspu, GstBuffer * buf)
{
  GstClockTime new_ts;
  GstFlowReturn ret;
  gboolean using_ref = FALSE;

  DVD_SPU_LOCK (dvdspu);

  if (buf == NULL) {
    GstClockTime next_ts = dvdspu->video_seg.last_stop;

    next_ts += gst_util_uint64_scale_int (GST_SECOND,
        dvdspu->spu_state.fps_d, dvdspu->spu_state.fps_n);

    /* NULL buffer was passed - use the reference frame and update the timestamp,
     * or else there's nothing to draw, and just return GST_FLOW_OK */
    if (dvdspu->ref_frame == NULL) {
      gst_segment_set_last_stop (&dvdspu->video_seg, GST_FORMAT_TIME, next_ts);
      goto no_ref_frame;
    }

    buf = gst_buffer_copy (dvdspu->ref_frame);

#if 0
    g_print ("Duping frame %" GST_TIME_FORMAT " with new TS %" GST_TIME_FORMAT
        "\n", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (next_ts));
#endif

    GST_BUFFER_TIMESTAMP (buf) = next_ts;
    using_ref = TRUE;
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_segment_set_last_stop (&dvdspu->video_seg, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buf));
  }

  new_ts = gst_segment_to_running_time (&dvdspu->video_seg, GST_FORMAT_TIME,
      dvdspu->video_seg.last_stop);

#if 0
  g_print ("TS %" GST_TIME_FORMAT " running: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (dvdspu->video_seg.last_stop), GST_TIME_ARGS (new_ts));
#endif

  gst_dvdspu_advance_spu (dvdspu, new_ts);

  /* If we have an active SPU command set, we store a copy of the frame in case
   * we hit a still and need to draw on it. Otherwise, a reference is
   * sufficient in case we later encounter a still */
  if ((dvdspu->spu_state.flags & SPU_STATE_FORCED_DSP) ||
      ((dvdspu->spu_state.flags & SPU_STATE_FORCED_ONLY) == 0 &&
          (dvdspu->spu_state.flags & SPU_STATE_DISPLAY))) {
    if (using_ref == FALSE) {
      GstBuffer *copy;

      /* Take a copy in case we hit a still frame and need the pristine 
       * frame around */
      copy = gst_buffer_copy (buf);
      gst_buffer_replace (&dvdspu->ref_frame, copy);
      gst_buffer_unref (copy);
    }

    /* Render the SPU overlay onto the buffer */
    buf = gst_buffer_make_writable (buf);

    gstgst_dvdspu_render_spu (dvdspu, buf);
  } else {
    if (using_ref == FALSE) {
      /* Not going to draw anything on this frame, just store a reference
       * in case we hit a still frame and need it */
      gst_buffer_replace (&dvdspu->ref_frame, buf);
    }
  }

  if (dvdspu->spu_state.flags & SPU_STATE_STILL_FRAME) {
    GST_DEBUG_OBJECT (dvdspu, "Outputting buffer with TS %" GST_TIME_FORMAT
        "from chain while in still",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  }

  DVD_SPU_UNLOCK (dvdspu);

  /* just push out the incoming buffer without touching it */
  ret = gst_pad_push (dvdspu->srcpad, buf);

  return ret;

no_ref_frame:

  DVD_SPU_UNLOCK (dvdspu);

  return GST_FLOW_OK;
}

/* With SPU LOCK */
static void
gst_dvdspu_redraw_still (GstDVDSpu * dvdspu)
{
  /* If we have an active SPU command set and a reference frame, copy the
   * frame, redraw the SPU and store it as the pending frame for output */
  if (dvdspu->ref_frame) {
    if ((dvdspu->spu_state.flags & SPU_STATE_FORCED_DSP) ||
        ((dvdspu->spu_state.flags & SPU_STATE_FORCED_ONLY) == 0 &&
            (dvdspu->spu_state.flags & SPU_STATE_DISPLAY))) {
      GstBuffer *buf = gst_buffer_copy (dvdspu->ref_frame);

      buf = gst_buffer_make_writable (buf);

      GST_LOG_OBJECT (dvdspu, "Redraw due to Still Frame with ref %p",
          dvdspu->ref_frame);
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;

      /* Render the SPU overlay onto the buffer */
      gstgst_dvdspu_render_spu (dvdspu, buf);
      gst_buffer_replace (&dvdspu->pending_frame, buf);
    } else {
      GST_LOG_OBJECT (dvdspu,
          "Redraw due to Still Frame skipped - no SPU to draw");
    }
  } else {
    GST_LOG_OBJECT (dvdspu, "Not redrawing still frame - no ref frame");
  }
}

static void
gstgst_dvdspu_parse_chg_colcon (GstDVDSpu * dvdspu, guint8 * data, guint8 * end)
{
  SpuState *state = &dvdspu->spu_state;
  guint8 *cur;
  gint16 n_entries;
  gint16 i;

  /* Clear any existing chg colcon info */
  state->n_line_ctrl_i = 0;
  if (state->line_ctrl_i != NULL) {
    g_free (state->line_ctrl_i);
    state->line_ctrl_i = NULL;
  }
  GST_DEBUG_OBJECT (dvdspu, "Change Color & Contrast. Pixel data = %d bytes",
      (gint16) (end - data));

  /* Count the number of entries we'll need */
  n_entries = 0;
  for (cur = data; cur < end;) {
    guint8 n_changes;
    guint32 code;

    if (cur + 4 > end)
      break;

    code = GST_READ_UINT32_BE (cur);
    if (code == 0x0fffffff)
      break;                    /* Termination code */

    n_changes = CLAMP ((cur[2] >> 4), 1, 8);
    cur += 4 + (6 * n_changes);

    if (cur > end)
      break;                    /* Invalid entry overrunning buffer */

    n_entries++;
  }

  state->n_line_ctrl_i = n_entries;
  state->line_ctrl_i = g_new (SpuLineCtrlI, n_entries);

  cur = data;
  for (i = 0; i < n_entries; i++) {
    SpuLineCtrlI *cur_line_ctrl = state->line_ctrl_i + i;
    guint8 n_changes = CLAMP ((cur[2] >> 4), 1, 8);
    guint8 c;

    cur_line_ctrl->n_changes = n_changes;
    cur_line_ctrl->top = ((cur[0] << 8) & 0x300) | cur[1];
    cur_line_ctrl->bottom = ((cur[2] << 8) & 0x300) | cur[3];

    GST_LOG_OBJECT (dvdspu, "ChgColcon Entry %d Top: %d Bottom: %d Changes: %d",
        i, cur_line_ctrl->top, cur_line_ctrl->bottom, n_changes);
    cur += 4;

    for (c = 0; c < n_changes; c++) {
      SpuPixCtrlI *cur_pix_ctrl = cur_line_ctrl->pix_ctrl_i + c;

      cur_pix_ctrl->left = ((cur[0] << 8) & 0x300) | cur[1];
      cur_pix_ctrl->palette = GST_READ_UINT32_BE (cur + 2);
      GST_LOG_OBJECT (dvdspu, "  %d: left: %d palette 0x%x", c,
          cur_pix_ctrl->left, cur_pix_ctrl->palette);
      cur += 6;
    }
  }
}

static void
gst_dvdspu_exec_cmd_blk (GstDVDSpu * dvdspu, guint8 * data, guint8 * end)
{
  SpuState *state = &dvdspu->spu_state;

  while (data < end) {
    guint8 cmd;

    cmd = data[0];

    switch (cmd) {
      case SPU_CMD_FSTA_DSP:
        GST_DEBUG_OBJECT (dvdspu, " Forced Display");
        state->flags |= SPU_STATE_FORCED_DSP;
        data += 1;
        break;
      case SPU_CMD_DSP:
        GST_DEBUG_OBJECT (dvdspu, " Display On");
        state->flags |= SPU_STATE_DISPLAY;
        data += 1;
        break;
      case SPU_CMD_STP_DSP:
        GST_DEBUG_OBJECT (dvdspu, " Display Off");
        state->flags &= ~(SPU_STATE_FORCED_DSP | SPU_STATE_DISPLAY);
        data += 1;
        break;
      case SPU_CMD_SET_COLOR:{
        if (G_UNLIKELY (data + 3 >= end))
          return;               /* Invalid SET_COLOR cmd at the end of the blk */

        state->main_idx[3] = data[1] >> 4;
        state->main_idx[2] = data[1] & 0x0f;
        state->main_idx[1] = data[2] >> 4;
        state->main_idx[0] = data[2] & 0x0f;

        state->main_pal_dirty = TRUE;

        GST_DEBUG_OBJECT (dvdspu,
            " Set Color bg %u pattern %u emph-1 %u emph-2 %u",
            state->main_idx[0], state->main_idx[1], state->main_idx[2],
            state->main_idx[3]);
        data += 3;
        break;
      }
      case SPU_CMD_SET_ALPHA:{
        if (G_UNLIKELY (data + 3 >= end))
          return;               /* Invalid SET_ALPHA cmd at the end of the blk */

        state->main_alpha[3] = data[1] >> 4;
        state->main_alpha[2] = data[1] & 0x0f;
        state->main_alpha[1] = data[2] >> 4;
        state->main_alpha[0] = data[2] & 0x0f;

        state->main_pal_dirty = TRUE;

        GST_DEBUG_OBJECT (dvdspu,
            " Set Alpha bg %u pattern %u emph-1 %u emph-2 %u",
            state->main_alpha[0], state->main_alpha[1], state->main_alpha[2],
            state->main_alpha[3]);
        data += 3;
        break;
      }
      case SPU_CMD_SET_DAREA:{
        SpuRect *r = &state->disp_rect;

        if (G_UNLIKELY (data + 7 >= end))
          return;               /* Invalid SET_DAREA cmd at the end of the blk */

        r->top = ((data[4] & 0x3f) << 4) | ((data[5] & 0xe0) >> 4);
        r->left = ((data[1] & 0x3f) << 4) | ((data[2] & 0xf0) >> 4);
        r->right = ((data[2] & 0x03) << 8) | data[3];
        r->bottom = ((data[5] & 0x03) << 8) | data[6];

        GST_DEBUG_OBJECT (dvdspu,
            " Set Display Area top %u left %u bottom %u right %u", r->top,
            r->left, r->bottom, r->right);

        data += 7;
        break;
      }
      case SPU_CMD_DSPXA:{
        if (G_UNLIKELY (data + 5 >= end))
          return;               /* Invalid SET_DSPXE cmd at the end of the blk */

        state->pix_data[0] = GST_READ_UINT16_BE (data + 1);
        state->pix_data[1] = GST_READ_UINT16_BE (data + 3);
        /* Store a reference to the current command buffer, as that's where 
         * we'll need to take our pixel data from */
        gst_buffer_replace (&state->pix_buf, state->buf);

        GST_DEBUG_OBJECT (dvdspu, " Set Pixel Data Offsets top: %u bot: %u",
            state->pix_data[0], state->pix_data[1]);

        data += 5;
        break;
      }
      case SPU_CMD_CHG_COLCON:{
        guint16 field_size;

        GST_DEBUG_OBJECT (dvdspu, " Set Color & Contrast Change");
        if (G_UNLIKELY (data + 3 >= end))
          return;               /* Invalid CHG_COLCON cmd at the end of the blk */

        data++;
        field_size = GST_READ_UINT16_BE (data);

        if (G_UNLIKELY (data + field_size >= end))
          return;               /* Invalid CHG_COLCON cmd at the end of the blk */

        gstgst_dvdspu_parse_chg_colcon (dvdspu, data + 2, data + field_size);
        state->line_ctrl_i_pal_dirty = TRUE;
        data += field_size;
        break;
      }
      case SPU_CMD_END:
      default:
        GST_DEBUG_OBJECT (dvdspu, " END");
        data = end;
        break;
    }
  }
}

static void
gst_dvdspu_finish_spu_buf (GstDVDSpu * dvdspu)
{
  SpuState *state = &dvdspu->spu_state;

  state->next_ts = state->base_ts = GST_CLOCK_TIME_NONE;
  gst_buffer_replace (&state->buf, NULL);

  GST_DEBUG_OBJECT (dvdspu, "Finished SPU buffer");
}

static gboolean
gst_dvdspu_setup_cmd_blk (GstDVDSpu * dvdspu, guint16 cmd_blk_offset,
    guint8 * start, guint8 * end)
{
  SpuState *state = &dvdspu->spu_state;
  guint16 delay;
  guint8 *cmd_blk = start + cmd_blk_offset;

  if (G_UNLIKELY (cmd_blk + 5 >= end))
    return FALSE;               /* No valid command block to read */

  delay = GST_READ_UINT16_BE (cmd_blk);
  state->next_ts = state->base_ts + STM_TO_GST (delay);
  state->cur_cmd_blk = cmd_blk_offset;

  GST_DEBUG_OBJECT (dvdspu, "Setup CMD Block @ %u with TS %" GST_TIME_FORMAT,
      state->cur_cmd_blk, GST_TIME_ARGS (state->next_ts));
  return TRUE;
}

static void
gst_dvdspu_handle_new_spu_buf (GstDVDSpu * dvdspu, SpuPacket * packet)
{
  guint8 *start, *end;
  SpuState *state = &dvdspu->spu_state;

  if (G_UNLIKELY (GST_BUFFER_SIZE (packet->buf) < 4))
    goto invalid;

  if (state->buf != NULL) {
    gst_buffer_unref (state->buf);
    state->buf = NULL;
  }
  state->buf = packet->buf;
  state->base_ts = packet->event_ts;

  start = GST_BUFFER_DATA (state->buf);
  end = start + GST_BUFFER_SIZE (state->buf);

  /* Configure the first command block in this buffer as our initial blk */
  state->cur_cmd_blk = GST_READ_UINT16_BE (start + 2);
  gst_dvdspu_setup_cmd_blk (dvdspu, state->cur_cmd_blk, start, end);
  /* Clear existing chg-colcon info */
  if (state->line_ctrl_i != NULL) {
    g_free (state->line_ctrl_i);
    state->line_ctrl_i = NULL;
  }
  return;

invalid:
  /* Invalid buffer */
  gst_dvdspu_finish_spu_buf (dvdspu);
}

static void
gst_dvdspu_handle_dvd_event (GstDVDSpu * dvdspu, GstEvent * event)
{
  const gchar *event_type;
  const GstStructure *structure = gst_event_get_structure (event);
  SpuState *state = &dvdspu->spu_state;
  gboolean hl_change = FALSE;

  event_type = gst_structure_get_string (structure, "event");
  GST_INFO_OBJECT (dvdspu, "DVD event of type %s on subp pad OOB=%d",
      event_type, (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM_OOB));

  if (strcmp (event_type, "dvd-spu-clut-change") == 0) {
    gchar prop_name[32];
    gint i;
    gint entry;

    for (i = 0; i < 16; i++) {
      g_snprintf (prop_name, 32, "clut%02d", i);
      if (!gst_structure_get_int (structure, prop_name, &entry))
        entry = 0;
      state->current_clut[i] = (guint32) entry;
    }

    state->main_pal_dirty = TRUE;
    state->hl_pal_dirty = TRUE;
    state->line_ctrl_i_pal_dirty = TRUE;
    hl_change = TRUE;
  } else if (strcmp (event_type, "dvd-spu-highlight") == 0) {
    gint val;

    if (gst_structure_get_int (structure, "palette", &val)) {
      state->hl_idx[3] = ((guint32) (val) >> 28) & 0x0f;
      state->hl_idx[2] = ((guint32) (val) >> 24) & 0x0f;
      state->hl_idx[1] = ((guint32) (val) >> 20) & 0x0f;
      state->hl_idx[0] = ((guint32) (val) >> 16) & 0x0f;

      state->hl_alpha[3] = ((guint32) (val) >> 12) & 0x0f;
      state->hl_alpha[2] = ((guint32) (val) >> 8) & 0x0f;
      state->hl_alpha[1] = ((guint32) (val) >> 4) & 0x0f;
      state->hl_alpha[0] = ((guint32) (val) >> 0) & 0x0f;

      state->hl_pal_dirty = TRUE;
    }
    if (gst_structure_get_int (structure, "sx", &val))
      state->hl_rect.left = (gint16) val;
    if (gst_structure_get_int (structure, "sy", &val))
      state->hl_rect.top = (gint16) val;
    if (gst_structure_get_int (structure, "ex", &val))
      state->hl_rect.right = (gint16) val;
    if (gst_structure_get_int (structure, "ey", &val))
      state->hl_rect.bottom = (gint16) val;

    GST_INFO_OBJECT (dvdspu, "Highlight rect is now (%d,%d) to (%d,%d)",
        state->hl_rect.left, state->hl_rect.top,
        state->hl_rect.right, state->hl_rect.bottom);
    hl_change = TRUE;
  } else if (strcmp (event_type, "dvd-spu-reset-highlight") == 0) {
    if (state->hl_rect.top != -1 || state->hl_rect.bottom != -1)
      hl_change = TRUE;
    state->hl_rect.top = -1;
    state->hl_rect.bottom = -1;
    GST_INFO_OBJECT (dvdspu, "Highlight off");
  } else if (strcmp (event_type, "dvd-set-subpicture-track") == 0) {
    gboolean forced_only;

    if (gst_structure_get_boolean (structure, "forced-only", &forced_only)) {
      gboolean was_forced = (state->flags & SPU_STATE_FORCED_ONLY);

      if (forced_only)
        state->flags |= SPU_STATE_FORCED_ONLY;
      else
        state->flags &= ~(SPU_STATE_FORCED_ONLY);

      if ((was_forced && !forced_only) || (!was_forced && forced_only))
        hl_change = TRUE;
    }
  }

  if (hl_change && (state->flags & SPU_STATE_STILL_FRAME)) {
    gst_dvdspu_redraw_still (dvdspu);
  }

  gst_event_unref (event);
}

#if 0
static void
gst_dvdspu_dump_dcsq (GstDVDSpu * dvdspu,
    GstClockTime start_ts, GstBuffer * spu_buf)
{
  guint16 cmd_blk_offset;
  guint16 next_blk;
  guint8 *start, *end;

  start = GST_BUFFER_DATA (spu_buf);
  end = start + GST_BUFFER_SIZE (spu_buf);

  g_return_if_fail (start != NULL);

  /* First command */
  next_blk = GST_READ_UINT16_BE (start + 2);
  cmd_blk_offset = 0;

  /* Loop through all commands */
  g_print ("SPU begins @ %" GST_TIME_FORMAT " offset %u\n",
      GST_TIME_ARGS (start_ts), next_blk);

  while (cmd_blk_offset != next_blk) {
    guint8 *data;
    GstClockTime cmd_blk_ts;

    cmd_blk_offset = next_blk;

    if (G_UNLIKELY (start + cmd_blk_offset + 5 >= end))
      break;                    /* No valid command to read */

    data = start + cmd_blk_offset;

    cmd_blk_ts = start_ts + STM_TO_GST (GST_READ_UINT16_BE (data));
    next_blk = GST_READ_UINT16_BE (data + 2);

    g_print ("Cmd Blk @ offset %u next %u ts %" GST_TIME_FORMAT "\n",
        cmd_blk_offset, next_blk, GST_TIME_ARGS (cmd_blk_ts));

    data += 4;
    gst_dvdspu_exec_cmd_blk (dvdspu, data, end);
  }
}
#endif

/* Advance the SPU packet/command queue to a time. new_ts is in running time */
static void
gst_dvdspu_advance_spu (GstDVDSpu * dvdspu, GstClockTime new_ts)
{
  SpuState *state = &dvdspu->spu_state;

  while (state->next_ts == GST_CLOCK_TIME_NONE || state->next_ts <= new_ts) {
    guint8 *start, *cmd_blk, *end;
    guint16 next_blk;

    if (state->buf == NULL) {
      GstClockTime vid_run_ts;

      /* No current command buffer, try and get one */
      SpuPacket *packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);

      if (packet == NULL)
        return;                 /* No SPU packets available */

      vid_run_ts =
          gst_segment_to_running_time (&dvdspu->video_seg, GST_FORMAT_TIME,
          dvdspu->video_seg.last_stop);
      GST_LOG_OBJECT (dvdspu,
          "Popped new SPU packet with TS %" GST_TIME_FORMAT
          ". Video last_stop=%" GST_TIME_FORMAT " (%" GST_TIME_FORMAT ")",
          GST_TIME_ARGS (packet->event_ts), GST_TIME_ARGS (vid_run_ts),
          GST_TIME_ARGS (dvdspu->video_seg.last_stop));

      if (packet->buf) {
        // gst_dvdspu_dump_dcsq (dvdspu, packet->event_ts, packet->buf);
        gst_dvdspu_handle_new_spu_buf (dvdspu, packet);
      }
      if (packet->event)
        gst_dvdspu_handle_dvd_event (dvdspu, packet->event);

      g_free (packet);
      continue;
    }

    GST_DEBUG_OBJECT (dvdspu,
        "Advancing SPU from TS %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (state->next_ts), GST_TIME_ARGS (new_ts));

    /* If we get here, we have an SPU buffer, and it's time to process the
     * next cmd */
    g_assert (state->buf != NULL && state->next_ts <= new_ts);

    GST_DEBUG_OBJECT (dvdspu, "Executing cmd blk with TS %" GST_TIME_FORMAT
        " @ offset %u", GST_TIME_ARGS (state->next_ts), state->cur_cmd_blk);

    start = GST_BUFFER_DATA (state->buf);
    end = start + GST_BUFFER_SIZE (state->buf);

    cmd_blk = start + state->cur_cmd_blk;

    if (G_UNLIKELY (cmd_blk + 5 >= end)) {
      /* Invalid. Finish the buffer and loop again */
      gst_dvdspu_finish_spu_buf (dvdspu);
      continue;
    }

    gst_dvdspu_exec_cmd_blk (dvdspu, cmd_blk + 4, end);

    next_blk = GST_READ_UINT16_BE (cmd_blk + 2);
    if (next_blk != state->cur_cmd_blk) {
      /* Advance to the next block of commands */
      gst_dvdspu_setup_cmd_blk (dvdspu, next_blk, start, end);
    } else {
      /* Next Block points to the current block, so we're finished with this
       * SPU buffer */
      gst_dvdspu_finish_spu_buf (dvdspu);
    }
  }
}

static void
gst_dvdspu_check_still_updates (GstDVDSpu * dvdspu)
{
  GstClockTime sub_ts;
  GstClockTime vid_ts;

  if (dvdspu->spu_state.flags & SPU_STATE_STILL_FRAME) {

    vid_ts = gst_segment_to_running_time (&dvdspu->video_seg,
        GST_FORMAT_TIME, dvdspu->video_seg.last_stop);
    sub_ts = gst_segment_to_running_time (&dvdspu->subp_seg,
        GST_FORMAT_TIME, dvdspu->subp_seg.last_stop);

    vid_ts = MAX (vid_ts, sub_ts);

    GST_DEBUG_OBJECT (dvdspu,
        "In still frame - advancing TS to %" GST_TIME_FORMAT
        " to process SPU buffer", GST_TIME_ARGS (vid_ts));
    gst_dvdspu_advance_spu (dvdspu, vid_ts);
  }
}

static GstFlowReturn
gst_dvdspu_subpic_chain (GstPad * pad, GstBuffer * buf)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) (gst_object_get_parent (GST_OBJECT (pad)));

  g_return_val_if_fail (dvdspu != NULL, GST_FLOW_ERROR);

  GST_INFO_OBJECT (dvdspu, "Have subpicture buffer with timestamp %"
      GST_TIME_FORMAT " and size %u",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));

  DVD_SPU_LOCK (dvdspu);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_segment_set_last_stop (&dvdspu->subp_seg, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buf));
  }

  if (dvdspu->partial_spu != NULL) {
    dvdspu->partial_spu = gst_buffer_join (dvdspu->partial_spu, buf);
  } else {
    /* If we don't yet have a buffer, wait for one with a timestamp,
     * since that will avoid collecting the 2nd half of a partial buf */
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
      dvdspu->partial_spu = buf;
    else
      gst_buffer_unref (buf);
  }

  if (dvdspu->partial_spu != NULL && GST_BUFFER_SIZE (dvdspu->partial_spu) > 4) {
    guint16 packet_size;
    guint8 *data;

    data = GST_BUFFER_DATA (dvdspu->partial_spu);
    packet_size = GST_READ_UINT16_BE (data);

    if (packet_size == GST_BUFFER_SIZE (dvdspu->partial_spu)) {
      SpuPacket *spu_packet;
      GstClockTime ts;
      GstClockTime run_ts = GST_CLOCK_TIME_NONE;

      GST_DEBUG_OBJECT (dvdspu,
          "Complete subpicture buffer of %u bytes with TS %" GST_TIME_FORMAT,
          GST_BUFFER_SIZE (dvdspu->partial_spu),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (dvdspu->partial_spu)));

      /* Decide whether to pass this buffer through to the rendering code */
      ts = GST_BUFFER_TIMESTAMP (dvdspu->partial_spu);
      if (GST_CLOCK_TIME_IS_VALID (ts)) {
        if (ts < (GstClockTime) dvdspu->subp_seg.start) {
          GstClockTimeDiff diff = dvdspu->subp_seg.start - ts;

          /* Buffer starts before segment, see if we can calculate a running time */
          run_ts =
              gst_segment_to_running_time (&dvdspu->subp_seg, GST_FORMAT_TIME,
              dvdspu->subp_seg.start);
          if (run_ts >= (GstClockTime) diff)
            run_ts -= diff;
          else
            run_ts = GST_CLOCK_TIME_NONE;       /* No running time possible for this subpic */
        } else {
          /* TS within segment, convert to running time */
          run_ts =
              gst_segment_to_running_time (&dvdspu->subp_seg, GST_FORMAT_TIME,
              ts);
        }
      }

      if (GST_CLOCK_TIME_IS_VALID (run_ts)) {
        /* Complete SPU packet, push it onto the queue for processing when
         * video packets come past */
        spu_packet = g_new0 (SpuPacket, 1);
        spu_packet->buf = dvdspu->partial_spu;

        /* Store the activation time of this buffer in running time */
        spu_packet->event_ts =
            gst_segment_to_running_time (&dvdspu->subp_seg, GST_FORMAT_TIME,
            ts);
        GST_INFO_OBJECT (dvdspu,
            "Pushing SPU buf with TS %" GST_TIME_FORMAT " running time %"
            GST_TIME_FORMAT, GST_TIME_ARGS (ts),
            GST_TIME_ARGS (spu_packet->event_ts));

        g_queue_push_tail (dvdspu->pending_spus, spu_packet);
        dvdspu->partial_spu = NULL;

        /* In a still frame condition, advance the SPU to make sure the state is 
         * up to date */
        gst_dvdspu_check_still_updates (dvdspu);
      } else {
        gst_buffer_unref (dvdspu->partial_spu);
        dvdspu->partial_spu = NULL;
      }
    } else if (packet_size < GST_BUFFER_SIZE (dvdspu->partial_spu)) {
      /* Somehow we collected too much - something is wrong. Drop the
       * packet entirely and wait for a new one */
      GST_DEBUG_OBJECT (dvdspu, "Discarding invalid SPU buffer of size %u",
          GST_BUFFER_SIZE (dvdspu->partial_spu));

      gst_buffer_unref (dvdspu->partial_spu);
      dvdspu->partial_spu = NULL;
    }
  }

  DVD_SPU_UNLOCK (dvdspu);

  gst_object_unref (dvdspu);
  return GST_FLOW_OK;
}

static gboolean
gst_dvdspu_subpic_event (GstPad * pad, GstEvent * event)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) (gst_object_get_parent (GST_OBJECT (pad)));
  gboolean res = TRUE;

  g_return_val_if_fail (dvdspu != NULL, FALSE);

  /* Some events on the subpicture sink pad just get ignored, like 
   * FLUSH_START */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      const GstStructure *structure = gst_event_get_structure (event);
      gboolean need_push;

      if (!gst_structure_has_name (structure, "application/x-gst-dvd")) {
        res = gst_pad_event_default (pad, event);
        break;
      }

      DVD_SPU_LOCK (dvdspu);
      if (GST_EVENT_IS_SERIALIZED (event)) {
        SpuPacket *spu_packet = g_new0 (SpuPacket, 1);

        spu_packet->event = event;
        g_queue_push_tail (dvdspu->pending_spus, spu_packet);
      } else {
        gst_dvdspu_handle_dvd_event (dvdspu, event);
      }

      /* If the handle_dvd_event generated a pending frame, we
       * need to synchronise with the video pad's stream lock and push it.
       * This requires some dancing to preserve locking order and handle
       * flushes correctly */
      need_push = (dvdspu->pending_frame != NULL);
      DVD_SPU_UNLOCK (dvdspu);
      if (need_push) {
        GstBuffer *to_push = NULL;
        gboolean flushing;

        GST_LOG_OBJECT (dvdspu, "Going for stream lock");
        GST_PAD_STREAM_LOCK (dvdspu->videosinkpad);
        GST_LOG_OBJECT (dvdspu, "Got stream lock");
        GST_OBJECT_LOCK (dvdspu->videosinkpad);
        flushing = GST_PAD_IS_FLUSHING (dvdspu->videosinkpad);
        GST_OBJECT_UNLOCK (dvdspu->videosinkpad);

        DVD_SPU_LOCK (dvdspu);
        if (dvdspu->pending_frame == NULL || flushing) {
          /* Got flushed while waiting for the stream lock */
          DVD_SPU_UNLOCK (dvdspu);
        } else {
          to_push = dvdspu->pending_frame;
          dvdspu->pending_frame = NULL;

          DVD_SPU_UNLOCK (dvdspu);
          gst_pad_push (dvdspu->srcpad, to_push);
        }
        GST_LOG_OBJECT (dvdspu, "Dropping stream lock");
        GST_PAD_STREAM_UNLOCK (dvdspu->videosinkpad);
      }

      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 start, stop, time;
      gdouble arate;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* Only print updates if they have an end time (don't print start_time
       * updates */
      GST_DEBUG_OBJECT (dvdspu, "subpic pad NewSegment:"
          " Update %d, rate %g arate %g format %d start %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT " position %" GST_TIME_FORMAT,
          update, rate, arate, format, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop), GST_TIME_ARGS (time));

      DVD_SPU_LOCK (dvdspu);
      gst_segment_set_newsegment_full (&dvdspu->subp_seg, update, rate, arate,
          format, start, stop, time);
      DVD_SPU_UNLOCK (dvdspu);

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_event_unref (event);
      goto done;
    case GST_EVENT_FLUSH_STOP:
      DVD_SPU_LOCK (dvdspu);
      gst_dvdspu_flush_spu_info (dvdspu);
      DVD_SPU_UNLOCK (dvdspu);

      /* We don't forward flushes on the spu pad */
      gst_event_unref (event);
      goto done;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

done:
  gst_object_unref (dvdspu);

  return res;
}

static GstStateChangeReturn
gst_dvdspu_change_state (GstElement * element, GstStateChange transition)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) element;
  GstStateChangeReturn ret;

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      DVD_SPU_LOCK (dvdspu);
      gst_dvdspu_clear (dvdspu);
      DVD_SPU_UNLOCK (dvdspu);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gstgst_dvdspu_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_dvdspu_debug, "gstdvdspu",
      0, "DVD Sub-picture Overlay decoder/renderer");

  return gst_element_register (plugin, "dvdspu",
      GST_RANK_NONE, GST_TYPE_DVD_SPU);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvdspu",
    "DVD Sub-picture Overlay element",
    gstgst_dvdspu_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
