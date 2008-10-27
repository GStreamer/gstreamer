/* GStreamer
 *
 * Copyright (c) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* TODO:
 *   - Write metadata for the file, see FLV spec page 13
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gstflvmux.h"

GST_DEBUG_CATEGORY_STATIC (flvmux_debug);
#define GST_CAT_DEFAULT flvmux_debug

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-flv")
    );

static GstStaticPadTemplate videosink_templ = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-flash-video; "
        "video/x-flash-screen; "
        "video/x-vp6-flash; " "video/x-vp6-alpha; " "video/x-h264;")
    );

static GstStaticPadTemplate audiosink_templ = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS
    ("audio/x-adpcm, layout = (string) swf, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; "
        "audio/mpeg, mpegversion = (int) 1, layer = (int) 3, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 22050, 44100 }; "
        "audio/mpeg, mpegversion = (int) 4; "
        "audio/x-nellymoser, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 16000, 22050, 44100 }; "
        "audio/x-raw-int, endianness = (int) LITTLE_ENDIAN, channels = (int) { 1, 2 }, width = (int) 8, depth = (int) 8, rate = (int) { 5512, 11025, 22050, 44100 }, signed = (boolean) FALSE; "
        "audio/x-raw-int, endianness = (int) LITTLE_ENDIAN, channels = (int) { 1, 2 }, width = (int) 16, depth = (int) 16, rate = (int) { 5512, 11025, 22050, 44100 }, signed = (boolean) TRUE; "
        "audio/x-alaw, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; "
        "audio/x-mulaw, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; "
        "audio/x-speex, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 };")
    );

GST_BOILERPLATE (GstFlvMux, gst_flv_mux, GstElement, GST_TYPE_ELEMENT);

static void gst_flv_mux_finalize (GObject * object);
static GstFlowReturn
gst_flv_mux_collected (GstCollectPads * pads, gpointer user_data);

static gboolean gst_flv_mux_handle_src_event (GstPad * pad, GstEvent * event);
static GstPad *gst_flv_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_flv_mux_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn
gst_flv_mux_change_state (GstElement * element, GstStateChange transition);

static void gst_flv_mux_reset (GstElement * element);

static void
gst_flv_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&videosink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audiosink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_set_details_simple (element_class, "FLV muxer",
      "Codec/Muxer",
      "Muxes video/audio streams into a FLV stream",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (flvmux_debug, "flvmux", 0, "FLV muxer");
}

static void
gst_flv_mux_class_init (GstFlvMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  GST_DEBUG_CATEGORY_INIT (flvmux_debug, "flvmux", 0, "FLV muxer");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_flv_mux_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_flv_mux_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_flv_mux_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_flv_mux_release_pad);
}

static void
gst_flv_mux_init (GstFlvMux * mux, GstFlvMuxClass * g_class)
{
  mux->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (mux->srcpad, gst_flv_mux_handle_src_event);
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (mux->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_flv_mux_collected), mux);

  gst_flv_mux_reset (GST_ELEMENT (mux));
}

static void
gst_flv_mux_finalize (GObject * object)
{
  GstFlvMux *mux = GST_FLV_MUX (object);

  gst_object_unref (mux->collect);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_flv_mux_reset (GstElement * element)
{
  GstFlvMux *mux = GST_FLV_MUX (element);
  GSList *sl;

  while ((sl = mux->collect->data) != NULL) {
    GstFlvPad *cpad = (GstFlvPad *) sl->data;

    if (cpad->audio_codec_data)
      gst_buffer_unref (cpad->audio_codec_data);
    if (cpad->video_codec_data)
      gst_buffer_unref (cpad->video_codec_data);

    gst_collect_pads_remove_pad (mux->collect, cpad->collect.pad);
  }

  mux->state = GST_FLV_MUX_STATE_HEADER;
}

static gboolean
gst_flv_mux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstEventType type;

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      /* disable seeking for now */
      return FALSE;
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static gboolean
gst_flv_mux_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstFlvMux *mux = GST_FLV_MUX (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      /* TODO do something with the tags */
      break;
    case GST_EVENT_NEWSEGMENT:
      /* We don't support NEWSEGMENT events */
      ret = FALSE;
      gst_event_unref (event);
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  if (ret)
    ret = mux->collect_event (pad, event);
  gst_object_unref (mux);

  return ret;
}

static gboolean
gst_flv_mux_video_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFlvMux *mux = GST_FLV_MUX (gst_pad_get_parent (pad));
  GstFlvPad *cpad = (GstFlvPad *) gst_pad_get_element_private (pad);
  gboolean ret = TRUE;
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);

  if (strcmp (gst_structure_get_name (s), "video/x-flash-video") == 0) {
    cpad->video_codec = 2;
  } else if (strcmp (gst_structure_get_name (s), "video/x-flash-screen") == 0) {
    cpad->video_codec = 3;
  } else if (strcmp (gst_structure_get_name (s), "video/x-vp6-flash") == 0) {
    cpad->video_codec = 4;
  } else if (strcmp (gst_structure_get_name (s), "video/x-vp6-alpha") == 0) {
    cpad->video_codec = 5;
  } else if (strcmp (gst_structure_get_name (s), "video/x-h264") == 0) {
    cpad->video_codec = 7;
  } else {
    ret = FALSE;
  }

  if (ret && gst_structure_has_field (s, "codec_data")) {
    const GValue *val = gst_structure_get_value (s, "codec_data");

    if (val) {
      cpad->video_codec_data = gst_buffer_ref (gst_value_get_buffer (val));
      cpad->sent_codec_data = FALSE;
    } else {
      cpad->sent_codec_data = TRUE;
    }
  } else {
    cpad->sent_codec_data = TRUE;
  }

  gst_object_unref (mux);

  return ret;
}

static gboolean
gst_flv_mux_audio_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFlvMux *mux = GST_FLV_MUX (gst_pad_get_parent (pad));
  GstFlvPad *cpad = (GstFlvPad *) gst_pad_get_element_private (pad);
  gboolean ret = TRUE;
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);

  if (strcmp (gst_structure_get_name (s), "audio/x-adpcm") == 0) {
    const gchar *layout = gst_structure_get_string (s, "layout");
    if (layout && strcmp (layout, "swf") == 0) {
      cpad->audio_codec = 1;
    } else {
      ret = FALSE;
    }
  } else if (strcmp (gst_structure_get_name (s), "audio/mpeg") == 0) {
    gint mpegversion;

    if (gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      if (mpegversion == 1) {
        gint layer;

        if (gst_structure_get_int (s, "layer", &layer) && layer == 3) {
          gint rate;

          if (gst_structure_get_int (s, "rate", &rate) && rate == 8000)
            cpad->audio_codec = 14;
          else
            cpad->audio_codec = 2;
        } else {
          ret = FALSE;
        }
      } else if (mpegversion == 4) {
        cpad->audio_codec = 10;
      } else {
        ret = FALSE;
      }
    } else {
      ret = FALSE;
    }
  } else if (strcmp (gst_structure_get_name (s), "audio/x-nellymoser") == 0) {
    gint rate, channels;

    if (gst_structure_get_int (s, "rate", &rate)
        && gst_structure_get_int (s, "channels", &channels)) {
      if (channels == 1 && rate == 16000)
        cpad->audio_codec = 4;
      else if (channels == 1 && rate == 8000)
        cpad->audio_codec = 5;
    } else {
      cpad->audio_codec = 6;
    }
  } else if (strcmp (gst_structure_get_name (s), "audio/x-raw-int") == 0) {
    gint endianness;

    if (gst_structure_get_int (s, "endianness", &endianness)
        && endianness == G_LITTLE_ENDIAN)
      cpad->audio_codec = 3;
    else
      ret = FALSE;
  } else if (strcmp (gst_structure_get_name (s), "audio/x-alaw") == 0) {
    cpad->audio_codec = 7;
  } else if (strcmp (gst_structure_get_name (s), "audio/x-mulaw") == 0) {
    cpad->audio_codec = 8;
  } else if (strcmp (gst_structure_get_name (s), "audio/x-speex") == 0) {
    cpad->audio_codec = 11;
  } else {
    ret = FALSE;
  }

  if (ret) {
    gint rate, channels, width;

    if (gst_structure_get_int (s, "rate", &rate)) {
      if (cpad->audio_codec == 10)
        cpad->rate = 3;
      else if (rate == 5512)
        cpad->rate = 0;
      else if (rate == 11025)
        cpad->rate = 1;
      else if (rate == 22050)
        cpad->rate = 2;
      else if (rate == 44100)
        cpad->rate = 3;
      else if (rate == 8000 && (cpad->audio_codec == 5
              || cpad->audio_codec == 14))
        cpad->rate = 0;
      else if (rate == 16000 && cpad->audio_codec == 4)
        cpad->rate = 0;
      else
        ret = FALSE;
    } else if (cpad->audio_codec == 10) {
      cpad->rate = 3;
    } else {
      ret = FALSE;
    }

    if (gst_structure_get_int (s, "channels", &channels)) {
      if (cpad->audio_codec == 4 || cpad->audio_codec == 5
          || cpad->audio_codec == 6)
        cpad->channels = 0;
      else if (cpad->audio_codec == 10)
        cpad->channels = 1;
      else if (channels == 1)
        cpad->channels = 0;
      else if (channels == 2)
        cpad->channels = 1;
      else
        ret = FALSE;
    } else if (cpad->audio_codec == 4 || cpad->audio_codec == 5
        || cpad->audio_codec == 6) {
      cpad->channels = 0;
    } else if (cpad->audio_codec == 10) {
      cpad->channels = 1;
    } else {
      ret = FALSE;
    }

    if (gst_structure_get_int (s, "width", &width)) {
      if (cpad->audio_codec != 3)
        cpad->width = 1;
      else if (width == 8)
        cpad->width = 0;
      else if (width == 16)
        cpad->width = 1;
      else
        ret = FALSE;
    } else if (cpad->audio_codec != 3) {
      cpad->width = 1;
    } else {
      ret = FALSE;
    }
  }

  if (ret && gst_structure_has_field (s, "codec_data")) {
    const GValue *val = gst_structure_get_value (s, "codec_data");

    if (val) {
      cpad->audio_codec_data = gst_buffer_ref (gst_value_get_buffer (val));
      cpad->sent_codec_data = FALSE;
    } else {
      cpad->sent_codec_data = TRUE;
    }
  } else {
    cpad->sent_codec_data = TRUE;
  }

  gst_object_unref (mux);

  return ret;
}

static GstPad *
gst_flv_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * pad_name)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstFlvMux *mux = GST_FLV_MUX (element);
  GstFlvPad *cpad;
  GstPad *pad = NULL;
  const gchar *name = NULL;
  GstPadSetCapsFunction setcapsfunc = NULL;
  gboolean video;

  if (mux->state != GST_FLV_MUX_STATE_HEADER) {
    GST_WARNING_OBJECT (mux, "Can't request pads after writing header");
    return NULL;
  }

  if (templ == gst_element_class_get_pad_template (klass, "audio")) {
    if (mux->have_audio) {
      GST_WARNING_OBJECT (mux, "Already have an audio pad");
      return NULL;
    }
    mux->have_audio = TRUE;
    name = "audio";
    video = FALSE;
    setcapsfunc = GST_DEBUG_FUNCPTR (gst_flv_mux_audio_pad_setcaps);
  } else if (templ == gst_element_class_get_pad_template (klass, "video")) {
    if (mux->have_video) {
      GST_WARNING_OBJECT (mux, "Already have a video pad");
      return NULL;
    }
    mux->have_video = TRUE;
    name = "video";
    video = TRUE;
    setcapsfunc = GST_DEBUG_FUNCPTR (gst_flv_mux_video_pad_setcaps);
  } else {
    GST_WARNING_OBJECT (mux, "Invalid template");
    return NULL;
  }

  pad = gst_pad_new_from_template (templ, name);
  cpad = (GstFlvPad *)
      gst_collect_pads_add_pad (mux->collect, pad, sizeof (GstFlvPad));

  cpad->video = video;

  cpad->audio_codec = G_MAXUINT;
  cpad->rate = G_MAXUINT;
  cpad->width = G_MAXUINT;
  cpad->channels = G_MAXUINT;
  cpad->audio_codec_data = NULL;

  cpad->video_codec = G_MAXUINT;
  cpad->video_codec_data = NULL;

  cpad->sent_codec_data = FALSE;

  cpad->last_timestamp = 0;

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events.
   */
  mux->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (pad);
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_flv_mux_handle_sink_event));

  gst_pad_set_setcaps_function (pad, setcapsfunc);
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (element, pad);

  return pad;
}

static void
gst_flv_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstFlvMux *mux = GST_FLV_MUX (GST_PAD_PARENT (pad));
  GstFlvPad *cpad = (GstFlvPad *) gst_pad_get_element_private (pad);

  if (cpad && cpad->audio_codec_data)
    gst_buffer_unref (cpad->audio_codec_data);
  if (cpad && cpad->video_codec_data)
    gst_buffer_unref (cpad->video_codec_data);

  gst_collect_pads_remove_pad (mux->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstFlowReturn
gst_flv_mux_write_header (GstFlvMux * mux)
{
  GstBuffer *header = gst_buffer_new_and_alloc (9 + 4);
  guint8 *data = GST_BUFFER_DATA (header);

  if (GST_PAD_CAPS (mux->srcpad) == NULL) {
    GstCaps *caps = gst_caps_new_simple ("video/x-flv", NULL);

    gst_pad_set_caps (mux->srcpad, caps);
    gst_caps_unref (caps);
  }
  gst_buffer_set_caps (header, GST_PAD_CAPS (mux->srcpad));

  data[0] = 'F';
  data[1] = 'L';
  data[2] = 'V';
  data[3] = 0x01;               /* Version */

  data[4] = (mux->have_audio << 2) | mux->have_video;   /* flags */
  GST_WRITE_UINT32_BE (data + 5, 9);    /* data offset */

  GST_WRITE_UINT32_BE (data + 9, 0);    /* previous tag size */

  return gst_pad_push (mux->srcpad, header);
}

static GstFlowReturn
gst_flv_mux_write_buffer (GstFlvMux * mux, GstFlvPad * cpad)
{
  GstBuffer *tag;
  guint8 *data;
  guint size;
  GstBuffer *buffer =
      gst_collect_pads_pop (mux->collect, (GstCollectData *) cpad);
  guint32 timestamp =
      (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) ? GST_BUFFER_TIMESTAMP (buffer) /
      GST_MSECOND : cpad->last_timestamp / GST_MSECOND;
  gboolean second_run = FALSE;
  GstFlowReturn ret;

next:
  size = 11;
  if (cpad->video) {
    size += 1;
    if (cpad->video_codec == 7 && !cpad->sent_codec_data)
      size += 4 + GST_BUFFER_SIZE (cpad->video_codec_data);
    else if (cpad->video_codec == 7)
      size += 4 + GST_BUFFER_SIZE (buffer);
    else
      size += GST_BUFFER_SIZE (buffer);
  } else {
    size += 1;
    if (cpad->audio_codec == 10 && !cpad->sent_codec_data)
      size += 1 + GST_BUFFER_SIZE (cpad->audio_codec_data);
    else if (cpad->audio_codec == 10)
      size += 1 + GST_BUFFER_SIZE (buffer);
    else
      size += GST_BUFFER_SIZE (buffer);
  }
  size += 4;

  tag = gst_buffer_new_and_alloc (size);
  data = GST_BUFFER_DATA (tag);
  memset (data, 0, size);

  data[0] = (cpad->video) ? 9 : 8;

  data[1] = ((size - 11 - 4) >> 16) & 0xff;
  data[2] = ((size - 11 - 4) >> 8) & 0xff;
  data[3] = ((size - 11 - 4) >> 0) & 0xff;

  data[4] = (timestamp >> 16) & 0xff;
  data[5] = (timestamp >> 8) & 0xff;
  data[6] = (timestamp >> 0) & 0xff;
  data[7] = (timestamp >> 24) & 0xff;

  data[8] = data[9] = data[10] = 0;

  if (cpad->video) {
    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
      data[11] |= 2 << 4;
    else
      data[11] |= 1 << 4;

    data[11] |= cpad->video_codec & 0x0f;

    if (cpad->video_codec == 7 && !cpad->sent_codec_data) {
      data[12] = 0;
      data[13] = data[14] = data[15] = 0;

      memcpy (data + 11 + 1 + 4, GST_BUFFER_DATA (cpad->video_codec_data),
          GST_BUFFER_SIZE (cpad->video_codec_data));
      second_run = TRUE;
    } else if (cpad->video_codec == 7) {
      data[12] = 1;

      /* FIXME: what to do about composition time */
      data[13] = data[14] = data[15] = 0;

      memcpy (data + 11 + 1 + 4, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer));
    } else {
      memcpy (data + 11 + 1, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer));
    }
  } else {
    data[11] |= (cpad->audio_codec << 4) & 0xf0;
    data[11] |= (cpad->rate << 2) & 0x0c;
    data[11] |= (cpad->width << 1) & 0x02;
    data[11] |= (cpad->channels << 0) & 0x01;

    if (cpad->audio_codec == 10 && !cpad->sent_codec_data) {
      data[12] = 0;

      memcpy (data + 11 + 1 + 1, GST_BUFFER_DATA (cpad->audio_codec_data),
          GST_BUFFER_SIZE (cpad->audio_codec_data));
      second_run = TRUE;
    } else if (cpad->audio_codec == 10) {
      data[12] = 1;

      memcpy (data + 11 + 1 + 1, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer));
    } else {
      memcpy (data + 11 + 1, GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer));
    }
  }

  GST_WRITE_UINT32_BE (data + size - 4, size - 4);

  gst_buffer_set_caps (tag, GST_PAD_CAPS (mux->srcpad));

  if (second_run) {
    second_run = FALSE;
    cpad->sent_codec_data = TRUE;

    ret = gst_pad_push (mux->srcpad, tag);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buffer);
      return ret;
    }

    cpad->last_timestamp = timestamp;

    tag = NULL;
    goto next;
  }

  gst_buffer_copy_metadata (tag, buffer, GST_BUFFER_COPY_TIMESTAMPS);
  GST_BUFFER_OFFSET (tag) = GST_BUFFER_OFFSET_END (tag) =
      GST_BUFFER_OFFSET_NONE;

  gst_buffer_unref (buffer);

  ret = gst_pad_push (mux->srcpad, tag);

  if (ret == GST_FLOW_OK)
    cpad->last_timestamp = timestamp;

  return ret;
}

static GstFlowReturn
gst_flv_mux_collected (GstCollectPads * pads, gpointer user_data)
{
  GstFlvMux *mux = GST_FLV_MUX (user_data);
  GstFlvPad *best;
  GstClockTime best_time;
  GstFlowReturn ret;
  GSList *sl;
  gboolean eos = TRUE;

  if (mux->state == GST_FLV_MUX_STATE_HEADER) {
    if (mux->collect->data == NULL) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("No input streams configured"));
      return GST_FLOW_ERROR;
    }

    if (gst_pad_push_event (mux->srcpad,
            gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0)))
      ret = gst_flv_mux_write_header (mux);
    else
      ret = GST_FLOW_ERROR;

    if (ret != GST_FLOW_OK)
      return ret;
    mux->state = GST_FLV_MUX_STATE_DATA;
  }

  best = NULL;
  best_time = GST_CLOCK_TIME_NONE;
  for (sl = mux->collect->data; sl; sl = sl->next) {
    GstFlvPad *cpad = sl->data;
    GstBuffer *buffer = gst_collect_pads_peek (pads, (GstCollectData *) cpad);
    GstClockTime time;

    if (!buffer)
      continue;

    eos = FALSE;

    time = GST_BUFFER_TIMESTAMP (buffer);
    gst_buffer_unref (buffer);

    /* Use buffers without valid timestamp first */
    if (!GST_CLOCK_TIME_IS_VALID (time)) {
      GST_WARNING_OBJECT (pads, "Buffer without valid timestamp");

      best_time = cpad->last_timestamp;
      best = cpad;
      break;
    }


    if (best == NULL || (GST_CLOCK_TIME_IS_VALID (best_time)
            && time < best_time)) {
      best = cpad;
      best_time = time;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (best_time)
      && best_time / GST_MSECOND > G_MAXUINT32) {
    GST_WARNING_OBJECT (mux, "Timestamp larger than FLV supports - EOS");
    eos = TRUE;
  }

  if (!eos && best) {
    return gst_flv_mux_write_buffer (mux, best);
  } else if (eos) {
    gst_pad_push_event (mux->srcpad, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  } else {
    return GST_FLOW_OK;
  }
}

static GstStateChangeReturn
gst_flv_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstFlvMux *mux = GST_FLV_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (mux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (mux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_flv_mux_reset (GST_ELEMENT (mux));
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
