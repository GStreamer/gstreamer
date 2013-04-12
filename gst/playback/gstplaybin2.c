/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) <2011> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Playbin can handle both audio and video files and features
 * <itemizedlist>
 * <listitem>
 * automatic file type recognition and based on that automatic
 * selection and usage of the right audio/video/subtitle demuxers/decoders
 * </listitem>
 * <listitem>
 * visualisations for audio files
 * </listitem>
 * <listitem>
 * subtitle support for video files. Subtitles can be store in external
 * files.
 * </listitem>
 * <listitem>
 * stream selection between different video/audio/subtitles streams
 * </listitem>
 * <listitem>
 * meta info (tag) extraction
 * </listitem>
 * <listitem>
 * easy access to the last video sample
 * </listitem>
 * <listitem>
 * buffering when playing streams over a network
 * </listitem>
 * <listitem>
 * volume control with mute option
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
 * #GstPlayBin:audio-sink or #GstPlayBin:video-sink property, playbin will use the autoaudiosink
 * and autovideosink elements to find the first-best available output method.
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
 * from the negotiated caps on the sink pads of the sinks.
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
 * own, however. This can be done using the #GstVideoOverlay interface, which most
 * video sinks implement. See the documentation there for more details.
 * </refsect2>
 * <refsect2>
 * <title>Specifying which CD/DVD device to use</title>
 * The device to use for CDs/DVDs needs to be set on the source element
 * playbin creates before it is opened. The most generic way of doing this
 * is to connect to playbin's "source-setup" (or "notify::source") signal,
 * which will be emitted by playbin when it has created the source element
 * for a particular URI. In the signal callback you can check if the source
 * element has a "device" property and set it appropriately. In some cases
 * the device can also be set as part of the URI, but it depends on the
 * elements involved if this will work or not. For example, for DVD menu
 * playback, the following syntax might work (if the resindvd plugin is used):
 * dvd://[/path/to/device]
 * </refsect2>
 * <refsect2>
 * <title>Handling redirects</title>
 * <para>
 * Some elements may post 'redirect' messages on the bus to tell the
 * application to open another location. These are element messages containing
 * a structure named 'redirect' along with a 'new-location' field of string
 * type. The new location may be a relative or an absolute URI. Examples
 * for such redirects can be found in many quicktime movie trailers.
 * </para>
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
 * gst-launch -v playbin uri=dvd://
 * ]| This will play back the DVD in your disc drive (assuming
 * the drive is detected automatically by the plugin).
 * </refsect2>
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>

#include <gst/gst-i18n-plugin.h>
#include <gst/pbutils/pbutils.h>
#include <gst/audio/streamvolume.h>
#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>
#include <gst/video/colorbalance.h>
#include "gstplay-enum.h"
#include "gstplayback.h"
#include "gstplaysink.h"
#include "gstsubtitleoverlay.h"

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
typedef struct _GstSourceGroup GstSourceGroup;
typedef struct _GstSourceSelect GstSourceSelect;

typedef GstCaps *(*SourceSelectGetMediaCapsFunc) (void);

/* has the info for a selector and provides the link to the sink */
struct _GstSourceSelect
{
  const gchar *media_list[8];   /* the media types for the selector */
  SourceSelectGetMediaCapsFunc get_media_caps;  /* more complex caps for the selector */
  GstPlaySinkType type;         /* the sink pad type of the selector */

  GstElement *selector;         /* the selector */
  GPtrArray *channels;
  GstPad *srcpad;               /* the source pad of the selector */
  GstPad *sinkpad;              /* the sinkpad of the sink when the selector
                                 * is linked
                                 */
  gulong block_id;
};

#define GST_SOURCE_GROUP_GET_LOCK(group) (&((GstSourceGroup*)(group))->lock)
#define GST_SOURCE_GROUP_LOCK(group) (g_mutex_lock (GST_SOURCE_GROUP_GET_LOCK(group)))
#define GST_SOURCE_GROUP_UNLOCK(group) (g_mutex_unlock (GST_SOURCE_GROUP_GET_LOCK(group)))

enum
{
  PLAYBIN_STREAM_AUDIO = 0,
  PLAYBIN_STREAM_VIDEO,
  PLAYBIN_STREAM_TEXT,
  PLAYBIN_STREAM_LAST
};

/* a structure to hold the objects for decoding a uri and the subtitle uri
 */
struct _GstSourceGroup
{
  GstPlayBin *playbin;

  GMutex lock;

  gboolean valid;               /* the group has valid info to start playback */
  gboolean active;              /* the group is active */

  /* properties */
  gchar *uri;
  gchar *suburi;
  GValueArray *streaminfo;
  GstElement *source;

  GPtrArray *video_channels;    /* links to selector pads */
  GPtrArray *audio_channels;    /* links to selector pads */
  GPtrArray *text_channels;     /* links to selector pads */

  GstElement *audio_sink;       /* autoplugged audio and video sinks */
  GstElement *video_sink;

  /* uridecodebins for uri and subtitle uri */
  GstElement *uridecodebin;
  GstElement *suburidecodebin;
  gint pending;
  gboolean sub_pending;

  gulong pad_added_id;
  gulong pad_removed_id;
  gulong no_more_pads_id;
  gulong notify_source_id;
  gulong drained_id;
  gulong autoplug_factories_id;
  gulong autoplug_select_id;
  gulong autoplug_continue_id;

  gulong sub_pad_added_id;
  gulong sub_pad_removed_id;
  gulong sub_no_more_pads_id;
  gulong sub_autoplug_continue_id;

  gulong block_id;

  GMutex stream_changed_pending_lock;
  GList *stream_changed_pending;

  /* to prevent that suburidecodebin seek flushes disrupt playback */
  GMutex suburi_flushes_to_drop_lock;
  GSList *suburi_flushes_to_drop;

  /* selectors for different streams */
  GstSourceSelect selector[PLAYBIN_STREAM_LAST];
};

#define GST_PLAY_BIN_GET_LOCK(bin) (&((GstPlayBin*)(bin))->lock)
#define GST_PLAY_BIN_LOCK(bin) (g_rec_mutex_lock (GST_PLAY_BIN_GET_LOCK(bin)))
#define GST_PLAY_BIN_UNLOCK(bin) (g_rec_mutex_unlock (GST_PLAY_BIN_GET_LOCK(bin)))

/* lock to protect dynamic callbacks, like no-more-pads */
#define GST_PLAY_BIN_DYN_LOCK(bin)    g_mutex_lock (&(bin)->dyn_lock)
#define GST_PLAY_BIN_DYN_UNLOCK(bin)  g_mutex_unlock (&(bin)->dyn_lock)

/* lock for shutdown */
#define GST_PLAY_BIN_SHUTDOWN_LOCK(bin,label)           \
G_STMT_START {                                          \
  if (G_UNLIKELY (g_atomic_int_get (&bin->shutdown)))   \
    goto label;                                         \
  GST_PLAY_BIN_DYN_LOCK (bin);                          \
  if (G_UNLIKELY (g_atomic_int_get (&bin->shutdown))) { \
    GST_PLAY_BIN_DYN_UNLOCK (bin);                      \
    goto label;                                         \
  }                                                     \
} G_STMT_END

/* unlock for shutdown */
#define GST_PLAY_BIN_SHUTDOWN_UNLOCK(bin)         \
  GST_PLAY_BIN_DYN_UNLOCK (bin);                  \

/**
 * GstPlayBin:
 *
 * playbin element structure
 */
struct _GstPlayBin
{
  GstPipeline parent;

  GRecMutex lock;               /* to protect group switching */

  /* the groups, we use a double buffer to switch between current and next */
  GstSourceGroup groups[2];     /* array with group info */
  GstSourceGroup *curr_group;   /* pointer to the currently playing group */
  GstSourceGroup *next_group;   /* pointer to the next group */

  /* properties */
  guint64 connection_speed;     /* connection speed in bits/sec (0 = unknown) */
  gint current_video;           /* the currently selected stream */
  gint current_audio;           /* the currently selected stream */
  gint current_text;            /* the currently selected stream */

  guint64 buffer_duration;      /* When buffering, the max buffer duration (ns) */
  guint buffer_size;            /* When buffering, the max buffer size (bytes) */
  gboolean force_aspect_ratio;

  /* our play sink */
  GstPlaySink *playsink;

  /* the last activated source */
  GstElement *source;

  /* lock protecting dynamic adding/removing */
  GMutex dyn_lock;
  /* if we are shutting down or not */
  gint shutdown;

  GMutex elements_lock;
  guint32 elements_cookie;
  GList *elements;              /* factories we can use for selecting elements */

  gboolean have_selector;       /* set to FALSE when we fail to create an
                                 * input-selector, so that we only post a
                                 * warning once */

  gboolean video_pending_flush_finish;  /* whether we are pending to send a custom
                                         * custom-video-flush-finish event
                                         * on pad activation */
  gboolean audio_pending_flush_finish;  /* whether we are pending to send a custom
                                         * custom-audio-flush-finish event
                                         * on pad activation */
  gboolean text_pending_flush_finish;   /* whether we are pending to send a custom
                                         * custom-subtitle-flush-finish event
                                         * on pad activation */

  GstElement *audio_sink;       /* configured audio sink, or NULL      */
  GstElement *video_sink;       /* configured video sink, or NULL      */
  GstElement *text_sink;        /* configured text sink, or NULL       */

  struct
  {
    gboolean valid;
    GstFormat format;
    gint64 duration;
  } duration[5];                /* cached durations */

  guint64 ring_buffer_max_size; /* 0 means disabled */
};

struct _GstPlayBinClass
{
  GstPipelineClass parent_class;

  /* notify app that the current uri finished decoding and it is possible to
   * queue a new one for gapless playback */
  void (*about_to_finish) (GstPlayBin * playbin);

  /* notify app that number of audio/video/text streams changed */
  void (*video_changed) (GstPlayBin * playbin);
  void (*audio_changed) (GstPlayBin * playbin);
  void (*text_changed) (GstPlayBin * playbin);

  /* notify app that the tags of audio/video/text streams changed */
  void (*video_tags_changed) (GstPlayBin * playbin, gint stream);
  void (*audio_tags_changed) (GstPlayBin * playbin, gint stream);
  void (*text_tags_changed) (GstPlayBin * playbin, gint stream);

  /* get audio/video/text tags for a stream */
  GstTagList *(*get_video_tags) (GstPlayBin * playbin, gint stream);
  GstTagList *(*get_audio_tags) (GstPlayBin * playbin, gint stream);
  GstTagList *(*get_text_tags) (GstPlayBin * playbin, gint stream);

  /* get the last video sample and convert it to the given caps */
  GstSample *(*convert_sample) (GstPlayBin * playbin, GstCaps * caps);

  /* get audio/video/text pad for a stream */
  GstPad *(*get_video_pad) (GstPlayBin * playbin, gint stream);
  GstPad *(*get_audio_pad) (GstPlayBin * playbin, gint stream);
  GstPad *(*get_text_pad) (GstPlayBin * playbin, gint stream);
};

/* props */
#define DEFAULT_URI               NULL
#define DEFAULT_SUBURI            NULL
#define DEFAULT_SOURCE            NULL
#define DEFAULT_FLAGS             GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_TEXT | \
                                  GST_PLAY_FLAG_SOFT_VOLUME | GST_PLAY_FLAG_DEINTERLACE | \
                                  GST_PLAY_FLAG_SOFT_COLORBALANCE
#define DEFAULT_N_VIDEO           0
#define DEFAULT_CURRENT_VIDEO     -1
#define DEFAULT_N_AUDIO           0
#define DEFAULT_CURRENT_AUDIO     -1
#define DEFAULT_N_TEXT            0
#define DEFAULT_CURRENT_TEXT      -1
#define DEFAULT_SUBTITLE_ENCODING NULL
#define DEFAULT_AUDIO_SINK        NULL
#define DEFAULT_VIDEO_SINK        NULL
#define DEFAULT_VIS_PLUGIN        NULL
#define DEFAULT_TEXT_SINK         NULL
#define DEFAULT_VOLUME            1.0
#define DEFAULT_MUTE              FALSE
#define DEFAULT_FRAME             NULL
#define DEFAULT_FONT_DESC         NULL
#define DEFAULT_CONNECTION_SPEED  0
#define DEFAULT_BUFFER_DURATION   -1
#define DEFAULT_BUFFER_SIZE       -1
#define DEFAULT_RING_BUFFER_MAX_SIZE 0

enum
{
  PROP_0,
  PROP_URI,
  PROP_CURRENT_URI,
  PROP_SUBURI,
  PROP_CURRENT_SUBURI,
  PROP_SOURCE,
  PROP_FLAGS,
  PROP_N_VIDEO,
  PROP_CURRENT_VIDEO,
  PROP_N_AUDIO,
  PROP_CURRENT_AUDIO,
  PROP_N_TEXT,
  PROP_CURRENT_TEXT,
  PROP_SUBTITLE_ENCODING,
  PROP_AUDIO_SINK,
  PROP_VIDEO_SINK,
  PROP_VIS_PLUGIN,
  PROP_TEXT_SINK,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_SAMPLE,
  PROP_FONT_DESC,
  PROP_CONNECTION_SPEED,
  PROP_BUFFER_SIZE,
  PROP_BUFFER_DURATION,
  PROP_AV_OFFSET,
  PROP_RING_BUFFER_MAX_SIZE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_LAST
};

/* signals */
enum
{
  SIGNAL_ABOUT_TO_FINISH,
  SIGNAL_CONVERT_SAMPLE,
  SIGNAL_VIDEO_CHANGED,
  SIGNAL_AUDIO_CHANGED,
  SIGNAL_TEXT_CHANGED,
  SIGNAL_VIDEO_TAGS_CHANGED,
  SIGNAL_AUDIO_TAGS_CHANGED,
  SIGNAL_TEXT_TAGS_CHANGED,
  SIGNAL_GET_VIDEO_TAGS,
  SIGNAL_GET_AUDIO_TAGS,
  SIGNAL_GET_TEXT_TAGS,
  SIGNAL_GET_VIDEO_PAD,
  SIGNAL_GET_AUDIO_PAD,
  SIGNAL_GET_TEXT_PAD,
  SIGNAL_SOURCE_SETUP,
  LAST_SIGNAL
};

static void gst_play_bin_class_init (GstPlayBinClass * klass);
static void gst_play_bin_init (GstPlayBin * playbin);
static void gst_play_bin_finalize (GObject * object);

static void gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static GstStateChangeReturn gst_play_bin_change_state (GstElement * element,
    GstStateChange transition);

static void gst_play_bin_handle_message (GstBin * bin, GstMessage * message);
static gboolean gst_play_bin_query (GstElement * element, GstQuery * query);

static GstTagList *gst_play_bin_get_video_tags (GstPlayBin * playbin,
    gint stream);
static GstTagList *gst_play_bin_get_audio_tags (GstPlayBin * playbin,
    gint stream);
static GstTagList *gst_play_bin_get_text_tags (GstPlayBin * playbin,
    gint stream);

static GstSample *gst_play_bin_convert_sample (GstPlayBin * playbin,
    GstCaps * caps);

static GstPad *gst_play_bin_get_video_pad (GstPlayBin * playbin, gint stream);
static GstPad *gst_play_bin_get_audio_pad (GstPlayBin * playbin, gint stream);
static GstPad *gst_play_bin_get_text_pad (GstPlayBin * playbin, gint stream);

static gboolean setup_next_source (GstPlayBin * playbin, GstState target);

static void no_more_pads_cb (GstElement * decodebin, GstSourceGroup * group);
static void pad_removed_cb (GstElement * decodebin, GstPad * pad,
    GstSourceGroup * group);

static void gst_play_bin_suburidecodebin_block (GstSourceGroup * group,
    GstElement * suburidecodebin, gboolean block);
static void gst_play_bin_suburidecodebin_seek_to_start (GstSourceGroup * group);

static GstElementClass *parent_class;

static guint gst_play_bin_signals[LAST_SIGNAL] = { 0 };

#define REMOVE_SIGNAL(obj,id)            \
if (id) {                                \
  g_signal_handler_disconnect (obj, id); \
  id = 0;                                \
}

static void gst_play_bin_overlay_init (gpointer g_iface, gpointer g_iface_data);
static void gst_play_bin_navigation_init (gpointer g_iface,
    gpointer g_iface_data);
static void gst_play_bin_colorbalance_init (gpointer g_iface,
    gpointer g_iface_data);

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
    static const GInterfaceInfo svol_info = {
      NULL, NULL, NULL
    };
    static const GInterfaceInfo ov_info = {
      gst_play_bin_overlay_init,
      NULL, NULL
    };
    static const GInterfaceInfo nav_info = {
      gst_play_bin_navigation_init,
      NULL, NULL
    };
    static const GInterfaceInfo col_info = {
      gst_play_bin_colorbalance_init,
      NULL, NULL
    };

    gst_play_bin_type = g_type_register_static (GST_TYPE_PIPELINE,
        "GstPlayBin", &gst_play_bin_info, 0);

    g_type_add_interface_static (gst_play_bin_type, GST_TYPE_STREAM_VOLUME,
        &svol_info);
    g_type_add_interface_static (gst_play_bin_type, GST_TYPE_VIDEO_OVERLAY,
        &ov_info);
    g_type_add_interface_static (gst_play_bin_type, GST_TYPE_NAVIGATION,
        &nav_info);
    g_type_add_interface_static (gst_play_bin_type, GST_TYPE_COLOR_BALANCE,
        &col_info);
  }

  return gst_play_bin_type;
}

static void
gst_play_bin_class_init (GstPlayBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_play_bin_set_property;
  gobject_klass->get_property = gst_play_bin_get_property;

  gobject_klass->finalize = gst_play_bin_finalize;

  /**
   * GstPlayBin:uri
   *
   * Set the next URI that playbin will play. This property can be set from the
   * about-to-finish signal to queue the next media file.
   */
  g_object_class_install_property (gobject_klass, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   /**
   * GstPlayBin:current-uri
   *
   * The currently playing uri.
   */
  g_object_class_install_property (gobject_klass, PROP_CURRENT_URI,
      g_param_spec_string ("current-uri", "Current URI",
          "The currently playing URI", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin:suburi
   *
   * Set the next subtitle URI that playbin will play. This property can be
   * set from the about-to-finish signal to queue the next subtitle media file.
   */
  g_object_class_install_property (gobject_klass, PROP_SUBURI,
      g_param_spec_string ("suburi", ".sub-URI", "Optional URI of a subtitle",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin:current-suburi
   *
   * The currently playing subtitle uri.
   */
  g_object_class_install_property (gobject_klass, PROP_CURRENT_SUBURI,
      g_param_spec_string ("current-suburi", "Current .sub-URI",
          "The currently playing URI of a subtitle",
          NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_SOURCE,
      g_param_spec_object ("source", "Source", "Source element",
          GST_TYPE_ELEMENT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin:flags
   *
   * Control the behaviour of playbin.
   */
  g_object_class_install_property (gobject_klass, PROP_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Flags to control behaviour",
          GST_TYPE_PLAY_FLAGS, DEFAULT_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin:n-video
   *
   * Get the total number of available video streams.
   */
  g_object_class_install_property (gobject_klass, PROP_N_VIDEO,
      g_param_spec_int ("n-video", "Number Video",
          "Total number of video streams", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin:current-video
   *
   * Get or set the currently playing video stream. By default the first video
   * stream with data is played.
   */
  g_object_class_install_property (gobject_klass, PROP_CURRENT_VIDEO,
      g_param_spec_int ("current-video", "Current Video",
          "Currently playing video stream (-1 = auto)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin:n-audio
   *
   * Get the total number of available audio streams.
   */
  g_object_class_install_property (gobject_klass, PROP_N_AUDIO,
      g_param_spec_int ("n-audio", "Number Audio",
          "Total number of audio streams", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin:current-audio
   *
   * Get or set the currently playing audio stream. By default the first audio
   * stream with data is played.
   */
  g_object_class_install_property (gobject_klass, PROP_CURRENT_AUDIO,
      g_param_spec_int ("current-audio", "Current audio",
          "Currently playing audio stream (-1 = auto)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin:n-text
   *
   * Get the total number of available subtitle streams.
   */
  g_object_class_install_property (gobject_klass, PROP_N_TEXT,
      g_param_spec_int ("n-text", "Number Text",
          "Total number of text streams", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin:current-text:
   *
   * Get or set the currently playing subtitle stream. By default the first
   * subtitle stream with data is played.
   */
  g_object_class_install_property (gobject_klass, PROP_CURRENT_TEXT,
      g_param_spec_int ("current-text", "Current Text",
          "Currently playing text stream (-1 = auto)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_SUBTITLE_ENCODING,
      g_param_spec_string ("subtitle-encoding", "subtitle encoding",
          "Encoding to assume if input subtitles are not in UTF-8 encoding. "
          "If not set, the GST_SUBTITLE_ENCODING environment variable will "
          "be checked for an encoding to use. If that is not set either, "
          "ISO-8859-15 will be assumed.", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_AUDIO_SINK,
      g_param_spec_object ("audio-sink", "Audio Sink",
          "the audio output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_VIS_PLUGIN,
      g_param_spec_object ("vis-plugin", "Vis plugin",
          "the visualization element to use (NULL = default)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_TEXT_SINK,
      g_param_spec_object ("text-sink", "Text plugin",
          "the text output element to use (NULL = default subtitleoverlay)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin:volume:
   *
   * Get or set the current audio stream volume. 1.0 means 100%,
   * 0.0 means mute. This uses a linear volume scale.
   *
   */
  g_object_class_install_property (gobject_klass, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "The audio volume, 1.0=100%",
          0.0, VOLUME_MAX_DOUBLE, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute",
          "Mute the audio channel without changing the volume", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin:sample:
   * @playbin: a #GstPlayBin
   *
   * Get the currently rendered or prerolled sample in the video sink.
   * The #GstCaps in the sample will describe the format of the buffer.
   */
  g_object_class_install_property (gobject_klass, PROP_SAMPLE,
      g_param_spec_boxed ("sample", "Sample",
          "The last sample (NULL = no video available)",
          GST_TYPE_SAMPLE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_FONT_DESC,
      g_param_spec_string ("subtitle-font-desc",
          "Subtitle font description",
          "Pango font description of font "
          "to be used for subtitle rendering", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_CONNECTION_SPEED,
      g_param_spec_uint64 ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXUINT64 / 1000, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_BUFFER_SIZE,
      g_param_spec_int ("buffer-size", "Buffer size (bytes)",
          "Buffer size when buffering network streams",
          -1, G_MAXINT, DEFAULT_BUFFER_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_BUFFER_DURATION,
      g_param_spec_int64 ("buffer-duration", "Buffer duration (ns)",
          "Buffer duration when buffering network streams",
          -1, G_MAXINT64, DEFAULT_BUFFER_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin:av-offset:
   *
   * Control the synchronisation offset between the audio and video streams.
   * Positive values make the audio ahead of the video and negative values make
   * the audio go behind the video.
   *
   * Since: 0.10.30
   */
  g_object_class_install_property (gobject_klass, PROP_AV_OFFSET,
      g_param_spec_int64 ("av-offset", "AV Offset",
          "The synchronisation offset between audio and video in nanoseconds",
          G_MININT64, G_MAXINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin:ring-buffer-max-size
   *
   * The maximum size of the ring buffer in bytes. If set to 0, the ring
   * buffer is disabled. Default 0.
   *
   * Since: 0.10.31
   */
  g_object_class_install_property (gobject_klass, PROP_RING_BUFFER_MAX_SIZE,
      g_param_spec_uint64 ("ring-buffer-max-size",
          "Max. ring buffer size (bytes)",
          "Max. amount of data in the ring buffer (bytes, 0 = ring buffer disabled)",
          0, G_MAXUINT, DEFAULT_RING_BUFFER_MAX_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin::force-aspect-ratio:
   *
   * Requests the video sink to enforce the video display aspect ratio.
   *
   * Since: 0.10.37
   */
  g_object_class_install_property (gobject_klass, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force Aspect Ratio",
          "When enabled, scaling will respect original aspect ratio", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin::about-to-finish
   * @playbin: a #GstPlayBin
   *
   * This signal is emitted when the current uri is about to finish. You can
   * set the uri and suburi to make sure that playback continues.
   *
   * This signal is emitted from the context of a GStreamer streaming thread.
   */
  gst_play_bin_signals[SIGNAL_ABOUT_TO_FINISH] =
      g_signal_new ("about-to-finish", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBinClass, about_to_finish), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstPlayBin::video-changed
   * @playbin: a #GstPlayBin
   *
   * This signal is emitted whenever the number or order of the video
   * streams has changed. The application will most likely want to select
   * a new video stream.
   *
   * This signal is usually emitted from the context of a GStreamer streaming
   * thread. You can use gst_message_new_application() and
   * gst_element_post_message() to notify your application's main thread.
   */
  /* FIXME 0.11: turn video-changed signal into message? */
  gst_play_bin_signals[SIGNAL_VIDEO_CHANGED] =
      g_signal_new ("video-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBinClass, video_changed), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);
  /**
   * GstPlayBin::audio-changed
   * @playbin: a #GstPlayBin
   *
   * This signal is emitted whenever the number or order of the audio
   * streams has changed. The application will most likely want to select
   * a new audio stream.
   *
   * This signal may be emitted from the context of a GStreamer streaming thread.
   * You can use gst_message_new_application() and gst_element_post_message()
   * to notify your application's main thread.
   */
  /* FIXME 0.11: turn audio-changed signal into message? */
  gst_play_bin_signals[SIGNAL_AUDIO_CHANGED] =
      g_signal_new ("audio-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBinClass, audio_changed), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);
  /**
   * GstPlayBin::text-changed
   * @playbin: a #GstPlayBin
   *
   * This signal is emitted whenever the number or order of the text
   * streams has changed. The application will most likely want to select
   * a new text stream.
   *
   * This signal may be emitted from the context of a GStreamer streaming thread.
   * You can use gst_message_new_application() and gst_element_post_message()
   * to notify your application's main thread.
   */
  /* FIXME 0.11: turn text-changed signal into message? */
  gst_play_bin_signals[SIGNAL_TEXT_CHANGED] =
      g_signal_new ("text-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBinClass, text_changed), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstPlayBin::video-tags-changed
   * @playbin: a #GstPlayBin
   * @stream: stream index with changed tags
   *
   * This signal is emitted whenever the tags of a video stream have changed.
   * The application will most likely want to get the new tags.
   *
   * This signal may be emitted from the context of a GStreamer streaming thread.
   * You can use gst_message_new_application() and gst_element_post_message()
   * to notify your application's main thread.
   *
   * Since: 0.10.24
   */
  gst_play_bin_signals[SIGNAL_VIDEO_TAGS_CHANGED] =
      g_signal_new ("video-tags-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBinClass, video_tags_changed), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GstPlayBin::audio-tags-changed
   * @playbin: a #GstPlayBin
   * @stream: stream index with changed tags
   *
   * This signal is emitted whenever the tags of an audio stream have changed.
   * The application will most likely want to get the new tags.
   *
   * This signal may be emitted from the context of a GStreamer streaming thread.
   * You can use gst_message_new_application() and gst_element_post_message()
   * to notify your application's main thread.
   *
   * Since: 0.10.24
   */
  gst_play_bin_signals[SIGNAL_AUDIO_TAGS_CHANGED] =
      g_signal_new ("audio-tags-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBinClass, audio_tags_changed), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GstPlayBin::text-tags-changed
   * @playbin: a #GstPlayBin
   * @stream: stream index with changed tags
   *
   * This signal is emitted whenever the tags of a text stream have changed.
   * The application will most likely want to get the new tags.
   *
   * This signal may be emitted from the context of a GStreamer streaming thread.
   * You can use gst_message_new_application() and gst_element_post_message()
   * to notify your application's main thread.
   *
   * Since: 0.10.24
   */
  gst_play_bin_signals[SIGNAL_TEXT_TAGS_CHANGED] =
      g_signal_new ("text-tags-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBinClass, text_tags_changed), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GstPlayBin::source-setup:
   * @playbin: a #GstPlayBin
   * @source: source element
   *
   * This signal is emitted after the source element has been created, so
   * it can be configured by setting additional properties (e.g. set a
   * proxy server for an http source, or set the device and read speed for
   * an audio cd source). This is functionally equivalent to connecting to
   * the notify::source signal, but more convenient.
   *
   * This signal is usually emitted from the context of a GStreamer streaming
   * thread.
   *
   * Since: 0.10.33
   */
  gst_play_bin_signals[SIGNAL_SOURCE_SETUP] =
      g_signal_new ("source-setup", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  /**
   * GstPlayBin::get-video-tags
   * @playbin: a #GstPlayBin
   * @stream: a video stream number
   *
   * Action signal to retrieve the tags of a specific video stream number.
   * This information can be used to select a stream.
   *
   * Returns: a GstTagList with tags or NULL when the stream number does not
   * exist.
   */
  gst_play_bin_signals[SIGNAL_GET_VIDEO_TAGS] =
      g_signal_new ("get-video-tags", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBinClass, get_video_tags), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_TAG_LIST, 1, G_TYPE_INT);
  /**
   * GstPlayBin::get-audio-tags
   * @playbin: a #GstPlayBin
   * @stream: an audio stream number
   *
   * Action signal to retrieve the tags of a specific audio stream number.
   * This information can be used to select a stream.
   *
   * Returns: a GstTagList with tags or NULL when the stream number does not
   * exist.
   */
  gst_play_bin_signals[SIGNAL_GET_AUDIO_TAGS] =
      g_signal_new ("get-audio-tags", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBinClass, get_audio_tags), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_TAG_LIST, 1, G_TYPE_INT);
  /**
   * GstPlayBin::get-text-tags
   * @playbin: a #GstPlayBin
   * @stream: a text stream number
   *
   * Action signal to retrieve the tags of a specific text stream number.
   * This information can be used to select a stream.
   *
   * Returns: a GstTagList with tags or NULL when the stream number does not
   * exist.
   */
  gst_play_bin_signals[SIGNAL_GET_TEXT_TAGS] =
      g_signal_new ("get-text-tags", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBinClass, get_text_tags), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_TAG_LIST, 1, G_TYPE_INT);
  /**
   * GstPlayBin::convert-sample
   * @playbin: a #GstPlayBin
   * @caps: the target format of the frame
   *
   * Action signal to retrieve the currently playing video frame in the format
   * specified by @caps.
   * If @caps is %NULL, no conversion will be performed and this function is
   * equivalent to the #GstPlayBin::frame property.
   *
   * Returns: a #GstBuffer of the current video frame converted to #caps.
   * The caps on the buffer will describe the final layout of the buffer data.
   * %NULL is returned when no current buffer can be retrieved or when the
   * conversion failed.
   */
  gst_play_bin_signals[SIGNAL_CONVERT_SAMPLE] =
      g_signal_new ("convert-sample", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBinClass, convert_sample), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_SAMPLE, 1, GST_TYPE_CAPS);

  /**
   * GstPlayBin::get-video-pad
   * @playbin: a #GstPlayBin
   * @stream: a video stream number
   *
   * Action signal to retrieve the stream-selector sinkpad for a specific
   * video stream.
   * This pad can be used for notifications of caps changes, stream-specific
   * queries, etc.
   *
   * Returns: a #GstPad, or NULL when the stream number does not exist.
   */
  gst_play_bin_signals[SIGNAL_GET_VIDEO_PAD] =
      g_signal_new ("get-video-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBinClass, get_video_pad), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_PAD, 1, G_TYPE_INT);
  /**
   * GstPlayBin::get-audio-pad
   * @playbin: a #GstPlayBin
   * @stream: an audio stream number
   *
   * Action signal to retrieve the stream-selector sinkpad for a specific
   * audio stream.
   * This pad can be used for notifications of caps changes, stream-specific
   * queries, etc.
   *
   * Returns: a #GstPad, or NULL when the stream number does not exist.
   */
  gst_play_bin_signals[SIGNAL_GET_AUDIO_PAD] =
      g_signal_new ("get-audio-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBinClass, get_audio_pad), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_PAD, 1, G_TYPE_INT);
  /**
   * GstPlayBin::get-text-pad
   * @playbin: a #GstPlayBin
   * @stream: a text stream number
   *
   * Action signal to retrieve the stream-selector sinkpad for a specific
   * text stream.
   * This pad can be used for notifications of caps changes, stream-specific
   * queries, etc.
   *
   * Returns: a #GstPad, or NULL when the stream number does not exist.
   */
  gst_play_bin_signals[SIGNAL_GET_TEXT_PAD] =
      g_signal_new ("get-text-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBinClass, get_text_pad), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_PAD, 1, G_TYPE_INT);

  klass->get_video_tags = gst_play_bin_get_video_tags;
  klass->get_audio_tags = gst_play_bin_get_audio_tags;
  klass->get_text_tags = gst_play_bin_get_text_tags;

  klass->convert_sample = gst_play_bin_convert_sample;

  klass->get_video_pad = gst_play_bin_get_video_pad;
  klass->get_audio_pad = gst_play_bin_get_audio_pad;
  klass->get_text_pad = gst_play_bin_get_text_pad;

  gst_element_class_set_static_metadata (gstelement_klass,
      "Player Bin 2", "Generic/Bin/Player",
      "Autoplug and play media from an uri",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_bin_change_state);
  gstelement_klass->query = GST_DEBUG_FUNCPTR (gst_play_bin_query);

  gstbin_klass->handle_message =
      GST_DEBUG_FUNCPTR (gst_play_bin_handle_message);
}

static void
init_group (GstPlayBin * playbin, GstSourceGroup * group)
{
  /* store the array for the different channels */
  group->video_channels = g_ptr_array_new ();
  group->audio_channels = g_ptr_array_new ();
  group->text_channels = g_ptr_array_new ();
  g_mutex_init (&group->lock);
  /* init selectors. The selector is found by finding the first prefix that
   * matches the media. */
  group->playbin = playbin;
  /* If you add any items to these lists, check that media_list[] is defined
   * above to be large enough to hold MAX(items)+1, so as to accommodate a
   * NULL terminator (set when the memory is zeroed on allocation) */
  group->selector[PLAYBIN_STREAM_AUDIO].media_list[0] = "audio/";
  group->selector[PLAYBIN_STREAM_AUDIO].type = GST_PLAY_SINK_TYPE_AUDIO;
  group->selector[PLAYBIN_STREAM_AUDIO].channels = group->audio_channels;
  group->selector[PLAYBIN_STREAM_VIDEO].media_list[0] = "video/";
  group->selector[PLAYBIN_STREAM_VIDEO].media_list[1] = "image/";
  group->selector[PLAYBIN_STREAM_VIDEO].type = GST_PLAY_SINK_TYPE_VIDEO;
  group->selector[PLAYBIN_STREAM_VIDEO].channels = group->video_channels;
  group->selector[PLAYBIN_STREAM_TEXT].media_list[0] = "text/";
  group->selector[PLAYBIN_STREAM_TEXT].media_list[1] = "application/x-subtitle";
  group->selector[PLAYBIN_STREAM_TEXT].media_list[2] = "application/x-ssa";
  group->selector[PLAYBIN_STREAM_TEXT].media_list[3] = "application/x-ass";
  group->selector[PLAYBIN_STREAM_TEXT].media_list[4] = "subpicture/x-dvd";
  group->selector[PLAYBIN_STREAM_TEXT].media_list[5] = "subpicture/";
  group->selector[PLAYBIN_STREAM_TEXT].media_list[6] = "subtitle/";
  group->selector[PLAYBIN_STREAM_TEXT].get_media_caps =
      gst_subtitle_overlay_create_factory_caps;
  group->selector[PLAYBIN_STREAM_TEXT].type = GST_PLAY_SINK_TYPE_TEXT;
  group->selector[PLAYBIN_STREAM_TEXT].channels = group->text_channels;
}

static void
free_group (GstPlayBin * playbin, GstSourceGroup * group)
{
  g_free (group->uri);
  g_free (group->suburi);
  g_ptr_array_free (group->video_channels, TRUE);
  g_ptr_array_free (group->audio_channels, TRUE);
  g_ptr_array_free (group->text_channels, TRUE);

  g_mutex_clear (&group->lock);
  if (group->audio_sink) {
    if (group->audio_sink != playbin->audio_sink)
      gst_element_set_state (group->audio_sink, GST_STATE_NULL);
    gst_object_unref (group->audio_sink);
  }
  group->audio_sink = NULL;
  if (group->video_sink) {
    if (group->video_sink != playbin->video_sink)
      gst_element_set_state (group->video_sink, GST_STATE_NULL);
    gst_object_unref (group->video_sink);
  }
  group->video_sink = NULL;

  g_list_free (group->stream_changed_pending);
  group->stream_changed_pending = NULL;

  if (group->stream_changed_pending_lock.p)
    g_mutex_clear (&group->stream_changed_pending_lock);
  group->stream_changed_pending_lock.p = NULL;

  g_slist_free (group->suburi_flushes_to_drop);
  group->suburi_flushes_to_drop = NULL;

  if (group->suburi_flushes_to_drop_lock.p)
    g_mutex_clear (&group->suburi_flushes_to_drop_lock);
  group->suburi_flushes_to_drop_lock.p = NULL;
}

static void
notify_volume_cb (GObject * selector, GParamSpec * pspec, GstPlayBin * playbin)
{
  g_object_notify (G_OBJECT (playbin), "volume");
}

static void
notify_mute_cb (GObject * selector, GParamSpec * pspec, GstPlayBin * playbin)
{
  g_object_notify (G_OBJECT (playbin), "mute");
}

static void
colorbalance_value_changed_cb (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value, GstPlayBin * playbin)
{
  gst_color_balance_value_changed (GST_COLOR_BALANCE (playbin), channel, value);
}

static gint
compare_factories_func (gconstpointer p1, gconstpointer p2)
{
  GstPluginFeature *f1, *f2;
  gint diff;
  gboolean is_sink1, is_sink2;
  gboolean is_parser1, is_parser2;

  f1 = (GstPluginFeature *) p1;
  f2 = (GstPluginFeature *) p2;

  is_sink1 = gst_element_factory_list_is_type (GST_ELEMENT_FACTORY_CAST (f1),
      GST_ELEMENT_FACTORY_TYPE_SINK);
  is_sink2 = gst_element_factory_list_is_type (GST_ELEMENT_FACTORY_CAST (f2),
      GST_ELEMENT_FACTORY_TYPE_SINK);
  is_parser1 = gst_element_factory_list_is_type (GST_ELEMENT_FACTORY_CAST (f1),
      GST_ELEMENT_FACTORY_TYPE_PARSER);
  is_parser2 = gst_element_factory_list_is_type (GST_ELEMENT_FACTORY_CAST (f2),
      GST_ELEMENT_FACTORY_TYPE_PARSER);

  /* First we want all sinks as we prefer a sink if it directly
   * supports the current caps */
  if (is_sink1 && !is_sink2)
    return -1;
  else if (!is_sink1 && is_sink2)
    return 1;

  /* Then we want all parsers as we always want to plug parsers
   * before decoders */
  if (is_parser1 && !is_parser2)
    return -1;
  else if (!is_parser1 && is_parser2)
    return 1;

  /* And if it's a both a parser or sink we first sort by rank
   * and then by factory name */
  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;

  diff = strcmp (GST_OBJECT_NAME (f2), GST_OBJECT_NAME (f1));

  return diff;
}

/* Must be called with elements lock! */
static void
gst_play_bin_update_elements_list (GstPlayBin * playbin)
{
  GList *res, *tmp;
  guint cookie;

  cookie = gst_registry_get_feature_list_cookie (gst_registry_get ());
  if (!playbin->elements || playbin->elements_cookie != cookie) {
    if (playbin->elements)
      gst_plugin_feature_list_free (playbin->elements);
    res =
        gst_element_factory_list_get_elements
        (GST_ELEMENT_FACTORY_TYPE_DECODABLE, GST_RANK_MARGINAL);
    tmp =
        gst_element_factory_list_get_elements
        (GST_ELEMENT_FACTORY_TYPE_AUDIOVIDEO_SINKS, GST_RANK_MARGINAL);
    playbin->elements = g_list_concat (res, tmp);
    playbin->elements = g_list_sort (playbin->elements, compare_factories_func);
    playbin->elements_cookie = cookie;
  }
}

static void
gst_play_bin_init (GstPlayBin * playbin)
{
  g_rec_mutex_init (&playbin->lock);
  g_mutex_init (&playbin->dyn_lock);

  /* assume we can create a selector */
  playbin->have_selector = TRUE;

  /* init groups */
  playbin->curr_group = &playbin->groups[0];
  playbin->next_group = &playbin->groups[1];
  init_group (playbin, &playbin->groups[0]);
  init_group (playbin, &playbin->groups[1]);

  /* first filter out the interesting element factories */
  g_mutex_init (&playbin->elements_lock);

  /* add sink */
  playbin->playsink =
      g_object_new (GST_TYPE_PLAY_SINK, "name", "playsink", "send-event-mode",
      1, NULL);
  gst_bin_add (GST_BIN_CAST (playbin), GST_ELEMENT_CAST (playbin->playsink));
  gst_play_sink_set_flags (playbin->playsink, DEFAULT_FLAGS);
  /* Connect to notify::volume and notify::mute signals for proxying */
  g_signal_connect (playbin->playsink, "notify::volume",
      G_CALLBACK (notify_volume_cb), playbin);
  g_signal_connect (playbin->playsink, "notify::mute",
      G_CALLBACK (notify_mute_cb), playbin);
  g_signal_connect (playbin->playsink, "value-changed",
      G_CALLBACK (colorbalance_value_changed_cb), playbin);

  playbin->current_video = DEFAULT_CURRENT_VIDEO;
  playbin->current_audio = DEFAULT_CURRENT_AUDIO;
  playbin->current_text = DEFAULT_CURRENT_TEXT;

  playbin->buffer_duration = DEFAULT_BUFFER_DURATION;
  playbin->buffer_size = DEFAULT_BUFFER_SIZE;
  playbin->ring_buffer_max_size = DEFAULT_RING_BUFFER_MAX_SIZE;

  playbin->force_aspect_ratio = TRUE;
}

static void
gst_play_bin_finalize (GObject * object)
{
  GstPlayBin *playbin;

  playbin = GST_PLAY_BIN (object);

  free_group (playbin, &playbin->groups[0]);
  free_group (playbin, &playbin->groups[1]);

  if (playbin->source)
    gst_object_unref (playbin->source);
  if (playbin->video_sink) {
    gst_element_set_state (playbin->video_sink, GST_STATE_NULL);
    gst_object_unref (playbin->video_sink);
  }
  if (playbin->audio_sink) {
    gst_element_set_state (playbin->audio_sink, GST_STATE_NULL);
    gst_object_unref (playbin->audio_sink);
  }
  if (playbin->text_sink) {
    gst_element_set_state (playbin->text_sink, GST_STATE_NULL);
    gst_object_unref (playbin->text_sink);
  }

  if (playbin->elements)
    gst_plugin_feature_list_free (playbin->elements);

  g_rec_mutex_clear (&playbin->lock);
  g_mutex_clear (&playbin->dyn_lock);
  g_mutex_clear (&playbin->elements_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_playbin_uri_is_valid (GstPlayBin * playbin, const gchar * uri)
{
  const gchar *c;

  GST_LOG_OBJECT (playbin, "checking uri '%s'", uri);

  /* this just checks the protocol */
  if (!gst_uri_is_valid (uri))
    return FALSE;

  for (c = uri; *c != '\0'; ++c) {
    if (!g_ascii_isprint (*c))
      goto invalid;
    if (*c == ' ')
      goto invalid;
  }

  return TRUE;

invalid:
  {
    GST_WARNING_OBJECT (playbin, "uri '%s' not valid, character #%u",
        uri, (guint) ((guintptr) c - (guintptr) uri));
    return FALSE;
  }
}

static void
gst_play_bin_set_uri (GstPlayBin * playbin, const gchar * uri)
{
  GstSourceGroup *group;

  if (uri == NULL) {
    g_warning ("cannot set NULL uri");
    return;
  }

  if (!gst_playbin_uri_is_valid (playbin, uri)) {
    if (g_str_has_prefix (uri, "file:")) {
      GST_WARNING_OBJECT (playbin, "not entirely correct file URI '%s' - make "
          "sure to escape spaces and non-ASCII characters properly and specify "
          "an absolute path. Use gst_filename_to_uri() to convert filenames "
          "to URIs", uri);
    } else {
      /* GST_ERROR_OBJECT (playbin, "malformed URI '%s'", uri); */
    }
  }

  GST_PLAY_BIN_LOCK (playbin);
  group = playbin->next_group;

  GST_SOURCE_GROUP_LOCK (group);
  /* store the uri in the next group we will play */
  g_free (group->uri);
  group->uri = g_strdup (uri);
  group->valid = TRUE;
  GST_SOURCE_GROUP_UNLOCK (group);

  GST_DEBUG ("set new uri to %s", uri);
  GST_PLAY_BIN_UNLOCK (playbin);
}

static void
gst_play_bin_set_suburi (GstPlayBin * playbin, const gchar * suburi)
{
  GstSourceGroup *group;

  GST_PLAY_BIN_LOCK (playbin);
  group = playbin->next_group;

  GST_SOURCE_GROUP_LOCK (group);
  g_free (group->suburi);
  group->suburi = g_strdup (suburi);
  GST_SOURCE_GROUP_UNLOCK (group);

  GST_DEBUG ("setting new .sub uri to %s", suburi);

  GST_PLAY_BIN_UNLOCK (playbin);
}

static void
gst_play_bin_set_flags (GstPlayBin * playbin, GstPlayFlags flags)
{
  gst_play_sink_set_flags (playbin->playsink, flags);
  gst_play_sink_reconfigure (playbin->playsink);
}

static GstPlayFlags
gst_play_bin_get_flags (GstPlayBin * playbin)
{
  GstPlayFlags flags;

  flags = gst_play_sink_get_flags (playbin->playsink);

  return flags;
}

/* get the currently playing group or if nothing is playing, the next
 * group. Must be called with the PLAY_BIN_LOCK. */
static GstSourceGroup *
get_group (GstPlayBin * playbin)
{
  GstSourceGroup *result;

  if (!(result = playbin->curr_group))
    result = playbin->next_group;

  return result;
}

static GstPad *
gst_play_bin_get_video_pad (GstPlayBin * playbin, gint stream)
{
  GstPad *sinkpad = NULL;
  GstSourceGroup *group;

  GST_PLAY_BIN_LOCK (playbin);
  group = get_group (playbin);
  if (stream < group->video_channels->len) {
    sinkpad = g_ptr_array_index (group->video_channels, stream);
    gst_object_ref (sinkpad);
  }
  GST_PLAY_BIN_UNLOCK (playbin);

  return sinkpad;
}

static GstPad *
gst_play_bin_get_audio_pad (GstPlayBin * playbin, gint stream)
{
  GstPad *sinkpad = NULL;
  GstSourceGroup *group;

  GST_PLAY_BIN_LOCK (playbin);
  group = get_group (playbin);
  if (stream < group->audio_channels->len) {
    sinkpad = g_ptr_array_index (group->audio_channels, stream);
    gst_object_ref (sinkpad);
  }
  GST_PLAY_BIN_UNLOCK (playbin);

  return sinkpad;
}

static GstPad *
gst_play_bin_get_text_pad (GstPlayBin * playbin, gint stream)
{
  GstPad *sinkpad = NULL;
  GstSourceGroup *group;

  GST_PLAY_BIN_LOCK (playbin);
  group = get_group (playbin);
  if (stream < group->text_channels->len) {
    sinkpad = g_ptr_array_index (group->text_channels, stream);
    gst_object_ref (sinkpad);
  }
  GST_PLAY_BIN_UNLOCK (playbin);

  return sinkpad;
}


static GstTagList *
get_tags (GstPlayBin * playbin, GPtrArray * channels, gint stream)
{
  GstTagList *result;
  GstPad *sinkpad;

  if (!channels || stream >= channels->len)
    return NULL;

  sinkpad = g_ptr_array_index (channels, stream);
  g_object_get (sinkpad, "tags", &result, NULL);

  return result;
}

static GstTagList *
gst_play_bin_get_video_tags (GstPlayBin * playbin, gint stream)
{
  GstTagList *result;
  GstSourceGroup *group;

  GST_PLAY_BIN_LOCK (playbin);
  group = get_group (playbin);
  result = get_tags (playbin, group->video_channels, stream);
  GST_PLAY_BIN_UNLOCK (playbin);

  return result;
}

static GstTagList *
gst_play_bin_get_audio_tags (GstPlayBin * playbin, gint stream)
{
  GstTagList *result;
  GstSourceGroup *group;

  GST_PLAY_BIN_LOCK (playbin);
  group = get_group (playbin);
  result = get_tags (playbin, group->audio_channels, stream);
  GST_PLAY_BIN_UNLOCK (playbin);

  return result;
}

static GstTagList *
gst_play_bin_get_text_tags (GstPlayBin * playbin, gint stream)
{
  GstTagList *result;
  GstSourceGroup *group;

  GST_PLAY_BIN_LOCK (playbin);
  group = get_group (playbin);
  result = get_tags (playbin, group->text_channels, stream);
  GST_PLAY_BIN_UNLOCK (playbin);

  return result;
}

static GstSample *
gst_play_bin_convert_sample (GstPlayBin * playbin, GstCaps * caps)
{
  return gst_play_sink_convert_sample (playbin->playsink, caps);
}

/* Returns current stream number, or -1 if none has been selected yet */
static int
get_current_stream_number (GstPlayBin * playbin, GPtrArray * channels)
{
  /* Internal API cleanup would make this easier... */
  int i;
  GstPad *pad, *current;
  GstObject *selector = NULL;
  int ret = -1;

  for (i = 0; i < channels->len; i++) {
    pad = g_ptr_array_index (channels, i);
    if ((selector = gst_pad_get_parent (pad))) {
      g_object_get (selector, "active-pad", &current, NULL);
      gst_object_unref (selector);

      if (pad == current) {
        gst_object_unref (current);
        ret = i;
        break;
      }

      if (current)
        gst_object_unref (current);
    }
  }

  return ret;
}

static gboolean
gst_play_bin_send_custom_event (GstObject * selector, const gchar * event_name)
{
  GstPad *src;
  GstPad *peer;
  GstStructure *s;
  GstEvent *event;
  gboolean ret = FALSE;

  src = gst_element_get_static_pad (GST_ELEMENT_CAST (selector), "src");
  peer = gst_pad_get_peer (src);
  if (peer) {
    s = gst_structure_new_empty (event_name);
    event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB, s);
    gst_pad_send_event (peer, event);
    gst_object_unref (peer);
    ret = TRUE;
  }
  gst_object_unref (src);
  return ret;
}

static gboolean
gst_play_bin_set_current_video_stream (GstPlayBin * playbin, gint stream)
{
  GstSourceGroup *group;
  GPtrArray *channels;
  GstPad *sinkpad;

  GST_PLAY_BIN_LOCK (playbin);

  GST_DEBUG_OBJECT (playbin, "Changing current video stream %d -> %d",
      playbin->current_video, stream);

  group = get_group (playbin);
  if (!(channels = group->video_channels))
    goto no_channels;

  if (stream == -1 || channels->len <= stream) {
    sinkpad = NULL;
  } else {
    /* take channel from selected stream */
    sinkpad = g_ptr_array_index (channels, stream);
  }

  if (sinkpad)
    gst_object_ref (sinkpad);
  GST_PLAY_BIN_UNLOCK (playbin);

  if (sinkpad) {
    GstObject *selector;

    if ((selector = gst_pad_get_parent (sinkpad))) {
      GstPad *old_sinkpad;

      g_object_get (selector, "active-pad", &old_sinkpad, NULL);

      if (old_sinkpad != sinkpad) {
        if (gst_play_bin_send_custom_event (selector,
                "playsink-custom-video-flush"))
          playbin->video_pending_flush_finish = TRUE;

        /* activate the selected pad */
        g_object_set (selector, "active-pad", sinkpad, NULL);
      }

      gst_object_unref (selector);
    }
    gst_object_unref (sinkpad);
  }
  return TRUE;

no_channels:
  {
    GST_PLAY_BIN_UNLOCK (playbin);
    GST_DEBUG_OBJECT (playbin, "can't switch video, we have no channels");
    return FALSE;
  }
}

static gboolean
gst_play_bin_set_current_audio_stream (GstPlayBin * playbin, gint stream)
{
  GstSourceGroup *group;
  GPtrArray *channels;
  GstPad *sinkpad;

  GST_PLAY_BIN_LOCK (playbin);

  GST_DEBUG_OBJECT (playbin, "Changing current audio stream %d -> %d",
      playbin->current_audio, stream);

  group = get_group (playbin);
  if (!(channels = group->audio_channels))
    goto no_channels;

  if (stream == -1 || channels->len <= stream) {
    sinkpad = NULL;
  } else {
    /* take channel from selected stream */
    sinkpad = g_ptr_array_index (channels, stream);
  }

  if (sinkpad)
    gst_object_ref (sinkpad);
  GST_PLAY_BIN_UNLOCK (playbin);

  if (sinkpad) {
    GstObject *selector;

    if ((selector = gst_pad_get_parent (sinkpad))) {
      GstPad *old_sinkpad;

      g_object_get (selector, "active-pad", &old_sinkpad, NULL);

      if (old_sinkpad != sinkpad) {
        if (gst_play_bin_send_custom_event (selector,
                "playsink-custom-audio-flush"))
          playbin->audio_pending_flush_finish = TRUE;

        /* activate the selected pad */
        g_object_set (selector, "active-pad", sinkpad, NULL);
      }
      gst_object_unref (selector);
    }
    gst_object_unref (sinkpad);
  }
  return TRUE;

no_channels:
  {
    GST_PLAY_BIN_UNLOCK (playbin);
    GST_DEBUG_OBJECT (playbin, "can't switch audio, we have no channels");
    return FALSE;
  }
}

static void
gst_play_bin_suburidecodebin_seek_to_start (GstSourceGroup * group)
{
  GstElement *suburidecodebin = group->suburidecodebin;
  GstIterator *it = gst_element_iterate_src_pads (suburidecodebin);
  GstPad *sinkpad;
  GValue item = { 0, };

  if (it && gst_iterator_next (it, &item) == GST_ITERATOR_OK
      && ((sinkpad = g_value_get_object (&item)) != NULL)) {
    GstEvent *event;
    guint32 seqnum;

    event =
        gst_event_new_seek (1.0, GST_FORMAT_BYTES, GST_SEEK_FLAG_FLUSH,
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, -1);
    seqnum = gst_event_get_seqnum (event);

    /* store the seqnum to drop flushes from this seek later */
    g_mutex_lock (&group->suburi_flushes_to_drop_lock);
    group->suburi_flushes_to_drop =
        g_slist_append (group->suburi_flushes_to_drop,
        GUINT_TO_POINTER (seqnum));
    g_mutex_unlock (&group->suburi_flushes_to_drop_lock);

    if (!gst_pad_send_event (sinkpad, event)) {
      event =
          gst_event_new_seek (1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
          GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, -1);
      gst_event_set_seqnum (event, seqnum);
      if (!gst_pad_send_event (sinkpad, event)) {
        GST_DEBUG_OBJECT (suburidecodebin, "Seeking to the beginning failed!");

        g_mutex_lock (&group->suburi_flushes_to_drop_lock);
        group->suburi_flushes_to_drop =
            g_slist_remove (group->suburi_flushes_to_drop,
            GUINT_TO_POINTER (seqnum));
        g_mutex_unlock (&group->suburi_flushes_to_drop_lock);
      }
    }

    g_value_unset (&item);
  }

  if (it)
    gst_iterator_free (it);
}

static void
gst_play_bin_suburidecodebin_block (GstSourceGroup * group,
    GstElement * suburidecodebin, gboolean block)
{
  GstIterator *it = gst_element_iterate_src_pads (suburidecodebin);
  gboolean done = FALSE;
  GValue item = { 0, };

  GST_DEBUG_OBJECT (suburidecodebin, "Blocking suburidecodebin: %d", block);

  if (!it)
    return;
  while (!done) {
    GstPad *sinkpad;

    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
        sinkpad = g_value_get_object (&item);
        if (block) {
          group->block_id =
              gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
              NULL, NULL, NULL);
        } else if (group->block_id) {
          gst_pad_remove_probe (sinkpad, group->block_id);
          group->block_id = 0;
        }
        g_value_reset (&item);
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (it);
}

static gboolean
gst_play_bin_set_current_text_stream (GstPlayBin * playbin, gint stream)
{
  GstSourceGroup *group;
  GPtrArray *channels;
  GstPad *sinkpad;

  GST_PLAY_BIN_LOCK (playbin);

  GST_DEBUG_OBJECT (playbin, "Changing current text stream %d -> %d",
      playbin->current_text, stream);

  group = get_group (playbin);
  if (!(channels = group->text_channels))
    goto no_channels;

  if (stream == -1 || channels->len <= stream) {
    sinkpad = NULL;
  } else {
    /* take channel from selected stream */
    sinkpad = g_ptr_array_index (channels, stream);
  }

  if (sinkpad)
    gst_object_ref (sinkpad);
  GST_PLAY_BIN_UNLOCK (playbin);

  if (sinkpad) {
    GstObject *selector;

    if ((selector = gst_pad_get_parent (sinkpad))) {
      GstPad *old_sinkpad;

      g_object_get (selector, "active-pad", &old_sinkpad, NULL);

      if (old_sinkpad != sinkpad) {
        gboolean need_unblock, need_block, need_seek;
        GstPad *peer = NULL, *oldpeer = NULL;
        GstElement *parent_element = NULL, *old_parent_element = NULL;

        /* Now check if we need to seek the suburidecodebin to the beginning
         * or if we need to block all suburidecodebin sinkpads or if we need
         * to unblock all suburidecodebin sinkpads
         */
        if (sinkpad)
          peer = gst_pad_get_peer (sinkpad);
        if (old_sinkpad)
          oldpeer = gst_pad_get_peer (old_sinkpad);

        if (peer)
          parent_element = gst_pad_get_parent_element (peer);
        if (oldpeer)
          old_parent_element = gst_pad_get_parent_element (oldpeer);

        need_block = (old_parent_element == group->suburidecodebin
            && parent_element != old_parent_element);
        need_unblock = (parent_element == group->suburidecodebin
            && parent_element != old_parent_element);
        need_seek = (parent_element == group->suburidecodebin);

        if (peer)
          gst_object_unref (peer);
        if (oldpeer)
          gst_object_unref (oldpeer);
        if (parent_element)
          gst_object_unref (parent_element);
        if (old_parent_element)
          gst_object_unref (old_parent_element);

        /* Block all suburidecodebin sinkpads */
        if (need_block)
          gst_play_bin_suburidecodebin_block (group, group->suburidecodebin,
              TRUE);

        if (gst_play_bin_send_custom_event (selector,
                "playsink-custom-subtitle-flush"))
          playbin->text_pending_flush_finish = TRUE;

        /* activate the selected pad */
        g_object_set (selector, "active-pad", sinkpad, NULL);

        /* Unblock pads if necessary */
        if (need_unblock)
          gst_play_bin_suburidecodebin_block (group, group->suburidecodebin,
              FALSE);

        /* seek to the beginning */
        if (need_seek)
          gst_play_bin_suburidecodebin_seek_to_start (group);
      }
      gst_object_unref (selector);

      if (old_sinkpad)
        gst_object_unref (old_sinkpad);
    }
    gst_object_unref (sinkpad);
  }
  return TRUE;

no_channels:
  {
    GST_PLAY_BIN_UNLOCK (playbin);
    return FALSE;
  }
}

static void
gst_play_bin_set_sink (GstPlayBin * playbin, GstElement ** elem,
    const gchar * dbg, GstElement * sink)
{
  GST_INFO_OBJECT (playbin, "Setting %s sink to %" GST_PTR_FORMAT, dbg, sink);

  GST_PLAY_BIN_LOCK (playbin);
  if (*elem != sink) {
    GstElement *old;

    old = *elem;
    if (sink)
      gst_object_ref_sink (sink);

    *elem = sink;
    if (old)
      gst_object_unref (old);
  }
  GST_LOG_OBJECT (playbin, "%s sink now %" GST_PTR_FORMAT, dbg, *elem);
  GST_PLAY_BIN_UNLOCK (playbin);
}

static void
gst_play_bin_set_encoding (GstPlayBin * playbin, const gchar * encoding)
{
  GstElement *elem;

  GST_PLAY_BIN_LOCK (playbin);

  /* set subtitles on all current and next decodebins. */
  if ((elem = playbin->groups[0].uridecodebin))
    g_object_set (G_OBJECT (elem), "subtitle-encoding", encoding, NULL);
  if ((elem = playbin->groups[0].suburidecodebin))
    g_object_set (G_OBJECT (elem), "subtitle-encoding", encoding, NULL);
  if ((elem = playbin->groups[1].uridecodebin))
    g_object_set (G_OBJECT (elem), "subtitle-encoding", encoding, NULL);
  if ((elem = playbin->groups[1].suburidecodebin))
    g_object_set (G_OBJECT (elem), "subtitle-encoding", encoding, NULL);

  gst_play_sink_set_subtitle_encoding (playbin->playsink, encoding);
  GST_PLAY_BIN_UNLOCK (playbin);
}

static void
gst_play_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBin *playbin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case PROP_URI:
      gst_play_bin_set_uri (playbin, g_value_get_string (value));
      break;
    case PROP_SUBURI:
      gst_play_bin_set_suburi (playbin, g_value_get_string (value));
      break;
    case PROP_FLAGS:
      gst_play_bin_set_flags (playbin, g_value_get_flags (value));
      break;
    case PROP_CURRENT_VIDEO:
      gst_play_bin_set_current_video_stream (playbin, g_value_get_int (value));
      break;
    case PROP_CURRENT_AUDIO:
      gst_play_bin_set_current_audio_stream (playbin, g_value_get_int (value));
      break;
    case PROP_CURRENT_TEXT:
      gst_play_bin_set_current_text_stream (playbin, g_value_get_int (value));
      break;
    case PROP_SUBTITLE_ENCODING:
      gst_play_bin_set_encoding (playbin, g_value_get_string (value));
      break;
    case PROP_VIDEO_SINK:
      gst_play_bin_set_sink (playbin, &playbin->video_sink, "video",
          g_value_get_object (value));
      break;
    case PROP_AUDIO_SINK:
      gst_play_bin_set_sink (playbin, &playbin->audio_sink, "audio",
          g_value_get_object (value));
      break;
    case PROP_VIS_PLUGIN:
      gst_play_sink_set_vis_plugin (playbin->playsink,
          g_value_get_object (value));
      break;
    case PROP_TEXT_SINK:
      gst_play_bin_set_sink (playbin, &playbin->text_sink, "text",
          g_value_get_object (value));
      break;
    case PROP_VOLUME:
      gst_play_sink_set_volume (playbin->playsink, g_value_get_double (value));
      break;
    case PROP_MUTE:
      gst_play_sink_set_mute (playbin->playsink, g_value_get_boolean (value));
      break;
    case PROP_FONT_DESC:
      gst_play_sink_set_font_desc (playbin->playsink,
          g_value_get_string (value));
      break;
    case PROP_CONNECTION_SPEED:
      GST_PLAY_BIN_LOCK (playbin);
      playbin->connection_speed = g_value_get_uint64 (value) * 1000;
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    case PROP_BUFFER_SIZE:
      playbin->buffer_size = g_value_get_int (value);
      break;
    case PROP_BUFFER_DURATION:
      playbin->buffer_duration = g_value_get_int64 (value);
      break;
    case PROP_AV_OFFSET:
      gst_play_sink_set_av_offset (playbin->playsink,
          g_value_get_int64 (value));
      break;
    case PROP_RING_BUFFER_MAX_SIZE:
      playbin->ring_buffer_max_size = g_value_get_uint64 (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_object_set (playbin->playsink, "force-aspect-ratio",
          g_value_get_boolean (value), NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElement *
gst_play_bin_get_current_sink (GstPlayBin * playbin, GstElement ** elem,
    const gchar * dbg, GstPlaySinkType type)
{
  GstElement *sink = gst_play_sink_get_sink (playbin->playsink, type);

  GST_LOG_OBJECT (playbin, "play_sink_get_sink() returned %s sink %"
      GST_PTR_FORMAT ", the originally set %s sink is %" GST_PTR_FORMAT,
      dbg, sink, dbg, *elem);

  if (sink == NULL) {
    GST_PLAY_BIN_LOCK (playbin);
    if ((sink = *elem))
      gst_object_ref (sink);
    GST_PLAY_BIN_UNLOCK (playbin);
  }

  return sink;
}

static void
gst_play_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBin *playbin = GST_PLAY_BIN (object);

  switch (prop_id) {
    case PROP_URI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN_LOCK (playbin);
      group = playbin->next_group;
      g_value_set_string (value, group->uri);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
      break;
    }
    case PROP_CURRENT_URI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN_LOCK (playbin);
      group = get_group (playbin);
      g_value_set_string (value, group->uri);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    }
    case PROP_SUBURI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN_LOCK (playbin);
      group = playbin->next_group;
      g_value_set_string (value, group->suburi);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    }
    case PROP_CURRENT_SUBURI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN_LOCK (playbin);
      group = get_group (playbin);
      g_value_set_string (value, group->suburi);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    }
    case PROP_SOURCE:
    {
      GST_OBJECT_LOCK (playbin);
      g_value_set_object (value, playbin->source);
      GST_OBJECT_UNLOCK (playbin);
      break;
    }
    case PROP_FLAGS:
      g_value_set_flags (value, gst_play_bin_get_flags (playbin));
      break;
    case PROP_N_VIDEO:
    {
      GstSourceGroup *group;
      gint n_video;

      GST_PLAY_BIN_LOCK (playbin);
      group = get_group (playbin);
      n_video = (group->video_channels ? group->video_channels->len : 0);
      g_value_set_int (value, n_video);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    }
    case PROP_CURRENT_VIDEO:
      GST_PLAY_BIN_LOCK (playbin);
      g_value_set_int (value, playbin->current_video);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    case PROP_N_AUDIO:
    {
      GstSourceGroup *group;
      gint n_audio;

      GST_PLAY_BIN_LOCK (playbin);
      group = get_group (playbin);
      n_audio = (group->audio_channels ? group->audio_channels->len : 0);
      g_value_set_int (value, n_audio);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    }
    case PROP_CURRENT_AUDIO:
      GST_PLAY_BIN_LOCK (playbin);
      g_value_set_int (value, playbin->current_audio);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    case PROP_N_TEXT:
    {
      GstSourceGroup *group;
      gint n_text;

      GST_PLAY_BIN_LOCK (playbin);
      group = get_group (playbin);
      n_text = (group->text_channels ? group->text_channels->len : 0);
      g_value_set_int (value, n_text);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    }
    case PROP_CURRENT_TEXT:
      GST_PLAY_BIN_LOCK (playbin);
      g_value_set_int (value, playbin->current_text);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    case PROP_SUBTITLE_ENCODING:
      GST_PLAY_BIN_LOCK (playbin);
      g_value_take_string (value,
          gst_play_sink_get_subtitle_encoding (playbin->playsink));
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    case PROP_VIDEO_SINK:
      g_value_take_object (value,
          gst_play_bin_get_current_sink (playbin, &playbin->video_sink,
              "video", GST_PLAY_SINK_TYPE_VIDEO));
      break;
    case PROP_AUDIO_SINK:
      g_value_take_object (value,
          gst_play_bin_get_current_sink (playbin, &playbin->audio_sink,
              "audio", GST_PLAY_SINK_TYPE_AUDIO));
      break;
    case PROP_VIS_PLUGIN:
      g_value_take_object (value,
          gst_play_sink_get_vis_plugin (playbin->playsink));
      break;
    case PROP_TEXT_SINK:
      g_value_take_object (value,
          gst_play_bin_get_current_sink (playbin, &playbin->text_sink,
              "text", GST_PLAY_SINK_TYPE_TEXT));
      break;
    case PROP_VOLUME:
      g_value_set_double (value, gst_play_sink_get_volume (playbin->playsink));
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, gst_play_sink_get_mute (playbin->playsink));
      break;
    case PROP_SAMPLE:
      gst_value_take_sample (value,
          gst_play_sink_get_last_sample (playbin->playsink));
      break;
    case PROP_FONT_DESC:
      g_value_take_string (value,
          gst_play_sink_get_font_desc (playbin->playsink));
      break;
    case PROP_CONNECTION_SPEED:
      GST_PLAY_BIN_LOCK (playbin);
      g_value_set_uint64 (value, playbin->connection_speed / 1000);
      GST_PLAY_BIN_UNLOCK (playbin);
      break;
    case PROP_BUFFER_SIZE:
      GST_OBJECT_LOCK (playbin);
      g_value_set_int (value, playbin->buffer_size);
      GST_OBJECT_UNLOCK (playbin);
      break;
    case PROP_BUFFER_DURATION:
      GST_OBJECT_LOCK (playbin);
      g_value_set_int64 (value, playbin->buffer_duration);
      GST_OBJECT_UNLOCK (playbin);
      break;
    case PROP_AV_OFFSET:
      g_value_set_int64 (value,
          gst_play_sink_get_av_offset (playbin->playsink));
      break;
    case PROP_RING_BUFFER_MAX_SIZE:
      g_value_set_uint64 (value, playbin->ring_buffer_max_size);
      break;
    case PROP_FORCE_ASPECT_RATIO:{
      gboolean v;

      g_object_get (playbin->playsink, "force-aspect-ratio", &v, NULL);
      g_value_set_boolean (value, v);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_bin_update_cached_duration_from_query (GstPlayBin * playbin,
    gboolean valid, GstQuery * query)
{
  GstFormat fmt;
  gint64 duration;
  gint i;

  GST_DEBUG_OBJECT (playbin, "Updating cached duration from query");
  gst_query_parse_duration (query, &fmt, &duration);

  for (i = 0; i < G_N_ELEMENTS (playbin->duration); i++) {
    if (playbin->duration[i].format == 0 || fmt == playbin->duration[i].format) {
      playbin->duration[i].valid = valid;
      playbin->duration[i].format = fmt;
      playbin->duration[i].duration = valid ? duration : -1;
      break;
    }
  }
}

static void
gst_play_bin_update_cached_duration (GstPlayBin * playbin)
{
  const GstFormat formats[] =
      { GST_FORMAT_TIME, GST_FORMAT_BYTES, GST_FORMAT_DEFAULT };
  gboolean ret;
  GstQuery *query;
  gint i;

  GST_DEBUG_OBJECT (playbin, "Updating cached durations before group switch");
  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    query = gst_query_new_duration (formats[i]);
    ret =
        GST_ELEMENT_CLASS (parent_class)->query (GST_ELEMENT_CAST (playbin),
        query);
    gst_play_bin_update_cached_duration_from_query (playbin, ret, query);
    gst_query_unref (query);
  }
}

static gboolean
gst_play_bin_query (GstElement * element, GstQuery * query)
{
  GstPlayBin *playbin = GST_PLAY_BIN (element);
  gboolean ret;

  /* During a group switch we shouldn't allow duration queries
   * because it's not clear if the old or new group's duration
   * is returned and if the sinks are already playing new data
   * or old data. See bug #585969
   *
   * While we're at it, also don't do any other queries during
   * a group switch or any other event that causes topology changes
   * by taking the playbin lock in any case.
   */
  GST_PLAY_BIN_LOCK (playbin);

  if (GST_QUERY_TYPE (query) == GST_QUERY_DURATION) {
    GstSourceGroup *group = playbin->curr_group;
    gboolean pending;

    GST_SOURCE_GROUP_LOCK (group);
    if (group->stream_changed_pending_lock.p) {
      g_mutex_lock (&group->stream_changed_pending_lock);
      pending = group->pending || group->stream_changed_pending;
      g_mutex_unlock (&group->stream_changed_pending_lock);
    } else {
      pending = group->pending;
    }
    if (pending) {
      GstFormat fmt;
      gint i;

      ret = FALSE;
      gst_query_parse_duration (query, &fmt, NULL);
      for (i = 0; i < G_N_ELEMENTS (playbin->duration); i++) {
        if (fmt == playbin->duration[i].format) {
          ret = playbin->duration[i].valid;
          gst_query_set_duration (query, fmt,
              (ret ? playbin->duration[i].duration : -1));
          break;
        }
      }
      /* if nothing cached yet, we might as well request duration,
       * such as during initial startup */
      if (ret) {
        GST_DEBUG_OBJECT (playbin,
            "Taking cached duration because of pending group switch: %d", ret);
        GST_SOURCE_GROUP_UNLOCK (group);
        GST_PLAY_BIN_UNLOCK (playbin);
        return ret;
      }
    }
    GST_SOURCE_GROUP_UNLOCK (group);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->query (element, query);

  if (GST_QUERY_TYPE (query) == GST_QUERY_DURATION)
    gst_play_bin_update_cached_duration_from_query (playbin, ret, query);
  GST_PLAY_BIN_UNLOCK (playbin);

  return ret;
}

/* mime types we are not handling on purpose right now, don't post a
 * missing-plugin message for these */
static const gchar *blacklisted_mimes[] = {
  NULL
};

static void
gst_play_bin_handle_message (GstBin * bin, GstMessage * msg)
{
  GstPlayBin *playbin = GST_PLAY_BIN (bin);
  GstSourceGroup *group;

  if (gst_is_missing_plugin_message (msg)) {
    gchar *detail;
    guint i;

    detail = gst_missing_plugin_message_get_installer_detail (msg);
    for (i = 0; detail != NULL && blacklisted_mimes[i] != NULL; ++i) {
      if (strstr (detail, "|decoder-") && strstr (detail, blacklisted_mimes[i])) {
        GST_LOG_OBJECT (bin, "suppressing message %" GST_PTR_FORMAT, msg);
        gst_message_unref (msg);
        g_free (detail);
        return;
      }
    }
    g_free (detail);
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_START ||
      GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_DONE) {
    GstObject *src = GST_OBJECT_CAST (msg->src);

    /* Ignore async state changes from the uridecodebin children,
     * see bug #602000. */
    group = playbin->curr_group;
    if (src && (group = playbin->curr_group) &&
        ((group->uridecodebin && src == GST_OBJECT_CAST (group->uridecodebin))
            || (group->suburidecodebin
                && src == GST_OBJECT_CAST (group->suburidecodebin)))) {
      GST_DEBUG_OBJECT (playbin,
          "Ignoring async state change of uridecodebin: %s",
          GST_OBJECT_NAME (src));
      gst_message_unref (msg);
      msg = NULL;
    }
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    /* If we get an error of the subtitle uridecodebin transform
     * them into warnings and disable the subtitles */
    group = playbin->curr_group;
    if (group && group->suburidecodebin) {
      if (G_UNLIKELY (gst_object_has_ancestor (msg->src, GST_OBJECT_CAST
                  (group->suburidecodebin)))) {
        GError *err;
        gchar *debug = NULL;
        GstMessage *new_msg;
        GstIterator *it;
        gboolean done = FALSE;
        GValue item = { 0, };

        gst_message_parse_error (msg, &err, &debug);
        new_msg = gst_message_new_warning (msg->src, err, debug);

        gst_message_unref (msg);
        g_error_free (err);
        g_free (debug);
        msg = new_msg;

        REMOVE_SIGNAL (group->suburidecodebin, group->sub_pad_added_id);
        REMOVE_SIGNAL (group->suburidecodebin, group->sub_pad_removed_id);
        REMOVE_SIGNAL (group->suburidecodebin, group->sub_no_more_pads_id);
        REMOVE_SIGNAL (group->suburidecodebin, group->sub_autoplug_continue_id);

        it = gst_element_iterate_src_pads (group->suburidecodebin);
        while (it && !done) {
          GstPad *p = NULL;
          GstIteratorResult res;

          res = gst_iterator_next (it, &item);

          switch (res) {
            case GST_ITERATOR_DONE:
              done = TRUE;
              break;
            case GST_ITERATOR_OK:
              p = g_value_get_object (&item);
              pad_removed_cb (NULL, p, group);
              g_value_reset (&item);
              break;

            case GST_ITERATOR_RESYNC:
              gst_iterator_resync (it);
              break;
            case GST_ITERATOR_ERROR:
              done = TRUE;
              break;
          }
        }
        g_value_unset (&item);
        if (it)
          gst_iterator_free (it);

        gst_object_ref (group->suburidecodebin);
        gst_bin_remove (bin, group->suburidecodebin);
        gst_element_set_locked_state (group->suburidecodebin, FALSE);

        if (group->sub_pending) {
          group->sub_pending = FALSE;
          no_more_pads_cb (NULL, group);
        }
      }
    }
  }

  if (msg)
    GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}

static void
selector_active_pad_changed (GObject * selector, GParamSpec * pspec,
    GstPlayBin * playbin)
{
  const gchar *property;
  GstSourceGroup *group;
  GstSourceSelect *select = NULL;
  int i;

  GST_PLAY_BIN_LOCK (playbin);
  group = get_group (playbin);

  for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
    if (selector == G_OBJECT (group->selector[i].selector)) {
      select = &group->selector[i];
    }
  }

  /* We got a pad-change after our group got switched out; no need to notify */
  if (!select) {
    GST_PLAY_BIN_UNLOCK (playbin);
    return;
  }

  switch (select->type) {
    case GST_PLAY_SINK_TYPE_VIDEO:
    case GST_PLAY_SINK_TYPE_VIDEO_RAW:
      property = "current-video";
      playbin->current_video = get_current_stream_number (playbin,
          group->video_channels);

      if (playbin->video_pending_flush_finish) {
        playbin->video_pending_flush_finish = FALSE;
        GST_PLAY_BIN_UNLOCK (playbin);
        gst_play_bin_send_custom_event (GST_OBJECT (selector),
            "playsink-custom-video-flush-finish");
        goto notify;
      }
      break;
    case GST_PLAY_SINK_TYPE_AUDIO:
    case GST_PLAY_SINK_TYPE_AUDIO_RAW:
      property = "current-audio";
      playbin->current_audio = get_current_stream_number (playbin,
          group->audio_channels);

      if (playbin->audio_pending_flush_finish) {
        playbin->audio_pending_flush_finish = FALSE;
        GST_PLAY_BIN_UNLOCK (playbin);
        gst_play_bin_send_custom_event (GST_OBJECT (selector),
            "playsink-custom-audio-flush-finish");
        goto notify;
      }
      break;
    case GST_PLAY_SINK_TYPE_TEXT:
      property = "current-text";
      playbin->current_text = get_current_stream_number (playbin,
          group->text_channels);

      if (playbin->text_pending_flush_finish) {
        playbin->text_pending_flush_finish = FALSE;
        GST_PLAY_BIN_UNLOCK (playbin);
        gst_play_bin_send_custom_event (GST_OBJECT (selector),
            "playsink-custom-subtitle-flush-finish");
        goto notify;
      }
      break;
    default:
      property = NULL;
  }
  GST_PLAY_BIN_UNLOCK (playbin);

notify:
  if (property)
    g_object_notify (G_OBJECT (playbin), property);
}

static GstPadProbeReturn
_suburidecodebin_event_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer udata)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstSourceGroup *group = udata;
  GstEvent *event = GST_PAD_PROBE_INFO_DATA (info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    {
      guint32 seqnum = gst_event_get_seqnum (event);
      GSList *item = g_slist_find (group->suburi_flushes_to_drop,
          GUINT_TO_POINTER (seqnum));
      if (item) {
        ret = GST_PAD_PROBE_DROP;       /* this is from subtitle seek only, drop it */
        if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
          group->suburi_flushes_to_drop =
              g_slist_delete_link (group->suburi_flushes_to_drop, item);
        }
      }
    }
    default:
      break;
  }
  return ret;
}

/* helper function to lookup stuff in lists */
static gboolean
array_has_value (const gchar * values[], const gchar * value, gboolean exact)
{
  gint i;

  for (i = 0; values[i]; i++) {
    if (exact && !strcmp (value, values[i]))
      return TRUE;
    if (!exact && g_str_has_prefix (value, values[i]))
      return TRUE;
  }
  return FALSE;
}

typedef struct
{
  GstPlayBin *playbin;
  gint stream_id;
  GstPlaySinkType type;
} NotifyTagsData;

static void
notify_tags_cb (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  NotifyTagsData *ntdata = (NotifyTagsData *) user_data;
  gint signal;

  GST_DEBUG_OBJECT (ntdata->playbin, "Tags on pad %" GST_PTR_FORMAT
      " with stream id %d and type %d have changed",
      object, ntdata->stream_id, ntdata->type);

  switch (ntdata->type) {
    case GST_PLAY_SINK_TYPE_VIDEO:
    case GST_PLAY_SINK_TYPE_VIDEO_RAW:
      signal = SIGNAL_VIDEO_TAGS_CHANGED;
      break;
    case GST_PLAY_SINK_TYPE_AUDIO:
    case GST_PLAY_SINK_TYPE_AUDIO_RAW:
      signal = SIGNAL_AUDIO_TAGS_CHANGED;
      break;
    case GST_PLAY_SINK_TYPE_TEXT:
      signal = SIGNAL_TEXT_TAGS_CHANGED;
      break;
    default:
      signal = -1;
      break;
  }

  if (signal >= 0)
    g_signal_emit (G_OBJECT (ntdata->playbin), gst_play_bin_signals[signal], 0,
        ntdata->stream_id);
}

/* this function is called when a new pad is added to decodebin. We check the
 * type of the pad and add it to the selector element of the group.
 */
static void
pad_added_cb (GstElement * decodebin, GstPad * pad, GstSourceGroup * group)
{
  GstPlayBin *playbin;
  GstCaps *caps;
  const GstStructure *s;
  const gchar *name;
  GstPad *sinkpad;
  GstPadLinkReturn res;
  GstSourceSelect *select = NULL;
  gint i, pass;
  gboolean changed = FALSE;

  playbin = group->playbin;

  caps = gst_pad_query_caps (pad, NULL);
  s = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (s);

  GST_DEBUG_OBJECT (playbin,
      "pad %s:%s with caps %" GST_PTR_FORMAT " added in group %p",
      GST_DEBUG_PAD_NAME (pad), caps, group);

  /* major type of the pad, this determines the selector to use,
     try exact match first */
  for (pass = 0; !select && pass < 2; pass++) {
    for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
      if (array_has_value (group->selector[i].media_list, name, pass == 0)) {
        select = &group->selector[i];
        break;
      } else if (group->selector[i].get_media_caps) {
        GstCaps *media_caps = group->selector[i].get_media_caps ();

        if (media_caps && gst_caps_can_intersect (media_caps, caps)) {
          select = &group->selector[i];
          gst_caps_unref (media_caps);
          break;
        }
        gst_caps_unref (media_caps);
      }
    }
  }
  /* no selector found for the media type, don't bother linking it to a
   * selector. This will leave the pad unlinked and thus ignored. */
  if (select == NULL)
    goto unknown_type;

  GST_SOURCE_GROUP_LOCK (group);
  if (select->selector == NULL && playbin->have_selector) {
    /* no selector, create one */
    GST_DEBUG_OBJECT (playbin, "creating new input selector");
    select->selector = gst_element_factory_make ("input-selector", NULL);
    if (select->selector == NULL) {
      /* post the missing selector message only once */
      playbin->have_selector = FALSE;
      gst_element_post_message (GST_ELEMENT_CAST (playbin),
          gst_missing_element_message_new (GST_ELEMENT_CAST (playbin),
              "input-selector"));
      GST_ELEMENT_WARNING (playbin, CORE, MISSING_PLUGIN,
          (_("Missing element '%s' - check your GStreamer installation."),
              "input-selector"), (NULL));
    } else {
      /* sync-mode=1, use clock */
      if (select->type == GST_PLAY_SINK_TYPE_TEXT)
        g_object_set (select->selector, "sync-streams", TRUE,
            "sync-mode", 1, "cache-buffers", TRUE, NULL);
      else
        g_object_set (select->selector, "sync-streams", TRUE, NULL);

      g_signal_connect (select->selector, "notify::active-pad",
          G_CALLBACK (selector_active_pad_changed), playbin);

      GST_DEBUG_OBJECT (playbin, "adding new selector %p", select->selector);
      gst_bin_add (GST_BIN_CAST (playbin), select->selector);
      gst_element_set_state (select->selector, GST_STATE_PAUSED);
    }
  }

  if (select->srcpad == NULL) {
    if (select->selector) {
      /* save source pad of the selector */
      select->srcpad = gst_element_get_static_pad (select->selector, "src");
    } else {
      /* no selector, use the pad as the source pad then */
      select->srcpad = gst_object_ref (pad);
    }

    /* block the selector srcpad. It's possible that multiple decodebins start
     * pushing data into the selectors before we have a chance to collect all
     * streams and connect the sinks, resulting in not-linked errors. After we
     * configured the sinks we will unblock them all. */
    GST_DEBUG_OBJECT (playbin, "blocking %" GST_PTR_FORMAT, select->srcpad);
    select->block_id =
        gst_pad_add_probe (select->srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
        NULL, NULL, NULL);
  }

  /* get sinkpad for the new stream */
  if (select->selector) {
    if ((sinkpad = gst_element_get_request_pad (select->selector, "sink_%u"))) {
      gulong notify_tags_handler = 0;
      NotifyTagsData *ntdata;

      GST_DEBUG_OBJECT (playbin, "got pad %s:%s from selector",
          GST_DEBUG_PAD_NAME (sinkpad));

      /* store the selector for the pad */
      g_object_set_data (G_OBJECT (sinkpad), "playbin.select", select);

      /* connect to the notify::tags signal for our
       * own *-tags-changed signals
       */
      ntdata = g_new0 (NotifyTagsData, 1);
      ntdata->playbin = playbin;
      ntdata->stream_id = select->channels->len;
      ntdata->type = select->type;

      notify_tags_handler =
          g_signal_connect_data (G_OBJECT (sinkpad), "notify::tags",
          G_CALLBACK (notify_tags_cb), ntdata, (GClosureNotify) g_free,
          (GConnectFlags) 0);
      g_object_set_data (G_OBJECT (sinkpad), "playbin.notify_tags_handler",
          (gpointer) (guintptr) notify_tags_handler);

      /* store the pad in the array */
      GST_DEBUG_OBJECT (playbin, "pad %p added to array", sinkpad);
      g_ptr_array_add (select->channels, sinkpad);

      res = gst_pad_link (pad, sinkpad);
      if (GST_PAD_LINK_FAILED (res))
        goto link_failed;

      /* store selector pad so we can release it */
      g_object_set_data (G_OBJECT (pad), "playbin.sinkpad", sinkpad);

      changed = TRUE;
      GST_DEBUG_OBJECT (playbin, "linked pad %s:%s to selector %p",
          GST_DEBUG_PAD_NAME (pad), select->selector);
    }
  } else {
    /* no selector, don't configure anything, we'll link the new pad directly to
     * the sink. */
    changed = FALSE;
    sinkpad = NULL;

    /* store the selector for the pad */
    g_object_set_data (G_OBJECT (pad), "playbin.select", select);
  }
  GST_SOURCE_GROUP_UNLOCK (group);

  if (decodebin == group->suburidecodebin) {
    /* TODO store the probe id */
    /* to avoid propagating flushes from suburi specific seeks */
    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        _suburidecodebin_event_probe, group, NULL);
  }

  if (changed) {
    int signal;
    gboolean always_ok = (decodebin == group->suburidecodebin);

    switch (select->type) {
      case GST_PLAY_SINK_TYPE_VIDEO:
      case GST_PLAY_SINK_TYPE_VIDEO_RAW:
        /* we want to return NOT_LINKED for unselected pads but only for pads
         * from the normal uridecodebin. This makes sure that subtitle streams
         * are not raced past audio/video from decodebin's multiqueue.
         * For pads from suburidecodebin OK should always be returned, otherwise
         * it will most likely stop. */
        g_object_set (sinkpad, "always-ok", always_ok, NULL);
        signal = SIGNAL_VIDEO_CHANGED;
        break;
      case GST_PLAY_SINK_TYPE_AUDIO:
      case GST_PLAY_SINK_TYPE_AUDIO_RAW:
        g_object_set (sinkpad, "always-ok", always_ok, NULL);
        signal = SIGNAL_AUDIO_CHANGED;
        break;
      case GST_PLAY_SINK_TYPE_TEXT:
        g_object_set (sinkpad, "always-ok", always_ok, NULL);
        signal = SIGNAL_TEXT_CHANGED;
        break;
      default:
        signal = -1;
    }

    if (signal >= 0)
      g_signal_emit (G_OBJECT (playbin), gst_play_bin_signals[signal], 0, NULL);
  }

done:
  gst_caps_unref (caps);
  return;

  /* ERRORS */
unknown_type:
  {
    GST_ERROR_OBJECT (playbin, "unknown type %s for pad %s:%s",
        name, GST_DEBUG_PAD_NAME (pad));
    goto done;
  }
link_failed:
  {
    GST_ERROR_OBJECT (playbin,
        "failed to link pad %s:%s to selector, reason %d",
        GST_DEBUG_PAD_NAME (pad), res);
    GST_SOURCE_GROUP_UNLOCK (group);
    goto done;
  }
}

/* called when a pad is removed from the uridecodebin. We unlink the pad from
 * the selector. This will make the selector select a new pad. */
static void
pad_removed_cb (GstElement * decodebin, GstPad * pad, GstSourceGroup * group)
{
  GstPlayBin *playbin;
  GstPad *peer;
  GstElement *selector;
  GstSourceSelect *select;

  playbin = group->playbin;

  GST_DEBUG_OBJECT (playbin,
      "pad %s:%s removed from group %p", GST_DEBUG_PAD_NAME (pad), group);

  GST_SOURCE_GROUP_LOCK (group);

  if ((select = g_object_get_data (G_OBJECT (pad), "playbin.select"))) {
    g_assert (select->selector == NULL);
    g_assert (select->srcpad == pad);
    gst_object_unref (pad);
    select->srcpad = NULL;
    goto exit;
  }

  /* get the selector sinkpad */
  if (!(peer = g_object_get_data (G_OBJECT (pad), "playbin.sinkpad")))
    goto not_linked;

  if ((select = g_object_get_data (G_OBJECT (peer), "playbin.select"))) {
    gulong notify_tags_handler;

    notify_tags_handler =
        (guintptr) g_object_get_data (G_OBJECT (peer),
        "playbin.notify_tags_handler");
    if (notify_tags_handler != 0)
      g_signal_handler_disconnect (G_OBJECT (peer), notify_tags_handler);
    g_object_set_data (G_OBJECT (peer), "playbin.notify_tags_handler", NULL);

    /* remove the pad from the array */
    g_ptr_array_remove (select->channels, peer);
    GST_DEBUG_OBJECT (playbin, "pad %p removed from array", peer);

    if (!select->channels->len && select->selector) {
      GST_DEBUG_OBJECT (playbin, "all selector sinkpads removed");
      GST_DEBUG_OBJECT (playbin, "removing selector %p", select->selector);
      gst_object_unref (select->srcpad);
      select->srcpad = NULL;
      gst_element_set_state (select->selector, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (playbin), select->selector);
      select->selector = NULL;
    }
  }

  /* unlink the pad now (can fail, the pad is unlinked before it's removed) */
  gst_pad_unlink (pad, peer);

  /* get selector, this can be NULL when the element is removing the pads
   * because it's being disposed. */
  selector = GST_ELEMENT_CAST (gst_pad_get_parent (peer));
  if (!selector) {
    gst_object_unref (peer);
    goto no_selector;
  }

  /* release the pad to the selector, this will make the selector choose a new
   * pad. */
  gst_element_release_request_pad (selector, peer);
  gst_object_unref (peer);

  gst_object_unref (selector);
exit:
  GST_SOURCE_GROUP_UNLOCK (group);

  return;

  /* ERRORS */
not_linked:
  {
    GST_DEBUG_OBJECT (playbin, "pad not linked");
    GST_SOURCE_GROUP_UNLOCK (group);
    return;
  }
no_selector:
  {
    GST_DEBUG_OBJECT (playbin, "selector not found");
    GST_SOURCE_GROUP_UNLOCK (group);
    return;
  }
}

/* we get called when all pads are available and we must connect the sinks to
 * them.
 * The main purpose of the code is to see if we have video/audio and subtitles
 * and pick the right pipelines to display them.
 *
 * The selectors installed on the group tell us about the presence of
 * audio/video and subtitle streams. This allows us to see if we need
 * visualisation, video or/and audio.
 */
static void
no_more_pads_cb (GstElement * decodebin, GstSourceGroup * group)
{
  GstPlayBin *playbin;
  GstPadLinkReturn res;
  gint i;
  gboolean configure;

  playbin = group->playbin;

  GST_DEBUG_OBJECT (playbin, "no more pads in group %p", group);

  GST_PLAY_BIN_SHUTDOWN_LOCK (playbin, shutdown);

  GST_SOURCE_GROUP_LOCK (group);
  for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
    GstSourceSelect *select = &group->selector[i];

    /* check if the specific media type was detected and thus has a selector
     * created for it. If there is the media type, get a sinkpad from the sink
     * and link it. We only do this if we have not yet requested the sinkpad
     * before. */
    if (select->srcpad && select->sinkpad == NULL) {
      GST_DEBUG_OBJECT (playbin, "requesting new sink pad %d", select->type);
      select->sinkpad =
          gst_play_sink_request_pad (playbin->playsink, select->type);
    } else if (select->srcpad && select->sinkpad) {
      GST_DEBUG_OBJECT (playbin, "refreshing new sink pad %d", select->type);
      gst_play_sink_refresh_pad (playbin->playsink, select->sinkpad,
          select->type);
    } else if (select->sinkpad && select->srcpad == NULL) {
      GST_DEBUG_OBJECT (playbin, "releasing sink pad %d", select->type);
      gst_play_sink_release_pad (playbin->playsink, select->sinkpad);
      select->sinkpad = NULL;
    }
    if (select->sinkpad && select->srcpad &&
        !gst_pad_is_linked (select->srcpad)) {
      res = gst_pad_link (select->srcpad, select->sinkpad);
      GST_DEBUG_OBJECT (playbin, "linked type %s, result: %d",
          select->media_list[0], res);
      if (res != GST_PAD_LINK_OK) {
        GST_ELEMENT_ERROR (playbin, CORE, PAD,
            ("Internal playbin error."),
            ("Failed to link selector to sink. Error %d", res));
      }
    }
  }
  GST_DEBUG_OBJECT (playbin, "pending %d > %d", group->pending,
      group->pending - 1);

  if (group->pending > 0)
    group->pending--;

  if (group->suburidecodebin == decodebin)
    group->sub_pending = FALSE;

  if (group->pending == 0) {
    /* we are the last group to complete, we will configure the output and then
     * signal the other waiters. */
    GST_LOG_OBJECT (playbin, "last group complete");
    configure = TRUE;
  } else {
    GST_LOG_OBJECT (playbin, "have more pending groups");
    configure = FALSE;
  }
  GST_SOURCE_GROUP_UNLOCK (group);

  if (configure) {
    /* if we have custom sinks, configure them now */
    GST_SOURCE_GROUP_LOCK (group);

    if (group->audio_sink) {
      GST_INFO_OBJECT (playbin, "setting custom audio sink %" GST_PTR_FORMAT,
          group->audio_sink);
      gst_play_sink_set_sink (playbin->playsink, GST_PLAY_SINK_TYPE_AUDIO,
          group->audio_sink);
    }

    if (group->video_sink) {
      GST_INFO_OBJECT (playbin, "setting custom video sink %" GST_PTR_FORMAT,
          group->video_sink);
      gst_play_sink_set_sink (playbin->playsink, GST_PLAY_SINK_TYPE_VIDEO,
          group->video_sink);
    }

    if (playbin->text_sink) {
      GST_INFO_OBJECT (playbin, "setting custom text sink %" GST_PTR_FORMAT,
          playbin->text_sink);
      gst_play_sink_set_sink (playbin->playsink, GST_PLAY_SINK_TYPE_TEXT,
          playbin->text_sink);
    }

    GST_SOURCE_GROUP_UNLOCK (group);

    /* signal the other decodebins that they can continue now. */
    GST_SOURCE_GROUP_LOCK (group);
    /* unblock all selectors */
    for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
      GstSourceSelect *select = &group->selector[i];

      if (select->srcpad) {
        GST_DEBUG_OBJECT (playbin, "unblocking %" GST_PTR_FORMAT,
            select->srcpad);
        if (select->block_id) {
          gst_pad_remove_probe (select->srcpad, select->block_id);
          select->block_id = 0;
        }
      }
    }
    GST_SOURCE_GROUP_UNLOCK (group);
  }

  GST_PLAY_BIN_SHUTDOWN_UNLOCK (playbin);

  return;

shutdown:
  {
    GST_DEBUG ("ignoring, we are shutting down");
    /* Request a flushing pad from playsink that we then link to the selector.
     * Then we unblock the selectors so that they stop with a WRONG_STATE
     * instead of a NOT_LINKED error.
     */
    GST_SOURCE_GROUP_LOCK (group);
    for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
      GstSourceSelect *select = &group->selector[i];

      if (select->srcpad) {
        if (select->sinkpad == NULL) {
          GST_DEBUG_OBJECT (playbin, "requesting new flushing sink pad");
          select->sinkpad =
              gst_play_sink_request_pad (playbin->playsink,
              GST_PLAY_SINK_TYPE_FLUSHING);
          res = gst_pad_link (select->srcpad, select->sinkpad);
          GST_DEBUG_OBJECT (playbin, "linked flushing, result: %d", res);
        }
        GST_DEBUG_OBJECT (playbin, "unblocking %" GST_PTR_FORMAT,
            select->srcpad);
        if (select->block_id) {
          gst_pad_remove_probe (select->srcpad, select->block_id);
          select->block_id = 0;
        }
      }
    }
    GST_SOURCE_GROUP_UNLOCK (group);
    return;
  }
}

static void
drained_cb (GstElement * decodebin, GstSourceGroup * group)
{
  GstPlayBin *playbin;

  playbin = group->playbin;

  GST_DEBUG_OBJECT (playbin, "about to finish in group %p", group);

  /* after this call, we should have a next group to activate or we EOS */
  g_signal_emit (G_OBJECT (playbin),
      gst_play_bin_signals[SIGNAL_ABOUT_TO_FINISH], 0, NULL);

  /* now activate the next group. If the app did not set a uri, this will
   * fail and we can do EOS */
  setup_next_source (playbin, GST_STATE_PAUSED);
}

/* Like gst_element_factory_can_sink_any_caps() but doesn't
 * allow ANY caps on the sinkpad template */
static gboolean
_factory_can_sink_caps (GstElementFactory * factory, GstCaps * caps)
{
  const GList *templs;

  templs = gst_element_factory_get_static_pad_templates (factory);

  while (templs) {
    GstStaticPadTemplate *templ = (GstStaticPadTemplate *) templs->data;

    if (templ->direction == GST_PAD_SINK) {
      GstCaps *templcaps = gst_static_caps_get (&templ->static_caps);

      if (!gst_caps_is_any (templcaps)
          && gst_caps_can_intersect (templcaps, caps)) {
        gst_caps_unref (templcaps);
        return TRUE;
      }
      gst_caps_unref (templcaps);
    }
    templs = g_list_next (templs);
  }

  return FALSE;
}

/* Called when we must provide a list of factories to plug to @pad with @caps.
 * We first check if we have a sink that can handle the format and if we do, we
 * return NULL, to expose the pad. If we have no sink (or the sink does not
 * work), we return the list of elements that can connect. */
static GValueArray *
autoplug_factories_cb (GstElement * decodebin, GstPad * pad,
    GstCaps * caps, GstSourceGroup * group)
{
  GstPlayBin *playbin;
  GList *mylist, *tmp;
  GValueArray *result;

  playbin = group->playbin;

  GST_DEBUG_OBJECT (playbin, "factories group %p for %s:%s, %" GST_PTR_FORMAT,
      group, GST_DEBUG_PAD_NAME (pad), caps);

  /* filter out the elements based on the caps. */
  g_mutex_lock (&playbin->elements_lock);
  gst_play_bin_update_elements_list (playbin);
  mylist =
      gst_element_factory_list_filter (playbin->elements, caps, GST_PAD_SINK,
      FALSE);
  g_mutex_unlock (&playbin->elements_lock);

  GST_DEBUG_OBJECT (playbin, "found factories %p", mylist);
  GST_PLUGIN_FEATURE_LIST_DEBUG (mylist);

  /* 2 additional elements for the already set audio/video sinks */
  result = g_value_array_new (g_list_length (mylist) + 2);

  /* Check if we already have an audio/video sink and if this is the case
   * put it as the first element of the array */
  if (group->audio_sink) {
    GstElementFactory *factory = gst_element_get_factory (group->audio_sink);

    if (factory && _factory_can_sink_caps (factory, caps)) {
      GValue val = { 0, };

      g_value_init (&val, G_TYPE_OBJECT);
      g_value_set_object (&val, factory);
      result = g_value_array_append (result, &val);
      g_value_unset (&val);
    }
  }

  if (group->video_sink) {
    GstElementFactory *factory = gst_element_get_factory (group->video_sink);

    if (factory && _factory_can_sink_caps (factory, caps)) {
      GValue val = { 0, };

      g_value_init (&val, G_TYPE_OBJECT);
      g_value_set_object (&val, factory);
      result = g_value_array_append (result, &val);
      g_value_unset (&val);
    }
  }

  for (tmp = mylist; tmp; tmp = tmp->next) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY_CAST (tmp->data);
    GValue val = { 0, };

    if (group->audio_sink && gst_element_factory_list_is_type (factory,
            GST_ELEMENT_FACTORY_TYPE_SINK |
            GST_ELEMENT_FACTORY_TYPE_MEDIA_AUDIO)) {
      continue;
    }
    if (group->video_sink && gst_element_factory_list_is_type (factory,
            GST_ELEMENT_FACTORY_TYPE_SINK | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO
            | GST_ELEMENT_FACTORY_TYPE_MEDIA_IMAGE)) {
      continue;
    }

    g_value_init (&val, G_TYPE_OBJECT);
    g_value_set_object (&val, factory);
    g_value_array_append (result, &val);
    g_value_unset (&val);
  }
  gst_plugin_feature_list_free (mylist);

  return result;
}

/* autoplug-continue decides, if a pad has raw caps that can be exposed
 * directly or if further decoding is necessary. We use this to expose
 * supported subtitles directly */

/* FIXME 0.11: Remove the checks for ANY caps, a sink should specify
 * explicitly the caps it supports and if it claims to support ANY
 * caps it really should support everything */
static gboolean
autoplug_continue_cb (GstElement * element, GstPad * pad, GstCaps * caps,
    GstSourceGroup * group)
{
  gboolean ret = TRUE;
  GstElement *sink;
  GstPad *sinkpad = NULL;

  GST_SOURCE_GROUP_LOCK (group);

  if ((sink = group->playbin->text_sink))
    sinkpad = gst_element_get_static_pad (sink, "sink");
  if (sinkpad) {
    GstCaps *sinkcaps;

    /* Ignore errors here, if a custom sink fails to go
     * to READY things are wrong and will error out later
     */
    if (GST_STATE (sink) < GST_STATE_READY)
      gst_element_set_state (sink, GST_STATE_READY);

    sinkcaps = gst_pad_query_caps (sinkpad, NULL);
    if (!gst_caps_is_any (sinkcaps))
      ret = !gst_pad_query_accept_caps (sinkpad, caps);
    gst_caps_unref (sinkcaps);
    gst_object_unref (sinkpad);
  } else {
    GstCaps *subcaps = gst_subtitle_overlay_create_factory_caps ();
    ret = !gst_caps_is_subset (caps, subcaps);
    gst_caps_unref (subcaps);
  }
  /* If autoplugging can stop don't do additional checks */
  if (!ret)
    goto done;

  /* If this is from the subtitle uridecodebin we don't need to
   * check the audio and video sink */
  if (group->suburidecodebin
      && gst_object_has_ancestor (GST_OBJECT_CAST (element),
          GST_OBJECT_CAST (group->suburidecodebin)))
    goto done;

  if ((sink = group->audio_sink)) {
    sinkpad = gst_element_get_static_pad (sink, "sink");
    if (sinkpad) {
      GstCaps *sinkcaps;

      /* Ignore errors here, if a custom sink fails to go
       * to READY things are wrong and will error out later
       */
      if (GST_STATE (sink) < GST_STATE_READY)
        gst_element_set_state (sink, GST_STATE_READY);

      sinkcaps = gst_pad_query_caps (sinkpad, NULL);
      if (!gst_caps_is_any (sinkcaps))
        ret = !gst_pad_query_accept_caps (sinkpad, caps);
      gst_caps_unref (sinkcaps);
      gst_object_unref (sinkpad);
    }
  }
  if (!ret)
    goto done;

  if ((sink = group->video_sink)) {
    sinkpad = gst_element_get_static_pad (sink, "sink");
    if (sinkpad) {
      GstCaps *sinkcaps;

      /* Ignore errors here, if a custom sink fails to go
       * to READY things are wrong and will error out later
       */
      if (GST_STATE (sink) < GST_STATE_READY)
        gst_element_set_state (sink, GST_STATE_READY);

      sinkcaps = gst_pad_query_caps (sinkpad, NULL);
      if (!gst_caps_is_any (sinkcaps))
        ret = !gst_pad_query_accept_caps (sinkpad, caps);
      gst_caps_unref (sinkcaps);
      gst_object_unref (sinkpad);
    }
  }

done:
  GST_SOURCE_GROUP_UNLOCK (group);

  GST_DEBUG_OBJECT (group->playbin,
      "continue autoplugging group %p for %s:%s, %" GST_PTR_FORMAT ": %d",
      group, GST_DEBUG_PAD_NAME (pad), caps, ret);

  return ret;
}

static gboolean
sink_accepts_caps (GstElement * sink, GstCaps * caps)
{
  GstPad *sinkpad;

  /* ... activate it ... We do this before adding it to the bin so that we
   * don't accidentally make it post error messages that will stop
   * everything. */
  if (GST_STATE (sink) < GST_STATE_READY &&
      gst_element_set_state (sink,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
    return FALSE;
  }

  if ((sinkpad = gst_element_get_static_pad (sink, "sink"))) {
    /* Got the sink pad, now let's see if the element actually does accept the
     * caps that we have */
    if (!gst_pad_query_accept_caps (sinkpad, caps)) {
      gst_object_unref (sinkpad);
      return FALSE;
    }
    gst_object_unref (sinkpad);
  }

  return TRUE;
}

static GstStaticCaps raw_audio_caps = GST_STATIC_CAPS ("audio/x-raw");
static GstStaticCaps raw_video_caps = GST_STATIC_CAPS ("video/x-raw");

/* We are asked to select an element. See if the next element to check
 * is a sink. If this is the case, we see if the sink works by setting it to
 * READY. If the sink works, we return SELECT_EXPOSE to make decodebin
 * expose the raw pad so that we can setup the mixers. */
static GstAutoplugSelectResult
autoplug_select_cb (GstElement * decodebin, GstPad * pad,
    GstCaps * caps, GstElementFactory * factory, GstSourceGroup * group)
{
  GstPlayBin *playbin;
  GstElement *element;
  const gchar *klass;
  GstPlaySinkType type;
  GstElement **sinkp;

  playbin = group->playbin;

  GST_DEBUG_OBJECT (playbin, "select group %p for %s:%s, %" GST_PTR_FORMAT,
      group, GST_DEBUG_PAD_NAME (pad), caps);

  GST_DEBUG_OBJECT (playbin, "checking factory %s", GST_OBJECT_NAME (factory));

  /* if it's not a sink, we make sure the element is compatible with
   * the fixed sink */
  if (!gst_element_factory_list_is_type (factory,
          GST_ELEMENT_FACTORY_TYPE_SINK)) {
    gboolean isvideodec = gst_element_factory_list_is_type (factory,
        GST_ELEMENT_FACTORY_TYPE_DECODER |
        GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO |
        GST_ELEMENT_FACTORY_TYPE_MEDIA_IMAGE);
    gboolean isaudiodec = gst_element_factory_list_is_type (factory,
        GST_ELEMENT_FACTORY_TYPE_DECODER |
        GST_ELEMENT_FACTORY_TYPE_MEDIA_AUDIO);

    /* If it is a decoder and we have a fixed sink for the media
     * type it outputs, check that the decoder is compatible with this sink */
    if ((isvideodec && group->video_sink) || (isaudiodec && group->audio_sink)) {
      gboolean compatible = TRUE;
      GstPad *sinkpad;
      GstCaps *caps;
      GstElement *sink;

      if (isaudiodec)
        sink = group->audio_sink;
      else
        sink = group->video_sink;

      if ((sinkpad = gst_element_get_static_pad (sink, "sink"))) {
        GstPlayFlags flags = gst_play_bin_get_flags (playbin);
        GstCaps *raw_caps =
            (isaudiodec) ? gst_static_caps_get (&raw_audio_caps) :
            gst_static_caps_get (&raw_video_caps);

        caps = gst_pad_query_caps (sinkpad, NULL);

        /* If the sink supports raw audio/video, we first check
         * if the decoder could output any raw audio/video format
         * and assume it is compatible with the sink then. We don't
         * do a complete compatibility check here if converters
         * are plugged between the decoder and the sink because
         * the converters will convert between raw formats and
         * even if the decoder format is not supported by the decoder
         * a converter will convert it.
         *
         * We assume here that the converters can convert between
         * any raw format.
         */
        if ((isaudiodec && !(flags & GST_PLAY_FLAG_NATIVE_AUDIO)
                && gst_caps_can_intersect (caps, raw_caps)) || (!isaudiodec
                && !(flags & GST_PLAY_FLAG_NATIVE_VIDEO)
                && gst_caps_can_intersect (caps, raw_caps))) {
          compatible = gst_element_factory_can_src_any_caps (factory, raw_caps)
              || gst_element_factory_can_src_any_caps (factory, caps);
        } else {
          compatible = gst_element_factory_can_src_any_caps (factory, caps);
        }

        gst_object_unref (sinkpad);
        gst_caps_unref (caps);
      }

      if (compatible)
        return GST_AUTOPLUG_SELECT_TRY;

      GST_DEBUG_OBJECT (playbin, "%s not compatible with the fixed sink",
          GST_OBJECT_NAME (factory));

      return GST_AUTOPLUG_SELECT_SKIP;
    } else
      return GST_AUTOPLUG_SELECT_TRY;
  }

  /* it's a sink, see if an instance of it actually works */
  GST_DEBUG_OBJECT (playbin, "we found a sink");

  klass =
      gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);

  /* figure out the klass */
  if (strstr (klass, "Audio")) {
    GST_DEBUG_OBJECT (playbin, "we found an audio sink");
    type = GST_PLAY_SINK_TYPE_AUDIO;
    sinkp = &group->audio_sink;
  } else if (strstr (klass, "Video")) {
    GST_DEBUG_OBJECT (playbin, "we found a video sink");
    type = GST_PLAY_SINK_TYPE_VIDEO;
    sinkp = &group->video_sink;
  } else {
    /* unknown klass, skip this element */
    GST_WARNING_OBJECT (playbin, "unknown sink klass %s found", klass);
    return GST_AUTOPLUG_SELECT_SKIP;
  }

  /* if we are asked to do visualisations and it's an audio sink, skip the
   * element. We can only do visualisations with raw sinks */
  if (gst_play_sink_get_flags (playbin->playsink) & GST_PLAY_FLAG_VIS) {
    if (type == GST_PLAY_SINK_TYPE_AUDIO) {
      GST_DEBUG_OBJECT (playbin, "skip audio sink because of vis");
      return GST_AUTOPLUG_SELECT_SKIP;
    }
  }

  /* now see if we already have a sink element */
  GST_SOURCE_GROUP_LOCK (group);
  if (*sinkp) {
    GstElement *sink = gst_object_ref (*sinkp);

    if (sink_accepts_caps (sink, caps)) {
      GST_DEBUG_OBJECT (playbin,
          "Existing sink '%s' accepts caps: %" GST_PTR_FORMAT,
          GST_ELEMENT_NAME (sink), caps);
      gst_object_unref (sink);
      GST_SOURCE_GROUP_UNLOCK (group);
      return GST_AUTOPLUG_SELECT_EXPOSE;
    } else {
      GST_DEBUG_OBJECT (playbin,
          "Existing sink '%s' does not accept caps: %" GST_PTR_FORMAT,
          GST_ELEMENT_NAME (sink), caps);
      gst_object_unref (sink);
      GST_SOURCE_GROUP_UNLOCK (group);
      return GST_AUTOPLUG_SELECT_SKIP;
    }
  }
  GST_DEBUG_OBJECT (playbin, "we have no pending sink, try to create one");

  if ((element = gst_element_factory_create (factory, NULL)) == NULL) {
    GST_WARNING_OBJECT (playbin, "Could not create an element from %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
    GST_SOURCE_GROUP_UNLOCK (group);
    return GST_AUTOPLUG_SELECT_SKIP;
  }

  /* Check if the selected sink actually supports the
   * caps and can be set to READY*/
  if (!sink_accepts_caps (element, caps)) {
    gst_element_set_state (element, GST_STATE_NULL);
    gst_object_unref (element);
    GST_SOURCE_GROUP_UNLOCK (group);
    return GST_AUTOPLUG_SELECT_SKIP;
  }

  /* remember the sink in the group now, the element is floating, we take
   * ownership now 
   *
   * store the sink in the group, we will configure it later when we
   * reconfigure the sink */
  GST_DEBUG_OBJECT (playbin, "remember sink");
  gst_object_ref_sink (element);
  *sinkp = element;
  GST_SOURCE_GROUP_UNLOCK (group);

  /* tell decodebin to expose the pad because we are going to use this
   * sink */
  GST_DEBUG_OBJECT (playbin, "we found a working sink, expose pad");

  return GST_AUTOPLUG_SELECT_EXPOSE;
}

static void
notify_source_cb (GstElement * uridecodebin, GParamSpec * pspec,
    GstSourceGroup * group)
{
  GstPlayBin *playbin;
  GstElement *source;

  playbin = group->playbin;

  g_object_get (group->uridecodebin, "source", &source, NULL);

  GST_OBJECT_LOCK (playbin);
  if (playbin->source)
    gst_object_unref (playbin->source);
  playbin->source = source;
  GST_OBJECT_UNLOCK (playbin);

  g_object_notify (G_OBJECT (playbin), "source");

  g_signal_emit (playbin, gst_play_bin_signals[SIGNAL_SOURCE_SETUP],
      0, playbin->source);
}

/* must be called with the group lock */
static gboolean
group_set_locked_state_unlocked (GstPlayBin * playbin, GstSourceGroup * group,
    gboolean locked)
{
  GST_DEBUG_OBJECT (playbin, "locked_state %d on group %p", locked, group);

  if (group->uridecodebin)
    gst_element_set_locked_state (group->uridecodebin, locked);
  if (group->suburidecodebin)
    gst_element_set_locked_state (group->suburidecodebin, locked);

  return TRUE;
}

/* must be called with PLAY_BIN_LOCK */
static gboolean
activate_group (GstPlayBin * playbin, GstSourceGroup * group, GstState target)
{
  GstElement *uridecodebin;
  GstElement *suburidecodebin = NULL;
  GstPlayFlags flags;

  g_return_val_if_fail (group->valid, FALSE);
  g_return_val_if_fail (!group->active, FALSE);

  GST_DEBUG_OBJECT (playbin, "activating group %p", group);

  GST_SOURCE_GROUP_LOCK (group);

  /* First set up the custom sources */
  if (playbin->audio_sink)
    group->audio_sink = gst_object_ref (playbin->audio_sink);
  if (playbin->video_sink)
    group->video_sink = gst_object_ref (playbin->video_sink);

  g_list_free (group->stream_changed_pending);
  group->stream_changed_pending = NULL;
  if (!group->stream_changed_pending_lock.p)
    g_mutex_init (&group->stream_changed_pending_lock);

  g_slist_free (group->suburi_flushes_to_drop);
  group->suburi_flushes_to_drop = NULL;
  if (!group->suburi_flushes_to_drop_lock.p)
    g_mutex_init (&group->suburi_flushes_to_drop_lock);

  if (group->uridecodebin) {
    GST_DEBUG_OBJECT (playbin, "reusing existing uridecodebin");
    uridecodebin = group->uridecodebin;
    gst_element_set_state (uridecodebin, GST_STATE_READY);
    /* no need to take extra ref, we already have one
     * and the bin will add one since it is no longer floating,
     * as it was at least added once before (below) */
    gst_bin_add (GST_BIN_CAST (playbin), uridecodebin);
  } else {
    GST_DEBUG_OBJECT (playbin, "making new uridecodebin");
    uridecodebin = gst_element_factory_make ("uridecodebin", NULL);
    if (!uridecodebin)
      goto no_decodebin;
    gst_bin_add (GST_BIN_CAST (playbin), uridecodebin);
    group->uridecodebin = gst_object_ref (uridecodebin);
  }

  flags = gst_play_sink_get_flags (playbin->playsink);

  g_object_set (uridecodebin,
      /* configure connection speed */
      "connection-speed", playbin->connection_speed / 1000,
      /* configure uri */
      "uri", group->uri,
      /* configure download buffering */
      "download", ((flags & GST_PLAY_FLAG_DOWNLOAD) != 0),
      /* configure buffering of demuxed/parsed data */
      "use-buffering", ((flags & GST_PLAY_FLAG_BUFFERING) != 0),
      /* configure buffering parameters */
      "buffer-duration", playbin->buffer_duration,
      "buffer-size", playbin->buffer_size,
      "ring-buffer-max-size", playbin->ring_buffer_max_size, NULL);

  /* connect pads and other things */
  group->pad_added_id = g_signal_connect (uridecodebin, "pad-added",
      G_CALLBACK (pad_added_cb), group);
  group->pad_removed_id = g_signal_connect (uridecodebin, "pad-removed",
      G_CALLBACK (pad_removed_cb), group);
  group->no_more_pads_id = g_signal_connect (uridecodebin, "no-more-pads",
      G_CALLBACK (no_more_pads_cb), group);
  group->notify_source_id = g_signal_connect (uridecodebin, "notify::source",
      G_CALLBACK (notify_source_cb), group);

  /* we have 1 pending no-more-pads */
  group->pending = 1;

  /* is called when the uridecodebin is out of data and we can switch to the
   * next uri */
  group->drained_id =
      g_signal_connect (uridecodebin, "drained", G_CALLBACK (drained_cb),
      group);

  /* will be called when a new media type is found. We return a list of decoders
   * including sinks for decodebin to try */
  group->autoplug_factories_id =
      g_signal_connect (uridecodebin, "autoplug-factories",
      G_CALLBACK (autoplug_factories_cb), group);
  group->autoplug_select_id =
      g_signal_connect (uridecodebin, "autoplug-select",
      G_CALLBACK (autoplug_select_cb), group);
  group->autoplug_continue_id =
      g_signal_connect (uridecodebin, "autoplug-continue",
      G_CALLBACK (autoplug_continue_cb), group);

  if (group->suburi) {
    /* subtitles */
    if (group->suburidecodebin) {
      GST_DEBUG_OBJECT (playbin, "reusing existing suburidecodebin");
      suburidecodebin = group->suburidecodebin;
      gst_element_set_state (suburidecodebin, GST_STATE_READY);
      /* no need to take extra ref, we already have one
       * and the bin will add one since it is no longer floating,
       * as it was at least added once before (below) */
      gst_bin_add (GST_BIN_CAST (playbin), suburidecodebin);
    } else {
      GST_DEBUG_OBJECT (playbin, "making new suburidecodebin");
      suburidecodebin = gst_element_factory_make ("uridecodebin", NULL);
      if (!suburidecodebin)
        goto no_decodebin;

      gst_bin_add (GST_BIN_CAST (playbin), suburidecodebin);
      group->suburidecodebin = gst_object_ref (suburidecodebin);
    }

    g_object_set (suburidecodebin,
        /* configure connection speed */
        "connection-speed", playbin->connection_speed,
        /* configure uri */
        "uri", group->suburi, NULL);

    /* connect pads and other things */
    group->sub_pad_added_id = g_signal_connect (suburidecodebin, "pad-added",
        G_CALLBACK (pad_added_cb), group);
    group->sub_pad_removed_id = g_signal_connect (suburidecodebin,
        "pad-removed", G_CALLBACK (pad_removed_cb), group);
    group->sub_no_more_pads_id = g_signal_connect (suburidecodebin,
        "no-more-pads", G_CALLBACK (no_more_pads_cb), group);

    group->sub_autoplug_continue_id =
        g_signal_connect (suburidecodebin, "autoplug-continue",
        G_CALLBACK (autoplug_continue_cb), group);

    /* we have 2 pending no-more-pads */
    group->pending = 2;
    group->sub_pending = TRUE;
  } else {
    group->sub_pending = FALSE;
  }

  /* release the group lock before setting the state of the decodebins, they
   * might fire signals in this thread that we need to handle with the
   * group_lock taken. */
  GST_SOURCE_GROUP_UNLOCK (group);

  if (suburidecodebin) {
    if (gst_element_set_state (suburidecodebin,
            target) == GST_STATE_CHANGE_FAILURE) {
      GST_DEBUG_OBJECT (playbin,
          "failed state change of subtitle uridecodebin");
      GST_SOURCE_GROUP_LOCK (group);

      REMOVE_SIGNAL (group->suburidecodebin, group->sub_pad_added_id);
      REMOVE_SIGNAL (group->suburidecodebin, group->sub_pad_removed_id);
      REMOVE_SIGNAL (group->suburidecodebin, group->sub_no_more_pads_id);
      REMOVE_SIGNAL (group->suburidecodebin, group->sub_autoplug_continue_id);
      /* Might already be removed because of an error message */
      if (GST_OBJECT_PARENT (suburidecodebin) == GST_OBJECT_CAST (playbin))
        gst_bin_remove (GST_BIN_CAST (playbin), suburidecodebin);
      if (group->sub_pending) {
        group->pending--;
        group->sub_pending = FALSE;
      }
      gst_element_set_state (suburidecodebin, GST_STATE_READY);
      GST_SOURCE_GROUP_UNLOCK (group);
    }
  }
  if (gst_element_set_state (uridecodebin, target) == GST_STATE_CHANGE_FAILURE)
    goto uridecodebin_failure;

  GST_SOURCE_GROUP_LOCK (group);
  /* alow state changes of the playbin affect the group elements now */
  group_set_locked_state_unlocked (playbin, group, FALSE);
  group->active = TRUE;
  GST_SOURCE_GROUP_UNLOCK (group);

  return TRUE;

  /* ERRORS */
no_decodebin:
  {
    GstMessage *msg;

    /* delete any custom sinks we might have */
    if (group->audio_sink) {
      /* If this is a automatically created sink set it to NULL */
      if (group->audio_sink != playbin->audio_sink)
        gst_element_set_state (group->audio_sink, GST_STATE_NULL);
      gst_object_unref (group->audio_sink);
    }
    group->audio_sink = NULL;
    if (group->video_sink) {
      /* If this is a automatically created sink set it to NULL */
      if (group->video_sink != playbin->video_sink)
        gst_element_set_state (group->video_sink, GST_STATE_NULL);
      gst_object_unref (group->video_sink);
    }
    group->video_sink = NULL;

    GST_SOURCE_GROUP_UNLOCK (group);
    msg =
        gst_missing_element_message_new (GST_ELEMENT_CAST (playbin),
        "uridecodebin");
    gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);

    GST_ELEMENT_ERROR (playbin, CORE, MISSING_PLUGIN,
        (_("Could not create \"uridecodebin\" element.")), (NULL));
    return FALSE;
  }
uridecodebin_failure:
  {
    /* delete any custom sinks we might have */
    if (group->audio_sink) {
      /* If this is a automatically created sink set it to NULL */
      if (group->audio_sink != playbin->audio_sink)
        gst_element_set_state (group->audio_sink, GST_STATE_NULL);
      gst_object_unref (group->audio_sink);
    }
    group->audio_sink = NULL;
    if (group->video_sink) {
      /* If this is a automatically created sink set it to NULL */
      if (group->video_sink != playbin->video_sink)
        gst_element_set_state (group->video_sink, GST_STATE_NULL);
      gst_object_unref (group->video_sink);
    }
    group->video_sink = NULL;

    gst_bin_remove (GST_BIN_CAST (playbin), uridecodebin);

    GST_DEBUG_OBJECT (playbin, "failed state change of uridecodebin");
    return FALSE;
  }
}

/* unlink a group of uridecodebins from the sink.
 * must be called with PLAY_BIN_LOCK */
static gboolean
deactivate_group (GstPlayBin * playbin, GstSourceGroup * group)
{
  gint i;

  g_return_val_if_fail (group->valid, FALSE);
  g_return_val_if_fail (group->active, FALSE);

  GST_DEBUG_OBJECT (playbin, "unlinking group %p", group);

  GST_SOURCE_GROUP_LOCK (group);
  group->active = FALSE;
  for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
    GstSourceSelect *select = &group->selector[i];

    GST_DEBUG_OBJECT (playbin, "unlinking selector %s", select->media_list[0]);

    if (select->srcpad) {
      if (select->sinkpad) {
        GST_LOG_OBJECT (playbin, "unlinking from sink");
        gst_pad_unlink (select->srcpad, select->sinkpad);

        /* release back */
        GST_LOG_OBJECT (playbin, "release sink pad");
        gst_play_sink_release_pad (playbin->playsink, select->sinkpad);
        select->sinkpad = NULL;
      }

      gst_object_unref (select->srcpad);
      select->srcpad = NULL;
    }

    if (select->selector) {
      gint n;

      /* release and unref requests pad from the selector */
      for (n = 0; n < select->channels->len; n++) {
        GstPad *sinkpad = g_ptr_array_index (select->channels, n);

        gst_element_release_request_pad (select->selector, sinkpad);
        gst_object_unref (sinkpad);
      }
      g_ptr_array_set_size (select->channels, 0);

      gst_element_set_state (select->selector, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (playbin), select->selector);
      select->selector = NULL;
    }
  }
  /* delete any custom sinks we might have */
  if (group->audio_sink) {
    /* If this is a automatically created sink set it to NULL */
    if (group->audio_sink != playbin->audio_sink)
      gst_element_set_state (group->audio_sink, GST_STATE_NULL);
    gst_object_unref (group->audio_sink);
  }
  group->audio_sink = NULL;
  if (group->video_sink) {
    /* If this is a automatically created sink set it to NULL */
    if (group->video_sink != playbin->video_sink)
      gst_element_set_state (group->video_sink, GST_STATE_NULL);
    gst_object_unref (group->video_sink);
  }
  group->video_sink = NULL;

  if (group->uridecodebin) {
    REMOVE_SIGNAL (group->uridecodebin, group->pad_added_id);
    REMOVE_SIGNAL (group->uridecodebin, group->pad_removed_id);
    REMOVE_SIGNAL (group->uridecodebin, group->no_more_pads_id);
    REMOVE_SIGNAL (group->uridecodebin, group->notify_source_id);
    REMOVE_SIGNAL (group->uridecodebin, group->drained_id);
    REMOVE_SIGNAL (group->uridecodebin, group->autoplug_factories_id);
    REMOVE_SIGNAL (group->uridecodebin, group->autoplug_select_id);
    REMOVE_SIGNAL (group->uridecodebin, group->autoplug_continue_id);
    gst_bin_remove (GST_BIN_CAST (playbin), group->uridecodebin);
  }

  if (group->suburidecodebin) {
    REMOVE_SIGNAL (group->suburidecodebin, group->sub_pad_added_id);
    REMOVE_SIGNAL (group->suburidecodebin, group->sub_pad_removed_id);
    REMOVE_SIGNAL (group->suburidecodebin, group->sub_no_more_pads_id);
    REMOVE_SIGNAL (group->suburidecodebin, group->sub_autoplug_continue_id);

    /* Might already be removed because of errors */
    if (GST_OBJECT_PARENT (group->suburidecodebin) == GST_OBJECT_CAST (playbin))
      gst_bin_remove (GST_BIN_CAST (playbin), group->suburidecodebin);
  }

  GST_SOURCE_GROUP_UNLOCK (group);

  return TRUE;
}

/* setup the next group to play, this assumes the next_group is valid and
 * configured. It swaps out the current_group and activates the valid
 * next_group. */
static gboolean
setup_next_source (GstPlayBin * playbin, GstState target)
{
  GstSourceGroup *new_group, *old_group;

  GST_DEBUG_OBJECT (playbin, "setup sources");

  /* see if there is a next group */
  GST_PLAY_BIN_LOCK (playbin);
  new_group = playbin->next_group;
  if (!new_group || !new_group->valid)
    goto no_next_group;

  /* first unlink the current source, if any */
  old_group = playbin->curr_group;
  if (old_group && old_group->valid && old_group->active) {
    gst_play_bin_update_cached_duration (playbin);
    /* unlink our pads with the sink */
    deactivate_group (playbin, old_group);
    old_group->valid = FALSE;
  }

  /* swap old and new */
  playbin->curr_group = new_group;
  playbin->next_group = old_group;

  /* activate the new group */
  if (!activate_group (playbin, new_group, target))
    goto activate_failed;

  GST_PLAY_BIN_UNLOCK (playbin);

  return TRUE;

  /* ERRORS */
no_next_group:
  {
    GST_DEBUG_OBJECT (playbin, "no next group");
    if (target == GST_STATE_READY && new_group && new_group->uri == NULL)
      GST_ELEMENT_ERROR (playbin, RESOURCE, NOT_FOUND, ("No URI set"), (NULL));
    GST_PLAY_BIN_UNLOCK (playbin);
    return FALSE;
  }
activate_failed:
  {
    GST_DEBUG_OBJECT (playbin, "activate failed");
    GST_PLAY_BIN_UNLOCK (playbin);
    return FALSE;
  }
}

/* The group that is currently playing is copied again to the
 * next_group so that it will start playing the next time.
 */
static gboolean
save_current_group (GstPlayBin * playbin)
{
  GstSourceGroup *curr_group;

  GST_DEBUG_OBJECT (playbin, "save current group");

  /* see if there is a current group */
  GST_PLAY_BIN_LOCK (playbin);
  curr_group = playbin->curr_group;
  if (curr_group && curr_group->valid && curr_group->active) {
    /* unlink our pads with the sink */
    deactivate_group (playbin, curr_group);
  }
  /* swap old and new */
  playbin->curr_group = playbin->next_group;
  playbin->next_group = curr_group;
  GST_PLAY_BIN_UNLOCK (playbin);

  return TRUE;
}

/* clear the locked state from all groups. This function is called before a
 * state change to NULL is performed on them. */
static gboolean
groups_set_locked_state (GstPlayBin * playbin, gboolean locked)
{
  GST_DEBUG_OBJECT (playbin, "setting locked state to %d on all groups",
      locked);

  GST_PLAY_BIN_LOCK (playbin);
  GST_SOURCE_GROUP_LOCK (playbin->curr_group);
  group_set_locked_state_unlocked (playbin, playbin->curr_group, locked);
  GST_SOURCE_GROUP_UNLOCK (playbin->curr_group);
  GST_SOURCE_GROUP_LOCK (playbin->next_group);
  group_set_locked_state_unlocked (playbin, playbin->next_group, locked);
  GST_SOURCE_GROUP_UNLOCK (playbin->next_group);
  GST_PLAY_BIN_UNLOCK (playbin);

  return TRUE;
}

static GstStateChangeReturn
gst_play_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlayBin *playbin;
  gboolean do_save = FALSE;

  playbin = GST_PLAY_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      memset (&playbin->duration, 0, sizeof (playbin->duration));
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LOG_OBJECT (playbin, "clearing shutdown flag");
      memset (&playbin->duration, 0, sizeof (playbin->duration));
      g_atomic_int_set (&playbin->shutdown, 0);

      if (!setup_next_source (playbin, GST_STATE_READY)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto failure;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    async_down:
      /* FIXME unlock our waiting groups */
      GST_LOG_OBJECT (playbin, "setting shutdown flag");
      g_atomic_int_set (&playbin->shutdown, 1);
      memset (&playbin->duration, 0, sizeof (playbin->duration));

      /* wait for all callbacks to end by taking the lock.
       * No dynamic (critical) new callbacks will
       * be able to happen as we set the shutdown flag. */
      GST_PLAY_BIN_DYN_LOCK (playbin);
      GST_LOG_OBJECT (playbin, "dynamic lock taken, we can continue shutdown");
      GST_PLAY_BIN_DYN_UNLOCK (playbin);
      if (!do_save)
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* we go async to PAUSED, so if that fails, we never make it to PAUSED
       * an no state change PAUSED to READY passes here,
       * though it is a nice-to-have ... */
      if (!g_atomic_int_get (&playbin->shutdown)) {
        do_save = TRUE;
        goto async_down;
      }
      memset (&playbin->duration, 0, sizeof (playbin->duration));

      /* unlock so that all groups go to NULL */
      groups_set_locked_state (playbin, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto failure;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* FIXME Release audio device when we implement that */
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      save_current_group (playbin);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
      guint i;

      /* also do missed state change down to READY */
      if (do_save)
        save_current_group (playbin);
      /* Deactive the groups, set the uridecodebins to NULL
       * and unref them.
       */
      for (i = 0; i < 2; i++) {
        if (playbin->groups[i].active && playbin->groups[i].valid) {
          deactivate_group (playbin, &playbin->groups[i]);
          playbin->groups[i].valid = FALSE;
        }

        if (playbin->groups[i].uridecodebin) {
          gst_element_set_state (playbin->groups[i].uridecodebin,
              GST_STATE_NULL);
          gst_object_unref (playbin->groups[i].uridecodebin);
          playbin->groups[i].uridecodebin = NULL;
        }

        if (playbin->groups[i].suburidecodebin) {
          gst_element_set_state (playbin->groups[i].suburidecodebin,
              GST_STATE_NULL);
          gst_object_unref (playbin->groups[i].suburidecodebin);
          playbin->groups[i].suburidecodebin = NULL;
        }
      }

      /* Set our sinks back to NULL, they might not be child of playbin */
      if (playbin->audio_sink)
        gst_element_set_state (playbin->audio_sink, GST_STATE_NULL);
      if (playbin->video_sink)
        gst_element_set_state (playbin->video_sink, GST_STATE_NULL);
      if (playbin->text_sink)
        gst_element_set_state (playbin->text_sink, GST_STATE_NULL);

      /* make sure the groups don't perform a state change anymore until we
       * enable them again */
      groups_set_locked_state (playbin, TRUE);
      break;
    }
    default:
      break;
  }

  return ret;

  /* ERRORS */
failure:
  {
    if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
      GstSourceGroup *curr_group;

      curr_group = playbin->curr_group;
      if (curr_group && curr_group->active && curr_group->valid) {
        /* unlink our pads with the sink */
        deactivate_group (playbin, curr_group);
        curr_group->valid = FALSE;
      }

      /* Swap current and next group back */
      playbin->curr_group = playbin->next_group;
      playbin->next_group = curr_group;
    }
    return ret;
  }
}

static void
gst_play_bin_overlay_expose (GstVideoOverlay * overlay)
{
  GstPlayBin *playbin = GST_PLAY_BIN (overlay);

  gst_video_overlay_expose (GST_VIDEO_OVERLAY (playbin->playsink));
}

static void
gst_play_bin_overlay_handle_events (GstVideoOverlay * overlay,
    gboolean handle_events)
{
  GstPlayBin *playbin = GST_PLAY_BIN (overlay);

  gst_video_overlay_handle_events (GST_VIDEO_OVERLAY (playbin->playsink),
      handle_events);
}

static void
gst_play_bin_overlay_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
{
  GstPlayBin *playbin = GST_PLAY_BIN (overlay);

  gst_video_overlay_set_render_rectangle (GST_VIDEO_OVERLAY (playbin->playsink),
      x, y, width, height);
}

static void
gst_play_bin_overlay_set_window_handle (GstVideoOverlay * overlay,
    guintptr handle)
{
  GstPlayBin *playbin = GST_PLAY_BIN (overlay);

  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (playbin->playsink),
      handle);
}

static void
gst_play_bin_overlay_init (gpointer g_iface, gpointer g_iface_data)
{
  GstVideoOverlayInterface *iface = (GstVideoOverlayInterface *) g_iface;
  iface->expose = gst_play_bin_overlay_expose;
  iface->handle_events = gst_play_bin_overlay_handle_events;
  iface->set_render_rectangle = gst_play_bin_overlay_set_render_rectangle;
  iface->set_window_handle = gst_play_bin_overlay_set_window_handle;
}

static void
gst_play_bin_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstPlayBin *playbin = GST_PLAY_BIN (navigation);

  gst_navigation_send_event (GST_NAVIGATION (playbin->playsink), structure);
}

static void
gst_play_bin_navigation_init (gpointer g_iface, gpointer g_iface_data)
{
  GstNavigationInterface *iface = (GstNavigationInterface *) g_iface;

  iface->send_event = gst_play_bin_navigation_send_event;
}

static const GList *
gst_play_bin_colorbalance_list_channels (GstColorBalance * balance)
{
  GstPlayBin *playbin = GST_PLAY_BIN (balance);

  return
      gst_color_balance_list_channels (GST_COLOR_BALANCE (playbin->playsink));
}

static void
gst_play_bin_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstPlayBin *playbin = GST_PLAY_BIN (balance);

  gst_color_balance_set_value (GST_COLOR_BALANCE (playbin->playsink), channel,
      value);
}

static gint
gst_play_bin_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstPlayBin *playbin = GST_PLAY_BIN (balance);

  return gst_color_balance_get_value (GST_COLOR_BALANCE (playbin->playsink),
      channel);
}

static GstColorBalanceType
gst_play_bin_colorbalance_get_balance_type (GstColorBalance * balance)
{
  GstPlayBin *playbin = GST_PLAY_BIN (balance);

  return
      gst_color_balance_get_balance_type (GST_COLOR_BALANCE
      (playbin->playsink));
}

static void
gst_play_bin_colorbalance_init (gpointer g_iface, gpointer g_iface_data)
{
  GstColorBalanceInterface *iface = (GstColorBalanceInterface *) g_iface;

  iface->list_channels = gst_play_bin_colorbalance_list_channels;
  iface->set_value = gst_play_bin_colorbalance_set_value;
  iface->get_value = gst_play_bin_colorbalance_get_value;
  iface->get_balance_type = gst_play_bin_colorbalance_get_balance_type;
}

gboolean
gst_play_bin2_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_play_bin_debug, "playbin", 0, "play bin");

  return gst_element_register (plugin, "playbin", GST_RANK_NONE,
      GST_TYPE_PLAY_BIN);
}
