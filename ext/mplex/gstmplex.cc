/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2008 Mark Nauwelaerts <mnauw@users.sourceforge.net>
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

/**
 * SECTION:element-mplex
 * @see_also: mpeg2enc
 *
 * This element is an audio/video multiplexer for MPEG-1/2 video streams
 * and (un)compressed audio streams such as AC3, MPEG layer I/II/III.
 * It is based on the <ulink url="http://mjpeg.sourceforge.net/">mjpegtools</ulink> library.
 * Documentation on creating MPEG videos in general can be found in the
 * <ulink url="https://sourceforge.net/docman/display_doc.php?docid=3456&group_id=5776">MJPEG Howto</ulink>
 * and the man-page of the mplex tool documents the properties of this element,
 * which are shared with the mplex tool.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v videotestsrc num-buffers=1000 ! mpeg2enc ! mplex ! filesink location=videotestsrc.mpg
 * ]| This example pipeline will encode a test video source to an
 * MPEG1 elementary stream and multiplexes this to an MPEG system stream.
 * <para>
 * If several streams are being multiplexed, there should (as usual) be
 * a queue in each stream, and due to mplex' buffering the capacities of these
 * may have to be set to a few times the default settings to prevent the
 * pipeline stalling.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmplex.hh"
#include "gstmplexoutputstream.hh"
#include "gstmplexibitstream.hh"
#include "gstmplexjob.hh"

GST_DEBUG_CATEGORY (mplex_debug);

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, systemstream = (boolean) true ")
    );

static GstStaticPadTemplate video_sink_templ =
GST_STATIC_PAD_TEMPLATE ("video_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2 }, "
        "systemstream = (boolean) false, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], framerate = (fraction) [ 0, MAX ]")
    );

#define COMMON_AUDIO_CAPS \
  "channels = (int) [ 1, 8 ], " \
  "rate = (int) [ 8000, 96000 ]"

static GstStaticPadTemplate audio_sink_templ =
    GST_STATIC_PAD_TEMPLATE ("audio_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        COMMON_AUDIO_CAPS "; "
        "audio/x-ac3, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-dts; "
        "audio/x-raw-int, "
        "endianness = (int) BIG_ENDIAN, "
        "signed = (boolean) TRUE, "
        "width = (int) { 16, 20, 24 }, "
        "depth = (int) { 16, 20, 24 }, "
        "rate = (int) { 48000, 96000 }, " "channels = (int) [ 1, 6 ]")
    );

/* FIXME: subtitles */

static void gst_mplex_finalize (GObject * object);
static void gst_mplex_reset (GstMplex * mplex);
static void gst_mplex_loop (GstMplex * mplex);
static GstPad *gst_mplex_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_mplex_release_pad (GstElement * element, GstPad * pad);
static gboolean gst_mplex_src_activate_push (GstPad * pad, gboolean active);
static GstStateChangeReturn gst_mplex_change_state (GstElement * element,
    GstStateChange transition);

static void gst_mplex_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_mplex_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstMplex, gst_mplex, GstElement, GST_TYPE_ELEMENT);

static void
gst_mplex_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "mplex video multiplexer", "Codec/Muxer",
      "High-quality MPEG/DVD/SVCD/VCD video/audio multiplexer",
      "Andrew Stevens <andrew.stevens@nexgo.de>\n"
      "Ronald Bultje <rbultje@ronald.bitfreak.net>\n"
      "Mark Nauwelaerts <mnauw@users.sourceforge.net>");

  gst_element_class_add_static_pad_template (element_class, &src_templ);
  gst_element_class_add_static_pad_template (element_class,
      &video_sink_templ);
  gst_element_class_add_static_pad_template (element_class,
      &audio_sink_templ);
}

static void
gst_mplex_class_init (GstMplexClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (mplex_debug, "mplex", 0, "MPEG video/audio muxer");

  object_class->set_property = gst_mplex_set_property;
  object_class->get_property = gst_mplex_get_property;

  /* register properties */
  GstMplexJob::initProperties (object_class);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_mplex_finalize);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mplex_change_state);
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_mplex_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_mplex_release_pad);
}

static void
gst_mplex_finalize (GObject * object)
{
  GstMplex *mplex = GST_MPLEX (object);
  GSList *walk;

  /* release all pads */
  walk = mplex->pads;
  while (walk) {
    GstMplexPad *mpad = (GstMplexPad *) walk->data;

    gst_object_unref (mpad->pad);
    mpad->pad = NULL;
    walk = walk->next;
  }

  /* clean up what's left of them */
  gst_mplex_reset (mplex);

  /* ... and of the rest */
  delete mplex->job;

  g_mutex_free (mplex->tlock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mplex_init (GstMplex * mplex, GstMplexClass * g_class)
{
  GstElement *element = GST_ELEMENT (mplex);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  mplex->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "src"), "src");
  gst_element_add_pad (element, mplex->srcpad);
  gst_pad_use_fixed_caps (mplex->srcpad);
  gst_pad_set_activatepush_function (mplex->srcpad,
      GST_DEBUG_FUNCPTR (gst_mplex_src_activate_push));

  mplex->job = new GstMplexJob ();
  mplex->num_apads = 0;
  mplex->num_vpads = 0;

  mplex->tlock = g_mutex_new ();

  gst_mplex_reset (mplex);
}

static void
gst_mplex_reset (GstMplex * mplex)
{
  GSList *walk;
  GSList *nlist = NULL;

  mplex->eos = FALSE;
  mplex->srcresult = GST_FLOW_CUSTOM_SUCCESS;

  /* reset existing streams */
  walk = mplex->pads;
  while (walk != NULL) {
    GstMplexPad *mpad;

    mpad = (GstMplexPad *) walk->data;

    mpad->needed = 0;
    mpad->eos = FALSE;
    gst_adapter_clear (mpad->adapter);
    if (mpad->bs) {
      delete mpad->bs;

      mpad->bs = NULL;
    }

    if (!mpad->pad) {
      g_cond_free (mpad->cond);
      g_object_unref (mpad->adapter);
      g_free (mpad);
    } else
      nlist = g_slist_append (nlist, mpad);

    walk = walk->next;
  }

  g_slist_free (mplex->pads);
  mplex->pads = nlist;

  /* clear mplex stuff */
  /* clean up stream settings */
  while (!mplex->job->streams.empty ()) {
    delete mplex->job->streams.back ();

    mplex->job->streams.pop_back ();
  }
  while (!mplex->job->video_param.empty ()) {
    delete mplex->job->video_param.back ();

    mplex->job->video_param.pop_back ();
  }
  while (!mplex->job->lpcm_param.empty ()) {
    delete mplex->job->lpcm_param.back ();

    mplex->job->lpcm_param.pop_back ();
  }
  mplex->job->audio_tracks = 0;
  mplex->job->video_tracks = 0;
  mplex->job->lpcm_tracks = 0;
}

static gboolean
gst_mplex_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMplex *mplex;
  const gchar *mime;
  GstStructure *structure;
  StreamKind type;
  JobStream *jobstream;
  GstMplexIBitStream *inputstream;
  GstMplexPad *mpad;
  GstCaps *othercaps;
  gboolean ret = TRUE;

  mplex = GST_MPLEX (GST_PAD_PARENT (pad));

  /* does not go well to negotiate when started */
  if (mplex->srcresult != GST_FLOW_CUSTOM_SUCCESS)
    goto refuse_renegotiation;

  /* since muxer does not really check much ... */
  othercaps = gst_caps_intersect (caps, gst_pad_get_pad_template_caps (pad));
  if (othercaps)
    gst_caps_unref (othercaps);
  else
    goto refuse_caps;

  /* set the fixed template caps on the srcpad, should accept without objection */
  othercaps = gst_caps_copy (gst_pad_get_pad_template_caps (mplex->srcpad));
  ret = gst_pad_set_caps (mplex->srcpad, othercaps);
  gst_caps_unref (othercaps);
  if (!ret)
    goto refuse_caps;

  structure = gst_caps_get_structure (caps, 0);
  mime = gst_structure_get_name (structure);

  if (!strcmp (mime, "video/mpeg")) {   /* video */
    VideoParams *params;

    type = MPEG_VIDEO;
    if (mplex->job->bufsize)
      params = VideoParams::Checked (mplex->job->bufsize);
    else
      params = VideoParams::Default (mplex->job->mux_format);
    /* set standard values if forced by the selected profile */
    if (params->Force (mplex->job->mux_format))
      GST_WARNING_OBJECT (mplex,
          "overriding non-standard option due to selected profile");

    mplex->job->video_param.push_back (params);
    mplex->job->video_tracks++;
  } else {                      /* audio */
    if (!strcmp (mime, "audio/mpeg")) {
      type = MPEG_AUDIO;
    } else if (!strcmp (mime, "audio/x-ac3")) {
      type = AC3_AUDIO;
    } else if (!strcmp (mime, "audio/x-dts")) {
      type = DTS_AUDIO;
    } else if (!strcmp (mime, "audio/x-raw-int")) {
      LpcmParams *params;
      gint bits, chans, rate;
      gboolean result = TRUE;

      type = LPCM_AUDIO;

      /* set LPCM params */
      result &= gst_structure_get_int (structure, "depth", &bits);
      result &= gst_structure_get_int (structure, "rate", &rate);
      result &= gst_structure_get_int (structure, "channels", &chans);
      if (!result)
        goto refuse_caps;

      params = LpcmParams::Checked (rate, chans, bits);

      mplex->job->lpcm_param.push_back (params);
      mplex->job->lpcm_tracks++;
    } else
      goto refuse_caps;

    mplex->job->audio_tracks++;
  }

  mpad = (GstMplexPad *) gst_pad_get_element_private (pad);
  g_return_val_if_fail (mpad, FALSE);
  inputstream = new GstMplexIBitStream (mpad);
  mpad->bs = inputstream;
  jobstream = new JobStream (inputstream, type);
  mplex->job->streams.push_back (jobstream);

  return TRUE;

refuse_caps:
  {
    GST_WARNING_OBJECT (mplex, "refused caps %" GST_PTR_FORMAT, caps);

    /* undo if we were a bit too fast/confident */
    if (GST_PAD_CAPS (mplex->srcpad))
      gst_pad_set_caps (mplex->srcpad, NULL);

    return FALSE;
  }
refuse_renegotiation:
  {
    GST_WARNING_OBJECT (mplex, "already started; "
        "refused (re)negotiation (to %" GST_PTR_FORMAT ")", caps);

    return FALSE;
  }
}

static void
gst_mplex_loop (GstMplex * mplex)
{
  GstMplexOutputStream *out = NULL;
  Multiplexor *mux = NULL;
  GSList *walk;

  /* do not try to resume muxing after it finished
   * this can be relevant mainly/only in case of forced state change */
  if (mplex->eos)
    goto eos;

  /* inform downstream about what's coming */
  gst_pad_push_event (mplex->srcpad, gst_event_new_new_segment (FALSE, 1.0,
          GST_FORMAT_BYTES, 0, -1, 0));

  /* hm (!) each inputstream really needs an initial read
   * so that all is internally in the proper state */
  walk = mplex->pads;
  while (walk != NULL) {
    GstMplexPad *mpad;

    mpad = (GstMplexPad *) walk->data;
    mpad->bs->ReadBuffer ();

    walk = walk->next;
  }

  /* create new multiplexer with inputs/output */
  out = new GstMplexOutputStream (mplex, mplex->srcpad);
#if GST_MJPEGTOOLS_API >= 10900
  mux = new Multiplexor (*mplex->job, *out, NULL);
#else
  mux = new Multiplexor (*mplex->job, *out);
#endif

  if (mux) {
    mux->Multiplex ();
    delete mux;
    delete out;

    /* if not well and truly eos, something strange happened  */
    if (!mplex->eos) {
      GST_ERROR_OBJECT (mplex, "muxing task ended without being eos");
      /* notify there is no point in collecting any more */
      GST_MPLEX_MUTEX_LOCK (mplex);
      mplex->srcresult = GST_FLOW_ERROR;
      GST_MPLEX_SIGNAL_ALL (mplex);
      GST_MPLEX_MUTEX_UNLOCK (mplex);
    } else
      goto eos;
  } else {
    GST_WARNING_OBJECT (mplex, "failed to create Multiplexor");
  }

  /* fall-through */
done:
  {
    /* no need to run wildly, stopped elsewhere, e.g. state change */
    GST_DEBUG_OBJECT (mplex, "pausing muxing task");
    gst_pad_pause_task (mplex->srcpad);

    return;
  }
eos:
  {
    GST_DEBUG_OBJECT (mplex, "encoding task reached eos");
    goto done;
  }
}

static gboolean
gst_mplex_sink_event (GstPad * sinkpad, GstEvent * event)
{
  GstMplex *mplex;
  GstMplexPad *mpad;
  gboolean result = TRUE;

  mplex = (GstMplex *) (GST_PAD_PARENT (sinkpad));
  mpad = (GstMplexPad *) gst_pad_get_element_private (sinkpad);
  g_return_val_if_fail (mpad, FALSE);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* forward event */
      gst_pad_event_default (sinkpad, event);

      /* now unblock the chain function */
      GST_MPLEX_MUTEX_LOCK (mplex);
      mplex->srcresult = GST_FLOW_WRONG_STATE;
      GST_MPLEX_SIGNAL (mplex, mpad);
      GST_MPLEX_MUTEX_UNLOCK (mplex);
      /* no way to pause/restart loop task */
      goto done;
    case GST_EVENT_FLUSH_STOP:
      /* forward event */
      gst_pad_event_default (sinkpad, event);

      /* clear state and resume */
      GST_MPLEX_MUTEX_LOCK (mplex);
      gst_adapter_clear (mpad->adapter);
      mplex->srcresult = GST_FLOW_OK;
      GST_MPLEX_MUTEX_UNLOCK (mplex);
      goto done;
    case GST_EVENT_NEWSEGMENT:
      /* eat segments; we make our own (byte)stream */
      gst_event_unref (event);
      goto done;
    case GST_EVENT_EOS:
      /* inform this pad that it can stop now */
      GST_MPLEX_MUTEX_LOCK (mplex);
      mpad->eos = TRUE;
      GST_MPLEX_SIGNAL (mplex, mpad);
      GST_MPLEX_MUTEX_UNLOCK (mplex);

      /* eat this event for now, task will send eos when finished */
      gst_event_unref (event);
      goto done;
    default:
      /* for a serialized event, wait until earlier data is gone,
       * though this is no guarantee as to when task is done with it.
       * Only wait if loop has been started already */
      if (GST_EVENT_IS_SERIALIZED (event)) {
        GST_MPLEX_MUTEX_LOCK (mplex);
        while (mplex->srcresult == GST_FLOW_OK && !mpad->needed)
          GST_MPLEX_WAIT (mplex, mpad);
        GST_MPLEX_MUTEX_UNLOCK (mplex);
      }
      break;
  }

  result = gst_pad_event_default (sinkpad, event);

done:
  return result;
}

/* starts task if conditions are right for it
 * must be called with mutex_lock held */
static void
gst_mplex_start_task (GstMplex * mplex)
{
  /* start task to create multiplexor and start muxing */
  if (G_UNLIKELY (mplex->srcresult == GST_FLOW_CUSTOM_SUCCESS)
      && mplex->job->video_tracks == mplex->num_vpads
      && mplex->job->audio_tracks == mplex->num_apads) {
    gst_pad_start_task (mplex->srcpad, (GstTaskFunction) gst_mplex_loop, mplex);
    mplex->srcresult = GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_mplex_chain (GstPad * sinkpad, GstBuffer * buffer)
{
  GstMplex *mplex;
  GstMplexPad *mpad;

  mplex = (GstMplex *) (GST_PAD_PARENT (sinkpad));
  mpad = (GstMplexPad *) gst_pad_get_element_private (sinkpad);
  g_return_val_if_fail (mpad, GST_FLOW_ERROR);

  /* check if pad were properly negotiated and set up */
  if (G_UNLIKELY (!mpad->bs)) {
    GST_ELEMENT_ERROR (mplex, CORE, NEGOTIATION, (NULL),
        ("input pad has not been set up prior to chain function"));
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_MPLEX_MUTEX_LOCK (mplex);

  gst_mplex_start_task (mplex);

  if (G_UNLIKELY (mpad->eos))
    goto eos;

  if (G_UNLIKELY (mplex->srcresult != GST_FLOW_OK))
    goto ignore;

  gst_adapter_push (mpad->adapter, buffer);
  buffer = NULL;
  while (gst_adapter_available (mpad->adapter) >= mpad->needed) {
    GST_MPLEX_SIGNAL (mplex, mpad);
    GST_MPLEX_WAIT (mplex, mpad);
    /* may have become flushing or in error */
    if (G_UNLIKELY (mplex->srcresult != GST_FLOW_OK))
      goto ignore;
    /* or been removed */
    if (G_UNLIKELY (mpad->eos))
      goto eos;
  }

  GST_MPLEX_MUTEX_UNLOCK (mplex);

  return GST_FLOW_OK;

/* special cases */
eos:
  {
    GST_DEBUG_OBJECT (mplex, "ignoring buffer at end-of-stream");
    GST_MPLEX_MUTEX_UNLOCK (mplex);

    gst_buffer_unref (buffer);
    return GST_FLOW_UNEXPECTED;
  }
ignore:
  {
    GstFlowReturn ret = mplex->srcresult;

    GST_DEBUG_OBJECT (mplex, "ignoring buffer because src task encountered %s",
        gst_flow_get_name (ret));
    GST_MPLEX_MUTEX_UNLOCK (mplex);

    if (buffer)
      gst_buffer_unref (buffer);
    return ret;
  }
}

static GstPad *
gst_mplex_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstMplex *mplex = GST_MPLEX (element);
  gchar *padname;
  GstPad *newpad;
  GstMplexPad *mpad;

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    GST_DEBUG_OBJECT (mplex, "request pad audio %d", mplex->num_apads);
    padname = g_strdup_printf ("audio_%d", mplex->num_apads++);
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    GST_DEBUG_OBJECT (mplex, "request pad video %d", mplex->num_vpads);
    padname = g_strdup_printf ("video_%d", mplex->num_vpads++);
  } else {
    GST_WARNING_OBJECT (mplex, "This is not our template!");
    return NULL;
  }

  newpad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  mpad = g_new0 (GstMplexPad, 1);
  mpad->adapter = gst_adapter_new ();
  mpad->cond = g_cond_new ();
  gst_object_ref (newpad);
  mpad->pad = newpad;

  gst_pad_set_setcaps_function (newpad, GST_DEBUG_FUNCPTR (gst_mplex_setcaps));
  gst_pad_set_chain_function (newpad, GST_DEBUG_FUNCPTR (gst_mplex_chain));
  gst_pad_set_event_function (newpad, GST_DEBUG_FUNCPTR (gst_mplex_sink_event));
  gst_pad_set_element_private (newpad, mpad);
  gst_element_add_pad (element, newpad);
  mplex->pads = g_slist_append (mplex->pads, mpad);

  return newpad;
}

static void
gst_mplex_release_pad (GstElement * element, GstPad * pad)
{
  GstMplex *mplex = GST_MPLEX (element);
  GstMplexPad *mpad;

  g_return_if_fail (pad);
  mpad = (GstMplexPad *) gst_pad_get_element_private (pad);
  g_return_if_fail (mpad);

  if (gst_element_remove_pad (element, pad)) {
    gchar *padname;

    GST_MPLEX_MUTEX_LOCK (mplex);
    mpad->eos = TRUE;
    gst_object_unref (mpad->pad);
    mpad->pad = NULL;
    /* wake up if waiting on this pad */
    GST_MPLEX_SIGNAL (mplex, mpad);

    padname = gst_object_get_name (GST_OBJECT (pad));
    if (strstr (padname, "audio")) {
      mplex->num_apads--;
    } else {
      mplex->num_vpads--;
    }
    g_free (padname);

    /* may now be up to us to get things going */
    gst_mplex_start_task (mplex);
    GST_MPLEX_MUTEX_UNLOCK (mplex);
  }
}

static void
gst_mplex_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GST_MPLEX (object)->job->getProperty (prop_id, value);
}

static void
gst_mplex_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GST_MPLEX (object)->job->setProperty (prop_id, value);
}

static gboolean
gst_mplex_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstMplex *mplex;

  mplex = GST_MPLEX (GST_PAD_PARENT (pad));

  if (active) {
    /* chain will start task once all streams have been setup */
  } else {
    /* end the muxing loop by forcing eos and unblock chains */
    GST_MPLEX_MUTEX_LOCK (mplex);
    mplex->eos = TRUE;
    mplex->srcresult = GST_FLOW_WRONG_STATE;
    GST_MPLEX_SIGNAL_ALL (mplex);
    GST_MPLEX_MUTEX_UNLOCK (mplex);

    /* muxing loop should have ended now and can be joined */
    result = gst_pad_stop_task (pad);
  }

  return result;
}

static GstStateChangeReturn
gst_mplex_change_state (GstElement * element, GstStateChange transition)
{
  GstMplex *mplex = GST_MPLEX (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mplex_reset (mplex);
      break;
    default:
      break;
  }

done:
  return ret;
}

#ifndef GST_DISABLE_GST_DEBUG

static mjpeg_log_handler_t old_handler = NULL;

/* note that this will affect all mjpegtools elements/threads */
static void
gst_mplex_log_callback (log_level_t level, const char *message)
{
  GstDebugLevel gst_level;

#if GST_MJPEGTOOLS_API >= 10900
  static const gint mjpeg_log_error = mjpeg_loglev_t ("error");
  static const gint mjpeg_log_warn = mjpeg_loglev_t ("warn");
  static const gint mjpeg_log_info = mjpeg_loglev_t ("info");
  static const gint mjpeg_log_debug = mjpeg_loglev_t ("debug");
#else
  static const gint mjpeg_log_error = LOG_ERROR;
  static const gint mjpeg_log_warn = LOG_WARN;
  static const gint mjpeg_log_info = LOG_INFO;
  static const gint mjpeg_log_debug = LOG_DEBUG;
#endif

  if (level == mjpeg_log_error) {
    gst_level = GST_LEVEL_ERROR;
  } else if (level == mjpeg_log_warn) {
    gst_level = GST_LEVEL_WARNING;
  } else if (level == mjpeg_log_info) {
    gst_level = GST_LEVEL_INFO;
  } else if (level == mjpeg_log_debug) {
    gst_level = GST_LEVEL_DEBUG;
  } else {
    gst_level = GST_LEVEL_INFO;
  }

  /* message could have a % in it, do not segfault in such case */
  gst_debug_log (mplex_debug, gst_level, "", "", 0, NULL, "%s", message);

  /* chain up to the old handler;
   * this could actually be a handler from another mjpegtools based
   * gstreamer element; in which case messages can come out double or from
   * the wrong element ... */
  old_handler (level, message);
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
#ifndef GST_DISABLE_GST_DEBUG
  old_handler = mjpeg_log_set_handler (gst_mplex_log_callback);
  g_assert (old_handler != NULL);
#endif
  /* in any case, we do not want default handler output */
  mjpeg_default_handler_verbosity (0);

  return gst_element_register (plugin, "mplex", GST_RANK_NONE, GST_TYPE_MPLEX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mplex",
    "High-quality MPEG/DVD/SVCD/VCD video/audio multiplexer",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
