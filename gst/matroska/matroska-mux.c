/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METADATA,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "matroskamux_src",
     "video/x-matroska",
       NULL
  )
)

/* FIXME: caps */

GST_PAD_TEMPLATE_FACTORY (videosink_templ,
  "video_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  NULL
)

GST_PAD_TEMPLATE_FACTORY (audiosink_templ,
  "audio_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  NULL
)

GST_PAD_TEMPLATE_FACTORY (subtitlesink_templ,
  "subtitle_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  NULL
)

/* gobject magic foo */
static void     gst_matroska_mux_base_init	    (GstMatroskaMuxClass *klass);
static void 	gst_matroska_mux_class_init	    (GstMatroskaMuxClass *klass);
static void 	gst_matroska_mux_init		    (GstMatroskaMux *mux);

/* element functions */
static void 	gst_matroska_mux_loop 	            (GstElement  *element);

/* pad functions */
static GstPad *	gst_matroska_mux_request_new_pad    (GstElement *element,
						     GstPadTemplate *templ,
						     const gchar *name);

/* gst internal change state handler */
static GstElementStateReturn
		gst_matroska_mux_change_state       (GstElement  *element);

/* gobject bla bla */
static void     gst_matroska_mux_set_property       (GObject     *object,
						     guint        prop_id, 	
						     const GValue *value,
						     GParamSpec  *pspec);
static void     gst_matroska_mux_get_property       (GObject     *object,
						     guint        prop_id, 	
						     GValue      *value,
						     GParamSpec  *pspec);

/* reset muxer */
static void     gst_matroska_mux_reset	            (GstElement  *element);

static GstEbmlWriteClass *parent_class = NULL;
/*static guint gst_matroska_mux_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_matroska_mux_get_type (void) 
{
  static GType gst_matroska_mux_type = 0;

  if (!gst_matroska_mux_type) {
    static const GTypeInfo gst_matroska_mux_info = {
      sizeof (GstMatroskaMuxClass),      
      (GBaseInitFunc) gst_matroska_mux_base_init,
      NULL,
      (GClassInitFunc) gst_matroska_mux_class_init,
      NULL,
      NULL,
      sizeof (GstMatroskaMux),
      0,
      (GInstanceInitFunc) gst_matroska_mux_init,
    };

    gst_matroska_mux_type =
	g_type_register_static (GST_TYPE_EBML_WRITE,
				"GstMatroskaMmux",
				&gst_matroska_mux_info, 0);
  }

  return gst_matroska_mux_type;
}

static void
gst_matroska_mux_base_init (GstMatroskaMuxClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_matroska_mux_details = {
    "Matroska muxer",
    "Codec/Muxer",
    "Muxes video/audio/subtitle streams into a matroska stream",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };

  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (videosink_templ));
  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (audiosink_templ));
  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (subtitlesink_templ));
  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (src_templ));
  gst_element_class_set_details (element_class,
		&gst_matroska_mux_details);
}

static void
gst_matroska_mux_class_init (GstMatroskaMuxClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata",
                        GST_TYPE_CAPS, G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_EBML_WRITE);

  gobject_class->get_property = gst_matroska_mux_get_property;
  gobject_class->set_property = gst_matroska_mux_set_property;

  gstelement_class->change_state = gst_matroska_mux_change_state;
  gstelement_class->request_new_pad = gst_matroska_mux_request_new_pad;
}

static void 
gst_matroska_mux_init (GstMatroskaMux *mux) 
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mux);
  gint i;

  mux->srcpad = gst_pad_new_from_template (
	gst_element_class_get_pad_template (klass, "src"), "src");
  gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);
  GST_EBML_WRITE (mux)->srcpad = mux->srcpad;

  gst_element_set_loop_function (GST_ELEMENT (mux),
				 gst_matroska_mux_loop);

  /* initial stream no. */
  for (i = 0; i < GST_MATROSKA_MUX_MAX_STREAMS; i++) {
    mux->sink[i].buffer = NULL;
    mux->sink[i].track = NULL;
  }
  mux->index = NULL;

  /* finish off */
  gst_matroska_mux_reset (GST_ELEMENT (mux));
}

static void
gst_matroska_mux_reset (GstElement *element)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);
  guint i;

  /* reset input */
  mux->state = GST_MATROSKA_MUX_STATE_START;

  /* clean up existing streams */
  for (i = 0; i < GST_MATROSKA_MUX_MAX_STREAMS; i++) {
    if (mux->sink[i].track != NULL) {
      if (mux->sink[i].track->pad != NULL) {
        gst_element_remove_pad (GST_ELEMENT (mux), mux->sink[i].track->pad);
      }
      g_free (mux->sink[i].track->codec_id);
      g_free (mux->sink[i].track->codec_name);
      g_free (mux->sink[i].track->name);
      g_free (mux->sink[i].track->language);
      g_free (mux->sink[i].track->codec_priv);
      g_free (mux->sink[i].track);
      mux->sink[i].track = NULL;
    }
    if (mux->sink[i].buffer != NULL) {
      gst_buffer_unref (mux->sink[i].buffer);
      mux->sink[i].buffer = NULL;
    }
    mux->sink[i].eos = FALSE;
  }
  mux->num_streams = 0;
  mux->num_a_streams = 0;
  mux->num_t_streams = 0;
  mux->num_v_streams = 0;

  /* reset media info  (to default) */
  gst_caps_replace (&mux->metadata,
		    GST_CAPS_NEW ("matroska_metadata",
				  "application/x-gst-metadata",
				    "application", GST_PROPS_STRING (""),
				    "date",        GST_PROPS_STRING ("")));

  /* reset indexes */
  mux->num_indexes = 0;
  g_free (mux->index);
  mux->index = NULL;

  /* reset timers */
  mux->time_scale = 1000000;
  mux->duration = 0;
}

static GstPadLinkReturn
gst_matroska_mux_video_pad_link (GstPad  *pad,
				 GstCaps *caps)
{
  GstMatroskaTrackContext *context = NULL;
  GstMatroskaTrackVideoContext *videocontext;
  GstMatroskaMux *mux = GST_MATROSKA_MUX (gst_pad_get_parent (pad));
  const gchar *mimetype;
  gint width, height, pixel_width, pixel_height, i;
  gfloat framerate;

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  /* find context */
  for (i = 0; i < mux->num_streams; i++) {
    if (mux->sink[i].track && mux->sink[i].track->pad &&
        mux->sink[i].track->pad == pad) {
      context = mux->sink[i].track;
      break;
    }
  }
  g_assert (i < mux->num_streams);
  g_assert (context->type == GST_MATROSKA_TRACK_TYPE_VIDEO);
  videocontext = (GstMatroskaTrackVideoContext *) context;

  /* gst -> matroska ID'ing */
  for (; caps != NULL; caps = caps->next) {
    mimetype = gst_caps_get_mime (caps);

    /* get general properties */
    gst_caps_get (caps,
		  "width",     &width,
		  "height",    &height,
		  "framerate", &framerate,
		  NULL);
    videocontext->pixel_width = width;
    videocontext->pixel_height = height;
    context->default_duration = GST_SECOND / framerate;

    if (gst_caps_has_property (caps, "pixel_width") &&
        gst_caps_has_property (caps, "pixel_height")) {
      gst_caps_get (caps,
		    "pixel_width",  &pixel_width,
		    "pixel_height", &pixel_height,
		    NULL);
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

    videocontext->asr_mode = GST_MATROSKA_ASPECT_RATIO_MODE_FREE;
    videocontext->eye_mode = GST_MATROSKA_EYE_MODE_MONO;
    videocontext->fourcc = 0;

    /* find type */
    if (!strcmp (mimetype, "video/x-raw-yuv")) {
      context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED);
      gst_caps_get_fourcc_int (caps, "format", &videocontext->fourcc);

      return GST_PAD_LINK_OK;
    } else if (!strcmp (mimetype, "video/x-jpeg")) {
      context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MJPEG);

      return GST_PAD_LINK_OK;
    } else if (!strcmp (mimetype, "video/x-divx")) {
      gint divxversion;

      gst_caps_get_int (caps, "divxversion", &divxversion);
      switch (divxversion) {
        case 3:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3);
          break;
        case 4:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP);
          break;
        case 5:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP);
          break;
      }

      return GST_PAD_LINK_OK;
    } else if (!strcmp (mimetype, "video/x-xvid")) {
      context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP);

      return GST_PAD_LINK_OK;
    } else if (!strcmp (mimetype, "video/mpeg")) {
      gint mpegversion;

      gst_caps_get_int (caps, "mpegversion", &mpegversion);
      switch (mpegversion) {
        case 1:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG1);
          break;
        case 2:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG2);
          break;
        case 3:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP);
          break;
      }

      return GST_PAD_LINK_OK;
    } else if (!strcmp (mimetype, "video/x-msmpeg")) {
      context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3);

      return GST_PAD_LINK_OK;
    }
  }

  return GST_PAD_LINK_REFUSED;
}

static GstPadLinkReturn
gst_matroska_mux_audio_pad_link (GstPad  *pad,
				 GstCaps *caps)
{
  GstMatroskaTrackContext *context = NULL;
  GstMatroskaTrackAudioContext *audiocontext;
  GstMatroskaMux *mux = GST_MATROSKA_MUX (gst_pad_get_parent (pad));
  const gchar *mimetype;
  gint samplerate, channels, i;

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  /* find context */
  for (i = 0; i < mux->num_streams; i++) {
    if (mux->sink[i].track && mux->sink[i].track->pad &&
        mux->sink[i].track->pad == pad) {
      context = mux->sink[i].track;
      break;
    }
  }
  g_assert (i < mux->num_streams);
  g_assert (context->type == GST_MATROSKA_TRACK_TYPE_AUDIO);
  audiocontext = (GstMatroskaTrackAudioContext *) context;

  for (; caps != NULL; caps = caps->next) {
    mimetype = gst_caps_get_mime (caps);

    /* general setup */
    gst_caps_get (caps,
		  "rate",     &samplerate,
		  "channels", &channels,
		  NULL);
    audiocontext->samplerate = samplerate;
    audiocontext->channels = channels;
    audiocontext->bitdepth = 16;

    if (!strcmp (mimetype, "audio/mpeg")) {
      gint mpegversion = 1;

      gst_caps_get_int (caps, "mpegversion", &mpegversion);
      switch (mpegversion) {
        case 1: {
          gint layer;

          gst_caps_get_int (caps, "layer", &layer);
          switch (layer) {
            case 1:
              context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1);
              break;
            case 2:
              context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2);
              break;
            case 3:
              context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3);
              break;
          }
          break;
        }
        case 2:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG2
					"MAIN");
          break;
        case 4:
          context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_MPEG4
					"MAIN");
          break;
      }

      return GST_PAD_LINK_OK;
    } else if (!strcmp (mimetype, "audio/x-raw-int")) {
      gint endianness, width, depth;
      gboolean signedness;

      gst_caps_get (caps,
		    "endianness", &endianness,
		    "width",      &width,
		    "depth",      &depth,
		    "signed",     &signedness,
		    NULL);
      if (width != depth ||
	  (width == 8 && signedness) || (width == 16 && !signedness))
        continue;

      audiocontext->bitdepth = depth;
      if (endianness == G_BIG_ENDIAN)
        context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE);
      else
        context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE);

      return GST_PAD_LINK_OK;
    } else if (!strcmp (mimetype, "audio/x-raw-float")) {
      /* FIXME: endianness is undefined */
    } else if (!strcmp (mimetype, "audio/x-vorbis")) {
      /* FIXME: private data setup needs work */
    } else if (!strcmp (mimetype, "audio/x-ac3")) {
      context->codec_id = g_strdup (GST_MATROSKA_CODEC_ID_AUDIO_AC3);

      return GST_PAD_LINK_OK;
    }
  }

  return GST_PAD_LINK_REFUSED;
}

static GstPadLinkReturn
gst_matroska_mux_subtitle_pad_link (GstPad  *pad,
				    GstCaps *caps)
{
  /* Consider this as boilerplate code for now. There is
   * no single subtitle creation element in GStreamer,
   * neither do I know how subtitling works at all. */

  return GST_PAD_LINK_REFUSED;
}

static GstPad *
gst_matroska_mux_request_new_pad (GstElement     *element,
				  GstPadTemplate *templ,
				  const gchar    *pad_name)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  GstPadLinkFunction linkfunc = NULL;
  GstMatroskaTrackContext *context = NULL;

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    name = g_strdup_printf ("audio_%d", mux->num_a_streams++);
    linkfunc = gst_matroska_mux_audio_pad_link;
    context = (GstMatroskaTrackContext *)
		g_new0 (GstMatroskaTrackAudioContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_AUDIO;
    context->name = g_strdup ("Audio");
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    name = g_strdup_printf ("video_%d", mux->num_v_streams++);
    linkfunc = gst_matroska_mux_video_pad_link;
    context = (GstMatroskaTrackContext *)
		g_new0 (GstMatroskaTrackVideoContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_VIDEO;
    context->name = g_strdup ("Video");
  } else if (templ == gst_element_class_get_pad_template (klass, "subtitle_%d")) {
    name = g_strdup_printf ("subtitle_%d", mux->num_t_streams++);
    linkfunc = gst_matroska_mux_subtitle_pad_link;
    context = (GstMatroskaTrackContext *)
		g_new0 (GstMatroskaTrackSubtitleContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_SUBTITLE;
    context->name = g_strdup ("Subtitle");
  } else {
    g_warning ("matroskamux: this is not our template!");
    return NULL;
  }

  pad = gst_pad_new_from_template (templ, name);
  g_free (name);
  gst_element_add_pad (element, pad);
  gst_pad_set_link_function (pad, linkfunc);
  context->index = mux->num_streams++;
  mux->sink[context->index].track = context;
  context->pad = pad;
  context->flags = GST_MATROSKA_TRACK_ENABLED |
		   GST_MATROSKA_TRACK_DEFAULT;
  
  return pad;
}

static void
gst_matroska_mux_track_header (GstMatroskaMux          *mux,
			       GstMatroskaTrackContext *context)
{
  GstEbmlWrite *ebml = GST_EBML_WRITE (mux);
  guint64 master;

  /* track type goes before the type-specific stuff */
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKNUMBER, context->num);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKTYPE, context->type);

  /* type-specific stuff */
  switch (context->type) {
    case GST_MATROSKA_TRACK_TYPE_VIDEO: {
      GstMatroskaTrackVideoContext *videocontext =
	(GstMatroskaTrackVideoContext *) context;

      /* framerate, but not in the video part */
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKDEFAULTDURATION,
			   context->default_duration);

      master = gst_ebml_write_master_start (ebml,
				GST_MATROSKA_ID_TRACKVIDEO);
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
			       (gpointer) &fcc_le, 4);
      }
      gst_ebml_write_master_finish (ebml, master);

      break;
    }

    case GST_MATROSKA_TRACK_TYPE_AUDIO: {
      GstMatroskaTrackAudioContext *audiocontext =
	(GstMatroskaTrackAudioContext *) context;

      master = gst_ebml_write_master_start (ebml,
				GST_MATROSKA_ID_TRACKAUDIO);
      if (audiocontext->samplerate != 8000)
        gst_ebml_write_float (ebml, GST_MATROSKA_ID_AUDIOSAMPLINGFREQ,
			      audiocontext->samplerate);
      if (audiocontext->channels != 1)
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_AUDIOCHANNELS,
			     audiocontext->channels);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_AUDIOBITDEPTH,
			   audiocontext->bitdepth);
      gst_ebml_write_master_finish (ebml, master);

      break;
    }

    default:
      /* doesn't need type-specific data */
      break;
  }

  gst_ebml_write_ascii (ebml, GST_MATROSKA_ID_CODECID,
			context->codec_id);
  if (context->codec_priv)
    gst_ebml_write_binary (ebml, GST_MATROSKA_ID_CODECPRIVATE,
			   context->codec_priv, context->codec_priv_size);
  /* FIXME: until we have a nice way of getting the codecname
   * out of the caps, I'm not going to enable this. Too much
   * (useless, double, boring) work... */
  /*gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_CODECNAME,
		       context->codec_name);*/
  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_TRACKNAME,
		       context->name);
}

static void
gst_matroska_mux_start (GstMatroskaMux *mux)
{
  GstEbmlWrite *ebml = GST_EBML_WRITE (mux);
  guint32 seekhead_id[] = { GST_MATROSKA_ID_INFO,
			    GST_MATROSKA_ID_TRACKS,
			    GST_MATROSKA_ID_CUES,
#if 0
			    GST_MATROSKA_ID_TAGS,
#endif
			    0 };
  guint64 master, child;
  gint i;
  guint tracknum = 1;

  /* we start with a EBML header */
  gst_ebml_write_header (ebml, "matroska", 1);

  /* start a segment */
  mux->segment_pos = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEGMENT);
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
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TIMECODESCALE, mux->time_scale);
  mux->duration_pos = ebml->pos;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_DURATION, 0);
  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_MUXINGAPP, "GStreamer");
  if (mux->metadata &&
      gst_caps_has_property (mux->metadata, "application")) {
    const gchar *app;

    gst_caps_get_string (mux->metadata, "application", &app);
    if (app && app[0]) {
      gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_WRITINGAPP, app);
    }
  }
  /* FIXME: how do I get this? Automatic? Via tags? */
  /*gst_ebml_write_date (ebml, GST_MATROSKA_ID_DATEUTC, 0);*/
  gst_ebml_write_master_finish (ebml, master);

  /* tracks */
  mux->tracks_pos = ebml->pos;
  master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKS);
  for (i = 0; i < mux->num_streams; i++) {
    if (GST_PAD_IS_USABLE (mux->sink[i].track->pad)) {
      mux->sink[i].track->num = tracknum++;
      child = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKENTRY);
      gst_matroska_mux_track_header (mux, mux->sink[i].track);
      gst_ebml_write_master_finish (ebml, child);
    }
  }
  gst_ebml_write_master_finish (ebml, master);

  /* lastly, flush the cache */
  gst_ebml_write_flush_cache (ebml);
}

static void
gst_matroska_mux_finish (GstMatroskaMux *mux)
{
  GstEbmlWrite *ebml = GST_EBML_WRITE (mux);
  guint64 pos;

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
      gst_ebml_write_date (ebml, GST_MATROSKA_ID_CUETIME,
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

  /* FIXME: tags */

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
#if 0
  gst_ebml_replace_uint (ebml, mux->seekhead_pos + 116,
			 mux->tags_pos - mux->segment_master);
#endif

  /* update duration */
  pos = GST_EBML_WRITE (mux)->pos;
  gst_ebml_write_seek (ebml, mux->duration_pos);
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_DURATION,
			mux->duration / mux->time_scale);
  gst_ebml_write_seek (ebml, pos);

  /* finish segment - this also writes element length */
  gst_ebml_write_master_finish (ebml, mux->segment_pos);
}

static gint
gst_matroska_mux_prepare_data (GstMatroskaMux *mux)
{
  gint i, first = -1;

  for (i = 0; i < mux->num_streams; i++) {
    while (!mux->sink[i].eos && !mux->sink[i].buffer &&
           mux->sink[i].track->num > 0 &&
           GST_PAD_IS_USABLE (mux->sink[i].track->pad)) {
      GstData *data;

      data = gst_pad_pull (mux->sink[i].track->pad);
      if (GST_IS_EVENT (data)) {
        if (GST_EVENT_TYPE (GST_EVENT (data)) == GST_EVENT_EOS)
          mux->sink[i].eos = TRUE;
        gst_event_unref (GST_EVENT (data));
      } else {
        mux->sink[i].buffer = GST_BUFFER (data);
      }
    }

    if (mux->sink[i].buffer) {
      if (first < 0 || GST_BUFFER_TIMESTAMP (mux->sink[i].buffer) <
			 GST_BUFFER_TIMESTAMP (mux->sink[first].buffer))
        first = i;
    }
  }

  return first;
}

static void
gst_matroska_mux_write_data (GstMatroskaMux *mux)
{
  GstEbmlWrite *ebml = GST_EBML_WRITE (mux);
  GstBuffer *buf, *hdr;
  gint i;
  guint64 cluster, blockgroup;

  /* which stream-num to write from? */
  if ((i = gst_matroska_mux_prepare_data (mux)) < 0) {
    GstEvent *event = gst_event_new (GST_EVENT_EOS);

    gst_matroska_mux_finish (mux);
    gst_pad_push (mux->srcpad, GST_DATA (event));
    gst_element_set_eos (GST_ELEMENT (mux));

    return;
  }

  /* write data */
  buf = mux->sink[i].buffer;
  mux->sink[i].buffer = NULL;

  /* We currently write an index entry for each keyframe in a
   * video track. This can be largely improved, such as doing
   * one for each keyframe or each second (for all-keyframe
   * streams), only the *first* video track or the audio track
   * if we have no video tracks. But that'll come later... */
  if (mux->sink[i].track->type == GST_MATROSKA_TRACK_TYPE_VIDEO &&
      GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_KEY_UNIT)) {
    GstMatroskaIndex *idx;

    if (mux->num_indexes % 32 == 0) {
      mux->index = g_renew (GstMatroskaIndex, mux->index,
			    mux->num_indexes + 32);
    }
    idx = &mux->index[mux->num_indexes++];

    idx->pos   = ebml->pos;
    idx->time  = GST_BUFFER_TIMESTAMP (buf);
    idx->track = mux->sink[i].track->num;
  }

  /* write one cluster with one blockgroup with one block with
   * one slice (*breath*).
   * FIXME: lacing, multiple frames/cluster, etc. */
  cluster = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CLUSTER);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CLUSTERTIMECODE,
		       GST_BUFFER_TIMESTAMP (buf) / mux->time_scale);
  blockgroup = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_BLOCKGROUP);
  gst_ebml_write_buffer_header (ebml, GST_MATROSKA_ID_BLOCK,
				GST_BUFFER_SIZE (buf) + 4);
  hdr = gst_buffer_new_and_alloc (4);
  /* track num - FIXME: what if num >= 0x80 (unlikely)? */
  GST_BUFFER_DATA (hdr)[0] = mux->sink[i].track->num | 0x80;
  /* time relative to clustertime - we don't use this yet */
  * (guint16 *) &GST_BUFFER_DATA (hdr)[1] = GUINT16_TO_BE (0);
  /* flags - no lacing (yet) */
  GST_BUFFER_DATA (hdr)[3] = 0;
  gst_ebml_write_buffer (ebml, hdr);
  gst_ebml_write_buffer (ebml, buf);
  gst_ebml_write_master_finish (ebml, blockgroup);
  gst_ebml_write_master_finish (ebml, cluster);
}

static void
gst_matroska_mux_loop (GstElement *element)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);

  /* start with a header */
  if (mux->state == GST_MATROSKA_MUX_STATE_START) {
    mux->state = GST_MATROSKA_MUX_STATE_HEADER;
    gst_matroska_mux_start (mux);
    mux->state = GST_MATROSKA_MUX_STATE_DATA;
  }

  /* do one single buffer */
  gst_matroska_mux_write_data (mux);
}

static GstElementStateReturn
gst_matroska_mux_change_state (GstElement *element)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      gst_matroska_mux_reset (GST_ELEMENT (mux));
      break;
    default:
      break;
  }

  if (((GstElementClass *) parent_class)->change_state)
    return ((GstElementClass *) parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_matroska_mux_set_property (GObject      *object,
			       guint         prop_id, 	
			       const GValue *value,
			       GParamSpec   *pspec)
{
  GstMatroskaMux *mux;

  g_return_if_fail (GST_IS_MATROSKA_MUX (object));
  mux = GST_MATROSKA_MUX (object);

  switch (prop_id) {
    case ARG_METADATA:
      gst_caps_replace (&mux->metadata,
			g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_matroska_mux_get_property (GObject    *object,
			       guint       prop_id, 	
			       GValue     *value,
			       GParamSpec *pspec)
{
  GstMatroskaMux *mux;

  g_return_if_fail (GST_IS_MATROSKA_MUX (object));
  mux = GST_MATROSKA_MUX (object);

  switch (prop_id) {
    case ARG_METADATA:
      g_value_set_boxed (value, mux->metadata);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_matroska_mux_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "matroskamux",
			       GST_RANK_NONE,
			       GST_TYPE_MATROSKA_MUX);
}
