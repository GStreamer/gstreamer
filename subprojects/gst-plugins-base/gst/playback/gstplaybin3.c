/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) <2011> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2013> Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2015> Jan Schmidt <jan@centricular.com>
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
 * SECTION:element-playbin3
 * @title: playbin3
 *
 * playbin3 provides a stand-alone everything-in-one abstraction for an
 * audio and/or video player. It differs from the previous playbin (playbin2)
 * by supporting publication and selection of available streams via the
 * #GstStreamCollection message and #GST_EVENT_SELECT_STREAMS event API.
 *
 * > playbin3 is still experimental API and a technology preview.
 * > Its behaviour and exposed API is subject to change.
 *
 * playbin3 can handle both audio and video files and features
 *
 * * automatic file type recognition and based on that automatic
 * selection and usage of the right audio/video/subtitle demuxers/decoders
 *
 * * auxiliary files - such as external subtitles and audio tracks
 * * visualisations for audio files
 * * subtitle support for video files. Subtitles can be store in external
 *   files.
 * * stream selection between different video/audio/subtitles streams
 * * meta info (tag) extraction
 * * easy access to the last video sample
 * * buffering when playing streams over a network
 * * volume control with mute option
 *
 * ## Usage
 *
 * A playbin element can be created just like any other element using
 * gst_element_factory_make(). The file/URI to play should be set via the #GstPlayBin3:uri
 * property. This must be an absolute URI, relative file paths are not allowed.
 * Example URIs are file:///home/joe/movie.avi or http://www.joedoe.com/foo.ogg
 *
 * Playbin3 is a #GstPipeline. It will notify the application of everything
 * that's happening (errors, end of stream, tags found, state changes, etc.)
 * by posting messages on its #GstBus. The application needs to watch the
 * bus.
 *
 * Playback can be initiated by setting the element to PLAYING state using
 * gst_element_set_state(). Note that the state change will take place in
 * the background in a separate thread, when the function returns playback
 * is probably not happening yet and any errors might not have occurred yet.
 * Applications using playbin3 should ideally be written to deal with things
 * completely asynchroneous.
 *
 * When playback has finished (an EOS message has been received on the bus)
 * or an error has occurred (an ERROR message has been received on the bus) or
 * the user wants to play a different track, playbin3 should be set back to
 * READY or NULL state, then the #GstPlayBin3:uri property should be set to the
 * new location and then playbin3 be set to PLAYING state again.
 *
 * Seeking can be done using gst_element_seek_simple() or gst_element_seek()
 * on the playbin3 element. Again, the seek will not be executed
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
 *
 * ## Selecting streams
 *
 * The recommended way to select streams (instead of the default selection) is
 * to listen to GST_MESSAGE_STREAM_COLLECTION messages on the GstBus and send a
 * GST_EVENT_SELECT_STREAMS on the pipeline with the selected streams. This
 * provides more information and flexibility compared to the legacy #GstPlayBin
 * property and signal-based mechanism.
 *
 * Note: The application should not assume that collections will not change
 * throughout a single file. If it wishes to modify the default selection, it
 * should always respond to new collections posted on the bus with a
 * GST_EVENT_SELECT_STREAMS.
 *
 * ## Advanced Usage: specifying the audio and video sink
 *
 * By default, if no audio sink or video sink has been specified via the
 * #GstPlayBin3:audio-sink or #GstPlayBin3:video-sink property, playbin3 will use the autoaudiosink
 * and autovideosink elements to find the first-best available output method.
 * This should work in most cases, but is not always desirable. Often either
 * the user or application might want to specify more explicitly what to use
 * for audio and video output.
 *
 * If the application wants more control over how audio or video should be
 * output, it may create the audio/video sink elements itself (for example
 * using gst_element_factory_make()) and provide them to playbin3 using the
 * #GstPlayBin3:audio-sink or #GstPlayBin3:video-sink property.
 *
 * GNOME-based applications, for example, will usually want to create
 * gconfaudiosink and gconfvideosink elements and make playbin3 use those,
 * so that output happens to whatever the user has configured in the GNOME
 * Multimedia System Selector configuration dialog.
 *
 * The sink elements do not necessarily need to be ready-made sinks. It is
 * possible to create container elements that look like a sink to playbin3,
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
 *
 * ## Retrieving Tags and Other Meta Data
 *
 * Most of the common meta data (artist, title, etc.) can be retrieved by
 * watching for TAG messages on the pipeline's bus (see above).
 *
 * Other more specific meta information like width/height/framerate of video
 * streams or samplerate/number of channels of audio streams can be obtained
 * from the negotiated caps on the sink pads of the sinks.
 *
 * ## Buffering
 * Playbin3 handles buffering automatically for the most part, but applications
 * need to handle parts of the buffering process as well. Whenever playbin3 is
 * buffering, it will post BUFFERING messages on the bus with a percentage
 * value that shows the progress of the buffering process. Applications need
 * to set playbin3 to PLAYING or PAUSED state in response to these messages.
 * They may also want to convey the buffering progress to the user in some
 * way. Here is how to extract the percentage information from the message:
 * |[
 * switch (GST_MESSAGE_TYPE (msg)) {
 *   case GST_MESSAGE_BUFFERING: {
 *     gint percent = 0;
 *     gst_message_parse_buffering (msg, &percent);
 *     g_print ("Buffering (%u percent done)", percent);
 *     break;
 *   }
 *   ...
 * }
 * ]|
 *
 * Note that applications should keep/set the pipeline in the PAUSED state when
 * a BUFFERING message is received with a buffer percent value < 100 and set
 * the pipeline back to PLAYING state when a BUFFERING message with a value
 * of 100 percent is received (if PLAYING is the desired state, that is).
 *
 * ## Embedding the video window in your application
 * By default, playbin3 (or rather the video sinks used) will create their own
 * window. Applications will usually want to force output to a window of their
 * own, however. This can be done using the #GstVideoOverlay interface, which most
 * video sinks implement. See the documentation there for more details.
 *
 * ## Specifying which CD/DVD device to use
 *
 * The device to use for CDs/DVDs needs to be set on the source element playbin3
 * creates before it is opened. The most generic way of doing this is to connect
 * to playbin3's "source-setup" signal, which will be emitted by playbin3 when
 * it has created the source element for a particular URI. In the signal
 * callback you can check if the source element has a "device" property and set
 * it appropriately. In some cases the device can also be set as part of the
 * URI, but it depends on the elements involved if this will work or not. For
 * example, for DVD menu playback, the following syntax might work (if the
 * resindvd plugin is used): dvd://[/path/to/device]
 *
 * ## Handling redirects
 *
 * Some elements may post 'redirect' messages on the bus to tell the
 * application to open another location. These are element messages containing
 * a structure named 'redirect' along with a 'new-location' field of string
 * type. The new location may be a relative or an absolute URI. Examples
 * for such redirects can be found in many quicktime movie trailers.
 *
 * ## Examples
 * |[
 * gst-launch-1.0 -v playbin3 uri=file:///path/to/somefile.mp4
 * ]|
 *  This will play back the given AVI video file, given that the video and
 * audio decoders required to decode the content are installed. Since no
 * special audio sink or video sink is supplied (via playbin3's audio-sink or
 * video-sink properties) playbin3 will try to find a suitable audio and
 * video sink automatically using the autoaudiosink and autovideosink elements.
 * |[
 * gst-launch-1.0 -v playbin3 uri=cdda://4
 * ]|
 *  This will play back track 4 on an audio CD in your disc drive (assuming
 * the drive is detected automatically by the plugin).
 * |[
 * gst-launch-1.0 -v playbin3 uri=dvd://
 * ]|
 *  This will play back the DVD in your disc drive (assuming
 * the drive is detected automatically by the plugin).
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>

#include <glib/gi18n-lib.h>
#include <gst/pbutils/pbutils.h>
#include <gst/audio/streamvolume.h>
#include <gst/video/video-info.h>
#include <gst/video/video-multiview.h>
#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>
#include <gst/video/colorbalance.h>
#include "gstplay-enum.h"
#include "gstplaybackelements.h"
#include "gstplaysink.h"
#include "gstsubtitleoverlay.h"
#include "gstplaybackutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_bin3_debug);
#define GST_CAT_DEFAULT gst_play_bin3_debug

#define GST_TYPE_PLAY_BIN               (gst_play_bin3_get_type())
#define GST_PLAY_BIN3(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BIN,GstPlayBin3))
#define GST_PLAY_BIN3_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BIN,GstPlayBin3Class))
#define GST_IS_PLAY_BIN(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BIN))
#define GST_IS_PLAY_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BIN))

#define ULONG_TO_POINTER(number)        ((gpointer) (guintptr) (number))
#define POINTER_TO_ULONG(number)        ((guintptr) (number))

#define VOLUME_MAX_DOUBLE 10.0

typedef struct _GstPlayBin3 GstPlayBin3;
typedef struct _GstPlayBin3Class GstPlayBin3Class;
typedef struct _GstSourceGroup GstSourceGroup;
typedef struct _GstSourceCombine GstSourceCombine;
typedef struct _SourcePad SourcePad;

/* GstSourceCombine controls all the information regarding a certain
 * media type.
 *
 * It can control a custom combiner element (by default none)
 */
struct _GstSourceCombine
{
  GstStreamType stream_type;    /* The GstStreamType of the combiner */

  GstElement *combiner;         /* the combiner */
  GPtrArray *inputpads;         /* Array of sink request GstPad of the combiner */

  GstPad *srcpad;               /* the source pad of the combiner */
  GstPad *sinkpad;              /* the sinkpad of the sink when the combiner
                                 * is linked */

  GPtrArray *streams;           /* Sorted array of GstStream for the given type */

  gboolean has_active_pad;      /* stream combiner has the "active-pad" property */

  gboolean is_concat;           /* The stream combiner is the 'concat' element */
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

/* names matching the enum above */
static const gchar *stream_type_names[] = {
  "audio", "video", "text"
};


#define STREAM_TYPES_FORMAT "s%s%s"
#define STREAM_TYPES_ARGS(s) (s) & GST_STREAM_TYPE_AUDIO ? "audio " : "", \
    (s) & GST_STREAM_TYPE_VIDEO ? "video " : "",			\
    (s) & GST_STREAM_TYPE_TEXT ? "text " : ""


/* a structure to hold information about a uridecodebin pad */
struct _SourcePad
{
  GstPad *pad;                  /* The controlled pad */
  GstStreamType stream_type;    /* stream type of the controlled pad */
  GstPad *combine_sinkpad;      /* Combiner request sinkpad linked to pad */
  gulong event_probe_id;
};

/* a structure to hold the objects for decoding a uri and the subtitle uri
 */
struct _GstSourceGroup
{
  GstPlayBin3 *playbin;

  GMutex lock;

  gboolean valid;               /* the group has valid info to start playback */
  gboolean active;              /* the group is active */

  gboolean playing;             /* the group is currently playing
                                 * (outputted on the sinks) */

  /* properties */
  gchar *uri;
  gchar *suburi;

  /* The currently outputted group_id */
  guint group_id;

  /* Bit-wise set of stream types we have requested from uridecodebin3 */
  GstStreamType selected_stream_types;

  /* Bit-wise set of stream types for which pads are present */
  GstStreamType present_stream_types;

  /* TRUE if a 'about-to-finish' needs to be posted once we have
   * got source pads for all requested stream types
   *
   * FIXME : Move this logic to uridecodebin3 later */
  gboolean pending_about_to_finish;

  /* uridecodebin to handle uri and suburi */
  GstElement *uridecodebin;

  /* Active sinks for each media type. These are initialized with
   * the configured or currently used sink, otherwise
   * left as NULL and playbin tries to automatically
   * select a good sink */
  GstElement *audio_sink;
  GstElement *video_sink;
  GstElement *text_sink;

  /* List of source pads */
  GList *source_pads;

  /* uridecodebin signals */
  gulong pad_added_id;
  gulong pad_removed_id;
  gulong select_stream_id;
  gulong source_setup_id;
  gulong about_to_finish_id;

  gboolean stream_changed_pending;

  /* Active stream collection */
  GstStreamCollection *collection;


  /* buffering message stored for after switching */
  GstMessage *pending_buffering_msg;
};

#define GST_PLAY_BIN3_GET_LOCK(bin) (&((GstPlayBin3*)(bin))->lock)
#define GST_PLAY_BIN3_LOCK(bin) (g_rec_mutex_lock (GST_PLAY_BIN3_GET_LOCK(bin)))
#define GST_PLAY_BIN3_UNLOCK(bin) (g_rec_mutex_unlock (GST_PLAY_BIN3_GET_LOCK(bin)))

/* lock to protect dynamic callbacks, like no-more-pads */
#define GST_PLAY_BIN3_DYN_LOCK(bin)    g_mutex_lock (&(bin)->dyn_lock)
#define GST_PLAY_BIN3_DYN_UNLOCK(bin)  g_mutex_unlock (&(bin)->dyn_lock)

/* lock for shutdown */
#define GST_PLAY_BIN3_SHUTDOWN_LOCK(bin,label)			\
  G_STMT_START {						\
    if (G_UNLIKELY (g_atomic_int_get (&bin->shutdown)))		\
      goto label;						\
    GST_PLAY_BIN3_DYN_LOCK (bin);				\
    if (G_UNLIKELY (g_atomic_int_get (&bin->shutdown))) {	\
      GST_PLAY_BIN3_DYN_UNLOCK (bin);				\
      goto label;						\
    }								\
  } G_STMT_END

/* unlock for shutdown */
#define GST_PLAY_BIN3_SHUTDOWN_UNLOCK(bin)	\
  GST_PLAY_BIN3_DYN_UNLOCK (bin);		\

/**
 * GstPlayBin3:
 *
 * playbin element structure
 */
struct _GstPlayBin3
{
  GstPipeline parent;

  GRecMutex lock;               /* to protect group switching */

  /* the input groups, we use a double buffer to switch between current and next */
  GstSourceGroup groups[2];     /* array with group info */
  GstSourceGroup *curr_group;   /* pointer to the currently playing group */
  GstSourceGroup *next_group;   /* pointer to the next group */

  /* combiners for different streams */
  GstSourceCombine combiner[PLAYBIN_STREAM_LAST];

  /* Bit-wise set of stream types we have requested from uridecodebin3.
   * Calculated as the combination of the 'selected_stream_types' of
   * each sourcegroup */
  GstStreamType selected_stream_types;

  /* Bit-wise set of configured output stream types (i.e. active
     playsink inputs and combiners) */
  GstStreamType active_stream_types;

  /* properties */
  guint64 connection_speed;     /* connection speed in bits/sec (0 = unknown) */
  gint current_video;           /* the currently selected stream */
  gint current_audio;           /* the currently selected stream */
  gint current_text;            /* the currently selected stream */

  gboolean do_stream_selections;        /* Set to TRUE when any of current-{video|audio|text} are set to
                                           say playbin should do backwards-compatibility behaviours */

  guint64 buffer_duration;      /* When buffering, the max buffer duration (ns) */
  guint buffer_size;            /* When buffering, the max buffer size (bytes) */
  gboolean force_aspect_ratio;

  /* Multiview/stereoscopic overrides */
  GstVideoMultiviewFramePacking multiview_mode;
  GstVideoMultiviewFlags multiview_flags;

  /* our play sink */
  GstPlaySink *playsink;

  /* Task for (de)activating groups, protected by the activation lock */
  GstTask *activation_task;
  GRecMutex activation_lock;

  /* lock protecting dynamic adding/removing */
  GMutex dyn_lock;
  /* if we are shutting down or not */
  gint shutdown;
  gboolean async_pending;       /* async-start has been emitted */

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

  GstElement *audio_stream_combiner;    /* configured audio stream combiner, or NULL */
  GstElement *video_stream_combiner;    /* configured video stream combiner, or NULL */
  GstElement *text_stream_combiner;     /* configured text stream combiner, or NULL */

  guint64 ring_buffer_max_size; /* 0 means disabled */

  gboolean is_live;             /* Whether our current group is live */

  GMutex buffering_post_lock;   /* Protect serialisation of buffering messages. Must not acquire this while holding any SOURCE_GROUP lock */
};

struct _GstPlayBin3Class
{
  GstPipelineClass parent_class;

  /* notify app that the current uri finished decoding and it is possible to
   * queue a new one for gapless playback */
  void (*about_to_finish) (GstPlayBin3 * playbin);

  /* get the last video sample and convert it to the given caps */
  GstSample *(*convert_sample) (GstPlayBin3 * playbin, GstCaps * caps);
};

/* props */
#define DEFAULT_URI               NULL
#define DEFAULT_SUBURI            NULL
#define DEFAULT_FLAGS             GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_TEXT | \
                                  GST_PLAY_FLAG_SOFT_VOLUME | GST_PLAY_FLAG_DEINTERLACE | \
                                  GST_PLAY_FLAG_SOFT_COLORBALANCE | GST_PLAY_FLAG_BUFFERING
#define DEFAULT_CURRENT_VIDEO     -1
#define DEFAULT_CURRENT_AUDIO     -1
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
  PROP_FLAGS,
  PROP_SUBTITLE_ENCODING,
  PROP_AUDIO_SINK,
  PROP_VIDEO_SINK,
  PROP_VIS_PLUGIN,
  PROP_TEXT_SINK,
  PROP_VIDEO_STREAM_COMBINER,
  PROP_AUDIO_STREAM_COMBINER,
  PROP_TEXT_STREAM_COMBINER,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_SAMPLE,
  PROP_FONT_DESC,
  PROP_CONNECTION_SPEED,
  PROP_BUFFER_SIZE,
  PROP_BUFFER_DURATION,
  PROP_AV_OFFSET,
  PROP_TEXT_OFFSET,
  PROP_RING_BUFFER_MAX_SIZE,
  PROP_FORCE_ASPECT_RATIO,
  PROP_AUDIO_FILTER,
  PROP_VIDEO_FILTER,
  PROP_MULTIVIEW_MODE,
  PROP_MULTIVIEW_FLAGS
};

/* signals */
enum
{
  SIGNAL_ABOUT_TO_FINISH,
  SIGNAL_CONVERT_SAMPLE,
  SIGNAL_SOURCE_SETUP,
  SIGNAL_ELEMENT_SETUP,
  LAST_SIGNAL
};

static void gst_play_bin3_finalize (GObject * object);

static void gst_play_bin3_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_bin3_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static GstStateChangeReturn gst_play_bin3_change_state (GstElement * element,
    GstStateChange transition);

static void gst_play_bin3_handle_message (GstBin * bin, GstMessage * message);
static void gst_play_bin3_deep_element_added (GstBin * playbin,
    GstBin * sub_bin, GstElement * child);
static gboolean gst_play_bin3_send_event (GstElement * element,
    GstEvent * event);

static GstSample *gst_play_bin3_convert_sample (GstPlayBin3 * playbin,
    GstCaps * caps);

static GstStateChangeReturn setup_next_source (GstPlayBin3 * playbin);

static void gst_play_bin3_check_group_status (GstPlayBin3 * playbin);
static void emit_about_to_finish (GstPlayBin3 * playbin);
static void reconfigure_output (GstPlayBin3 * playbin);
static void pad_removed_cb (GstElement * decodebin, GstPad * pad,
    GstSourceGroup * group);

static gint select_stream_cb (GstElement * decodebin,
    GstStreamCollection * collection, GstStream * stream,
    GstSourceGroup * group);

static void do_stream_selection (GstPlayBin3 * playbin, GstSourceGroup * group);

static GstElementClass *parent_class;

static guint gst_play_bin3_signals[LAST_SIGNAL] = { 0 };

#define REMOVE_SIGNAL(obj,id)			\
  if (id) {					\
    g_signal_handler_disconnect (obj, id);	\
    id = 0;					\
  }

static void gst_play_bin3_overlay_init (gpointer g_iface,
    gpointer g_iface_data);
static void gst_play_bin3_navigation_init (gpointer g_iface,
    gpointer g_iface_data);
static void gst_play_bin3_colorbalance_init (gpointer g_iface,
    gpointer g_iface_data);

static void
_do_init_type (GType type)
{
  static const GInterfaceInfo svol_info = {
    NULL, NULL, NULL
  };
  static const GInterfaceInfo ov_info = {
    gst_play_bin3_overlay_init,
    NULL, NULL
  };
  static const GInterfaceInfo nav_info = {
    gst_play_bin3_navigation_init,
    NULL, NULL
  };
  static const GInterfaceInfo col_info = {
    gst_play_bin3_colorbalance_init,
    NULL, NULL
  };

  g_type_add_interface_static (type, GST_TYPE_STREAM_VOLUME, &svol_info);
  g_type_add_interface_static (type, GST_TYPE_VIDEO_OVERLAY, &ov_info);
  g_type_add_interface_static (type, GST_TYPE_NAVIGATION, &nav_info);
  g_type_add_interface_static (type, GST_TYPE_COLOR_BALANCE, &col_info);
}

static GType gst_play_bin3_get_type (void);
G_DEFINE_TYPE_WITH_CODE (GstPlayBin3, gst_play_bin3, GST_TYPE_PIPELINE,
    _do_init_type (g_define_type_id));

GST_ELEMENT_REGISTER_DEFINE_CUSTOM (playbin3,
    gst_play_bin3_custom_element_init);

static void
gst_play_bin3_class_init (GstPlayBin3Class * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_play_bin3_set_property;
  gobject_klass->get_property = gst_play_bin3_get_property;

  gobject_klass->finalize = gst_play_bin3_finalize;

  /**
   * GstPlayBin3:uri
   *
   * Set the next URI that playbin will play. This property can be set from the
   * about-to-finish signal to queue the next media file.
   */
  g_object_class_install_property (gobject_klass, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:current-uri
   *
   * The currently playing uri.
   */
  g_object_class_install_property (gobject_klass, PROP_CURRENT_URI,
      g_param_spec_string ("current-uri", "Current URI",
          "The currently playing URI", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:suburi
   *
   * Set the next subtitle URI that playbin will play. This property can be
   * set from the about-to-finish signal to queue the next subtitle media file.
   */
  g_object_class_install_property (gobject_klass, PROP_SUBURI,
      g_param_spec_string ("suburi", ".sub-URI", "Optional URI of a subtitle",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:current-suburi
   *
   * The currently playing subtitle uri.
   */
  g_object_class_install_property (gobject_klass, PROP_CURRENT_SUBURI,
      g_param_spec_string ("current-suburi", "Current .sub-URI",
          "The currently playing URI of a subtitle",
          NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:flags
   *
   * Control the behaviour of playbin.
   */
  g_object_class_install_property (gobject_klass, PROP_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Flags to control behaviour",
          GST_TYPE_PLAY_FLAGS, DEFAULT_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_SUBTITLE_ENCODING,
      g_param_spec_string ("subtitle-encoding", "subtitle encoding",
          "Encoding to assume if input subtitles are not in UTF-8 encoding. "
          "If not set, the GST_SUBTITLE_ENCODING environment variable will "
          "be checked for an encoding to use. If that is not set either, "
          "ISO-8859-15 will be assumed.", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_VIDEO_FILTER,
      g_param_spec_object ("video-filter", "Video filter",
          "the video filter(s) to apply, if possible",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, PROP_AUDIO_FILTER,
      g_param_spec_object ("audio-filter", "Audio filter",
          "the audio filter(s) to apply, if possible",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin3:video-sink
   *
   * Get or set the video sink to use for video output. If set to
   * NULL, one will be auto-selected. To disable video entirely, unset
   * the VIDEO flag in the #GstPlayBin3:flags property.
   *
   */
  g_object_class_install_property (gobject_klass, PROP_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default sink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin3:audio-sink
   *
   * Get or set the audio sink to use for audio output. If set to
   * NULL, one will be auto-selected. To disable audio entirely, unset
   * the AUDIO flag in the #GstPlayBin3:flags property.
   *
   */
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
   * GstPlayBin3:video-stream-combiner
   *
   * Get or set the current video stream combiner. By default, no
   * element is used and the selected stream is used directly.
   */
  g_object_class_install_property (gobject_klass, PROP_VIDEO_STREAM_COMBINER,
      g_param_spec_object ("video-stream-combiner", "Video stream combiner",
          "Current video stream combiner (default: none)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:audio-stream-combiner
   *
   * Get or set the current audio stream combiner. By default, no
   * element is used and the selected stream is used directly.
   */
  g_object_class_install_property (gobject_klass, PROP_AUDIO_STREAM_COMBINER,
      g_param_spec_object ("audio-stream-combiner", "Audio stream combiner",
          "Current audio stream combiner (default: none))",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:text-stream-combiner
   *
   * Get or set the current text stream combiner. By default, no
   * element is used and the selected stream is used directly.
   */
  g_object_class_install_property (gobject_klass, PROP_TEXT_STREAM_COMBINER,
      g_param_spec_object ("text-stream-combiner", "Text stream combiner",
          "Current text stream combiner (default: none)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:volume:
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
   * GstPlayBin3:sample:
   * @playbin: a #GstPlayBin3
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
   * GstPlayBin3:av-offset:
   *
   * Control the synchronisation offset between the audio and video streams.
   * Positive values make the audio ahead of the video and negative values make
   * the audio go behind the video.
   */
  g_object_class_install_property (gobject_klass, PROP_AV_OFFSET,
      g_param_spec_int64 ("av-offset", "AV Offset",
          "The synchronisation offset between audio and video in nanoseconds",
          G_MININT64, G_MAXINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstPlayBin3:text-offset:
   *
   * Control the synchronisation offset between the text and video streams.
   * Positive values make the text ahead of the video and negative values make
   * the text go behind the video.
   */
  g_object_class_install_property (gobject_klass, PROP_TEXT_OFFSET,
      g_param_spec_int64 ("text-offset", "Text Offset",
          "The synchronisation offset between text and video in nanoseconds",
          G_MININT64, G_MAXINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3:ring-buffer-max-size
   *
   * The maximum size of the ring buffer in bytes. If set to 0, the ring
   * buffer is disabled. Default 0.
   */
  g_object_class_install_property (gobject_klass, PROP_RING_BUFFER_MAX_SIZE,
      g_param_spec_uint64 ("ring-buffer-max-size",
          "Max. ring buffer size (bytes)",
          "Max. amount of data in the ring buffer (bytes, 0 = ring buffer disabled)",
          0, G_MAXUINT, DEFAULT_RING_BUFFER_MAX_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3::force-aspect-ratio:
   *
   * Requests the video sink to enforce the video display aspect ratio.
   */
  g_object_class_install_property (gobject_klass, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force Aspect Ratio",
          "When enabled, scaling will respect original aspect ratio", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3::video-multiview-mode:
   *
   * Set the stereoscopic mode for video streams that don't contain
   * any information in the stream, so they can be correctly played
   * as 3D streams. If a video already has multiview information
   * encoded, this property can override other modes in the set,
   * but cannot be used to re-interpret MVC or mixed-mono streams.
   *
   * See Also: The #GstPlayBin3::video-multiview-flags property
   *
   */
  g_object_class_install_property (gobject_klass, PROP_MULTIVIEW_MODE,
      g_param_spec_enum ("video-multiview-mode",
          "Multiview Mode Override",
          "Re-interpret a video stream as one of several frame-packed stereoscopic modes.",
          GST_TYPE_VIDEO_MULTIVIEW_FRAME_PACKING,
          GST_VIDEO_MULTIVIEW_FRAME_PACKING_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3::video-multiview-flags:
   *
   * When overriding the multiview mode of an input stream,
   * these flags modify details of the view layout.
   *
   * See Also: The #GstPlayBin3::video-multiview-mode property
   */
  g_object_class_install_property (gobject_klass, PROP_MULTIVIEW_FLAGS,
      g_param_spec_flags ("video-multiview-flags",
          "Multiview Flags Override",
          "Override details of the multiview frame layout",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGS, GST_VIDEO_MULTIVIEW_FLAGS_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPlayBin3::about-to-finish
   * @playbin: a #GstPlayBin3
   *
   * This signal is emitted when the current uri is about to finish. You can
   * set the uri and suburi to make sure that playback continues.
   *
   * This signal is emitted from the context of a GStreamer streaming thread.
   */
  gst_play_bin3_signals[SIGNAL_ABOUT_TO_FINISH] =
      g_signal_new ("about-to-finish", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBin3Class, about_to_finish), NULL, NULL,
      NULL, G_TYPE_NONE, 0, G_TYPE_NONE);


  /**
   * GstPlayBin3::source-setup:
   * @playbin: a #GstPlayBin3
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
   */
  gst_play_bin3_signals[SIGNAL_SOURCE_SETUP] =
      g_signal_new ("source-setup", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  /**
   * GstPlayBin3::element-setup:
   * @playbin: a #GstPlayBin3
   * @element: an element that was added to the playbin hierarchy
   *
   * This signal is emitted when a new element is added to playbin or any of
   * its sub-bins. This signal can be used to configure elements, e.g. to set
   * properties on decoders. This is functionally equivalent to connecting to
   * the deep-element-added signal, but more convenient.
   *
   * This signal is usually emitted from the context of a GStreamer streaming
   * thread, so might be called at the same time as code running in the main
   * application thread.
   *
   * Since: 1.10
   */
  gst_play_bin3_signals[SIGNAL_ELEMENT_SETUP] =
      g_signal_new ("element-setup", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  /**
   * GstPlayBin3::convert-sample
   * @playbin: a #GstPlayBin3
   * @caps: the target format of the frame
   *
   * Action signal to retrieve the currently playing video frame in the format
   * specified by @caps.
   * If @caps is %NULL, no conversion will be performed and this function is
   * equivalent to the #GstPlayBin3:sample property.
   *
   * Returns: a #GstSample of the current video frame converted to #caps.
   * The caps on the sample will describe the final layout of the buffer data.
   * %NULL is returned when no current buffer can be retrieved or when the
   * conversion failed.
   */
  gst_play_bin3_signals[SIGNAL_CONVERT_SAMPLE] =
      g_signal_new ("convert-sample", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstPlayBin3Class, convert_sample), NULL, NULL,
      NULL, GST_TYPE_SAMPLE, 1, GST_TYPE_CAPS);

  klass->convert_sample = gst_play_bin3_convert_sample;

  gst_element_class_set_static_metadata (gstelement_klass,
      "Player Bin 3", "Generic/Bin/Player",
      "Autoplug and play media from an uri",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_bin3_change_state);
  gstelement_klass->send_event = GST_DEBUG_FUNCPTR (gst_play_bin3_send_event);

  gstbin_klass->handle_message =
      GST_DEBUG_FUNCPTR (gst_play_bin3_handle_message);
  gstbin_klass->deep_element_added =
      GST_DEBUG_FUNCPTR (gst_play_bin3_deep_element_added);
}

static void
do_async_start (GstPlayBin3 * playbin)
{
  GstMessage *message;

  playbin->async_pending = TRUE;

  message = gst_message_new_async_start (GST_OBJECT_CAST (playbin));
  GST_BIN_CLASS (parent_class)->handle_message (GST_BIN_CAST (playbin),
      message);
}

static void
do_async_done (GstPlayBin3 * playbin)
{
  GstMessage *message;

  if (playbin->async_pending) {
    GST_DEBUG_OBJECT (playbin, "posting ASYNC_DONE");
    message =
        gst_message_new_async_done (GST_OBJECT_CAST (playbin),
        GST_CLOCK_TIME_NONE);
    GST_BIN_CLASS (parent_class)->handle_message (GST_BIN_CAST (playbin),
        message);

    playbin->async_pending = FALSE;
  }
}

/* init combiners. The combiner is found by finding the first prefix that
 * matches the media. */
static void
init_combiners (GstPlayBin3 * playbin)
{
  playbin->combiner[PLAYBIN_STREAM_AUDIO].stream_type = GST_STREAM_TYPE_AUDIO;
  playbin->combiner[PLAYBIN_STREAM_AUDIO].inputpads =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);
  playbin->combiner[PLAYBIN_STREAM_AUDIO].streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);

  playbin->combiner[PLAYBIN_STREAM_VIDEO].stream_type = GST_STREAM_TYPE_VIDEO;
  playbin->combiner[PLAYBIN_STREAM_VIDEO].inputpads =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);
  playbin->combiner[PLAYBIN_STREAM_VIDEO].streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);

  playbin->combiner[PLAYBIN_STREAM_TEXT].stream_type = GST_STREAM_TYPE_TEXT;
  playbin->combiner[PLAYBIN_STREAM_TEXT].inputpads =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);
  playbin->combiner[PLAYBIN_STREAM_TEXT].streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);
}

/* Update the combiner information to be in sync with the current collection
 *
 * FIXME : "current" collection doesn't mean anything until we have a "combined"
 *  collection of all groups */
static void
update_combiner_info (GstPlayBin3 * playbin, GstStreamCollection * collection)
{
  guint i, len;

  if (collection == NULL)
    return;

  GST_DEBUG_OBJECT (playbin, "Updating combiner info");

  /* Wipe current combiner streams */
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_AUDIO].streams, TRUE);
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_VIDEO].streams, TRUE);
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_TEXT].streams, TRUE);
  playbin->combiner[PLAYBIN_STREAM_AUDIO].streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);
  playbin->combiner[PLAYBIN_STREAM_VIDEO].streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);
  playbin->combiner[PLAYBIN_STREAM_TEXT].streams =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);

  len = gst_stream_collection_get_size (collection);
  for (i = 0; i < len; i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    GstStreamType stype = gst_stream_get_stream_type (stream);

    if (stype & GST_STREAM_TYPE_AUDIO) {
      g_ptr_array_add (playbin->combiner[PLAYBIN_STREAM_AUDIO].streams,
          gst_object_ref (stream));
    } else if (stype & GST_STREAM_TYPE_VIDEO) {
      g_ptr_array_add (playbin->combiner[PLAYBIN_STREAM_VIDEO].streams,
          gst_object_ref (stream));
    } else if (stype & GST_STREAM_TYPE_TEXT) {
      g_ptr_array_add (playbin->combiner[PLAYBIN_STREAM_TEXT].streams,
          gst_object_ref (stream));
    }
  }

  GST_DEBUG_OBJECT (playbin, "There are %d audio streams",
      playbin->combiner[PLAYBIN_STREAM_AUDIO].streams->len);
  GST_DEBUG_OBJECT (playbin, "There are %d video streams",
      playbin->combiner[PLAYBIN_STREAM_VIDEO].streams->len);
  GST_DEBUG_OBJECT (playbin, "There are %d text streams",
      playbin->combiner[PLAYBIN_STREAM_TEXT].streams->len);
}

#ifndef GST_DISABLE_GST_DEBUG
#define debug_groups(playbin) G_STMT_START {	\
    guint i;					\
    						\
    for (i = 0; i < 2; i++) {				\
      GstSourceGroup *group = &playbin->groups[i];	\
      							\
      GST_DEBUG ("GstSourceGroup #%d (%s) : %s", i, (group == playbin->curr_group) ? "current" : (group == playbin->next_group) ? "next" : "unused", \
		 group->uridecodebin ? GST_ELEMENT_NAME (group->uridecodebin) : "NULL" ); \
      GST_DEBUG ("  valid:%d , active:%d , playing:%d", group->valid, group->active, group->playing); \
      GST_DEBUG ("  uri:%s", group->uri);				\
      GST_DEBUG ("  suburi:%s", group->suburi);				\
      GST_DEBUG ("  group_id:%d", group->group_id);			\
      GST_DEBUG ("  pending_about_to_finish:%d", group->pending_about_to_finish); \
    }									\
  } G_STMT_END
#else
#define debug_groups(p) {}
#endif

static void
init_group (GstPlayBin3 * playbin, GstSourceGroup * group)
{
  g_mutex_init (&group->lock);

  group->stream_changed_pending = FALSE;
  group->group_id = GST_GROUP_ID_INVALID;

  group->playbin = playbin;
}

static void
free_group (GstPlayBin3 * playbin, GstSourceGroup * group)
{
  g_free (group->uri);
  g_free (group->suburi);

  g_mutex_clear (&group->lock);
  group->stream_changed_pending = FALSE;

  if (group->pending_buffering_msg)
    gst_message_unref (group->pending_buffering_msg);
  group->pending_buffering_msg = NULL;

  gst_object_replace ((GstObject **) & group->collection, NULL);

  gst_object_replace ((GstObject **) & group->audio_sink, NULL);
  gst_object_replace ((GstObject **) & group->video_sink, NULL);
  gst_object_replace ((GstObject **) & group->text_sink, NULL);
}

static void
notify_volume_cb (GObject * combiner, GParamSpec * pspec, GstPlayBin3 * playbin)
{
  g_object_notify (G_OBJECT (playbin), "volume");
}

static void
notify_mute_cb (GObject * combiner, GParamSpec * pspec, GstPlayBin3 * playbin)
{
  g_object_notify (G_OBJECT (playbin), "mute");
}

static void
colorbalance_value_changed_cb (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value, GstPlayBin3 * playbin)
{
  gst_color_balance_value_changed (GST_COLOR_BALANCE (playbin), channel, value);
}

static void
gst_play_bin3_init (GstPlayBin3 * playbin)
{
  g_rec_mutex_init (&playbin->lock);
  g_mutex_init (&playbin->dyn_lock);

  g_mutex_init (&playbin->buffering_post_lock);

  /* assume we can create an input-selector */
  playbin->have_selector = TRUE;

  init_combiners (playbin);

  /* init groups */
  playbin->curr_group = &playbin->groups[0];
  playbin->next_group = &playbin->groups[1];
  init_group (playbin, &playbin->groups[0]);
  init_group (playbin, &playbin->groups[1]);

  g_rec_mutex_init (&playbin->activation_lock);

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

  playbin->multiview_mode = GST_VIDEO_MULTIVIEW_FRAME_PACKING_NONE;
  playbin->multiview_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;

  playbin->is_live = FALSE;
}

static void
gst_play_bin3_finalize (GObject * object)
{
  GstPlayBin3 *playbin;

  playbin = GST_PLAY_BIN3 (object);

  free_group (playbin, &playbin->groups[0]);
  free_group (playbin, &playbin->groups[1]);

  /* Setting states to NULL is safe here because playsink
   * will already be gone and none of these sinks will be
   * a child of playsink
   */
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

  if (playbin->video_stream_combiner) {
    gst_element_set_state (playbin->video_stream_combiner, GST_STATE_NULL);
    gst_object_unref (playbin->video_stream_combiner);
  }
  if (playbin->audio_stream_combiner) {
    gst_element_set_state (playbin->audio_stream_combiner, GST_STATE_NULL);
    gst_object_unref (playbin->audio_stream_combiner);
  }
  if (playbin->text_stream_combiner) {
    gst_element_set_state (playbin->text_stream_combiner, GST_STATE_NULL);
    gst_object_unref (playbin->text_stream_combiner);
  }

  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_AUDIO].streams, TRUE);
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_AUDIO].inputpads, TRUE);
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_VIDEO].streams, TRUE);
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_VIDEO].inputpads, TRUE);
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_TEXT].streams, TRUE);
  g_ptr_array_free (playbin->combiner[PLAYBIN_STREAM_TEXT].inputpads, TRUE);

  g_rec_mutex_clear (&playbin->activation_lock);
  g_rec_mutex_clear (&playbin->lock);

  g_mutex_clear (&playbin->buffering_post_lock);
  g_mutex_clear (&playbin->dyn_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_playbin_uri_is_valid (GstPlayBin3 * playbin, const gchar * uri)
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
gst_play_bin3_set_uri (GstPlayBin3 * playbin, const gchar * uri)
{
  GstSourceGroup *group;

  if (uri && !gst_playbin_uri_is_valid (playbin, uri)) {
    if (g_str_has_prefix (uri, "file:")) {
      GST_WARNING_OBJECT (playbin, "not entirely correct file URI '%s' - make "
          "sure to escape spaces and non-ASCII characters properly and specify "
          "an absolute path. Use gst_filename_to_uri() to convert filenames "
          "to URIs", uri);
    } else {
      /* GST_ERROR_OBJECT (playbin, "malformed URI '%s'", uri); */
    }
  }

  GST_PLAY_BIN3_LOCK (playbin);
  group = playbin->next_group;

  GST_SOURCE_GROUP_LOCK (group);
  /* store the uri in the next group we will play */
  g_free (group->uri);
  if (uri) {
    group->uri = g_strdup (uri);
    group->valid = TRUE;
  } else {
    group->uri = NULL;
    group->valid = FALSE;
  }
  GST_SOURCE_GROUP_UNLOCK (group);

  GST_DEBUG ("set new uri to %s", GST_STR_NULL (uri));
  GST_PLAY_BIN3_UNLOCK (playbin);
}

static void
gst_play_bin3_set_suburi (GstPlayBin3 * playbin, const gchar * suburi)
{
  GstSourceGroup *group;

  GST_PLAY_BIN3_LOCK (playbin);
  group = playbin->next_group;

  GST_SOURCE_GROUP_LOCK (group);
  g_free (group->suburi);
  group->suburi = g_strdup (suburi);
  GST_SOURCE_GROUP_UNLOCK (group);

  GST_DEBUG ("setting new .sub uri to %s", suburi);

  GST_PLAY_BIN3_UNLOCK (playbin);
}

static void
gst_play_bin3_set_flags (GstPlayBin3 * playbin, GstPlayFlags flags)
{
  GstPlayFlags old_flags;
  old_flags = gst_play_sink_get_flags (playbin->playsink);

  if (flags != old_flags) {
    gst_play_sink_set_flags (playbin->playsink, flags);
    gst_play_sink_reconfigure (playbin->playsink);
  }
}

static GstPlayFlags
gst_play_bin3_get_flags (GstPlayBin3 * playbin)
{
  GstPlayFlags flags;

  flags = gst_play_sink_get_flags (playbin->playsink);

  return flags;
}

/* get the currently playing group or if nothing is playing, the next
 * group. Must be called with the PLAY_BIN_LOCK. */
static GstSourceGroup *
get_group (GstPlayBin3 * playbin)
{
  GstSourceGroup *result;

  if (!(result = playbin->curr_group))
    result = playbin->next_group;

  return result;
}


static GstSample *
gst_play_bin3_convert_sample (GstPlayBin3 * playbin, GstCaps * caps)
{
  return gst_play_sink_convert_sample (playbin->playsink, caps);
}

static gboolean
gst_play_bin3_send_custom_event (GstObject * combiner, const gchar * event_name)
{
  GstPad *src;
  GstPad *peer;
  GstStructure *s;
  GstEvent *event;
  gboolean ret = FALSE;

  src = gst_element_get_static_pad (GST_ELEMENT_CAST (combiner), "src");
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
gst_play_bin3_set_current_stream (GstPlayBin3 * playbin,
    gint stream_type, gint * current_value, gint stream,
    gboolean * flush_marker)
{
  GstSourceCombine *combine;
  GstPad *sinkpad;

  GST_PLAY_BIN3_LOCK (playbin);
  /* This function is only called if the app sets
   * one of the current-* properties, which means it doesn't
   * handle collections or select-streams yet */
  playbin->do_stream_selections = TRUE;

  combine = playbin->combiner + stream_type;

  GST_DEBUG_OBJECT (playbin, "Changing current %s stream %d -> %d",
      stream_type_names[stream_type], *current_value, stream);

  if (combine->combiner == NULL || combine->is_concat) {
    /* FIXME: Check that the current_value is within range */
    *current_value = stream;
    do_stream_selection (playbin, playbin->curr_group);
    GST_PLAY_BIN3_UNLOCK (playbin);
    return TRUE;
  }

  GST_DEBUG_OBJECT (playbin, "Using old style combiner");

  if (!combine->has_active_pad)
    goto no_active_pad;
  if (combine->inputpads == NULL)
    goto no_channels;

  if (stream == -1 || combine->inputpads->len <= stream) {
    sinkpad = NULL;
  } else {
    /* take combiner sink pad for selected stream */
    sinkpad = g_ptr_array_index (combine->inputpads, stream);
  }

  if (sinkpad)
    gst_object_ref (sinkpad);
  GST_PLAY_BIN3_UNLOCK (playbin);

  if (sinkpad) {
    GstObject *combiner;

    if ((combiner = gst_pad_get_parent (sinkpad))) {
      GstPad *old_sinkpad;

      g_object_get (combiner, "active-pad", &old_sinkpad, NULL);

      if (old_sinkpad != sinkpad) {
        /* FIXME: Is there actually any reason playsink
         * needs special names for each type of stream we flush? */
        gchar *flush_event_name = g_strdup_printf ("playsink-custom-%s-flush",
            stream_type_names[stream_type]);
        if (gst_play_bin3_send_custom_event (combiner, flush_event_name))
          *flush_marker = TRUE;
        g_free (flush_event_name);

        /* activate the selected pad */
        g_object_set (combiner, "active-pad", sinkpad, NULL);
      }

      if (old_sinkpad)
        gst_object_unref (old_sinkpad);

      gst_object_unref (combiner);
    }
    gst_object_unref (sinkpad);
  }
  return TRUE;

no_active_pad:
  {
    GST_PLAY_BIN3_UNLOCK (playbin);
    GST_WARNING_OBJECT (playbin,
        "can't switch %s, the stream combiner's sink pads don't have the \"active-pad\" property",
        stream_type_names[stream_type]);
    return FALSE;
  }
no_channels:
  {
    GST_PLAY_BIN3_UNLOCK (playbin);
    GST_DEBUG_OBJECT (playbin,
        "can't switch stream, we have no combiner input pad");
    return FALSE;
  }
}

static gboolean
gst_play_bin3_set_current_video_stream (GstPlayBin3 * playbin, gint stream)
{
  return gst_play_bin3_set_current_stream (playbin, PLAYBIN_STREAM_VIDEO,
      &playbin->current_video, stream, &playbin->video_pending_flush_finish);
}

static gboolean
gst_play_bin3_set_current_audio_stream (GstPlayBin3 * playbin, gint stream)
{
  return gst_play_bin3_set_current_stream (playbin, PLAYBIN_STREAM_AUDIO,
      &playbin->current_audio, stream, &playbin->audio_pending_flush_finish);
}

static gboolean
gst_play_bin3_set_current_text_stream (GstPlayBin3 * playbin, gint stream)
{
  return gst_play_bin3_set_current_stream (playbin, PLAYBIN_STREAM_TEXT,
      &playbin->current_text, stream, &playbin->text_pending_flush_finish);
}


static void
gst_play_bin3_set_sink (GstPlayBin3 * playbin, GstPlaySinkType type,
    const gchar * dbg, GstElement ** elem, GstElement * sink)
{
  GST_INFO_OBJECT (playbin, "Setting %s sink to %" GST_PTR_FORMAT, dbg, sink);

  gst_play_sink_set_sink (playbin->playsink, type, sink);

  if (*elem)
    gst_object_unref (*elem);
  *elem = sink ? gst_object_ref (sink) : NULL;
}

static void
gst_play_bin3_set_stream_combiner (GstPlayBin3 * playbin, GstElement ** elem,
    const gchar * dbg, GstElement * combiner)
{
  GST_INFO_OBJECT (playbin, "Setting %s stream combiner to %" GST_PTR_FORMAT,
      dbg, combiner);

  GST_PLAY_BIN3_LOCK (playbin);
  if (*elem != combiner) {
    GstElement *old;

    old = *elem;
    if (combiner)
      gst_object_ref_sink (combiner);

    *elem = combiner;
    if (old)
      gst_object_unref (old);
  }
  GST_LOG_OBJECT (playbin, "%s stream combiner now %" GST_PTR_FORMAT, dbg,
      *elem);
  GST_PLAY_BIN3_UNLOCK (playbin);
}

static void
gst_play_bin3_set_encoding (GstPlayBin3 * playbin, const gchar * encoding)
{
  GST_PLAY_BIN3_LOCK (playbin);
  gst_play_sink_set_subtitle_encoding (playbin->playsink, encoding);
  GST_PLAY_BIN3_UNLOCK (playbin);
}

static void
gst_play_bin3_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (object);

  switch (prop_id) {
    case PROP_URI:
      gst_play_bin3_set_uri (playbin, g_value_get_string (value));
      break;
    case PROP_SUBURI:
      gst_play_bin3_set_suburi (playbin, g_value_get_string (value));
      break;
    case PROP_FLAGS:
      gst_play_bin3_set_flags (playbin, g_value_get_flags (value));
      if (playbin->curr_group) {
        GST_SOURCE_GROUP_LOCK (playbin->curr_group);
        if (playbin->curr_group->uridecodebin) {
          g_object_set (playbin->curr_group->uridecodebin, "download",
              (g_value_get_flags (value) & GST_PLAY_FLAG_DOWNLOAD) != 0, NULL);
        }
        GST_SOURCE_GROUP_UNLOCK (playbin->curr_group);
      }
      break;
    case PROP_SUBTITLE_ENCODING:
      gst_play_bin3_set_encoding (playbin, g_value_get_string (value));
      break;
    case PROP_VIDEO_FILTER:
      gst_play_sink_set_filter (playbin->playsink, GST_PLAY_SINK_TYPE_VIDEO,
          GST_ELEMENT (g_value_get_object (value)));
      break;
    case PROP_AUDIO_FILTER:
      gst_play_sink_set_filter (playbin->playsink, GST_PLAY_SINK_TYPE_AUDIO,
          GST_ELEMENT (g_value_get_object (value)));
      break;
    case PROP_VIDEO_SINK:
      gst_play_bin3_set_sink (playbin, GST_PLAY_SINK_TYPE_VIDEO, "video",
          &playbin->video_sink, g_value_get_object (value));
      break;
    case PROP_AUDIO_SINK:
      gst_play_bin3_set_sink (playbin, GST_PLAY_SINK_TYPE_AUDIO, "audio",
          &playbin->audio_sink, g_value_get_object (value));
      break;
    case PROP_VIS_PLUGIN:
      gst_play_sink_set_vis_plugin (playbin->playsink,
          g_value_get_object (value));
      break;
    case PROP_TEXT_SINK:
      gst_play_bin3_set_sink (playbin, GST_PLAY_SINK_TYPE_TEXT, "text",
          &playbin->text_sink, g_value_get_object (value));
      break;
    case PROP_VIDEO_STREAM_COMBINER:
      gst_play_bin3_set_stream_combiner (playbin,
          &playbin->video_stream_combiner, "video", g_value_get_object (value));
      break;
    case PROP_AUDIO_STREAM_COMBINER:
      gst_play_bin3_set_stream_combiner (playbin,
          &playbin->audio_stream_combiner, "audio", g_value_get_object (value));
      break;
    case PROP_TEXT_STREAM_COMBINER:
      gst_play_bin3_set_stream_combiner (playbin,
          &playbin->text_stream_combiner, "text", g_value_get_object (value));
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
      GST_PLAY_BIN3_LOCK (playbin);
      playbin->connection_speed = g_value_get_uint64 (value) * 1000;
      GST_PLAY_BIN3_UNLOCK (playbin);
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
    case PROP_TEXT_OFFSET:
      gst_play_sink_set_text_offset (playbin->playsink,
          g_value_get_int64 (value));
      break;
    case PROP_RING_BUFFER_MAX_SIZE:
      playbin->ring_buffer_max_size = g_value_get_uint64 (value);
      if (playbin->curr_group) {
        GST_SOURCE_GROUP_LOCK (playbin->curr_group);
        if (playbin->curr_group->uridecodebin) {
          g_object_set (playbin->curr_group->uridecodebin,
              "ring-buffer-max-size", playbin->ring_buffer_max_size, NULL);
        }
        GST_SOURCE_GROUP_UNLOCK (playbin->curr_group);
      }
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_object_set (playbin->playsink, "force-aspect-ratio",
          g_value_get_boolean (value), NULL);
      break;
    case PROP_MULTIVIEW_MODE:
      GST_PLAY_BIN3_LOCK (playbin);
      playbin->multiview_mode = g_value_get_enum (value);
      GST_PLAY_BIN3_UNLOCK (playbin);
      break;
    case PROP_MULTIVIEW_FLAGS:
      GST_PLAY_BIN3_LOCK (playbin);
      playbin->multiview_flags = g_value_get_flags (value);
      GST_PLAY_BIN3_UNLOCK (playbin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElement *
gst_play_bin3_get_current_sink (GstPlayBin3 * playbin, GstElement ** elem,
    const gchar * dbg, GstPlaySinkType type)
{
  GstElement *sink = gst_play_sink_get_sink (playbin->playsink, type);

  GST_LOG_OBJECT (playbin, "play_sink_get_sink() returned %s sink %"
      GST_PTR_FORMAT ", the originally set %s sink is %" GST_PTR_FORMAT,
      dbg, sink, dbg, *elem);

  if (sink == NULL) {
    GST_PLAY_BIN3_LOCK (playbin);
    if ((sink = *elem))
      gst_object_ref (sink);
    GST_PLAY_BIN3_UNLOCK (playbin);
  }

  return sink;
}

static GstElement *
gst_play_bin3_get_current_stream_combiner (GstPlayBin3 * playbin,
    GstElement ** elem, const gchar * dbg, int stream_type)
{
  GstElement *combiner;

  GST_PLAY_BIN3_LOCK (playbin);
  /* The special concat element should never be returned */
  if (playbin->combiner[stream_type].is_concat)
    combiner = NULL;
  else if ((combiner = playbin->combiner[stream_type].combiner))
    gst_object_ref (combiner);
  else if ((combiner = *elem))
    gst_object_ref (combiner);
  GST_PLAY_BIN3_UNLOCK (playbin);

  return combiner;
}

static void
gst_play_bin3_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (object);

  switch (prop_id) {
    case PROP_URI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN3_LOCK (playbin);
      group = playbin->next_group;
      g_value_set_string (value, group->uri);
      GST_PLAY_BIN3_UNLOCK (playbin);
      break;
    }
    case PROP_CURRENT_URI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN3_LOCK (playbin);
      group = get_group (playbin);
      g_value_set_string (value, group->uri);
      GST_PLAY_BIN3_UNLOCK (playbin);
      break;
    }
    case PROP_SUBURI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN3_LOCK (playbin);
      group = playbin->next_group;
      g_value_set_string (value, group->suburi);
      GST_PLAY_BIN3_UNLOCK (playbin);
      break;
    }
    case PROP_CURRENT_SUBURI:
    {
      GstSourceGroup *group;

      GST_PLAY_BIN3_LOCK (playbin);
      group = get_group (playbin);
      g_value_set_string (value, group->suburi);
      GST_PLAY_BIN3_UNLOCK (playbin);
      break;
    }
    case PROP_FLAGS:
      g_value_set_flags (value, gst_play_bin3_get_flags (playbin));
      break;
    case PROP_SUBTITLE_ENCODING:
      GST_PLAY_BIN3_LOCK (playbin);
      g_value_take_string (value,
          gst_play_sink_get_subtitle_encoding (playbin->playsink));
      GST_PLAY_BIN3_UNLOCK (playbin);
      break;
    case PROP_VIDEO_FILTER:
      g_value_take_object (value,
          gst_play_sink_get_filter (playbin->playsink,
              GST_PLAY_SINK_TYPE_VIDEO));
      break;
    case PROP_AUDIO_FILTER:
      g_value_take_object (value,
          gst_play_sink_get_filter (playbin->playsink,
              GST_PLAY_SINK_TYPE_AUDIO));
      break;
    case PROP_VIDEO_SINK:
      g_value_take_object (value,
          gst_play_bin3_get_current_sink (playbin, &playbin->video_sink,
              "video", GST_PLAY_SINK_TYPE_VIDEO));
      break;
    case PROP_AUDIO_SINK:
      g_value_take_object (value,
          gst_play_bin3_get_current_sink (playbin, &playbin->audio_sink,
              "audio", GST_PLAY_SINK_TYPE_AUDIO));
      break;
    case PROP_VIS_PLUGIN:
      g_value_take_object (value,
          gst_play_sink_get_vis_plugin (playbin->playsink));
      break;
    case PROP_TEXT_SINK:
      g_value_take_object (value,
          gst_play_bin3_get_current_sink (playbin, &playbin->text_sink,
              "text", GST_PLAY_SINK_TYPE_TEXT));
      break;
    case PROP_VIDEO_STREAM_COMBINER:
      g_value_take_object (value,
          gst_play_bin3_get_current_stream_combiner (playbin,
              &playbin->video_stream_combiner, "video", PLAYBIN_STREAM_VIDEO));
      break;
    case PROP_AUDIO_STREAM_COMBINER:
      g_value_take_object (value,
          gst_play_bin3_get_current_stream_combiner (playbin,
              &playbin->audio_stream_combiner, "audio", PLAYBIN_STREAM_AUDIO));
      break;
    case PROP_TEXT_STREAM_COMBINER:
      g_value_take_object (value,
          gst_play_bin3_get_current_stream_combiner (playbin,
              &playbin->text_stream_combiner, "text", PLAYBIN_STREAM_TEXT));
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
      GST_PLAY_BIN3_LOCK (playbin);
      g_value_set_uint64 (value, playbin->connection_speed / 1000);
      GST_PLAY_BIN3_UNLOCK (playbin);
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
    case PROP_TEXT_OFFSET:
      g_value_set_int64 (value,
          gst_play_sink_get_text_offset (playbin->playsink));
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
    case PROP_MULTIVIEW_MODE:
      GST_OBJECT_LOCK (playbin);
      g_value_set_enum (value, playbin->multiview_mode);
      GST_OBJECT_UNLOCK (playbin);
      break;
    case PROP_MULTIVIEW_FLAGS:
      GST_OBJECT_LOCK (playbin);
      g_value_set_flags (value, playbin->multiview_flags);
      GST_OBJECT_UNLOCK (playbin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
get_combiner_stream_id (GstPlayBin3 * playbin, GstSourceCombine * combine,
    GList * full_list)
{
  gint i;
  GList *tmp;

  for (i = 0; i < combine->streams->len; i++) {
    GstStream *stream = (GstStream *) g_ptr_array_index (combine->streams, i);
    const gchar *sid = gst_stream_get_stream_id (stream);
    for (tmp = full_list; tmp; tmp = tmp->next) {
      gchar *orig = (gchar *) tmp->data;
      if (!g_strcmp0 (orig, sid))
        return i;
    }
  }

  /* Fallback */
  return -1;
}

static GList *
extend_list_of_streams (GstPlayBin3 * playbin, GstStreamType stype,
    GList * list, GstStreamCollection * collection)
{
  GList *tmp, *res;
  gint i, nb;

  res = list;

  nb = gst_stream_collection_get_size (collection);
  for (i = 0; i < nb; i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    GstStreamType curtype = gst_stream_get_stream_type (stream);
    if (stype == curtype) {
      gboolean already_there = FALSE;
      const gchar *sid = gst_stream_get_stream_id (stream);
      for (tmp = res; tmp; tmp = tmp->next) {
        const gchar *other = (const gchar *) tmp->data;
        if (!g_strcmp0 (sid, other)) {
          already_there = TRUE;
          break;
        }
      }
      if (!already_there) {
        GST_DEBUG_OBJECT (playbin, "Adding stream %s", sid);
        res = g_list_append (res, g_strdup (sid));
      }
    }
  }

  return res;
}

static GstEvent *
update_select_streams_event (GstPlayBin3 * playbin, GstEvent * event,
    GstSourceGroup * group)
{
  GList *streams = NULL;
  GList *to_use;
  gint combine_id;

  if (!playbin->audio_stream_combiner && !playbin->video_stream_combiner &&
      !playbin->text_stream_combiner) {
    /* Nothing to do */
    GST_DEBUG_OBJECT (playbin,
        "No custom combiners, no need to modify SELECT_STREAMS event");
    return event;
  }

  if (!group->collection) {
    GST_DEBUG_OBJECT (playbin,
        "No stream collection for group, no need to modify SELECT_STREAMS event");
    return event;
  }

  gst_event_parse_select_streams (event, &streams);
  to_use = g_list_copy_deep (streams, (GCopyFunc) g_strdup, NULL);

  /* For each combiner, we want to add all streams of that type to the
   * selection */
  if (playbin->audio_stream_combiner) {
    to_use =
        extend_list_of_streams (playbin, GST_STREAM_TYPE_AUDIO, to_use,
        group->collection);
    combine_id =
        get_combiner_stream_id (playbin,
        &playbin->combiner[PLAYBIN_STREAM_AUDIO], streams);
    if (combine_id != -1)
      gst_play_bin3_set_current_audio_stream (playbin, combine_id);
  }
  if (playbin->video_stream_combiner) {
    to_use =
        extend_list_of_streams (playbin, GST_STREAM_TYPE_VIDEO, to_use,
        group->collection);
    combine_id =
        get_combiner_stream_id (playbin,
        &playbin->combiner[PLAYBIN_STREAM_VIDEO], streams);
    if (combine_id != -1)
      gst_play_bin3_set_current_video_stream (playbin, combine_id);
  }
  if (playbin->text_stream_combiner) {
    to_use =
        extend_list_of_streams (playbin, GST_STREAM_TYPE_TEXT, to_use,
        group->collection);
    combine_id =
        get_combiner_stream_id (playbin,
        &playbin->combiner[PLAYBIN_STREAM_TEXT], streams);
    if (combine_id != -1)
      gst_play_bin3_set_current_text_stream (playbin, combine_id);
  }

  gst_event_unref (event);
  event = gst_event_new_select_streams (to_use);

  if (streams)
    g_list_free_full (streams, g_free);
  if (to_use)
    g_list_free_full (to_use, g_free);

  return event;
}

/* Returns TRUE if the given list of streams belongs to the stream collection */
static gboolean
gst_streams_belong_to_collection (GList * streams,
    GstStreamCollection * collection)
{
  GList *tmp;
  guint i, nb;

  if (streams == NULL || collection == NULL)
    return FALSE;
  nb = gst_stream_collection_get_size (collection);
  if (nb == 0)
    return FALSE;

  for (tmp = streams; tmp; tmp = tmp->next) {
    const gchar *cand = (const gchar *) tmp->data;
    gboolean found = FALSE;

    for (i = 0; i < nb; i++) {
      GstStream *stream = gst_stream_collection_get_stream (collection, i);
      if (!g_strcmp0 (cand, gst_stream_get_stream_id (stream))) {
        found = TRUE;
        break;
      }
    }
    if (!found)
      return FALSE;
  }
  return TRUE;
}

static GstSourceGroup *
get_source_group_for_streams (GstPlayBin3 * playbin, GstEvent * event)
{
  GList *streams;
  GstSourceGroup *res = NULL;

  gst_event_parse_select_streams (event, &streams);
  if (playbin->curr_group->collection &&
      gst_streams_belong_to_collection (streams,
          playbin->curr_group->collection))
    res = playbin->curr_group;
  else if (playbin->next_group->collection &&
      gst_streams_belong_to_collection (streams,
          playbin->next_group->collection))
    res = playbin->next_group;
  g_list_free_full (streams, g_free);

  return res;
}

static gboolean
gst_play_bin3_send_event (GstElement * element, GstEvent * event)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (element);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SELECT_STREAMS) {
    gboolean res;
    GstSourceGroup *group;

    GST_PLAY_BIN3_LOCK (playbin);
    GST_LOG_OBJECT (playbin,
        "App sent select-streams, we won't do anything ourselves now");
    /* This is probably already false, but it doesn't hurt to be sure */
    playbin->do_stream_selections = FALSE;

    group = get_source_group_for_streams (playbin, event);
    if (group == NULL) {
      GST_WARNING_OBJECT (playbin,
          "Can't figure out to which uridecodebin the select-streams event should be sent to");
      GST_PLAY_BIN3_UNLOCK (playbin);
      return FALSE;
    }

    /* If we have custom combiners, we need to extend the selection with
     * the list of all streams for that given type since we will be handling
     * the selection with that combiner */
    event = update_select_streams_event (playbin, event, group);

    /* Don't reconfigure playsink just yet, until the streams-selected
     * message(s) tell us as streams become active / available */

    /* Send this event directly to uridecodebin, so it works even
     * if uridecodebin didn't add any pads yet */
    res = gst_element_send_event (group->uridecodebin, event);
    GST_PLAY_BIN3_UNLOCK (playbin);

    return res;
  }

  /* Send event directly to playsink instead of letting GstBin iterate
   * over all sink elements. The latter might send the event multiple times
   * in case the SEEK causes a reconfiguration of the pipeline, as can easily
   * happen with adaptive streaming demuxers.
   *
   * What would then happen is that the iterator would be reset, we send the
   * event again, and on the second time it will fail in the majority of cases
   * because the pipeline is still being reconfigured
   */
  if (GST_EVENT_IS_UPSTREAM (event)) {
    return gst_element_send_event (GST_ELEMENT_CAST (playbin->playsink), event);
  }

  return GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
}

/* Called with playbin lock held */
static void
do_stream_selection (GstPlayBin3 * playbin, GstSourceGroup * group)
{
  GstStreamCollection *collection;
  guint i, nb_streams;
  GList *streams = NULL;
  gint nb_video = 0, nb_audio = 0, nb_text = 0;
  GstStreamType chosen_stream_types = 0;

  if (group == NULL)
    return;

  collection = group->collection;
  if (collection == NULL) {
    GST_LOG_OBJECT (playbin, "No stream collection. Not doing stream-select");
    return;
  }

  nb_streams = gst_stream_collection_get_size (collection);
  if (nb_streams == 0) {
    GST_INFO_OBJECT (playbin, "Empty collection received! Ignoring");
  }

  GST_DEBUG_OBJECT (playbin, "Doing selection on collection with %d streams",
      nb_streams);

  /* Iterate the collection and choose the streams that match
   * either the current-* setting, or all streams of a type if there's
   * a combiner for that type */
  for (i = 0; i < nb_streams; i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    GstStreamType stream_type = gst_stream_get_stream_type (stream);
    const gchar *stream_id = gst_stream_get_stream_id (stream);
    gint pb_stream_type = -1;
    gboolean select_this = FALSE;

    GST_LOG_OBJECT (playbin, "Looking at stream #%d : %s", i, stream_id);

    if (stream_type & GST_STREAM_TYPE_AUDIO) {
      pb_stream_type = PLAYBIN_STREAM_AUDIO;
      /* Select the stream if it's the current one or if there's a custom selector */
      select_this =
          (nb_audio == playbin->current_audio ||
          (playbin->current_audio == -1 && nb_audio == 0) ||
          playbin->audio_stream_combiner != NULL);
      nb_audio++;
    } else if (stream_type & GST_STREAM_TYPE_VIDEO) {
      pb_stream_type = PLAYBIN_STREAM_VIDEO;
      select_this =
          (nb_video == playbin->current_video ||
          (playbin->current_video == -1 && nb_video == 0) ||
          playbin->video_stream_combiner != NULL);
      nb_video++;
    } else if (stream_type & GST_STREAM_TYPE_TEXT) {
      pb_stream_type = PLAYBIN_STREAM_TEXT;
      select_this =
          (nb_text == playbin->current_text ||
          (playbin->current_text == -1 && nb_text == 0) ||
          playbin->text_stream_combiner != NULL);
      nb_text++;
    }
    if (pb_stream_type < 0) {
      GST_DEBUG_OBJECT (playbin,
          "Stream %d (id %s) of unhandled type %s. Ignoring", i, stream_id,
          gst_stream_type_get_name (stream_type));
      continue;
    }
    if (select_this) {
      GST_DEBUG_OBJECT (playbin, "Selecting stream %s of type %s",
          stream_id, gst_stream_type_get_name (stream_type));
      /* Don't build the list if we're not in charge of stream selection */
      if (playbin->do_stream_selections)
        streams = g_list_append (streams, (gpointer) stream_id);
      chosen_stream_types |= stream_type;
    }
  }

  if (streams) {
    if (group->uridecodebin) {
      GstEvent *ev = gst_event_new_select_streams (streams);
      gst_element_send_event (group->uridecodebin, ev);
    }
    g_list_free (streams);
  }

  group->selected_stream_types = chosen_stream_types;
  /* Update global selected_stream_types */
  playbin->selected_stream_types =
      playbin->groups[0].selected_stream_types | playbin->groups[1].
      selected_stream_types;
  if (playbin->active_stream_types != playbin->selected_stream_types)
    reconfigure_output (playbin);
}

/* Return the GstSourceGroup to which this element belongs
 * Can be NULL (if it belongs to playsink for example) */
static GstSourceGroup *
find_source_group_owner (GstPlayBin3 * playbin, GstObject * element)
{
  if (playbin->curr_group->uridecodebin
      && gst_object_has_as_ancestor (element,
          GST_OBJECT_CAST (playbin->curr_group->uridecodebin)))
    return playbin->curr_group;
  if (playbin->next_group->uridecodebin
      && gst_object_has_as_ancestor (element,
          GST_OBJECT_CAST (playbin->next_group->uridecodebin)))
    return playbin->next_group;
  return NULL;
}

static void
gst_play_bin3_handle_message (GstBin * bin, GstMessage * msg)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (bin);
  gboolean do_reset_time = FALSE;

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_STREAM_START) {
    GstSourceGroup *group = NULL, *other_group = NULL;
    gboolean changed = FALSE;
    guint group_id;
    GstMessage *buffering_msg;

    if (!gst_message_parse_group_id (msg, &group_id)) {
      GST_ERROR_OBJECT (bin,
          "Could not get group_id from STREAM_START message !");
      goto beach;
    }
    GST_DEBUG_OBJECT (bin, "STREAM_START group_id:%u", group_id);

    /* Figure out to which group this group_id corresponds */
    GST_PLAY_BIN3_LOCK (playbin);
    if (playbin->groups[0].group_id == group_id) {
      group = &playbin->groups[0];
      other_group = &playbin->groups[1];
    } else if (playbin->groups[1].group_id == group_id) {
      group = &playbin->groups[1];
      other_group = &playbin->groups[0];
    }
    if (group == NULL) {
      GST_ERROR_OBJECT (bin, "group_id %u is not provided by any group !",
          group_id);
      GST_PLAY_BIN3_UNLOCK (playbin);
      goto beach;
    }

    debug_groups (playbin);

    /* Do the switch now ! */
    playbin->curr_group = group;
    playbin->next_group = other_group;

    /* we may need to serialise a buffering
     * message, and need to take that lock
     * before any source group lock, so
     * do that now */
    g_mutex_lock (&playbin->buffering_post_lock);

    GST_SOURCE_GROUP_LOCK (group);
    if (group->playing == FALSE)
      changed = TRUE;
    group->playing = TRUE;

    buffering_msg = group->pending_buffering_msg;
    group->pending_buffering_msg = NULL;

    GST_SOURCE_GROUP_UNLOCK (group);

    GST_SOURCE_GROUP_LOCK (other_group);
    other_group->playing = FALSE;
    GST_SOURCE_GROUP_UNLOCK (other_group);

    debug_groups (playbin);
    GST_PLAY_BIN3_UNLOCK (playbin);
    if (changed)
      gst_play_bin3_check_group_status (playbin);
    else
      GST_DEBUG_OBJECT (bin, "Groups didn't changed");

    /* If there was a pending buffering message to send, do it now */
    if (buffering_msg)
      GST_BIN_CLASS (parent_class)->handle_message (bin, buffering_msg);

    g_mutex_unlock (&playbin->buffering_post_lock);

  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_BUFFERING) {
    GstSourceGroup *group;

    /* Only post buffering messages for group which is currently playing */
    GST_PLAY_BIN3_LOCK (playbin);
    group = find_source_group_owner (playbin, msg->src);
    if (group->active) {
      g_mutex_lock (&playbin->buffering_post_lock);

      GST_SOURCE_GROUP_LOCK (group);
      GST_PLAY_BIN3_UNLOCK (playbin);

      if (!group->playing) {
        GST_DEBUG_OBJECT (playbin,
            "Storing buffering message from pending group " "%p %"
            GST_PTR_FORMAT, group, msg);
        gst_message_replace (&group->pending_buffering_msg, msg);
        gst_message_unref (msg);
        msg = NULL;
      } else {
        /* Ensure there's no cached buffering message for this group */
        gst_message_replace (&group->pending_buffering_msg, NULL);
      }
      GST_SOURCE_GROUP_UNLOCK (group);

      if (msg != NULL) {
        GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
        msg = NULL;
      }
      g_mutex_unlock (&playbin->buffering_post_lock);
    } else {
      GST_PLAY_BIN3_UNLOCK (playbin);
    }
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_STREAM_COLLECTION) {
    GstStreamCollection *collection = NULL;

    gst_message_parse_stream_collection (msg, &collection);

    if (collection) {
      gboolean pstate = playbin->do_stream_selections;
      GstSourceGroup *target_group = NULL;

      GST_PLAY_BIN3_LOCK (playbin);
      GST_DEBUG_OBJECT (playbin,
          "STREAM_COLLECTION: Got a collection from %" GST_PTR_FORMAT,
          msg->src);
      target_group = find_source_group_owner (playbin, msg->src);
      if (target_group)
        gst_object_replace ((GstObject **) & target_group->collection,
            (GstObject *) collection);
      /* FIXME: Only do the following if it's the current group? */
      if (target_group == playbin->curr_group)
        update_combiner_info (playbin, target_group->collection);
      if (pstate)
        playbin->do_stream_selections = FALSE;
      do_stream_selection (playbin, target_group);
      if (pstate)
        playbin->do_stream_selections = TRUE;
      GST_PLAY_BIN3_UNLOCK (playbin);

      gst_object_unref (collection);
    }
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_RESET_TIME) {
    if (playbin->is_live && GST_STATE_TARGET (playbin) == GST_STATE_PLAYING) {
      do_reset_time = TRUE;
    }
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_STREAMS_SELECTED) {
    GstSourceGroup *target_group;

    GST_PLAY_BIN3_LOCK (playbin);

    target_group = find_source_group_owner (playbin, msg->src);
    if (target_group) {
      GstStreamType selected_types = 0;
      guint i, nb;
      nb = gst_message_streams_selected_get_size (msg);
      for (i = 0; i < nb; i++) {
        GstStream *stream = gst_message_streams_selected_get_stream (msg, i);
        selected_types |= gst_stream_get_stream_type (stream);
        gst_object_unref (stream);
      }
      target_group->selected_stream_types = selected_types;
      playbin->selected_stream_types =
          playbin->groups[0].selected_stream_types | playbin->groups[1].
          selected_stream_types;
      if (playbin->active_stream_types != playbin->selected_stream_types) {
        GST_DEBUG_OBJECT (playbin,
            "selected stream types changed, reconfiguring output");
        reconfigure_output (playbin);
      }
    }
    GST_PLAY_BIN3_UNLOCK (playbin);
  }

beach:
  if (msg)
    GST_BIN_CLASS (parent_class)->handle_message (bin, msg);

  if (do_reset_time) {
    /* If we are live, sample a new base_time immediately */
    gst_element_change_state (GST_ELEMENT (playbin),
        GST_STATE_CHANGE_PAUSED_TO_PLAYING);
  }
}

static void
gst_play_bin3_deep_element_added (GstBin * playbin, GstBin * sub_bin,
    GstElement * child)
{
  GST_LOG_OBJECT (playbin, "element %" GST_PTR_FORMAT " was added to "
      "%" GST_PTR_FORMAT, child, sub_bin);

  g_signal_emit (playbin, gst_play_bin3_signals[SIGNAL_ELEMENT_SETUP], 0,
      child);

  GST_BIN_CLASS (parent_class)->deep_element_added (playbin, sub_bin, child);
}

/* Returns current stream number, or -1 if none has been selected yet */
static int
get_current_stream_number (GstPlayBin3 * playbin, GstSourceCombine * combine)
{
  /* Internal API cleanup would make this easier... */
  int i;
  GstPad *pad, *current;
  GstObject *combiner = NULL;
  int ret = -1;

  if (!combine->has_active_pad) {
    GST_WARNING_OBJECT (playbin,
        "combiner doesn't have the \"active-pad\" property");
    return ret;
  }

  for (i = 0; i < combine->inputpads->len; i++) {
    pad = g_ptr_array_index (combine->inputpads, i);
    if ((combiner = gst_pad_get_parent (pad))) {
      g_object_get (combiner, "active-pad", &current, NULL);
      gst_object_unref (combiner);

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

static void
combiner_active_pad_changed (GObject * combiner, GParamSpec * pspec,
    GstPlayBin3 * playbin)
{
  GstSourceCombine *combine = NULL;
  int i;

  GST_PLAY_BIN3_LOCK (playbin);

  for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
    if (combiner == G_OBJECT (playbin->combiner[i].combiner)) {
      combine = &playbin->combiner[i];
    }
  }

  /* We got a pad-change after our group got switched out; no need to notify */
  if (!combine) {
    GST_PLAY_BIN3_UNLOCK (playbin);
    return;
  }

  switch (combine->stream_type) {
    case GST_STREAM_TYPE_VIDEO:
      playbin->current_video = get_current_stream_number (playbin, combine);

      if (playbin->video_pending_flush_finish) {
        playbin->video_pending_flush_finish = FALSE;
        GST_PLAY_BIN3_UNLOCK (playbin);
        gst_play_bin3_send_custom_event (GST_OBJECT (combiner),
            "playsink-custom-video-flush-finish");
      }
      break;
    case GST_STREAM_TYPE_AUDIO:
      playbin->current_audio = get_current_stream_number (playbin, combine);

      if (playbin->audio_pending_flush_finish) {
        playbin->audio_pending_flush_finish = FALSE;
        GST_PLAY_BIN3_UNLOCK (playbin);
        gst_play_bin3_send_custom_event (GST_OBJECT (combiner),
            "playsink-custom-audio-flush-finish");
      }
      break;
    case GST_STREAM_TYPE_TEXT:
      playbin->current_text = get_current_stream_number (playbin, combine);

      if (playbin->text_pending_flush_finish) {
        playbin->text_pending_flush_finish = FALSE;
        GST_PLAY_BIN3_UNLOCK (playbin);
        gst_play_bin3_send_custom_event (GST_OBJECT (combiner),
            "playsink-custom-subtitle-flush-finish");
      }
      break;
    default:
      break;
  }
  GST_PLAY_BIN3_UNLOCK (playbin);
}

static GstCaps *
update_video_multiview_caps (GstPlayBin3 * playbin, GstCaps * caps)
{
  GstVideoMultiviewMode mv_mode;
  GstVideoMultiviewMode cur_mv_mode;
  guint mv_flags, cur_mv_flags;
  GstStructure *s;
  const gchar *mview_mode_str;
  GstCaps *out_caps;

  GST_OBJECT_LOCK (playbin);
  mv_mode = (GstVideoMultiviewMode) playbin->multiview_mode;
  mv_flags = playbin->multiview_flags;
  GST_OBJECT_UNLOCK (playbin);

  if (mv_mode == GST_VIDEO_MULTIVIEW_MODE_NONE)
    return NULL;

  cur_mv_mode = GST_VIDEO_MULTIVIEW_MODE_NONE;
  cur_mv_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;

  s = gst_caps_get_structure (caps, 0);

  gst_structure_get_flagset (s, "multiview-flags", &cur_mv_flags, NULL);
  if ((mview_mode_str = gst_structure_get_string (s, "multiview-mode")))
    cur_mv_mode = gst_video_multiview_mode_from_caps_string (mview_mode_str);

  /* We can't override an existing annotated multiview mode, except
   * maybe (in the future) we could change some flags. */
  if ((gint) cur_mv_mode > GST_VIDEO_MULTIVIEW_MAX_FRAME_PACKING) {
    GST_INFO_OBJECT (playbin, "Cannot override existing multiview mode");
    return NULL;
  }

  mview_mode_str = gst_video_multiview_mode_to_caps_string (mv_mode);
  g_assert (mview_mode_str != NULL);
  out_caps = gst_caps_copy (caps);
  s = gst_caps_get_structure (out_caps, 0);

  gst_structure_set (s, "multiview-mode", G_TYPE_STRING, mview_mode_str,
      "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, mv_flags,
      GST_FLAG_SET_MASK_EXACT, NULL);

  return out_caps;
}

static void
emit_about_to_finish (GstPlayBin3 * playbin)
{
  GST_DEBUG_OBJECT (playbin, "Emitting about-to-finish");

  /* after this call, we should have a next group to activate or we EOS */
  g_signal_emit (G_OBJECT (playbin),
      gst_play_bin3_signals[SIGNAL_ABOUT_TO_FINISH], 0, NULL);

  debug_groups (playbin);

  /* now activate the next group. If the app did not set a uri, this will
   * fail and we can do EOS */
  setup_next_source (playbin);
}

static SourcePad *
find_source_pad (GstSourceGroup * group, GstPad * target)
{
  GList *tmp;

  for (tmp = group->source_pads; tmp; tmp = tmp->next) {
    SourcePad *res = (SourcePad *) tmp->data;
    if (res->pad == target)
      return res;
  }
  return NULL;
}

static GstPadProbeReturn
_decodebin_event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer udata)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstSourceGroup *group = (GstSourceGroup *) udata;
  GstPlayBin3 *playbin = group->playbin;
  GstEvent *event = GST_PAD_PROBE_INFO_DATA (info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps = NULL;
      const GstStructure *s;
      const gchar *name;

      gst_event_parse_caps (event, &caps);
      /* If video caps, check if we should override multiview flags */
      s = gst_caps_get_structure (caps, 0);
      name = gst_structure_get_name (s);
      if (g_str_has_prefix (name, "video/")) {
        caps = update_video_multiview_caps (playbin, caps);
        if (caps) {
          gst_event_unref (event);
          event = gst_event_new_caps (caps);
          GST_PAD_PROBE_INFO_DATA (info) = event;
          gst_caps_unref (caps);
        }
      }
      break;
    }
    case GST_EVENT_STREAM_START:
    {
      guint group_id;
      if (gst_event_parse_group_id (event, &group_id)) {
        GST_LOG_OBJECT (pad, "STREAM_START group_id:%u", group_id);
        if (group->group_id == GST_GROUP_ID_INVALID)
          group->group_id = group_id;
        else if (group->group_id != group_id) {
          GST_DEBUG_OBJECT (pad, "group_id changing from %u to %u",
              group->group_id, group_id);
          group->group_id = group_id;
        }
      }
      break;
    }
    default:
      break;
  }

  return ret;
}

static void
control_source_pad (GstSourceGroup * group, GstPad * pad,
    GstPad * combine_pad, GstStreamType stream_type)
{
  SourcePad *sourcepad = g_slice_new0 (SourcePad);

  sourcepad->pad = pad;
  sourcepad->event_probe_id =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      _decodebin_event_probe, group, NULL);
  sourcepad->stream_type = stream_type;
  sourcepad->combine_sinkpad = combine_pad;
  group->source_pads = g_list_append (group->source_pads, sourcepad);
}

static void
remove_combiner (GstPlayBin3 * playbin, GstSourceCombine * combine)
{
  gint n;

  if (combine->combiner == NULL) {
    GST_DEBUG_OBJECT (playbin, "No combiner element to remove");
    return;
  }

  /* Go over all sink pads and release them ! */
  for (n = 0; n < combine->inputpads->len; n++) {
    GstPad *sinkpad = g_ptr_array_index (combine->inputpads, n);

    gst_element_release_request_pad (combine->combiner, sinkpad);
  }
  g_ptr_array_set_size (combine->inputpads, 0);

  gst_element_set_state (combine->combiner, GST_STATE_NULL);
  gst_bin_remove (GST_BIN_CAST (playbin), combine->combiner);
  combine->combiner = NULL;

}

/* Create the combiner element if needed for the given combine */
static void
create_combiner (GstPlayBin3 * playbin, GstSourceCombine * combine)
{
  GstElement *custom_combiner = NULL;

  if (combine->combiner) {
    GST_WARNING_OBJECT (playbin, "Combiner element already exists!");
    return;
  }

  if (combine->stream_type == GST_STREAM_TYPE_VIDEO)
    custom_combiner = playbin->video_stream_combiner;
  else if (combine->stream_type == GST_STREAM_TYPE_AUDIO)
    custom_combiner = playbin->audio_stream_combiner;
  else if (combine->stream_type == GST_STREAM_TYPE_TEXT)
    custom_combiner = playbin->text_stream_combiner;

  combine->combiner = custom_combiner;

  if (!combine->combiner) {
    gchar *concat_name;
    GST_DEBUG_OBJECT (playbin,
        "No custom combiner requested, using 'concat' element");
    concat_name =
        g_strdup_printf ("%s-concat",
        gst_stream_type_get_name (combine->stream_type));
    combine->combiner = gst_element_factory_make ("concat", concat_name);
    g_object_set (combine->combiner, "adjust-base", FALSE, NULL);
    g_free (concat_name);
    combine->is_concat = TRUE;
  }

  combine->srcpad = gst_element_get_static_pad (combine->combiner, "src");

  /* We only want to use 'active-pad' if it's a regular combiner that
   * will consume all streams, and not concat (which is just used for
   * gapless) */
  if (!combine->is_concat) {
    combine->has_active_pad =
        g_object_class_find_property (G_OBJECT_GET_CLASS (combine->combiner),
        "active-pad") != NULL;

    if (combine->has_active_pad)
      g_signal_connect (combine->combiner, "notify::active-pad",
          G_CALLBACK (combiner_active_pad_changed), playbin);
  }

  GST_DEBUG_OBJECT (playbin, "adding new stream combiner %" GST_PTR_FORMAT,
      combine->combiner);
  gst_bin_add (GST_BIN_CAST (playbin), combine->combiner);
  gst_element_sync_state_with_parent (combine->combiner);
}

static GstPad *
combiner_control_pad (GstPlayBin3 * playbin, GstSourceCombine * combine,
    GstPad * srcpad)
{
  GstPadLinkReturn res;
  GstPad *combine_pad = NULL;

  GST_DEBUG_OBJECT (playbin, "srcpad %" GST_PTR_FORMAT, srcpad);

  if (combine->combiner) {
    GstPad *sinkpad =
        gst_element_request_pad_simple (combine->combiner, "sink_%u");

    if (sinkpad == NULL)
      goto request_pad_failed;

    GST_DEBUG_OBJECT (playbin, "Got new combiner pad %" GST_PTR_FORMAT,
        sinkpad);

    /* store the pad in the array */
    GST_DEBUG_OBJECT (playbin, "pad %" GST_PTR_FORMAT " added to array",
        sinkpad);
    g_ptr_array_add (combine->inputpads, sinkpad);

    res = gst_pad_link (srcpad, sinkpad);
    if (GST_PAD_LINK_FAILED (res))
      goto failed_combiner_link;

    GST_DEBUG_OBJECT (playbin,
        "linked pad %" GST_PTR_FORMAT " to combiner %" GST_PTR_FORMAT, srcpad,
        combine->combiner);
    combine_pad = sinkpad;
  } else {
    GST_LOG_OBJECT (playbin, "combine->sinkpad:%" GST_PTR_FORMAT,
        combine->sinkpad);
    g_assert (combine->sinkpad != NULL);
    /* Connect directly to playsink */
    if (gst_pad_is_linked (combine->sinkpad))
      goto sinkpad_already_linked;

    GST_DEBUG_OBJECT (playbin, "Linking new pad straight to playsink");
    res = gst_pad_link (srcpad, combine->sinkpad);

    if (res != GST_PAD_LINK_OK)
      goto failed_sinkpad_link;
  }

  return combine_pad;

  /* Failure cases */
request_pad_failed:
  GST_ELEMENT_ERROR (playbin, CORE, PAD,
      ("Internal playbin error."),
      ("Failed to get request pad from combiner %p.", combine->combiner));
  return NULL;


sinkpad_already_linked:
  GST_ELEMENT_ERROR (playbin, CORE, PAD,
      ("Internal playbin error."), ("playsink pad already used !"));
  return NULL;

failed_sinkpad_link:
  GST_ELEMENT_ERROR (playbin, CORE, PAD,
      ("Internal playbin error."),
      ("Failed to link pad to sink. Error %d", res));
  return NULL;

failed_combiner_link:
  GST_ELEMENT_ERROR (playbin, CORE, PAD,
      ("Internal playbin error."),
      ("Failed to link pad to combiner. Error %d", res));
  return NULL;
}


/* Call after pad was unlinked from (potential) combiner */
static void
release_source_pad (GstPlayBin3 * playbin, GstSourceGroup * group,
    GstSourceCombine * combine, GstPad * pad)
{
  SourcePad *sourcepad;
  GList *tmp;
  GstStreamType alltype = 0;

  sourcepad = find_source_pad (group, pad);
  if (!sourcepad) {
    GST_DEBUG_OBJECT (playbin, "Not a pad controlled by us ?");
    return;
  }

  if (sourcepad->event_probe_id) {
    gst_pad_remove_probe (pad, sourcepad->event_probe_id);
    sourcepad->event_probe_id = 0;
  }

  if (sourcepad->combine_sinkpad) {
    gst_element_release_request_pad (combine->combiner,
        sourcepad->combine_sinkpad);
    g_ptr_array_remove (combine->inputpads, sourcepad->combine_sinkpad);
  }

  /* Remove from list of controlled pads and check again for EOS status */
  group->source_pads = g_list_remove (group->source_pads, sourcepad);
  g_slice_free (SourcePad, sourcepad);

  /* Update present stream types */
  for (tmp = group->source_pads; tmp; tmp = tmp->next) {
    SourcePad *cand = (SourcePad *) tmp->data;
    alltype |= cand->stream_type;
  }
  group->present_stream_types = alltype;
}

/* this function is called when a new pad is added to decodebin. We check the
 * type of the pad and add it to the combiner element
 */
static void
pad_added_cb (GstElement * uridecodebin, GstPad * pad, GstSourceGroup * group)
{
  GstSourceCombine *combine = NULL;
  gint pb_stream_type = -1;
  gchar *pad_name;
  GstPlayBin3 *playbin = group->playbin;
  GstPad *combine_pad;
  GstStreamType selected, active, cur;

  GST_PLAY_BIN3_SHUTDOWN_LOCK (playbin, shutdown);

  pad_name = gst_object_get_name (GST_OBJECT (pad));

  GST_DEBUG_OBJECT (playbin, "decoded pad %s:%s added",
      GST_DEBUG_PAD_NAME (pad));

  /* major type of the pad, this determines the combiner to use,
     try exact match first */
  if (g_str_has_prefix (pad_name, "video")) {
    pb_stream_type = PLAYBIN_STREAM_VIDEO;
    cur = GST_STREAM_TYPE_VIDEO;
  } else if (g_str_has_prefix (pad_name, "audio")) {
    pb_stream_type = PLAYBIN_STREAM_AUDIO;
    cur = GST_STREAM_TYPE_AUDIO;
  } else if (g_str_has_prefix (pad_name, "text")) {
    pb_stream_type = PLAYBIN_STREAM_TEXT;
    cur = GST_STREAM_TYPE_TEXT;
  }

  g_free (pad_name);

  /* no stream type found for the media type, don't bother linking it to a
   * combiner. This will leave the pad unlinked and thus ignored. */
  if (pb_stream_type < 0) {
    GST_PLAY_BIN3_SHUTDOWN_UNLOCK (playbin);
    goto unknown_type;
  }

  GST_PLAY_BIN3_LOCK (playbin);
  combine = &playbin->combiner[pb_stream_type];

  /* (uri)decodebin3 will post streams-selected once all pads are expose.
   * Therefore this stream might not be marked as selected on pad-added,
   * and associated combiner can be null here.
   * Marks this stream as selected manually, exposed pad implies it's selected
   * already */
  selected = playbin->selected_stream_types | cur;
  active = playbin->active_stream_types;

  if (selected != active) {
    GST_DEBUG_OBJECT (playbin,
        "%s:%s added but not an active stream, marking active",
        GST_DEBUG_PAD_NAME (pad));
    playbin->selected_stream_types = selected;
    reconfigure_output (playbin);

    /* shutdown state can be changed meantime then combiner will not be
     * configured */
    if (g_atomic_int_get (&playbin->shutdown)) {
      GST_PLAY_BIN3_UNLOCK (playbin);
      GST_PLAY_BIN3_SHUTDOWN_UNLOCK (playbin);
      return;
    }
  }

  combine_pad = combiner_control_pad (playbin, combine, pad);

  control_source_pad (group, pad, combine_pad, combine->stream_type);

  /* Update present stream_types and check whether we should post a pending about-to-finish */
  group->present_stream_types |= combine->stream_type;

  if (group->playing && group->pending_about_to_finish
      && group->present_stream_types == group->selected_stream_types) {
    group->pending_about_to_finish = FALSE;
    emit_about_to_finish (playbin);
  }
  GST_PLAY_BIN3_UNLOCK (playbin);

  GST_PLAY_BIN3_SHUTDOWN_UNLOCK (playbin);

  return;

  /* ERRORS */
unknown_type:
  GST_DEBUG_OBJECT (playbin, "Ignoring pad with unknown type");
  return;

shutdown:
  {
    GST_DEBUG ("ignoring, we are shutting down. Pad will be left unlinked");
    /* not going to done as we didn't request the caps */
    return;
  }
}

/* called when a pad is removed from the decodebin. We unlink the pad from
 * the combiner. */
static void
pad_removed_cb (GstElement * decodebin, GstPad * pad, GstSourceGroup * group)
{
  GstSourceCombine *combine;
  GstPlayBin3 *playbin = group->playbin;

  GST_DEBUG_OBJECT (playbin,
      "decoded pad %s:%s removed", GST_DEBUG_PAD_NAME (pad));

  GST_PLAY_BIN3_LOCK (playbin);

  /* Get combiner for pad */
  if (g_str_has_prefix (GST_PAD_NAME (pad), "video"))
    combine = &playbin->combiner[PLAYBIN_STREAM_VIDEO];
  else if (g_str_has_prefix (GST_PAD_NAME (pad), "audio"))
    combine = &playbin->combiner[PLAYBIN_STREAM_AUDIO];
  else if (g_str_has_prefix (GST_PAD_NAME (pad), "text"))
    combine = &playbin->combiner[PLAYBIN_STREAM_TEXT];
  else
    goto done;

  release_source_pad (playbin, group, combine, pad);

done:
  GST_PLAY_BIN3_UNLOCK (playbin);
}


static gint
select_stream_cb (GstElement * decodebin, GstStreamCollection * collection,
    GstStream * stream, GstSourceGroup * group)
{
  GstStreamType stype = gst_stream_get_stream_type (stream);
  GstElement *combiner = NULL;
  GstPlayBin3 *playbin = group->playbin;

  if (stype & GST_STREAM_TYPE_AUDIO)
    combiner = playbin->audio_stream_combiner;
  else if (stype & GST_STREAM_TYPE_VIDEO)
    combiner = playbin->video_stream_combiner;
  else if (stype & GST_STREAM_TYPE_TEXT)
    combiner = playbin->text_stream_combiner;

  if (combiner) {
    GST_DEBUG_OBJECT (playbin, "Got a combiner, requesting stream activation");
    return 1;
  }

  /* Let decodebin3 decide otherwise */
  return -1;
}

/* We get called when the selected stream types change and
 * reconfiguration of output (i.e. playsink and potential combiners)
 * are required.
 */
static void
reconfigure_output (GstPlayBin3 * playbin)
{
  GstPadLinkReturn res;
  gint i;

  g_assert (playbin->selected_stream_types != playbin->active_stream_types);

  GST_DEBUG_OBJECT (playbin, "selected_stream_types : %" STREAM_TYPES_FORMAT,
      STREAM_TYPES_ARGS (playbin->selected_stream_types));
  GST_DEBUG_OBJECT (playbin, "active_stream_types : %" STREAM_TYPES_FORMAT,
      STREAM_TYPES_ARGS (playbin->active_stream_types));

  GST_PLAY_BIN3_LOCK (playbin);

  /* Make sure combiners/playsink are in sync with selected stream types */
  for (i = 0; i < PLAYBIN_STREAM_LAST; i++) {
    GstSourceCombine *combine = &playbin->combiner[i];
    gboolean is_selected =
        (combine->stream_type & playbin->selected_stream_types) ==
        combine->stream_type;
    gboolean is_active =
        (combine->stream_type & playbin->active_stream_types) ==
        combine->stream_type;

    GST_DEBUG_OBJECT (playbin, "Stream type status: '%s' %s %s",
        gst_stream_type_get_name (combine->stream_type),
        is_selected ? "selected" : "NOT selected",
        is_active ? "active" : "NOT active");
    /* FIXME : Remove asserts below once enough testing has been done */

    if (is_selected && is_active) {
      GST_DEBUG_OBJECT (playbin, "Stream type '%s' already active",
          gst_stream_type_get_name (combine->stream_type));
    } else if (is_active && !is_selected) {
      GST_DEBUG_OBJECT (playbin, "Stream type '%s' is no longer requested",
          gst_stream_type_get_name (combine->stream_type));

      /* Unlink combiner from sink */
      if (combine->srcpad) {
        GST_LOG_OBJECT (playbin, "Unlinking from sink");
        if (combine->sinkpad)
          gst_pad_unlink (combine->srcpad, combine->sinkpad);
        gst_object_unref (combine->srcpad);
        combine->srcpad = NULL;
      }

      if (combine->sinkpad) {
        /* Release playsink sink pad */
        GST_LOG_OBJECT (playbin, "Releasing playsink pad");
        gst_play_sink_release_pad (playbin->playsink, combine->sinkpad);
        gst_object_unref (combine->sinkpad);
        combine->sinkpad = NULL;
      }

      /* Release combiner */
      GST_FIXME_OBJECT (playbin, "Release combiner");
      remove_combiner (playbin, combine);
    } else if (!is_active && is_selected) {
      GST_DEBUG_OBJECT (playbin, "Stream type '%s' is now requested",
          gst_stream_type_get_name (combine->stream_type));

      /* If we are shutting down, do *not* add more combiners */
      if (g_atomic_int_get (&playbin->shutdown))
        continue;

      g_assert (combine->sinkpad == NULL);

      /* Request playsink sink pad */
      combine->sinkpad =
          gst_play_sink_request_pad (playbin->playsink,
          gst_play_sink_type_from_stream_type (combine->stream_type));
      gst_object_ref (combine->sinkpad);
      /* Create combiner if needed and link it */
      create_combiner (playbin, combine);
      if (combine->combiner) {
        res = gst_pad_link (combine->srcpad, combine->sinkpad);
        GST_DEBUG_OBJECT (playbin, "linked type %s, result: %d",
            gst_stream_type_get_name (combine->stream_type), res);
        if (res != GST_PAD_LINK_OK) {
          GST_ELEMENT_ERROR (playbin, CORE, PAD,
              ("Internal playbin error."),
              ("Failed to link combiner to sink. Error %d", res));
        }

      }
    }
  }

  playbin->active_stream_types = playbin->selected_stream_types;

  GST_PLAY_BIN3_UNLOCK (playbin);

  gst_play_sink_reconfigure (playbin->playsink);

  do_async_done (playbin);

  GST_DEBUG_OBJECT (playbin, "selected_stream_types : %" STREAM_TYPES_FORMAT,
      STREAM_TYPES_ARGS (playbin->selected_stream_types));
  GST_DEBUG_OBJECT (playbin, "active_stream_types : %" STREAM_TYPES_FORMAT,
      STREAM_TYPES_ARGS (playbin->active_stream_types));

  return;
}

static void
about_to_finish_cb (GstElement * uridecodebin, GstSourceGroup * group)
{
  GstPlayBin3 *playbin = group->playbin;
  GST_DEBUG_OBJECT (playbin, "about to finish in group %p", group);

  GST_LOG_OBJECT (playbin, "selected_stream_types:%" STREAM_TYPES_FORMAT,
      STREAM_TYPES_ARGS (group->selected_stream_types));
  GST_LOG_OBJECT (playbin, "present_stream_types:%" STREAM_TYPES_FORMAT,
      STREAM_TYPES_ARGS (group->present_stream_types));

  if (group->selected_stream_types == 0
      || (group->selected_stream_types != group->present_stream_types)) {
    GST_LOG_OBJECT (playbin,
        "Delaying emission of signal until this group is ready");
    group->pending_about_to_finish = TRUE;
  } else
    emit_about_to_finish (playbin);
}

static GstBusSyncReply
activate_sink_bus_handler (GstBus * bus, GstMessage * msg,
    GstPlayBin3 * playbin)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    /* Only proxy errors from a fixed sink. If that fails we can just error out
     * early as stuff will fail later anyway */
    if (playbin->audio_sink
        && gst_object_has_as_ancestor (GST_MESSAGE_SRC (msg),
            GST_OBJECT_CAST (playbin->audio_sink)))
      gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);
    else if (playbin->video_sink
        && gst_object_has_as_ancestor (GST_MESSAGE_SRC (msg),
            GST_OBJECT_CAST (playbin->video_sink)))
      gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);
    else if (playbin->text_sink
        && gst_object_has_as_ancestor (GST_MESSAGE_SRC (msg),
            GST_OBJECT_CAST (playbin->text_sink)))
      gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);
    else
      gst_message_unref (msg);
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_HAVE_CONTEXT) {
    GstContext *context;

    gst_message_parse_have_context (msg, &context);
    gst_element_set_context (GST_ELEMENT_CAST (playbin), context);
    gst_context_unref (context);
    gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);
  } else {
    gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);
  }

  /* Doesn't really matter, nothing is using this bus */
  return GST_BUS_DROP;
}

static gboolean
activate_sink (GstPlayBin3 * playbin, GstElement * sink, gboolean * activated)
{
  GstState state;
  GstBus *bus = NULL;
  GstStateChangeReturn sret;
  gboolean ret = FALSE;

  if (activated)
    *activated = FALSE;

  GST_OBJECT_LOCK (sink);
  state = GST_STATE (sink);
  GST_OBJECT_UNLOCK (sink);
  if (state >= GST_STATE_READY) {
    ret = TRUE;
    goto done;
  }

  if (!GST_OBJECT_PARENT (sink)) {
    bus = gst_bus_new ();
    gst_bus_set_sync_handler (bus,
        (GstBusSyncHandler) activate_sink_bus_handler, playbin, NULL);
    gst_element_set_bus (sink, bus);
  }

  sret = gst_element_set_state (sink, GST_STATE_READY);
  if (sret == GST_STATE_CHANGE_FAILURE)
    goto done;

  if (activated)
    *activated = TRUE;
  ret = TRUE;

done:
  if (bus) {
    gst_element_set_bus (sink, NULL);
    gst_object_unref (bus);
  }

  return ret;
}

/* must be called with the group lock */
static gboolean
group_set_locked_state_unlocked (GstPlayBin3 * playbin, GstSourceGroup * group,
    gboolean locked)
{
  GST_DEBUG_OBJECT (playbin, "locked_state %d on group %p", locked, group);

  if (group->uridecodebin)
    gst_element_set_locked_state (group->uridecodebin, locked);

  return TRUE;
}

static gboolean
make_or_reuse_element (GstPlayBin3 * playbin, const gchar * name,
    GstElement ** elem)
{
  if (*elem) {
    GST_DEBUG_OBJECT (playbin, "reusing existing %s", name);
    gst_element_set_state (*elem, GST_STATE_READY);
    /* no need to take extra ref, we already have one
     * and the bin will add one since it is no longer floating,
     * as we added a non-floating ref when removing it from the
     * bin earlier */
  } else {
    GstElement *new_elem;
    GST_DEBUG_OBJECT (playbin, "making new %s", name);
    new_elem = gst_element_factory_make (name, NULL);
    if (!new_elem)
      return FALSE;
    *elem = gst_object_ref (new_elem);
  }

  if (GST_OBJECT_PARENT (*elem) != GST_OBJECT_CAST (playbin))
    gst_bin_add (GST_BIN_CAST (playbin), *elem);
  return TRUE;
}


static void
source_setup_cb (GstElement * element, GstElement * source,
    GstSourceGroup * group)
{
  g_signal_emit (group->playbin, gst_play_bin3_signals[SIGNAL_SOURCE_SETUP], 0,
      source);
}

/* must be called with PLAY_BIN_LOCK */
static GstStateChangeReturn
activate_group (GstPlayBin3 * playbin, GstSourceGroup * group)
{
  GstElement *uridecodebin = NULL;
  GstPlayFlags flags;
  gboolean audio_sink_activated = FALSE;
  gboolean video_sink_activated = FALSE;
  gboolean text_sink_activated = FALSE;
  GstStateChangeReturn state_ret;

  g_return_val_if_fail (group->valid, GST_STATE_CHANGE_FAILURE);
  g_return_val_if_fail (!group->active, GST_STATE_CHANGE_FAILURE);

  GST_DEBUG_OBJECT (playbin, "activating group %p", group);

  GST_SOURCE_GROUP_LOCK (group);

  /* First set up the custom sinks */
  if (playbin->audio_sink)
    group->audio_sink = gst_object_ref (playbin->audio_sink);
  else
    group->audio_sink =
        gst_play_sink_get_sink (playbin->playsink, GST_PLAY_SINK_TYPE_AUDIO);

  if (group->audio_sink) {
    if (!activate_sink (playbin, group->audio_sink, &audio_sink_activated)) {
      if (group->audio_sink == playbin->audio_sink) {
        goto sink_failure;
      } else {
        gst_object_unref (group->audio_sink);
        group->audio_sink = NULL;
      }
    }
  }

  if (playbin->video_sink)
    group->video_sink = gst_object_ref (playbin->video_sink);
  else
    group->video_sink =
        gst_play_sink_get_sink (playbin->playsink, GST_PLAY_SINK_TYPE_VIDEO);

  if (group->video_sink) {
    if (!activate_sink (playbin, group->video_sink, &video_sink_activated)) {
      if (group->video_sink == playbin->video_sink) {
        goto sink_failure;
      } else {
        gst_object_unref (group->video_sink);
        group->video_sink = NULL;
      }
    }
  }

  if (playbin->text_sink)
    group->text_sink = gst_object_ref (playbin->text_sink);
  else
    group->text_sink =
        gst_play_sink_get_sink (playbin->playsink, GST_PLAY_SINK_TYPE_TEXT);

  if (group->text_sink) {
    if (!activate_sink (playbin, group->text_sink, &text_sink_activated)) {
      if (group->text_sink == playbin->text_sink) {
        goto sink_failure;
      } else {
        gst_object_unref (group->text_sink);
        group->text_sink = NULL;
      }
    }
  }


  if (!make_or_reuse_element (playbin, "uridecodebin3", &group->uridecodebin))
    goto no_uridecodebin;
  uridecodebin = group->uridecodebin;

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

  group->pad_added_id = g_signal_connect (uridecodebin, "pad-added",
      G_CALLBACK (pad_added_cb), group);
  group->pad_removed_id = g_signal_connect (uridecodebin,
      "pad-removed", G_CALLBACK (pad_removed_cb), group);
  group->select_stream_id = g_signal_connect (uridecodebin, "select-stream",
      G_CALLBACK (select_stream_cb), group);
  group->source_setup_id = g_signal_connect (uridecodebin, "source-setup",
      G_CALLBACK (source_setup_cb), group);
  group->about_to_finish_id =
      g_signal_connect (uridecodebin, "about-to-finish",
      G_CALLBACK (about_to_finish_cb), group);

  if (group->suburi)
    g_object_set (group->uridecodebin, "suburi", group->suburi, NULL);

  /* release the group lock before setting the state of the source bins, they
   * might fire signals in this thread that we need to handle with the
   * group_lock taken. */
  GST_SOURCE_GROUP_UNLOCK (group);

  if ((state_ret =
          gst_element_set_state (uridecodebin,
              GST_STATE_PAUSED)) == GST_STATE_CHANGE_FAILURE)
    goto uridecodebin_failure;

  GST_SOURCE_GROUP_LOCK (group);
  /* allow state changes of the playbin affect the group elements now */
  group_set_locked_state_unlocked (playbin, group, FALSE);
  group->active = TRUE;
  GST_SOURCE_GROUP_UNLOCK (group);

  return state_ret;

  /* ERRORS */
no_uridecodebin:
  {
    GstMessage *msg;

    GST_SOURCE_GROUP_UNLOCK (group);
    msg =
        gst_missing_element_message_new (GST_ELEMENT_CAST (playbin),
        "uridecodebin3");
    gst_element_post_message (GST_ELEMENT_CAST (playbin), msg);

    GST_ELEMENT_ERROR (playbin, CORE, MISSING_PLUGIN,
        (_("Could not create \"uridecodebin3\" element.")), (NULL));

    GST_SOURCE_GROUP_LOCK (group);

    goto error_cleanup;
  }
uridecodebin_failure:
  {
    GST_DEBUG_OBJECT (playbin, "failed state change of uridecodebin");
    GST_SOURCE_GROUP_LOCK (group);
    goto error_cleanup;
  }
sink_failure:
  {
    GST_ERROR_OBJECT (playbin, "failed to activate sinks");
    goto error_cleanup;
  }

error_cleanup:
  {
    group->selected_stream_types = 0;

    /* delete any custom sinks we might have */
    if (group->audio_sink) {
      /* If this is a automatically created sink set it to NULL */
      if (audio_sink_activated)
        gst_element_set_state (group->audio_sink, GST_STATE_NULL);
      gst_object_unref (group->audio_sink);
    }
    group->audio_sink = NULL;

    if (group->video_sink) {
      /* If this is a automatically created sink set it to NULL */
      if (video_sink_activated)
        gst_element_set_state (group->video_sink, GST_STATE_NULL);
      gst_object_unref (group->video_sink);
    }
    group->video_sink = NULL;

    if (group->text_sink) {
      /* If this is a automatically created sink set it to NULL */
      if (text_sink_activated)
        gst_element_set_state (group->text_sink, GST_STATE_NULL);
      gst_object_unref (group->text_sink);
    }
    group->text_sink = NULL;

    if (uridecodebin) {
      REMOVE_SIGNAL (group->uridecodebin, group->pad_added_id);
      REMOVE_SIGNAL (group->uridecodebin, group->pad_removed_id);
      REMOVE_SIGNAL (group->uridecodebin, group->select_stream_id);
      REMOVE_SIGNAL (group->uridecodebin, group->source_setup_id);
      REMOVE_SIGNAL (group->uridecodebin, group->about_to_finish_id);

      gst_element_set_state (uridecodebin, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (playbin), uridecodebin);
    }

    GST_SOURCE_GROUP_UNLOCK (group);

    return GST_STATE_CHANGE_FAILURE;
  }
}

/* must be called with PLAY_BIN_LOCK, which is dropped temporarily
 * if changing states */
static gboolean
deactivate_group (GstPlayBin3 * playbin, GstSourceGroup * group)
{
  g_return_val_if_fail (group->active, FALSE);
  g_return_val_if_fail (group->valid, FALSE);

  GST_DEBUG_OBJECT (playbin, "unlinking group %p", group);

  GST_SOURCE_GROUP_LOCK (group);
  group->active = FALSE;
  group->playing = FALSE;
  group->group_id = GST_GROUP_ID_INVALID;

  group->selected_stream_types = 0;
  /* Update global selected_stream_types */
  playbin->selected_stream_types =
      playbin->groups[0].selected_stream_types | playbin->groups[1].
      selected_stream_types;
  if (playbin->active_stream_types != playbin->selected_stream_types)
    reconfigure_output (playbin);

  if (group->uridecodebin) {
    REMOVE_SIGNAL (group->uridecodebin, group->select_stream_id);
    REMOVE_SIGNAL (group->uridecodebin, group->source_setup_id);
    REMOVE_SIGNAL (group->uridecodebin, group->about_to_finish_id);

    GST_PLAY_BIN3_UNLOCK (playbin);
    gst_element_set_state (group->uridecodebin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (playbin), group->uridecodebin);
    GST_PLAY_BIN3_LOCK (playbin);

    REMOVE_SIGNAL (group->uridecodebin, group->pad_added_id);
    REMOVE_SIGNAL (group->uridecodebin, group->pad_removed_id);
  }

  GST_SOURCE_GROUP_UNLOCK (group);

  GST_DEBUG_OBJECT (playbin, "Done");

  return TRUE;
}

/* setup the next group to play, this assumes the next_group is valid and
 * configured. It swaps out the current_group and activates the valid
 * next_group. */
static GstStateChangeReturn
setup_next_source (GstPlayBin3 * playbin)
{
  GstSourceGroup *new_group;
  GstStateChangeReturn state_ret;

  GST_DEBUG_OBJECT (playbin, "setup next source");

  debug_groups (playbin);

  /* see if there is a next group */
  GST_PLAY_BIN3_LOCK (playbin);
  new_group = playbin->next_group;
  if (!new_group || !new_group->valid || new_group->active)
    goto no_next_group;

  /* activate the new group */
  state_ret = activate_group (playbin, new_group);
  if (state_ret == GST_STATE_CHANGE_FAILURE)
    goto activate_failed;

  GST_PLAY_BIN3_UNLOCK (playbin);

  debug_groups (playbin);

  return state_ret;

  /* ERRORS */
no_next_group:
  {
    GST_DEBUG_OBJECT (playbin, "no next group");
    GST_PLAY_BIN3_UNLOCK (playbin);
    return GST_STATE_CHANGE_FAILURE;
  }
activate_failed:
  {
    new_group->stream_changed_pending = FALSE;
    GST_DEBUG_OBJECT (playbin, "activate failed");
    new_group->valid = FALSE;
    GST_PLAY_BIN3_UNLOCK (playbin);
    return GST_STATE_CHANGE_FAILURE;
  }
}

/* The group that is currently playing is copied again to the
 * next_group so that it will start playing the next time.
 */
static gboolean
save_current_group (GstPlayBin3 * playbin)
{
  GstSourceGroup *curr_group;
  gboolean swapped = FALSE;

  GST_DEBUG_OBJECT (playbin, "save current group");

  /* see if there is a current group */
  GST_PLAY_BIN3_LOCK (playbin);
  curr_group = playbin->curr_group;
  if (curr_group && curr_group->valid && curr_group->active) {
    swapped = TRUE;
  }
  /* swap old and new */
  playbin->curr_group = playbin->next_group;
  playbin->next_group = curr_group;

  if (swapped) {
    /* unlink our pads with the sink */
    deactivate_group (playbin, curr_group);
  }
  GST_PLAY_BIN3_UNLOCK (playbin);

  return TRUE;
}

/* clear the locked state from all groups. This function is called before a
 * state change to NULL is performed on them. */
static gboolean
groups_set_locked_state (GstPlayBin3 * playbin, gboolean locked)
{
  GST_DEBUG_OBJECT (playbin, "setting locked state to %d on all groups",
      locked);

  GST_PLAY_BIN3_LOCK (playbin);
  GST_SOURCE_GROUP_LOCK (playbin->curr_group);
  group_set_locked_state_unlocked (playbin, playbin->curr_group, locked);
  GST_SOURCE_GROUP_UNLOCK (playbin->curr_group);
  GST_SOURCE_GROUP_LOCK (playbin->next_group);
  group_set_locked_state_unlocked (playbin, playbin->next_group, locked);
  GST_SOURCE_GROUP_UNLOCK (playbin->next_group);
  GST_PLAY_BIN3_UNLOCK (playbin);

  return TRUE;
}

static void
gst_play_bin3_check_group_status (GstPlayBin3 * playbin)
{
  if (playbin->activation_task)
    gst_task_start (playbin->activation_task);
}

static void
gst_play_bin3_activation_thread (GstPlayBin3 * playbin)
{
  GST_DEBUG_OBJECT (playbin, "starting");

  debug_groups (playbin);

  /* Check if next_group needs to be deactivated */
  GST_PLAY_BIN3_LOCK (playbin);
  if (playbin->next_group->active) {
    deactivate_group (playbin, playbin->next_group);
    playbin->next_group->valid = FALSE;
  }

  /* Is there a pending about-to-finish to be emitted ? */
  GST_SOURCE_GROUP_LOCK (playbin->curr_group);
  if (playbin->curr_group->pending_about_to_finish) {
    GST_LOG_OBJECT (playbin, "Propagating about-to-finish");
    playbin->curr_group->pending_about_to_finish = FALSE;
    GST_SOURCE_GROUP_UNLOCK (playbin->curr_group);
    /* This will activate the next source afterwards */
    emit_about_to_finish (playbin);
  } else
    GST_SOURCE_GROUP_UNLOCK (playbin->curr_group);

  GST_LOG_OBJECT (playbin, "Pausing task");
  if (playbin->activation_task)
    gst_task_pause (playbin->activation_task);
  GST_PLAY_BIN3_UNLOCK (playbin);

  GST_DEBUG_OBJECT (playbin, "done");
  return;
}

static gboolean
gst_play_bin3_start (GstPlayBin3 * playbin)
{
  GST_DEBUG_OBJECT (playbin, "starting");

  GST_PLAY_BIN3_LOCK (playbin);

  if (playbin->activation_task == NULL) {
    playbin->activation_task =
        gst_task_new ((GstTaskFunction) gst_play_bin3_activation_thread,
        playbin, NULL);
    if (playbin->activation_task == NULL)
      goto task_error;
    gst_task_set_lock (playbin->activation_task, &playbin->activation_lock);
  }
  GST_LOG_OBJECT (playbin, "clearing shutdown flag");
  g_atomic_int_set (&playbin->shutdown, 0);
  do_async_start (playbin);

  GST_PLAY_BIN3_UNLOCK (playbin);

  return TRUE;

task_error:
  {
    GST_PLAY_BIN3_UNLOCK (playbin);
    GST_ERROR_OBJECT (playbin, "Failed to create task");
    return FALSE;
  }
}

static void
gst_play_bin3_stop (GstPlayBin3 * playbin)
{
  GstTask *task;

  GST_DEBUG_OBJECT (playbin, "stopping");

  /* FIXME unlock our waiting groups */
  GST_LOG_OBJECT (playbin, "setting shutdown flag");
  g_atomic_int_set (&playbin->shutdown, 1);

  /* wait for all callbacks to end by taking the lock.
   * No dynamic (critical) new callbacks will
   * be able to happen as we set the shutdown flag. */
  GST_PLAY_BIN3_DYN_LOCK (playbin);
  GST_LOG_OBJECT (playbin, "dynamic lock taken, we can continue shutdown");
  GST_PLAY_BIN3_DYN_UNLOCK (playbin);

  /* Stop the activation task */
  GST_PLAY_BIN3_LOCK (playbin);
  if ((task = playbin->activation_task)) {
    playbin->activation_task = NULL;
    GST_PLAY_BIN3_UNLOCK (playbin);

    gst_task_stop (task);

    /* Make sure task is not running */
    g_rec_mutex_lock (&playbin->activation_lock);
    g_rec_mutex_unlock (&playbin->activation_lock);

    /* Wait for task to finish and unref it */
    gst_task_join (task);
    gst_object_unref (task);

    GST_PLAY_BIN3_LOCK (playbin);
  }
  GST_PLAY_BIN3_UNLOCK (playbin);
}

static GstStateChangeReturn
gst_play_bin3_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlayBin3 *playbin;
  gboolean do_save = FALSE;

  playbin = GST_PLAY_BIN3 (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_play_bin3_start (playbin))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    async_down:
      gst_play_bin3_stop (playbin);
      if (!do_save)
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* we go async to PAUSED, so if that fails, we never make it to PAUSED
       * and we will never be called with the GST_STATE_CHANGE_PAUSED_TO_READY.
       * Make sure we do go through the same steps (see above) for
       * proper cleanup */
      if (!g_atomic_int_get (&playbin->shutdown)) {
        do_save = TRUE;
        goto async_down;
      }

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
      if ((ret = setup_next_source (playbin)) == GST_STATE_CHANGE_FAILURE)
        goto failure;
      if (ret == GST_STATE_CHANGE_SUCCESS)
        ret = GST_STATE_CHANGE_ASYNC;

      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      do_async_done (playbin);
      /* FIXME Release audio device when we implement that */
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      playbin->is_live = FALSE;
      save_current_group (playbin);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
      guint i;

      /* also do missed state change down to READY */
      if (do_save)
        save_current_group (playbin);
      /* Deactivate the groups, set uridecodebin to NULL and unref it */
      GST_PLAY_BIN3_LOCK (playbin);
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

      }
      GST_PLAY_BIN3_UNLOCK (playbin);

      /* Set our sinks back to NULL, they might not be child of playbin */
      if (playbin->audio_sink)
        gst_element_set_state (playbin->audio_sink, GST_STATE_NULL);
      if (playbin->video_sink)
        gst_element_set_state (playbin->video_sink, GST_STATE_NULL);
      if (playbin->text_sink)
        gst_element_set_state (playbin->text_sink, GST_STATE_NULL);

      if (playbin->video_stream_combiner)
        gst_element_set_state (playbin->video_stream_combiner, GST_STATE_NULL);
      if (playbin->audio_stream_combiner)
        gst_element_set_state (playbin->audio_stream_combiner, GST_STATE_NULL);
      if (playbin->text_stream_combiner)
        gst_element_set_state (playbin->text_stream_combiner, GST_STATE_NULL);

      /* make sure the groups don't perform a state change anymore until we
       * enable them again */
      groups_set_locked_state (playbin, TRUE);
      break;
    }
    default:
      break;
  }

  if (GST_STATE_TRANSITION_NEXT (transition) == GST_STATE_PAUSED)
    playbin->is_live = ret == GST_STATE_CHANGE_NO_PREROLL;

  if (ret == GST_STATE_CHANGE_NO_PREROLL)
    do_async_done (playbin);

  return ret;

  /* ERRORS */
failure:
  {
    do_async_done (playbin);

    if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
      GstSourceGroup *curr_group;

      GST_PLAY_BIN3_LOCK (playbin);

      curr_group = playbin->curr_group;
      if (curr_group) {
        if (curr_group->active && curr_group->valid) {
          /* unlink our pads with the sink */
          deactivate_group (playbin, curr_group);
        }
        curr_group->valid = FALSE;
      }

      /* Swap current and next group back */
      playbin->curr_group = playbin->next_group;
      playbin->next_group = curr_group;

      GST_PLAY_BIN3_UNLOCK (playbin);
    }
    return ret;
  }
}

static void
gst_play_bin3_overlay_expose (GstVideoOverlay * overlay)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (overlay);

  gst_video_overlay_expose (GST_VIDEO_OVERLAY (playbin->playsink));
}

static void
gst_play_bin3_overlay_handle_events (GstVideoOverlay * overlay,
    gboolean handle_events)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (overlay);

  gst_video_overlay_handle_events (GST_VIDEO_OVERLAY (playbin->playsink),
      handle_events);
}

static void
gst_play_bin3_overlay_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (overlay);

  gst_video_overlay_set_render_rectangle (GST_VIDEO_OVERLAY (playbin->playsink),
      x, y, width, height);
}

static void
gst_play_bin3_overlay_set_window_handle (GstVideoOverlay * overlay,
    guintptr handle)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (overlay);

  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (playbin->playsink),
      handle);
}

static void
gst_play_bin3_overlay_init (gpointer g_iface, gpointer g_iface_data)
{
  GstVideoOverlayInterface *iface = (GstVideoOverlayInterface *) g_iface;
  iface->expose = gst_play_bin3_overlay_expose;
  iface->handle_events = gst_play_bin3_overlay_handle_events;
  iface->set_render_rectangle = gst_play_bin3_overlay_set_render_rectangle;
  iface->set_window_handle = gst_play_bin3_overlay_set_window_handle;
}

static void
gst_play_bin3_navigation_send_event (GstNavigation * navigation,
    GstEvent * event)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (navigation);

  gst_navigation_send_event_simple (GST_NAVIGATION (playbin->playsink), event);
}

static void
gst_play_bin3_navigation_init (gpointer g_iface, gpointer g_iface_data)
{
  GstNavigationInterface *iface = (GstNavigationInterface *) g_iface;

  iface->send_event_simple = gst_play_bin3_navigation_send_event;
}

static const GList *
gst_play_bin3_colorbalance_list_channels (GstColorBalance * balance)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (balance);

  return
      gst_color_balance_list_channels (GST_COLOR_BALANCE (playbin->playsink));
}

static void
gst_play_bin3_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (balance);

  gst_color_balance_set_value (GST_COLOR_BALANCE (playbin->playsink), channel,
      value);
}

static gint
gst_play_bin3_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (balance);

  return gst_color_balance_get_value (GST_COLOR_BALANCE (playbin->playsink),
      channel);
}

static GstColorBalanceType
gst_play_bin3_colorbalance_get_balance_type (GstColorBalance * balance)
{
  GstPlayBin3 *playbin = GST_PLAY_BIN3 (balance);

  return
      gst_color_balance_get_balance_type (GST_COLOR_BALANCE
      (playbin->playsink));
}

static void
gst_play_bin3_colorbalance_init (gpointer g_iface, gpointer g_iface_data)
{
  GstColorBalanceInterface *iface = (GstColorBalanceInterface *) g_iface;

  iface->list_channels = gst_play_bin3_colorbalance_list_channels;
  iface->set_value = gst_play_bin3_colorbalance_set_value;
  iface->get_value = gst_play_bin3_colorbalance_get_value;
  iface->get_balance_type = gst_play_bin3_colorbalance_get_balance_type;
}

gboolean
gst_play_bin3_custom_element_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_play_bin3_debug, "playbin3", 0, "play bin3");

  playback_element_init (plugin);

  if (g_getenv ("USE_PLAYBIN3")) {
    ret &= gst_element_register (plugin, "playbin", GST_RANK_NONE,
        GST_TYPE_PLAY_BIN);
  }

  ret &= gst_element_register (plugin, "playbin3", GST_RANK_NONE,
      GST_TYPE_PLAY_BIN);

  return ret;
}
