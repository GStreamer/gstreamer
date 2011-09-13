/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * SECTION:element-playbin
 *
 * Playbin provides a stand-alone everything-in-one abstraction for an
 * audio and/or video player.
 *
 * <note>
 * This element is deprecated and no longer supported. You should use
 * the #playbin2 element instead.
 * </note>
 *
 * It can handle both audio and video files and features
 * <itemizedlist>
 * <listitem>
 * automatic file type recognition and based on that automatic
 * selection and usage of the right audio/video/subtitle demuxers/decoders
 * </listitem>
 * <listitem>
 * visualisations for audio files
 * </listitem>
 * <listitem>
 * subtitle support for video files
 * </listitem>
 * <listitem>
 * stream selection between different audio/subtitles streams
 * </listitem>
 * <listitem>
 * meta info (tag) extraction
 * </listitem>
 * <listitem>
 * easy access to the last video frame
 * </listitem>
 * <listitem>
 * buffering when playing streams over a network
 * </listitem>
 * <listitem>
 * volume control
 * </listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Usage</title>
 * <para>
 * A playbin element can be created just like any other element using
 * gst_element_factory_make(). The file/URI to play should be set via the #GstPlayBin:uri
 * property. This must be an absolute URI, relative file paths are not allowed.
 * Example URIs are file:///home/joe/movie.avi or http://www.joedoe.com/foo.ogg
 *
 * Playbin is a #GstPipeline. It will notify the application of everything
 * that's happening (errors, end of stream, tags found, state changes, etc.)
 * by posting messages on its #GstBus. The application needs to watch the
 * bus.
 *
 * Playback can be initiated by setting the element to PLAYING state using
 * gst_element_set_state(). Note that the state change will take place in
 * the background in a separate thread, when the function returns playback
 * is probably not happening yet and any errors might not have occured yet.
 * Applications using playbin should ideally be written to deal with things
 * completely asynchroneous.
 *
 * When playback has finished (an EOS message has been received on the bus)
 * or an error has occured (an ERROR message has been received on the bus) or
 * the user wants to play a different track, playbin should be set back to
 * READY or NULL state, then the #GstPlayBin:uri property should be set to the
 * new location and then playbin be set to PLAYING state again.
 *
 * Seeking can be done using gst_element_seek_simple() or gst_element_seek()
 * on the playbin element. Again, the seek will not be executed
 * instantaneously, but will be done in a background thread. When the seek
 * call returns the seek will most likely still be in process. An application
 * may wait for the seek to finish (or fail) using gst_element_get_state() with
 * -1 as the timeout, but this will block the user interface and is not
 * recommended at all.
 *
 * Applications may query the current position and duration of the stream
 * via gst_element_query_position() and gst_element_query_duration() and
 * setting the format passed to GST_FORMAT_TIME. If the query was successful,
 * the duration or position will have been returned in units of nanoseconds.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Advanced Usage: specifying the audio and video sink</title>
 * <para>
 * By default, if no audio sink or video sink has been specified via the
 * #GstPlayBin:audio-sink or #GstPlayBin:video-sink property, playbin will use
 * the autoaudiosink and autovideosink elements to find the first-best
 * available output method.
 * This should work in most cases, but is not always desirable. Often either
 * the user or application might want to specify more explicitly what to use
 * for audio and video output.
 *
 * If the application wants more control over how audio or video should be
 * output, it may create the audio/video sink elements itself (for example
 * using gst_element_factory_make()) and provide them to playbin using the
 * #GstPlayBin:audio-sink or #GstPlayBin:video-sink property.
 *
 * GNOME-based applications, for example, will usually want to create
 * gconfaudiosink and gconfvideosink elements and make playbin use those,
 * so that output happens to whatever the user has configured in the GNOME
 * Multimedia System Selector configuration dialog.
 *
 * The sink elements do not necessarily need to be ready-made sinks. It is
 * possible to create container elements that look like a sink to playbin,
 * but in reality contain a number of custom elements linked together. This
 * can be achieved by creating a #GstBin and putting elements in there and
 * linking them, and then creating a sink #GstGhostPad for the bin and pointing
 * it to the sink pad of the first element within the bin. This can be used
 * for a number of purposes, for example to force output to a particular
 * format or to modify or observe the data before it is output.
 *
 * It is also possible to 'suppress' audio and/or video output by using
 * 'fakesink' elements (or capture it from there using the fakesink element's
 * "handoff" signal, which, nota bene, is fired from the streaming thread!).
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Retrieving Tags and Other Meta Data</title>
 * <para>
 * Most of the common meta data (artist, title, etc.) can be retrieved by
 * watching for TAG messages on the pipeline's bus (see above).
 *
 * Other more specific meta information like width/height/framerate of video
 * streams or samplerate/number of channels of audio streams can be obtained
 * using the  #GstPlayBaseBin:stream-info property, which will return a GList of
 * stream info objects, one for each stream. These are opaque objects that can
 * only be accessed via the standard GObject property interface, ie. g_object_get().
 * Each stream info object has the following properties:
 * <itemizedlist>
 * <listitem>"object" (GstObject) (the decoder source pad usually)</listitem>
 * <listitem>"type" (enum) (if this is an audio/video/subtitle stream)</listitem>
 * <listitem>"decoder" (string) (name of decoder used to decode this stream)</listitem>
 * <listitem>"mute" (boolean) (to mute or unmute this stream)</listitem>
 * <listitem>"caps" (GstCaps) (caps of the decoded stream)</listitem>
 * <listitem>"language-code" (string) (ISO-639 language code for this stream, mostly used for audio/subtitle streams)</listitem>
 * <listitem>"codec" (string) (format this stream was encoded in)</listitem>
 * </itemizedlist>
 * Stream information from the #GstPlayBaseBin:stream-info property is best queried once
 * playbin has changed into PAUSED or PLAYING state (which can be detected
 * via a state-changed message on the #GstBus where old_state=READY and
 * new_state=PAUSED), since before that the list might not be complete yet or
 * not contain all available information (like language-codes).
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Buffering</title>
 * Playbin handles buffering automatically for the most part, but applications
 * need to handle parts of the buffering process as well. Whenever playbin is
 * buffering, it will post BUFFERING messages on the bus with a percentage
 * value that shows the progress of the buffering process. Applications need
 * to set playbin to PLAYING or PAUSED state in response to these messages.
 * They may also want to convey the buffering progress to the user in some
 * way. Here is how to extract the percentage information from the message
 * (requires GStreamer >= 0.10.11):
 * |[
 * switch (GST_MESSAGE_TYPE (msg)) {
 *   case GST_MESSAGE_BUFFERING: {
 *     gint percent = 0;
 *     gst_message_parse_buffering (msg, &amp;percent);
 *     g_print ("Buffering (%%u percent done)", percent);
 *     break;
 *   }
 *   ...
 * }
 * ]|
 * Note that applications should keep/set the pipeline in the PAUSED state when
 * a BUFFERING message is received with a buffer percent value < 100 and set
 * the pipeline back to PLAYING state when a BUFFERING message with a value
 * of 100 percent is received (if PLAYING is the desired state, that is).
 * </refsect2>
 * <refsect2>
 * <title>Embedding the video window in your application</title>
 * By default, playbin (or rather the video sinks used) will create their own
 * window. Applications will usually want to force output to a window of their
 * own, however. This can be done using the #GstXOverlay interface, which most
 * video sinks implement. See the documentation there for more details.
 * </refsect2>
 * <refsect2>
 * <title>Specifying which CD/DVD device to use</title>
 * The device to use for CDs/DVDs needs to be set on the source element
 * playbin creates before it is opened. The only way to do this at the moment
 * is to connect to playbin's "notify::source" signal, which will be emitted
 * by playbin when it has created the source element for a particular URI.
 * In the signal callback you can check if the source element has a "device"
 * property and set it appropriately. In future ways might be added to specify
 * the device as part of the URI, but at the time of writing this is not
 * possible yet.
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v playbin uri=file:///path/to/somefile.avi
 * ]| This will play back the given AVI video file, given that the video and
 * audio decoders required to decode the content are installed. Since no
 * special audio sink or video sink is supplied (not possible via gst-launch),
 * playbin will try to find a suitable audio and video sink automatically
 * using the autoaudiosink and autovideosink elements.
 * |[
 * gst-launch -v playbin uri=cdda://4
 * ]| This will play back track 4 on an audio CD in your disc drive (assuming
 * the drive is detected automatically by the plugin).
 * |[
 * gst-launch -v playbin uri=dvd://1
 * ]| This will play back title 1 of a DVD in your disc drive (assuming
 * the drive is detected automatically by the plugin).
 * </refsect2>
 *
 * Deprecated: use playbin2 instead
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>

#include <gst/gst-i18n-plugin.h>
#include <gst/pbutils/pbutils.h>

#include "gstplaybasebin.h"
#include "gstplayback.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_bin_debug);
#define GST_CAT_DEFAULT gst_play_bin_debug

#define GST_TYPE_PLAY_BIN               (gst_play_bin_get_type())
#define GST_PLAY_BIN(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BIN,GstPlayBin))
#define GST_PLAY_BIN_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BIN,GstPlayBinClass))
#define GST_IS_PLAY_BIN(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BIN))
#define GST_IS_PLAY_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BIN))

#define VOLUME_MAX_DOUBLE 10.0

typedef struct _GstPlayBin GstPlayBin;
typedef struct _GstPlayBinClass GstPlayBinClass;

/**
 * GstPlayBin:
 *
 * High-level player element
 */
struct _GstPlayBin
{
  GstPlayBaseBin parent;

  /* the configurable elements */
  GstElement *fakesink;
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *visualisation;
  GstElement *pending_visualisation;
  GstElement *volume_element;
  GstElement *textoverlay_element;
  GstElement *spu_element;
  gfloat volume;

  /* these are the currently active sinks */
  GList *sinks;

  /* the last captured frame for snapshots */
  GstBuffer *frame;

  /* our cache for the sinks */
  GHashTable *cache;

  /* font description */
  gchar *font_desc;

  /* indication if the pipeline is live */
  gboolean is_live;
};

struct _GstPlayBinClass
{
  GstPlayBaseBinClass parent_class;
};

/* props */
enum
{
  ARG_0,
  ARG_AUDIO_SINK,
  ARG_VIDEO_SINK,
  ARG_VIS_PLUGIN,
  ARG_VOLUME,
  ARG_FRAME,
  ARG_FONT_DESC
};

/* signals */
enum
{
  LAST_SIGNAL
};

static void gst_play_bin_class_init (GstPlayBinClass * klass);
static void gst_play_bin_init (GstPlayBin * play_bin);
static void gst_play_bin_dispose (GObject * object);

static gboolean setup_sinks (GstPlayBaseBin * play_base_bin,
    GstPlayBaseGroup * group);
static void remove_sinks (GstPlayBin * play_bin);
static void playbin_set_subtitles_visible (GstPlayBaseBin * play_base_bin,
    gboolean visible);
static void playbin_set_audio_mute (GstPlayBaseBin * play_base_bin,
    gboolean mute);

static void gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static gboolean gst_play_bin_send_event (GstElement * element,
    GstEvent * event);
static GstStateChangeReturn gst_play_bin_change_state (GstElement * element,
    GstStateChange transition);

static void gst_play_bin_handle_message (GstBin * bin, GstMessage * message);

static GstElementClass *parent_class;

//static guint gst_play_bin_signals[LAST_SIGNAL] = { 0 };

static GType
gst_play_bin_get_type (void)
{
  static GType gst_play_bin_type = 0;

  if (!gst_play_bin_type) {
    static const GTypeInfo gst_play_bin_info = {
      sizeof (GstPlayBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_bin_class_init,
      NULL,
      NULL,
      sizeof (GstPlayBin),
      0,
      (GInstanceInitFunc) gst_play_bin_init,
      NULL
    };

    gst_play_bin_type = g_type_register_static (GST_TYPE_PLAY_BASE_BIN,
        "GstPlayBin", &gst_play_bin_info, 0);
  }

  return gst_play_bin_type;
}

static void
gst_play_bin_class_init (GstPlayBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;
  GstPlayBaseBinClass *playbasebin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;
  playbasebin_klass = (GstPlayBaseBinClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_play_bin_set_property;
  gobject_klass->get_property = gst_play_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_AUDIO_SINK,
      g_param_spec_object ("audio-sink", "Audio Sink",
          "the audio output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_VIS_PLUGIN,
      g_param_spec_object ("vis-plugin", "Vis plugin",
          "the visualization element to use (NULL = none)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin:volume:
   *
   * Get or set the current audio stream volume. 1.0 means 100%,
   * 0.0 means mute. This uses a linear volume scale.
   *
   */
  g_object_class_install_property (gobject_klass, ARG_VOLUME,
      g_param_spec_double ("volume", "volume", "volume",
          0.0, VOLUME_MAX_DOUBLE, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_FRAME,
      gst_param_spec_mini_object ("frame", "Frame",
          "The last frame (NULL = no video available)", GST_TYPE_BUFFER,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_FONT_DESC,
      g_param_spec_string ("subtitle-font-desc", "Subtitle font description",
          "Pango font description of font " "to be used for subtitle rendering",
          NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gobject_klass->dispose = gst_play_bin_dispose;

  gst_element_class_set_details_simple (gstelement_klass,
      "Player Bin", "Generic/Bin/Player",
      "Autoplug and play media from an uri",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_bin_change_state);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_play_bin_send_event);

  gstbin_klass->handle_message =
      GST_DEBUG_FUNCPTR (gst_play_bin_handle_message);

  playbasebin_klass->setup_output_pads = setup_sinks;
  playbasebin_klass->set_subtitles_visible = playbin_set_subtitles_visible;
  playbasebin_klass->set_audio_mute = playbin_set_audio_mute;
}

static void
gst_play_bin_init (GstPlayBin * play_bin)
{
  play_bin->video_sink = NULL;
  play_bin->audio_sink = NULL;
  play_bin->visualisation = NULL;
  play_bin->pending_visualisation = NULL;
  play_bin->volume_element = NULL;
  play_bin->textoverlay_element = NULL;
  play_bin->spu_element = NULL;
  play_bin->volume = 1.0;
  play_bin->sinks = NULL;
  play_bin->frame = NULL;
  play_bin->font_desc = NULL;
  play_bin->cache = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) gst_object_unref);
}

static void
gst_play_bin_dispose (GObject * object)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);

  if (play_bin->cache != NULL) {
    remove_sinks (play_bin);
    g_hash_table_destroy (play_bin->cache);
    play_bin->cache = NULL;
  }

  if (play_bin->audio_sink != NULL) {
    gst_element_set_state (play_bin->audio_sink, GST_STATE_NULL);
    gst_object_unref (play_bin->audio_sink);
    play_bin->audio_sink = NULL;
  }
  if (play_bin->video_sink != NULL) {
    gst_element_set_state (play_bin->video_sink, GST_STATE_NULL);
    gst_object_unref (play_bin->video_sink);
    play_bin->video_sink = NULL;
  }
  if (play_bin->visualisation != NULL) {
    gst_element_set_state (play_bin->visualisation, GST_STATE_NULL);
    gst_object_unref (play_bin->visualisation);
    play_bin->visualisation = NULL;
  }
  if (play_bin->pending_visualisation != NULL) {
    gst_element_set_state (play_bin->pending_visualisation, GST_STATE_NULL);
    gst_object_unref (play_bin->pending_visualisation);
    play_bin->pending_visualisation = NULL;
  }
  if (play_bin->textoverlay_element != NULL) {
    gst_object_unref (play_bin->textoverlay_element);
    play_bin->textoverlay_element = NULL;
  }
  if (play_bin->volume_element) {
    gst_object_unref (play_bin->volume_element);
    play_bin->volume_element = NULL;
  }
  if (play_bin->spu_element != NULL) {
    gst_object_unref (play_bin->spu_element);
    play_bin->spu_element = NULL;
  }
  g_free (play_bin->font_desc);
  play_bin->font_desc = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_bin_vis_unblocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (user_data);

  if (play_bin->pending_visualisation)
    gst_pad_set_blocked_async (tee_pad, FALSE, gst_play_bin_vis_unblocked,
        play_bin);
}

static void
gst_play_bin_vis_blocked (GstPad * tee_pad, gboolean blocked,
    gpointer user_data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (user_data);
  GstBin *vis_bin = NULL;
  GstPad *vis_sink_pad = NULL, *vis_src_pad = NULL, *vqueue_pad = NULL;
  GstState bin_state;
  GstElement *pending_visualisation;

  GST_OBJECT_LOCK (play_bin);
  pending_visualisation = play_bin->pending_visualisation;
  play_bin->pending_visualisation = NULL;
  GST_OBJECT_UNLOCK (play_bin);

  /* We want to disable visualisation */
  if (!GST_IS_ELEMENT (pending_visualisation)) {
    /* Set visualisation element to READY */
    gst_element_set_state (play_bin->visualisation, GST_STATE_READY);
    goto beach;
  }

  vis_bin =
      GST_BIN_CAST (gst_object_get_parent (GST_OBJECT_CAST
          (play_bin->visualisation)));

  if (!GST_IS_BIN (vis_bin) || !GST_IS_PAD (tee_pad)) {
    goto beach;
  }

  vis_src_pad = gst_element_get_static_pad (play_bin->visualisation, "src");
  vis_sink_pad = gst_pad_get_peer (tee_pad);

  /* Can be fakesink */
  if (GST_IS_PAD (vis_src_pad)) {
    vqueue_pad = gst_pad_get_peer (vis_src_pad);
  }

  if (!GST_IS_PAD (vis_sink_pad)) {
    goto beach;
  }

  /* Check the bin's state */
  GST_OBJECT_LOCK (vis_bin);
  bin_state = GST_STATE (vis_bin);
  GST_OBJECT_UNLOCK (vis_bin);

  /* Unlink */
  gst_pad_unlink (tee_pad, vis_sink_pad);
  gst_object_unref (vis_sink_pad);
  vis_sink_pad = NULL;

  if (GST_IS_PAD (vqueue_pad)) {
    gst_pad_unlink (vis_src_pad, vqueue_pad);
    gst_object_unref (vis_src_pad);
    vis_src_pad = NULL;
  }

  /* Remove from vis_bin */
  gst_bin_remove (vis_bin, play_bin->visualisation);
  /* Set state to NULL */
  gst_element_set_state (play_bin->visualisation, GST_STATE_NULL);
  /* And loose our ref */
  gst_object_unref (play_bin->visualisation);

  if (pending_visualisation) {
    /* Ref this new visualisation element before adding to the bin */
    gst_object_ref (pending_visualisation);
    /* Add the new one */
    gst_bin_add (vis_bin, pending_visualisation);
    /* Synchronizing state */
    gst_element_set_state (pending_visualisation, bin_state);

    vis_sink_pad = gst_element_get_static_pad (pending_visualisation, "sink");
    vis_src_pad = gst_element_get_static_pad (pending_visualisation, "src");

    if (!GST_IS_PAD (vis_sink_pad) || !GST_IS_PAD (vis_src_pad)) {
      goto beach;
    }

    /* Link */
    gst_pad_link (tee_pad, vis_sink_pad);
    gst_pad_link (vis_src_pad, vqueue_pad);
  }

  /* We are done */
  gst_object_unref (play_bin->visualisation);
  play_bin->visualisation = pending_visualisation;

beach:
  if (vis_sink_pad) {
    gst_object_unref (vis_sink_pad);
  }
  if (vis_src_pad) {
    gst_object_unref (vis_src_pad);
  }
  if (vqueue_pad) {
    gst_object_unref (vqueue_pad);
  }
  if (vis_bin) {
    gst_object_unref (vis_bin);
  }

  /* Unblock the pad */
  gst_pad_set_blocked_async (tee_pad, FALSE, gst_play_bin_vis_unblocked,
      play_bin);
}

static void
gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_VIDEO_SINK:
      if (play_bin->video_sink != NULL) {
        gst_object_unref (play_bin->video_sink);
      }
      play_bin->video_sink = g_value_get_object (value);
      if (play_bin->video_sink != NULL) {
        gst_object_ref (play_bin->video_sink);
        gst_object_sink (GST_OBJECT_CAST (play_bin->video_sink));
      }
      /* when changing the videosink, we just remove the
       * video pipeline from the cache so that it will be
       * regenerated with the new sink element */
      g_hash_table_remove (play_bin->cache, "vbin");
      break;
    case ARG_AUDIO_SINK:
      if (play_bin->audio_sink != NULL) {
        gst_object_unref (play_bin->audio_sink);
      }
      if (play_bin->volume_element != NULL) {
        gst_object_unref (play_bin->volume_element);
        play_bin->volume_element = NULL;
      }
      play_bin->audio_sink = g_value_get_object (value);
      if (play_bin->audio_sink != NULL) {
        gst_object_ref (play_bin->audio_sink);
        gst_object_sink (GST_OBJECT_CAST (play_bin->audio_sink));
      }
      g_hash_table_remove (play_bin->cache, "abin");
      break;
    case ARG_VIS_PLUGIN:
    {
      GstElement *pending_visualisation =
          GST_ELEMENT_CAST (g_value_get_object (value));

      /* Take ownership */
      if (pending_visualisation) {
        gst_object_ref (pending_visualisation);
        gst_object_sink (pending_visualisation);
      }

      /* Do we already have a visualisation change pending ? */
      GST_OBJECT_LOCK (play_bin);
      if (play_bin->pending_visualisation) {
        gst_object_unref (play_bin->pending_visualisation);
        play_bin->pending_visualisation = pending_visualisation;
        GST_OBJECT_UNLOCK (play_bin);
      } else {
        GST_OBJECT_UNLOCK (play_bin);
        /* Was there a visualisation already set ? */
        if (play_bin->visualisation != NULL) {
          GstBin *vis_bin = NULL;

          vis_bin =
              GST_BIN_CAST (gst_object_get_parent (GST_OBJECT_CAST
                  (play_bin->visualisation)));

          /* Check if the visualisation is already in a bin */
          if (GST_IS_BIN (vis_bin)) {
            GstPad *vis_sink_pad = NULL, *tee_pad = NULL;

            /* Now get tee pad and block it async */
            vis_sink_pad = gst_element_get_static_pad (play_bin->visualisation,
                "sink");
            if (!GST_IS_PAD (vis_sink_pad)) {
              goto beach;
            }
            tee_pad = gst_pad_get_peer (vis_sink_pad);
            if (!GST_IS_PAD (tee_pad)) {
              goto beach;
            }

            play_bin->pending_visualisation = pending_visualisation;
            /* Block with callback */
            gst_pad_set_blocked_async (tee_pad, TRUE, gst_play_bin_vis_blocked,
                play_bin);
          beach:
            if (vis_sink_pad) {
              gst_object_unref (vis_sink_pad);
            }
            if (tee_pad) {
              gst_object_unref (tee_pad);
            }
            gst_object_unref (vis_bin);
          } else {
            play_bin->visualisation = pending_visualisation;
          }
        } else {
          play_bin->visualisation = pending_visualisation;
        }
      }
      break;
    }
    case ARG_VOLUME:
      play_bin->volume = g_value_get_double (value);
      if (play_bin->volume_element) {
        g_object_set (G_OBJECT (play_bin->volume_element), "volume",
            play_bin->volume, NULL);
      }
      break;
    case ARG_FONT_DESC:
      g_free (play_bin->font_desc);
      play_bin->font_desc = g_strdup (g_value_get_string (value));
      if (play_bin->textoverlay_element) {
        g_object_set (G_OBJECT (play_bin->textoverlay_element),
            "font-desc", g_value_get_string (value), NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case ARG_VIDEO_SINK:
      g_value_set_object (value, play_bin->video_sink);
      break;
    case ARG_AUDIO_SINK:
      g_value_set_object (value, play_bin->audio_sink);
      break;
    case ARG_VIS_PLUGIN:
      g_value_set_object (value, play_bin->visualisation);
      break;
    case ARG_VOLUME:
      g_value_set_double (value, play_bin->volume);
      break;
    case ARG_FRAME:{
      GstBuffer *cur_frame = NULL;

      gst_buffer_replace (&cur_frame, play_bin->frame);
      gst_value_take_buffer (value, cur_frame);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* signal fired when the identity has received a new buffer. This is used for
 * making screenshots.
 */
static void
handoff (GstElement * identity, GstBuffer * frame, gpointer data)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (data);

  /* applications need to know the buffer caps,
   * make sure they are always set on the frame */
  if (GST_BUFFER_CAPS (frame) == NULL) {
    GstPad *pad;

    if ((pad = gst_element_get_static_pad (identity, "sink"))) {
      gst_buffer_set_caps (frame, GST_PAD_CAPS (pad));
      gst_object_unref (pad);
    }
  }

  gst_buffer_replace (&play_bin->frame, frame);
}

static void
post_missing_element_message (GstPlayBin * playbin, const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (GST_ELEMENT_CAST (playbin), name);
  gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);
}

/* make the element (bin) that contains the elements needed to perform
 * video display. We connect a handoff signal to identity so that we
 * can grab snapshots. Identity's sinkpad is ghosted to vbin.
 *
 *  +-------------------------------------------------------------+
 *  | vbin                                                        |
 *  |      +--------+   +----------+   +----------+   +---------+ |
 *  |      |identity|   |colorspace|   |videoscale|   |videosink| |
 *  |   +-sink     src-sink       src-sink       src-sink       | |
 *  |   |  +---+----+   +----------+   +----------+   +---------+ |
 * sink-+      |                                                  |
 *  +----------|--------------------------------------------------+
 *           handoff
 */
static GstElement *
gen_video_element (GstPlayBin * play_bin)
{
  GstElement *element;
  GstElement *conv;

  GstElement *scale;
  GstElement *sink;
  GstElement *identity;
  GstPad *pad;

  /* first see if we have it in the cache */
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    return element;
  }

  if (play_bin->video_sink) {
    sink = play_bin->video_sink;
  } else {
    sink = gst_element_factory_make ("autovideosink", "videosink");
    if (sink == NULL) {
      sink = gst_element_factory_make ("xvimagesink", "videosink");
    }
    if (sink == NULL)
      goto no_sinks;
  }
  gst_object_ref (sink);
  g_hash_table_insert (play_bin->cache, (gpointer) "video_sink", sink);

  /* create a bin to hold objects, as we create them we add them to this bin so
   * that when something goes wrong we only need to unref the bin */
  element = gst_bin_new ("vbin");
  gst_bin_add (GST_BIN_CAST (element), sink);

  conv = gst_element_factory_make (COLORSPACE, "vconv");
  if (conv == NULL)
    goto no_colorspace;
  gst_bin_add (GST_BIN_CAST (element), conv);

  scale = gst_element_factory_make ("videoscale", "vscale");
  if (scale == NULL)
    goto no_videoscale;
  gst_bin_add (GST_BIN_CAST (element), scale);

  identity = gst_element_factory_make ("identity", "id");
  g_object_set (identity, "silent", TRUE, NULL);
  g_signal_connect (identity, "handoff", G_CALLBACK (handoff), play_bin);
  gst_bin_add (GST_BIN_CAST (element), identity);

  gst_element_link_pads (identity, "src", conv, "sink");
  gst_element_link_pads (conv, "src", scale, "sink");
  /* be more careful with the pad from the custom sink element, it might not
   * be named 'sink' */
  if (!gst_element_link_pads (scale, "src", sink, NULL))
    goto link_failed;

  pad = gst_element_get_static_pad (identity, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (element);
  g_hash_table_insert (play_bin->cache, (gpointer) "vbin", element);

  return element;

  /* ERRORS */
no_sinks:
  {
    post_missing_element_message (play_bin, "autovideosink");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Both autovideosink and xvimagesink elements are missing.")),
        (NULL));
    return NULL;
  }
no_colorspace:
  {
    post_missing_element_message (play_bin, COLORSPACE);
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            COLORSPACE), (NULL));
    gst_object_unref (element);
    return NULL;
  }

no_videoscale:
  {
    post_missing_element_message (play_bin, "videoscale");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "videoscale"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
link_failed:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, PAD,
        (NULL), ("Failed to configure the video sink."));
    gst_object_unref (element);
    return NULL;
  }
}

/* make an element for playback of video with subtitles embedded.
 *
 *  +--------------------------------------------------+
 *  | tbin                  +-------------+            |
 *  |          +-----+      | textoverlay |   +------+ |
 *  |          | csp | +--video_sink      |   | vbin | |
 * video_sink-sink  src+ +-text_sink    src---sink   | |
 *  |          +-----+   |  +-------------+   +------+ |
 * text_sink-------------+                             |
 *  +--------------------------------------------------+
 *
 *  If there is no subtitle renderer this function will simply return the
 *  videosink without the text_sink pad.
 */
static GstElement *
add_text_element (GstPlayBin * play_bin, GstElement * vbin)
{
  GstElement *element, *csp, *overlay;
  GstPad *pad;

  /* Text overlay */
  overlay = gst_element_factory_make ("textoverlay", "overlay");

  /* If no overlay return the video bin without subtitle support. */
  if (!overlay)
    goto no_overlay;

  /* Create our bin */
  element = gst_bin_new ("textbin");

  /* Set some parameters */
  g_object_set (G_OBJECT (overlay),
      "halign", "center", "valign", "bottom", NULL);
  if (play_bin->font_desc) {
    g_object_set (G_OBJECT (overlay), "font-desc", play_bin->font_desc, NULL);
  }

  /* Take a ref */
  play_bin->textoverlay_element = GST_ELEMENT_CAST (gst_object_ref (overlay));

  /* we know this will succeed, as the video bin already created one before */
  csp = gst_element_factory_make (COLORSPACE, "subtitlecsp");

  /* Add our elements */
  gst_bin_add_many (GST_BIN_CAST (element), csp, overlay, vbin, NULL);

  /* Link */
  gst_element_link_pads (csp, "src", overlay, "video_sink");
  gst_element_link_pads (overlay, "src", vbin, "sink");

  /* Add ghost pads on the subtitle bin */
  pad = gst_element_get_static_pad (overlay, "text_sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("text_sink", pad));
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (csp, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  /* If the vbin provides a subpicture sink pad, ghost it too */
  pad = gst_element_get_static_pad (vbin, "subpicture_sink");
  if (pad) {
    gst_element_add_pad (element, gst_ghost_pad_new ("subpicture_sink", pad));
    gst_object_unref (pad);
  }

  /* Set state to READY */
  gst_element_set_state (element, GST_STATE_READY);

  return element;

  /* ERRORS */
no_overlay:
  {
    post_missing_element_message (play_bin, "textoverlay");
    GST_WARNING_OBJECT (play_bin,
        "No overlay (pango) element, subtitles disabled");
    return vbin;
  }
}

/* make an element for rendering DVD subpictures onto output video
 *
 *  +---------------------------------------------+
 *  | tbin                   +--------+           |
 *  |          +-----+       |        |  +------+ |
 *  |          | csp | src-videosink  |  | vbin | |
 * video_sink-sink  src+     |       src-sink   | |
 *  |          +-----+   +subpicture  |  +------+ |
 * subpicture_pad--------+   +--------+           |
 *  +---------- ----------------------------------+
 *
 */
static GstElement *
add_spu_element (GstPlayBin * play_bin, GstElement * vbin)
{
  GstElement *element, *csp, *overlay;
  GstPad *pad;

  /* DVD spu overlay */
  GST_DEBUG_OBJECT (play_bin, "Attempting to insert DVD SPU element");

  overlay = gst_element_factory_make ("dvdspu", "overlay");

  /* If no overlay return the video bin without subpicture support. */
  if (!overlay)
    goto no_overlay;

  /* Create our bin */
  element = gst_bin_new ("spubin");

  /* Take a ref */
  play_bin->spu_element = GST_ELEMENT_CAST (gst_object_ref (overlay));

  /* we know this will succeed, as the video bin already created one before */
  csp = gst_element_factory_make (COLORSPACE, "spucsp");

  /* Add our elements */
  gst_bin_add_many (GST_BIN_CAST (element), csp, overlay, vbin, NULL);

  /* Link */
  gst_element_link_pads (csp, "src", overlay, "video");
  gst_element_link_pads (overlay, "src", vbin, "sink");

  /* Add ghost pad on the subpicture bin so it looks like vbin */
  pad = gst_element_get_static_pad (csp, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (overlay, "subpicture");
  gst_element_add_pad (element, gst_ghost_pad_new ("subpicture_sink", pad));
  gst_object_unref (pad);

  /* Set state to READY */
  gst_element_set_state (element, GST_STATE_READY);

  return element;

  /* ERRORS */
no_overlay:
  {
    post_missing_element_message (play_bin, "dvdspu");
    GST_WARNING_OBJECT (play_bin,
        "No DVD overlay (dvdspu) element. "
        "menu highlight/subtitles unavailable");
    return vbin;
  }
}

/* make the element (bin) that contains the elements needed to perform
 * audio playback.
 *
 *  +-------------------------------------------------------------+
 *  | abin                                                        |
 *  |      +---------+   +----------+   +---------+   +---------+ |
 *  |      |audioconv|   |audioscale|   | volume  |   |audiosink| |
 *  |   +-sink      src-sink       src-sink      src-sink       | |
 *  |   |  +---------+   +----------+   +---------+   +---------+ |
 * sink-+                                                         |
 *  +-------------------------------------------------------------+
 */
static GstElement *
gen_audio_element (GstPlayBin * play_bin)
{
  gboolean res;
  GstElement *element;
  GstElement *conv;
  GstElement *scale;
  GstElement *sink;
  GstElement *volume;
  GstPad *pad;

  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL)
    return element;

  if (play_bin->audio_sink) {
    sink = play_bin->audio_sink;
  } else {
    sink = gst_element_factory_make ("autoaudiosink", "audiosink");
    if (sink == NULL) {
      sink = gst_element_factory_make ("alsasink", "audiosink");
    }
    if (sink == NULL)
      goto no_sinks;

    play_bin->audio_sink = GST_ELEMENT_CAST (gst_object_ref (sink));
  }

  gst_object_ref (sink);
  g_hash_table_insert (play_bin->cache, (gpointer) "audio_sink", sink);

  element = gst_bin_new ("abin");
  gst_bin_add (GST_BIN_CAST (element), sink);

  conv = gst_element_factory_make ("audioconvert", "aconv");
  if (conv == NULL)
    goto no_audioconvert;
  gst_bin_add (GST_BIN_CAST (element), conv);

  scale = gst_element_factory_make ("audioresample", "aresample");
  if (scale == NULL)
    goto no_audioresample;
  gst_bin_add (GST_BIN_CAST (element), scale);

  volume = gst_element_factory_make ("volume", "volume");
  if (volume == NULL)
    goto no_volume;
  g_object_set (G_OBJECT (volume), "volume", play_bin->volume, NULL);
  play_bin->volume_element = GST_ELEMENT_CAST (gst_object_ref (volume));
  gst_bin_add (GST_BIN_CAST (element), volume);

  res = gst_element_link_pads (conv, "src", scale, "sink");
  res &= gst_element_link_pads (scale, "src", volume, "sink");
  res &= gst_element_link_pads (volume, "src", sink, NULL);
  if (!res)
    goto link_failed;

  pad = gst_element_get_static_pad (conv, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  gst_element_set_state (element, GST_STATE_READY);

  /* since we're gonna add it to a bin but don't want to lose it,
   * we keep a reference. */
  gst_object_ref (element);
  g_hash_table_insert (play_bin->cache, (gpointer) "abin", element);

  return element;

  /* ERRORS */
no_sinks:
  {
    post_missing_element_message (play_bin, "autoaudiosink");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Both autoaudiosink and alsasink elements are missing.")), (NULL));
    return NULL;
  }
no_audioconvert:
  {
    post_missing_element_message (play_bin, "audioconvert");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioconvert"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
no_audioresample:
  {
    post_missing_element_message (play_bin, "audioresample");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioresample"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
no_volume:
  {
    post_missing_element_message (play_bin, "volume");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "volume"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
link_failed:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, PAD,
        (NULL), ("Failed to configure the audio sink."));
    gst_object_unref (element);
    return NULL;
  }
}

/* make the element (bin) that contains the elements needed to perform
 * visualisation output.  The idea is to split the audio using tee, then
 * sending the output to the regular audio bin and the other output to
 * the vis plugin that transforms it into a video that is rendered with the
 * normal video bin. The video and audio bins are run in threads to make sure
 * they don't block eachother.
 *
 *  +-----------------------------------------------------------------------+
 *  | visbin                                                                |
 *  |      +------+   +--------+   +----------------+                       |
 *  |      | tee  |   | aqueue |   |   abin ...     |                       |
 *  |   +-sink   src-sink     src-sink              |                       |
 *  |   |  |      |   +--------+   +----------------+                       |
 *  |   |  |      |                                                         |
 *  |   |  |      |   +------+   +------------+   +------+   +-----------+  |
 *  |   |  |      |   |vqueue|   | audioconv  |   | vis  |   | vbin ...  |  |
 *  |   |  |     src-sink   src-sink + samp  src-sink   src-sink         |  |
 *  |   |  |      |   +------+   +------------+   +------+   +-----------+  |
 *  |   |  |      |                                                         |
 *  |   |  +------+                                                         |
 * sink-+                                                                   |
 *  +-----------------------------------------------------------------------+
 */
static GstElement *
gen_vis_element (GstPlayBin * play_bin)
{
  gboolean res;
  GstElement *element;
  GstElement *tee;
  GstElement *asink;
  GstElement *vsink;
  GstElement *conv;
  GstElement *resamp;
  GstElement *conv2;
  GstElement *vis;
  GstElement *vqueue, *aqueue;
  GstPad *pad, *rpad;

  /* errors are already posted when these fail. */
  asink = gen_audio_element (play_bin);
  if (!asink)
    return NULL;
  vsink = gen_video_element (play_bin);
  if (!vsink) {
    gst_object_unref (asink);
    return NULL;
  }

  element = gst_bin_new ("visbin");
  tee = gst_element_factory_make ("tee", "tee");

  vqueue = gst_element_factory_make ("queue", "vqueue");
  aqueue = gst_element_factory_make ("queue", "aqueue");

  gst_bin_add (GST_BIN_CAST (element), asink);
  gst_bin_add (GST_BIN_CAST (element), vqueue);
  gst_bin_add (GST_BIN_CAST (element), aqueue);
  gst_bin_add (GST_BIN_CAST (element), vsink);
  gst_bin_add (GST_BIN_CAST (element), tee);

  conv = gst_element_factory_make ("audioconvert", "aconv");
  if (conv == NULL)
    goto no_audioconvert;
  gst_bin_add (GST_BIN_CAST (element), conv);

  resamp = gst_element_factory_make ("audioresample", "aresamp");
  if (resamp == NULL)
    goto no_audioresample;
  gst_bin_add (GST_BIN_CAST (element), resamp);

  conv2 = gst_element_factory_make ("audioconvert", "aconv2");
  if (conv2 == NULL)
    goto no_audioconvert;
  gst_bin_add (GST_BIN_CAST (element), conv2);

  if (play_bin->visualisation) {
    gst_object_ref (play_bin->visualisation);
    vis = play_bin->visualisation;
  } else {
    vis = gst_element_factory_make ("goom", "vis");
    if (!vis)
      goto no_goom;
  }
  gst_bin_add (GST_BIN_CAST (element), vis);

  res = gst_element_link_pads (vqueue, "src", conv, "sink");
  res &= gst_element_link_pads (conv, "src", resamp, "sink");
  res &= gst_element_link_pads (resamp, "src", conv2, "sink");
  res &= gst_element_link_pads (conv2, "src", vis, "sink");
  res &= gst_element_link_pads (vis, "src", vsink, "sink");
  if (!res)
    goto link_failed;

  pad = gst_element_get_static_pad (aqueue, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_pads (aqueue, "src", asink, "sink");

  pad = gst_element_get_static_pad (vqueue, "sink");
  rpad = gst_element_get_request_pad (tee, "src%d");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (tee, "sink");
  gst_element_add_pad (element, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  return element;

  /* ERRORS */
no_audioconvert:
  {
    post_missing_element_message (play_bin, "audioconvert");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioconvert"), ("possibly a liboil version mismatch?"));
    gst_object_unref (element);
    return NULL;
  }
no_audioresample:
  {
    post_missing_element_message (play_bin, "audioresample");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "audioresample"), (NULL));
    gst_object_unref (element);
    return NULL;
  }
no_goom:
  {
    post_missing_element_message (play_bin, "goom");
    GST_ELEMENT_ERROR (play_bin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "goom"), (NULL));
    gst_object_unref (element);
    return NULL;
  }
link_failed:
  {
    GST_ELEMENT_ERROR (play_bin, CORE, PAD,
        (NULL), ("Failed to configure the visualisation element."));
    gst_object_unref (element);
    return NULL;
  }
}

/* get rid of all installed sinks */
static void
remove_sinks (GstPlayBin * play_bin)
{
  GList *sinks;
  GstObject *parent;
  GstElement *element;
  GstPad *pad, *peer;

  if (play_bin->cache == NULL)
    return;

  GST_DEBUG ("removesinks");
  element = g_hash_table_lookup (play_bin->cache, "abin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      /* we remove the element from the parent so that
       * there is no unwanted state change when the parent
       * is disposed */
      play_bin->sinks = g_list_remove (play_bin->sinks, element);
      gst_element_set_state (element, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (parent), element);
      gst_object_unref (parent);
    }
    pad = gst_element_get_static_pad (element, "sink");
    if (pad != NULL) {
      peer = gst_pad_get_peer (pad);
      if (peer != NULL) {
        gst_pad_unlink (peer, pad);
        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }
  element = g_hash_table_lookup (play_bin->cache, "vbin");
  if (element != NULL) {
    parent = gst_element_get_parent (element);
    if (parent != NULL) {
      play_bin->sinks = g_list_remove (play_bin->sinks, element);
      gst_element_set_state (element, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (parent), element);
      gst_object_unref (parent);
    }
    pad = gst_element_get_static_pad (element, "sink");
    if (pad != NULL) {
      peer = gst_pad_get_peer (pad);
      if (peer != NULL) {
        gst_pad_unlink (peer, pad);
        gst_object_unref (peer);
      }
      gst_object_unref (pad);
    }
  }

  for (sinks = play_bin->sinks; sinks; sinks = g_list_next (sinks)) {
    GstElement *element = GST_ELEMENT_CAST (sinks->data);
    GstPad *pad;
    GstPad *peer;

    pad = gst_element_get_static_pad (element, "sink");

    GST_LOG ("removing sink %p", element);

    peer = gst_pad_get_peer (pad);
    if (peer) {
      gst_pad_unlink (peer, pad);
      gst_object_unref (peer);
    }
    gst_object_unref (pad);

    gst_element_set_state (element, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (play_bin), element);
  }
  g_list_free (play_bin->sinks);
  play_bin->sinks = NULL;

  if (play_bin->visualisation) {
    GstElement *vis_bin;

    vis_bin =
        GST_ELEMENT_CAST (gst_element_get_parent (play_bin->visualisation));

    gst_element_set_state (play_bin->visualisation, GST_STATE_NULL);

    if (vis_bin) {
      gst_bin_remove (GST_BIN_CAST (vis_bin), play_bin->visualisation);
      gst_object_unref (vis_bin);
    }
  }

  if (play_bin->frame) {
    gst_buffer_unref (play_bin->frame);
    play_bin->frame = NULL;
  }

  if (play_bin->textoverlay_element) {
    gst_object_unref (play_bin->textoverlay_element);
    play_bin->textoverlay_element = NULL;
  }
}

/* loop over the streams and set up the pipeline to play this
 * media file. First we count the number of audio and video streams.
 * If there is no video stream but there exists an audio stream,
 * we install a visualisation pipeline.
 *
 * Also make sure to only connect the first audio and video pad. FIXME
 * this should eventually be handled with a tuner interface so that
 * one can switch the streams.
 *
 * This function takes ownership of @sink.*
 */
static gboolean
add_sink (GstPlayBin * play_bin, GstElement * sink, GstPad * srcpad,
    GstPad * subtitle_pad)
{
  GstPad *sinkpad;
  GstPadLinkReturn linkres;
  GstElement *parent;
  GstStateChangeReturn stateret;
  GstState state;

  g_return_val_if_fail (sink != NULL, FALSE);

  state = GST_STATE_PAUSED;

  /* this is only for debugging */
  parent = gst_pad_get_parent_element (srcpad);
  if (parent) {
    GST_DEBUG ("Adding sink %" GST_PTR_FORMAT
        " with state %d (parent: %d, peer: %d)", sink,
        GST_STATE (sink), GST_STATE (play_bin), GST_STATE (parent));
    gst_object_unref (parent);
  }
  gst_bin_add (GST_BIN_CAST (play_bin), sink);

  /* bring it to the required state so we can link to the peer without
   * breaking the flow */
  stateret = gst_element_set_state (sink, state);
  if (stateret == GST_STATE_CHANGE_FAILURE)
    goto state_failed;

  /* we found a sink for this stream, now try to install it */
  sinkpad = gst_element_get_static_pad (sink, "sink");
  linkres = gst_pad_link (srcpad, sinkpad);
  gst_object_unref (sinkpad);

  /* try to link the pad of the sink to the stream */
  if (GST_PAD_LINK_FAILED (linkres))
    goto link_failed;

  if (GST_IS_PAD (subtitle_pad)) {
    sinkpad = gst_element_get_static_pad (sink, "text_sink");
    linkres = gst_pad_link (subtitle_pad, sinkpad);
    gst_object_unref (sinkpad);
  }

  /* try to link the subtitle pad of the sink to the stream, this is not
   * fatal. */
  if (GST_PAD_LINK_FAILED (linkres))
    goto subtitle_failed;

done:
  /* we got the sink successfully linked, now keep the sink
   * in our internal list */
  play_bin->sinks = g_list_prepend (play_bin->sinks, sink);

  return TRUE;

  /* ERRORS */
state_failed:
  {
    gst_element_set_state (sink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (play_bin), sink);
    GST_DEBUG_OBJECT (play_bin, "state change failure when adding sink");
    return FALSE;
  }
link_failed:
  {
    gchar *capsstr;
    GstCaps *caps;

    /* could not link this stream */
    caps = gst_pad_get_caps (srcpad);
    capsstr = gst_caps_to_string (caps);
    g_warning ("could not link %s: %d", capsstr, linkres);
    GST_DEBUG_OBJECT (play_bin,
        "link failed when adding sink, caps %s, reason %d", capsstr, linkres);
    g_free (capsstr);
    gst_caps_unref (caps);

    gst_element_set_state (sink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (play_bin), sink);
    return FALSE;
  }
subtitle_failed:
  {
    GstCaps *caps;

    /* could not link this stream */
    caps = gst_pad_get_caps (subtitle_pad);
    GST_WARNING_OBJECT (play_bin, "subtitle link failed when adding sink, "
        "caps = %" GST_PTR_FORMAT ", reason %d", caps, linkres);
    gst_caps_unref (caps);

    /* not fatal */
    goto done;
  }
}

static void
dummy_blocked_cb (GstPad * pad, gboolean blocked, gpointer user_data)
{
}

static gboolean
setup_sinks (GstPlayBaseBin * play_base_bin, GstPlayBaseGroup * group)
{
  GstPlayBin *play_bin = GST_PLAY_BIN (play_base_bin);
  gboolean have_video = FALSE;
  gboolean need_vis = FALSE;
  gboolean need_text = FALSE;
  gboolean need_spu = FALSE;
  GstPad *textsrcpad = NULL, *pad = NULL, *origtextsrcpad = NULL;
  GstElement *sink;
  gboolean res = TRUE;

  /* get rid of existing sinks */
  if (play_bin->sinks) {
    remove_sinks (play_bin);
  }
  GST_DEBUG_OBJECT (play_base_bin, "setupsinks");

  /* find out what to do */
  have_video = (group->type[GST_STREAM_TYPE_VIDEO - 1].npads > 0);
  need_spu = (group->type[GST_STREAM_TYPE_SUBPICTURE - 1].npads != 0);

  if (have_video && group->type[GST_STREAM_TYPE_TEXT - 1].npads > 0) {
    need_text = TRUE;
  } else if (!have_video &&
      group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0 &&
      play_bin->visualisation != NULL) {
    need_vis = TRUE;
  }

  /* now actually connect everything */

  /* link audio */
  if (group->type[GST_STREAM_TYPE_AUDIO - 1].npads > 0) {
    if (need_vis) {
      sink = gen_vis_element (play_bin);
    } else {
      sink = gen_audio_element (play_bin);
    }
    if (!sink)
      return FALSE;

    pad =
        gst_element_get_static_pad (group->type[GST_STREAM_TYPE_AUDIO -
            1].preroll, "src");
    res = add_sink (play_bin, sink, pad, NULL);
    gst_object_unref (pad);
  }

  /* link video */
  if (have_video) {
    /* Create the video rendering bin, error is posted when this fails. */
    sink = gen_video_element (play_bin);
    if (!sink)
      return FALSE;
    if (need_spu) {
      sink = add_spu_element (play_bin, sink);
    }

    if (need_text) {
      GstObject *parent = NULL, *grandparent = NULL;
      GstPad *ghost = NULL;

      /* Add the subtitle overlay element into the video sink */
      sink = add_text_element (play_bin, sink);

      /* Link the incoming subtitle stream into the output bin */
      textsrcpad =
          gst_element_get_static_pad (group->type[GST_STREAM_TYPE_TEXT -
              1].preroll, "src");

      /* This pad is from subtitle-bin, we need to create a ghost pad to have
         common grandparents */
      parent = gst_object_get_parent (GST_OBJECT_CAST (textsrcpad));
      if (!parent) {
        GST_WARNING_OBJECT (textsrcpad, "subtitle pad has no parent !");
        gst_object_unref (textsrcpad);
        textsrcpad = NULL;
        goto beach;
      }

      grandparent = gst_object_get_parent (parent);
      if (!grandparent) {
        GST_WARNING_OBJECT (textsrcpad, "subtitle pad has no grandparent !");
        gst_object_unref (parent);
        gst_object_unref (textsrcpad);
        textsrcpad = NULL;
        goto beach;
      }

      /* We ghost the pad on subtitle_bin only, if the text pad is from the
         media demuxer we keep it as it is */
      if (!GST_IS_PLAY_BIN (grandparent)) {
        GST_DEBUG_OBJECT (textsrcpad, "this subtitle pad is from a subtitle "
            "file, ghosting to a suitable hierarchy");
        /* Block the pad first, because as soon as we add a ghostpad, the queue
         * will try and start pushing */
        gst_pad_set_blocked_async (textsrcpad, TRUE, dummy_blocked_cb, NULL);
        origtextsrcpad = gst_object_ref (textsrcpad);

        ghost = gst_ghost_pad_new ("text_src", textsrcpad);
        if (!GST_IS_PAD (ghost)) {
          GST_WARNING_OBJECT (textsrcpad, "failed creating ghost pad for "
              "subtitle-bin");
          gst_object_unref (parent);
          gst_object_unref (grandparent);
          gst_object_unref (textsrcpad);
          textsrcpad = NULL;
          goto beach;
        }

        gst_pad_set_active (ghost, TRUE);
        if (gst_element_add_pad (GST_ELEMENT_CAST (grandparent), ghost)) {
          gst_object_unref (textsrcpad);
          textsrcpad = gst_object_ref (ghost);
        } else {
          GST_WARNING_OBJECT (ghost, "failed adding ghost pad on subtitle-bin");
          gst_pad_set_active (ghost, FALSE);
          gst_object_unref (ghost);
          gst_object_unref (textsrcpad);
          textsrcpad = NULL;
        }
      } else {
        GST_DEBUG_OBJECT (textsrcpad, "this subtitle pad is from the demuxer "
            "no changes to hierarchy needed");
      }

      gst_object_unref (parent);
      gst_object_unref (grandparent);
    }
  beach:
    if (!sink)
      return FALSE;
    pad =
        gst_element_get_static_pad (group->type[GST_STREAM_TYPE_VIDEO -
            1].preroll, "src");
    res = add_sink (play_bin, sink, pad, textsrcpad);
    gst_object_unref (pad);
    if (textsrcpad)
      gst_object_unref (textsrcpad);
    if (origtextsrcpad) {
      gst_pad_set_blocked_async (origtextsrcpad, FALSE, dummy_blocked_cb, NULL);
      gst_object_unref (origtextsrcpad);
    }

    /* If we have a DVD subpicture stream, link it to the SPU now */
    if (need_spu) {
      GstPad *subpic_pad;
      GstPad *spu_sink_pad;

      subpic_pad =
          gst_element_get_static_pad (group->type[GST_STREAM_TYPE_SUBPICTURE
              - 1].preroll, "src");
      spu_sink_pad = gst_element_get_static_pad (sink, "subpicture_sink");
      if (subpic_pad && spu_sink_pad) {
        GST_LOG_OBJECT (play_bin, "Linking DVD subpicture stream onto SPU");
        gst_pad_set_blocked_async (subpic_pad, TRUE, dummy_blocked_cb, NULL);
        if (gst_pad_link (subpic_pad, spu_sink_pad) != GST_PAD_LINK_OK) {
          GST_WARNING_OBJECT (play_bin,
              "Failed to link DVD subpicture stream onto SPU");
        }
        gst_pad_set_blocked_async (subpic_pad, FALSE, dummy_blocked_cb, NULL);
      }
      if (subpic_pad)
        gst_object_unref (subpic_pad);
      if (spu_sink_pad)
        gst_object_unref (spu_sink_pad);
    }
  }

  /* remove the sinks now, pipeline get_state will now wait for the
   * sinks to preroll */
  if (play_bin->fakesink) {
    gst_element_set_state (play_bin->fakesink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (play_bin), play_bin->fakesink);
    play_bin->fakesink = NULL;
  }

  return res;
}

static void
playbin_set_subtitles_visible (GstPlayBaseBin * play_base_bin, gboolean visible)
{
  GstPlayBin *playbin = GST_PLAY_BIN (play_base_bin);

  /* we're ignoring the case of someone setting the 'current-text' property
   * before textoverlay is set up (which is probably okay, since playbasebin
   * will just select the first subtitle stream as active stream regardless) */
  if (playbin->textoverlay_element != NULL) {
    GST_LOG_OBJECT (playbin, "setting subtitle visibility to %d", visible);
    g_object_set (playbin->textoverlay_element, "silent", !visible, NULL);
  }
}

static void
playbin_set_audio_mute (GstPlayBaseBin * play_base_bin, gboolean mute)
{
  GstPlayBin *playbin = GST_PLAY_BIN (play_base_bin);

  if (playbin->volume_element) {
    g_object_set (G_OBJECT (playbin->volume_element), "mute", mute, NULL);
  }
}

/* Send an event to our sinks until one of them works; don't then send to the
 * remaining sinks (unlike GstBin)
 */
static gboolean
gst_play_bin_send_event_to_sink (GstPlayBin * play_bin, GstEvent * event)
{
  GList *sinks = play_bin->sinks;
  gboolean res = TRUE;

  while (sinks) {
    GstElement *sink = GST_ELEMENT_CAST (sinks->data);

    gst_event_ref (event);
    if ((res = gst_element_send_event (sink, event))) {
      GST_DEBUG_OBJECT (play_bin,
          "Sent event successfully to sink %" GST_PTR_FORMAT, sink);
      break;
    }
    GST_DEBUG_OBJECT (play_bin,
        "Event failed when sent to sink %" GST_PTR_FORMAT, sink);

    sinks = g_list_next (sinks);
  }

  gst_event_unref (event);

  return res;
}

/* We only want to send the event to a single sink (overriding GstBin's
 * behaviour), but we want to keep GstPipeline's behaviour - wrapping seek
 * events appropriately. So, this is a messy duplication of code. */
static gboolean
gst_play_bin_send_event (GstElement * element, GstEvent * event)
{
  gboolean res = FALSE;
  GstEventType event_type = GST_EVENT_TYPE (event);

  switch (event_type) {
    case GST_EVENT_SEEK:
      GST_DEBUG_OBJECT (element, "Sending seek event to a sink");
      res = gst_play_bin_send_event_to_sink (GST_PLAY_BIN (element), event);
      break;
    default:
      res = parent_class->send_event (element, event);
      break;
  }

  return res;
}

static void
value_list_append_structure_list (GValue * list_val, GstStructure ** first,
    GList * structure_list)
{
  GList *l;

  for (l = structure_list; l != NULL; l = l->next) {
    GValue val = { 0, };

    if (*first == NULL)
      *first = gst_structure_copy ((GstStructure *) l->data);

    g_value_init (&val, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&val, gst_structure_copy ((GstStructure *) l->data));
    gst_value_list_append_value (list_val, &val);
    g_value_unset (&val);
  }
}

/* if it's a redirect message with multiple redirect locations we might
 * want to pick a different 'best' location depending on the required
 * bitrates and the connection speed */
static GstMessage *
gst_play_bin_handle_redirect_message (GstPlayBin * playbin, GstMessage * msg)
{
  const GValue *locations_list, *location_val;
  GstMessage *new_msg;
  GstStructure *new_structure = NULL;
  GList *l_good = NULL, *l_neutral = NULL, *l_bad = NULL;
  GValue new_list = { 0, };
  guint size, i;
  GstPlayBaseBin *playbasebin = GST_PLAY_BASE_BIN (playbin);
  guint connection_speed = playbasebin->connection_speed;

  GST_DEBUG_OBJECT (playbin, "redirect message: %" GST_PTR_FORMAT, msg);
  GST_DEBUG_OBJECT (playbin, "connection speed: %u", connection_speed);

  if (connection_speed == 0 || msg->structure == NULL)
    return msg;

  locations_list = gst_structure_get_value (msg->structure, "locations");
  if (locations_list == NULL)
    return msg;

  size = gst_value_list_get_size (locations_list);
  if (size < 2)
    return msg;

  /* maintain existing order as much as possible, just sort references
   * with too high a bitrate to the end (the assumption being that if
   * bitrates are given they are given for all interesting streams and
   * that the you-need-at-least-version-xyz redirect has the same bitrate
   * as the lowest referenced redirect alternative) */
  for (i = 0; i < size; ++i) {
    const GstStructure *s;
    gint bitrate = 0;

    location_val = gst_value_list_get_value (locations_list, i);
    s = (const GstStructure *) g_value_get_boxed (location_val);
    if (!gst_structure_get_int (s, "minimum-bitrate", &bitrate) || bitrate <= 0) {
      GST_DEBUG_OBJECT (playbin, "no bitrate: %" GST_PTR_FORMAT, s);
      l_neutral = g_list_append (l_neutral, (gpointer) s);
    } else if (bitrate > connection_speed) {
      GST_DEBUG_OBJECT (playbin, "bitrate too high: %" GST_PTR_FORMAT, s);
      l_bad = g_list_append (l_bad, (gpointer) s);
    } else if (bitrate <= connection_speed) {
      GST_DEBUG_OBJECT (playbin, "bitrate OK: %" GST_PTR_FORMAT, s);
      l_good = g_list_append (l_good, (gpointer) s);
    }
  }

  g_value_init (&new_list, GST_TYPE_LIST);
  value_list_append_structure_list (&new_list, &new_structure, l_good);
  value_list_append_structure_list (&new_list, &new_structure, l_neutral);
  value_list_append_structure_list (&new_list, &new_structure, l_bad);
  gst_structure_set_value (new_structure, "locations", &new_list);
  g_value_unset (&new_list);

  g_list_free (l_good);
  g_list_free (l_neutral);
  g_list_free (l_bad);

  new_msg = gst_message_new_element (msg->src, new_structure);
  gst_message_unref (msg);

  GST_DEBUG_OBJECT (playbin, "new redirect message: %" GST_PTR_FORMAT, new_msg);
  return new_msg;
}

static void
gst_play_bin_handle_message (GstBin * bin, GstMessage * msg)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ELEMENT && msg->structure != NULL
      && gst_structure_has_name (msg->structure, "redirect")) {
    msg = gst_play_bin_handle_redirect_message (GST_PLAY_BIN (bin), msg);
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}

static GstStateChangeReturn
gst_play_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlayBin *play_bin;

  play_bin = GST_PLAY_BIN (element);


  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* this really is the easiest way to make the state change return
       * ASYNC until we added the sinks */
      if (!play_bin->fakesink) {
        play_bin->fakesink = gst_element_factory_make ("fakesink", "test");
        gst_bin_add (GST_BIN_CAST (play_bin), play_bin->fakesink);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* remember us being a live pipeline */
      play_bin->is_live = (ret == GST_STATE_CHANGE_NO_PREROLL);
      GST_DEBUG_OBJECT (play_bin, "is live: %d", play_bin->is_live);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* FIXME Release audio device when we implement that */
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* remove sinks we added */
      remove_sinks (play_bin);
      /* and there might be a fakesink we need to clean up now */
      if (play_bin->fakesink) {
        gst_element_set_state (play_bin->fakesink, GST_STATE_NULL);
        gst_bin_remove (GST_BIN_CAST (play_bin), play_bin->fakesink);
        play_bin->fakesink = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_play_bin_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_play_bin_debug, "playbin", 0, "play bin");

  return gst_element_register (plugin, "playbin", GST_RANK_NONE,
      GST_TYPE_PLAY_BIN);
}
