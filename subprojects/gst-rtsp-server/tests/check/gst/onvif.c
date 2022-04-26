/* GStreamer
 * Copyright (C) 2018 Mathieu Duponchelle <mathieu@centricular.com>
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

#include <gst/check/gstcheck.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/base/gstpushsrc.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <rtsp-onvif-client.h>
#include <rtsp-onvif-media.h>
#include <rtsp-onvif-media-factory.h>

/* Test source implementation */

#define FRAME_DURATION (GST_MSECOND)

typedef struct
{
  GstPushSrc element;

  GstSegment *segment;
  /* In milliseconds */
  guint trickmode_interval;
  GstClockTime ntp_offset;
} TestSrc;

typedef struct
{
  GstPushSrcClass parent_class;
} TestSrcClass;

/**
 * video/x-dumdum is a very simple encoded video format:
 *
 * - It has I-frames, P-frames and B-frames for the purpose
 *   of testing trick modes, and is infinitely scalable, mimicking server-side
 *   trick modes that would have the server reencode when a trick mode seek with
 *   an absolute rate different from 1.0 is requested.
 *
 * - The only source capable of outputting this format, `TestSrc`, happens
 *   to always output frames following this pattern:
 *
 *   IBBBBPBBBBI
 *
 *   Its framerate is 1000 / 1, each Group of Pictures is thus 10 milliseconds
 *   long. The first frame in the stream dates back to January the first,
 *   1900, at exactly midnight. There are no gaps in the stream.
 *
 *   A nice side effect of this for testing purposes is that as the resolution
 *   of UTC (clock=) seeks is a hundredth of a second, this coincides with the
 *   alignment of our Group of Pictures, which means we don't have to worry
 *   about synchronization points.
 *
 * - Size is used to distinguish the various frame types:
 *
 *   * I frames: 20 bytes
 *   * P frames: 10 bytes
 *   * B frames: 5 bytes
 *
 */

#define TEST_CAPS "video/x-dumdum"

typedef enum
{
  FRAME_TYPE_I,
  FRAME_TYPE_P,
  FRAME_TYPE_B,
} FrameType;

static FrameType
frame_type_for_index (guint64 index)
{
  FrameType ret;

  if (index % 10 == 0)
    ret = FRAME_TYPE_I;
  else if (index % 5 == 0)
    ret = FRAME_TYPE_P;
  else
    ret = FRAME_TYPE_B;

  return ret;
}

static GstStaticPadTemplate test_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (TEST_CAPS)
    );

GType test_src_get_type (void);

#define test_src_parent_class parent_class
G_DEFINE_TYPE (TestSrc, test_src, GST_TYPE_PUSH_SRC);

#define TEST_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), test_src_get_type(), TestSrc))

#define ROUND_UP_TO_10(x) (((x + 10 - 1) / 10) * 10)
#define ROUND_DOWN_TO_10(x) (x - (x % 10))

/*
 * For now, the theoretical range of our test source is infinite.
 *
 * When creating a buffer, we use the current segment position to
 * determine the PTS, and simply increment it afterwards.
 *
 * When the stop time of a buffer we have created reaches segment->stop,
 * GstBaseSrc will take care of sending an EOS for us, which rtponviftimestamp
 * will translate to setting the T flag in the RTP header extension.
 */
static GstFlowReturn
test_src_create (GstPushSrc * psrc, GstBuffer ** buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gsize buf_size;
  TestSrc *src = (TestSrc *) psrc;
  GstClockTime pts, duration;
  FrameType ftype;
  guint64 n_frames;

  if (src->segment->rate < 1.0) {
    if (src->segment->position < src->segment->start) {
      ret = GST_FLOW_EOS;
      goto done;
    }
  } else if ((src->segment->position >= src->segment->stop)) {
    ret = GST_FLOW_EOS;
    goto done;
  }

  pts = src->segment->position;
  duration = FRAME_DURATION;

  if ((src->segment->flags & GST_SEGMENT_FLAG_TRICKMODE_KEY_UNITS)) {
    duration =
        MAX (duration * 10,
        duration * ROUND_UP_TO_10 (src->trickmode_interval));
  } else if ((src->
          segment->flags & GST_SEGMENT_FLAG_TRICKMODE_FORWARD_PREDICTED)) {
    duration *= 5;
  }

  n_frames = gst_util_uint64_scale (src->segment->position, 1000, GST_SECOND);

  ftype = frame_type_for_index (n_frames);

  switch (ftype) {
    case FRAME_TYPE_I:
      buf_size = 20;
      break;
    case FRAME_TYPE_P:
      buf_size = 10;
      break;
    case FRAME_TYPE_B:
      buf_size = 5;
      break;
  }

  *buffer = gst_buffer_new_allocate (NULL, buf_size, NULL);

  if (ftype != FRAME_TYPE_I) {
    GST_BUFFER_FLAG_SET (*buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  GST_BUFFER_PTS (*buffer) = pts;
  GST_BUFFER_DURATION (*buffer) = duration;

  src->segment->position = pts + duration;

  if (!GST_CLOCK_TIME_IS_VALID (src->ntp_offset)) {
    GstClock *clock = gst_system_clock_obtain ();
    GstClockTime clock_time = gst_clock_get_time (clock);
    guint64 real_time = g_get_real_time ();
    GstStructure *s;
    GstEvent *onvif_event;

    real_time *= 1000;
    real_time += (G_GUINT64_CONSTANT (2208988800) * GST_SECOND);
    src->ntp_offset = real_time - clock_time;

    s = gst_structure_new ("GstOnvifTimestamp",
        "ntp-offset", G_TYPE_UINT64, src->ntp_offset,
        "discont", G_TYPE_BOOLEAN, FALSE, NULL);

    onvif_event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

    gst_element_send_event (GST_ELEMENT (src), onvif_event);
  }

  if (src->segment->rate < 1.0) {
    guint64 next_n_frames =
        gst_util_uint64_scale (src->segment->position, 1000, GST_SECOND);

    if (src->segment->position > src->segment->stop
        || next_n_frames / 10 > n_frames / 10) {
      GstStructure *s;
      GstEvent *onvif_event;
      guint n_gops;

      n_gops = MAX (1, ((int) src->trickmode_interval / 10));

      next_n_frames = (n_frames / 10 - n_gops) * 10;

      src->segment->position = next_n_frames * GST_MSECOND;
      s = gst_structure_new ("GstOnvifTimestamp",
          "ntp-offset", G_TYPE_UINT64, src->ntp_offset,
          "discont", G_TYPE_BOOLEAN, TRUE, NULL);

      onvif_event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

      gst_element_send_event (GST_ELEMENT (src), onvif_event);
    }
  }

done:
  return ret;
}

static void
test_src_init (TestSrc * src)
{
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_automatic_eos (GST_BASE_SRC (src), FALSE);
  src->segment = NULL;
  src->ntp_offset = GST_CLOCK_TIME_NONE;
}

static void
test_src_finalize (GObject * obj)
{
  TestSrc *src = TEST_SRC (obj);

  if (src->segment != NULL)
    gst_segment_free (src->segment);

  G_OBJECT_CLASS (test_src_parent_class)->finalize (obj);
}

/*
 * We support seeking, both this method and GstBaseSrc.do_seek must
 * be implemented for GstBaseSrc to report TRUE in the seeking query.
 */
static gboolean
test_src_is_seekable (GstBaseSrc * bsrc)
{
  return TRUE;
}

/* Extremely simple seek handling for now, we simply update our
 * segment, which will cause test_src_create to timestamp output
 * buffers as expected.
 */
static gboolean
test_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  TestSrc *src = (TestSrc *) bsrc;

  if ((segment->flags & GST_SEGMENT_FLAG_TRICKMODE
          && ABS (segment->rate) != 1.0)) {
    segment->applied_rate = segment->rate;
    segment->stop =
        segment->start + ((segment->stop -
            segment->start) / ABS (segment->rate));
    segment->rate = segment->rate > 0 ? 1.0 : -1.0;
  }

  if (src->segment)
    gst_segment_free (src->segment);

  src->segment = gst_segment_copy (segment);

  if (src->segment->rate < 0) {
    guint64 n_frames =
        ROUND_DOWN_TO_10 (gst_util_uint64_scale (src->segment->stop - 1, 1000,
            GST_SECOND));

    src->segment->position = n_frames * GST_MSECOND;
  }

  return TRUE;
}

static gboolean
test_src_event (GstBaseSrc * bsrc, GstEvent * event)
{
  TestSrc *src = (TestSrc *) bsrc;

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
    GstClockTime interval;

    gst_event_parse_seek_trickmode_interval (event, &interval);

    src->trickmode_interval = interval / 1000000;
  }

  return GST_BASE_SRC_CLASS (parent_class)->event (bsrc, event);
}

static void
test_src_class_init (TestSrcClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = test_src_finalize;

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &test_src_template);
  GST_PUSH_SRC_CLASS (klass)->create = test_src_create;
  GST_BASE_SRC_CLASS (klass)->is_seekable = test_src_is_seekable;
  GST_BASE_SRC_CLASS (klass)->do_seek = test_src_do_seek;
  GST_BASE_SRC_CLASS (klass)->event = test_src_event;
}

static GstElement *
test_src_new (void)
{
  return g_object_new (test_src_get_type (), NULL);
}

/* Test media factory */

typedef struct
{
  GstRTSPMediaFactory factory;
} TestMediaFactory;

typedef struct
{
  GstRTSPMediaFactoryClass parent_class;
} TestMediaFactoryClass;

GType test_media_factory_get_type (void);

G_DEFINE_TYPE (TestMediaFactory, test_media_factory,
    GST_TYPE_RTSP_MEDIA_FACTORY);

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

static GstElement *
test_media_factory_create_element (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstElement *ret = gst_bin_new (NULL);
  GstElement *pbin = gst_bin_new ("pay0");
  GstElement *src, *pay, *onvifts, *queue;
  GstPad *sinkpad, *srcpad;
  GstPadLinkReturn link_ret;

  src = test_src_new ();
  gst_bin_add (GST_BIN (ret), src);
  MAKE_AND_ADD (pay, pbin, "rtpgstpay", fail, NULL);
  MAKE_AND_ADD (onvifts, pbin, "rtponviftimestamp", fail, NULL);
  MAKE_AND_ADD (queue, pbin, "queue", fail, NULL);

  gst_bin_add (GST_BIN (ret), pbin);
  if (!gst_element_link_many (pay, onvifts, queue, NULL))
    goto fail;

  sinkpad = gst_element_get_static_pad (pay, "sink");
  gst_element_add_pad (pbin, gst_ghost_pad_new ("sink", sinkpad));
  gst_object_unref (sinkpad);

  sinkpad = gst_element_get_static_pad (pbin, "sink");
  srcpad = gst_element_get_static_pad (src, "src");
  link_ret = gst_pad_link (srcpad, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  if (link_ret != GST_PAD_LINK_OK)
    goto fail;

  srcpad = gst_element_get_static_pad (queue, "src");
  gst_element_add_pad (pbin, gst_ghost_pad_new ("src", srcpad));
  gst_object_unref (srcpad);

  g_object_set (pay, "timestamp-offset", 0, NULL);
  g_object_set (onvifts, "set-t-bit", TRUE, NULL);

done:
  return ret;

fail:
  gst_object_unref (ret);
  ret = NULL;
  goto done;
}

static void
test_media_factory_init (TestMediaFactory * factory)
{
}

static void
test_media_factory_class_init (TestMediaFactoryClass * klass)
{
  GST_RTSP_MEDIA_FACTORY_CLASS (klass)->create_element =
      test_media_factory_create_element;
}

static GstRTSPMediaFactory *
test_media_factory_new (void)
{
  GstRTSPMediaFactory *result;

  result = g_object_new (test_media_factory_get_type (), NULL);

  return result;
}

/* Actual tests implementation */

static gchar *session_id;
static gint cseq;
static gboolean terminal_frame;
static gboolean received_rtcp;

static GstSDPMessage *
sdp_from_message (GstRTSPMessage * msg)
{
  GstSDPMessage *sdp_message;
  guint8 *body = NULL;
  guint body_size;

  fail_unless (gst_rtsp_message_get_body (msg, &body,
          &body_size) == GST_RTSP_OK);
  fail_unless (gst_sdp_message_new (&sdp_message) == GST_SDP_OK);
  fail_unless (gst_sdp_message_parse_buffer (body, body_size,
          sdp_message) == GST_SDP_OK);

  return sdp_message;
}

static gboolean
test_response_x_onvif_track (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstSDPMessage *sdp = sdp_from_message (response);
  guint medias_len = gst_sdp_message_medias_len (sdp);
  guint i;

  fail_unless_equals_int (medias_len, 1);

  for (i = 0; i < medias_len; i++) {
    const GstSDPMedia *smedia = gst_sdp_message_get_media (sdp, i);
    gchar *x_onvif_track = g_strdup_printf ("APPLICATION%03d", i);

    fail_unless_equals_string (gst_sdp_media_get_attribute_val (smedia,
            "x-onvif-track"), x_onvif_track);
    g_free (x_onvif_track);
  }

  gst_sdp_message_free (sdp);

  return TRUE;
}

static gboolean
test_setup_response_200 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  gchar *str;
  GstRTSPSessionPool *session_pool;
  GstRTSPSession *session;
  gchar **session_hdr_params;

  fail_unless_equals_int (gst_rtsp_message_get_type (response),
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless_equals_int (code, GST_RTSP_STS_OK);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_CSEQ, &str,
          0) == GST_RTSP_OK);
  fail_unless (atoi (str) == cseq++);

  fail_unless (gst_rtsp_message_get_header (response, GST_RTSP_HDR_SESSION,
          &str, 0) == GST_RTSP_OK);
  session_hdr_params = g_strsplit (str, ";", -1);

  /* session-id value */
  fail_unless (session_hdr_params[0] != NULL);

  session_pool = gst_rtsp_client_get_session_pool (client);
  fail_unless (session_pool != NULL);

  session = gst_rtsp_session_pool_find (session_pool, session_hdr_params[0]);
  g_strfreev (session_hdr_params);

  /* remember session id to be able to send teardown */
  if (session_id)
    g_free (session_id);
  session_id = g_strdup (gst_rtsp_session_get_sessionid (session));
  fail_unless (session_id != NULL);

  fail_unless (session != NULL);
  g_object_unref (session);

  g_object_unref (session_pool);

  return TRUE;
}

static gboolean
test_response_200 (GstRTSPClient * client, GstRTSPMessage * response,
    gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;

  fail_unless_equals_int (gst_rtsp_message_get_type (response),
      GST_RTSP_MESSAGE_RESPONSE);
  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless_equals_int (code, GST_RTSP_STS_OK);

  return TRUE;
}

typedef struct
{
  guint32 previous_ts;
  gint32 expected_ts_interval;
  gint32 expected_i_frame_ts_interval;
  guint expected_n_buffers;
  guint n_buffers;
  guint expected_n_i_frames;
  guint n_i_frames;
  guint expected_n_p_frames;
  guint n_p_frames;
  guint expected_n_b_frames;
  guint n_b_frames;
  guint expected_n_clean_points;
  guint n_clean_points;
  gboolean timestamped_rtcp;
} RTPCheckData;

#define EXTENSION_ID 0xABAC
#define EXTENSION_SIZE 3

static gboolean
read_length (guint8 * data, guint size, guint * length, guint * skip)
{
  guint b, len, offset;

  /* start reading the length, we need this to skip to the data later */
  len = offset = 0;
  do {
    if (offset >= size)
      return FALSE;
    b = data[offset++];
    len = (len << 7) | (b & 0x7f);
  } while (b & 0x80);

  /* check remaining buffer size */
  if (size - offset < len)
    return FALSE;

  *length = len;
  *skip = offset;

  return TRUE;
}

static GstCaps *
read_caps (GstBuffer * buf, guint * skip)
{
  guint offset, length;
  GstCaps *caps;
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_READ);

  if (!read_length (map.data, map.size, &length, &offset))
    goto too_small;

  if (length == 0 || map.data[offset + length - 1] != '\0')
    goto invalid_buffer;

  /* parse and store in cache */
  caps = gst_caps_from_string ((gchar *) & map.data[offset]);
  gst_buffer_unmap (buf, &map);

  *skip = length + offset;

  return caps;

too_small:
  {
    gst_buffer_unmap (buf, &map);
    return NULL;
  }
invalid_buffer:
  {
    gst_buffer_unmap (buf, &map);
    return NULL;
  }
}

static GstEvent *
read_event (guint type, GstBuffer * buf, guint * skip)
{
  guint offset, length;
  GstStructure *s;
  GstEvent *event;
  GstEventType etype;
  gchar *end;
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_READ);

  if (!read_length (map.data, map.size, &length, &offset))
    goto too_small;

  if (length == 0)
    goto invalid_buffer;
  /* backward compat, old payloader did not put 0-byte at the end */
  if (map.data[offset + length - 1] != '\0'
      && map.data[offset + length - 1] != ';')
    goto invalid_buffer;

  /* parse */
  s = gst_structure_from_string ((gchar *) & map.data[offset], &end);
  gst_buffer_unmap (buf, &map);

  if (s == NULL)
    goto parse_failed;

  switch (type) {
    case 1:
      etype = GST_EVENT_TAG;
      break;
    case 2:
      etype = GST_EVENT_CUSTOM_DOWNSTREAM;
      break;
    case 3:
      etype = GST_EVENT_CUSTOM_BOTH;
      break;
    case 4:
      etype = GST_EVENT_STREAM_START;
      break;
    default:
      goto unknown_event;
  }
  event = gst_event_new_custom (etype, s);

  *skip = length + offset;

  return event;

too_small:
  {
    gst_buffer_unmap (buf, &map);
    return NULL;
  }
invalid_buffer:
  {
    gst_buffer_unmap (buf, &map);
    return NULL;
  }
parse_failed:
  {
    return NULL;
  }
unknown_event:
  {
    gst_structure_free (s);
    return NULL;
  }
}

static gboolean
parse_gstpay_payload (GstRTPBuffer * rtp, GstEvent ** event, GstCaps ** caps,
    GstBuffer ** outbuf)
{
  gint payload_len;
  guint8 *payload;
  guint avail, offset;

  payload_len = gst_rtp_buffer_get_payload_len (rtp);

  if (payload_len <= 8)
    goto empty_packet;

  /* We don't need to deal with fragmentation */
  fail_unless (gst_rtp_buffer_get_marker (rtp));

  payload = gst_rtp_buffer_get_payload (rtp);

  *outbuf = gst_rtp_buffer_get_payload_subbuffer (rtp, 8, -1);
  avail = gst_buffer_get_size (*outbuf);
  offset = 0;

  if (payload[0] & 0x80) {
    guint size;

    /* C bit, we have inline caps */
    *caps = read_caps (*outbuf, &size);
    if (*caps == NULL)
      goto no_caps;

    /* skip caps */
    offset += size;
    avail -= size;
  }

  if (payload[1]) {
    guint size;

    /* we have an event */
    *event = read_event (payload[1], *outbuf, &size);
    if (*event == NULL)
      goto no_event;

    /* no buffer after event */
    avail = 0;
  }

  if (avail) {
    if (offset != 0) {
      GstBuffer *temp;

      temp =
          gst_buffer_copy_region (*outbuf, GST_BUFFER_COPY_ALL, offset, avail);

      gst_buffer_unref (*outbuf);
      *outbuf = temp;
    }

    if (payload[0] & 0x8)
      GST_BUFFER_FLAG_SET (*outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  }

  return TRUE;

empty_packet:
  return FALSE;

no_caps:
  {
    gst_buffer_unref (*outbuf);
    return FALSE;
  }
no_event:
  {
    gst_buffer_unref (*outbuf);
    return FALSE;
  }
}

static gboolean
test_play_response_200_and_check_data (GstRTSPClient * client,
    GstRTSPMessage * response, gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;
  RTPCheckData *check = (RTPCheckData *) user_data;

  /* We check data in the same send function because client->send_func cannot
   * be changed from client->send_func
   */
  if (gst_rtsp_message_get_type (response) == GST_RTSP_MESSAGE_DATA) {
    GstRTSPStreamTransport *trans;
    guint8 channel = 42;

    gst_rtsp_message_parse_data (response, &channel);
    fail_unless (trans =
        gst_rtsp_client_get_stream_transport (client, channel));

    if (channel == 0) {         /* RTP */
      GstBuffer *buf;
      GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
      guint8 *body = NULL;
      guint body_size;
      guint8 *data;
      guint16 bits;
      guint wordlen;
      guint8 flags;
      gint32 expected_interval = 0;
      GstBuffer *outbuf = NULL;
      GstCaps *outcaps = NULL;
      GstEvent *outevent = NULL;

      fail_unless (gst_rtsp_message_get_body (response, &body,
              &body_size) == GST_RTSP_OK);

      buf = gst_rtp_buffer_new_copy_data (body, body_size);

      fail_unless (gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp));

      fail_unless (parse_gstpay_payload (&rtp, &outevent, &outcaps, &outbuf));

      if (outbuf) {
        switch (gst_buffer_get_size (outbuf)) {
          case 20:
            expected_interval = check->expected_i_frame_ts_interval;
            check->n_i_frames += 1;
            break;
          case 10:
            expected_interval = check->expected_ts_interval;
            check->n_p_frames += 1;
            break;
          case 5:
            expected_interval = check->expected_ts_interval;
            check->n_b_frames += 1;
            break;
          default:
            fail ("Invalid payload size %u", gst_buffer_get_size (outbuf));
        }

        gst_buffer_unref (outbuf);
      }

      if (outcaps) {
        gst_caps_unref (outcaps);
      }

      if (outevent) {
        const GstStructure *s;

        fail_unless (GST_EVENT_TYPE (outevent) == GST_EVENT_CUSTOM_DOWNSTREAM);
        s = gst_event_get_structure (outevent);
        fail_unless (gst_structure_has_name (s, "GstOnvifTimestamp"));
        gst_event_unref (outevent);
      }

      if (expected_interval) {
        if (check->previous_ts) {
          fail_unless_equals_int (gst_rtp_buffer_get_timestamp (&rtp) -
              check->previous_ts, expected_interval);
        }

        check->previous_ts = gst_rtp_buffer_get_timestamp (&rtp);
        check->n_buffers += 1;

        fail_unless (gst_rtp_buffer_get_extension_data (&rtp, &bits,
                (gpointer) & data, &wordlen));

        fail_unless (bits == EXTENSION_ID && wordlen == EXTENSION_SIZE);

        flags = GST_READ_UINT8 (data + 8);

        if (flags & (1 << 7)) {
          check->n_clean_points += 1;
        }

        /* T flag is set, we are done */
        if (flags & (1 << 4)) {
          fail_unless_equals_int (check->expected_n_buffers, check->n_buffers);
          fail_unless_equals_int (check->expected_n_i_frames,
              check->n_i_frames);
          fail_unless_equals_int (check->expected_n_p_frames,
              check->n_p_frames);
          fail_unless_equals_int (check->expected_n_b_frames,
              check->n_b_frames);
          fail_unless_equals_int (check->expected_n_clean_points,
              check->n_clean_points);

          terminal_frame = TRUE;

        }
      }

      gst_rtp_buffer_unmap (&rtp);
      gst_buffer_unref (buf);
    } else if (channel == 1) {  /* RTCP */
      GstBuffer *buf;
      guint8 *body = NULL;
      guint body_size;
      GstRTCPPacket packet;
      GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
      guint32 ssrc, rtptime, packet_count, octet_count;
      guint64 ntptime;

      received_rtcp = TRUE;
      fail_unless (gst_rtsp_message_get_body (response, &body,
              &body_size) == GST_RTSP_OK);

      buf = gst_rtp_buffer_new_copy_data (body, body_size);
      gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
      gst_rtcp_buffer_get_first_packet (&rtcp, &packet);

      gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, &ntptime, &rtptime,
          &packet_count, &octet_count);

      if (check->timestamped_rtcp) {
        fail_unless (rtptime != 0);
        fail_unless (ntptime != 0);
      } else {
        fail_unless (rtptime == 0);
        fail_unless (ntptime == 0);
      }

      gst_rtcp_buffer_unmap (&rtcp);
      gst_buffer_unref (buf);
    }

    gst_rtsp_stream_transport_message_sent (trans);

    if (terminal_frame && received_rtcp) {
      g_mutex_lock (&check_mutex);
      g_cond_broadcast (&check_cond);
      g_mutex_unlock (&check_mutex);
    }

    return TRUE;
  }

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OK);

  return TRUE;
}

static gboolean
test_teardown_response_200 (GstRTSPClient * client,
    GstRTSPMessage * response, gboolean close, gpointer user_data)
{
  GstRTSPStatusCode code;
  const gchar *reason;
  GstRTSPVersion version;

  /* We might still be seeing stray RTCP messages */
  if (gst_rtsp_message_get_type (response) == GST_RTSP_MESSAGE_DATA)
    return TRUE;

  fail_unless (gst_rtsp_message_get_type (response) ==
      GST_RTSP_MESSAGE_RESPONSE);

  fail_unless (gst_rtsp_message_parse_response (response, &code, &reason,
          &version)
      == GST_RTSP_OK);
  fail_unless (code == GST_RTSP_STS_OK);
  fail_unless (g_str_equal (reason, "OK"));
  fail_unless (version == GST_RTSP_VERSION_1_0);

  return TRUE;
}

static void
send_teardown (GstRTSPClient * client)
{
  GstRTSPMessage request = { 0, };
  gchar *str;

  fail_unless (session_id != NULL);
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_TEARDOWN,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_client_set_send_func (client, test_teardown_response_200,
      NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);
  g_free (session_id);
  session_id = NULL;
}

static GstRTSPClient *
setup_client (const gchar * launch_line)
{
  GstRTSPClient *client;
  GstRTSPSessionPool *session_pool;
  GstRTSPMountPoints *mount_points;
  GstRTSPMediaFactory *factory;
  GstRTSPThreadPool *thread_pool;

  client = gst_rtsp_onvif_client_new ();

  session_pool = gst_rtsp_session_pool_new ();
  gst_rtsp_client_set_session_pool (client, session_pool);

  mount_points = gst_rtsp_mount_points_new ();
  factory = test_media_factory_new ();

  gst_rtsp_media_factory_set_media_gtype (factory, GST_TYPE_RTSP_ONVIF_MEDIA);

  gst_rtsp_mount_points_add_factory (mount_points, "/test", factory);
  gst_rtsp_client_set_mount_points (client, mount_points);

  thread_pool = gst_rtsp_thread_pool_new ();
  gst_rtsp_client_set_thread_pool (client, thread_pool);

  g_object_unref (mount_points);
  g_object_unref (session_pool);
  g_object_unref (thread_pool);

  return client;
}

static void
teardown_client (GstRTSPClient * client)
{
  gst_rtsp_client_set_thread_pool (client, NULL);
  g_object_unref (client);
}

/**
 * https://www.onvif.org/specs/stream/ONVIF-Streaming-Spec.pdf
 * 6.2 RTSP describe
 */
GST_START_TEST (test_x_onvif_track)
{
  GstRTSPClient *client;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_client (NULL);
  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_x_onvif_track, NULL,
      NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  teardown_client (client);
}

GST_END_TEST;

static void
create_connection (GstRTSPConnection ** conn)
{
  GSocket *sock;
  GError *error = NULL;

  sock = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_TCP, &error);
  g_assert_no_error (error);
  fail_unless (gst_rtsp_connection_create_from_socket (sock, "127.0.0.1", 444,
          NULL, conn) == GST_RTSP_OK);
  g_object_unref (sock);
}

static void
test_seek (const gchar * range, const gchar * speed, const gchar * scale,
    const gchar * frames, const gchar * rate_control, RTPCheckData * rtp_check)
{
  GstRTSPClient *client;
  GstRTSPConnection *conn;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_client (NULL);
  create_connection (&conn);
  fail_unless (gst_rtsp_client_set_connection (client, conn));

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP/TCP;unicast");

  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SESSION, session_id);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_RANGE, range);

  if (scale) {
    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SCALE, scale);
  }

  if (speed) {
    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SPEED, speed);
  }

  if (frames) {
    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_FRAMES, frames);
  }

  if (rate_control) {
    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_RATE_CONTROL,
        rate_control);
  }

  gst_rtsp_client_set_send_func (client, test_play_response_200_and_check_data,
      rtp_check, NULL);

  terminal_frame = FALSE;
  received_rtcp = FALSE;

  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  g_mutex_lock (&check_mutex);
  while (!terminal_frame || !received_rtcp)
    g_cond_wait (&check_cond, &check_mutex);
  g_mutex_unlock (&check_mutex);

  send_teardown (client);

  teardown_client (client);
}

GST_START_TEST (test_src_seek_simple)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 90;
  rtp_check.expected_i_frame_ts_interval = 90;
  rtp_check.expected_n_buffers = 100;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 80;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", NULL, NULL, NULL,
      NULL, &rtp_check);
}

GST_END_TEST;

/**
 * https://www.onvif.org/specs/stream/ONVIF-Streaming-Spec.pdf
 * 6.4 RTSP Feature Tag
 */
GST_START_TEST (test_onvif_replay)
{
  GstRTSPClient *client;
  GstRTSPConnection *conn;
  GstRTSPMessage request = { 0, };
  gchar *str;

  client = setup_client (NULL);
  create_connection (&conn);
  fail_unless (gst_rtsp_client_set_connection (client, conn));

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
          "rtsp://localhost/test") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CSEQ, str);
  g_free (str);

  gst_rtsp_client_set_send_func (client, test_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  fail_unless (gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
          "rtsp://localhost/test/stream=0") == GST_RTSP_OK);
  str = g_strdup_printf ("%d", cseq);
  gst_rtsp_message_take_header (&request, GST_RTSP_HDR_CSEQ, str);
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT,
      "RTP/AVP/TCP;unicast");
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_REQUIRE, "onvif-replay");

  gst_rtsp_client_set_send_func (client, test_setup_response_200, NULL, NULL);
  fail_unless (gst_rtsp_client_handle_message (client,
          &request) == GST_RTSP_OK);
  gst_rtsp_message_unset (&request);

  send_teardown (client);
  teardown_client (client);
}

GST_END_TEST;

GST_START_TEST (test_speed_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 45;
  rtp_check.expected_i_frame_ts_interval = 45;
  rtp_check.expected_n_buffers = 100;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 80;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", "2.0", NULL, NULL,
      NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_scale_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 90;
  rtp_check.expected_i_frame_ts_interval = 90;
  rtp_check.expected_n_buffers = 50;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 5;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 5;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 40;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 5;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", NULL, "2.0", NULL,
      NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_intra_frames_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 900;
  rtp_check.expected_i_frame_ts_interval = 900;
  rtp_check.expected_n_buffers = 10;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 0;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 0;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", NULL, NULL,
      "intra", NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_intra_frames_with_interval_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 1800;
  rtp_check.expected_i_frame_ts_interval = 1800;
  rtp_check.expected_n_buffers = 5;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 5;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 0;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 0;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 5;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", NULL, NULL,
      "intra/20", NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_predicted_frames_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 450;
  rtp_check.expected_i_frame_ts_interval = 450;
  rtp_check.expected_n_buffers = 20;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 0;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", NULL, NULL,
      "predicted", NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_reverse_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = -90;
  rtp_check.expected_i_frame_ts_interval = 1710;
  rtp_check.expected_n_buffers = 100;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 80;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.10Z-19000101T010000.00Z", NULL, "-1.0",
      NULL, NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_speed_reverse_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = -45;
  rtp_check.expected_i_frame_ts_interval = 855;
  rtp_check.expected_n_buffers = 100;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 80;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010000.10Z-19000101T010000.00Z", "2.0", "-1.0",
      NULL, NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_scale_reverse_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = -90;
  rtp_check.expected_i_frame_ts_interval = 1710;
  rtp_check.expected_n_buffers = 50;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 5;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 5;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 40;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 5;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010001.10Z-19000101T010001.00Z", NULL, "-2.0",
      NULL, NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_intra_frames_reverse_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 0;
  rtp_check.expected_i_frame_ts_interval = 900;
  rtp_check.expected_n_buffers = 10;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 0;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 0;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010001.10Z-19000101T010001.00Z", NULL, "-1.0",
      "intra", NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_predicted_frames_reverse_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = -450;
  rtp_check.expected_i_frame_ts_interval = 1350;
  rtp_check.expected_n_buffers = 20;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 0;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010001.10Z-19000101T010001.00Z", NULL, "-1.0",
      "predicted", NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_intra_frames_with_interval_reverse_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 0;
  rtp_check.expected_i_frame_ts_interval = 1800;
  rtp_check.expected_n_buffers = 5;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 5;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 0;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 0;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 5;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = TRUE;

  test_seek ("clock=19000101T010001.10Z-19000101T010001.00Z", NULL, "-1.0",
      "intra/20", NULL, &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_rate_control_no_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 90;
  rtp_check.expected_i_frame_ts_interval = 90;
  rtp_check.expected_n_buffers = 100;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 80;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = FALSE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", NULL, NULL, NULL,
      "no", &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_rate_control_no_reverse_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 90;
  rtp_check.expected_i_frame_ts_interval = -1710;
  rtp_check.expected_n_buffers = 100;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 10;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 80;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = FALSE;

  test_seek ("clock=19000101T010000.10Z-19000101T010000.00Z", NULL, "-1.0",
      NULL, "no", &rtp_check);
}

GST_END_TEST;

GST_START_TEST (test_rate_control_no_frames_trick_mode)
{
  RTPCheckData rtp_check;

  rtp_check.previous_ts = 0;
  rtp_check.expected_ts_interval = 900;
  rtp_check.expected_i_frame_ts_interval = 900;
  rtp_check.expected_n_buffers = 10;
  rtp_check.n_buffers = 0;
  rtp_check.expected_n_i_frames = 10;
  rtp_check.n_i_frames = 0;
  rtp_check.expected_n_p_frames = 0;
  rtp_check.n_p_frames = 0;
  rtp_check.expected_n_b_frames = 0;
  rtp_check.n_b_frames = 0;
  rtp_check.expected_n_clean_points = 10;
  rtp_check.n_clean_points = 0;
  rtp_check.timestamped_rtcp = FALSE;

  test_seek ("clock=19000101T010000.00Z-19000101T010000.10Z", NULL, NULL,
      "intra", "no", &rtp_check);
}

GST_END_TEST;
static Suite *
onvif_suite (void)
{
  Suite *s = suite_create ("onvif");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, test_x_onvif_track);
  tcase_add_test (tc, test_onvif_replay);
  tcase_add_test (tc, test_src_seek_simple);
  tcase_add_test (tc, test_speed_trick_mode);
  tcase_add_test (tc, test_scale_trick_mode);
  tcase_add_test (tc, test_intra_frames_trick_mode);
  tcase_add_test (tc, test_predicted_frames_trick_mode);
  tcase_add_test (tc, test_intra_frames_with_interval_trick_mode);
  tcase_add_test (tc, test_reverse_trick_mode);
  tcase_add_test (tc, test_speed_reverse_trick_mode);
  tcase_add_test (tc, test_scale_reverse_trick_mode);
  tcase_add_test (tc, test_intra_frames_reverse_trick_mode);
  tcase_add_test (tc, test_predicted_frames_reverse_trick_mode);
  tcase_add_test (tc, test_intra_frames_with_interval_reverse_trick_mode);
  tcase_add_test (tc, test_rate_control_no_trick_mode);
  tcase_add_test (tc, test_rate_control_no_reverse_trick_mode);
  tcase_add_test (tc, test_rate_control_no_frames_trick_mode);

  return s;
}

GST_CHECK_MAIN (onvif);
