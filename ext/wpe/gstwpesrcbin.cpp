/* Copyright (C) <2018, 2019> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018, 2019> Žan Doberšek <zdobersek@igalia.com>
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
 * SECTION:element-wpesrc
 * @title: wpesrc
 *
 * The wpesrc element is used to produce a video texture representing a web page
 * rendered off-screen by WPE.
 *
 * Starting from WPEBackend-FDO 1.6.x, software rendering support is available.
 * This features allows wpesrc to be used on machines without GPU, and/or for
 * testing purpose. To enable it, set the `LIBGL_ALWAYS_SOFTWARE=true`
 * environment variable and make sure `video/x-raw, format=BGRA` caps are
 * negotiated by the wpesrc element.
 *
 * Additionally, any audio stream created by WPE is exposed as "sometimes" audio
 * source pads.
 */

#include "gstwpesrcbin.h"
#include "gstwpevideosrc.h"
#include "gstwpe-private.h"
#include "WPEThreadedView.h"

#include <gst/allocators/allocators.h>
#include <gst/base/gstflowcombiner.h>
#include <wpe/extensions/audio.h>

G_DEFINE_TYPE (GstWpeAudioPad, gst_wpe_audio_pad, GST_TYPE_GHOST_PAD);

static void
gst_wpe_audio_pad_class_init (GstWpeAudioPadClass * klass)
{
}

static void
gst_wpe_audio_pad_init (GstWpeAudioPad * pad)
{
  gst_audio_info_init (&pad->info);
  pad->discont_pending = FALSE;
  pad->buffer_time = GST_CLOCK_TIME_NONE;
}

static GstWpeAudioPad *
gst_wpe_audio_pad_new (const gchar * name)
{
  GstWpeAudioPad *pad = GST_WPE_AUDIO_PAD (g_object_new (gst_wpe_audio_pad_get_type (),
    "name", name, "direction", GST_PAD_SRC, NULL));

  if (!gst_ghost_pad_construct (GST_GHOST_PAD (pad))) {
    gst_object_unref (pad);
    return NULL;
  }

  return pad;
}

struct _GstWpeSrc
{
  GstBin parent;

  GstAllocator *fd_allocator;
  GstElement *video_src;
  GHashTable *audio_src_pads;
  GstFlowCombiner *flow_combiner;
};

enum
{
 PROP_0,
 PROP_LOCATION,
 PROP_DRAW_BACKGROUND
};

enum
{
 SIGNAL_LOAD_BYTES,
 LAST_SIGNAL
};

static guint gst_wpe_video_src_signals[LAST_SIGNAL] = { 0 };

static void gst_wpe_src_uri_handler_init (gpointer iface, gpointer data);

GST_DEBUG_CATEGORY_EXTERN (wpe_src_debug);
#define GST_CAT_DEFAULT wpe_src_debug

#define gst_wpe_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWpeSrc, gst_wpe_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_wpe_src_uri_handler_init));

static GstStaticPadTemplate video_src_factory =
GST_STATIC_PAD_TEMPLATE ("video", GST_PAD_SRC, GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw(memory:GLMemory), "
                     "format = (string) RGBA, "
                     "width = " GST_VIDEO_SIZE_RANGE ", "
                     "height = " GST_VIDEO_SIZE_RANGE ", "
                     "framerate = " GST_VIDEO_FPS_RANGE ", "
                     "pixel-aspect-ratio = (fraction)1/1,"
                     "texture-target = (string)2D"
#if ENABLE_SHM_BUFFER_SUPPORT
                     "; video/x-raw, "
                     "format = (string) BGRA, "
                     "width = " GST_VIDEO_SIZE_RANGE ", "
                     "height = " GST_VIDEO_SIZE_RANGE ", "
                     "framerate = " GST_VIDEO_FPS_RANGE ", "
                     "pixel-aspect-ratio = (fraction)1/1"
#endif
                     ));

static GstStaticPadTemplate audio_src_factory =
GST_STATIC_PAD_TEMPLATE ("audio_%u", GST_PAD_SRC, GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ( \
        GST_AUDIO_CAPS_MAKE (GST_AUDIO_NE (F32)) ", layout=(string)interleaved; " \
        GST_AUDIO_CAPS_MAKE (GST_AUDIO_NE (F64)) ", layout=(string)interleaved; " \
        GST_AUDIO_CAPS_MAKE (GST_AUDIO_NE (S16)) ", layout=(string)interleaved" \
));

static GstFlowReturn
gst_wpe_src_chain_buffer (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstWpeSrc *src = GST_WPE_SRC (gst_object_get_parent (parent));
  GstFlowReturn result, chain_result;

  chain_result = gst_proxy_pad_chain_default (pad, GST_OBJECT_CAST (src), buffer);
  result = gst_flow_combiner_update_pad_flow (src->flow_combiner, pad, chain_result);
  gst_object_unref (src);

  if (result == GST_FLOW_FLUSHING)
    return chain_result;

  return result;
}

static void
on_audio_receiver_handle_start(void* data, uint32_t id, int32_t channels, const char* format, int32_t sampleRate)
{
  GstWpeSrc* src = GST_WPE_SRC (data);
  GstWpeAudioPad *audio_pad;
  GstPad *pad;
  gchar *name;
  GstEvent *stream_start;
  GstSegment segment;
  GstCaps *caps;

  GST_DEBUG_OBJECT (src, "Exposing audio pad for stream %u", id);
  name = g_strdup_printf ("audio_%u", id);
  audio_pad = gst_wpe_audio_pad_new (name);
  pad = GST_PAD_CAST (audio_pad);
  g_free (name);

  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (src), pad);
  gst_flow_combiner_add_pad (src->flow_combiner, pad);

  name = gst_pad_create_stream_id_printf(pad, GST_ELEMENT_CAST (src), "%03u", id);
  stream_start = gst_event_new_stream_start (name);
  gst_pad_push_event (pad, stream_start);
  g_free (name);

  caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, format,
    "rate", G_TYPE_INT, sampleRate,
    "channels", G_TYPE_INT, channels,
    "channel-mask", GST_TYPE_BITMASK, gst_audio_channel_get_fallback_mask (channels),
    "layout", G_TYPE_STRING, "interleaved", NULL);
  gst_audio_info_from_caps (&audio_pad->info, caps);
  gst_pad_push_event (pad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (pad, gst_event_new_segment (&segment));

  g_hash_table_insert (src->audio_src_pads, GUINT_TO_POINTER (id), audio_pad);
}

static void
on_audio_receiver_handle_packet(void* data, struct wpe_audio_packet_export* packet_export, uint32_t id, int32_t fd, uint32_t size)
{
  GstWpeSrc* src = GST_WPE_SRC (data);
  GstWpeAudioPad *audio_pad = GST_WPE_AUDIO_PAD (g_hash_table_lookup (src->audio_src_pads, GUINT_TO_POINTER (id)));
  GstPad *pad = GST_PAD_CAST (audio_pad);
  GstBuffer *buffer;
  GstClock *clock;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (fd >= 0);

  GST_TRACE_OBJECT (pad, "Handling incoming audio packet");
  buffer = gst_buffer_new ();

  GstMemory *mem = gst_fd_allocator_alloc (src->fd_allocator, dup (fd), size, GST_FD_MEMORY_FLAG_KEEP_MAPPED);
  gst_buffer_append_memory (buffer, mem);
  gst_buffer_add_audio_meta (buffer, &audio_pad->info, size, NULL);

  clock = gst_element_get_clock (GST_ELEMENT_CAST (src));
  if (clock) {
    GstClockTime now;
    GstClockTime base_time = gst_element_get_base_time (GST_ELEMENT_CAST (src));

    now = gst_clock_get_time (clock);
    if (now > base_time)
      now -= base_time;
    else
      now = 0;
    gst_object_unref (clock);

    audio_pad->buffer_time = now;
    GST_BUFFER_DTS (buffer) = audio_pad->buffer_time;
  }

  GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  if (audio_pad->discont_pending) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    audio_pad->discont_pending = FALSE;
  }

  gst_flow_combiner_update_pad_flow (src->flow_combiner, pad, gst_pad_push (pad, buffer));
  wpe_audio_packet_export_release (packet_export);
  close (fd);
}

static void
on_audio_receiver_handle_stop(void* data, uint32_t id)
{
  GstWpeSrc* src = GST_WPE_SRC (data);
  GstWpeAudioPad *audio_pad = GST_WPE_AUDIO_PAD (g_hash_table_lookup (src->audio_src_pads, GUINT_TO_POINTER (id)));
  GstPad *pad = GST_PAD_CAST (audio_pad);
  g_return_if_fail (GST_IS_PAD (pad));

  GST_INFO_OBJECT(pad, "Stopping");
  gst_pad_push_event (pad, gst_event_new_eos ());
  gst_element_remove_pad (GST_ELEMENT_CAST (src), pad);
  gst_flow_combiner_remove_pad (src->flow_combiner, pad);
  g_hash_table_remove (src->audio_src_pads, GUINT_TO_POINTER (id));
}

static void
on_audio_receiver_handle_pause(void* data, uint32_t id)
{
  GstWpeSrc* src = GST_WPE_SRC (data);
  GstWpeAudioPad *audio_pad = GST_WPE_AUDIO_PAD (g_hash_table_lookup (src->audio_src_pads, GUINT_TO_POINTER (id)));
  GstPad *pad = GST_PAD_CAST (audio_pad);
  g_return_if_fail (GST_IS_PAD (pad));

  GST_INFO_OBJECT(pad, "Pausing");
  gst_pad_push_event (pad, gst_event_new_gap (audio_pad->buffer_time, GST_CLOCK_TIME_NONE));

  audio_pad->discont_pending = TRUE;
}

static void
on_audio_receiver_handle_resume(void* data, uint32_t id)
{
  GstWpeSrc* src = GST_WPE_SRC (data);
  GstWpeAudioPad *audio_pad = GST_WPE_AUDIO_PAD (g_hash_table_lookup (src->audio_src_pads, GUINT_TO_POINTER (id)));
  GstPad *pad = GST_PAD_CAST (audio_pad);
  g_return_if_fail (GST_IS_PAD (pad));

  GST_INFO_OBJECT(pad, "Resuming");
}


static const struct wpe_audio_receiver audio_receiver = {
  .handle_start = on_audio_receiver_handle_start,
  .handle_packet = on_audio_receiver_handle_packet,
  .handle_stop = on_audio_receiver_handle_stop,
  .handle_pause = on_audio_receiver_handle_pause,
  .handle_resume = on_audio_receiver_handle_resume
};

static void
gst_wpe_src_load_bytes (GstWpeVideoSrc * src, GBytes * bytes)
{
  GstWpeSrc *self = GST_WPE_SRC (src);

  if (self->video_src)
    g_signal_emit_by_name (self->video_src, "load-bytes", bytes, NULL);
}

static void
gst_wpe_src_set_location (GstWpeSrc * src, const gchar * location)
{
  GstPad *pad;
  GstPad *ghost_pad;
  GstProxyPad *proxy_pad;

  g_object_set (src->video_src, "location", location, NULL);

  ghost_pad = gst_element_get_static_pad (GST_ELEMENT_CAST (src), "video");
  if (GST_IS_PAD (ghost_pad)) {
    gst_object_unref (ghost_pad);
    return;
  }

  gst_bin_add (GST_BIN_CAST (src), src->video_src);

  pad = gst_element_get_static_pad (GST_ELEMENT_CAST (src->video_src), "src");
  ghost_pad = gst_ghost_pad_new_from_template ("video", pad,
    gst_static_pad_template_get (&video_src_factory));
  proxy_pad = gst_proxy_pad_get_internal (GST_PROXY_PAD (ghost_pad));
  gst_pad_set_active (GST_PAD_CAST (proxy_pad), TRUE);

  gst_element_add_pad (GST_ELEMENT_CAST (src), GST_PAD_CAST (ghost_pad));
  gst_flow_combiner_add_pad (src->flow_combiner, GST_PAD_CAST (ghost_pad));
  gst_pad_set_chain_function (GST_PAD_CAST (proxy_pad), gst_wpe_src_chain_buffer);

  gst_object_unref (proxy_pad);
  gst_object_unref (pad);
}

static void
gst_wpe_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWpeSrc *self = GST_WPE_SRC (object);

  if (self->video_src)
    g_object_get_property (G_OBJECT (self->video_src), pspec->name, value);
}

static void
gst_wpe_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWpeSrc *self = GST_WPE_SRC (object);

  if (self->video_src) {
    if (prop_id == PROP_LOCATION)
      gst_wpe_src_set_location (self, g_value_get_string (value));
    else
      g_object_set_property (G_OBJECT (self->video_src), pspec->name, value);
  }
}

static GstURIType
gst_wpe_src_uri_get_type (GType)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_wpe_src_get_protocols (GType)
{
  static const char *protocols[] = { "wpe", NULL };
  return protocols;
}

static gchar *
gst_wpe_src_get_uri (GstURIHandler * handler)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);
  const gchar *location;
  g_object_get (src->video_src, "location", &location, NULL);
  return g_strdup_printf ("wpe://%s", location);
}

static gboolean
gst_wpe_src_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);

  gst_wpe_src_set_location(src, uri + 6);
  return TRUE;
}

static void
gst_wpe_src_uri_handler_init (gpointer iface_ptr, gpointer data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) iface_ptr;

  iface->get_type = gst_wpe_src_uri_get_type;
  iface->get_protocols = gst_wpe_src_get_protocols;
  iface->get_uri = gst_wpe_src_get_uri;
  iface->set_uri = gst_wpe_src_set_uri;
}

static void
gst_wpe_src_init (GstWpeSrc * src)
{
  gst_bin_set_suppressed_flags (GST_BIN_CAST (src),
      static_cast<GstElementFlags>(GST_ELEMENT_FLAG_SOURCE | GST_ELEMENT_FLAG_SINK));
  GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_SOURCE);

  src->fd_allocator = gst_fd_allocator_new ();
  src->audio_src_pads = g_hash_table_new (g_direct_hash, g_direct_equal);
  src->flow_combiner = gst_flow_combiner_new ();
  src->video_src = gst_element_factory_make ("wpevideosrc", NULL);

  gst_wpe_video_src_register_audio_receiver (src->video_src, &audio_receiver, src);
}

static GstStateChangeReturn
gst_wpe_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn result;
  GstWpeSrc *src = GST_WPE_SRC (element);

  GST_DEBUG_OBJECT (src, "%s", gst_state_change_get_name (transition));
  result = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS , change_state, (element, transition), GST_STATE_CHANGE_FAILURE);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:{
    gst_flow_combiner_reset (src->flow_combiner);
    break;
  }
  default:
    break;
  }

  return result;
}

static void
gst_wpe_src_dispose (GObject *object)
{
    GstWpeSrc *src = GST_WPE_SRC (object);

    g_hash_table_unref (src->audio_src_pads);
    gst_flow_combiner_free (src->flow_combiner);
    gst_object_unref (src->fd_allocator);

    GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_wpe_src_class_init (GstWpeSrcClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_wpe_src_set_property;
  gobject_class->get_property = gst_wpe_src_get_property;
  gobject_class->dispose = gst_wpe_src_dispose;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location", "The URL to display", "",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DRAW_BACKGROUND,
      g_param_spec_boolean ("draw-background", "Draws the background",
          "Whether to draw the WebView background", TRUE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class, "WPE source",
      "Source/Video/Audio", "Creates a video stream from a WPE browser",
      "Philippe Normand <philn@igalia.com>, Žan Doberšek "
      "<zdobersek@igalia.com>");

  /**
   * GstWpeSrc::load-bytes:
   * @src: the object which received the signal
   * @bytes: the GBytes data to load
   *
   * Load the specified bytes into the internal webView.
   */
  gst_wpe_video_src_signals[SIGNAL_LOAD_BYTES] =
      g_signal_new_class_handler ("load-bytes", G_TYPE_FROM_CLASS (klass),
      static_cast < GSignalFlags > (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_wpe_src_load_bytes), NULL, NULL, NULL, G_TYPE_NONE, 1,
      G_TYPE_BYTES);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_wpe_src_change_state);

  gst_element_class_add_static_pad_template (element_class, &video_src_factory);
  gst_element_class_add_static_pad_template (element_class, &audio_src_factory);
}
