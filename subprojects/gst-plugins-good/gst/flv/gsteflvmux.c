/* GStreamer
 *
 * Copyright (c) 2025 Centricular Ltd
 *  @author: Taruntej Kanakamalla <tarun@centricular.com>
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
 * SECTION:element-eflvmux
 * @title: eflvmux
 * @see_also: #GstFlvMux
 *
 * `eflvmux` is an extension to the `flvmux`, capable of multiplexing multiple tracks and signalling
 * advanced codecs in FOURCC format as per Enhanced RTMP (V2) specification.
 *
 * Two types of sink pads can be requested from `eflvmux`
 *
 * 1. `audio`/`video` - for create a default track and can be used to stream in the legacy or extended FLV format.
 *    The format can be selected using the `flv-track-mode` pad property. See #GstEFlvMuxPad:flv-track-mode
 *
 * 2. `audio_%u`/`video_%u` - for creating multiple tracks in extended FLV format of multitrack type, each with a
 *     unique non-zero track ID. The track ID will be same as the pad index.
 *
 * It is important to specify the right pad template (preferably with specific pad index in case of `_%u` type pads) while
 * linking pads using `gst_element_link_pads` or `_parse_launch`. Failing to specify the pad template can result streaming in
 * the wrong FLV format.
 *
 * Note:
 * Since we use pad index as the track id and a track with id 0 can be created only through the `audio`/`video` pads, requesting a
 * `audio_0` or `video_0` is not allowed.
 *
 * ## Example launch line saving 2 audio and 2 video tracks to a FLV file
 *
 * ```
 * gst-launch-1.0 eflvmux name=mux audio::flv-track-mode=multitrack video::flv-track-mode=non-multitrack  ! filesink location=$HOME/Videos/test_eflv_non.flv \
 *    audiotestsrc  wave=ticks ! audio/x-raw,rate=44100,format=S16LE,channels=2 ! fdkaacenc ! mux.audio \
 *    audiotestsrc  wave=white-noise  ! audio/x-raw,rate=44100,format=S16LE,channels=2 !  fdkaacenc ! mux.audio_2  \
 *    videotestsrc ! videoconvert ! timeoverlay ! x264enc tune=zerolatency key-int-max=30 ! h264parse ! mux.video   \
 *    videotestsrc ! videoconvert ! timeoverlay ! x265enc ! h265parse ! mux.video_2
 * ```
 *
 * The pipeline creates
 *    - the default audio track from the `audio` pad in multitrack eFLV format with track id 0
 *    - additional audio track from the `audio_1` pad in the multitrack eFLV
 *    format with track id 1
 *    - the default video track from the `video` pad in non-mulitrack eFLV
 *    format so it won't have any track id
 *    - additional video track from the `video_1` pad in the multitrack eFLV
 *    format with track id 1
 *
 * ## To play the above file using `ffplay` (FFmpeg version 8 or greater)
 *
 * `ffplay $HOME/Videos/test_eflv_non.flv`
 *
 * (switch the video tracks using `v` and audio tracks with `a` keys)
 *
 * ## Example launch line for streaming 2 audio tracks to Twitch
 *
 * ``` bash
 * gst-launch-1.0 videotestsrc pattern=ball ! 'video/x-raw,format=I420,width=1280,height=720,framerate=30/1' ! \
 *    timeoverlay ! videoconvert ! x264enc tune=zerolatency key-int-max=30 ! h264parse ! eflvmux name=mux ! \
 *    rtmp2sink location="rtmp://ingest.global-contribute.live-video.net/app/$STREAM_KEY" \
 *    audiotestsrc wave=ticks ! fdkaacenc ! mux.audio \
 *    audiotestsrc !  fdkaacenc ! mux.audio_1
 * ```
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstflvelements.h"
#include "gsteflvmux.h"

enum
{
  PROP_PAD_0,
  PROP_PAD_FLV_TRACK_MODE
};

#define DEFAULT_PAD_FLV_TRACK_MODE (GST_FLV_TRACK_MODE_LEGACY)

#define gst_eflv_mux_parent_class parent_class

GST_DEBUG_CATEGORY_STATIC (eflvmux_debug);
#define GST_CAT_DEFAULT eflvmux_debug

#define GST_TYPE_FLV_TRACK_MODE (gst_elfvmux_flv_track_mode_get_type())
static GType
gst_elfvmux_flv_track_mode_get_type (void)
{
  static GType flv_track_mode = 0;
  static const GEnumValue flv_track_modes[] = {
    {GST_FLV_TRACK_MODE_ENHANCED_MULTITRACK,
        "Enhanced FLV multitrack type with track ID 0", "multitrack"},
    {GST_FLV_TRACK_MODE_ENHANCED_NON_MULTITRACK,
          "Enhanced FLV but not multitrack type, i.e., no track ID",
        "non-multitrack"},
    {GST_FLV_TRACK_MODE_LEGACY, "Legacy FLV", "legacy"},
    {0, NULL, NULL},
  };

  if (G_UNLIKELY (flv_track_mode == 0)) {
    flv_track_mode =
        g_enum_register_static ("GstFlvTrackMode", flv_track_modes);
  }
  return flv_track_mode;
}


G_DEFINE_TYPE (GstEFlvMuxPad, gst_eflv_mux_pad, GST_TYPE_FLV_MUX_PAD);

static GstStaticPadTemplate multitrack_audiosink_templ =
    GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS
    ("audio/mpeg, mpegversion = (int) 1, layer = (int) 3, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 22050, 44100 }, parsed = (boolean) TRUE; "
        "audio/mpeg, mpegversion = (int) { 4, 2 }, stream-format = (string) raw; ")
    );

static GstStaticPadTemplate multitrack_videosink_templ =
    GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-h264, stream-format=avc;"
        "video/x-h265, stream-format=hvc1;")
    );

static GstStaticPadTemplate audiosink_templ = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (LEGACY_FLV_AUDIO_CAPS)
    );

static GstStaticPadTemplate videosink_templ = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (LEGACY_FLV_VIDEO_CAPS FLV_ENHANCED_VIDEO_CAPS)
    );

G_DEFINE_TYPE (GstEFlvMux, gst_eflv_mux, GST_TYPE_FLV_MUX);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (eflvmux, "eflvmux",
    GST_RANK_PRIMARY, GST_TYPE_EFLV_MUX, flv_element_init (plugin));

static void
gst_eflv_mux_class_init (GstEFlvMuxClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &multitrack_audiosink_templ, GST_TYPE_FLV_MUX_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &multitrack_videosink_templ, GST_TYPE_FLV_MUX_PAD);

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &audiosink_templ, GST_TYPE_EFLV_MUX_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &videosink_templ, GST_TYPE_EFLV_MUX_PAD);

  gst_element_class_set_static_metadata (gstelement_class, "Enhanced FLV muxer",
      "Codec/Muxer",
      "Muxes multiple video/audio streams into an FLV stream in the extended format as per Enhnaced RTMP (V2) specification",
      "Taruntej Kanakamalla <tarun@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (eflvmux_debug, "eflvmux", 0, "eFLV muxer");

  gst_type_mark_as_plugin_api (GST_TYPE_EFLV_MUX_PAD, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_FLV_TRACK_MODE, 0);
}

static void
gst_eflv_mux_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEFlvMuxPad *pad = GST_EFLV_MUX_PAD (object);

  switch (prop_id) {
    case PROP_PAD_FLV_TRACK_MODE:
      GST_OBJECT_LOCK (pad);
      pad->parent_pad.flv_track_mode = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (pad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eflv_mux_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEFlvMuxPad *pad = GST_EFLV_MUX_PAD (object);

  switch (prop_id) {
    case PROP_PAD_FLV_TRACK_MODE:
      g_value_set_enum (value, pad->parent_pad.flv_track_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eflv_mux_pad_class_init (GstEFlvMuxPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_eflv_mux_pad_set_property;
  gobject_class->get_property = gst_eflv_mux_pad_get_property;

  /**
   * GstEFlvMuxPad::flv-track-mode:
   *
   * Determines whether to stream FLV in Enhanced multitrack or non-multitrack or legacy format
   *
   * @GST_FLV_TRACK_MODE_ENHANCED_MULTITRACK: Stream the track with each FLV packet containing a Multitrack.OneTrack type. The track ID is always 0.
   * @GST_FLV_TRACK_MODE_ENHANCED_NON_MULTITRACK: Stream the track in enhanced FLV, but it is not Multitrack type packet, so there won't be a track ID.
   * @GST_FLV_TRACK_MODE_LEGACY: Stream the track in the legacy FLV format without any extended header.
   *
   * Since: 1.28
   */
  g_object_class_install_property (gobject_class, PROP_PAD_FLV_TRACK_MODE,
      g_param_spec_enum ("flv-track-mode", "FLV track mode",
          "Determines whether to stream FLV in Enhanced multitrack or non-multitrack or legacy format",
          GST_TYPE_FLV_TRACK_MODE, DEFAULT_PAD_FLV_TRACK_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_eflv_mux_pad_init (GstEFlvMuxPad * pad)
{
}

static void
gst_eflv_mux_init (GstEFlvMux * mux)
{
  GST_DEBUG_OBJECT (mux, "eFLV muxer initialized");
}
