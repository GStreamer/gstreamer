/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Mark Nauwelaerts <manauw@skynet.be>
 *
 * gstmpeg2enc.cc: gstreamer mpeg2enc wrapping
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
 * SECTION:element-mpeg2enc
 * @see_also: mpeg2dec
 *
 * This element encodes raw video into an MPEG-1/2 elementary stream using the
 * <ulink url="http://mjpeg.sourceforge.net/">mjpegtools</ulink> library.
 * Documentation on MPEG encoding in general can be found in the 
 * <ulink url="https://sourceforge.net/docman/display_doc.php?docid=3456&group_id=5776">MJPEG Howto</ulink>
 * and on the various available parameters in the documentation
 * of the mpeg2enc tool in particular, which shares options with this element.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-0.10 videotestsrc num-buffers=1000 ! mpeg2enc ! filesink location=videotestsrc.m1v
 * ]| This example pipeline will encode a test video source to a an MPEG1
 * elementary stream (with Generic MPEG1 profile).
 * <para>
 * Likely, the #GstMpeg2enc:format property
 * is most important, as it selects the type of MPEG stream that is produced.
 * In particular, default property values are dependent on the format,
 * and can even be forcibly restrained to certain pre-sets (and thereby ignored).
 * Note that the (S)VCD profiles also restrict the image size, so some scaling
 * may be needed to accomodate this.  The so-called generic profiles (as used
 * in the example above) allow most parameters to be adjusted.
 * </para>
 * |[
 * gst-launch-0.10 videotestsrc num-buffers=1000 ! videoscale ! mpeg2enc format=1 norm=p ! filesink location=videotestsrc.m1v
 * ]| This will produce an MPEG1 profile stream according to VCD2.0 specifications
 * for PAL #GstMpeg2enc:norm (as the image height is dependent on video norm).
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpeg2enc.hh"

GST_DEBUG_CATEGORY (mpeg2enc_debug);

#define COMMON_VIDEO_CAPS \
  "width = (int) [ 16, 4096 ], " \
  "height = (int) [ 16, 4096 ], " \
  "framerate = " \
  " (fraction) { 24000/1001, 24/1, 25/1, 30000/1001, 30/1, 50/1, 60000/1001 }"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) { I420 }, " COMMON_VIDEO_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "systemstream = (boolean) false, "
        "mpegversion = (int) { 1, 2 }, " COMMON_VIDEO_CAPS)
    );


static void gst_mpeg2enc_finalize (GObject * object);
static void gst_mpeg2enc_reset (GstMpeg2enc * enc);
static gboolean gst_mpeg2enc_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_mpeg2enc_getcaps (GstPad * pad);
static gboolean gst_mpeg2enc_sink_event (GstPad * pad, GstEvent * event);
static void gst_mpeg2enc_loop (GstMpeg2enc * enc);
static GstFlowReturn gst_mpeg2enc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_mpeg2enc_src_activate_push (GstPad * pad, gboolean active);
static GstStateChangeReturn gst_mpeg2enc_change_state (GstElement * element,
    GstStateChange transition);

static void gst_mpeg2enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_mpeg2enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);
}

GST_BOILERPLATE_FULL (GstMpeg2enc, gst_mpeg2enc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_mpeg2enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "mpeg2enc video encoder", "Codec/Encoder/Video",
      "High-quality MPEG-1/2 video encoder",
      "Andrew Stevens <andrew.stevens@nexgo.de>\n"
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
}

static void
gst_mpeg2enc_class_init (GstMpeg2encClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (mpeg2enc_debug, "mpeg2enc", 0, "MPEG1/2 encoder");

  object_class->set_property = gst_mpeg2enc_set_property;
  object_class->get_property = gst_mpeg2enc_get_property;

  /* register properties */
  GstMpeg2EncOptions::initProperties (object_class);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_mpeg2enc_finalize);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mpeg2enc_change_state);
}

static void
gst_mpeg2enc_finalize (GObject * object)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (object);

  if (enc->encoder) {
    delete enc->encoder;

    enc->encoder = NULL;
  }
  delete enc->options;

  g_mutex_free (enc->tlock);
  g_cond_free (enc->cond);
  g_queue_free (enc->time);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mpeg2enc_init (GstMpeg2enc * enc, GstMpeg2encClass * g_class)
{
  GstElement *element = GST_ELEMENT (enc);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  enc->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "sink"), "sink");
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2enc_setcaps));
  gst_pad_set_getcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2enc_getcaps));
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2enc_sink_event));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2enc_chain));
  gst_element_add_pad (element, enc->sinkpad);

  enc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "src"), "src");
  gst_pad_use_fixed_caps (enc->srcpad);
  gst_pad_set_activatepush_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_mpeg2enc_src_activate_push));
  gst_element_add_pad (element, enc->srcpad);

  enc->options = new GstMpeg2EncOptions ();
  enc->encoder = NULL;

  enc->buffer = NULL;
  enc->tlock = g_mutex_new ();
  enc->cond = g_cond_new ();
  enc->time = g_queue_new ();

  gst_mpeg2enc_reset (enc);
}

static void
gst_mpeg2enc_reset (GstMpeg2enc * enc)
{
  GstBuffer *buf;

  enc->eos = FALSE;
  enc->srcresult = GST_FLOW_OK;

  /* in case of error'ed ending */
  if (enc->buffer)
    gst_buffer_unref (enc->buffer);
  enc->buffer = NULL;
  while ((buf = (GstBuffer *) g_queue_pop_head (enc->time)))
    gst_buffer_unref (buf);

  if (enc->encoder) {
    delete enc->encoder;

    enc->encoder = NULL;
  }
}

/* some (!) coding to get caps depending on the video norm and chosen format */
static void
gst_mpeg2enc_add_fps (GstStructure * structure, gint fpss[])
{
  GValue list = { 0, }, fps = {
  0,};
  guint n;

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&fps, GST_TYPE_FRACTION);
  for (n = 0; fpss[n] != 0; n++) {
    gst_value_set_fraction (&fps, fpss[n], fpss[n + 1]);
    gst_value_list_append_value (&list, &fps);
    n++;
  }
  gst_structure_set_value (structure, "framerate", &list);
  g_value_unset (&list);
  g_value_unset (&fps);
}

static inline gint *
gst_mpeg2enc_get_fps (GstMpeg2enc * enc)
{
  static gint fps_pal[]
  = { 24, 1, 25, 1, 50, 1, 0 };
  static gint fps_ntsc[]
  = { 24000, 1001, 24, 1, 30000, 1001, 30, 1, 60000, 1001, 0 };
  static gint fps_all[]
  = { 24000, 1001, 24, 1, 30000, 1001, 30, 1, 60000, 1001, 25, 1, 50, 1, 0 };

  if (enc->options->norm == 'n')
    return fps_ntsc;
  else if (enc->options->norm == 0)
    return fps_all;
  else
    return fps_pal;
}

static GstStructure *
gst_mpeg2enc_structure_from_norm (GstMpeg2enc * enc, gint horiz,
    gint pal_v, gint ntsc_v)
{
  GstStructure *structure;

  structure = gst_structure_new ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'), NULL);

  switch (enc->options->norm) {
    case 0:
    {
      GValue list = { 0, }
      , val = {
      0,};

      g_value_init (&list, GST_TYPE_LIST);
      g_value_init (&val, G_TYPE_INT);
      g_value_set_int (&val, pal_v);
      gst_value_list_append_value (&list, &val);
      g_value_set_int (&val, ntsc_v);
      gst_value_list_append_value (&list, &val);
      gst_structure_set_value (structure, "height", &list);
      g_value_unset (&list);
      g_value_unset (&val);
      break;
    }
    case 'n':
      gst_structure_set (structure, "height", G_TYPE_INT, ntsc_v,
          (void *) NULL);
      break;
    default:
      gst_structure_set (structure, "height", G_TYPE_INT, pal_v, (void *) NULL);
      break;
  }
  gst_structure_set (structure, "width", G_TYPE_INT, horiz, (void *) NULL);
  gst_mpeg2enc_add_fps (structure, gst_mpeg2enc_get_fps (enc));

  return structure;
}

static GstCaps *
gst_mpeg2enc_getcaps (GstPad * pad)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (GST_PAD_PARENT (pad));
  GstCaps *caps;

  caps = GST_PAD_CAPS (pad);
  if (caps) {
    gst_caps_ref (caps);
    return caps;
  }

  switch (enc->options->format) {
    case 1:                    /* vcd */
    case 2:                    /* user vcd */
      caps = gst_caps_new_full (gst_mpeg2enc_structure_from_norm (enc,
              352, 288, 240), NULL);
      break;
    case 4:                    /* svcd */
    case 5:                    /* user svcd */
      caps = gst_caps_new_full (gst_mpeg2enc_structure_from_norm (enc,
              480, 576, 480), NULL);
      break;
    case 6:                    /* vcd stills */
      /* low resolution */
      caps = gst_caps_new_full (gst_mpeg2enc_structure_from_norm (enc,
              352, 288, 240), NULL);
      /* high resolution */
      gst_caps_append_structure (caps,
          gst_mpeg2enc_structure_from_norm (enc, 704, 576, 480));
      break;
    case 7:                    /* svcd stills */
      /* low resolution */
      caps = gst_caps_new_full (gst_mpeg2enc_structure_from_norm (enc,
              480, 576, 480), NULL);
      /* high resolution */
      gst_caps_append_structure (caps,
          gst_mpeg2enc_structure_from_norm (enc, 704, 576, 480));
      break;
    case 0:
    case 3:
    case 8:
    case 9:
    default:
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
      gst_mpeg2enc_add_fps (gst_caps_get_structure (caps, 0),
          gst_mpeg2enc_get_fps (enc));
      break;
  }

  GST_DEBUG_OBJECT (enc, "returned caps %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_mpeg2enc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (GST_PAD_PARENT (pad));
  GstCaps *othercaps = NULL, *mycaps;
  gboolean ret;

  /* does not go well to restart stream mid-way */
  if (enc->encoder)
    goto refuse_renegotiation;

  /* since mpeg encoder does not really check, let's check caps */
  mycaps = gst_pad_get_caps (pad);
  othercaps = gst_caps_intersect (caps, mycaps);
  gst_caps_unref (mycaps);
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto refuse_caps;
  gst_caps_unref (othercaps);
  othercaps = NULL;

  /* create new encoder with these settings */
  enc->encoder = new GstMpeg2Encoder (enc->options, GST_ELEMENT (enc), caps);

  if (!enc->encoder->setup ())
    goto refuse_caps;

  /* and set caps on other side, which should accept anyway */
  othercaps = enc->encoder->getFormat ();
  ret = gst_pad_set_caps (enc->srcpad, othercaps);
  gst_caps_unref (othercaps);
  othercaps = NULL;
  if (!ret)
    goto refuse_caps;

  /* now that we have all the setup and buffers are expected incoming;
   * task can get going */
  gst_pad_start_task (enc->srcpad, (GstTaskFunction) gst_mpeg2enc_loop, enc);

  return TRUE;

refuse_caps:
  {
    GST_WARNING_OBJECT (enc, "refused caps %" GST_PTR_FORMAT, caps);

    if (othercaps)
      gst_caps_unref (othercaps);

    if (enc->encoder) {
      delete enc->encoder;

      enc->encoder = NULL;
    }

    return FALSE;
  }
refuse_renegotiation:
  {
    GST_WARNING_OBJECT (enc, "refused renegotiation (to %" GST_PTR_FORMAT ")",
        caps);

    return FALSE;
  }
}

static gboolean
gst_mpeg2enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstMpeg2enc *enc;
  gboolean result = TRUE;

  enc = GST_MPEG2ENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* forward event */
      result = gst_pad_push_event (enc->srcpad, event);

      /* no special action as there is not much to flush;
       * neither is it possible to halt the mpeg encoding loop */
      goto done;
      break;
    case GST_EVENT_FLUSH_STOP:
      /* forward event */
      result = gst_pad_push_event (enc->srcpad, event);
      if (!result)
        goto done;

      /* this clears the error state in case of a failure in encoding task;
       * so chain function can carry on again */
      GST_MPEG2ENC_MUTEX_LOCK (enc);
      enc->srcresult = GST_FLOW_OK;
      GST_MPEG2ENC_MUTEX_UNLOCK (enc);
      goto done;
      break;
    case GST_EVENT_EOS:
      /* inform the encoding task that it can stop now */
      GST_MPEG2ENC_MUTEX_LOCK (enc);
      enc->eos = TRUE;
      GST_MPEG2ENC_SIGNAL (enc);
      GST_MPEG2ENC_MUTEX_UNLOCK (enc);

      /* eat this event for now, task will send eos when finished */
      gst_event_unref (event);
      goto done;
      break;
    default:
      /* for a serialized event, wait until an earlier buffer is gone,
       * though this is no guarantee as to when the encoder is done with it */
      if (GST_EVENT_IS_SERIALIZED (event)) {
        GST_MPEG2ENC_MUTEX_LOCK (enc);
        while (enc->buffer)
          GST_MPEG2ENC_WAIT (enc);
        GST_MPEG2ENC_MUTEX_UNLOCK (enc);
      }
      break;
  }

  result = gst_pad_push_event (enc->srcpad, event);

done:
  return result;
}

static void
gst_mpeg2enc_loop (GstMpeg2enc * enc)
{
  /* do not try to resume or start when output problems;
   * also ensures a proper (forced) state change */
  if (enc->srcresult != GST_FLOW_OK)
    goto ignore;

  if (enc->encoder) {
    /* note that init performs a pre-fill and therefore needs buffers */
    enc->encoder->init ();
    /* task will stay in here during all of the encoding */
    enc->encoder->encode ();

    /* if not well and truly eos, something strange happened  */
    if (!enc->eos) {
      GST_ERROR_OBJECT (enc, "encoding task ended without being eos");
      /* notify the chain function that it's over */
      GST_MPEG2ENC_MUTEX_LOCK (enc);
      enc->srcresult = GST_FLOW_ERROR;
      GST_MPEG2ENC_SIGNAL (enc);
      GST_MPEG2ENC_MUTEX_UNLOCK (enc);
    } else {
      /* send eos if this was not a forced stop or other problem */
      if (enc->srcresult == GST_FLOW_OK)
        gst_pad_push_event (enc->srcpad, gst_event_new_eos ());
      goto eos;
    }
  } else {
    GST_WARNING_OBJECT (enc, "task started without Mpeg2Encoder");
  }

  /* fall-through */
done:
  {
    /* no need to run wildly, stopped elsewhere, e.g. state change */
    GST_DEBUG_OBJECT (enc, "pausing encoding task");
    gst_pad_pause_task (enc->srcpad);

    return;
  }
eos:
  {
    GST_DEBUG_OBJECT (enc, "encoding task reached eos");
    goto done;
  }
ignore:
  {
    GST_DEBUG_OBJECT (enc, "not looping because encoding task encountered %s",
        gst_flow_get_name (enc->srcresult));
    goto done;
  }
}

static GstFlowReturn
gst_mpeg2enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMpeg2enc *enc;

  enc = GST_MPEG2ENC (GST_PAD_PARENT (pad));

  if (G_UNLIKELY (!enc->encoder))
    goto not_negotiated;

  GST_MPEG2ENC_MUTEX_LOCK (enc);

  if (G_UNLIKELY (enc->eos))
    goto eos;

  if (G_UNLIKELY (enc->srcresult != GST_FLOW_OK))
    goto ignore;

  /* things look good, now inform the encoding task that a buffer is ready */
  while (enc->buffer)
    GST_MPEG2ENC_WAIT (enc);
  enc->buffer = buffer;
  g_queue_push_tail (enc->time, gst_buffer_ref (buffer));
  GST_MPEG2ENC_SIGNAL (enc);
  GST_MPEG2ENC_MUTEX_UNLOCK (enc);

  /* buffer will be released by task */
  return GST_FLOW_OK;

  /* special cases */
not_negotiated:
  {
    GST_ELEMENT_ERROR (enc, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));

    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }
eos:
  {
    GST_DEBUG_OBJECT (enc, "ignoring buffer at end-of-stream");
    GST_MPEG2ENC_MUTEX_UNLOCK (enc);

    gst_buffer_unref (buffer);
    return GST_FLOW_UNEXPECTED;
  }
ignore:
  {
    GstFlowReturn ret = enc->srcresult;

    GST_DEBUG_OBJECT (enc,
        "ignoring buffer because encoding task encountered %s",
        gst_flow_get_name (enc->srcresult));
    GST_MPEG2ENC_MUTEX_UNLOCK (enc);

    gst_buffer_unref (buffer);
    return ret;
  }
}

static void
gst_mpeg2enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GST_MPEG2ENC (object)->options->getProperty (prop_id, value);
}

static void
gst_mpeg2enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GST_MPEG2ENC (object)->options->setProperty (prop_id, value);
}

static gboolean
gst_mpeg2enc_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstMpeg2enc *enc;

  enc = GST_MPEG2ENC (GST_PAD_PARENT (pad));

  if (active) {
    /* setcaps will start task once encoder is setup */
  } else {
    /* can only end the encoding loop by forcing eos */
    GST_MPEG2ENC_MUTEX_LOCK (enc);
    enc->eos = TRUE;
    enc->srcresult = GST_FLOW_WRONG_STATE;
    GST_MPEG2ENC_SIGNAL (enc);
    GST_MPEG2ENC_MUTEX_UNLOCK (enc);

    /* encoding loop should have ended now and can be joined */
    result = gst_pad_stop_task (pad);
  }

  return result;
}

static GstStateChangeReturn
gst_mpeg2enc_change_state (GstElement * element, GstStateChange transition)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mpeg2enc_reset (enc);
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
gst_mpeg2enc_log_callback (log_level_t level, const char *message)
{
  GstDebugLevel gst_level;

#if GST_MJPEGTOOLS_API >= 10903
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
  gst_debug_log (mpeg2enc_debug, gst_level, "", "", 0, NULL, "%s", message);

  /* chain up to the old handler;
   * this could actually be a handler from another mjpegtools based
   * plugin; in which case messages can come out double or from
   * the wrong plugin (element)... */
  old_handler (level, message);
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
#ifndef GST_DISABLE_GST_DEBUG
  old_handler = mjpeg_log_set_handler (gst_mpeg2enc_log_callback);
  g_assert (old_handler != NULL);
#endif
  /* in any case, we do not want default handler output */
  mjpeg_default_handler_verbosity (0);

  return gst_element_register (plugin, "mpeg2enc",
      GST_RANK_MARGINAL, GST_TYPE_MPEG2ENC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpeg2enc",
    "High-quality MPEG-1/2 video encoder",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
