/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <libvisual/libvisual.h>

#define GST_TYPE_VISUAL (gst_visual_get_type())
#define GST_IS_VISUAL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VISUAL))
#define GST_VISUAL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VISUAL,GstVisual))
#define GST_IS_VISUAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VISUAL))
#define GST_VISUAL_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VISUAL,GstVisualClass))
#define GST_VISUAL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VISUAL, GstVisualClass))

typedef struct _GstVisual GstVisual;
typedef struct _GstVisualClass GstVisualClass;

GST_DEBUG_CATEGORY_STATIC (libvisual_debug);
#define GST_CAT_DEFAULT (libvisual_debug)

/* amounf of samples before we can feed libvisual */
#define VISUAL_SAMPLES  512

#define DEFAULT_WIDTH   320
#define DEFAULT_HEIGHT  240
#define DEFAULT_FPS_N   25
#define DEFAULT_FPS_D   1

struct _GstVisual
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;
  GstSegment segment;

  /* libvisual stuff */
  VisAudio *audio;
  VisVideo *video;
  VisActor *actor;

  /* audio/video state */
  gint channels;
  gint rate;                    /* Input samplerate */
  gint bps;

  /* framerate numerator & denominator */
  gint fps_n;
  gint fps_d;
  gint width;
  gint height;
  GstClockTime duration;
  guint outsize;

  /* samples per frame based on caps */
  guint spf;

  /* state stuff */
  GstAdapter *adapter;
  guint count;

  /* QoS stuff *//* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;
};

struct _GstVisualClass
{
  GstElementClass parent_class;

  VisPluginRef *plugin;
};

GType gst_visual_get_type (void);


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN "; "
#if G_BYTE_ORDER == G_BIG_ENDIAN
        GST_VIDEO_CAPS_RGB "; "
#else
        GST_VIDEO_CAPS_BGR "; "
#endif
        GST_VIDEO_CAPS_RGB_16)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "channels = (int) { 1, 2 }, "
#if defined(VISUAL_API_VERSION) && VISUAL_API_VERSION >= 4000 && VISUAL_API_VERSION < 5000
        "rate = (int) { 8000, 11250, 22500, 32000, 44100, 48000, 96000 }"
#else
        "rate = (int) [ 1000, MAX ]"
#endif
    )
    );


static void gst_visual_class_init (gpointer g_class, gpointer class_data);
static void gst_visual_init (GstVisual * visual);
static void gst_visual_dispose (GObject * object);

static GstStateChangeReturn gst_visual_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_visual_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_visual_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_visual_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_visual_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_visual_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_visual_src_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_visual_getcaps (GstPad * pad);
static void libvisual_log_handler (const char *message, const char *funcname,
    void *priv);

static GstElementClass *parent_class = NULL;

GType
gst_visual_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstVisualClass),
      NULL,
      NULL,
      gst_visual_class_init,
      NULL,
      NULL,
      sizeof (GstVisual),
      0,
      (GInstanceInitFunc) gst_visual_init,
    };

    type = g_type_register_static (GST_TYPE_ELEMENT, "GstVisual", &info, 0);
  }
  return type;
}

static void
libvisual_log_handler (const char *message, const char *funcname, void *priv)
{
  GST_CAT_LEVEL_LOG (libvisual_debug, (GstDebugLevel) (priv), NULL, "%s - %s",
      funcname, message);
}

static void
gst_visual_class_init (gpointer g_class, gpointer class_data)
{
  GstVisualClass *klass = GST_VISUAL_CLASS (g_class);
  GstElementClass *element = GST_ELEMENT_CLASS (g_class);
  GObjectClass *object = G_OBJECT_CLASS (g_class);

  klass->plugin = class_data;

  element->change_state = gst_visual_change_state;

  if (class_data == NULL) {
    parent_class = g_type_class_peek_parent (g_class);
  } else {
    char *longname = g_strdup_printf ("libvisual %s plugin v.%s",
        klass->plugin->info->name, klass->plugin->info->version);

    /* FIXME: improve to only register what plugin supports? */
    gst_element_class_add_static_pad_template (element, &src_template);
    gst_element_class_add_static_pad_template (element, &sink_template);
    gst_element_class_set_details_simple (element,
        longname, "Visualization",
        klass->plugin->info->about, "Benjamin Otte <otte@gnome.org>");

    g_free (longname);
  }

  object->dispose = gst_visual_dispose;
}

static void
gst_visual_init (GstVisual * visual)
{
  /* create the sink and src pads */
  visual->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (visual->sinkpad, gst_visual_sink_setcaps);
  gst_pad_set_chain_function (visual->sinkpad, gst_visual_chain);
  gst_pad_set_event_function (visual->sinkpad, gst_visual_sink_event);
  gst_element_add_pad (GST_ELEMENT (visual), visual->sinkpad);

  visual->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_setcaps_function (visual->srcpad, gst_visual_src_setcaps);
  gst_pad_set_getcaps_function (visual->srcpad, gst_visual_getcaps);
  gst_pad_set_event_function (visual->srcpad, gst_visual_src_event);
  gst_pad_set_query_function (visual->srcpad, gst_visual_src_query);
  gst_element_add_pad (GST_ELEMENT (visual), visual->srcpad);

  visual->adapter = gst_adapter_new ();
}

static void
gst_visual_clear_actors (GstVisual * visual)
{
  if (visual->actor) {
    visual_object_unref (VISUAL_OBJECT (visual->actor));
    visual->actor = NULL;
  }
  if (visual->video) {
    visual_object_unref (VISUAL_OBJECT (visual->video));
    visual->video = NULL;
  }
  if (visual->audio) {
    visual_object_unref (VISUAL_OBJECT (visual->audio));
    visual->audio = NULL;
  }
}

static void
gst_visual_dispose (GObject * object)
{
  GstVisual *visual = GST_VISUAL (object);

  if (visual->adapter) {
    g_object_unref (visual->adapter);
    visual->adapter = NULL;
  }
  gst_visual_clear_actors (visual);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_visual_reset (GstVisual * visual)
{
  gst_adapter_clear (visual->adapter);
  gst_segment_init (&visual->segment, GST_FORMAT_UNDEFINED);

  GST_OBJECT_LOCK (visual);
  visual->proportion = 1.0;
  visual->earliest_time = -1;
  GST_OBJECT_UNLOCK (visual);
}

static GstCaps *
gst_visual_getcaps (GstPad * pad)
{
  GstCaps *ret;
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  int depths;

  if (!visual->actor) {
    ret = gst_caps_copy (gst_pad_get_pad_template_caps (visual->srcpad));
    goto beach;
  }

  ret = gst_caps_new_empty ();
  depths = visual_actor_get_supported_depth (visual->actor);
  if (depths < 0) {
    /* FIXME: set an error */
    goto beach;
  }
  if (depths == VISUAL_VIDEO_DEPTH_GL) {
    /* We can't handle GL only plugins */
    goto beach;
  }

  GST_DEBUG_OBJECT (visual, "libvisual plugin supports depths %u (0x%04x)",
      depths, depths);
  /* if (depths & VISUAL_VIDEO_DEPTH_32BIT) Always supports 32bit output */
  gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN));

  if (depths & VISUAL_VIDEO_DEPTH_24BIT) {
#if G_BYTE_ORDER == G_BIG_ENDIAN
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_RGB));
#else
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_BGR));
#endif
  }
  if (depths & VISUAL_VIDEO_DEPTH_16BIT) {
    gst_caps_append (ret, gst_caps_from_string (GST_VIDEO_CAPS_RGB_16));
  }

beach:

  GST_DEBUG_OBJECT (visual, "returning caps %" GST_PTR_FORMAT, ret);
  gst_object_unref (visual);
  return ret;
}

static gboolean
gst_visual_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;
  gint depth, pitch;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (visual, "src pad got caps %" GST_PTR_FORMAT, caps);

  if (!gst_structure_get_int (structure, "width", &visual->width))
    goto error;
  if (!gst_structure_get_int (structure, "height", &visual->height))
    goto error;
  if (!gst_structure_get_int (structure, "bpp", &depth))
    goto error;
  if (!gst_structure_get_fraction (structure, "framerate", &visual->fps_n,
          &visual->fps_d))
    goto error;

  visual_video_set_depth (visual->video,
      visual_video_depth_enum_from_value (depth));
  visual_video_set_dimension (visual->video, visual->width, visual->height);
  pitch = GST_ROUND_UP_4 (visual->width * visual->video->bpp);
  visual_video_set_pitch (visual->video, pitch);
  visual_actor_video_negotiate (visual->actor, 0, FALSE, FALSE);

  /* precalc some values */
  visual->outsize = visual->video->height * pitch;
  visual->spf =
      gst_util_uint64_scale_int (visual->rate, visual->fps_d, visual->fps_n);
  visual->duration =
      gst_util_uint64_scale_int (GST_SECOND, visual->fps_d, visual->fps_n);

  gst_object_unref (visual);
  return TRUE;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (visual, "error parsing caps");
    gst_object_unref (visual);
    return FALSE;
  }
}

static gboolean
gst_visual_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &visual->channels);
  gst_structure_get_int (structure, "rate", &visual->rate);

  /* this is how many samples we need to fill one frame at the requested
   * framerate. */
  if (visual->fps_n != 0) {
    visual->spf =
        gst_util_uint64_scale_int (visual->rate, visual->fps_d, visual->fps_n);
  }
  visual->bps = visual->channels * sizeof (gint16);

  gst_object_unref (visual);
  return TRUE;
}

static gboolean
gst_vis_src_negotiate (GstVisual * visual)
{
  GstCaps *othercaps, *target;
  GstStructure *structure;
  GstCaps *caps;

  caps = gst_pad_get_caps (visual->srcpad);

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (visual->srcpad);
  if (othercaps) {
    target = gst_caps_intersect (othercaps, caps);
    gst_caps_unref (othercaps);
    gst_caps_unref (caps);

    if (gst_caps_is_empty (target))
      goto no_format;

    gst_caps_truncate (target);
  } else {
    /* need a copy, we'll be modifying it when fixating */
    target = gst_caps_copy (caps);
    gst_caps_unref (caps);
  }

  /* fixate in case something is not fixed. This does nothing if the value is
   * already fixed. For video we always try to fixate to something like
   * 320x240x25 by convention. */
  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_WIDTH);
  gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_HEIGHT);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate",
      DEFAULT_FPS_N, DEFAULT_FPS_D);

  gst_pad_set_caps (visual->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

  /* ERRORS */
no_format:
  {
    GST_ELEMENT_ERROR (visual, STREAM, FORMAT, (NULL),
        ("could not negotiate output format"));
    gst_caps_unref (target);
    return FALSE;
  }
}

static gboolean
gst_visual_sink_event (GstPad * pad, GstEvent * event)
{
  GstVisual *visual;
  gboolean res;

  visual = GST_VISUAL (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* reset QoS and adapter. */
      gst_visual_reset (visual);
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      /* the newsegment values are used to clip the input samples
       * and to convert the incomming timestamps to running time so
       * we can do QoS */
      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* now configure the values */
      gst_segment_set_newsegment_full (&visual->segment, update,
          rate, arate, format, start, stop, time);

      /* and forward */
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (visual->srcpad, event);
      break;
  }

  gst_object_unref (visual);
  return res;
}

static gboolean
gst_visual_src_event (GstPad * pad, GstEvent * event)
{
  GstVisual *visual;
  gboolean res;

  visual = GST_VISUAL (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      /* save stuff for the _chain function */
      GST_OBJECT_LOCK (visual);
      visual->proportion = proportion;
      if (diff >= 0)
        /* we're late, this is a good estimate for next displayable
         * frame (see part-qos.txt) */
        visual->earliest_time = timestamp + 2 * diff + visual->duration;
      else
        visual->earliest_time = timestamp + diff;

      GST_OBJECT_UNLOCK (visual);

      res = gst_pad_push_event (visual->sinkpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (visual->sinkpad, event);
      break;
  }

  gst_object_unref (visual);
  return res;
}

static gboolean
gst_visual_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res;
  GstVisual *visual;

  visual = GST_VISUAL (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;
      gboolean us_live;
      GstClockTime our_latency;
      guint max_samples;

      if ((res = gst_pad_peer_query (visual->sinkpad, query))) {
        gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (visual, "Peer latency: min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        /* the max samples we must buffer */
        max_samples = MAX (VISUAL_SAMPLES, visual->spf);
        our_latency =
            gst_util_uint64_scale_int (max_samples, GST_SECOND, visual->rate);

        GST_DEBUG_OBJECT (visual, "Our latency: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (our_latency));

        /* we add some latency but only if we need to buffer more than what
         * upstream gives us */
        min_latency += our_latency;
        if (max_latency != -1)
          max_latency += our_latency;

        GST_DEBUG_OBJECT (visual, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        gst_query_set_latency (query, TRUE, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_peer_query (visual->sinkpad, query);
      break;
  }

  gst_object_unref (visual);

  return res;
}

/* allocate and output buffer, if no format was negotiated, this
 * function will negotiate one. After calling this function, a
 * reverse negotiation could have happened. */
static GstFlowReturn
get_buffer (GstVisual * visual, GstBuffer ** outbuf)
{
  GstFlowReturn ret;

  /* we don't know an output format yet, pick one */
  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    if (!gst_vis_src_negotiate (visual))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_DEBUG_OBJECT (visual, "allocating output buffer with caps %"
      GST_PTR_FORMAT, GST_PAD_CAPS (visual->srcpad));

  /* now allocate a buffer with the last negotiated format. 
   * Downstream could renegotiate a new format, which will trigger
   * our setcaps function on the source pad. */
  ret =
      gst_pad_alloc_buffer_and_set_caps (visual->srcpad,
      GST_BUFFER_OFFSET_NONE, visual->outsize,
      GST_PAD_CAPS (visual->srcpad), outbuf);

  /* no buffer allocated, we don't care why. */
  if (ret != GST_FLOW_OK)
    return ret;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_visual_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBuffer *outbuf = NULL;
  guint i;
  GstVisual *visual = GST_VISUAL (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  guint avail;

  GST_DEBUG_OBJECT (visual, "chain function called");

  /* If we don't have an output format yet, preallocate a buffer to try and
   * set one */
  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    ret = get_buffer (visual, &outbuf);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buffer);
      goto beach;
    }
  }

  /* resync on DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (visual->adapter);
  }

  GST_DEBUG_OBJECT (visual,
      "Input buffer has %d samples, time=%" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (buffer) / visual->bps, GST_BUFFER_TIMESTAMP (buffer));

  gst_adapter_push (visual->adapter, buffer);

  while (TRUE) {
    gboolean need_skip;
    const guint16 *data;
    guint64 dist, timestamp;

    GST_DEBUG_OBJECT (visual, "processing buffer");

    avail = gst_adapter_available (visual->adapter);
    GST_DEBUG_OBJECT (visual, "avail now %u", avail);

    /* we need at least VISUAL_SAMPLES samples */
    if (avail < VISUAL_SAMPLES * visual->bps)
      break;

    /* we need at least enough samples to make one frame */
    if (avail < visual->spf * visual->bps)
      break;

    /* get timestamp of the current adapter byte */
    timestamp = gst_adapter_prev_timestamp (visual->adapter, &dist);
    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* convert bytes to time */
      dist /= visual->bps;
      timestamp += gst_util_uint64_scale_int (dist, GST_SECOND, visual->rate);
    }

    if (timestamp != -1) {
      gint64 qostime;

      /* QoS is done on running time */
      qostime = gst_segment_to_running_time (&visual->segment, GST_FORMAT_TIME,
          timestamp);
      qostime += visual->duration;

      GST_OBJECT_LOCK (visual);
      /* check for QoS, don't compute buffers that are known to be late */
      need_skip = visual->earliest_time != -1 &&
          qostime <= visual->earliest_time;
      GST_OBJECT_UNLOCK (visual);

      if (need_skip) {
        GST_WARNING_OBJECT (visual,
            "QoS: skip ts: %" GST_TIME_FORMAT ", earliest: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (qostime), GST_TIME_ARGS (visual->earliest_time));
        goto skip;
      }
    }

    /* Read VISUAL_SAMPLES samples per channel */
    data =
        (const guint16 *) gst_adapter_peek (visual->adapter,
        VISUAL_SAMPLES * visual->bps);

#if defined(VISUAL_API_VERSION) && VISUAL_API_VERSION >= 4000 && VISUAL_API_VERSION < 5000
    {
      VisBuffer *lbuf, *rbuf;
      guint16 ldata[VISUAL_SAMPLES], rdata[VISUAL_SAMPLES];
      VisAudioSampleRateType rate;

      lbuf = visual_buffer_new_with_buffer (ldata, sizeof (ldata), NULL);
      rbuf = visual_buffer_new_with_buffer (rdata, sizeof (rdata), NULL);

      if (visual->channels == 2) {
        for (i = 0; i < VISUAL_SAMPLES; i++) {
          ldata[i] = *data++;
          rdata[i] = *data++;
        }
      } else {
        for (i = 0; i < VISUAL_SAMPLES; i++) {
          ldata[i] = *data;
          rdata[i] = *data++;
        }
      }

      switch (visual->rate) {
        case 8000:
          rate = VISUAL_AUDIO_SAMPLE_RATE_8000;
          break;
        case 11250:
          rate = VISUAL_AUDIO_SAMPLE_RATE_11250;
          break;
        case 22500:
          rate = VISUAL_AUDIO_SAMPLE_RATE_22500;
          break;
        case 32000:
          rate = VISUAL_AUDIO_SAMPLE_RATE_32000;
          break;
        case 44100:
          rate = VISUAL_AUDIO_SAMPLE_RATE_44100;
          break;
        case 48000:
          rate = VISUAL_AUDIO_SAMPLE_RATE_48000;
          break;
        case 96000:
          rate = VISUAL_AUDIO_SAMPLE_RATE_96000;
          break;
        default:
          visual_object_unref (VISUAL_OBJECT (lbuf));
          visual_object_unref (VISUAL_OBJECT (rbuf));
          GST_ERROR_OBJECT (visual, "unsupported rate %d", visual->rate);
          ret = GST_FLOW_ERROR;
          goto beach;
          break;
      }

      visual_audio_samplepool_input_channel (visual->audio->samplepool,
          lbuf,
          rate, VISUAL_AUDIO_SAMPLE_FORMAT_S16,
          (char *) VISUAL_AUDIO_CHANNEL_LEFT);
      visual_audio_samplepool_input_channel (visual->audio->samplepool, rbuf,
          rate, VISUAL_AUDIO_SAMPLE_FORMAT_S16,
          (char *) VISUAL_AUDIO_CHANNEL_RIGHT);

      visual_object_unref (VISUAL_OBJECT (lbuf));
      visual_object_unref (VISUAL_OBJECT (rbuf));

    }
#else
    if (visual->channels == 2) {
      for (i = 0; i < VISUAL_SAMPLES; i++) {
        visual->audio->plugpcm[0][i] = *data++;
        visual->audio->plugpcm[1][i] = *data++;
      }
    } else {
      for (i = 0; i < VISUAL_SAMPLES; i++) {
        visual->audio->plugpcm[0][i] = *data;
        visual->audio->plugpcm[1][i] = *data++;
      }
    }
#endif

    /* alloc a buffer if we don't have one yet, this happens
     * when we pushed a buffer in this while loop before */
    if (outbuf == NULL) {
      ret = get_buffer (visual, &outbuf);
      if (ret != GST_FLOW_OK) {
        goto beach;
      }
    }
    visual_video_set_buffer (visual->video, GST_BUFFER_DATA (outbuf));
    visual_audio_analyze (visual->audio);
    visual_actor_run (visual->actor, visual->audio);
    visual_video_set_buffer (visual->video, NULL);
    GST_DEBUG_OBJECT (visual, "rendered one frame");

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    GST_BUFFER_DURATION (outbuf) = visual->duration;

    ret = gst_pad_push (visual->srcpad, outbuf);
    outbuf = NULL;

  skip:
    GST_DEBUG_OBJECT (visual, "finished frame, flushing %u samples from input",
        visual->spf);

    /* Flush out the number of samples per frame */
    gst_adapter_flush (visual->adapter, visual->spf * visual->bps);

    /* quit the loop if something was wrong */
    if (ret != GST_FLOW_OK)
      break;
  }

beach:

  if (outbuf != NULL)
    gst_buffer_unref (outbuf);

  gst_object_unref (visual);

  return ret;
}

static GstStateChangeReturn
gst_visual_change_state (GstElement * element, GstStateChange transition)
{
  GstVisual *visual = GST_VISUAL (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      visual->actor =
          visual_actor_new (GST_VISUAL_GET_CLASS (visual)->plugin->info->
          plugname);
      visual->video = visual_video_new ();
      visual->audio = visual_audio_new ();
      /* can't have a play without actors */
      if (!visual->actor || !visual->video)
        goto no_actors;

      if (visual_actor_realize (visual->actor) != 0)
        goto no_realize;

      visual_actor_set_video (visual->actor, visual->video);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_visual_reset (visual);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_visual_clear_actors (visual);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
no_actors:
  {
    GST_ELEMENT_ERROR (visual, LIBRARY, INIT, (NULL),
        ("could not create actors"));
    gst_visual_clear_actors (visual);
    return GST_STATE_CHANGE_FAILURE;
  }
no_realize:
  {
    GST_ELEMENT_ERROR (visual, LIBRARY, INIT, (NULL),
        ("could not realize actor"));
    gst_visual_clear_actors (visual);
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
make_valid_name (char *name)
{
  /*
   * Replace invalid chars with _ in the type name
   */
  static const gchar extra_chars[] = "-_+";
  gchar *p = name;

  for (; *p; p++) {
    int valid = ((p[0] >= 'A' && p[0] <= 'Z') ||
        (p[0] >= 'a' && p[0] <= 'z') ||
        (p[0] >= '0' && p[0] <= '9') || strchr (extra_chars, p[0]));
    if (!valid)
      *p = '_';
  }
}

static gboolean
gst_visual_actor_plugin_is_gl (VisObject * plugin, const gchar * name)
{
  gboolean is_gl;
  gint depth;

#if !defined(VISUAL_API_VERSION)

  depth = VISUAL_PLUGIN_ACTOR (plugin)->depth;
  is_gl = (depth == VISUAL_VIDEO_DEPTH_GL);

#elif VISUAL_API_VERSION >= 4000 && VISUAL_API_VERSION < 5000

  depth = VISUAL_ACTOR_PLUGIN (plugin)->vidoptions.depth;
  /* FIXME: how to figure this out correctly in 0.4? */
  is_gl = (depth & VISUAL_VIDEO_DEPTH_GL) == VISUAL_VIDEO_DEPTH_GL;

#else
# error what libvisual version is this?
#endif

  if (!is_gl) {
    GST_DEBUG ("plugin %s is not a GL plugin (%d), registering", name, depth);
  } else {
    GST_DEBUG ("plugin %s is a GL plugin (%d), ignoring", name, depth);
  }

  return is_gl;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  guint i, count;
  VisList *list;

  GST_DEBUG_CATEGORY_INIT (libvisual_debug, "libvisual", 0,
      "libvisual audio visualisations");

#ifdef LIBVISUAL_PLUGINSBASEDIR
  gst_plugin_add_dependency_simple (plugin, "HOME/.libvisual/actor",
      LIBVISUAL_PLUGINSBASEDIR "/actor", NULL, GST_PLUGIN_DEPENDENCY_FLAG_NONE);
#endif

  visual_log_set_verboseness (VISUAL_LOG_VERBOSENESS_LOW);
  visual_log_set_info_handler (libvisual_log_handler, (void *) GST_LEVEL_INFO);
  visual_log_set_warning_handler (libvisual_log_handler,
      (void *) GST_LEVEL_WARNING);
  visual_log_set_critical_handler (libvisual_log_handler,
      (void *) GST_LEVEL_ERROR);
  visual_log_set_error_handler (libvisual_log_handler,
      (void *) GST_LEVEL_ERROR);

  if (!visual_is_initialized ())
    if (visual_init (NULL, NULL) != 0)
      return FALSE;

  list = visual_actor_get_list ();

#if !defined(VISUAL_API_VERSION)
  count = visual_list_count (list);
#elif VISUAL_API_VERSION >= 4000 && VISUAL_API_VERSION < 5000
  count = visual_collection_size (VISUAL_COLLECTION (list));
#endif

  for (i = 0; i < count; i++) {
    VisPluginRef *ref = visual_list_get (list, i);
    VisPluginData *visplugin = NULL;
    gboolean skip = FALSE;
    GType type;
    gchar *name;
    GTypeInfo info = {
      sizeof (GstVisualClass),
      NULL,
      NULL,
      gst_visual_class_init,
      NULL,
      ref,
      sizeof (GstVisual),
      0,
      NULL
    };

    visplugin = visual_plugin_load (ref);

    if (ref->info->plugname == NULL)
      continue;

    /* Blacklist some plugins */
    if (strcmp (ref->info->plugname, "gstreamer") == 0 ||
        strcmp (ref->info->plugname, "gdkpixbuf") == 0) {
      skip = TRUE;
    } else {
      /* Ignore plugins that only support GL output for now */
      skip = gst_visual_actor_plugin_is_gl (visplugin->info->plugin,
          visplugin->info->plugname);
    }

    visual_plugin_unload (visplugin);

    if (!skip) {
      name = g_strdup_printf ("GstVisual%s", ref->info->plugname);
      make_valid_name (name);
      type = g_type_register_static (GST_TYPE_VISUAL, name, &info, 0);
      g_free (name);

      name = g_strdup_printf ("libvisual_%s", ref->info->plugname);
      make_valid_name (name);
      if (!gst_element_register (plugin, name, GST_RANK_NONE, type)) {
        g_free (name);
        return FALSE;
      }
      g_free (name);
    }
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "libvisual",
    "libvisual visualization plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
