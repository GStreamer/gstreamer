/* GStreamer
 *
 * Copyright (c) 2008,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-flvmux
 *
 * flvmux muxes different streams into an FLV file.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v flvmux name=mux ! filesink location=test.flv  audiotestsrc samplesperbuffer=44100 num-buffers=10 ! faac ! mux.  videotestsrc num-buffers=250 ! video/x-raw,framerate=25/1 ! x264enc ! mux.
 * ]| This pipeline encodes a test audio and video stream and muxes both into an FLV file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include <gst/audio/audio.h>

#include "gstflvmux.h"
#include "amfdefs.h"

GST_DEBUG_CATEGORY_STATIC (flvmux_debug);
#define GST_CAT_DEFAULT flvmux_debug

enum
{
  PROP_0,
  PROP_STREAMABLE,
  PROP_METADATACREATOR
};

#define DEFAULT_STREAMABLE FALSE
#define MAX_INDEX_ENTRIES 128
#define DEFAULT_METADATACREATOR "GStreamer " PACKAGE_VERSION " FLV muxer"

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
        "video/x-vp6-flash; " "video/x-vp6-alpha; "
        "video/x-h264, stream-format=avc;")
    );

static GstStaticPadTemplate audiosink_templ = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS
    ("audio/x-adpcm, layout = (string) swf, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; "
        "audio/mpeg, mpegversion = (int) 1, layer = (int) 3, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 22050, 44100 }, parsed = (boolean) TRUE; "
        "audio/mpeg, mpegversion = (int) { 4, 2 }, stream-format = (string) raw; "
        "audio/x-nellymoser, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 16000, 22050, 44100 }; "
        "audio/x-raw, format = (string) { U8, S16LE}, layout = (string) interleaved, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; "
        "audio/x-alaw, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; "
        "audio/x-mulaw, channels = (int) { 1, 2 }, rate = (int) { 5512, 11025, 22050, 44100 }; "
        "audio/x-speex, channels = (int) 1, rate = (int) 16000;")
    );

#define gst_flv_mux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstFlvMux, gst_flv_mux, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL));

static void gst_flv_mux_finalize (GObject * object);
static GstFlowReturn
gst_flv_mux_handle_buffer (GstCollectPads * pads, GstCollectData * cdata,
    GstBuffer * buf, gpointer user_data);
static gboolean
gst_flv_mux_handle_sink_event (GstCollectPads * pads, GstCollectData * data,
    GstEvent * event, gpointer user_data);

static gboolean gst_flv_mux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstPad *gst_flv_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps);
static void gst_flv_mux_release_pad (GstElement * element, GstPad * pad);

static gboolean gst_flv_mux_video_pad_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_flv_mux_audio_pad_setcaps (GstPad * pad, GstCaps * caps);

static void gst_flv_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_flv_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_flv_mux_change_state (GstElement * element, GstStateChange transition);

static void gst_flv_mux_reset (GstElement * element);
static void gst_flv_mux_reset_pad (GstFlvMux * mux, GstFlvPad * pad,
    gboolean video);

typedef struct
{
  gdouble position;
  gdouble time;
} GstFlvMuxIndexEntry;

static void
gst_flv_mux_index_entry_free (GstFlvMuxIndexEntry * entry)
{
  g_slice_free (GstFlvMuxIndexEntry, entry);
}

static GstBuffer *
_gst_buffer_new_wrapped (gpointer mem, gsize size, GFreeFunc free_func)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (free_func ? 0 : GST_MEMORY_FLAG_READONLY,
          mem, size, 0, size, mem, free_func));

  return buf;
}

static void
_gst_buffer_new_and_alloc (gsize size, GstBuffer ** buffer, guint8 ** data)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (buffer != NULL);

  *data = g_malloc (size);
  *buffer = _gst_buffer_new_wrapped (*data, size, g_free);
}

static GstFlowReturn
gst_flv_mux_clip_running_time (GstCollectPads * pads,
    GstCollectData * cdata, GstBuffer * buf, GstBuffer ** outbuf,
    gpointer user_data)
{
  buf = gst_buffer_make_writable (buf);

  if (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buf)))
    GST_BUFFER_PTS (buf) = GST_BUFFER_DTS (buf);

  return gst_collect_pads_clip_running_time (pads, cdata, buf, outbuf,
      user_data);
}

static void
gst_flv_mux_class_init (GstFlvMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  GST_DEBUG_CATEGORY_INIT (flvmux_debug, "flvmux", 0, "FLV muxer");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_flv_mux_get_property;
  gobject_class->set_property = gst_flv_mux_set_property;
  gobject_class->finalize = gst_flv_mux_finalize;

  /* FIXME: ideally the right mode of operation should be detected
   * automatically using queries when parameter not specified. */
  /**
   * GstFlvMux:streamable
   *
   * If True, the output will be streaming friendly. (ie without indexes and
   * duration)
   */
  g_object_class_install_property (gobject_class, PROP_STREAMABLE,
      g_param_spec_boolean ("streamable", "streamable",
          "If set to true, the output should be as if it is to be streamed "
          "and hence no indexes written or duration written.",
          DEFAULT_STREAMABLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_METADATACREATOR,
      g_param_spec_string ("metadatacreator", "metadatacreator",
          "The value of metadatacreator in the meta packet.",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_flv_mux_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_flv_mux_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_flv_mux_release_pad);

  gst_element_class_add_static_pad_template (gstelement_class,
      &videosink_templ);
  gst_element_class_add_static_pad_template (gstelement_class,
      &audiosink_templ);
  gst_element_class_add_static_pad_template (gstelement_class, &src_templ);
  gst_element_class_set_static_metadata (gstelement_class, "FLV muxer",
      "Codec/Muxer",
      "Muxes video/audio streams into a FLV stream",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (flvmux_debug, "flvmux", 0, "FLV muxer");
}

static void
gst_flv_mux_init (GstFlvMux * mux)
{
  mux->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (mux->srcpad, gst_flv_mux_handle_src_event);
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  /* property */
  mux->streamable = DEFAULT_STREAMABLE;
  mux->metadatacreator = g_strdup (DEFAULT_METADATACREATOR);

  mux->new_tags = FALSE;

  mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_buffer_function (mux->collect,
      GST_DEBUG_FUNCPTR (gst_flv_mux_handle_buffer), mux);
  gst_collect_pads_set_event_function (mux->collect,
      GST_DEBUG_FUNCPTR (gst_flv_mux_handle_sink_event), mux);
  gst_collect_pads_set_clip_function (mux->collect,
      GST_DEBUG_FUNCPTR (gst_flv_mux_clip_running_time), mux);

  gst_flv_mux_reset (GST_ELEMENT (mux));
}

static void
gst_flv_mux_finalize (GObject * object)
{
  GstFlvMux *mux = GST_FLV_MUX (object);

  gst_object_unref (mux->collect);
  g_free (mux->metadatacreator);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_flv_mux_reset (GstElement * element)
{
  GstFlvMux *mux = GST_FLV_MUX (element);
  GSList *sl;

  for (sl = mux->collect->data; sl != NULL; sl = g_slist_next (sl)) {
    GstFlvPad *cpad = (GstFlvPad *) sl->data;

    gst_flv_mux_reset_pad (mux, cpad, cpad->video);
  }

  g_list_foreach (mux->index, (GFunc) gst_flv_mux_index_entry_free, NULL);
  g_list_free (mux->index);
  mux->index = NULL;
  mux->byte_count = 0;

  mux->have_audio = mux->have_video = FALSE;
  mux->duration = GST_CLOCK_TIME_NONE;
  mux->new_tags = FALSE;
  mux->first_timestamp = GST_CLOCK_STIME_NONE;

  mux->state = GST_FLV_MUX_STATE_HEADER;

  /* tags */
  gst_tag_setter_reset_tags (GST_TAG_SETTER (mux));
}

static gboolean
gst_flv_mux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
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

  return gst_pad_event_default (pad, parent, event);
}

/* Extract per-codec relevant tags for
 * insertion into the metadata later - ie bitrate,
 * but maybe others in the future */
static void
gst_flv_mux_store_codec_tags (GstFlvMux * mux,
    GstFlvPad * flvpad, GstTagList * list)
{
  /* Look for a bitrate as either nominal or actual bitrate tag */
  if (gst_tag_list_get_uint (list, GST_TAG_NOMINAL_BITRATE, &flvpad->bitrate) ||
      gst_tag_list_get_uint (list, GST_TAG_BITRATE, &flvpad->bitrate)) {
    GST_DEBUG_OBJECT (mux, "Stored bitrate for pad %" GST_PTR_FORMAT " = %u",
        flvpad, flvpad->bitrate);
  }
}

static gboolean
gst_flv_mux_handle_sink_event (GstCollectPads * pads, GstCollectData * data,
    GstEvent * event, gpointer user_data)
{
  GstFlvMux *mux = GST_FLV_MUX (user_data);
  GstFlvPad *flvpad = (GstFlvPad *) data;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      /* find stream data */
      g_assert (flvpad);

      if (flvpad->video) {
        ret = gst_flv_mux_video_pad_setcaps (data->pad, caps);
      } else {
        ret = gst_flv_mux_audio_pad_setcaps (data->pad, caps);
      }
      /* and eat */
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_TAG:{
      GstTagList *list;
      GstTagSetter *setter = GST_TAG_SETTER (mux);
      const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

      gst_event_parse_tag (event, &list);
      gst_tag_setter_merge_tags (setter, list, mode);
      gst_flv_mux_store_codec_tags (mux, flvpad, list);
      mux->new_tags = TRUE;
      ret = TRUE;
      gst_event_unref (event);
      event = NULL;
      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return gst_collect_pads_event_default (pads, data, event, FALSE);

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

    if (val)
      cpad->video_codec_data = gst_buffer_ref (gst_value_get_buffer (val));
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
      } else if (mpegversion == 4 || mpegversion == 2) {
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
      else
        cpad->audio_codec = 6;
    } else {
      cpad->audio_codec = 6;
    }
  } else if (strcmp (gst_structure_get_name (s), "audio/x-raw") == 0) {
    GstAudioInfo info;

    if (gst_audio_info_from_caps (&info, caps)) {
      cpad->audio_codec = 3;

      if (GST_AUDIO_INFO_WIDTH (&info) == 8)
        cpad->width = 0;
      else if (GST_AUDIO_INFO_WIDTH (&info) == 16)
        cpad->width = 1;
      else
        ret = FALSE;
    } else
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
    gint rate, channels;

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
      else if (rate == 16000 && (cpad->audio_codec == 4
              || cpad->audio_codec == 11))
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
          || cpad->audio_codec == 6 || cpad->audio_codec == 11)
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

    if (cpad->audio_codec != 3)
      cpad->width = 1;
  }

  if (ret && gst_structure_has_field (s, "codec_data")) {
    const GValue *val = gst_structure_get_value (s, "codec_data");

    if (val)
      cpad->audio_codec_data = gst_buffer_ref (gst_value_get_buffer (val));
  }

  gst_object_unref (mux);

  return ret;
}

static void
gst_flv_mux_reset_pad (GstFlvMux * mux, GstFlvPad * cpad, gboolean video)
{
  cpad->video = video;

  if (cpad->audio_codec_data)
    gst_buffer_unref (cpad->audio_codec_data);
  cpad->audio_codec_data = NULL;
  cpad->audio_codec = G_MAXUINT;
  cpad->rate = G_MAXUINT;
  cpad->width = G_MAXUINT;
  cpad->channels = G_MAXUINT;

  if (cpad->video_codec_data)
    gst_buffer_unref (cpad->video_codec_data);
  cpad->video_codec_data = NULL;
  cpad->video_codec = G_MAXUINT;
  cpad->last_timestamp = 0;
  cpad->pts = GST_CLOCK_STIME_NONE;
  cpad->dts = GST_CLOCK_STIME_NONE;
}

static GstPad *
gst_flv_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstFlvMux *mux = GST_FLV_MUX (element);
  GstFlvPad *cpad;
  GstPad *pad = NULL;
  const gchar *name = NULL;
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
  } else if (templ == gst_element_class_get_pad_template (klass, "video")) {
    if (mux->have_video) {
      GST_WARNING_OBJECT (mux, "Already have a video pad");
      return NULL;
    }
    mux->have_video = TRUE;
    name = "video";
    video = TRUE;
  } else {
    GST_WARNING_OBJECT (mux, "Invalid template");
    return NULL;
  }

  pad = gst_pad_new_from_template (templ, name);
  cpad = (GstFlvPad *) gst_collect_pads_add_pad (mux->collect, pad,
      sizeof (GstFlvPad), NULL, TRUE);

  cpad->audio_codec_data = NULL;
  cpad->video_codec_data = NULL;
  gst_flv_mux_reset_pad (mux, cpad, video);

  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (element, pad);

  return pad;
}

static void
gst_flv_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstFlvMux *mux = GST_FLV_MUX (GST_PAD_PARENT (pad));
  GstFlvPad *cpad = (GstFlvPad *) gst_pad_get_element_private (pad);

  gst_flv_mux_reset_pad (mux, cpad, cpad->video);
  gst_collect_pads_remove_pad (mux->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstFlowReturn
gst_flv_mux_push (GstFlvMux * mux, GstBuffer * buffer)
{
  /* pushing the buffer that rewrites the header will make it no longer be the
   * total output size in bytes, but it doesn't matter at that point */
  mux->byte_count += gst_buffer_get_size (buffer);

  return gst_pad_push (mux->srcpad, buffer);
}

static GstBuffer *
gst_flv_mux_create_header (GstFlvMux * mux)
{
  GstBuffer *header;
  guint8 *data;

  _gst_buffer_new_and_alloc (9 + 4, &header, &data);

  data[0] = 'F';
  data[1] = 'L';
  data[2] = 'V';
  data[3] = 0x01;               /* Version */

  data[4] = (mux->have_audio << 2) | mux->have_video;   /* flags */
  GST_WRITE_UINT32_BE (data + 5, 9);    /* data offset */
  GST_WRITE_UINT32_BE (data + 9, 0);    /* previous tag size */

  return header;
}

static GstBuffer *
gst_flv_mux_preallocate_index (GstFlvMux * mux)
{
  GstBuffer *tmp;
  guint8 *data;
  gint preallocate_size;

  /* preallocate index of size:
   *  - 'keyframes' ECMA array key: 2 + 9 = 11 bytes
   *  - nested ECMA array header, length and end marker: 8 bytes
   *  - 'times' and 'filepositions' keys: 22 bytes
   *  - two strict arrays headers and lengths: 10 bytes
   *  - each index entry: 18 bytes
   */
  preallocate_size = 11 + 8 + 22 + 10 + MAX_INDEX_ENTRIES * 18;
  GST_DEBUG_OBJECT (mux, "preallocating %d bytes for the index",
      preallocate_size);

  _gst_buffer_new_and_alloc (preallocate_size, &tmp, &data);

  /* prefill the space with a gstfiller: <spaces> script tag variable */
  GST_WRITE_UINT16_BE (data, 9);        /* 9 characters */
  memcpy (data + 2, "gstfiller", 9);
  GST_WRITE_UINT8 (data + 11, AMF0_STRING_MARKER);      /* a string value */
  GST_WRITE_UINT16_BE (data + 12, preallocate_size - 14);
  memset (data + 14, ' ', preallocate_size - 14);       /* the rest is spaces */
  return tmp;
}

static GstBuffer *
gst_flv_mux_create_number_script_value (const gchar * name, gdouble value)
{
  GstBuffer *tmp;
  guint8 *data;
  gsize len = strlen (name);

  _gst_buffer_new_and_alloc (2 + len + 1 + 8, &tmp, &data);

  GST_WRITE_UINT16_BE (data, len);
  data += 2;                    /* name length */
  memcpy (data, name, len);
  data += len;
  *data++ = AMF0_NUMBER_MARKER; /* double type */
  GST_WRITE_DOUBLE_BE (data, value);

  return tmp;
}

static GstBuffer *
gst_flv_mux_create_metadata (GstFlvMux * mux, gboolean full)
{
  const GstTagList *tags;
  GstBuffer *script_tag, *tmp;
  GstMapInfo map;
  guint8 *data;
  gint i, n_tags, tags_written = 0;

  tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (mux));

  GST_DEBUG_OBJECT (mux, "tags = %" GST_PTR_FORMAT, tags);

  /* FIXME perhaps some bytewriter'ing here ... */

  _gst_buffer_new_and_alloc (11, &script_tag, &data);

  data[0] = 18;

  /* Data size, unknown for now */
  data[1] = 0;
  data[2] = 0;
  data[3] = 0;

  /* Timestamp */
  data[4] = data[5] = data[6] = data[7] = 0;

  /* Stream ID */
  data[8] = data[9] = data[10] = 0;

  _gst_buffer_new_and_alloc (13, &tmp, &data);
  data[0] = AMF0_STRING_MARKER; /* string */
  data[1] = 0;
  data[2] = 10;                 /* length 10 */
  memcpy (&data[3], "onMetaData", 10);

  script_tag = gst_buffer_append (script_tag, tmp);

  n_tags = (tags) ? gst_tag_list_n_tags (tags) : 0;
  _gst_buffer_new_and_alloc (5, &tmp, &data);
  data[0] = 8;                  /* ECMA array */
  GST_WRITE_UINT32_BE (data + 1, n_tags);
  script_tag = gst_buffer_append (script_tag, tmp);

  if (!full)
    goto tags;

  /* Some players expect the 'duration' to be always set. Fill it out later,
     after querying the pads or after getting EOS */
  if (!mux->streamable) {
    tmp = gst_flv_mux_create_number_script_value ("duration", 86400);
    script_tag = gst_buffer_append (script_tag, tmp);
    tags_written++;

    /* Sometimes the information about the total file size is useful for the
       player. It will be filled later, after getting EOS */
    tmp = gst_flv_mux_create_number_script_value ("filesize", 0);
    script_tag = gst_buffer_append (script_tag, tmp);
    tags_written++;

    /* Preallocate space for the index to be written at EOS */
    tmp = gst_flv_mux_preallocate_index (mux);
    script_tag = gst_buffer_append (script_tag, tmp);
  } else {
    GST_DEBUG_OBJECT (mux, "not preallocating index, streamable mode");
  }

tags:
  for (i = 0; tags && i < n_tags; i++) {
    const gchar *tag_name = gst_tag_list_nth_tag_name (tags, i);
    if (!strcmp (tag_name, GST_TAG_DURATION)) {
      guint64 dur;

      if (!gst_tag_list_get_uint64 (tags, GST_TAG_DURATION, &dur))
        continue;
      mux->duration = dur;
    } else if (!strcmp (tag_name, GST_TAG_ARTIST) ||
        !strcmp (tag_name, GST_TAG_TITLE)) {
      gchar *s;
      const gchar *t = NULL;

      if (!strcmp (tag_name, GST_TAG_ARTIST))
        t = "creator";
      else if (!strcmp (tag_name, GST_TAG_TITLE))
        t = "title";

      if (!gst_tag_list_get_string (tags, tag_name, &s))
        continue;

      _gst_buffer_new_and_alloc (2 + strlen (t) + 1 + 2 + strlen (s),
          &tmp, &data);
      data[0] = 0;              /* tag name length */
      data[1] = strlen (t);
      memcpy (&data[2], t, strlen (t));
      data[2 + strlen (t)] = 2; /* string */
      data[3 + strlen (t)] = (strlen (s) >> 8) & 0xff;
      data[4 + strlen (t)] = (strlen (s)) & 0xff;
      memcpy (&data[5 + strlen (t)], s, strlen (s));
      script_tag = gst_buffer_append (script_tag, tmp);

      g_free (s);
      tags_written++;
    }
  }

  if (!full)
    goto end;

  if (mux->duration == GST_CLOCK_TIME_NONE) {
    GSList *l;
    guint64 dur;

    for (l = mux->collect->data; l; l = l->next) {
      GstCollectData *cdata = l->data;

      if (gst_pad_peer_query_duration (cdata->pad, GST_FORMAT_TIME,
              (gint64 *) & dur) && dur != GST_CLOCK_TIME_NONE) {
        if (mux->duration == GST_CLOCK_TIME_NONE)
          mux->duration = dur;
        else
          mux->duration = MAX (dur, mux->duration);
      }
    }
  }

  if (!mux->streamable && mux->duration != GST_CLOCK_TIME_NONE) {
    gdouble d;
    GstMapInfo map;

    d = gst_guint64_to_gdouble (mux->duration);
    d /= (gdouble) GST_SECOND;

    GST_DEBUG_OBJECT (mux, "determined the duration to be %f", d);
    gst_buffer_map (script_tag, &map, GST_MAP_WRITE);
    GST_WRITE_DOUBLE_BE (map.data + 29 + 2 + 8 + 1, d);
    gst_buffer_unmap (script_tag, &map);
  }

  if (mux->have_video) {
    GstPad *video_pad = NULL;
    GstCaps *caps = NULL;
    GstFlvPad *cpad;
    GSList *l = mux->collect->data;

    for (; l; l = l->next) {
      cpad = l->data;
      if (cpad && cpad->video) {
        video_pad = cpad->collect.pad;
        break;
      }
    }

    if (video_pad) {
      caps = gst_pad_get_current_caps (video_pad);
    }

    if (caps != NULL) {
      GstStructure *s;
      gint size;
      gint num, den;

      GST_DEBUG_OBJECT (mux, "putting videocodecid %d in the metadata",
          cpad->video_codec);

      tmp = gst_flv_mux_create_number_script_value ("videocodecid",
          cpad->video_codec);
      script_tag = gst_buffer_append (script_tag, tmp);
      tags_written++;

      s = gst_caps_get_structure (caps, 0);
      gst_caps_unref (caps);

      if (gst_structure_get_int (s, "width", &size)) {
        GST_DEBUG_OBJECT (mux, "putting width %d in the metadata", size);

        tmp = gst_flv_mux_create_number_script_value ("width", size);
        script_tag = gst_buffer_append (script_tag, tmp);
        tags_written++;
      }

      if (gst_structure_get_int (s, "height", &size)) {
        GST_DEBUG_OBJECT (mux, "putting height %d in the metadata", size);

        tmp = gst_flv_mux_create_number_script_value ("height", size);
        script_tag = gst_buffer_append (script_tag, tmp);
        tags_written++;
      }

      if (gst_structure_get_fraction (s, "pixel-aspect-ratio", &num, &den)) {
        gdouble d;

        d = num;
        GST_DEBUG_OBJECT (mux, "putting AspectRatioX %f in the metadata", d);

        tmp = gst_flv_mux_create_number_script_value ("AspectRatioX", d);
        script_tag = gst_buffer_append (script_tag, tmp);
        tags_written++;

        d = den;
        GST_DEBUG_OBJECT (mux, "putting AspectRatioY %f in the metadata", d);

        tmp = gst_flv_mux_create_number_script_value ("AspectRatioY", d);
        script_tag = gst_buffer_append (script_tag, tmp);
        tags_written++;
      }

      if (gst_structure_get_fraction (s, "framerate", &num, &den)) {
        gdouble d;

        gst_util_fraction_to_double (num, den, &d);
        GST_DEBUG_OBJECT (mux, "putting framerate %f in the metadata", d);

        tmp = gst_flv_mux_create_number_script_value ("framerate", d);
        script_tag = gst_buffer_append (script_tag, tmp);
        tags_written++;
      }

      GST_DEBUG_OBJECT (mux, "putting videodatarate %u KB/s in the metadata",
          cpad->bitrate / 1024);
      tmp = gst_flv_mux_create_number_script_value ("videodatarate",
          cpad->bitrate / 1024);
      script_tag = gst_buffer_append (script_tag, tmp);
      tags_written++;
    }
  }

  if (mux->have_audio) {
    GstPad *audio_pad = NULL;
    GstFlvPad *cpad;
    GSList *l = mux->collect->data;

    for (; l; l = l->next) {
      cpad = l->data;
      if (cpad && !cpad->video) {
        audio_pad = cpad->collect.pad;
        break;
      }
    }

    if (audio_pad) {
      GST_DEBUG_OBJECT (mux, "putting audiocodecid %d in the metadata",
          cpad->audio_codec);

      tmp = gst_flv_mux_create_number_script_value ("audiocodecid",
          cpad->audio_codec);
      script_tag = gst_buffer_append (script_tag, tmp);
      tags_written++;

      GST_DEBUG_OBJECT (mux, "putting audiodatarate %u KB/s in the metadata",
          cpad->bitrate / 1024);
      tmp = gst_flv_mux_create_number_script_value ("audiodatarate",
          cpad->bitrate / 1024);
      script_tag = gst_buffer_append (script_tag, tmp);
      tags_written++;
    }
  }

  _gst_buffer_new_and_alloc (2 + 15 + 1 + 2 + strlen (mux->metadatacreator),
      &tmp, &data);
  data[0] = 0;                  /* 15 bytes name */
  data[1] = 15;
  memcpy (&data[2], "metadatacreator", 15);
  data[17] = 2;                 /* string */
  data[18] = (strlen (mux->metadatacreator) >> 8) & 0xff;
  data[19] = (strlen (mux->metadatacreator)) & 0xff;
  memcpy (&data[20], mux->metadatacreator, strlen (mux->metadatacreator));
  script_tag = gst_buffer_append (script_tag, tmp);

  tags_written++;

  {
    GTimeVal tv = { 0, };
    time_t secs;
    struct tm *tm;
    gchar *s;
    static const gchar *weekdays[] = {
      "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const gchar *months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
      "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    g_get_current_time (&tv);
    secs = tv.tv_sec;
    tm = gmtime (&secs);

    s = g_strdup_printf ("%s %s %d %02d:%02d:%02d %d", weekdays[tm->tm_wday],
        months[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
        tm->tm_year + 1900);

    _gst_buffer_new_and_alloc (2 + 12 + 1 + 2 + strlen (s), &tmp, &data);
    data[0] = 0;                /* 12 bytes name */
    data[1] = 12;
    memcpy (&data[2], "creationdate", 12);
    data[14] = 2;               /* string */
    data[15] = (strlen (s) >> 8) & 0xff;
    data[16] = (strlen (s)) & 0xff;
    memcpy (&data[17], s, strlen (s));
    script_tag = gst_buffer_append (script_tag, tmp);

    g_free (s);
    tags_written++;
  }

end:

  if (!tags_written) {
    gst_buffer_unref (script_tag);
    script_tag = NULL;
    goto exit;
  }

  _gst_buffer_new_and_alloc (2 + 0 + 1, &tmp, &data);
  data[0] = 0;                  /* 0 byte size */
  data[1] = 0;
  data[2] = 9;                  /* end marker */
  script_tag = gst_buffer_append (script_tag, tmp);


  _gst_buffer_new_and_alloc (4, &tmp, &data);
  GST_WRITE_UINT32_BE (data, gst_buffer_get_size (script_tag));
  script_tag = gst_buffer_append (script_tag, tmp);

  gst_buffer_map (script_tag, &map, GST_MAP_WRITE);
  map.data[1] = ((gst_buffer_get_size (script_tag) - 11 - 4) >> 16) & 0xff;
  map.data[2] = ((gst_buffer_get_size (script_tag) - 11 - 4) >> 8) & 0xff;
  map.data[3] = ((gst_buffer_get_size (script_tag) - 11 - 4) >> 0) & 0xff;

  GST_WRITE_UINT32_BE (map.data + 11 + 13 + 1, tags_written);
  gst_buffer_unmap (script_tag, &map);

exit:
  return script_tag;
}

static GstBuffer *
gst_flv_mux_buffer_to_tag_internal (GstFlvMux * mux, GstBuffer * buffer,
    GstFlvPad * cpad, gboolean is_codec_data)
{
  GstBuffer *tag;
  GstMapInfo map;
  guint size;
  guint32 pts, dts, cts;
  guint8 *data, *bdata = NULL;
  gsize bsize = 0;

  if (!GST_CLOCK_STIME_IS_VALID (cpad->dts)) {
    pts = dts = cpad->last_timestamp / GST_MSECOND;
  } else {
    pts = cpad->pts / GST_MSECOND;
    dts = cpad->dts / GST_MSECOND;
  }

  /* Be safe in case TS are buggy */
  if (pts > dts)
    cts = pts - dts;
  else
    cts = 0;

  /* Timestamp must start at zero */
  if (GST_CLOCK_STIME_IS_VALID (mux->first_timestamp)) {
    dts -= mux->first_timestamp / GST_MSECOND;
    pts = dts + cts;
  }

  GST_LOG_OBJECT (mux, "got pts %i dts %i cts %i\n", pts, dts, cts);

  if (buffer != NULL) {
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    bdata = map.data;
    bsize = map.size;
  }

  size = 11;
  if (cpad->video) {
    size += 1;
    if (cpad->video_codec == 7)
      size += 4 + bsize;
    else
      size += bsize;
  } else {
    size += 1;
    if (cpad->audio_codec == 10)
      size += 1 + bsize;
    else
      size += bsize;
  }
  size += 4;

  _gst_buffer_new_and_alloc (size, &tag, &data);
  memset (data, 0, size);

  data[0] = (cpad->video) ? 9 : 8;

  data[1] = ((size - 11 - 4) >> 16) & 0xff;
  data[2] = ((size - 11 - 4) >> 8) & 0xff;
  data[3] = ((size - 11 - 4) >> 0) & 0xff;

  GST_WRITE_UINT24_BE (data + 4, dts);
  data[7] = (((guint) dts) >> 24) & 0xff;

  data[8] = data[9] = data[10] = 0;

  if (cpad->video) {
    if (buffer && GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
      data[11] |= 2 << 4;
    else
      data[11] |= 1 << 4;

    data[11] |= cpad->video_codec & 0x0f;

    if (cpad->video_codec == 7) {
      if (is_codec_data) {
        data[12] = 0;
        GST_WRITE_UINT24_BE (data + 13, 0);
      } else if (bsize == 0) {
        /* AVC end of sequence */
        data[12] = 2;
        GST_WRITE_UINT24_BE (data + 13, 0);
      } else {
        /* ACV NALU */
        data[12] = 1;
        GST_WRITE_UINT24_BE (data + 13, cts);
      }
      memcpy (data + 11 + 1 + 4, bdata, bsize);
    } else {
      memcpy (data + 11 + 1, bdata, bsize);
    }
  } else {
    data[11] |= (cpad->audio_codec << 4) & 0xf0;
    data[11] |= (cpad->rate << 2) & 0x0c;
    data[11] |= (cpad->width << 1) & 0x02;
    data[11] |= (cpad->channels << 0) & 0x01;

    GST_DEBUG_OBJECT (mux, "Creating byte %02x with "
        "audio_codec:%d, rate:%d, width:%d, channels:%d",
        data[11], cpad->audio_codec, cpad->rate, cpad->width, cpad->channels);

    if (cpad->audio_codec == 10) {
      data[12] = is_codec_data ? 0 : 1;

      memcpy (data + 11 + 1 + 1, bdata, bsize);
    } else {
      memcpy (data + 11 + 1, bdata, bsize);
    }
  }

  if (buffer)
    gst_buffer_unmap (buffer, &map);

  GST_WRITE_UINT32_BE (data + size - 4, size - 4);

  GST_BUFFER_PTS (tag) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (tag) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (tag) = GST_CLOCK_TIME_NONE;

  if (buffer) {
    /* if we are streamable we copy over timestamps and offsets,
       if not just copy the offsets */
    if (mux->streamable) {
      gst_buffer_copy_into (tag, buffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
      GST_BUFFER_OFFSET (tag) = GST_BUFFER_OFFSET_NONE;
      GST_BUFFER_OFFSET_END (tag) = GST_BUFFER_OFFSET_NONE;
    } else {
      GST_BUFFER_OFFSET (tag) = GST_BUFFER_OFFSET (buffer);
      GST_BUFFER_OFFSET_END (tag) = GST_BUFFER_OFFSET_END (buffer);
    }

    /* mark the buffer if it's an audio buffer and there's also video being muxed
     * or it's a video interframe */
    if ((mux->have_video && !cpad->video) ||
        GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
      GST_BUFFER_FLAG_SET (tag, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (tag, GST_BUFFER_FLAG_DELTA_UNIT);
    GST_BUFFER_OFFSET (tag) = GST_BUFFER_OFFSET_END (tag) =
        GST_BUFFER_OFFSET_NONE;
  }

  return tag;
}

static inline GstBuffer *
gst_flv_mux_buffer_to_tag (GstFlvMux * mux, GstBuffer * buffer,
    GstFlvPad * cpad)
{
  return gst_flv_mux_buffer_to_tag_internal (mux, buffer, cpad, FALSE);
}

static inline GstBuffer *
gst_flv_mux_codec_data_buffer_to_tag (GstFlvMux * mux, GstBuffer * buffer,
    GstFlvPad * cpad)
{
  return gst_flv_mux_buffer_to_tag_internal (mux, buffer, cpad, TRUE);
}

static inline GstBuffer *
gst_flv_mux_eos_to_tag (GstFlvMux * mux, GstFlvPad * cpad)
{
  return gst_flv_mux_buffer_to_tag_internal (mux, NULL, cpad, FALSE);
}

static void
gst_flv_mux_put_buffer_in_streamheader (GValue * streamheader,
    GstBuffer * buffer)
{
  GValue value = { 0 };
  GstBuffer *buf;

  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buffer);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (streamheader, &value);
  g_value_unset (&value);
}

static GstFlowReturn
gst_flv_mux_write_header (GstFlvMux * mux)
{
  GstBuffer *header, *metadata;
  GstBuffer *video_codec_data, *audio_codec_data;
  GstCaps *caps;
  GstStructure *structure;
  GValue streamheader = { 0 };
  GSList *l;
  GstFlowReturn ret;
  GstSegment segment;
  gchar s_id[32];

  /* if not streaming, check if downstream is seekable */
  if (!mux->streamable) {
    gboolean seekable;
    GstQuery *query;

    query = gst_query_new_seeking (GST_FORMAT_BYTES);
    if (gst_pad_peer_query (mux->srcpad, query)) {
      gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
      GST_INFO_OBJECT (mux, "downstream is %sseekable", seekable ? "" : "not ");
    } else {
      /* have to assume seeking is supported if query not handled downstream */
      GST_WARNING_OBJECT (mux, "downstream did not handle seeking query");
      seekable = FALSE;
    }
    if (!seekable) {
      mux->streamable = TRUE;
      g_object_notify (G_OBJECT (mux), "streamable");
      GST_WARNING_OBJECT (mux, "downstream is not seekable, but "
          "streamable=false. Will ignore that and create streamable output "
          "instead");
    }
    gst_query_unref (query);
  }

  header = gst_flv_mux_create_header (mux);
  metadata = gst_flv_mux_create_metadata (mux, TRUE);
  video_codec_data = NULL;
  audio_codec_data = NULL;

  for (l = mux->collect->data; l != NULL; l = l->next) {
    GstFlvPad *cpad = l->data;

    /* Get H.264 and AAC codec data, if present */
    if (cpad && cpad->video && cpad->video_codec == 7) {
      if (cpad->video_codec_data == NULL)
        GST_WARNING_OBJECT (mux, "Codec data for video stream not found, "
            "output might not be playable");
      else
        video_codec_data =
            gst_flv_mux_codec_data_buffer_to_tag (mux, cpad->video_codec_data,
            cpad);
    } else if (cpad && !cpad->video && cpad->audio_codec == 10) {
      if (cpad->audio_codec_data == NULL)
        GST_WARNING_OBJECT (mux, "Codec data for audio stream not found, "
            "output might not be playable");
      else
        audio_codec_data =
            gst_flv_mux_codec_data_buffer_to_tag (mux, cpad->audio_codec_data,
            cpad);
    }
  }

  /* mark buffers that will go in the streamheader */
  GST_BUFFER_FLAG_SET (header, GST_BUFFER_FLAG_HEADER);
  GST_BUFFER_FLAG_SET (metadata, GST_BUFFER_FLAG_HEADER);
  if (video_codec_data != NULL) {
    GST_BUFFER_FLAG_SET (video_codec_data, GST_BUFFER_FLAG_HEADER);
    /* mark as a delta unit, so downstream will not try to synchronize on that
     * buffer - to actually start playback you need a real video keyframe */
    GST_BUFFER_FLAG_SET (video_codec_data, GST_BUFFER_FLAG_DELTA_UNIT);
  }
  if (audio_codec_data != NULL) {
    GST_BUFFER_FLAG_SET (audio_codec_data, GST_BUFFER_FLAG_HEADER);
  }

  /* put buffers in streamheader */
  g_value_init (&streamheader, GST_TYPE_ARRAY);
  gst_flv_mux_put_buffer_in_streamheader (&streamheader, header);
  gst_flv_mux_put_buffer_in_streamheader (&streamheader, metadata);
  if (video_codec_data != NULL)
    gst_flv_mux_put_buffer_in_streamheader (&streamheader, video_codec_data);
  if (audio_codec_data != NULL)
    gst_flv_mux_put_buffer_in_streamheader (&streamheader, audio_codec_data);

  /* stream-start (FIXME: create id based on input ids) */
  g_snprintf (s_id, sizeof (s_id), "flvmux-%08x", g_random_int ());
  gst_pad_push_event (mux->srcpad, gst_event_new_stream_start (s_id));

  /* create the caps and put the streamheader in them */
  caps = gst_caps_new_empty_simple ("video/x-flv");
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_set_value (structure, "streamheader", &streamheader);
  g_value_unset (&streamheader);

  gst_pad_set_caps (mux->srcpad, caps);

  gst_caps_unref (caps);

  /* segment */
  gst_segment_init (&segment,
      mux->streamable ? GST_FORMAT_TIME : GST_FORMAT_BYTES);
  gst_pad_push_event (mux->srcpad, gst_event_new_segment (&segment));

  /* push the header buffer, the metadata and the codec info, if any */
  ret = gst_flv_mux_push (mux, header);
  if (ret != GST_FLOW_OK)
    goto failure_header;
  ret = gst_flv_mux_push (mux, metadata);
  if (ret != GST_FLOW_OK)
    goto failure_metadata;
  if (video_codec_data != NULL) {
    ret = gst_flv_mux_push (mux, video_codec_data);
    if (ret != GST_FLOW_OK)
      goto failure_video_codec_data;
  }
  if (audio_codec_data != NULL) {
    ret = gst_flv_mux_push (mux, audio_codec_data);
    if (ret != GST_FLOW_OK)
      goto failure_audio_codec_data;
  }
  return GST_FLOW_OK;

failure_header:
  gst_buffer_unref (metadata);

failure_metadata:
  if (video_codec_data != NULL)
    gst_buffer_unref (video_codec_data);

failure_video_codec_data:
  if (audio_codec_data != NULL)
    gst_buffer_unref (audio_codec_data);

failure_audio_codec_data:
  return ret;
}

static void
gst_flv_mux_update_index (GstFlvMux * mux, GstBuffer * buffer, GstFlvPad * cpad)
{
  /*
   * Add the tag byte offset and to the index if it's a valid seek point, which
   * means it's either a video keyframe or if there is no video pad (in that
   * case every FLV tag is a valid seek point)
   */
  if (mux->have_video &&
      (!cpad->video ||
          GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)))
    return;

  if (GST_BUFFER_PTS_IS_VALID (buffer)) {
    GstFlvMuxIndexEntry *entry = g_slice_new (GstFlvMuxIndexEntry);
    entry->position = mux->byte_count;
    entry->time = gst_guint64_to_gdouble (GST_BUFFER_PTS (buffer)) / GST_SECOND;
    mux->index = g_list_prepend (mux->index, entry);
  }
}

static GstFlowReturn
gst_flv_mux_write_buffer (GstFlvMux * mux, GstFlvPad * cpad, GstBuffer * buffer)
{
  GstBuffer *tag;
  GstFlowReturn ret;
  GstClockTime dts = GST_BUFFER_DTS (buffer);

  /* clipping function arranged for running_time */

  if (!mux->streamable)
    gst_flv_mux_update_index (mux, buffer, cpad);

  tag = gst_flv_mux_buffer_to_tag (mux, buffer, cpad);

  gst_buffer_unref (buffer);

  ret = gst_flv_mux_push (mux, tag);

  if (ret == GST_FLOW_OK && GST_CLOCK_TIME_IS_VALID (dts))
    cpad->last_timestamp = dts;


  return ret;
}

static guint64
gst_flv_mux_determine_duration (GstFlvMux * mux)
{
  GSList *l;
  GstClockTime duration = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (mux, "trying to determine the duration "
      "from pad timestamps");

  for (l = mux->collect->data; l != NULL; l = l->next) {
    GstFlvPad *cpad = l->data;

    if (cpad && (cpad->last_timestamp != GST_CLOCK_TIME_NONE)) {
      if (duration == GST_CLOCK_TIME_NONE)
        duration = cpad->last_timestamp;
      else
        duration = MAX (duration, cpad->last_timestamp);
    }
  }

  return duration;
}

static GstFlowReturn
gst_flv_mux_write_eos (GstFlvMux * mux)
{
  GstBuffer *tag;
  GstFlvPad *video_pad = NULL;
  GSList *l = mux->collect->data;

  if (!mux->have_video)
    return GST_FLOW_OK;

  for (; l; l = l->next) {
    GstFlvPad *cpad = l->data;
    if (cpad && cpad->video) {
      video_pad = cpad;
      break;
    }
  }

  tag = gst_flv_mux_eos_to_tag (mux, video_pad);

  return gst_flv_mux_push (mux, tag);
}

static GstFlowReturn
gst_flv_mux_rewrite_header (GstFlvMux * mux)
{
  GstBuffer *rewrite, *index, *tmp;
  GstEvent *event;
  guint8 *data;
  gdouble d;
  GList *l;
  guint32 index_len, allocate_size;
  guint32 i, index_skip;
  GstSegment segment;
  GstClockTime dur;

  if (mux->streamable)
    return GST_FLOW_OK;

  /* seek back to the preallocated index space */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  segment.start = segment.time = 13 + 29;
  event = gst_event_new_segment (&segment);
  if (!gst_pad_push_event (mux->srcpad, event)) {
    GST_WARNING_OBJECT (mux, "Seek to rewrite header failed");
    return GST_FLOW_OK;
  }

  /* determine duration now based on our own timestamping,
   * so that it is likely many times better and consistent
   * than whatever obtained by some query */
  dur = gst_flv_mux_determine_duration (mux);
  if (dur != GST_CLOCK_TIME_NONE)
    mux->duration = dur;

  /* rewrite the duration tag */
  d = gst_guint64_to_gdouble (mux->duration);
  d /= (gdouble) GST_SECOND;

  GST_DEBUG_OBJECT (mux, "determined the final duration to be %f", d);

  rewrite = gst_flv_mux_create_number_script_value ("duration", d);

  /* rewrite the filesize tag */
  d = gst_guint64_to_gdouble (mux->byte_count);

  GST_DEBUG_OBJECT (mux, "putting total filesize %f in the metadata", d);

  tmp = gst_flv_mux_create_number_script_value ("filesize", d);
  rewrite = gst_buffer_append (rewrite, tmp);

  if (!mux->index) {
    /* no index, so push buffer and return */
    return gst_flv_mux_push (mux, rewrite);
  }

  /* rewrite the index */
  mux->index = g_list_reverse (mux->index);
  index_len = g_list_length (mux->index);

  /* We write at most MAX_INDEX_ENTRIES elements */
  if (index_len > MAX_INDEX_ENTRIES) {
    index_skip = 1 + index_len / MAX_INDEX_ENTRIES;
    index_len = (index_len + index_skip - 1) / index_skip;
  } else {
    index_skip = 1;
  }

  GST_DEBUG_OBJECT (mux, "Index length is %d", index_len);
  /* see size calculation in gst_flv_mux_preallocate_index */
  allocate_size = 11 + 8 + 22 + 10 + index_len * 18;
  GST_DEBUG_OBJECT (mux, "Allocating %d bytes for index", allocate_size);
  _gst_buffer_new_and_alloc (allocate_size, &index, &data);

  GST_WRITE_UINT16_BE (data, 9);        /* the 'keyframes' key */
  memcpy (data + 2, "keyframes", 9);
  GST_WRITE_UINT8 (data + 11, 8);       /* nested ECMA array */
  GST_WRITE_UINT32_BE (data + 12, 2);   /* two elements */
  GST_WRITE_UINT16_BE (data + 16, 5);   /* first string key: 'times' */
  memcpy (data + 18, "times", 5);
  GST_WRITE_UINT8 (data + 23, 10);      /* strict array */
  GST_WRITE_UINT32_BE (data + 24, index_len);
  data += 28;

  /* the keyframes' times */
  for (i = 0, l = mux->index; l; l = l->next, i++) {
    GstFlvMuxIndexEntry *entry = l->data;

    if (i % index_skip != 0)
      continue;
    GST_WRITE_UINT8 (data, 0);  /* numeric (aka double) */
    GST_WRITE_DOUBLE_BE (data + 1, entry->time);
    data += 9;
  }

  GST_WRITE_UINT16_BE (data, 13);       /* second string key: 'filepositions' */
  memcpy (data + 2, "filepositions", 13);
  GST_WRITE_UINT8 (data + 15, 10);      /* strict array */
  GST_WRITE_UINT32_BE (data + 16, index_len);
  data += 20;

  /* the keyframes' file positions */
  for (i = 0, l = mux->index; l; l = l->next, i++) {
    GstFlvMuxIndexEntry *entry = l->data;

    if (i % index_skip != 0)
      continue;
    GST_WRITE_UINT8 (data, 0);
    GST_WRITE_DOUBLE_BE (data + 1, entry->position);
    data += 9;
  }

  GST_WRITE_UINT24_BE (data, 9);        /* finish the ECMA array */

  /* If there is space left in the prefilled area, reinsert the filler.
     There is at least 18  bytes free, so it will always fit. */
  if (index_len < MAX_INDEX_ENTRIES) {
    GstBuffer *tmp;
    guint8 *data;
    guint32 remaining_filler_size;

    _gst_buffer_new_and_alloc (14, &tmp, &data);
    GST_WRITE_UINT16_BE (data, 9);
    memcpy (data + 2, "gstfiller", 9);
    GST_WRITE_UINT8 (data + 11, 2);     /* string */

    /* There is 18 bytes per remaining index entry minus what is used for
     * the'gstfiller' key. The rest is already filled with spaces, so just need
     * to update length. */
    remaining_filler_size = (MAX_INDEX_ENTRIES - index_len) * 18 - 14;
    GST_DEBUG_OBJECT (mux, "Remaining filler size is %d bytes",
        remaining_filler_size);
    GST_WRITE_UINT16_BE (data + 12, remaining_filler_size);
    index = gst_buffer_append (index, tmp);
  }

  rewrite = gst_buffer_append (rewrite, index);

  return gst_flv_mux_push (mux, rewrite);
}

static GstFlowReturn
gst_flv_mux_handle_buffer (GstCollectPads * pads, GstCollectData * cdata,
    GstBuffer * buffer, gpointer user_data)
{
  GstFlvMux *mux = GST_FLV_MUX (user_data);
  GstFlvPad *best;
  gint64 best_time = GST_CLOCK_STIME_NONE;
  GstFlowReturn ret;

  if (mux->state == GST_FLV_MUX_STATE_HEADER) {
    if (mux->collect->data == NULL) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("No input streams configured"));
      return GST_FLOW_ERROR;
    }

    ret = gst_flv_mux_write_header (mux);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buffer);
      return ret;
    }
    mux->state = GST_FLV_MUX_STATE_DATA;

    if (cdata && GST_COLLECT_PADS_DTS_IS_VALID (cdata))
      mux->first_timestamp = GST_COLLECT_PADS_DTS (cdata);
    else
      mux->first_timestamp = 0;
  }

  if (mux->new_tags) {
    GstBuffer *buf = gst_flv_mux_create_metadata (mux, FALSE);
    if (buf)
      gst_flv_mux_push (mux, buf);
    mux->new_tags = FALSE;
  }

  best = (GstFlvPad *) cdata;
  if (best) {
    g_assert (buffer);
    best->dts = GST_COLLECT_PADS_DTS (cdata);

    if (GST_CLOCK_STIME_IS_VALID (best->dts))
      best_time = best->dts - mux->first_timestamp;

    if (GST_BUFFER_PTS_IS_VALID (buffer))
      best->pts = GST_BUFFER_PTS (buffer);
    else
      best->pts = best->dts;

    GST_LOG_OBJECT (mux, "got buffer PTS %" GST_TIME_FORMAT " DTS %"
        GST_STIME_FORMAT "\n", GST_TIME_ARGS (best->pts),
        GST_STIME_ARGS (best->dts));
  } else {
    best_time = GST_CLOCK_STIME_NONE;
  }

  /* The FLV timestamp is an int32 field. For non-live streams error out if a
     bigger timestamp is seen, for live the timestamp will get wrapped in
     gst_flv_mux_buffer_to_tag */
  if (!mux->streamable && (GST_CLOCK_STIME_IS_VALID (best_time))
      && best_time / GST_MSECOND > G_MAXINT32) {
    GST_WARNING_OBJECT (mux, "Timestamp larger than FLV supports - EOS");
    gst_buffer_unref (buffer);
    buffer = NULL;
    best = NULL;
  }

  if (best) {
    return gst_flv_mux_write_buffer (mux, best, buffer);
  } else {
    /* FIXME check return values */
    gst_flv_mux_write_eos (mux);
    gst_flv_mux_rewrite_header (mux);
    gst_pad_push_event (mux->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }
}

static void
gst_flv_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFlvMux *mux = GST_FLV_MUX (object);

  switch (prop_id) {
    case PROP_STREAMABLE:
      g_value_set_boolean (value, mux->streamable);
      break;
    case PROP_METADATACREATOR:
      g_value_set_string (value, mux->metadatacreator);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_flv_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFlvMux *mux = GST_FLV_MUX (object);

  switch (prop_id) {
    case PROP_STREAMABLE:
      mux->streamable = g_value_get_boolean (value);
      if (mux->streamable)
        gst_tag_setter_set_tag_merge_mode (GST_TAG_SETTER (mux),
            GST_TAG_MERGE_REPLACE);
      else
        gst_tag_setter_set_tag_merge_mode (GST_TAG_SETTER (mux),
            GST_TAG_MERGE_KEEP);
      break;
    case PROP_METADATACREATOR:
      g_free (mux->metadatacreator);
      if (!g_value_get_string (value)) {
        GST_WARNING_OBJECT (mux, "metadatacreator property can not be NULL");
        mux->metadatacreator = g_strdup (DEFAULT_METADATACREATOR);
      } else {
        mux->metadatacreator = g_value_dup_string (value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
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
