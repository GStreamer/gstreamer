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
 * Note:
 * The `audio` pad can only send the data in legacy FLV format and the `audio_%u` can only send it in the Enhanced FLV format.
 * So it is important to specify the corresponding pad template while linking pads using `gst_element_link_pads` or `_parse_launch`.
 * Failing to specify the pad template can result in sending the data in the wrong FLV format.
 *
 * ## Example launch line streaming 2 audio tracks to Twitch
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


GST_DEBUG_CATEGORY_STATIC (eflvmux_debug);
#define GST_CAT_DEFAULT eflvmux_debug

G_DEFINE_TYPE_WITH_CODE (GstEFlvMux, gst_eflv_mux, GST_TYPE_FLV_MUX,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL));
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (eflvmux, "eflvmux",
    GST_RANK_PRIMARY, GST_TYPE_EFLV_MUX, flv_element_init (plugin));

static GstStaticPadTemplate audiosink_templ =
    GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS
    ("audio/mpeg, mpegversion = (int) 1, layer = (int) 3, channels = (int) { 1, 2 }, rate = (int) { 5512, 8000, 11025, 22050, 44100 }, parsed = (boolean) TRUE; "
        "audio/mpeg, mpegversion = (int) { 4, 2 }, stream-format = (string) raw; ")
    );

static GstStaticPadTemplate videosink_templ =
    GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-h264, stream-format=avc;"
        "video/x-h265, stream-format=hvc1;")
    );

static void
gst_eflv_mux_class_init (GstEFlvMuxClass * klass)
{
  GstElementClass *gstelement_class;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &audiosink_templ, GST_TYPE_FLV_MUX_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &videosink_templ, GST_TYPE_FLV_MUX_PAD);

  gst_element_class_set_static_metadata (gstelement_class, "Enhanced FLV muxer",
      "Codec/Muxer",
      "Muxes multiple video/audio streams into an FLV stream in the extended format as per Enhnaced RTMP (V2) specification",
      "Taruntej Kanakamalla <tarun@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (eflvmux_debug, "eflvmux", 0, "eFLV muxer");

  gst_type_mark_as_plugin_api (GST_TYPE_FLV_MUX_PAD, 0);
}

static void
gst_eflv_mux_init (GstEFlvMux * mux)
{
  GST_DEBUG_OBJECT (mux, "eFLV muxer initialized");
}
