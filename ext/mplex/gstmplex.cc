/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmplex.cc: gstreamer mplex wrapper
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

#include "gstmplex.hh"
#include "gstmplexoutputstream.hh"
#include "gstmplexibitstream.hh"
#include "gstmplexjob.hh"

static GstStaticPadTemplate src_templ =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/mpeg, "
      "systemstream = (boolean) true"
  )
);

static GstStaticPadTemplate video_sink_templ =
GST_STATIC_PAD_TEMPLATE (
  "video_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_STATIC_CAPS (
    "video/mpeg, "
      "mpegversion = (int) [ 1, 2 ], "
      "systemstream = (boolean) false"
  )
);

static GstStaticPadTemplate audio_sink_templ =
GST_STATIC_PAD_TEMPLATE (
  "audio_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_STATIC_CAPS (
    "audio/mpeg, "
      "mpegversion = (int) 1, "
      "layer = (int) [ 1, 2 ]; "
    "audio/x-ac3; "
    "audio/x-dts; "
    "audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (boolean) TRUE, "
      "width = (int) { 16, 20, 24 }, "
      "depth = (int) { 16, 20, 24 }, "
      "rate = (int) { 48000, 96000 }, "
      "channels = (int) [ 1, 6 ]"
  )
);

/* FIXME: subtitles */

static void gst_mplex_base_init    (GstMplexClass *klass);
static void gst_mplex_class_init   (GstMplexClass *klass);
static void gst_mplex_init         (GstMplex   *enc);
static void gst_mplex_dispose      (GObject    *object);

static void gst_mplex_loop         (GstElement *element);

static GstPad *gst_mplex_request_new_pad (GstElement     *element,
					  GstPadTemplate *templ,
					  const gchar    *name);

static GstElementStateReturn
            gst_mplex_change_state (GstElement *element);

static void gst_mplex_get_property (GObject    *object,
				    guint       prop_id, 	
				    GValue     *value,
				    GParamSpec *pspec);
static void gst_mplex_set_property (GObject    *object,
				    guint       prop_id, 	
				    const GValue *value,
				    GParamSpec *pspec);

static GstElementClass *parent_class = NULL;

GType
gst_mplex_get_type (void)
{
  static GType gst_mplex_type = 0;

  if (!gst_mplex_type) {
    static const GTypeInfo gst_mplex_info = {
      sizeof (GstMplexClass),      
      (GBaseInitFunc) gst_mplex_base_init,
      NULL,
      (GClassInitFunc) gst_mplex_class_init,
      NULL,
      NULL,
      sizeof (GstMplex),
      0,
      (GInstanceInitFunc) gst_mplex_init,
    };

    gst_mplex_type =
	g_type_register_static (GST_TYPE_ELEMENT,
				"GstMplex",
				&gst_mplex_info,
				(GTypeFlags) 0);
  }

  return gst_mplex_type;
}

static void
gst_mplex_base_init (GstMplexClass *klass)
{
  static GstElementDetails gst_mplex_details = {
    "mplex video multiplexer",
    "Codec/Muxer",
    "High-quality MPEG/DVD/SVCD/VCD video/audio multiplexer",
    "Andrew Stevens <andrew.stevens@nexgo.de>\n"
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&src_templ));
  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&video_sink_templ));
  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&audio_sink_templ));
  gst_element_class_set_details (element_class,
				 &gst_mplex_details);
}

static void
gst_mplex_class_init (GstMplexClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  /* register arguments */
  mjpeg_default_handler_verbosity (0);
  GstMplexJob::initProperties (object_class);

  object_class->set_property = gst_mplex_set_property;
  object_class->get_property = gst_mplex_get_property;

  object_class->dispose = gst_mplex_dispose;

  element_class->change_state = gst_mplex_change_state;
  element_class->request_new_pad = gst_mplex_request_new_pad;
}

static void
gst_mplex_dispose (GObject *object)
{
  GstMplex *mplex = GST_MPLEX (object);

  if (mplex->mux) {
    delete mplex->mux;
    mplex->mux = NULL;
  }
  delete mplex->job;
}

static void
gst_mplex_init (GstMplex *mplex)
{
  GstElement *element = GST_ELEMENT (mplex);

  GST_FLAG_SET (element, GST_ELEMENT_EVENT_AWARE);

  mplex->srcpad = gst_pad_new_from_template (
	gst_element_get_pad_template (element, "src"), "src");
  gst_element_add_pad (element, mplex->srcpad);

  mplex->job = new GstMplexJob ();
  mplex->mux = NULL;
  mplex->num_apads = 0;
  mplex->num_vpads = 0;

  gst_element_set_loop_function (element, gst_mplex_loop);
}

static void
gst_mplex_loop (GstElement *element)
{
  GstMplex *mplex = GST_MPLEX (element);

  if (!mplex->mux) {
    GstMplexOutputStream *out;
    const GList *item;

    for (item = gst_element_get_pad_list (element);
         item != NULL; item = item->next) {
      StreamKind type;
      GstMplexIBitStream *inputstream;
      JobStream *jobstream;
      GstPad *pad = GST_PAD (item->data);
      GstStructure *structure;
      const GstCaps *caps;
      const gchar *mime;

      /* skip our source pad */
      if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
        continue;

      /* create inputstream, assure we've got caps */
      inputstream = new GstMplexIBitStream (pad);

      /* skip unnegotiated pads */
      if (!(caps = GST_PAD_CAPS (pad))) {
        delete inputstream;
        continue;
      }

      /* get format */
      structure = gst_caps_get_structure (caps, 0);
      mime = gst_structure_get_name (structure);

      if (!strcmp (mime, "video/mpeg")) {
        VideoParams *params;

        type = MPEG_VIDEO;

        params = VideoParams::Default (mplex->job->mux_format);
        mplex->job->video_param.push_back (params);
        mplex->job->video_tracks++;
      } else if (!strcmp (mime, "audio/mpeg")) {
        type = MPEG_AUDIO;
        mplex->job->audio_tracks++;
      } else if (!strcmp (mime, "audio/x-ac3")) {
        type = AC3_AUDIO;
        mplex->job->audio_tracks++;
      } else if (!strcmp (mime, "audio/x-dts")) {
        type = DTS_AUDIO;
        mplex->job->audio_tracks++;
      } else if (!strcmp (mime, "audio/x-raw-int")) {
        LpcmParams *params;
        gint bits, chans, rate;

        type = LPCM_AUDIO;

        /* set LPCM params */
        gst_structure_get_int (structure, "depth", &bits);
        gst_structure_get_int (structure, "rate", &rate);
        gst_structure_get_int (structure, "channels", &chans);
        params = LpcmParams::Checked (rate, chans, bits);

        mplex->job->lpcm_param.push_back (params);
        mplex->job->audio_tracks++;
        mplex->job->lpcm_tracks++;
      } else {
        delete inputstream;
        continue;
      }

      jobstream = new JobStream (inputstream, type);
      mplex->job->streams.push_back (jobstream);
    }

    if (!mplex->job->video_tracks && !mplex->job->audio_tracks) {
      gst_element_error (element,
			 "No input stream set-up");
      return;
    }

    /* create new encoder with inputs/output */
    out = new GstMplexOutputStream (element, mplex->srcpad);
    mplex->mux = new Multiplexor (*mplex->job, *out);
  }

  mplex->mux->Multiplex ();
}

static GstPadLinkReturn
gst_mplex_sink_link (GstPad        *pad,
		     const GstCaps *caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  const gchar *mime = gst_structure_get_name (structure);

  /* raw audio caps needs to be fixed */
  if (!strcmp (mime, "audio/x-raw-int")) {
    gint width, depth;

    if (!gst_caps_is_fixed (caps))
      return GST_PAD_LINK_DELAYED;

    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "depth", &depth);

    if (depth != width)
      return GST_PAD_LINK_REFUSED;
  }

  /* we do the actual inputstream setup in our first loopfunc cycle */
  return GST_PAD_LINK_OK;
}

static GstPad *
gst_mplex_request_new_pad (GstElement     *element,
			   GstPadTemplate *templ,
			   const gchar    *name)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstMplex *mplex = GST_MPLEX (element);
  gchar *padname;
  GstPad *newpad;

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    padname = g_strdup_printf ("audio_%d", mplex->num_apads++);
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    padname = g_strdup_printf ("video_%d", mplex->num_vpads++);
  } else {
    g_warning ("mplex: this is not our template!");
    return NULL;
  }

  newpad = gst_pad_new_from_template (templ, padname);
  gst_pad_set_link_function (newpad, gst_mplex_sink_link);
  gst_element_add_pad (element, newpad);
  g_free (padname);

  return newpad;
}

static void
gst_mplex_get_property (GObject    *object,
			guint       prop_id, 	
			GValue     *value,
			GParamSpec *pspec)
{
  GST_MPLEX (object)->job->getProperty (prop_id, value);
}

static void
gst_mplex_set_property (GObject      *object,
			guint         prop_id, 	
			const GValue *value,
			GParamSpec   *pspec)
{
  GST_MPLEX (object)->job->setProperty (prop_id, value);
}

static GstElementStateReturn
gst_mplex_change_state (GstElement  *element)
{
  GstMplex *mplex = GST_MPLEX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      delete mplex->mux;
      mplex->mux = NULL;
      mplex->num_apads = 0;
      mplex->num_vpads = 0;
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  return gst_element_register (plugin, "mplex",
			       GST_RANK_NONE,
			       GST_TYPE_MPLEX);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mplex",
  "High-quality MPEG/DVD/SVCD/VCD video/audio multiplexer",
  plugin_init,
  VERSION,
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN
)
