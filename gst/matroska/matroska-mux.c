/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2005 Michal Benes <michal.benes@xeris.cz>
 *
 * matroska-mux.c: matroska file/stream muxer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "matroska-mux.h"
#include "matroska-ids.h"

GST_DEBUG_CATEGORY_STATIC (matroskamux_debug);
#define GST_CAT_DEFAULT matroskamux_debug

enum
{
  ARG_0,
  ARG_WRITING_APP,
  ARG_MATROSKA_VERSION
};

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska")
    );

#define COMMON_VIDEO_CAPS \
  "width = (int) [ 16, 4096 ], " \
  "height = (int) [ 16, 4096 ], " \
  "framerate = (fraction) [ 0, MAX ]"

static GstStaticPadTemplate videosink_templ =
    GST_STATIC_PAD_TEMPLATE ("video_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2, 4 }, "
        "systemstream = (boolean) false, "
        COMMON_VIDEO_CAPS "; "
        "video/x-h264, "
        COMMON_VIDEO_CAPS "; "
        "video/x-divx, "
        COMMON_VIDEO_CAPS "; "
        "video/x-xvid, "
        COMMON_VIDEO_CAPS "; "
        "video/x-huffyuv, "
        COMMON_VIDEO_CAPS "; "
        "video/x-dv, "
        COMMON_VIDEO_CAPS "; "
        "video/x-h263, "
        COMMON_VIDEO_CAPS "; "
        "video/x-msmpeg, "
        COMMON_VIDEO_CAPS "; "
        "image/jpeg, "
        COMMON_VIDEO_CAPS "; "
        "video/x-theora; "
        "video/x-dirac, "
        COMMON_VIDEO_CAPS "; "
        "video/x-raw-yuv, "
        "format = (fourcc) { YUY2, I420 }, " COMMON_VIDEO_CAPS)
    );

#define COMMON_AUDIO_CAPS \
  "channels = (int) [ 1, 8 ], " \
  "rate = (int) [ 8000, 96000 ]"

/* FIXME:
 * * audio/x-raw-float: endianness needs defining.
 */
static GstStaticPadTemplate audiosink_templ =
    GST_STATIC_PAD_TEMPLATE ("audio_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        COMMON_AUDIO_CAPS "; "
        "audio/mpeg, "
        "mpegversion = (int) { 2, 4 }, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-ac3, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-vorbis, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-raw-int, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "signed = (boolean) false, "
        COMMON_AUDIO_CAPS ";"
        "audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }, "
        "signed = (boolean) true, "
        COMMON_AUDIO_CAPS ";"
        "audio/x-tta, "
        "width = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, " "rate = (int) [ 8000, 96000 ]")
    );

static GstStaticPadTemplate subtitlesink_templ =
GST_STATIC_PAD_TEMPLATE ("subtitle_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GArray *used_uids;

static void gst_matroska_mux_add_interfaces (GType type);

GST_BOILERPLATE_FULL (GstMatroskaMux, gst_matroska_mux, GstElement,
    GST_TYPE_ELEMENT, gst_matroska_mux_add_interfaces);

/* Matroska muxer destructor */
static void gst_matroska_mux_finalize (GObject * object);

/* Pads collected callback */
static GstFlowReturn
gst_matroska_mux_collected (GstCollectPads * pads, gpointer user_data);

/* pad functions */
static gboolean gst_matroska_mux_handle_src_event (GstPad * pad,
    GstEvent * event);
static GstPad *gst_matroska_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_matroska_mux_release_pad (GstElement * element, GstPad * pad);

/* gst internal change state handler */
static GstStateChangeReturn
gst_matroska_mux_change_state (GstElement * element, GstStateChange transition);

/* gobject bla bla */
static void gst_matroska_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_matroska_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* reset muxer */
static void gst_matroska_mux_reset (GstElement * element);

/* uid generation */
static guint32 gst_matroska_mux_create_uid ();

static gboolean theora_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context);
static gboolean vorbis_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context);

static void
gst_matroska_mux_add_interfaces (GType type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };

  g_type_add_interface_static (type, GST_TYPE_TAG_SETTER, &tag_setter_info);
}

static void
gst_matroska_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static const GstElementDetails gst_matroska_mux_details =
      GST_ELEMENT_DETAILS ("Matroska muxer",
      "Codec/Muxer",
      "Muxes video/audio/subtitle streams into a matroska stream",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&videosink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audiosink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subtitlesink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_set_details (element_class, &gst_matroska_mux_details);

  GST_DEBUG_CATEGORY_INIT (matroskamux_debug, "matroskamux", 0,
      "Matroska muxer");
}

static void
gst_matroska_mux_class_init (GstMatroskaMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_matroska_mux_finalize);

  gobject_class->get_property = gst_matroska_mux_get_property;
  gobject_class->set_property = gst_matroska_mux_set_property;

  g_object_class_install_property (gobject_class, ARG_WRITING_APP,
      g_param_spec_string ("writing-app", "Writing application.",
          "The name the application that creates the matroska file.",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MATROSKA_VERSION,
      g_param_spec_int ("version", "Matroska version",
          "This parameter determines what matroska features can be used.",
          1, 2, 1, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_release_pad);
}


/**
 * gst_matroska_mux_init:
 * @mux: #GstMatroskaMux that should be initialized.
 * @g_class: Class of the muxer.
 *
 * Matroska muxer constructor.
 */
static void
gst_matroska_mux_init (GstMatroskaMux * mux, GstMatroskaMuxClass * g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (gstelement_class, "src"), "src");
  gst_pad_set_event_function (mux->srcpad, gst_matroska_mux_handle_src_event);
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);

  mux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (mux->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_matroska_mux_collected),
      mux);

  mux->ebml_write = gst_ebml_write_new (mux->srcpad);

  /* initialize internal variables */
  mux->index = NULL;
  mux->matroska_version = 1;

  /* Initialize all variables */
  gst_matroska_mux_reset (GST_ELEMENT (mux));
}


/**
 * gst_matroska_mux_finalize:
 * @object: #GstMatroskaMux that should be finalized.
 *
 * Finalize matroska muxer.
 */
static void
gst_matroska_mux_finalize (GObject * object)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (object);

  gst_object_unref (mux->collect);
  gst_object_unref (mux->ebml_write);
  if (mux->writing_app)
    g_free (mux->writing_app);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


/**
 * gst_matroska_mux_create_uid:
 *
 * Generate new unused track UID.
 *
 * Returns: New track UID.
 */
static guint32
gst_matroska_mux_create_uid (void)
{
  guint32 uid = 0;
  GRand *rand = g_rand_new ();

  while (!uid) {
    guint i;

    uid = g_rand_int (rand);
    for (i = 0; i < used_uids->len; i++) {
      if (g_array_index (used_uids, guint32, i) == uid) {
        uid = 0;
        break;
      }
    }
    g_array_append_val (used_uids, uid);
  }
  g_free (rand);
  return uid;
}

/**
 * gst_matroska_pad_free:
 * @collect_pad: the #GstMatroskaPad
 *
 * Release resources of a matroska collect pad.
 */
static void
gst_matroska_pad_free (GstMatroskaPad * collect_pad)
{
  /* free track information */
  if (collect_pad->track != NULL) {
    g_free (collect_pad->track->codec_id);
    g_free (collect_pad->track->codec_name);
    g_free (collect_pad->track->name);
    g_free (collect_pad->track->language);
    g_free (collect_pad->track->codec_priv);
    g_free (collect_pad->track);
    collect_pad->track = NULL;
  }

  /* free cached buffer */
  if (collect_pad->buffer != NULL) {
    gst_buffer_unref (collect_pad->buffer);
    collect_pad->buffer = NULL;
  }
}


/**
 * gst_matroska_mux_reset:
 * @element: #GstMatroskaMux that should be reseted.
 *
 * Reset matroska muxer back to initial state.
 */
static void
gst_matroska_mux_reset (GstElement * element)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);
  GSList *walk;

  /* reset EBML write */
  gst_ebml_write_reset (mux->ebml_write);

  /* reset input */
  mux->state = GST_MATROSKA_MUX_STATE_START;

  /* clean up existing streams */
  while ((walk = mux->collect->data) != NULL) {
    GstMatroskaPad *collect_pad;
    GstPad *thepad;

    collect_pad = (GstMatroskaPad *) walk->data;
    thepad = collect_pad->collect.pad;

    /* free collect pad resources */
    gst_matroska_pad_free (collect_pad);

    /* remove from collectpads */
    gst_collect_pads_remove_pad (mux->collect, thepad);
  }
  mux->num_streams = 0;
  mux->num_a_streams = 0;
  mux->num_t_streams = 0;
  mux->num_v_streams = 0;

  /* reset writing_app */
  if (mux->writing_app) {
    g_free (mux->writing_app);
  }
  mux->writing_app = g_strdup ("GStreamer Matroska muxer");

  /* reset indexes */
  mux->num_indexes = 0;
  g_free (mux->index);
  mux->index = NULL;

  /* reset timers */
  mux->time_scale = 1000000;
  mux->duration = 0;

  /* reset uid array */
  if (used_uids) {
    g_array_free (used_uids, TRUE);
  }
  /* arbitrary size, 10 should be enough in most cases */
  used_uids = g_array_sized_new (FALSE, FALSE, sizeof (guint32), 10);

  /* reset cluster */
  mux->cluster = 0;
  mux->cluster_time = 0;
  mux->cluster_pos = 0;

  /* reset meta-seek index */
  mux->num_meta_indexes = 0;
  g_free (mux->meta_index);
  mux->meta_index = NULL;

  /* reset tags */
  if (mux->tags) {
    gst_tag_list_free (mux->tags);
    mux->tags = NULL;
  }
}

/**
 * gst_matroska_mux_handle_src_event:
 * @pad: Pad which received the event.
 * @event: Received event.
 *
 * handle events - copied from oggmux without understanding 
 *
 * Returns: #TRUE on success.
 */
static gboolean
gst_matroska_mux_handle_src_event (GstPad * pad, GstEvent * event)
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

/**
 * gst_matroska_mux_handle_sink_event:
 * @pad: Pad which received the event.
 * @event: Received event.
 *
 * handle events - informational ones like tags
 *
 * Returns: #TRUE on success.
 */
static gboolean
gst_matroska_mux_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstMatroskaTrackContext *context;
  GstMatroskaPad *collect_pad;
  GstMatroskaMux *mux;
  GstTagList *list;
  gboolean ret;

  mux = GST_MATROSKA_MUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &list);
      collect_pad = (GstMatroskaPad *) gst_pad_get_element_private (pad);
      g_assert (collect_pad);
      context = collect_pad->track;
      g_assert (context);
      /* FIXME ?
       * strictly speaking, the incoming language code may only be 639-1, so not
       * 639-2 according to matroska specs, but it will have to do for now */
      gst_tag_list_get_string (list, GST_TAG_LANGUAGE_CODE, &context->language);

      if (mux->tags) {
        gst_tag_list_insert (mux->tags, list, GST_TAG_MERGE_PREPEND);
      } else {
        mux->tags = gst_tag_list_copy (list);
      }

      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = mux->collect_event (pad, event);
  gst_object_unref (mux);

  return ret;
}


/**
 * gst_matroska_mux_video_pad_setcaps:
 * @pad: Pad which got the caps.
 * @caps: New caps.
 *
 * Setcaps function for video sink pad.
 *
 * Returns: #TRUE on success.
 */
static gboolean
gst_matroska_mux_video_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMatroskaTrackContext *context = NULL;
  GstMatroskaTrackVideoContext *videocontext;
  GstMatroskaMux *mux;
  GstMatroskaPad *collect_pad;
  GstStructure *structure;
  const gchar *mimetype;
  gint width, height, pixel_width, pixel_height;
  gint fps_d, fps_n;

  mux = GST_MATROSKA_MUX (GST_PAD_PARENT (pad));

  /* find context */
  collect_pad = (GstMatroskaPad *) gst_pad_get_element_private (pad);
  g_assert (collect_pad);
  context = collect_pad->track;
  g_assert (context);
  g_assert (context->type == GST_MATROSKA_TRACK_TYPE_VIDEO);
  videocontext = (GstMatroskaTrackVideoContext *) context;

  /* gst -> matroska ID'ing */
  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-theora")) {
    /* we'll extract the details later from the theora identification header */
    goto skip_details;
  }

  /* get general properties */
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  videocontext->pixel_width = width;
  videocontext->pixel_height = height;
  if (gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d)) {
    context->default_duration =
        gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    GST_LOG_OBJECT (pad, "default duration = %" GST_TIME_FORMAT,
        GST_TIME_ARGS (context->default_duration));
  } else {
    context->default_duration = 0;
  }
  if (gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          &pixel_width, &pixel_height)) {
    if (pixel_width > pixel_height) {
      videocontext->display_width = width * pixel_width / pixel_height;
      videocontext->display_height = height;
    } else if (pixel_width < pixel_height) {
      videocontext->display_width = width;
      videocontext->display_height = height * pixel_height / pixel_width;
    } else {
      videocontext->display_width = 0;
      videocontext->display_height = 0;
    }
  } else {
    videocontext->display_width = 0;
    videocontext->display_height = 0;
  }

skip_details:

  videocontext->asr_mode = GST_MATROSKA_ASPECT_RATIO_MODE_FREE;
  videocontext->eye_mode = GST_MATROSKA_EYE_MODE_MONO;
  videocontext->fourcc = 0;

  /* find type */
  if (!strcmp (mimetype, "video/x-raw-yuv")) {
    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED);
    gst_structure_get_fourcc (structure, "format", &videocontext->fourcc);

    return TRUE;
  } else if (!strcmp (mimetype, "image/jpeg")) {
    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MJPEG);

    return TRUE;
  } else if (!strcmp (mimetype, "video/x-xvid") /* MS/VfW compatibility cases */
      ||!strcmp (mimetype, "video/x-huffyuv")
      || !strcmp (mimetype, "video/x-divx")
      || !strcmp (mimetype, "video/x-dv")
      || !strcmp (mimetype, "video/x-h263")
      || !strcmp (mimetype, "video/x-dirac")) {
    BITMAPINFOHEADER *bih;
    const GValue *codec_data;
    gint size = sizeof (BITMAPINFOHEADER);

    bih = g_new0 (BITMAPINFOHEADER, 1);
    GST_WRITE_UINT32_LE (&bih->bi_size, size);
    GST_WRITE_UINT32_LE (&bih->bi_width, videocontext->pixel_width);
    GST_WRITE_UINT32_LE (&bih->bi_height, videocontext->pixel_height);
    GST_WRITE_UINT16_LE (&bih->bi_planes, (guint16) 1);
    GST_WRITE_UINT16_LE (&bih->bi_bit_count, (guint16) 24);
    GST_WRITE_UINT32_LE (&bih->bi_size_image, videocontext->pixel_width *
        videocontext->pixel_height * 3);

    if (!strcmp (mimetype, "video/x-xvid"))
      GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("XVID"));
    else if (!strcmp (mimetype, "video/x-huffyuv"))
      GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("HFYU"));
    else if (!strcmp (mimetype, "video/x-dv"))
      GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("DVSD"));
    else if (!strcmp (mimetype, "video/x-h263"))
      GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("H263"));
    else if (!strcmp (mimetype, "video/x-divx")) {
      gint divxversion;

      gst_structure_get_int (structure, "divxversion", &divxversion);
      switch (divxversion) {
        case 3:
          GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("DIV3"));
          break;
        case 4:
          GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("DIVX"));
          break;
        case 5:
          GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("DX50"));
          break;
      }
    } else if (!strcmp (mimetype, "video/x-dirac")) {
      GST_WRITE_UINT32_LE (&bih->bi_compression, GST_STR_FOURCC ("drac"));
    }

    /* process codec private/initialization data, if any */
    codec_data = gst_structure_get_value (structure, "codec_data");
    if (codec_data) {
      GstBuffer *codec_data_buf;

      codec_data_buf = g_value_peek_pointer (codec_data);
      size += GST_BUFFER_SIZE (codec_data_buf);
      bih = g_realloc (bih, size);
      GST_WRITE_UINT32_LE (&bih->bi_size, size);
      memcpy ((guint8 *) bih + sizeof (BITMAPINFOHEADER),
          GST_BUFFER_DATA (codec_data_buf), GST_BUFFER_SIZE (codec_data_buf));
    }

    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC);
    context->codec_priv = (gpointer) bih;
    context->codec_priv_size = size;

    return TRUE;
  } else if (!strcmp (mimetype, "video/x-h264")) {
    const GValue *codec_data;

    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AVC);

    if (context->codec_priv != NULL) {
      g_free (context->codec_priv);
      context->codec_priv = NULL;
      context->codec_priv_size = 0;
    }


    /* Create avcC header */
    codec_data = gst_structure_get_value (structure, "codec_data");

    if (codec_data != NULL) {
      guint8 *priv_data = NULL;
      guint priv_data_size = 0;

      GstBuffer *codec_data_buf = g_value_peek_pointer (codec_data);

      priv_data_size = GST_BUFFER_SIZE (codec_data_buf);
      priv_data = g_malloc0 (priv_data_size);

      memcpy (priv_data, GST_BUFFER_DATA (codec_data_buf), priv_data_size);

      context->codec_priv = priv_data;
      context->codec_priv_size = priv_data_size;
    }

    return TRUE;
  } else if (!strcmp (mimetype, "video/x-theora")) {
    const GValue *streamheader;

    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_THEORA);

    if (context->codec_priv != NULL) {
      g_free (context->codec_priv);
      context->codec_priv = NULL;
      context->codec_priv_size = 0;
    }

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (!theora_streamheader_to_codecdata (streamheader, context)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("theora stream headers missing or malformed"));
      return FALSE;
    }
    return TRUE;
  } else if (!strcmp (mimetype, "video/mpeg")) {
    gint mpegversion;

    gst_structure_get_int (structure, "mpegversion", &mpegversion);
    switch (mpegversion) {
      case 1:
        context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG1);
        break;
      case 2:
        context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG2);
        break;
      case 4:
        context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP);
        break;
      default:
        return FALSE;
    }

    return TRUE;
  } else if (!strcmp (mimetype, "video/x-msmpeg")) {
    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3);

    return TRUE;
  }

  return FALSE;
}

static gboolean
xiph3_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context, GstBuffer ** p_buf0)
{
  GstBuffer *buf[3];
  GArray *bufarr;
  guint8 *priv_data;
  guint i, offset, priv_data_size;

  if (streamheader == NULL)
    goto no_stream_headers;

  if (G_VALUE_TYPE (streamheader) != GST_TYPE_ARRAY)
    goto wrong_type;

  bufarr = g_value_peek_pointer (streamheader);
  if (bufarr->len != 3)
    goto wrong_count;

  context->xiph_headers_to_skip = bufarr->len;

  for (i = 0; i < 3; i++) {
    GValue *bufval = &g_array_index (bufarr, GValue, i);

    if (G_VALUE_TYPE (bufval) != GST_TYPE_BUFFER)
      goto wrong_content_type;

    buf[i] = g_value_peek_pointer (bufval);
  }

  priv_data_size = 1;
  priv_data_size += GST_BUFFER_SIZE (buf[0]) / 0xff + 1;
  priv_data_size += GST_BUFFER_SIZE (buf[1]) / 0xff + 1;

  for (i = 0; i < 3; ++i) {
    priv_data_size += GST_BUFFER_SIZE (buf[i]);
  }

  priv_data = g_malloc0 (priv_data_size);

  priv_data[0] = 2;
  offset = 1;

  for (i = 0; i < GST_BUFFER_SIZE (buf[0]) / 0xff; ++i) {
    priv_data[offset++] = 0xff;
  }
  priv_data[offset++] = GST_BUFFER_SIZE (buf[0]) % 0xff;

  for (i = 0; i < GST_BUFFER_SIZE (buf[1]) / 0xff; ++i) {
    priv_data[offset++] = 0xff;
  }
  priv_data[offset++] = GST_BUFFER_SIZE (buf[1]) % 0xff;

  for (i = 0; i < 3; ++i) {
    memcpy (priv_data + offset, GST_BUFFER_DATA (buf[i]),
        GST_BUFFER_SIZE (buf[i]));
    offset += GST_BUFFER_SIZE (buf[i]);
  }

  context->codec_priv = priv_data;
  context->codec_priv_size = priv_data_size;

  if (p_buf0)
    *p_buf0 = gst_buffer_ref (buf[0]);

  return TRUE;

/* ERRORS */
no_stream_headers:
  {
    GST_WARNING ("required streamheaders missing in sink caps!");
    return FALSE;
  }
wrong_type:
  {
    GST_WARNING ("streamheaders are not a GST_TYPE_ARRAY, but a %s",
        G_VALUE_TYPE_NAME (streamheader));
    return FALSE;
  }
wrong_count:
  {
    GST_WARNING ("got %u streamheaders, not 3 as expected", bufarr->len);
    return FALSE;
  }
wrong_content_type:
  {
    GST_WARNING ("streamheaders array does not contain GstBuffers");
    return FALSE;
  }
}

static gboolean
vorbis_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GstBuffer *buf0 = NULL;

  if (!xiph3_streamheader_to_codecdata (streamheader, context, &buf0))
    return FALSE;

  if (buf0 == NULL || GST_BUFFER_SIZE (buf0) < 1 + 6 + 4) {
    GST_WARNING ("First vorbis header too small, ignoring");
  } else {
    if (memcmp (GST_BUFFER_DATA (buf0) + 1, "vorbis", 6) == 0) {
      GstMatroskaTrackAudioContext *audiocontext;
      guint8 *hdr;

      hdr = GST_BUFFER_DATA (buf0) + 1 + 6 + 4;
      audiocontext = (GstMatroskaTrackAudioContext *) context;
      audiocontext->channels = GST_READ_UINT8 (hdr);
      audiocontext->samplerate = GST_READ_UINT32_LE (hdr + 1);
    }
  }

  if (buf0)
    gst_buffer_unref (buf0);

  return TRUE;
}

static gboolean
theora_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GstBuffer *buf0 = NULL;

  if (!xiph3_streamheader_to_codecdata (streamheader, context, &buf0))
    return FALSE;

  if (buf0 == NULL || GST_BUFFER_SIZE (buf0) < 1 + 6 + 26) {
    GST_WARNING ("First theora header too small, ignoring");
  } else if (memcmp (GST_BUFFER_DATA (buf0), "\200theora\003\002", 9) != 0) {
    GST_WARNING ("First header not a theora identification header, ignoring");
  } else {
    GstMatroskaTrackVideoContext *videocontext;
    guint fps_num, fps_denom, par_num, par_denom;
    guint8 *hdr;

    hdr = GST_BUFFER_DATA (buf0) + 1 + 6 + 3 + 2 + 2;

    videocontext = (GstMatroskaTrackVideoContext *) context;
    videocontext->pixel_width = GST_READ_UINT32_BE (hdr) >> 8;
    videocontext->pixel_height = GST_READ_UINT32_BE (hdr + 3) >> 8;
    hdr += 3 + 3 + 1 + 1;
    fps_num = GST_READ_UINT32_BE (hdr);
    fps_denom = GST_READ_UINT32_BE (hdr + 4);
    context->default_duration = gst_util_uint64_scale_int (GST_SECOND,
        fps_denom, fps_num);
    hdr += 4 + 4;
    par_num = GST_READ_UINT32_BE (hdr) >> 8;
    par_denom = GST_READ_UINT32_BE (hdr + 3) >> 8;
    if (par_num > 0 && par_num > 0) {
      if (par_num > par_denom) {
        videocontext->display_width =
            videocontext->pixel_width * par_num / par_denom;
        videocontext->display_height = videocontext->pixel_height;
      } else if (par_num < par_denom) {
        videocontext->display_width = videocontext->pixel_width;
        videocontext->display_height =
            videocontext->pixel_height * par_denom / par_num;
      } else {
        videocontext->display_width = 0;
        videocontext->display_height = 0;
      }
    } else {
      videocontext->display_width = 0;
      videocontext->display_height = 0;
    }
    hdr += 3 + 3;
  }

  if (buf0)
    gst_buffer_unref (buf0);

  return TRUE;
}

/**
 * gst_matroska_mux_audio_pad_setcaps:
 * @pad: Pad which got the caps.
 * @caps: New caps.
 *
 * Setcaps function for audio sink pad.
 *
 * Returns: #TRUE on success.
 */
static gboolean
gst_matroska_mux_audio_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMatroskaTrackContext *context = NULL;
  GstMatroskaTrackAudioContext *audiocontext;
  GstMatroskaMux *mux;
  GstMatroskaPad *collect_pad;
  const gchar *mimetype;
  gint samplerate = 0, channels = 0;
  GstStructure *structure;

  mux = GST_MATROSKA_MUX (GST_PAD_PARENT (pad));

  /* find context */
  collect_pad = (GstMatroskaPad *) gst_pad_get_element_private (pad);
  g_assert (collect_pad);
  context = collect_pad->track;
  g_assert (context);
  g_assert (context->type == GST_MATROSKA_TRACK_TYPE_AUDIO);
  audiocontext = (GstMatroskaTrackAudioContext *) context;

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* general setup */
  gst_structure_get_int (structure, "rate", &samplerate);
  gst_structure_get_int (structure, "channels", &channels);

  audiocontext->samplerate = samplerate;
  audiocontext->channels = channels;
  audiocontext->bitdepth = 0;
  context->default_duration = 0;

  if (!strcmp (mimetype, "audio/mpeg")) {
    gint mpegversion = 0;

    gst_structure_get_int (structure, "mpegversion", &mpegversion);
    switch (mpegversion) {
      case 1:{
        gint layer;

        gst_structure_get_int (structure, "layer", &layer);
        switch (layer) {
          case 1:
            context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1);
            context->default_duration =
                384 * GST_SECOND / audiocontext->samplerate;
            break;
          case 2:
            context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2);
            context->default_duration =
                1152 * GST_SECOND / audiocontext->samplerate;
            break;
          case 3:
            context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3);
            context->default_duration =
                1152 * GST_SECOND / audiocontext->samplerate;
            break;
          default:
            return FALSE;
        }
        break;
      }
      case 2:
        context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG2 "MAIN");
        break;
      case 4:
        context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG4 "MAIN");
        break;
      default:
        return FALSE;
    }

    return TRUE;
  } else if (!strcmp (mimetype, "audio/x-raw-int")) {
    gint endianness, width, depth;
    gboolean signedness;

    if (!gst_structure_get_int (structure, "width", &width) ||
        !gst_structure_get_int (structure, "depth", &depth) ||
        !gst_structure_get_boolean (structure, "signed", &signedness)) {
      GST_DEBUG_OBJECT (mux, "broken caps, width/depth/signed field missing");
      return FALSE;
    }

    if (depth > 8 &&
        !gst_structure_get_int (structure, "endianness", &endianness)) {
      GST_DEBUG_OBJECT (mux, "broken caps, no endianness specified");
      return FALSE;
    }

    if (width != depth) {
      GST_DEBUG_OBJECT (mux, "width must be same as depth!");
      return FALSE;
    }

    /* where is this spec'ed out? (tpm) */
    if ((width == 8 && signedness) || (width == 16 && !signedness)) {
      GST_DEBUG_OBJECT (mux, "8-bit PCM must be unsigned, 16-bit PCM signed");
      return FALSE;
    }

    audiocontext->bitdepth = depth;
    if (endianness == G_BIG_ENDIAN)
      context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE);
    else
      context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE);

    return TRUE;
  } else if (!strcmp (mimetype, "audio/x-raw-float")) {
    /* FIXME: endianness is undefined */
  } else if (!strcmp (mimetype, "audio/x-vorbis")) {
    const GValue *streamheader;

    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_VORBIS);

    if (context->codec_priv != NULL) {
      g_free (context->codec_priv);
      context->codec_priv = NULL;
      context->codec_priv_size = 0;
    }

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (!vorbis_streamheader_to_codecdata (streamheader, context)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("vorbis stream headers missing or malformed"));
      return FALSE;
    }
    return TRUE;
  } else if (!strcmp (mimetype, "audio/x-ac3")) {
    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_AC3);

    return TRUE;
  } else if (!strcmp (mimetype, "audio/x-tta")) {
    gint width;

    /* TTA frame duration */
    context->default_duration = 1.04489795918367346939 * GST_SECOND;

    gst_structure_get_int (structure, "width", &width);
    audiocontext->bitdepth = width;
    context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_TTA);

    return TRUE;
  }

  return FALSE;
}


/**
 * gst_matroska_mux_subtitle_pad_setcaps:
 * @pad: Pad which got the caps.
 * @caps: New caps.
 *
 * Setcaps function for subtitle sink pad.
 *
 * Returns: #TRUE on success.
 */
static gboolean
gst_matroska_mux_subtitle_pad_setcaps (GstPad * pad, GstCaps * caps)
{
  /* Consider this as boilerplate code for now. There is
   * no single subtitle creation element in GStreamer,
   * neither do I know how subtitling works at all. */

  return FALSE;
}


/**
 * gst_matroska_mux_request_new_pad:
 * @element: #GstMatroskaMux.
 * @templ: #GstPadTemplate.
 * @pad_name: New pad name.
 *
 * Request pad function for sink templates.
 *
 * Returns: New #GstPad.
 */
static GstPad *
gst_matroska_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * pad_name)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);
  GstMatroskaPad *collect_pad;
  GstPad *newpad = NULL;
  gchar *name = NULL;
  GstPadSetCapsFunction setcapsfunc = NULL;
  GstMatroskaTrackContext *context = NULL;

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    name = g_strdup_printf ("audio_%d", mux->num_a_streams++);
    setcapsfunc = gst_matroska_mux_audio_pad_setcaps;
    context = (GstMatroskaTrackContext *)
        g_new0 (GstMatroskaTrackAudioContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_AUDIO;
    context->name = g_strdup ("Audio");
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    name = g_strdup_printf ("video_%d", mux->num_v_streams++);
    setcapsfunc = gst_matroska_mux_video_pad_setcaps;
    context = (GstMatroskaTrackContext *)
        g_new0 (GstMatroskaTrackVideoContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_VIDEO;
    context->name = g_strdup ("Video");
  } else if (templ == gst_element_class_get_pad_template (klass, "subtitle_%d")) {
    name = g_strdup_printf ("subtitle_%d", mux->num_t_streams++);
    setcapsfunc = gst_matroska_mux_subtitle_pad_setcaps;
    context = (GstMatroskaTrackContext *)
        g_new0 (GstMatroskaTrackSubtitleContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_SUBTITLE;
    context->name = g_strdup ("Subtitle");
  } else {
    GST_WARNING_OBJECT (mux, "This is not our template!");
    return NULL;
  }

  newpad = gst_pad_new_from_template (templ, name);
  g_free (name);
  collect_pad = (GstMatroskaPad *)
      gst_collect_pads_add_pad (mux->collect, newpad, sizeof (GstMatroskaPad));
  context->flags = GST_MATROSKA_TRACK_ENABLED | GST_MATROSKA_TRACK_DEFAULT;
  collect_pad->track = context;
  collect_pad->buffer = NULL;
  collect_pad->start_ts = GST_CLOCK_TIME_NONE;
  collect_pad->end_ts = GST_CLOCK_TIME_NONE;

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events.
   * TODO GstCollectPads should really give its 'users' a clean chance to
   * properly handle events that are not meant for collectpads itself.
   * Perhaps a callback or so, though rejected (?) in #340060.
   * This would allow (clean) transcoding of info from demuxer/streams
   * to another muxer */
  mux->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (newpad);
  gst_pad_set_event_function (newpad,
      GST_DEBUG_FUNCPTR (gst_matroska_mux_handle_sink_event));

  gst_pad_set_setcaps_function (newpad, setcapsfunc);
  gst_pad_set_active (newpad, TRUE);
  gst_element_add_pad (element, newpad);

  return newpad;
}

/**
 * gst_matroska_mux_release_pad:
 * @element: #GstMatroskaMux.
 * @pad: Pad to release.
 *
 * Release a previously requested pad.
*/
static void
gst_matroska_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstMatroskaMux *mux;
  GSList *walk;

  mux = GST_MATROSKA_MUX (GST_PAD_PARENT (pad));

  for (walk = mux->collect->data; walk; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstMatroskaPad *collect_pad = (GstMatroskaPad *) cdata;

    if (cdata->pad == pad) {
      GstClockTime min_dur;     /* observed minimum duration */

      /* no need to check if start_ts and end_ts are set, in the worst case
       * they're both -1 and we'll end up with a duration of 0 again */
      min_dur = GST_CLOCK_DIFF (collect_pad->start_ts, collect_pad->end_ts);
      if (collect_pad->duration < min_dur)
        collect_pad->duration = min_dur;
      if (collect_pad->duration > mux->duration)
        mux->duration = collect_pad->duration;
      gst_matroska_pad_free (collect_pad);
      gst_collect_pads_remove_pad (mux->collect, pad);
      gst_element_remove_pad (element, pad);
      return;
    }
  }

  g_warning ("%s: unknown pad %s", GST_FUNCTION, GST_PAD_NAME (pad));
}


/**
 * gst_matroska_mux_track_header:
 * @mux: #GstMatroskaMux
 * @context: Tack context.
 *
 * Write a track header.
 */
static void
gst_matroska_mux_track_header (GstMatroskaMux * mux,
    GstMatroskaTrackContext * context)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  guint64 master;

  /* track type goes before the type-specific stuff */
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKNUMBER, context->num);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKTYPE, context->type);

  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKUID,
      gst_matroska_mux_create_uid ());
  if (context->default_duration) {
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKDEFAULTDURATION,
        context->default_duration);
  }
  if (context->language) {
    gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_TRACKLANGUAGE,
        context->language);
  }

  /* type-specific stuff */
  switch (context->type) {
    case GST_MATROSKA_TRACK_TYPE_VIDEO:{
      GstMatroskaTrackVideoContext *videocontext =
          (GstMatroskaTrackVideoContext *) context;

      master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKVIDEO);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOPIXELWIDTH,
          videocontext->pixel_width);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOPIXELHEIGHT,
          videocontext->pixel_height);
      if (videocontext->display_width && videocontext->display_height) {
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEODISPLAYWIDTH,
            videocontext->display_width);
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEODISPLAYHEIGHT,
            videocontext->display_height);
      }
      if (context->flags & GST_MATROSKA_VIDEOTRACK_INTERLACED)
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOFLAGINTERLACED, 1);
      if (videocontext->fourcc) {
        guint32 fcc_le = GUINT32_TO_LE (videocontext->fourcc);

        gst_ebml_write_binary (ebml, GST_MATROSKA_ID_VIDEOCOLOURSPACE,
            (gpointer) & fcc_le, 4);
      }
      gst_ebml_write_master_finish (ebml, master);

      break;
    }

    case GST_MATROSKA_TRACK_TYPE_AUDIO:{
      GstMatroskaTrackAudioContext *audiocontext =
          (GstMatroskaTrackAudioContext *) context;

      master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKAUDIO);
      if (audiocontext->samplerate != 8000)
        gst_ebml_write_float (ebml, GST_MATROSKA_ID_AUDIOSAMPLINGFREQ,
            audiocontext->samplerate);
      if (audiocontext->channels != 1)
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_AUDIOCHANNELS,
            audiocontext->channels);
      if (audiocontext->bitdepth) {
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_AUDIOBITDEPTH,
            audiocontext->bitdepth);
      }
      gst_ebml_write_master_finish (ebml, master);

      break;
    }

    default:
      /* doesn't need type-specific data */
      break;
  }

  gst_ebml_write_ascii (ebml, GST_MATROSKA_ID_CODECID, context->codec_id);
  if (context->codec_priv)
    gst_ebml_write_binary (ebml, GST_MATROSKA_ID_CODECPRIVATE,
        context->codec_priv, context->codec_priv_size);
  /* FIXME: until we have a nice way of getting the codecname
   * out of the caps, I'm not going to enable this. Too much
   * (useless, double, boring) work... */
  /*gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_CODECNAME,
     context->codec_name); */
  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_TRACKNAME, context->name);
}


/**
 * gst_matroska_mux_start:
 * @mux: #GstMatroskaMux
 *
 * Start a new matroska file (write headers etc...)
 */
static void
gst_matroska_mux_start (GstMatroskaMux * mux)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  guint32 seekhead_id[] = { GST_MATROSKA_ID_INFO,
    GST_MATROSKA_ID_TRACKS,
    GST_MATROSKA_ID_CUES,
    GST_MATROSKA_ID_SEEKHEAD,
    GST_MATROSKA_ID_TAGS,
    0
  };
  guint64 master, child;
  GSList *collected;
  int i;
  guint tracknum = 1;
  gdouble duration = 0;
  guint32 *segment_uid = (guint32 *) g_malloc (16);
  GRand *rand = g_rand_new ();
  GTimeVal time = { 0, 0 };

  /* we start with a EBML header */
  gst_ebml_write_header (ebml, "matroska", mux->matroska_version);

  /* start a segment */
  mux->segment_pos =
      gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEGMENT);
  mux->segment_master = ebml->pos;

  /* the rest of the header is cached */
  gst_ebml_write_set_cache (ebml, 0x1000);

  /* seekhead (table of contents) - we set the positions later */
  mux->seekhead_pos = ebml->pos;
  master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEEKHEAD);
  for (i = 0; seekhead_id[i] != 0; i++) {
    child = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEEKENTRY);
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_SEEKID, seekhead_id[i]);
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_SEEKPOSITION, -1);
    gst_ebml_write_master_finish (ebml, child);
  }
  gst_ebml_write_master_finish (ebml, master);

  /* segment info */
  mux->info_pos = ebml->pos;
  master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_INFO);
  for (i = 0; i < 4; i++) {
    segment_uid[i] = g_rand_int (rand);
  }
  g_free (rand);
  gst_ebml_write_binary (ebml, GST_MATROSKA_ID_SEGMENTUID,
      (guint8 *) segment_uid, 16);
  g_free (segment_uid);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TIMECODESCALE, mux->time_scale);
  mux->duration_pos = ebml->pos;
  /* get duration */
  for (collected = mux->collect->data; collected;
      collected = g_slist_next (collected)) {
    GstMatroskaPad *collect_pad;
    GstFormat format = GST_FORMAT_TIME;
    GstPad *thepad;
    gint64 trackduration;

    collect_pad = (GstMatroskaPad *) collected->data;
    thepad = collect_pad->collect.pad;

    /* Query the total length of the track. */
    GST_DEBUG_OBJECT (thepad, "querying peer duration");
    if (gst_pad_query_peer_duration (thepad, &format, &trackduration)) {
      GST_DEBUG_OBJECT (thepad, "duration: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (trackduration));
      if (trackduration != GST_CLOCK_TIME_NONE &&
          (gdouble) trackduration > duration) {
        duration = (gdouble) trackduration;
      }
    }
  }
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_DURATION,
      duration / gst_guint64_to_gdouble (mux->time_scale));
  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_MUXINGAPP,
      "GStreamer plugin version " PACKAGE_VERSION);
  if (mux->writing_app && mux->writing_app[0]) {
    gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_WRITINGAPP, mux->writing_app);
  }
  g_get_current_time (&time);
  gst_ebml_write_date (ebml, GST_MATROSKA_ID_DATEUTC, time.tv_sec);
  gst_ebml_write_master_finish (ebml, master);

  /* tracks */
  mux->tracks_pos = ebml->pos;
  master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKS);

  for (collected = mux->collect->data; collected;
      collected = g_slist_next (collected)) {

    GstMatroskaPad *collect_pad;
    GstPad *thepad;

    collect_pad = (GstMatroskaPad *) collected->data;
    thepad = collect_pad->collect.pad;

    if (gst_pad_is_linked (thepad) && gst_pad_is_active (thepad) &&
        collect_pad->track->codec_id != 0) {
      collect_pad->track->num = tracknum++;
      child = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKENTRY);
      gst_matroska_mux_track_header (mux, collect_pad->track);
      gst_ebml_write_master_finish (ebml, child);
    }
  }
  gst_ebml_write_master_finish (ebml, master);

  /* lastly, flush the cache */
  gst_ebml_write_flush_cache (ebml);
}

static void
gst_matroska_mux_write_simple_tag (const GstTagList * list, const gchar * tag,
    gpointer data)
{
  struct
  {
    gchar *matroska_tagname;
    gchar *gstreamer_tagname;
  }
  tag_conv[] = {
    {
    GST_MATROSKA_TAG_ID_TITLE, GST_TAG_TITLE}, {
    GST_MATROSKA_TAG_ID_AUTHOR, GST_TAG_ARTIST}, {
    GST_MATROSKA_TAG_ID_ALBUM, GST_TAG_ALBUM}, {
    GST_MATROSKA_TAG_ID_COMMENTS, GST_TAG_COMMENT}, {
    GST_MATROSKA_TAG_ID_BITSPS, GST_TAG_BITRATE}, {
    GST_MATROSKA_TAG_ID_DATE, GST_TAG_DATE}, {
    GST_MATROSKA_TAG_ID_ISRC, GST_TAG_ISRC}, {
    GST_MATROSKA_TAG_ID_COPYRIGHT, GST_TAG_COPYRIGHT}
  };
  GstEbmlWrite *ebml = (GstEbmlWrite *) data;
  guint i;
  guint64 simpletag_master;

  for (i = 0; i < G_N_ELEMENTS (tag_conv); i++) {
    const gchar *tagname_gst = tag_conv[i].gstreamer_tagname;
    const gchar *tagname_mkv = tag_conv[i].matroska_tagname;

    if (strcmp (tagname_gst, tag) == 0) {
      GValue src = { 0, };
      GValue dest = { 0, };

      if (!gst_tag_list_copy_value (&src, list, tag))
        break;
      g_value_init (&dest, G_TYPE_STRING);
      g_value_transform (&src, &dest);
      g_value_unset (&src);

      simpletag_master = gst_ebml_write_master_start (ebml,
          GST_MATROSKA_ID_SIMPLETAG);
      gst_ebml_write_ascii (ebml, GST_MATROSKA_ID_TAGNAME, tagname_mkv);
      gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_TAGSTRING,
          g_value_get_string (&dest));
      gst_ebml_write_master_finish (ebml, simpletag_master);
      g_value_unset (&dest);
      break;
    }
  }
}


/**
 * gst_matroska_mux_finish:
 * @mux: #GstMatroskaMux
 *
 * Finish a new matroska file (write index etc...)
 */
static void
gst_matroska_mux_finish (GstMatroskaMux * mux)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  guint64 pos;
  guint64 duration = 0;
  GSList *collected;
  GstTagList *tags;

  /* finish last cluster */
  if (mux->cluster) {
    gst_ebml_write_master_finish (ebml, mux->cluster);
  }

  /* cues */
  if (mux->index != NULL) {
    guint n;
    guint64 master, pointentry_master, trackpos_master;

    mux->cues_pos = ebml->pos;
    gst_ebml_write_set_cache (ebml, 12 + 41 * mux->num_indexes);
    master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CUES);

    for (n = 0; n < mux->num_indexes; n++) {
      GstMatroskaIndex *idx = &mux->index[n];

      pointentry_master = gst_ebml_write_master_start (ebml,
          GST_MATROSKA_ID_POINTENTRY);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CUETIME,
          idx->time / mux->time_scale);
      trackpos_master = gst_ebml_write_master_start (ebml,
          GST_MATROSKA_ID_CUETRACKPOSITION);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CUETRACK, idx->track);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CUECLUSTERPOSITION,
          idx->pos - mux->segment_master);
      gst_ebml_write_master_finish (ebml, trackpos_master);
      gst_ebml_write_master_finish (ebml, pointentry_master);
    }

    gst_ebml_write_master_finish (ebml, master);
    gst_ebml_write_flush_cache (ebml);
  }

  if (mux->meta_index != NULL) {
    guint n;
    guint64 master, seekentry_master;

    mux->meta_pos = ebml->pos;
    gst_ebml_write_set_cache (ebml, 12 + 28 * mux->num_meta_indexes);
    master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEEKHEAD);

    for (n = 0; n < mux->num_meta_indexes; n++) {
      GstMatroskaMetaSeekIndex *idx = &mux->meta_index[n];

      seekentry_master = gst_ebml_write_master_start (ebml,
          GST_MATROSKA_ID_SEEKENTRY);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_SEEKID, idx->id);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_SEEKPOSITION,
          idx->pos - mux->segment_master);
      gst_ebml_write_master_finish (ebml, seekentry_master);
    }
    gst_ebml_write_master_finish (ebml, master);
  }
  gst_ebml_write_flush_cache (ebml);

  /* tags */
  tags = gst_tag_list_merge (gst_tag_setter_get_tag_list (GST_TAG_SETTER (mux)),
      mux->tags, GST_TAG_MERGE_APPEND);

  if (tags != NULL) {
    guint64 master_tags, master_tag;

    mux->tags_pos = ebml->pos;
    master_tags = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAGS);
    master_tag = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAG);
    gst_tag_list_foreach (tags, gst_matroska_mux_write_simple_tag, ebml);
    gst_ebml_write_master_finish (ebml, master_tag);
    gst_ebml_write_master_finish (ebml, master_tags);
    gst_tag_list_free (tags);
  }

  /* update seekhead. We know that:
   * - a seekhead contains 4 entries.
   * - order of entries is as above.
   * - a seekhead has a 4-byte header + 8-byte length
   * - each entry is 2-byte master, 2-byte ID pointer,
   *     2-byte length pointer, all 8/1-byte length, 4-
   *     byte ID and 8-byte length pointer, where the
   *     length pointer starts at 20.
   * - all entries are local to the segment (so pos - segment_master).
   * - so each entry is at 12 + 20 + num * 28. */
  gst_ebml_replace_uint (ebml, mux->seekhead_pos + 32,
      mux->info_pos - mux->segment_master);
  gst_ebml_replace_uint (ebml, mux->seekhead_pos + 60,
      mux->tracks_pos - mux->segment_master);
  if (mux->index != NULL) {
    gst_ebml_replace_uint (ebml, mux->seekhead_pos + 88,
        mux->cues_pos - mux->segment_master);
  } else {
    /* void'ify */
    guint64 my_pos = ebml->pos;

    gst_ebml_write_seek (ebml, mux->seekhead_pos + 68);
    gst_ebml_write_buffer_header (ebml, GST_EBML_ID_VOID, 26);
    gst_ebml_write_seek (ebml, my_pos);
  }
  if (mux->meta_index != NULL) {
    gst_ebml_replace_uint (ebml, mux->seekhead_pos + 116,
        mux->meta_pos - mux->segment_master);
  } else {
    /* void'ify */
    guint64 my_pos = ebml->pos;

    gst_ebml_write_seek (ebml, mux->seekhead_pos + 96);
    gst_ebml_write_buffer_header (ebml, GST_EBML_ID_VOID, 26);
    gst_ebml_write_seek (ebml, my_pos);
  }
  if (tags != NULL) {
    gst_ebml_replace_uint (ebml, mux->seekhead_pos + 144,
        mux->tags_pos - mux->segment_master);
  } else {
    /* void'ify */
    guint64 my_pos = ebml->pos;

    gst_ebml_write_seek (ebml, mux->seekhead_pos + 124);
    gst_ebml_write_buffer_header (ebml, GST_EBML_ID_VOID, 26);
    gst_ebml_write_seek (ebml, my_pos);
  }

  /* update duration */
  /* first get the overall duration */
  /* a released track may have left a duration in here */
  duration = mux->duration;
  for (collected = mux->collect->data; collected;
      collected = g_slist_next (collected)) {
    GstMatroskaPad *collect_pad;
    GstClockTime min_duration;  /* observed minimum duration */

    collect_pad = (GstMatroskaPad *) collected->data;

    /* no need to check if start_ts and end_ts are set, in the worst case
     * they're both -1 and we'll end up with a duration of 0 again */
    min_duration = GST_CLOCK_DIFF (collect_pad->start_ts, collect_pad->end_ts);
    if (collect_pad->duration < min_duration)
      collect_pad->duration = min_duration;
    GST_DEBUG_OBJECT (collect_pad, "final track duration: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (collect_pad->duration));
    if (collect_pad->duration > duration)
      duration = collect_pad->duration;
  }
  if (duration != 0) {
    GST_DEBUG_OBJECT (mux, "final total duration: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (duration));
    pos = mux->ebml_write->pos;
    gst_ebml_write_seek (ebml, mux->duration_pos);
    gst_ebml_write_float (ebml, GST_MATROSKA_ID_DURATION,
        gst_guint64_to_gdouble (duration / mux->time_scale));
    gst_ebml_write_seek (ebml, pos);
  }

  /* finish segment - this also writes element length */
  gst_ebml_write_master_finish (ebml, mux->segment_pos);
}


/**
 * gst_matroska_mux_best_pad:
 * @mux: #GstMatroskaMux
 * @popped: True if at least one buffer was popped from #GstCollectPads
 *
 * Find a pad with the oldest data 
 * (data from this pad should be written first).
 *
 * Returns: Selected pad.
 */
static GstMatroskaPad *
gst_matroska_mux_best_pad (GstMatroskaMux * mux, gboolean * popped)
{
  GSList *collected;
  GstMatroskaPad *best = NULL;

  *popped = FALSE;
  for (collected = mux->collect->data; collected;
      collected = g_slist_next (collected)) {
    GstMatroskaPad *collect_pad;

    collect_pad = (GstMatroskaPad *) collected->data;
    /* fetch a new buffer if needed */
    if (collect_pad->buffer == NULL) {
      collect_pad->buffer = gst_collect_pads_pop (mux->collect,
          (GstCollectData *) collect_pad);

      if (collect_pad->buffer != NULL)
        *popped = TRUE;
    }

    /* if we have a buffer check if it is better then the current best one */
    if (collect_pad->buffer != NULL) {
      if (best == NULL || !GST_BUFFER_TIMESTAMP_IS_VALID (collect_pad->buffer)
          || (GST_BUFFER_TIMESTAMP_IS_VALID (best->buffer)
              && GST_BUFFER_TIMESTAMP (collect_pad->buffer) <
              GST_BUFFER_TIMESTAMP (best->buffer))) {
        best = collect_pad;
      }
    }
  }

  return best;
}

/**
 * gst_matroska_mux_buffer_header:
 * @track: Track context.
 * @relative_timestamp: relative timestamp of the buffer
 * @flags: Buffer flags.
 *
 * Create a buffer containing buffer header.
 * 
 * Returns: New buffer.
 */
GstBuffer *
gst_matroska_mux_create_buffer_header (GstMatroskaTrackContext * track,
    gint16 relative_timestamp, int flags)
{
  GstBuffer *hdr;

  hdr = gst_buffer_new_and_alloc (4);
  /* track num - FIXME: what if num >= 0x80 (unlikely)? */
  GST_BUFFER_DATA (hdr)[0] = track->num | 0x80;
  /* time relative to clustertime */
  GST_WRITE_UINT16_BE (GST_BUFFER_DATA (hdr) + 1, relative_timestamp);

  /* flags */
  GST_BUFFER_DATA (hdr)[3] = flags;

  return hdr;
}

/**
 * gst_matroska_mux_write_data:
 * @mux: #GstMatroskaMux
 * @collect_pad: #GstMatroskaPad with the data
 *
 * Write collected data (called from gst_matroska_mux_collected).
 *
 * Returns: Result of the gst_pad_push issued to write the data.
 */
static GstFlowReturn
gst_matroska_mux_write_data (GstMatroskaMux * mux, GstMatroskaPad * collect_pad)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  GstBuffer *buf, *hdr;
  guint64 cluster, blockgroup;
  gboolean write_duration;
  gint16 relative_timestamp;
  gint64 relative_timestamp64;
  guint64 block_duration;
  gboolean is_video_keyframe = FALSE;

  /* write data */
  buf = collect_pad->buffer;
  collect_pad->buffer = NULL;

  /* vorbis/theora headers are retrieved from caps and put in CodecPrivate */
  if (collect_pad->track->xiph_headers_to_skip > 0) {
    GST_LOG_OBJECT (collect_pad->collect.pad, "dropping streamheader buffer");
    gst_buffer_unref (buf);
    --collect_pad->track->xiph_headers_to_skip;
    return GST_FLOW_OK;
  }

  /* hm, invalid timestamp (due to --to be fixed--- element upstream);
   * this would wreak havoc with time stored in matroska file */
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    GST_WARNING_OBJECT (collect_pad->collect.pad,
        "Invalid buffer timestamp; dropping buffer");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  /* set the timestamp for outgoing buffers */
  ebml->timestamp = GST_BUFFER_TIMESTAMP (buf);

  if (collect_pad->track->type == GST_MATROSKA_TRACK_TYPE_VIDEO &&
      !GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT)) {
    GST_LOG_OBJECT (mux, "have video keyframe, ts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
    is_video_keyframe = TRUE;
  }

  if (mux->cluster) {
    /* start a new cluster every two seconds or at keyframe */
    if (mux->cluster_time + GST_SECOND * 2 < GST_BUFFER_TIMESTAMP (buf)
        || is_video_keyframe) {
      GstMatroskaMetaSeekIndex *idx;

      gst_ebml_write_master_finish (ebml, mux->cluster);
      mux->cluster_pos = ebml->pos;
      mux->cluster =
          gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CLUSTER);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CLUSTERTIMECODE,
          GST_BUFFER_TIMESTAMP (buf) / mux->time_scale);
      mux->cluster_time = GST_BUFFER_TIMESTAMP (buf);

      if (mux->num_meta_indexes % 32 == 0) {
        mux->meta_index = g_renew (GstMatroskaMetaSeekIndex, mux->meta_index,
            mux->num_meta_indexes + 32);
      }
      idx = &mux->meta_index[mux->num_meta_indexes++];
      idx->id = GST_MATROSKA_ID_CLUSTER;
      idx->pos = mux->cluster_pos;
    }
  } else {
    /* first cluster */
    GstMatroskaMetaSeekIndex *idx;

    mux->cluster_pos = ebml->pos;
    mux->cluster = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CLUSTER);
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CLUSTERTIMECODE,
        GST_BUFFER_TIMESTAMP (buf) / mux->time_scale);
    mux->cluster_time = GST_BUFFER_TIMESTAMP (buf);

    if (mux->num_meta_indexes % 32 == 0) {
      mux->meta_index = g_renew (GstMatroskaMetaSeekIndex, mux->meta_index,
          mux->num_meta_indexes + 32);
    }
    idx = &mux->meta_index[mux->num_meta_indexes++];
    idx->id = GST_MATROSKA_ID_CLUSTER;
    idx->pos = mux->cluster_pos;
  }
  cluster = mux->cluster;

  /* update duration of this track */
  if (GST_BUFFER_DURATION_IS_VALID (buf))
    collect_pad->duration += GST_BUFFER_DURATION (buf);

  /* We currently write an index entry for each keyframe in a
   * video track or one entry for each cluster in an audio track
   * for audio only files. This can be largely improved, such as doing
   * one for each keyframe or each second (for all-keyframe
   * streams), only the *first* video track. But that'll come later... */
  if (is_video_keyframe) {
    GstMatroskaIndex *idx;

    if (mux->num_indexes % 32 == 0) {
      mux->index = g_renew (GstMatroskaIndex, mux->index,
          mux->num_indexes + 32);
    }
    idx = &mux->index[mux->num_indexes++];

    idx->pos = mux->cluster_pos;
    idx->time = GST_BUFFER_TIMESTAMP (buf);
    idx->track = collect_pad->track->num;
  } else if ((collect_pad->track->type == GST_MATROSKA_TRACK_TYPE_AUDIO) &&
      (mux->num_streams == 1)) {
    GstMatroskaIndex *idx;

    if (mux->num_indexes % 32 == 0) {
      mux->index = g_renew (GstMatroskaIndex, mux->index,
          mux->num_indexes + 32);
    }
    idx = &mux->index[mux->num_indexes++];

    idx->pos = mux->cluster_pos;
    idx->time = GST_BUFFER_TIMESTAMP (buf);
    idx->track = collect_pad->track->num;
  }

  /* Check if the duration differs from the default duration. */
  write_duration = FALSE;
  block_duration = GST_BUFFER_DURATION (buf);
  if (GST_BUFFER_DURATION_IS_VALID (buf)) {
    if (block_duration != collect_pad->track->default_duration) {
      write_duration = TRUE;
    }
  }

  /* write the block, for matroska v2 use SimpleBlock if possible
   * one slice (*breath*).
   * FIXME: lacing, etc. */
  relative_timestamp64 = GST_BUFFER_TIMESTAMP (buf) - mux->cluster_time;
  if (relative_timestamp64 >= 0) {
    /* round the timestamp */
    relative_timestamp64 += mux->time_scale / 2;
  } else {
    /* round the timestamp */
    relative_timestamp64 -= mux->time_scale / 2;
  }
  relative_timestamp = relative_timestamp64 / (gint64) mux->time_scale;
  if (mux->matroska_version > 1 && !write_duration) {
    int flags =
        GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT) ? 0 : 0x80;

    hdr =
        gst_matroska_mux_create_buffer_header (collect_pad->track,
        relative_timestamp, flags);
    gst_ebml_write_buffer_header (ebml, GST_MATROSKA_ID_SIMPLEBLOCK,
        GST_BUFFER_SIZE (buf) + GST_BUFFER_SIZE (hdr));
    gst_ebml_write_buffer (ebml, hdr);
    gst_ebml_write_buffer (ebml, buf);

    return gst_ebml_last_write_result (ebml);
  } else {
    blockgroup = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_BLOCKGROUP);
    hdr =
        gst_matroska_mux_create_buffer_header (collect_pad->track,
        relative_timestamp, 0);
    gst_ebml_write_buffer_header (ebml, GST_MATROSKA_ID_BLOCK,
        GST_BUFFER_SIZE (buf) + GST_BUFFER_SIZE (hdr));
    gst_ebml_write_buffer (ebml, hdr);
    gst_ebml_write_buffer (ebml, buf);
    if (write_duration) {
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_BLOCKDURATION,
          block_duration / mux->time_scale);
    }
    gst_ebml_write_master_finish (ebml, blockgroup);
    return gst_ebml_last_write_result (ebml);
  }
}


/**
 * gst_matroska_mux_collected:
 * @pads: #GstCollectPads
 * @uuser_data: #GstMatroskaMux
 *
 * Collectpads callback.
 *
 * Returns: #GstFlowReturn
 */
static GstFlowReturn
gst_matroska_mux_collected (GstCollectPads * pads, gpointer user_data)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (user_data);
  GstMatroskaPad *best;
  gboolean popped;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (mux, "Collected pads");

  /* start with a header */
  if (mux->state == GST_MATROSKA_MUX_STATE_START) {
    if (mux->collect->data == NULL) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("No input streams configured"));
      return GST_FLOW_ERROR;
    }
    mux->state = GST_MATROSKA_MUX_STATE_HEADER;
    gst_matroska_mux_start (mux);
    mux->state = GST_MATROSKA_MUX_STATE_DATA;
  }

  do {
    /* which stream to write from? */
    best = gst_matroska_mux_best_pad (mux, &popped);

    /* if there is no best pad, we have reached EOS */
    if (best == NULL) {
      GST_DEBUG_OBJECT (mux, "No best pad finishing...");
      gst_matroska_mux_finish (mux);
      gst_pad_push_event (mux->srcpad, gst_event_new_eos ());
      ret = GST_FLOW_UNEXPECTED;
      break;
    }
    GST_DEBUG_OBJECT (best->collect.pad, "best pad");

    /* make note of first and last encountered timestamps, so we can calculate
     * the actual duration later when we send an updated header on eos */
    best->end_ts = GST_BUFFER_TIMESTAMP (best->buffer);
    if (GST_BUFFER_DURATION_IS_VALID (best->buffer))
      best->end_ts += GST_BUFFER_DURATION (best->buffer);
    else if (best->track->default_duration)
      best->end_ts += best->track->default_duration;

    if (G_UNLIKELY (best->start_ts == GST_CLOCK_TIME_NONE)) {
      best->start_ts = GST_BUFFER_TIMESTAMP (best->buffer);
    }

    /* write one buffer */
    ret = gst_matroska_mux_write_data (mux, best);
  } while (ret == GST_FLOW_OK && !popped);

  return ret;
}


/**
 * gst_matroska_mux_change_state:
 * @element: #GstMatroskaMux
 * @transition: State change transition.
 *
 * Change the muxer state.
 *
 * Returns: #GstStateChangeReturn
 */
static GstStateChangeReturn
gst_matroska_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);

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
      gst_matroska_mux_reset (GST_ELEMENT (mux));
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_matroska_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstMatroskaMux *mux;

  g_return_if_fail (GST_IS_MATROSKA_MUX (object));
  mux = GST_MATROSKA_MUX (object);

  switch (prop_id) {
    case ARG_WRITING_APP:
      if (!g_value_get_string (value)) {
        GST_WARNING_OBJECT (mux, "writing-app property can not be NULL");
        break;
      }
      g_free (mux->writing_app);
      mux->writing_app = g_strdup (g_value_get_string (value));
      break;
    case ARG_MATROSKA_VERSION:
      mux->matroska_version = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_matroska_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstMatroskaMux *mux;

  g_return_if_fail (GST_IS_MATROSKA_MUX (object));
  mux = GST_MATROSKA_MUX (object);

  switch (prop_id) {
    case ARG_WRITING_APP:
      g_value_set_string (value, mux->writing_app);
      break;
    case ARG_MATROSKA_VERSION:
      g_value_set_int (value, mux->matroska_version);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_matroska_mux_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "matroskamux",
      GST_RANK_NONE, GST_TYPE_MATROSKA_MUX);
}
