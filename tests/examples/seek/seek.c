/* GStreamer
 *
 * seek.c: seeking sample application
 *
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *               2006 Stefan Kost <ensonic@users.sf.net>
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
/* FIXME: remove #if 0 code
 *
 */
#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (seek_debug);
#define GST_CAT_DEFAULT (seek_debug)

/* configuration */

//#define SOURCE "filesrc"
#define SOURCE "gnomevfssrc"

#define ASINK "alsasink"
//#define ASINK "osssink"

#define VSINK "xvimagesink"
//#define VSINK "sdlvideosink"
//#define VSINK "ximagesink"
//#define VSINK "aasink"
//#define VSINK "cacasink"

//#define UPDATE_INTERVAL 500
//#define UPDATE_INTERVAL 100
#define UPDATE_INTERVAL 10

/* number of milliseconds to play for after a seek */
#define SCRUB_TIME 100

/* timeout for gst_element_get_state() after a seek */
#define SEEK_TIMEOUT 40 * GST_MSECOND


static GList *seekable_pads = NULL;
static GList *rate_pads = NULL;
static GList *seekable_elements = NULL;

static gboolean accurate_seek = FALSE;
static gboolean keyframe_seek = FALSE;
static gboolean loop_seek = FALSE;
static gboolean flush_seek = TRUE;
static gboolean scrub = TRUE;
static gboolean play_scrub = FALSE;
static gdouble rate = 1.0;

static GstElement *pipeline;
static gint pipeline_type;
static const gchar *pipeline_spec;
static gint64 position = -1;
static gint64 duration = -1;
static GtkAdjustment *adjustment;
static GtkWidget *hscale;
static gboolean stats = FALSE;
static gboolean elem_seek = FALSE;
static gboolean verbose = FALSE;

static GstState state = GST_STATE_NULL;
static guint update_id = 0;
static guint seek_timeout_id = 0;
static gulong changed_id;

static gint n_video = 0, n_audio = 0, n_text = 0;
static gboolean need_streams = TRUE;
static GtkWidget *video_combo, *audio_combo, *text_combo, *vis_combo;
static GtkWidget *vis_checkbox, *video_checkbox, *audio_checkbox;
static GtkWidget *text_checkbox, *mute_checkbox, *volume_spinbutton;

/* we keep an array of the visualisation entries so that we can easily switch
 * with the combo box index. */
typedef struct
{
  GstElementFactory *factory;
} VisEntry;

static GArray *vis_entries;

static void clear_streams (GstElement * pipeline);

/* pipeline construction */

typedef struct
{
  const gchar *padname;
  GstPad *target;
  GstElement *bin;
}
dyn_link;

static GstElement *
gst_element_factory_make_or_warn (gchar * type, gchar * name)
{
  GstElement *element = gst_element_factory_make (type, name);

  if (!element) {
    g_warning ("Failed to create element %s of type %s", name, type);
  }

  return element;
}

static void
dynamic_link (GstPadTemplate * templ, GstPad * newpad, gpointer data)
{
  gchar *padname;
  dyn_link *connect = (dyn_link *) data;

  padname = gst_pad_get_name (newpad);

  if (connect->padname == NULL || !strcmp (padname, connect->padname)) {
    if (connect->bin)
      gst_bin_add (GST_BIN (pipeline), connect->bin);
    gst_pad_link (newpad, connect->target);

    //seekable_pads = g_list_prepend (seekable_pads, newpad);
    rate_pads = g_list_prepend (rate_pads, newpad);
  }
  g_free (padname);
}

static void
setup_dynamic_link (GstElement * element, const gchar * padname,
    GstPad * target, GstElement * bin)
{
  dyn_link *connect;

  connect = g_new0 (dyn_link, 1);
  connect->padname = g_strdup (padname);
  connect->target = target;
  connect->bin = bin;

  g_signal_connect (G_OBJECT (element), "pad-added", G_CALLBACK (dynamic_link),
      connect);
}

static GstElement *
make_mod_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("modplug", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  //g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, audiosink);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_dv_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *demux, *decoder, *audiosink, *videosink;
  GstElement *a_queue, *v_queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  demux = gst_element_factory_make_or_warn ("dvdemux", "demuxer");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  decoder = gst_element_factory_make_or_warn ("ffdec_dvvideo", "decoder");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  audiosink = gst_element_factory_make_or_warn ("alsasink", "a_sink");

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_bin_add (GST_BIN (pipeline), a_queue);
  gst_bin_add (GST_BIN (pipeline), audiosink);
  gst_bin_add (GST_BIN (pipeline), v_queue);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), videosink);

  gst_element_link (src, demux);
  gst_element_link (a_queue, audiosink);
  gst_element_link (v_queue, decoder);
  gst_element_link (decoder, videosink);

  setup_dynamic_link (demux, "video", gst_element_get_pad (v_queue, "sink"),
      NULL);
  setup_dynamic_link (demux, "audio", gst_element_get_pad (a_queue, "sink"),
      NULL);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);

  return pipeline;
}

static GstElement *
make_wav_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("wavparse", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);

  setup_dynamic_link (decoder, "src", gst_element_get_pad (audiosink, "sink"),
      NULL);

  seekable_elements = g_list_prepend (seekable_elements, audiosink);

  /* force element seeking on this pipeline */
  elem_seek = TRUE;

  return pipeline;
}

static GstElement *
make_flac_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("flacdec", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, audiosink);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_sid_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("siddec", "decoder");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  //g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), audiosink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, audiosink);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_parse_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *parser, *fakesink;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  parser = gst_element_factory_make_or_warn ("mpegparse", "parse");
  fakesink = gst_element_factory_make_or_warn ("fakesink", "sink");
  g_object_set (G_OBJECT (fakesink), "silent", TRUE, NULL);
  g_object_set (G_OBJECT (fakesink), "sync", TRUE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), parser);
  gst_bin_add (GST_BIN (pipeline), fakesink);

  gst_element_link (src, parser);
  gst_element_link (parser, fakesink);

  seekable = gst_element_get_pad (parser, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (parser, "sink"));

  return pipeline;
}

static GstElement *
make_vorbis_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin;
  GstElement *src, *demux, *decoder, *convert, *audiosink;
  GstPad *pad, *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  demux = gst_element_factory_make_or_warn ("oggdemux", "demux");
  decoder = gst_element_factory_make_or_warn ("vorbisdec", "decoder");
  convert = gst_element_factory_make_or_warn ("audioconvert", "convert");
  audiosink = gst_element_factory_make_or_warn (ASINK, "sink");
  g_object_set (G_OBJECT (audiosink), "sync", TRUE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  audio_bin = gst_bin_new ("a_decoder_bin");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_bin_add (GST_BIN (audio_bin), decoder);
  gst_bin_add (GST_BIN (audio_bin), convert);
  gst_bin_add (GST_BIN (audio_bin), audiosink);
  gst_bin_add (GST_BIN (pipeline), audio_bin);

  gst_element_link (src, demux);
  gst_element_link (decoder, convert);
  gst_element_link (convert, audiosink);

  pad = gst_element_get_pad (decoder, "sink");
  gst_element_add_pad (audio_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (audio_bin, "sink"),
      NULL);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_theora_pipeline (const gchar * location)
{
  GstElement *pipeline, *video_bin;
  GstElement *src, *demux, *decoder, *convert, *videosink;
  GstPad *pad, *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  demux = gst_element_factory_make_or_warn ("oggdemux", "demux");
  decoder = gst_element_factory_make_or_warn ("theoradec", "decoder");
  convert = gst_element_factory_make_or_warn ("ffmpegcolorspace", "convert");
  videosink = gst_element_factory_make_or_warn (VSINK, "sink");

  g_object_set (G_OBJECT (src), "location", location, NULL);

  video_bin = gst_bin_new ("v_decoder_bin");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_bin_add (GST_BIN (video_bin), decoder);
  gst_bin_add (GST_BIN (video_bin), convert);
  gst_bin_add (GST_BIN (video_bin), videosink);
  gst_bin_add (GST_BIN (pipeline), video_bin);

  gst_element_link (src, demux);
  gst_element_link (decoder, convert);
  gst_element_link (convert, videosink);

  pad = gst_element_get_pad (decoder, "sink");
  gst_element_add_pad (video_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (video_bin, "sink"),
      NULL);

  seekable = gst_element_get_pad (decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_vorbis_theora_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *a_convert, *v_decoder, *v_convert;
  GstElement *audiosink, *videosink;
  GstElement *a_queue, *v_queue, *v_scale;
  GstPad *seekable;
  GstPad *pad;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("oggdemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  a_decoder = gst_element_factory_make_or_warn ("vorbisdec", "a_dec");
  a_convert = gst_element_factory_make_or_warn ("audioconvert", "a_convert");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");

  gst_bin_add (GST_BIN (pipeline), audio_bin);

  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_convert);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  gst_element_link (a_queue, a_decoder);
  gst_element_link (a_decoder, a_convert);
  gst_element_link (a_convert, audiosink);

  pad = gst_element_get_pad (a_queue, "sink");
  gst_element_add_pad (audio_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (audio_bin, "sink"),
      NULL);

  video_bin = gst_bin_new ("v_decoder_bin");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  v_decoder = gst_element_factory_make_or_warn ("theoradec", "v_dec");
  v_convert =
      gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_convert");
  v_scale = gst_element_factory_make_or_warn ("videoscale", "v_scale");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");

  gst_bin_add (GST_BIN (pipeline), video_bin);

  gst_bin_add (GST_BIN (video_bin), v_queue);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), v_convert);
  gst_bin_add (GST_BIN (video_bin), v_scale);
  gst_bin_add (GST_BIN (video_bin), videosink);

  gst_element_link_many (v_queue, v_decoder, v_convert, v_scale, videosink,
      NULL);

  pad = gst_element_get_pad (v_queue, "sink");
  gst_element_add_pad (video_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (video_bin, "sink"),
      NULL);

  seekable = gst_element_get_pad (a_decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_avi_msmpeg4v3_mp3_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *a_convert, *v_decoder, *v_convert;
  GstElement *audiosink, *videosink;
  GstElement *a_queue, *v_queue;
  GstPad *seekable, *pad;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("avidemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  a_convert = gst_element_factory_make_or_warn ("audioconvert", "a_convert");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");

  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_convert);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  gst_element_link (a_queue, a_decoder);
  gst_element_link (a_decoder, a_convert);
  gst_element_link (a_convert, audiosink);

  gst_bin_add (GST_BIN (pipeline), audio_bin);

  pad = gst_element_get_pad (a_queue, "sink");
  gst_element_add_pad (audio_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (audio_bin, "sink"),
      NULL);

  video_bin = gst_bin_new ("v_decoder_bin");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  v_decoder = gst_element_factory_make_or_warn ("ffdec_msmpeg4", "v_dec");
  v_convert =
      gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_convert");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");

  gst_bin_add (GST_BIN (video_bin), v_queue);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), v_convert);
  gst_bin_add (GST_BIN (video_bin), videosink);

  gst_element_link_many (v_queue, v_decoder, v_convert, videosink, NULL);

  gst_bin_add (GST_BIN (pipeline), video_bin);

  pad = gst_element_get_pad (v_queue, "sink");
  gst_element_add_pad (video_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, NULL, gst_element_get_pad (video_bin, "sink"),
      NULL);

  seekable = gst_element_get_pad (a_decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_mp3_pipeline (const gchar * location)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *osssink, *queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  decoder = gst_element_factory_make_or_warn ("mad", "dec");
  queue = gst_element_factory_make_or_warn ("queue", "queue");
  osssink = gst_element_factory_make_or_warn (ASINK, "sink");

  seekable_elements = g_list_prepend (seekable_elements, osssink);

  g_object_set (G_OBJECT (src), "location", location, NULL);
  //g_object_set (G_OBJECT (osssink), "fragment", 0x00180008, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), queue);
  gst_bin_add (GST_BIN (pipeline), osssink);

  gst_element_link (src, decoder);
  gst_element_link (decoder, queue);
  gst_element_link (queue, osssink);

  seekable = gst_element_get_pad (queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement *
make_avi_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *audiosink, *videosink;
  GstElement *a_queue = NULL, *v_queue = NULL;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("avidemux", "demux");
  seekable_elements = g_list_prepend (seekable_elements, demux);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  gst_element_link (a_decoder, a_queue);
  gst_element_link (a_queue, audiosink);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), audiosink);
  gst_element_set_state (audio_bin, GST_STATE_PAUSED);

  setup_dynamic_link (demux, "audio_00", gst_element_get_pad (a_decoder,
          "sink"), audio_bin);

  seekable = gst_element_get_pad (a_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  video_bin = gst_bin_new ("v_decoder_bin");
  v_decoder = gst_element_factory_make_or_warn ("ffmpegdecall", "v_dec");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  gst_element_link (v_decoder, v_queue);
  gst_element_link (v_queue, videosink);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), v_queue);
  gst_bin_add (GST_BIN (video_bin), videosink);

  gst_element_set_state (video_bin, GST_STATE_PAUSED);

  setup_dynamic_link (demux, "video_00", gst_element_get_pad (v_decoder,
          "sink"), video_bin);

  seekable = gst_element_get_pad (v_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_mpeg_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *v_filter;
  GstElement *audiosink, *videosink;
  GstElement *a_queue, *v_queue;
  GstPad *seekable;
  GstPad *pad;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  //demux = gst_element_factory_make_or_warn ("mpegdemux", "demux");
  demux = gst_element_factory_make_or_warn ("flupsdemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  gst_element_link (a_decoder, a_queue);
  gst_element_link (a_queue, audiosink);

  gst_bin_add (GST_BIN (pipeline), audio_bin);

  pad = gst_element_get_pad (a_decoder, "sink");
  gst_element_add_pad (audio_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, "audio_c0", gst_element_get_pad (audio_bin,
          "sink"), NULL);

  video_bin = gst_bin_new ("v_decoder_bin");
  v_decoder = gst_element_factory_make_or_warn ("mpeg2dec", "v_dec");
  v_queue = gst_element_factory_make_or_warn ("queue", "v_queue");
  v_filter = gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_filter");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");

  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), v_queue);
  gst_bin_add (GST_BIN (video_bin), v_filter);
  gst_bin_add (GST_BIN (video_bin), videosink);

  gst_element_link (v_decoder, v_queue);
  gst_element_link (v_queue, v_filter);
  gst_element_link (v_filter, videosink);

  gst_bin_add (GST_BIN (pipeline), video_bin);

  pad = gst_element_get_pad (v_decoder, "sink");
  gst_element_add_pad (video_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);

  setup_dynamic_link (demux, "video_e0", gst_element_get_pad (video_bin,
          "sink"), NULL);

  seekable = gst_element_get_pad (v_filter, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_mpegnt_pipeline (const gchar * location)
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *v_filter;
  GstElement *audiosink, *videosink;
  GstElement *a_queue;
  GstPad *seekable;

  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make_or_warn (SOURCE, "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make_or_warn ("mpegdemux", "demux");
  //g_object_set (G_OBJECT (demux), "sync", TRUE, NULL);

  seekable_elements = g_list_prepend (seekable_elements, demux);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_link (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make_or_warn ("mad", "a_dec");
  a_queue = gst_element_factory_make_or_warn ("queue", "a_queue");
  audiosink = gst_element_factory_make_or_warn (ASINK, "a_sink");
  //g_object_set (G_OBJECT (audiosink), "fragment", 0x00180008, NULL);
  g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);
  gst_element_link (a_decoder, a_queue);
  gst_element_link (a_queue, audiosink);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), a_queue);
  gst_bin_add (GST_BIN (audio_bin), audiosink);

  setup_dynamic_link (demux, "audio_00", gst_element_get_pad (a_decoder,
          "sink"), audio_bin);

  seekable = gst_element_get_pad (a_queue, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  video_bin = gst_bin_new ("v_decoder_bin");
  v_decoder = gst_element_factory_make_or_warn ("mpeg2dec", "v_dec");
  v_filter = gst_element_factory_make_or_warn ("ffmpegcolorspace", "v_filter");
  videosink = gst_element_factory_make_or_warn (VSINK, "v_sink");
  gst_element_link_many (v_decoder, v_filter, videosink, NULL);

  gst_bin_add_many (GST_BIN (video_bin), v_decoder, v_filter, videosink, NULL);

  setup_dynamic_link (demux, "video_00", gst_element_get_pad (v_decoder,
          "sink"), video_bin);

  seekable = gst_element_get_pad (v_decoder, "src");
  seekable_pads = g_list_prepend (seekable_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

static GstElement *
make_playerbin_pipeline (const gchar * location)
{
  GstElement *player;

  player = gst_element_factory_make ("playbin", "player");
  g_assert (player);

  g_object_set (G_OBJECT (player), "uri", location, NULL);

  seekable_elements = g_list_prepend (seekable_elements, player);

  /* force element seeking on this pipeline */
  elem_seek = TRUE;

  return player;
}

static GstElement *
make_playerbin2_pipeline (const gchar * location)
{
  GstElement *player;

  player = gst_element_factory_make ("playbin2", "player");
  g_assert (player);

  g_object_set (G_OBJECT (player), "uri", location, NULL);

  seekable_elements = g_list_prepend (seekable_elements, player);

  /* force element seeking on this pipeline */
  elem_seek = TRUE;

  return player;
}

#ifndef GST_DISABLE_PARSE
static GstElement *
make_parselaunch_pipeline (const gchar * description)
{
  GstElement *pipeline;
  GError *error;

  pipeline = gst_parse_launch (description, &error);

  seekable_elements = g_list_prepend (seekable_elements, pipeline);

  elem_seek = TRUE;

  return pipeline;
}
#endif

typedef struct
{
  gchar *name;
  GstElement *(*func) (const gchar * location);
}
Pipeline;

static Pipeline pipelines[] = {
  {"mp3", make_mp3_pipeline},
  {"avi", make_avi_pipeline},
  {"mpeg1", make_mpeg_pipeline},
  {"mpegparse", make_parse_pipeline},
  {"vorbis", make_vorbis_pipeline},
  {"theora", make_theora_pipeline},
  {"ogg/v/t", make_vorbis_theora_pipeline},
  {"avi/msmpeg4v3/mp3", make_avi_msmpeg4v3_mp3_pipeline},
  {"sid", make_sid_pipeline},
  {"flac", make_flac_pipeline},
  {"wav", make_wav_pipeline},
  {"mod", make_mod_pipeline},
  {"dv", make_dv_pipeline},
  {"mpeg1nothreads", make_mpegnt_pipeline},
  {"playerbin", make_playerbin_pipeline},
#ifndef GST_DISABLE_PARSE
  {"parse-launch", make_parselaunch_pipeline},
#endif
  {"playerbin2", make_playerbin2_pipeline},
  {NULL, NULL},
};

#define NUM_TYPES       ((sizeof (pipelines) / sizeof (Pipeline)) - 1)

/* ui callbacks and helpers */

static gchar *
format_value (GtkScale * scale, gdouble value)
{
  gint64 real;
  gint64 seconds;
  gint64 subseconds;

  real = value * duration / 100;
  seconds = (gint64) real / GST_SECOND;
  subseconds = (gint64) real / (GST_SECOND / 100);

  return g_strdup_printf ("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02"
      G_GINT64_FORMAT, seconds / 60, seconds % 60, subseconds % 100);
}

typedef struct
{
  const gchar *name;
  const GstFormat format;
}
seek_format;

static seek_format seek_formats[] = {
  {"tim", GST_FORMAT_TIME},
  {"byt", GST_FORMAT_BYTES},
  {"buf", GST_FORMAT_BUFFERS},
  {"def", GST_FORMAT_DEFAULT},
  {NULL, 0},
};

G_GNUC_UNUSED static void
query_rates (void)
{
  GList *walk = rate_pads;

  while (walk) {
    GstPad *pad = GST_PAD (walk->data);
    gint i = 0;

    g_print ("rate/sec  %8.8s: ", GST_PAD_NAME (pad));
    while (seek_formats[i].name) {
      gint64 value;
      GstFormat format;

      format = seek_formats[i].format;

      if (gst_pad_query_convert (pad, GST_FORMAT_TIME, GST_SECOND, &format,
              &value)) {
        g_print ("%s %13" G_GINT64_FORMAT " | ", seek_formats[i].name, value);
      } else {
        g_print ("%s %13.13s | ", seek_formats[i].name, "*NA*");
      }

      i++;
    }
    g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));

    walk = g_list_next (walk);
  }
}

G_GNUC_UNUSED static void
query_positions_elems (void)
{
  GList *walk = seekable_elements;

  while (walk) {
    GstElement *element = GST_ELEMENT (walk->data);
    gint i = 0;

    g_print ("positions %8.8s: ", GST_ELEMENT_NAME (element));
    while (seek_formats[i].name) {
      gint64 position, total;
      GstFormat format;

      format = seek_formats[i].format;

      if (gst_element_query_position (element, &format, &position) &&
          gst_element_query_duration (element, &format, &total)) {
        g_print ("%s %13" G_GINT64_FORMAT " / %13" G_GINT64_FORMAT " | ",
            seek_formats[i].name, position, total);
      } else {
        g_print ("%s %13.13s / %13.13s | ", seek_formats[i].name, "*NA*",
            "*NA*");
      }
      i++;
    }
    g_print (" %s\n", GST_ELEMENT_NAME (element));

    walk = g_list_next (walk);
  }
}

G_GNUC_UNUSED static void
query_positions_pads (void)
{
  GList *walk = seekable_pads;

  while (walk) {
    GstPad *pad = GST_PAD (walk->data);
    gint i = 0;

    g_print ("positions %8.8s: ", GST_PAD_NAME (pad));
    while (seek_formats[i].name) {
      GstFormat format;
      gint64 position, total;

      format = seek_formats[i].format;

      if (gst_pad_query_position (pad, &format, &position) &&
          gst_pad_query_duration (pad, &format, &total)) {
        g_print ("%s %13" G_GINT64_FORMAT " / %13" G_GINT64_FORMAT " | ",
            seek_formats[i].name, position, total);
      } else {
        g_print ("%s %13.13s / %13.13s | ", seek_formats[i].name, "*NA*",
            "*NA*");
      }

      i++;
    }
    g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));

    walk = g_list_next (walk);
  }
}

static gboolean start_seek (GtkWidget * widget, GdkEventButton * event,
    gpointer user_data);
static gboolean stop_seek (GtkWidget * widget, GdkEventButton * event,
    gpointer user_data);
static void seek_cb (GtkWidget * widget);

static void
set_scale (gdouble value)
{
  g_signal_handlers_block_by_func (hscale, (void *) start_seek,
      (void *) pipeline);
  g_signal_handlers_block_by_func (hscale, (void *) stop_seek,
      (void *) pipeline);
  g_signal_handlers_block_by_func (hscale, (void *) seek_cb, (void *) pipeline);
  gtk_adjustment_set_value (adjustment, value);
  g_signal_handlers_unblock_by_func (hscale, (void *) start_seek,
      (void *) pipeline);
  g_signal_handlers_unblock_by_func (hscale, (void *) stop_seek,
      (void *) pipeline);
  g_signal_handlers_unblock_by_func (hscale, (void *) seek_cb,
      (void *) pipeline);
  gtk_widget_queue_draw (hscale);
}

static gboolean
update_scale (gpointer data)
{
  GstFormat format = GST_FORMAT_TIME;

  //position = 0;
  //duration = 0;

  if (elem_seek) {
    if (seekable_elements) {
      GstElement *element = GST_ELEMENT (seekable_elements->data);

      gst_element_query_position (element, &format, &position);
      gst_element_query_duration (element, &format, &duration);
    }
  } else {
    if (seekable_pads) {
      GstPad *pad = GST_PAD (seekable_pads->data);

      gst_pad_query_position (pad, &format, &position);
      gst_pad_query_duration (pad, &format, &duration);
    }
  }

  if (stats) {
    if (elem_seek) {
      query_positions_elems ();
    } else {
      query_positions_pads ();
    }
    query_rates ();
  }
  if (position >= duration)
    duration = position;

  if (duration > 0) {
    set_scale (position * 100.0 / duration);
  }

  return TRUE;
}

static void do_seek (GtkWidget * widget);
static void connect_bus_signals (GstElement * pipeline);
static void set_update_scale (gboolean active);

static gboolean
end_scrub (GtkWidget * widget)
{
  GST_DEBUG ("end scrub, PAUSE");
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  seek_timeout_id = 0;

  return FALSE;
}

static gboolean
send_event (GstEvent * event)
{
  gboolean res = FALSE;

  if (!elem_seek) {
    GList *walk = seekable_pads;

    while (walk) {
      GstPad *seekable = GST_PAD (walk->data);

      GST_DEBUG ("send event on pad %s:%s", GST_DEBUG_PAD_NAME (seekable));

      gst_event_ref (event);
      res = gst_pad_send_event (seekable, event);

      walk = g_list_next (walk);
    }
  } else {
    GList *walk = seekable_elements;

    while (walk) {
      GstElement *seekable = GST_ELEMENT (walk->data);

      GST_DEBUG ("send event on element %s", GST_ELEMENT_NAME (seekable));

      gst_event_ref (event);
      res = gst_element_send_event (seekable, event);

      walk = g_list_next (walk);
    }
  }
  gst_event_unref (event);
  return res;
}

static void
do_seek (GtkWidget * widget)
{
  gint64 real;
  gboolean res = FALSE;
  GstEvent *s_event;
  GstSeekFlags flags;

  real = gtk_range_get_value (GTK_RANGE (widget)) * duration / 100;

  flags = 0;
  if (flush_seek)
    flags |= GST_SEEK_FLAG_FLUSH;
  if (accurate_seek)
    flags |= GST_SEEK_FLAG_ACCURATE;
  if (keyframe_seek)
    flags |= GST_SEEK_FLAG_KEY_UNIT;
  if (loop_seek)
    flags |= GST_SEEK_FLAG_SEGMENT;

  if (rate >= 0) {
    s_event = gst_event_new_seek (rate,
        GST_FORMAT_TIME, flags, GST_SEEK_TYPE_SET, real, GST_SEEK_TYPE_SET, -1);
    GST_DEBUG ("seek with rate %lf to %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT,
        rate, GST_TIME_ARGS (real), GST_TIME_ARGS (duration));
  } else {
    s_event = gst_event_new_seek (rate,
        GST_FORMAT_TIME, flags, GST_SEEK_TYPE_SET, G_GINT64_CONSTANT (0),
        GST_SEEK_TYPE_SET, real);
    GST_DEBUG ("seek with rate %lf to %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT,
        rate, GST_TIME_ARGS (0), GST_TIME_ARGS (real));
  }

  res = send_event (s_event);

  if (res) {
    if (flush_seek) {
      gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, SEEK_TIMEOUT);
    } else {
      set_update_scale (TRUE);
    }
  } else {
    g_print ("seek failed\n");
    set_update_scale (TRUE);
  }
}

static void
seek_cb (GtkWidget * widget)
{
  /* If the timer hasn't expired yet, then the pipeline is running */
  if (play_scrub && seek_timeout_id != 0) {
    GST_DEBUG ("do scrub seek, PAUSED");
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
  }

  GST_DEBUG ("do seek");
  do_seek (widget);

  if (play_scrub) {
    GST_DEBUG ("do scrub seek, PLAYING");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    if (seek_timeout_id == 0) {
      seek_timeout_id =
          g_timeout_add (SCRUB_TIME, (GSourceFunc) end_scrub, widget);
    }
  }
}

static void
set_update_scale (gboolean active)
{

  GST_DEBUG ("update scale is %d", active);

  if (active) {
    if (update_id == 0) {
      update_id =
          g_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);
    }
  } else {
    if (update_id) {
      g_source_remove (update_id);
      update_id = 0;
    }
  }
}

static gboolean
start_seek (GtkWidget * widget, GdkEventButton * event, gpointer user_data)
{
  if (event->type != GDK_BUTTON_PRESS)
    return FALSE;

  set_update_scale (FALSE);

  if (state == GST_STATE_PLAYING && flush_seek && scrub) {
    GST_DEBUG ("start scrub seek, PAUSE");
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
  }

  if (changed_id == 0 && flush_seek && scrub) {
    changed_id = gtk_signal_connect (GTK_OBJECT (hscale),
        "value_changed", G_CALLBACK (seek_cb), pipeline);
  }

  return FALSE;
}

static gboolean
stop_seek (GtkWidget * widget, GdkEventButton * event, gpointer user_data)
{
  if (changed_id) {
    g_signal_handler_disconnect (GTK_OBJECT (hscale), changed_id);
    changed_id = 0;
  }

  if (!flush_seek || !scrub) {
    GST_DEBUG ("do final seek");
    do_seek (widget);
  }

  if (seek_timeout_id != 0) {
    g_source_remove (seek_timeout_id);
    seek_timeout_id = 0;
    /* Still scrubbing, so the pipeline is playing, see if we need PAUSED
     * instead. */
    if (state == GST_STATE_PAUSED) {
      GST_DEBUG ("stop scrub seek, PAUSED");
      gst_element_set_state (pipeline, GST_STATE_PAUSED);
    }
  } else {
    if (state == GST_STATE_PLAYING) {
      GST_DEBUG ("stop scrub seek, PLAYING");
      gst_element_set_state (pipeline, GST_STATE_PLAYING);
    }
  }

  return FALSE;
}

static void
play_cb (GtkButton * button, gpointer data)
{
  GstStateChangeReturn ret;

  if (state != GST_STATE_PLAYING) {
    g_print ("PLAY pipeline\n");
    ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
      goto failed;
    //do_seek(hscale);

    state = GST_STATE_PLAYING;
  }
  return;

failed:
  {
    g_print ("PLAY failed\n");
  }
}

static void
pause_cb (GtkButton * button, gpointer data)
{
  if (state != GST_STATE_PAUSED) {
    GstStateChangeReturn ret;

    g_print ("PAUSE pipeline\n");
    ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE)
      goto failed;

    state = GST_STATE_PAUSED;
  }
  return;

failed:
  {
    g_print ("PAUSE failed\n");
  }
}

static void
stop_cb (GtkButton * button, gpointer data)
{
  if (state != GST_STATE_READY) {
    GstStateChangeReturn ret;

    g_print ("READY pipeline\n");
    ret = gst_element_set_state (pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE)
      goto failed;

    state = GST_STATE_READY;

    set_update_scale (FALSE);
    set_scale (0.0);

    if (pipeline_type == 16)
      clear_streams (pipeline);

    /* if one uses parse_launch, play, stop and play again it fails as all the
     * pads after the demuxer can't be reconnected
     */
    if (!strcmp (pipelines[pipeline_type].name, "parse-launch")) {
      gst_element_set_state (pipeline, GST_STATE_NULL);
      gst_object_unref (pipeline);

      pipeline = pipelines[pipeline_type].func (pipeline_spec);
      g_assert (pipeline);
      gst_element_set_state (pipeline, GST_STATE_READY);
      connect_bus_signals (pipeline);
    }
  }
  return;

failed:
  {
    g_print ("STOP failed\n");
  }
}

static void
accurate_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  accurate_seek = gtk_toggle_button_get_active (button);
}

static void
key_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  keyframe_seek = gtk_toggle_button_get_active (button);
}

static void
loop_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  loop_seek = gtk_toggle_button_get_active (button);
  if (state == GST_STATE_PLAYING) {
    do_seek (hscale);
  }
}

static void
flush_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  flush_seek = gtk_toggle_button_get_active (button);
}

static void
scrub_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  scrub = gtk_toggle_button_get_active (button);
}

static void
play_scrub_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  play_scrub = gtk_toggle_button_get_active (button);
}

static void
rate_spinbutton_changed_cb (GtkSpinButton * button, GstPipeline * pipeline)
{
  gboolean res = FALSE;
  GstEvent *s_event;
  GstSeekFlags flags;

  rate = gtk_spin_button_get_value (button);

  flags = 0;
  if (flush_seek)
    flags |= GST_SEEK_FLAG_FLUSH;
  if (loop_seek)
    flags |= GST_SEEK_FLAG_SEGMENT;
  if (accurate_seek)
    flags |= GST_SEEK_FLAG_ACCURATE;
  if (keyframe_seek)
    flags |= GST_SEEK_FLAG_KEY_UNIT;

  if (rate >= 0) {
    s_event = gst_event_new_seek (rate,
        GST_FORMAT_TIME, flags, GST_SEEK_TYPE_SET, position,
        GST_SEEK_TYPE_SET, -1);
  } else {
    s_event = gst_event_new_seek (rate,
        GST_FORMAT_TIME, flags, GST_SEEK_TYPE_SET, G_GINT64_CONSTANT (0),
        GST_SEEK_TYPE_SET, position);
  }

  GST_DEBUG ("rate changed to %lf", rate);

  res = send_event (s_event);

  if (res) {
    if (flush_seek) {
      gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, SEEK_TIMEOUT);
    }
  } else
    g_print ("seek failed\n");
}

static void
update_flag (GstPipeline * pipeline, gint num, gboolean state)
{
  gint flags;

  g_object_get (pipeline, "flags", &flags, NULL);
  if (state)
    flags |= (1 << num);
  else
    flags &= ~(1 << num);
  g_object_set (pipeline, "flags", flags, NULL);
}

static void
vis_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  gboolean state;

  state = gtk_toggle_button_get_active (button);
  update_flag (pipeline, 3, state);
  gtk_widget_set_sensitive (vis_combo, state);
}

static void
audio_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  update_flag (pipeline, 1, gtk_toggle_button_get_active (button));
}

static void
video_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  update_flag (pipeline, 0, gtk_toggle_button_get_active (button));
}

static void
text_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  update_flag (pipeline, 2, gtk_toggle_button_get_active (button));
}

static void
mute_toggle_cb (GtkToggleButton * button, GstPipeline * pipeline)
{
  gboolean mute;

  mute = gtk_toggle_button_get_active (button);
  g_object_set (pipeline, "mute", mute, NULL);
}

static void
clear_streams (GstElement * pipeline)
{
  gint i;

  /* remove previous info */
  for (i = 0; i < n_video; i++)
    gtk_combo_box_remove_text (GTK_COMBO_BOX (video_combo), 0);
  for (i = 0; i < n_audio; i++)
    gtk_combo_box_remove_text (GTK_COMBO_BOX (audio_combo), 0);
  for (i = 0; i < n_text; i++)
    gtk_combo_box_remove_text (GTK_COMBO_BOX (text_combo), 0);

  n_audio = n_video = n_text = 0;
  gtk_widget_set_sensitive (video_combo, FALSE);
  gtk_widget_set_sensitive (audio_combo, FALSE);
  gtk_widget_set_sensitive (text_combo, FALSE);

  need_streams = TRUE;
}

static void
update_streams (GstPipeline * pipeline)
{
  gint i;

  if (pipeline_type == 16 && need_streams) {
    GstTagList *tags;
    gchar *name;
    gint active_idx;

    /* remove previous info */
    clear_streams (GST_ELEMENT_CAST (pipeline));

    /* here we get and update the different streams detected by playbin2 */
    g_object_get (pipeline, "n-video", &n_video, NULL);
    g_object_get (pipeline, "n-audio", &n_audio, NULL);
    g_object_get (pipeline, "n-text", &n_text, NULL);

    g_print ("video %d, audio %d, text %d\n", n_video, n_audio, n_text);

    active_idx = 0;
    for (i = 0; i < n_video; i++) {
      g_signal_emit_by_name (pipeline, "get-video-tags", i, &tags);
      /* find good name for the label */
      name = g_strdup_printf ("video %d", i + 1);
      gtk_combo_box_append_text (GTK_COMBO_BOX (video_combo), name);
      g_free (name);
    }
    gtk_widget_set_sensitive (video_combo, n_video > 0);
    gtk_combo_box_set_active (GTK_COMBO_BOX (video_combo), active_idx);

    active_idx = 0;
    for (i = 0; i < n_audio; i++) {
      g_signal_emit_by_name (pipeline, "get-audio-tags", i, &tags);
      /* find good name for the label */
      name = g_strdup_printf ("audio %d", i + 1);
      gtk_combo_box_append_text (GTK_COMBO_BOX (audio_combo), name);
      g_free (name);
    }
    gtk_widget_set_sensitive (audio_combo, n_audio > 0);
    gtk_combo_box_set_active (GTK_COMBO_BOX (audio_combo), active_idx);

    active_idx = 0;
    for (i = 0; i < n_text; i++) {
      g_signal_emit_by_name (pipeline, "get-text-tags", i, &tags);
      /* find good name for the label */
      name = g_strdup_printf ("text %d", i + 1);
      gtk_combo_box_append_text (GTK_COMBO_BOX (text_combo), name);
      g_free (name);
    }
    gtk_widget_set_sensitive (text_combo, n_text > 0);
    gtk_combo_box_set_active (GTK_COMBO_BOX (text_combo), active_idx);

    need_streams = FALSE;
  }
}

static void
video_combo_cb (GtkComboBox * combo, GstPipeline * pipeline)
{
  g_object_set (pipeline, "current-video", gtk_combo_box_get_active (combo),
      NULL);
}

static void
audio_combo_cb (GtkComboBox * combo, GstPipeline * pipeline)
{
  g_object_set (pipeline, "current-audio", gtk_combo_box_get_active (combo),
      NULL);
}

static void
text_combo_cb (GtkComboBox * combo, GstPipeline * pipeline)
{
  g_object_set (pipeline, "current-text", gtk_combo_box_get_active (combo),
      NULL);
}

static gboolean
filter_features (GstPluginFeature * feature, gpointer data)
{
  GstElementFactory *f;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;
  f = GST_ELEMENT_FACTORY (feature);
  if (!g_strrstr (gst_element_factory_get_klass (f), "Visualization"))
    return FALSE;

  return TRUE;
}

static void
init_visualization_features (void)
{
  GList *list, *walk;

  vis_entries = g_array_new (FALSE, FALSE, sizeof (VisEntry));

  list = gst_registry_feature_filter (gst_registry_get_default (),
      filter_features, FALSE, NULL);

  for (walk = list; walk; walk = g_list_next (walk)) {
    VisEntry entry;
    const gchar *name;

    entry.factory = GST_ELEMENT_FACTORY (walk->data);
    name = gst_element_factory_get_longname (entry.factory);

    g_array_append_val (vis_entries, entry);
    gtk_combo_box_append_text (GTK_COMBO_BOX (vis_combo), name);
  }
  gtk_combo_box_set_active (GTK_COMBO_BOX (vis_combo), 0);
}

static void
vis_combo_cb (GtkComboBox * combo, GstPipeline * pipeline)
{
  guint index;
  VisEntry *entry;
  GstElement *element;

  /* get the selected index and get the factory for this index */
  index = gtk_combo_box_get_active (GTK_COMBO_BOX (vis_combo));
  entry = &g_array_index (vis_entries, VisEntry, index);
  /* create an instance of the element from the factory */
  element = gst_element_factory_create (entry->factory, NULL);
  if (!element)
    return;

  /* set vis plugin for playbin2 */
  g_object_set (pipeline, "vis-plugin", element, NULL);
}

static void
volume_spinbutton_changed_cb (GtkSpinButton * button, GstPipeline * pipeline)
{
  gdouble volume;

  volume = gtk_spin_button_get_value (button);

  g_object_set (pipeline, "volume", volume, NULL);
}

static void
shot_cb (GtkButton * button, gpointer data)
{
  GstBuffer *buffer;
  GstCaps *caps;

  /* convert to our desired format (RGB24) */
  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "bpp", G_TYPE_INT, 24, "depth", G_TYPE_INT, 24,
      /* Note: we don't ask for a specific width/height here, so that
       * videoscale can adjust dimensions from a non-1/1 pixel aspect
       * ratio to a 1/1 pixel-aspect-ratio */
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "red_mask", G_TYPE_INT, 0xff0000,
      "green_mask", G_TYPE_INT, 0x00ff00,
      "blue_mask", G_TYPE_INT, 0x0000ff, NULL);

  /* convert the latest frame to the requested format */
  g_signal_emit_by_name (pipeline, "convert-frame", caps, &buffer);
  gst_caps_unref (caps);

  if (buffer) {
    GstCaps *caps;
    GstStructure *s;
    gboolean res;
    gint width, height;
    GdkPixbuf *pixbuf;
    GError *error = NULL;

    /* get the snapshot buffer format now. We set the caps on the appsink so
     * that it can only be an rgb buffer. The only thing we have not specified
     * on the caps is the height, which is dependant on the pixel-aspect-ratio
     * of the source material */
    caps = GST_BUFFER_CAPS (buffer);
    if (!caps) {
      g_warning ("could not get snapshot format\n");
      goto done;
    }
    s = gst_caps_get_structure (caps, 0);

    /* we need to get the final caps on the buffer to get the size */
    res = gst_structure_get_int (s, "width", &width);
    res |= gst_structure_get_int (s, "height", &height);
    if (!res) {
      g_warning ("could not get snapshot dimension\n");
      goto done;
    }

    /* create pixmap from buffer and save, gstreamer video buffers have a stride
     * that is rounded up to the nearest multiple of 4 */
    pixbuf = gdk_pixbuf_new_from_data (GST_BUFFER_DATA (buffer),
        GDK_COLORSPACE_RGB, FALSE, 8, width, height,
        GST_ROUND_UP_4 (width * 3), NULL, NULL);

    /* save the pixbuf */
    gdk_pixbuf_save (pixbuf, "snapshot.png", "png", &error, NULL);

  done:
    gst_buffer_unref (buffer);
  }
}

static void
message_received (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  const GstStructure *s;

  s = gst_message_get_structure (message);
  g_print ("message from \"%s\" (%s): ",
      GST_STR_NULL (GST_ELEMENT_NAME (GST_MESSAGE_SRC (message))),
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
  if (s) {
    gchar *sstr;

    sstr = gst_structure_to_string (s);
    g_print ("%s\n", sstr);
    g_free (sstr);
  } else {
    g_print ("no message details\n");
  }
}

static void
msg_async_done (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  GST_DEBUG ("async done");
  /* when we get ASYNC_DONE we can query position, duration and other
   * properties */
  update_scale (pipeline);

  /* update the available streams */
  update_streams (pipeline);
}

static void
msg_state_changed (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  const GstStructure *s;

  s = gst_message_get_structure (message);

  /* We only care about state changed on the pipeline */
  if (s && GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
    GstState old, new, pending;

    gst_message_parse_state_changed (message, &old, &new, &pending);

    /* When state of the pipeline changes to paused or playing we start updating scale */
    if (new == GST_STATE_PLAYING) {
      set_update_scale (TRUE);
    } else {
      set_update_scale (FALSE);
    }
  }
}

static void
msg_segment_done (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  GstEvent *s_event;
  GstSeekFlags flags;
  gboolean res;
  GstFormat format;

  GST_DEBUG ("position is %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
  gst_message_parse_segment_done (message, &format, &position);
  GST_DEBUG ("end of segment at %" GST_TIME_FORMAT, GST_TIME_ARGS (position));

  flags = 0;
  /* in the segment-done callback we never flush as this would not make sense
   * for seamless playback. */
  if (loop_seek)
    flags = GST_SEEK_FLAG_SEGMENT;

  s_event = gst_event_new_seek (rate,
      GST_FORMAT_TIME, flags, GST_SEEK_TYPE_SET, G_GINT64_CONSTANT (0),
      GST_SEEK_TYPE_SET, duration);

  GST_DEBUG ("restart loop with rate %lf to 0 / %" GST_TIME_FORMAT,
      rate, GST_TIME_ARGS (duration));

  res = send_event (s_event);
  if (!res)
    g_print ("segment seek failed\n");
}

static void
connect_bus_signals (GstElement * pipeline)
{
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  g_signal_connect (bus, "message::state-changed",
      (GCallback) msg_state_changed, pipeline);
  g_signal_connect (bus, "message::segment-done", (GCallback) msg_segment_done,
      pipeline);
  g_signal_connect (bus, "message::async-done", (GCallback) msg_async_done,
      pipeline);

  g_signal_connect (bus, "message::new-clock", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::error", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::warning", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::eos", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::tag", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::element", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::segment-done", (GCallback) message_received,
      pipeline);

  gst_object_unref (bus);
}

static void
print_usage (int argc, char **argv)
{
  gint i;

  g_print ("usage: %s <type> <filename>\n", argv[0]);
  g_print ("   possible types:\n");

  for (i = 0; i < NUM_TYPES; i++) {
    g_print ("     %d = %s\n", i, pipelines[i].name);
  }
}

int
main (int argc, char **argv)
{
  GtkWidget *window, *hbox, *vbox, *panel, *boxes, *flagtable, *boxes2;
  GtkWidget *play_button, *pause_button, *stop_button, *shot_button;
  GtkWidget *accurate_checkbox, *key_checkbox, *loop_checkbox, *flush_checkbox;
  GtkWidget *scrub_checkbox, *play_scrub_checkbox, *rate_spinbutton;
  GtkWidget *rate_label;
  GtkTooltips *tips;
  GOptionEntry options[] = {
    {"stats", 's', 0, G_OPTION_ARG_NONE, &stats,
        "Show pad stats", NULL},
    {"elem", 'e', 0, G_OPTION_ARG_NONE, &elem_seek,
        "Seek on elements instead of pads", NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        "Verbose properties", NULL},
    {NULL}
  };
  GOptionContext *ctx;
  GError *err = NULL;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("- test seeking in gsteamer");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }

  GST_DEBUG_CATEGORY_INIT (seek_debug, "seek", 0, "seek example");

  gtk_init (&argc, &argv);

  if (argc != 3) {
    print_usage (argc, argv);
    exit (-1);
  }

  pipeline_type = atoi (argv[1]);

  if (pipeline_type < 0 || pipeline_type >= NUM_TYPES) {
    print_usage (argc, argv);
    exit (-1);
  }

  pipeline_spec = argv[2];

  pipeline = pipelines[pipeline_type].func (pipeline_spec);
  g_assert (pipeline);

  /* initialize gui elements ... */
  tips = gtk_tooltips_new ();
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  hbox = gtk_hbox_new (FALSE, 0);
  vbox = gtk_vbox_new (FALSE, 0);
  flagtable = gtk_table_new (4, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 3);

  /* media controls */
  play_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PLAY);
  pause_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
  stop_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);

  /* seek flags */
  accurate_checkbox = gtk_check_button_new_with_label ("Accurate Seek");
  key_checkbox = gtk_check_button_new_with_label ("Key-unit Seek");
  loop_checkbox = gtk_check_button_new_with_label ("Loop");
  flush_checkbox = gtk_check_button_new_with_label ("Flush");
  scrub_checkbox = gtk_check_button_new_with_label ("Scrub");
  play_scrub_checkbox = gtk_check_button_new_with_label ("Play Scrub");
  rate_spinbutton = gtk_spin_button_new_with_range (-100, 100, 0.1);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (rate_spinbutton), 3);
  rate_label = gtk_label_new ("Rate");

  gtk_tooltips_set_tip (tips, accurate_checkbox,
      "accurate position is requested, this might be considerably slower for some formats",
      NULL);
  gtk_tooltips_set_tip (tips, key_checkbox,
      "seek to the nearest keyframe. This might be faster but less accurate",
      NULL);
  gtk_tooltips_set_tip (tips, loop_checkbox, "loop playback", NULL);
  gtk_tooltips_set_tip (tips, flush_checkbox, "flush pipeline after seeking",
      NULL);
  gtk_tooltips_set_tip (tips, rate_spinbutton, "define the playback rate, "
      "negative value trigger reverse playback", NULL);
  gtk_tooltips_set_tip (tips, scrub_checkbox, "show images while seeking",
      NULL);
  gtk_tooltips_set_tip (tips, play_scrub_checkbox, "play video while seeking",
      NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (flush_checkbox), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scrub_checkbox), TRUE);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (rate_spinbutton), rate);

  /* seek bar */
  adjustment =
      GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.00, 100.0, 0.1, 1.0, 1.0));
  hscale = gtk_hscale_new (adjustment);
  gtk_scale_set_digits (GTK_SCALE (hscale), 2);
#if GTK_CHECK_VERSION(2,12,0)
  gtk_range_set_show_fill_level (GTK_RANGE (hscale), TRUE);
  gtk_range_set_fill_level (GTK_RANGE (hscale), 100.0);
#endif
  gtk_range_set_update_policy (GTK_RANGE (hscale), GTK_UPDATE_CONTINUOUS);

  gtk_signal_connect (GTK_OBJECT (hscale),
      "button_press_event", G_CALLBACK (start_seek), pipeline);
  gtk_signal_connect (GTK_OBJECT (hscale),
      "button_release_event", G_CALLBACK (stop_seek), pipeline);
  gtk_signal_connect (GTK_OBJECT (hscale),
      "format_value", G_CALLBACK (format_value), pipeline);

  if (pipeline_type == 16) {
    /* the playbin2 panel controls for the video/audio/subtitle tracks */
    panel = gtk_hbox_new (FALSE, 0);
    video_combo = gtk_combo_box_new_text ();
    audio_combo = gtk_combo_box_new_text ();
    text_combo = gtk_combo_box_new_text ();
    gtk_widget_set_sensitive (video_combo, FALSE);
    gtk_widget_set_sensitive (audio_combo, FALSE);
    gtk_widget_set_sensitive (text_combo, FALSE);
    gtk_box_pack_start (GTK_BOX (panel), video_combo, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (panel), audio_combo, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (panel), text_combo, TRUE, TRUE, 2);
    g_signal_connect (G_OBJECT (video_combo), "changed",
        G_CALLBACK (video_combo_cb), pipeline);
    g_signal_connect (G_OBJECT (audio_combo), "changed",
        G_CALLBACK (audio_combo_cb), pipeline);
    g_signal_connect (G_OBJECT (text_combo), "changed",
        G_CALLBACK (text_combo_cb), pipeline);
    /* playbin2 panel for flag checkboxes and volume/mute */
    boxes = gtk_hbox_new (FALSE, 0);
    vis_checkbox = gtk_check_button_new_with_label ("Vis");
    video_checkbox = gtk_check_button_new_with_label ("Video");
    audio_checkbox = gtk_check_button_new_with_label ("Audio");
    text_checkbox = gtk_check_button_new_with_label ("Text");
    mute_checkbox = gtk_check_button_new_with_label ("Mute");
    volume_spinbutton = gtk_spin_button_new_with_range (0, 10.0, 0.1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (volume_spinbutton), 1.0);
    gtk_box_pack_start (GTK_BOX (boxes), vis_checkbox, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (boxes), audio_checkbox, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (boxes), video_checkbox, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (boxes), text_checkbox, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (boxes), mute_checkbox, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (boxes), volume_spinbutton, TRUE, TRUE, 2);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (vis_checkbox), FALSE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (audio_checkbox), TRUE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (video_checkbox), TRUE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (text_checkbox), TRUE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mute_checkbox), FALSE);
    g_signal_connect (G_OBJECT (vis_checkbox), "toggled",
        G_CALLBACK (vis_toggle_cb), pipeline);
    g_signal_connect (G_OBJECT (audio_checkbox), "toggled",
        G_CALLBACK (audio_toggle_cb), pipeline);
    g_signal_connect (G_OBJECT (video_checkbox), "toggled",
        G_CALLBACK (video_toggle_cb), pipeline);
    g_signal_connect (G_OBJECT (text_checkbox), "toggled",
        G_CALLBACK (text_toggle_cb), pipeline);
    g_signal_connect (G_OBJECT (mute_checkbox), "toggled",
        G_CALLBACK (mute_toggle_cb), pipeline);
    g_signal_connect (G_OBJECT (volume_spinbutton), "value_changed",
        G_CALLBACK (volume_spinbutton_changed_cb), pipeline);
    /* playbin2 panel for snapshot */
    boxes2 = gtk_hbox_new (FALSE, 0);
    shot_button = gtk_button_new_from_stock (GTK_STOCK_SAVE);
    gtk_tooltips_set_tip (tips, shot_button,
        "save a screenshot .png in the current directory", NULL);
    g_signal_connect (G_OBJECT (shot_button), "clicked", G_CALLBACK (shot_cb),
        pipeline);
    vis_combo = gtk_combo_box_new_text ();
    g_signal_connect (G_OBJECT (vis_combo), "changed",
        G_CALLBACK (vis_combo_cb), pipeline);
    gtk_widget_set_sensitive (vis_combo, FALSE);
    gtk_box_pack_start (GTK_BOX (boxes2), shot_button, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (boxes2), vis_combo, TRUE, TRUE, 2);

    /* fill the vis combo box and the array of factories */
    init_visualization_features ();
  } else {
    panel = boxes = boxes2 = NULL;
  }

  /* do the packing stuff ... */
  gtk_window_set_default_size (GTK_WINDOW (window), 250, 96);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);
  gtk_box_pack_start (GTK_BOX (hbox), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), flagtable, FALSE, FALSE, 2);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), accurate_checkbox, 0, 1, 0,
      1);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), flush_checkbox, 1, 2, 0, 1);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), loop_checkbox, 2, 3, 0, 1);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), key_checkbox, 0, 1, 1, 2);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), scrub_checkbox, 1, 2, 1, 2);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), play_scrub_checkbox, 2, 3,
      1, 2);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), rate_label, 3, 4, 0, 1);
  gtk_table_attach_defaults (GTK_TABLE (flagtable), rate_spinbutton, 3, 4, 1,
      2);
  if (panel && boxes && boxes2) {
    gtk_box_pack_start (GTK_BOX (vbox), panel, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (vbox), boxes, TRUE, TRUE, 2);
    gtk_box_pack_start (GTK_BOX (vbox), boxes2, TRUE, TRUE, 2);
  }
  gtk_box_pack_start (GTK_BOX (vbox), hscale, TRUE, TRUE, 2);

  /* connect things ... */
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb),
      pipeline);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb),
      pipeline);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb),
      pipeline);
  g_signal_connect (G_OBJECT (accurate_checkbox), "toggled",
      G_CALLBACK (accurate_toggle_cb), pipeline);
  g_signal_connect (G_OBJECT (key_checkbox), "toggled",
      G_CALLBACK (key_toggle_cb), pipeline);
  g_signal_connect (G_OBJECT (loop_checkbox), "toggled",
      G_CALLBACK (loop_toggle_cb), pipeline);
  g_signal_connect (G_OBJECT (flush_checkbox), "toggled",
      G_CALLBACK (flush_toggle_cb), pipeline);
  g_signal_connect (G_OBJECT (scrub_checkbox), "toggled",
      G_CALLBACK (scrub_toggle_cb), pipeline);
  g_signal_connect (G_OBJECT (play_scrub_checkbox), "toggled",
      G_CALLBACK (play_scrub_toggle_cb), pipeline);
  g_signal_connect (G_OBJECT (rate_spinbutton), "value_changed",
      G_CALLBACK (rate_spinbutton_changed_cb), pipeline);

  g_signal_connect (G_OBJECT (window), "delete-event", gtk_main_quit, NULL);

  /* show the gui. */
  gtk_widget_show_all (window);

  if (verbose) {
    g_signal_connect (pipeline, "deep_notify",
        G_CALLBACK (gst_object_default_deep_notify), NULL);
  }

  connect_bus_signals (pipeline);
  gtk_main ();

  g_print ("NULL pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("free pipeline\n");
  gst_object_unref (pipeline);

  return 0;
}
