/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstoggdemux.c: ogg stream demuxer
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
#include <ogg/ogg.h>
#include <string.h>

#define CHUNKSIZE (8500)        /* this is out of vorbisfile */

enum
{
  OV_EREAD = -1,
  OV_EFAULT = -2,
  OV_FALSE = -3,
  OV_EOF = -4,
};

GST_DEBUG_CATEGORY_STATIC (gst_ogg_demux_debug);
GST_DEBUG_CATEGORY_STATIC (gst_ogg_demux_setup_debug);
#define GST_CAT_DEFAULT gst_ogg_demux_debug

#define GST_TYPE_OGG_PAD (gst_ogg_pad_get_type())
#define GST_OGG_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGG_PAD, GstOggPad))
#define GST_OGG_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGG_PAD, GstOggPad))
#define GST_IS_OGG_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGG_PAD))
#define GST_IS_OGG_PAD_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGG_PAD))

typedef struct _GstOggPad GstOggPad;
typedef struct _GstOggPadClass GstOggPadClass;

#define GST_TYPE_OGG_DEMUX (gst_ogg_demux_get_type())
#define GST_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_OGG_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_IS_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGG_DEMUX))
#define GST_IS_OGG_DEMUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGG_DEMUX))

static GType gst_ogg_demux_get_type (void);

typedef struct _GstOggDemux GstOggDemux;
typedef struct _GstOggDemuxClass GstOggDemuxClass;

/* all information needed for one ogg chain (relevant for chained bitstreams) */
typedef struct _GstOggChain
{
  GstOggDemux *ogg;

  gint64 offset;                /* starting offset of chain */
  gint64 end_offset;            /* end offset of chain */
  gint64 bytes;                 /* number of bytes */

  gboolean have_bos;

  GArray *streams;

  GstClockTime total_time;      /* the total time of this chain, this is the MAX of
                                   the totals of all streams */
  GstClockTime begin_time;      /* when this chain starts in the stream */

  GstClockTime segment_start;   /* the timestamp of the first sample, this is the MIN of
                                   the start times of all streams. */
  GstClockTime segment_stop;    /* the timestamp of the last page, this is the MAX of the
                                   streams. */
} GstOggChain;

/* different modes for the pad */
typedef enum
{
  GST_OGG_PAD_MODE_INIT,        /* we are feeding our internal decoder to get info */
  GST_OGG_PAD_MODE_STREAMING,   /* we are streaming buffers to the outside */
} GstOggPadMode;

#define PARENT                GstPad
#define PARENTCLASS   GstPadClass

/* all information needed for one ogg stream */
struct _GstOggPad
{
  PARENT pad;                   /* subclass GstPad */

  gboolean have_type;
  GstOggPadMode mode;

  GstPad *elem_pad;             /* sinkpad of internal element */
  GstElement *element;          /* internal element */
  GstPad *elem_out;             /* our sinkpad to receive buffers form the internal element */

  GstOggChain *chain;           /* the chain we are part of */
  GstOggDemux *ogg;             /* the ogg demuxer we are part of */

  GList *headers;

  gint serialno;
  gint64 packetno;
  gint64 current_granule;

  GstClockTime start_time;      /* the timestamp of the first sample */

  gint64 first_granule;         /* the granulepos of first page == first sample in next page */
  GstClockTime first_time;      /* the timestamp of the second page */

  GstClockTime last_time;       /* the timestamp of the last page == last sample */
  gint64 last_granule;          /* the granulepos of the last page */

  GstClockTime total_time;      /* the total time of this stream */

  ogg_stream_state stream;
};

struct _GstOggPadClass
{
  PARENTCLASS parent_class;
};

#define GST_CHAIN_LOCK(ogg)	g_mutex_lock((ogg)->chain_lock)
#define GST_CHAIN_UNLOCK(ogg)	g_mutex_unlock((ogg)->chain_lock)

struct _GstOggDemux
{
  GstElement element;

  GstPad *sinkpad;

  gint64 length;
  gint64 offset;

  gboolean seekable;
  gboolean running;

  gboolean need_chains;

  /* state */
  GMutex *chain_lock;           /* we need the lock to protect the chains */
  GArray *chains;               /* list of chains we know */
  GstClockTime total_time;      /* the total time of this ogg, this is the sum of
                                   the totals of all chains */
  GstOggChain *current_chain;
  GstOggChain *building_chain;

  /* playback start/stop positions */
  GstClockTime segment_start;
  GstClockTime segment_stop;
  gboolean segment_play;
  gdouble segment_rate;

  gint64 current_granule;
  GstClockTime current_time;

  /* ogg stuff */
  ogg_sync_state sync;
};

struct _GstOggDemuxClass
{
  GstElementClass parent_class;
};

static GstStaticPadTemplate internaltemplate =
GST_STATIC_PAD_TEMPLATE ("internal",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean gst_ogg_demux_collect_chain_info (GstOggDemux * ogg,
    GstOggChain * chain);
static gboolean gst_ogg_demux_activate_chain (GstOggDemux * ogg,
    GstOggChain * chain, GstEvent * event);

static gboolean gst_ogg_demux_perform_seek (GstOggDemux * ogg,
    gboolean accurate, gboolean flush);

static void gst_ogg_pad_class_init (GstOggPadClass * klass);
static void gst_ogg_pad_init (GstOggPad * pad);
static void gst_ogg_pad_dispose (GObject * object);
static void gst_ogg_pad_finalize (GObject * object);

#if 0
static const GstFormat *gst_ogg_pad_formats (GstPad * pad);
static const GstEventMask *gst_ogg_pad_event_masks (GstPad * pad);
#endif
static const GstQueryType *gst_ogg_pad_query_types (GstPad * pad);
static gboolean gst_ogg_pad_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_ogg_pad_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_ogg_pad_getcaps (GstPad * pad);
static GstCaps *gst_ogg_type_find (ogg_packet * packet);

static GstPadClass *ogg_pad_parent_class = NULL;

static GType
gst_ogg_pad_get_type (void)
{
  static GType ogg_pad_type = 0;

  if (!ogg_pad_type) {
    static const GTypeInfo ogg_pad_info = {
      sizeof (GstOggPadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ogg_pad_class_init,
      NULL,
      NULL,
      sizeof (GstOggPad),
      0,
      (GInstanceInitFunc) gst_ogg_pad_init,
    };

    ogg_pad_type =
        g_type_register_static (GST_TYPE_PAD, "GstOggPad", &ogg_pad_info, 0);
  }
  return ogg_pad_type;
}

static void
gst_ogg_pad_class_init (GstOggPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  ogg_pad_parent_class = g_type_class_ref (GST_TYPE_PAD);

  gobject_class->dispose = gst_ogg_pad_dispose;
  gobject_class->finalize = gst_ogg_pad_finalize;
}

static void
gst_ogg_pad_init (GstOggPad * pad)
{
  gst_pad_set_event_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_event));
  gst_pad_set_getcaps_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_getcaps));
  gst_pad_set_query_type_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_query_types));
  gst_pad_set_query_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_src_query));

  pad->mode = GST_OGG_PAD_MODE_INIT;

  pad->first_granule = -1;
  pad->last_granule = -1;
  pad->current_granule = -1;

  pad->start_time = GST_CLOCK_TIME_NONE;
  pad->first_time = GST_CLOCK_TIME_NONE;
  pad->last_time = GST_CLOCK_TIME_NONE;
  pad->total_time = GST_CLOCK_TIME_NONE;

  pad->have_type = FALSE;
  pad->headers = NULL;
}

static void
gst_ogg_pad_dispose (GObject * object)
{
  GstOggPad *pad = GST_OGG_PAD (object);

  if (pad->element)
    gst_element_set_state (pad->element, GST_STATE_NULL);

  gst_object_replace ((GstObject **) (&pad->elem_pad), NULL);
  gst_object_replace ((GstObject **) (&pad->element), NULL);
  gst_object_replace ((GstObject **) (&pad->elem_out), NULL);

  pad->chain = NULL;
  pad->ogg = NULL;

  g_list_foreach (pad->headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (pad->headers);
  pad->headers = NULL;

  ogg_stream_reset (&pad->stream);

  G_OBJECT_CLASS (ogg_pad_parent_class)->dispose (object);
}

static void
gst_ogg_pad_finalize (GObject * object)
{
  GstOggPad *pad = GST_OGG_PAD (object);

  ogg_stream_clear (&pad->stream);

  G_OBJECT_CLASS (ogg_pad_parent_class)->finalize (object);
}

#if 0
static const GstFormat *
gst_ogg_pad_formats (GstPad * pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_DEFAULT,         /* time */
    GST_FORMAT_TIME,            /* granulepos */
    0
  };
  static GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,         /* bytes */
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}
#endif

#if 0
static const GstEventMask *
gst_ogg_pad_event_masks (GstPad * pad)
{
  static const GstEventMask src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return src_event_masks;
}
#endif

static const GstQueryType *
gst_ogg_pad_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return query_types;
}

static GstCaps *
gst_ogg_pad_getcaps (GstPad * pad)
{
  return gst_caps_ref (GST_PAD_CAPS (pad));
}

static gboolean
gst_ogg_pad_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstOggDemux *ogg;
  GstOggPad *cur;

  ogg = GST_OGG_DEMUX (GST_PAD_PARENT (pad));
  cur = GST_OGG_PAD (pad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL, NULL);
      /* can only get position in time */
      if (format != GST_FORMAT_TIME) {
        GST_DEBUG ("only query position on TIME is supported");
        res = FALSE;
        goto done;
      }
      /* can only return the total time position */
      gst_query_set_position (query, GST_FORMAT_TIME, -1, ogg->total_time);
      break;
    }
    case GST_QUERY_CONVERT:
      /* hmm .. */
      res = FALSE;
      break;
    default:
      res = FALSE;
      break;
  }
done:
  return res;
}

static gboolean
gst_ogg_pad_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstOggDemux *ogg;
  GstOggPad *cur;

  ogg = GST_OGG_DEMUX (GST_PAD_PARENT (pad));
  cur = GST_OGG_PAD (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gboolean running;
      gboolean flush, accurate;
      GstFormat format;
      gdouble rate;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;

      /* can't seek if we are not seekable, FIXME could pass the
       * seek query upstream after converting it to bytes using
       * the average bitrate of the stream. */
      if (!ogg->seekable) {
        GST_DEBUG ("seek on non seekable stream");
        goto error;
      }

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      /* we can only seek on time */
      if (format != GST_FORMAT_TIME) {
        GST_DEBUG ("can only seek on TIME");
        goto error;
      }
      /* cannot yet do backwards playback */
      if (rate <= 0.0) {
        GST_DEBUG ("can only seek with positive rate");
        goto error;
      }

      /* store start and stop values */
      GST_LOCK (ogg);
      if (cur_type == GST_SEEK_TYPE_SET)
        ogg->segment_start = cur;
      else if (cur_type == GST_SEEK_TYPE_CUR)
        ogg->segment_start += cur;

      if (stop_type != GST_SEEK_TYPE_NONE)
        ogg->segment_stop = stop;
      else if (stop_type == GST_SEEK_TYPE_CUR)
        ogg->segment_stop += cur;

      ogg->segment_rate = rate;
      ogg->segment_play = !!(flags & GST_SEEK_FLAG_SEGMENT);
      flush = (flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH;
      accurate = (flags & GST_SEEK_FLAG_ACCURATE) == GST_SEEK_FLAG_ACCURATE;
      gst_event_unref (event);

      GST_DEBUG ("segment positions set to %" GST_TIME_FORMAT "-%"
          GST_TIME_FORMAT, GST_TIME_ARGS (ogg->segment_start),
          GST_TIME_ARGS (ogg->segment_stop));

      /* check if we can do the seek now */
      running = ogg->running;
      GST_UNLOCK (ogg);

      /* now do the seek */
      if (running) {
        res = gst_ogg_demux_perform_seek (ogg, accurate, flush);
      } else
        res = TRUE;
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  return res;

error:
  GST_DEBUG ("error handling event");
  gst_event_unref (event);
  return FALSE;
}

static void
gst_ogg_pad_reset (GstOggPad * pad)
{
  ogg_stream_reset (&pad->stream);
  /* FIXME: need a discont here */
}

/* the filter function for selecting the elements we can use in
 * autoplugging */
static gboolean
gst_ogg_demux_factory_filter (GstPluginFeature * feature, GstCaps * caps)
{
  guint rank;
  const gchar *klass;

  /* we only care about element factories */
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  klass = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (feature));
  /* only demuxers and decoders can play */
  if (strstr (klass, "Demux") == NULL &&
      strstr (klass, "Decoder") == NULL && strstr (klass, "Parse") == NULL) {
    return FALSE;
  }

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (feature);
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  GST_DEBUG ("checking factory %s", GST_PLUGIN_FEATURE_NAME (feature));
  /* now see if it is compatible with the caps */
  {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);
    const GList *templates;
    GList *walk;

    /* get the templates from the element factory */
    templates = gst_element_factory_get_static_pad_templates (factory);

    for (walk = (GList *) templates; walk; walk = g_list_next (walk)) {
      GstStaticPadTemplate *templ = walk->data;

      /* we only care about the sink templates */
      if (templ->direction == GST_PAD_SINK) {
        GstCaps *intersect;
        GstCaps *scaps;
        gboolean empty;

        /* try to intersect the caps with the caps of the template */
        scaps = gst_static_caps_get (&templ->static_caps);
        intersect = gst_caps_intersect (caps, scaps);
        gst_caps_unref (scaps);

        empty = gst_caps_is_empty (intersect);
        gst_caps_unref (intersect);

        /* check if the intersection is empty */
        if (!empty) {
          /* non empty intersection, we can use this element */
          goto found;
        }
      }
    }
  }
  return FALSE;

found:
  return TRUE;
}

/* function used to sort element features */
static gint
compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;
  return strcmp (gst_plugin_feature_get_name (f2),
      gst_plugin_feature_get_name (f1));
}

/* function called by the internal decoder elements when it outputs
 * a buffer. We use it to get the first timestamp of the stream 
 */
static GstFlowReturn
gst_ogg_pad_internal_chain (GstPad * pad, GstBuffer * buffer)
{
  GstOggPad *oggpad;
  GstOggDemux *ogg;
  GstClockTime timestamp;

  oggpad = gst_pad_get_element_private (pad);
  ogg = GST_OGG_DEMUX (oggpad->ogg);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  GST_DEBUG_OBJECT (oggpad, "received buffer from iternal pad, TS=%lld",
      timestamp);

  if (oggpad->start_time == GST_CLOCK_TIME_NONE) {
    oggpad->start_time = timestamp;
    ogg->current_time = timestamp;
  }

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

/* runs typefind on the packet, which is assumed to be the first
 * packet in the stream.
 * 
 * Based on the type returned from the typefind function, an element
 * is created to help in conversion between granulepos and timestamps
 * so that we can do decent seeking.
 */
static gboolean
gst_ogg_pad_typefind (GstOggPad * pad, ogg_packet * packet)
{
  GstCaps *caps;
  GstElement *element = NULL;
  GstOggDemux *ogg = pad->ogg;

  if (GST_PAD_CAPS (pad) != NULL)
    return TRUE;

  caps = gst_ogg_type_find (packet);

  if (caps == NULL) {
    GST_WARNING_OBJECT (ogg,
        "couldn't find caps for stream with serial %08lx", pad->serialno);
    caps = gst_caps_new_simple ("application/octet-stream", NULL);
  } else {
    /* ogg requires you to use a decoder element to define the
     * meaning of granulepos etc so we make one. We also do this if
     * we are in the streaming mode to calculate the first timestamp. */
    GList *factories;

    /* first filter out the interesting element factories */
    factories = gst_default_registry_feature_filter (
        (GstPluginFeatureFilter) gst_ogg_demux_factory_filter, FALSE, caps);

    /* sort them according to their ranks */
    factories = g_list_sort (factories, (GCompareFunc) compare_ranks);

    /* then pick the first factory to create an element */
    if (factories) {
      element =
          gst_element_factory_create (GST_ELEMENT_FACTORY (factories->data),
          NULL);
      if (element) {
        /* this is ours */
        gst_object_ref (element);
        gst_object_sink (GST_OBJECT (element));

        /* FIXME, it might not be named "sink" */
        pad->elem_pad = gst_element_get_pad (element, "sink");
        gst_element_set_state (element, GST_STATE_PAUSED);
        pad->elem_out =
            gst_pad_new_from_template (gst_static_pad_template_get
            (&internaltemplate), "internal");
        gst_pad_set_chain_function (pad->elem_out, gst_ogg_pad_internal_chain);
        gst_pad_set_element_private (pad->elem_out, pad);
        gst_pad_set_active (pad->elem_out, TRUE);

        /* and this pad may not be named src.. */
        {
          GstPad *p;

          p = gst_element_get_pad (element, "src");
          gst_pad_link (p, pad->elem_out);
          gst_object_unref (p);
        }
      }
    }
    g_list_free (factories);
  }
  pad->element = element;

  gst_pad_set_caps (GST_PAD (pad), caps);
  gst_caps_unref (caps);

  return TRUE;
}

/* send packet to internal element */
static GstFlowReturn
gst_ogg_demux_chain_elem_pad (GstOggPad * pad, ogg_packet * packet)
{
  GstBuffer *buf;
  GstFlowReturn ret;
  GstOggDemux *ogg = pad->ogg;

  /* initialize our internal decoder with packets */
  if (!pad->elem_pad)
    goto no_decoder;

  GST_DEBUG_OBJECT (ogg, "%p init decoder serial %08lx", pad, pad->serialno);

  buf = gst_buffer_new_and_alloc (packet->bytes);
  memcpy (GST_BUFFER_DATA (buf), packet->packet, packet->bytes);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (pad));
  GST_BUFFER_OFFSET (buf) = -1;
  GST_BUFFER_OFFSET_END (buf) = packet->granulepos;

  ret = gst_pad_chain (pad->elem_pad, buf);
  if (GST_FLOW_IS_FATAL (ret))
    goto decoder_error;

  return ret;

no_decoder:
  {
    GST_WARNING_OBJECT (ogg,
        "pad %08lx does not have elem_pad, no decoder ?", pad);
    return GST_FLOW_ERROR;
  }
decoder_error:
  {
    GST_WARNING_OBJECT (ogg, "internal decoder error");
    return GST_FLOW_ERROR;
  }
}

/* queue data */
static GstFlowReturn
gst_ogg_demux_queue_data (GstOggPad * pad, ogg_packet * packet)
{
  GstBuffer *buf;
  GstOggDemux *ogg = pad->ogg;

  GST_DEBUG_OBJECT (ogg, "%p queueing data serial %08lx", pad, pad->serialno);

  buf = gst_buffer_new_and_alloc (packet->bytes);
  memcpy (buf->data, packet->packet, packet->bytes);
  GST_BUFFER_OFFSET (buf) = -1;
  GST_BUFFER_OFFSET_END (buf) = packet->granulepos;
  pad->headers = g_list_append (pad->headers, buf);

  /* we are ok now */
  return GST_FLOW_OK;
}

/* send packet to internal element */
static GstFlowReturn
gst_ogg_demux_chain_peer (GstOggPad * pad, ogg_packet * packet)
{
  GstBuffer *buf;
  GstFlowReturn ret;
  GstOggDemux *ogg = pad->ogg;

  ret = gst_pad_alloc_buffer (GST_PAD (pad), GST_BUFFER_OFFSET_NONE,
      packet->bytes, GST_PAD_CAPS (pad), &buf);

  GST_DEBUG_OBJECT (ogg,
      "%p streaming to peer serial %08lx", pad, pad->serialno);

  if (ret == GST_FLOW_OK) {
    memcpy (buf->data, packet->packet, packet->bytes);

    GST_BUFFER_OFFSET (buf) = -1;
    GST_BUFFER_OFFSET_END (buf) = packet->granulepos;

    ret = gst_pad_push (GST_PAD (pad), buf);
    if (ret == GST_FLOW_NOT_LINKED)
      ret = GST_FLOW_OK;

    if (packet->granulepos >= 0) {
      GstFormat format;

      ogg->current_granule = packet->granulepos;
      format = GST_FORMAT_TIME;
      if (!gst_pad_query_convert (pad->elem_pad,
              GST_FORMAT_DEFAULT, packet->granulepos, &format,
              (gint64 *) & ogg->current_time)) {
        g_warning ("could not convert granulepos to time");
      } else {
        GST_DEBUG ("ogg current time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (ogg->current_time));
      }
    }
  } else {
    GST_DEBUG_OBJECT (ogg,
        "%p could not get buffer from peer %08lx", pad, pad->serialno);

    if (ret == GST_FLOW_NOT_LINKED)
      ret = GST_FLOW_OK;
  }
  return ret;
}

/* submit a packet to the oggpad, this function will run the
 * typefind code for the pad if this is the first packet for this
 * stream 
 */
static GstFlowReturn
gst_ogg_pad_submit_packet (GstOggPad * pad, ogg_packet * packet)
{
  gint64 granule;
  GstFlowReturn ret;

  GstOggDemux *ogg = pad->ogg;

  GST_DEBUG_OBJECT (ogg, "%p submit packet serial %08lx", pad, pad->serialno);

  /* first packet */
  if (!pad->have_type) {
    gst_ogg_pad_typefind (pad, packet);
    pad->have_type = TRUE;
  }

  granule = packet->granulepos;
  if (granule != -1) {
    ogg->current_granule = granule;
    pad->current_granule = granule;
    if (pad->first_granule == -1 && granule != 0) {
      pad->first_granule = granule;
    }
  }

  /* no start time known, stream to internal plugin to
   * get time */
  if (pad->start_time == GST_CLOCK_TIME_NONE) {
    ret = gst_ogg_demux_chain_elem_pad (pad, packet);
  }
  /* we know the start_time of the pad data, see if we
   * can activate the complete chain if this is a dynamic
   * chain. */
  if (pad->start_time != GST_CLOCK_TIME_NONE) {
    GstOggChain *chain = pad->chain;

    /* check if complete chain has start time */
    if (chain == ogg->building_chain) {

      /* see if we have enough info to activate the chain */
      if (gst_ogg_demux_collect_chain_info (ogg, chain)) {
        GstEvent *event;
        GstClockTime segment_start, segment_stop;

        if (chain->begin_time != GST_CLOCK_TIME_NONE) {
          segment_start = chain->segment_start - chain->begin_time;
          segment_stop = chain->segment_stop - chain->begin_time;
        } else {
          segment_start = chain->segment_start;
          segment_stop = chain->segment_stop;
        }

        /* create the discont event we are going to send out */
        event = gst_event_new_newsegment (FALSE, ogg->segment_rate,
            GST_FORMAT_TIME, segment_start, segment_stop, 0);

        gst_ogg_demux_activate_chain (ogg, chain, event);

        ogg->building_chain = NULL;
      }
    }
  }

  /* if we are building a chain, store buffer for when we activate 
   * it. This path is taken if we operate in streaming mode. */
  if (ogg->building_chain) {
    ret = gst_ogg_demux_queue_data (pad, packet);
  }
  /* else we are completely streaming to the peer */
  else {
    ret = gst_ogg_demux_chain_peer (pad, packet);
  }
  return ret;
}

/* submit a page to an oggpad, this function will then submit all
 * the packets in the page.
 */
static GstFlowReturn
gst_ogg_pad_submit_page (GstOggPad * pad, ogg_page * page)
{
  ogg_packet packet;
  int ret;
  gboolean done = FALSE;
  GstFlowReturn result = GST_FLOW_OK;
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (GST_PAD_PARENT (pad));

  if (ogg_stream_pagein (&pad->stream, page) != 0)
    goto choked;

  while (!done) {
    ret = ogg_stream_packetout (&pad->stream, &packet);
    GST_LOG_OBJECT (ogg, "packetout gave %d", ret);
    switch (ret) {
      case 0:
        done = TRUE;
        break;
      case -1:
        /* out of sync, could call gst_ogg_pad_reset() here but ogg can decode
         * the packet just fine. We should probably send a DISCONT though. */
        break;
      case 1:
        result = gst_ogg_pad_submit_packet (pad, &packet);
        if (GST_FLOW_IS_FATAL (result))
          goto could_not_submit;
        break;
      default:
        GST_WARNING_OBJECT (ogg,
            "invalid return value %d for ogg_stream_packetout, resetting stream",
            ret);
        gst_ogg_pad_reset (pad);
        break;
    }
  }
  return result;

choked:
  {
    GST_WARNING_OBJECT (ogg,
        "ogg stream choked on page (serial %08lx), resetting stream",
        pad->serialno);
    gst_ogg_pad_reset (pad);
    /* we continue to recover */
    return GST_FLOW_OK;
  }
could_not_submit:
  {
    GST_WARNING_OBJECT (ogg, "could not submit packet, error: %d", result);
    gst_ogg_pad_reset (pad);
    return result;
  }
}


static GstOggChain *
gst_ogg_chain_new (GstOggDemux * ogg)
{
  GstOggChain *chain = g_new0 (GstOggChain, 1);

  GST_DEBUG_OBJECT (ogg, "creating new chain %p", chain);
  chain->ogg = ogg;
  chain->offset = -1;
  chain->bytes = -1;
  chain->have_bos = FALSE;
  chain->streams = g_array_new (FALSE, TRUE, sizeof (GstOggPad *));
  chain->begin_time = GST_CLOCK_TIME_NONE;
  chain->segment_start = GST_CLOCK_TIME_NONE;
  chain->total_time = GST_CLOCK_TIME_NONE;

  return chain;
}

static void
gst_ogg_chain_free (GstOggChain * chain)
{
  gint i;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    gst_object_unref (pad);
  }
  g_array_free (chain->streams, TRUE);
  chain->streams = NULL;
}

static GstOggPad *
gst_ogg_chain_new_stream (GstOggChain * chain, glong serialno)
{
  GstOggPad *ret;
  GstTagList *list;
  gchar *name;

  GST_DEBUG_OBJECT (chain->ogg, "creating new stream %08lx in chain %p",
      serialno, chain);

  ret = g_object_new (GST_TYPE_OGG_PAD, NULL);
  /* we own this one */
  gst_object_ref (ret);
  gst_object_sink (GST_OBJECT (ret));

  list = gst_tag_list_new ();
  name = g_strdup_printf ("serial_%08lx", serialno);

  GST_PAD_DIRECTION (ret) = GST_PAD_SRC;
  ret->chain = chain;
  ret->ogg = chain->ogg;
  gst_object_set_name (GST_OBJECT (ret), name);
  g_free (name);

  ret->serialno = serialno;
  if (ogg_stream_init (&ret->stream, serialno) != 0) {
    GST_ERROR ("Could not initialize ogg_stream struct for serial %08lx.",
        serialno);
    gst_object_unref (ret);
    return NULL;
  }
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_SERIAL, serialno,
      NULL);
  /* FIXME: either have it or remove it */
  //gst_element_found_tags (GST_ELEMENT (ogg), list);
  gst_tag_list_free (list);

  GST_DEBUG_OBJECT (chain->ogg,
      "created new ogg src %p for stream with serial %08lx", ret, serialno);

  g_array_append_val (chain->streams, ret);

  return ret;
}

static GstOggPad *
gst_ogg_chain_get_stream (GstOggChain * chain, glong serialno)
{
  gint i;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    if (pad->serialno == serialno)
      return pad;
  }
  return NULL;
}

static gboolean
gst_ogg_chain_has_stream (GstOggChain * chain, glong serialno)
{
  return gst_ogg_chain_get_stream (chain, serialno) != NULL;
}

#define CURRENT_CHAIN(ogg) (&g_array_index ((ogg)->chains, GstOggChain, (ogg)->current_chain))

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static GstStaticPadTemplate ogg_demux_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate ogg_demux_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/ogg")
    );

static void gst_ogg_demux_finalize (GObject * object);

//static const GstEventMask *gst_ogg_demux_get_event_masks (GstPad * pad);
//static const GstQueryType *gst_ogg_demux_get_query_types (GstPad * pad);
static GstOggChain *gst_ogg_demux_read_chain (GstOggDemux * ogg);
static gint gst_ogg_demux_read_end_chain (GstOggDemux * ogg,
    GstOggChain * chain);

static gboolean gst_ogg_demux_handle_event (GstPad * pad, GstEvent * event);
static void gst_ogg_demux_loop (GstOggPad * pad);
static GstFlowReturn gst_ogg_demux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_ogg_demux_sink_activate (GstPad * sinkpad);
static gboolean gst_ogg_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_ogg_demux_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static GstStateChangeReturn gst_ogg_demux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_ogg_demux_send_event (GstOggDemux * ogg, GstEvent * event);

static void gst_ogg_print (GstOggDemux * demux);

GST_BOILERPLATE (GstOggDemux, gst_ogg_demux, GstElement, GST_TYPE_ELEMENT);

static void
gst_ogg_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_ogg_demux_details =
      GST_ELEMENT_DETAILS ("ogg demuxer",
      "Codec/Demuxer",
      "demux ogg streams (info about ogg: http://xiph.org)",
      "Wim Taymand <wim@fluendo.com>");

  gst_element_class_set_details (element_class, &gst_ogg_demux_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_src_template_factory));
}
static void
gst_ogg_demux_class_init (GstOggDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gstelement_class->change_state = gst_ogg_demux_change_state;

  gobject_class->finalize = gst_ogg_demux_finalize;
}

static void
gst_ogg_demux_init (GstOggDemux * ogg, GstOggDemuxClass * g_class)
{
  /* create the sink pad */
  ogg->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&ogg_demux_sink_template_factory), "sink");
  gst_pad_set_event_function (ogg->sinkpad, gst_ogg_demux_handle_event);
  gst_pad_set_chain_function (ogg->sinkpad, gst_ogg_demux_chain);
  gst_pad_set_activate_function (ogg->sinkpad, gst_ogg_demux_sink_activate);
  gst_pad_set_activatepull_function (ogg->sinkpad,
      gst_ogg_demux_sink_activate_pull);
  gst_pad_set_activatepush_function (ogg->sinkpad,
      gst_ogg_demux_sink_activate_push);
  gst_element_add_pad (GST_ELEMENT (ogg), ogg->sinkpad);

  ogg->chain_lock = g_mutex_new ();
  ogg->chains = g_array_new (FALSE, TRUE, sizeof (GstOggChain *));

  ogg->current_granule = -1;
  ogg->current_time = 0;

  ogg->segment_rate = 1.0;
  ogg->segment_start = GST_CLOCK_TIME_NONE;
  ogg->segment_stop = GST_CLOCK_TIME_NONE;
  ogg->segment_play = FALSE;
  ogg->total_time = GST_CLOCK_TIME_NONE;

  ogg->running = FALSE;
}

static void
gst_ogg_demux_finalize (GObject * object)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (object);

  g_mutex_free (ogg->chain_lock);
  ogg_sync_clear (&ogg->sync);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ogg_demux_handle_event (GstPad * pad, GstEvent * event)
{
  GstOggDemux *ogg = GST_OGG_DEMUX (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      GST_DEBUG_OBJECT (ogg, "got a new segment event");
      ogg_sync_reset (&ogg->sync);
      gst_event_unref (event);
      break;
    default:
      return gst_pad_event_default (pad, event);
  }
  return TRUE;
}

/* submit the given buffer to the ogg sync.
 *
 * Returns the number of bytes submited.
 */
static gint
gst_ogg_demux_submit_buffer (GstOggDemux * ogg, GstBuffer * buffer)
{
  guint size;
  guint8 *data;
  gchar *oggbuffer;

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  oggbuffer = ogg_sync_buffer (&ogg->sync, size);
  memcpy (oggbuffer, data, size);
  ogg_sync_wrote (&ogg->sync, size);
  gst_buffer_unref (buffer);

  return size;
}

/* in random access mode this code updates the current read position
 * and resets the ogg sync buffer so that the next read will happen
 * from this new location.
 */
static void
gst_ogg_demux_seek (GstOggDemux * ogg, gint64 offset)
{
  GST_LOG_OBJECT (ogg, "seeking to %lld", offset);

  ogg->offset = offset;
  ogg_sync_reset (&ogg->sync);
}

/* read more data from the current offset and submit to
 * the ogg sync layer.
 *
 * Return number of bytes written.
 */
static gint
gst_ogg_demux_get_data (GstOggDemux * ogg)
{
  GstFlowReturn ret;
  GstBuffer *buffer;
  gint size;

  GST_LOG_OBJECT (ogg, "get data %lld %lld", ogg->offset, ogg->length);
  if (ogg->offset == ogg->length)
    return 0;

  ret = gst_pad_pull_range (ogg->sinkpad, ogg->offset, CHUNKSIZE, &buffer);
  if (ret != GST_FLOW_OK)
    return -1;

  size = gst_ogg_demux_submit_buffer (ogg, buffer);

  return size;
}

/* Read the next page from the current offset.
 */
static gint64
gst_ogg_demux_get_next_page (GstOggDemux * ogg, ogg_page * og, gint64 boundary)
{
  gint64 end_offset = 0;

  GST_LOG_OBJECT (ogg, "get next page %lld", boundary);

  if (boundary > 0)
    end_offset = ogg->offset + boundary;

  while (TRUE) {
    glong more;

    if (boundary > 0 && ogg->offset >= end_offset) {
      GST_LOG_OBJECT (ogg, "offset %lld >= end_offset %lld", ogg->offset,
          end_offset);
      return OV_FALSE;
    }

    more = ogg_sync_pageseek (&ogg->sync, og);

    if (more < 0) {
      GST_LOG_OBJECT (ogg, "skipped %ld bytes", more);
      /* skipped n bytes */
      ogg->offset -= more;
    } else if (more == 0) {
      gint ret;

      /* send more paramedics */
      if (boundary == 0)
        return OV_FALSE;

      ret = gst_ogg_demux_get_data (ogg);
      if (ret == 0)
        return OV_EOF;
      if (ret < 0)
        return OV_EREAD;
    } else {
      /* got a page.  Return the offset at the page beginning,
         advance the internal offset past the page end */
      gint64 ret = ogg->offset;

      ogg->offset += more;
      /* need to reset as we do not keep track of the bytes we
       * sent to the sync layer */
      ogg_sync_reset (&ogg->sync);

      GST_LOG_OBJECT (ogg,
          "got page at %lld, serial %08lx, end at %lld, granule %lld", ret,
          ogg_page_serialno (og), ogg->offset, ogg_page_granulepos (og));

      return ret;
    }
  }
}

/* from the current offset, find the previous page, seeking backwards
 * until we find the page. */
static gint
gst_ogg_demux_get_prev_page (GstOggDemux * ogg, ogg_page * og)
{
  gint64 begin = ogg->offset;
  gint64 end = begin;
  gint64 ret;
  gint64 offset = -1;

  while (offset == -1) {
    begin -= CHUNKSIZE;
    if (begin < 0)
      begin = 0;

    gst_ogg_demux_seek (ogg, begin);

    /* now continue reading until we run out of data, if we find a page
     * start, we save it. It might not be the final page as there could be
     * another page after this one. */
    while (ogg->offset < end) {
      ret = gst_ogg_demux_get_next_page (ogg, og, end - ogg->offset);
      if (ret == OV_EREAD)
        return OV_EREAD;
      if (ret < 0) {
        break;
      } else {
        offset = ret;
      }
    }
  }

  /* we have the offset.  Actually snork and hold the page now */
  gst_ogg_demux_seek (ogg, offset);
  ret = gst_ogg_demux_get_next_page (ogg, og, CHUNKSIZE);
  if (ret < 0)
    /* this shouldn't be possible */
    return OV_EFAULT;

  return offset;
}

static gboolean
gst_ogg_demux_deactivate_current_chain (GstOggDemux * ogg)
{
  gint i;
  GstOggChain *chain = ogg->current_chain;

  if (chain == NULL)
    return TRUE;

  /* send EOS on all the pads */
  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    gst_pad_push_event (GST_PAD (pad), gst_event_new_eos ());
    gst_element_remove_pad (GST_ELEMENT (ogg), GST_PAD (pad));
  }
  /* if we cannot seek back to the chain, we can destroy the chain 
   * completely */
  if (!ogg->seekable) {
    gst_ogg_chain_free (chain);
  }
  ogg->current_chain = NULL;

  return TRUE;
}

static gboolean
gst_ogg_demux_activate_chain (GstOggDemux * ogg, GstOggChain * chain,
    GstEvent * event)
{
  gint i;

  if (chain == ogg->current_chain) {
    if (event)
      gst_event_unref (event);
    return TRUE;
  }

  gst_ogg_demux_deactivate_current_chain (ogg);

  GST_DEBUG ("activating chain");

  /* first add the pads */
  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad;

    pad = g_array_index (chain->streams, GstOggPad *, i);
    gst_element_add_pad (GST_ELEMENT (ogg), GST_PAD (pad));
  }

  gst_element_no_more_pads (GST_ELEMENT (ogg));
  ogg->current_chain = chain;

  if (event)
    gst_ogg_demux_send_event (ogg, event);

  GST_DEBUG ("starting chain");

  /* then send out any queued buffers */
  for (i = 0; i < chain->streams->len; i++) {
    GList *headers;
    GstOggPad *pad;

    pad = g_array_index (chain->streams, GstOggPad *, i);

    for (headers = pad->headers; headers; headers = g_list_next (headers)) {
      GstBuffer *buffer = GST_BUFFER (headers->data);

      /* we don't care about the return value here */
      gst_pad_push (GST_PAD_CAST (pad), buffer);
    }
    /* and free the headers */
    g_list_free (pad->headers);
    pad->headers = NULL;
  }
  return TRUE;
}

static gboolean
gst_ogg_demux_perform_seek (GstOggDemux * ogg, gboolean accurate,
    gboolean flush)
{
  GstOggChain *chain = NULL;
  gint64 begin, end;
  gint64 begintime, endtime;
  gint64 target;
  gint64 best;
  gint64 total;
  gint64 result = 0;
  gint64 start, stop;
  gint i;

  /* first step is to unlock the streaming thread if it is
   * blocked in a chain call, we do this by starting the flush. because
   * we cannot yet hold any streaming lock, we have to protect the chains
   * with their own lock. */
  if (flush) {
    gint i;

    gst_pad_push_event (ogg->sinkpad, gst_event_new_flush_start ());

    GST_CHAIN_LOCK (ogg);
    for (i = 0; i < ogg->chains->len; i++) {
      GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);
      gint j;

      for (j = 0; j < chain->streams->len; j++) {
        GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, j);

        gst_pad_push_event (GST_PAD (pad), gst_event_new_flush_start ());
      }
    }
    GST_CHAIN_UNLOCK (ogg);
  } else {
    gst_pad_pause_task (ogg->sinkpad);
  }

  /* now grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever. */
  GST_STREAM_LOCK (ogg->sinkpad);

  GST_LOCK (ogg);
  /* nothing configured, play complete file */
  if (ogg->segment_start == GST_CLOCK_TIME_NONE)
    ogg->segment_start = 0;
  if (ogg->segment_stop == GST_CLOCK_TIME_NONE)
    ogg->segment_stop = ogg->total_time;

  ogg->segment_start = CLAMP (ogg->segment_start, 0, ogg->total_time);
  if (ogg->segment_stop != GST_CLOCK_TIME_NONE)
    ogg->segment_stop = CLAMP (ogg->segment_stop, 0, ogg->total_time);

  start = ogg->segment_start;
  stop = ogg->segment_stop;
  GST_UNLOCK (ogg);

  /* we need to stop flushing on the srcpad as we're going to use it
   * next. We can do this as we have the STREAM lock now. */
  gst_pad_push_event (ogg->sinkpad, gst_event_new_flush_stop ());

  {
    gint i;

    /* reset all ogg streams now, need to do this from within the lock to
     * make sure the streaming thread is not messing with the stream */
    for (i = 0; i < ogg->chains->len; i++) {
      GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);
      gint j;

      for (j = 0; j < chain->streams->len; j++) {
        GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, j);

        ogg_stream_reset (&pad->stream);
      }
    }
  }

  /* first find the chain to search in */
  total = ogg->total_time;
  if (ogg->chains->len == 0)
    goto no_chains;

  for (i = ogg->chains->len - 1; i >= 0; i--) {
    chain = g_array_index (ogg->chains, GstOggChain *, i);
    total -= chain->total_time;
    if (start >= total)
      break;
  }

  begin = chain->offset;
  end = chain->end_offset;
  begintime = chain->begin_time;
  endtime = chain->begin_time + chain->total_time;
  target = start - total + begintime;
  if (accurate) {
    /* FIXME, seek 4 seconds early to catch keyframes, better implement
     * keyframe detection. */
    target = target - (gint64) 4 *GST_SECOND;
  }
  target = MAX (target, 0);
  best = begin;

  GST_DEBUG_OBJECT (ogg,
      "seeking to %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT " in chain %p",
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop), chain);
  GST_DEBUG_OBJECT (ogg,
      "chain offset %" G_GINT64_FORMAT ", end offset %" G_GINT64_FORMAT, begin,
      end);
  GST_DEBUG_OBJECT (ogg,
      "chain begin time %" GST_TIME_FORMAT ", end time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (begintime), GST_TIME_ARGS (endtime));
  GST_DEBUG_OBJECT (ogg, "target %" GST_TIME_FORMAT, GST_TIME_ARGS (target));

  /* perform the seek */
  while (begin < end) {
    gint64 bisect;

    if ((end - begin < CHUNKSIZE) || (endtime == begintime)) {
      bisect = begin;
    } else {
      /* take a (pretty decent) guess, avoiding overflow */
      gint64 rate = (end - begin) * GST_MSECOND / (endtime - begintime);

      bisect = (target - begintime) / GST_MSECOND * rate + begin - CHUNKSIZE;

      if (bisect <= begin)
        bisect = begin;
    }
    gst_ogg_demux_seek (ogg, bisect);

    while (begin < end) {
      ogg_page og;

      GST_DEBUG_OBJECT (ogg,
          "after seek, bisect %" G_GINT64_FORMAT ", begin %" G_GINT64_FORMAT
          ", end %" G_GINT64_FORMAT, bisect, begin, end);

      result = gst_ogg_demux_get_next_page (ogg, &og, end - ogg->offset);
      if (result == OV_EREAD) {
        goto seek_error;
      }

      if (result < 0) {
        if (bisect <= begin + 1) {
          end = begin;          /* found it */
        } else {
          if (bisect == 0)
            goto seek_error;

          bisect -= CHUNKSIZE;
          if (bisect <= begin)
            bisect = begin + 1;

          gst_ogg_demux_seek (ogg, bisect);
        }
      } else {
        gint64 granulepos;
        GstClockTime granuletime;
        GstFormat format;
        GstOggPad *pad;

        granulepos = ogg_page_granulepos (&og);
        if (granulepos == -1)
          continue;

        pad = gst_ogg_chain_get_stream (chain, ogg_page_serialno (&og));
        if (pad == NULL)
          continue;

        format = GST_FORMAT_TIME;
        if (!gst_pad_query_convert (pad->elem_pad,
                GST_FORMAT_DEFAULT, granulepos, &format,
                (gint64 *) & granuletime)) {
          g_warning ("could not convert granulepos to time");
          granuletime = target;
        } else {
          granuletime -= pad->first_time;
        }

        GST_DEBUG_OBJECT (ogg,
            "found page with granule %" G_GINT64_FORMAT " and time %"
            GST_TIME_FORMAT, granulepos, GST_TIME_ARGS (granuletime));

        if (granuletime < target) {
          best = result;        /* raw offset of packet with granulepos */
          begin = ogg->offset;  /* raw offset of next page */
          begintime = granuletime;

          if (target - begintime > GST_SECOND)
            break;

          bisect = begin;       /* *not* begin + 1 */
        } else {
          if (bisect <= begin + 1) {
            end = begin;        /* found it */
          } else {
            if (end == ogg->offset) {   /* we're pretty close - we'd be stuck in */
              end = result;
              bisect -= CHUNKSIZE;      /* an endless loop otherwise. */
              if (bisect <= begin)
                bisect = begin + 1;
              gst_ogg_demux_seek (ogg, bisect);
            } else {
              end = result;
              endtime = granuletime;
              break;
            }
          }
        }
      }
    }
  }

  ogg->offset = best;

  /* current time starts from 0 again after a flush */
  if (flush)
    ogg->current_time = 0;

  /* now we have a new position, prepare for streaming again */
  {
    GstEvent *event;

    /* we have to send the flush to the old chain, not the new one */
    if (flush)
      gst_ogg_demux_send_event (ogg, gst_event_new_flush_stop ());

    /* create the discont event we are going to send out */
    event = gst_event_new_newsegment (FALSE, 1.0,
        GST_FORMAT_TIME,
        start + chain->segment_start, stop + chain->segment_start, start);

    if (chain != ogg->current_chain) {
      /* switch to different chain, send discont on new chain */
      gst_ogg_demux_activate_chain (ogg, chain, event);
    } else {
      /* send discont on old chain */
      gst_ogg_demux_send_event (ogg, event);
    }

    /* notify start of new segment */
    if (ogg->segment_play) {
      gst_element_post_message (GST_ELEMENT (ogg),
          gst_message_new_segment_start (GST_OBJECT (ogg), GST_FORMAT_TIME,
              ogg->segment_start));
    }
    /* restart our task since it might have been stopped when we did the 
     * flush. */
    gst_pad_start_task (ogg->sinkpad, (GstTaskFunction) gst_ogg_demux_loop,
        ogg->sinkpad);
  }

  /* streaming can continue now */
  GST_STREAM_UNLOCK (ogg->sinkpad);

  return TRUE;

no_chains:
  {
    GST_DEBUG_OBJECT (ogg, "no chains");
    GST_STREAM_UNLOCK (ogg->sinkpad);

    return FALSE;
  }
seek_error:
  {
    GST_DEBUG_OBJECT (ogg, "got a seek error");
    GST_STREAM_UNLOCK (ogg->sinkpad);

    return FALSE;
  }
}

/* finds each bitstream link one at a time using a bisection search
 * (has to begin by knowing the offset of the lb's initial page).
 * Recurses for each link so it can alloc the link storage after
 * finding them all, then unroll and fill the cache at the same time 
 */
static gint
gst_ogg_demux_bisect_forward_serialno (GstOggDemux * ogg,
    gint64 begin, gint64 searched, gint64 end, GstOggChain * chain, glong m)
{
  gint64 endsearched = end;
  gint64 next = end;
  ogg_page og;
  gint64 ret;
  GstOggChain *nextchain;

  GST_LOG_OBJECT (ogg,
      "bisect begin: %lld, searched: %lld, end %lld, chain: %p", begin,
      searched, end, chain);

  /* the below guards against garbage seperating the last and
   * first pages of two links. */
  while (searched < endsearched) {
    gint64 bisect;

    if (endsearched - searched < CHUNKSIZE) {
      bisect = searched;
    } else {
      bisect = (searched + endsearched) / 2;
    }

    gst_ogg_demux_seek (ogg, bisect);
    ret = gst_ogg_demux_get_next_page (ogg, &og, -1);
    if (ret == OV_EREAD) {
      GST_LOG_OBJECT (ogg, "OV_READ");
      return OV_EREAD;
    }

    if (ret < 0) {
      endsearched = bisect;
    } else {
      glong serial = ogg_page_serialno (&og);

      if (!gst_ogg_chain_has_stream (chain, serial)) {
        endsearched = bisect;
        next = ret;
      } else {
        searched = ret + og.header_len + og.body_len;
      }
    }
  }

  GST_LOG_OBJECT (ogg, "current chain ends at %lld", searched);

  chain->end_offset = searched;
  gst_ogg_demux_read_end_chain (ogg, chain);

  GST_LOG_OBJECT (ogg, "found begin at %lld", next);

  gst_ogg_demux_seek (ogg, next);
  nextchain = gst_ogg_demux_read_chain (ogg);

  if (searched < end && nextchain != NULL) {
    ret = gst_ogg_demux_bisect_forward_serialno (ogg, next, ogg->offset,
        end, nextchain, m + 1);

    if (ret == OV_EREAD) {
      GST_LOG_OBJECT (ogg, "OV_READ");
      return OV_EREAD;
    }
  }
  g_array_insert_val (ogg->chains, 0, chain);

  return 0;
}

/* read a chain from the ogg file. This code will
 * read all BOS pages and will create and return a GstOggChain 
 * structure with the results. 
 * 
 * This function will also read N pages from each stream in the
 * chain and submit them to the decoders. When the decoder has
 * decoded the first buffer, we know the timestamp of the first
 * page in the chain.
 */
static GstOggChain *
gst_ogg_demux_read_chain (GstOggDemux * ogg)
{
  GstOggChain *chain = NULL;
  gint64 offset = ogg->offset;
  ogg_page op;
  gboolean done;
  gint i;

  GST_LOG_OBJECT (ogg, "reading chain at %lld", offset);

  /* first read the BOS pages, do typefind on them, create
   * the decoders, send data to the decoders. */
  while (TRUE) {
    GstOggPad *pad;
    glong serial;
    gint ret;

    ret = gst_ogg_demux_get_next_page (ogg, &op, -1);
    if (ret < 0 || !ogg_page_bos (&op))
      break;

    if (chain == NULL) {
      chain = gst_ogg_chain_new (ogg);
      chain->offset = offset;
    }

    serial = ogg_page_serialno (&op);
    pad = gst_ogg_chain_new_stream (chain, serial);
    gst_ogg_pad_submit_page (pad, &op);
  }
  if (chain == NULL) {
    GST_WARNING_OBJECT (ogg, "failed to read chain");
    return NULL;
  }
  chain->have_bos = TRUE;
  GST_LOG_OBJECT (ogg, "read bos pages, init decoder now");

  /* now read pages until we receive a buffer from each of the
   * stream decoders, this will tell us the timestamp of the
   * first packet in the chain then */
  done = FALSE;
  while (!done) {
    glong serial;
    gint ret;

    serial = ogg_page_serialno (&op);
    done = TRUE;
    for (i = 0; i < chain->streams->len; i++) {
      GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

      if (pad->serialno == serial) {
        gst_ogg_pad_submit_page (pad, &op);
      }
      /* the timestamp will be filled in when we submit the pages */
      done &= (pad->start_time != GST_CLOCK_TIME_NONE);
      GST_LOG_OBJECT (ogg, "done %08lx now %d", serial, done);
    }

    if (!done) {
      ret = gst_ogg_demux_get_next_page (ogg, &op, -1);
      if (ret < 0)
        break;
    }
  }
  GST_LOG_OBJECT (ogg, "done reading chain");
  /* now we can fill in the missing info using queries */
  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);
    GstFormat target;

    target = GST_FORMAT_TIME;
    if (!gst_pad_query_convert (pad->elem_pad,
            GST_FORMAT_DEFAULT, pad->first_granule, &target,
            (gint64 *) & pad->first_time)) {
      g_warning ("could not convert granule to time");
    }

    pad->mode = GST_OGG_PAD_MODE_STREAMING;
  }
  return chain;
}

/* read the last pages from the ogg stream to get the final 
 * page end_offsets.
 */
static gint
gst_ogg_demux_read_end_chain (GstOggDemux * ogg, GstOggChain * chain)
{
  gint64 begin = chain->end_offset;
  gint64 end = begin;
  gint64 ret;
  gboolean done = FALSE;
  ogg_page og;
  gint i;

  while (!done) {
    begin -= CHUNKSIZE;
    if (begin < 0)
      begin = 0;

    gst_ogg_demux_seek (ogg, begin);

    /* now continue reading until we run out of data, if we find a page
     * start, we save it. It might not be the final page as there could be
     * another page after this one. */
    while (ogg->offset < end) {
      ret = gst_ogg_demux_get_next_page (ogg, &og, end - ogg->offset);
      if (ret == OV_EREAD)
        return OV_EREAD;
      if (ret < 0) {
        break;
      } else {
        done = TRUE;
        for (i = 0; i < chain->streams->len; i++) {
          GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

          if (pad->serialno == ogg_page_serialno (&og)) {
            gint64 granulepos = ogg_page_granulepos (&og);

            if (pad->last_granule < granulepos) {
              pad->last_granule = granulepos;
            }
          } else {
            done &= (pad->last_granule != -1);
          }
        }
      }
    }
  }
  /* now we can fill in the missing info using queries */
  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);
    GstFormat target;

    target = GST_FORMAT_TIME;
    if (!gst_pad_query_convert (pad->elem_pad,
            GST_FORMAT_DEFAULT, pad->last_granule, &target,
            (gint64 *) & pad->last_time)) {
      g_warning ("could not convert granule to time");
    }
  }
  return 0;
}

/* find a pad with a given serial number
 */
static GstOggPad *
gst_ogg_demux_find_pad (GstOggDemux * ogg, int serialno)
{
  GstOggPad *pad;
  gint i;

  /* first look in building chain if any */
  if (ogg->building_chain) {
    pad = gst_ogg_chain_get_stream (ogg->building_chain, serialno);
    if (pad)
      return pad;
  }

  /* then look in current chain if any */
  if (ogg->current_chain) {
    pad = gst_ogg_chain_get_stream (ogg->current_chain, serialno);
    if (pad)
      return pad;
  }

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    pad = gst_ogg_chain_get_stream (chain, serialno);
    if (pad)
      return pad;
  }
  return NULL;
}

/* find a chain with a given serial number
 */
static GstOggChain *
gst_ogg_demux_find_chain (GstOggDemux * ogg, int serialno)
{
  GstOggPad *pad;

  pad = gst_ogg_demux_find_pad (ogg, serialno);
  if (pad) {
    return pad->chain;
  }
  return NULL;
}

static gboolean
gst_ogg_demux_collect_chain_info (GstOggDemux * ogg, GstOggChain * chain)
{
  gint i;

  chain->total_time = 0;
  chain->segment_start = G_MAXINT64;
  chain->segment_stop = 0;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    /*  can do this if the pad start time is not defined */
    if (pad->start_time == GST_CLOCK_TIME_NONE)
      goto no_start_time;

    if (pad->last_time != GST_CLOCK_TIME_NONE) {
      pad->total_time = pad->last_time - pad->start_time;
      chain->total_time = MAX (chain->total_time, pad->total_time);
      chain->segment_stop = MAX (chain->segment_stop, pad->last_time);
    } else {
      pad->total_time = GST_CLOCK_TIME_NONE;
      chain->total_time = GST_CLOCK_TIME_NONE;
      chain->segment_stop = GST_CLOCK_TIME_NONE;
    }
    chain->segment_start = MIN (chain->segment_start, pad->start_time);
  }
  return TRUE;

  /* ERROR */
no_start_time:
  {
    chain->total_time = GST_CLOCK_TIME_NONE;
    chain->segment_start = GST_CLOCK_TIME_NONE;
    chain->segment_stop = GST_CLOCK_TIME_NONE;
    chain->begin_time = GST_CLOCK_TIME_NONE;
    return FALSE;
  }
}

static void
gst_ogg_demux_collect_info (GstOggDemux * ogg)
{
  gint i;

  /* collect all info */
  ogg->total_time = 0;

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    chain->begin_time = ogg->total_time;

    gst_ogg_demux_collect_chain_info (ogg, chain);

    ogg->total_time += chain->total_time;
  }
}

/* find all the chains in the ogg file, this reads the first and
 * last page of the ogg stream, if they match then the ogg file has
 * just one chain, else we do a binary search for all chains.
 */
static gboolean
gst_ogg_demux_find_chains (GstOggDemux * ogg)
{
  ogg_page og;
  GstPad *peer;
  GstFormat format;
  gboolean res;
  gulong serialno;
  GstOggChain *chain;

  /* get peer to figure out length */
  if ((peer = gst_pad_get_peer (ogg->sinkpad)) == NULL)
    goto no_peer;

  /* find length to read last page, we store this for later use. */
  format = GST_FORMAT_BYTES;
  res = gst_pad_query_position (peer, &format, NULL, &ogg->length);
  gst_object_unref (peer);
  if (!res)
    goto no_length;

  GST_DEBUG ("file length %lld", ogg->length);

  /* read chain from offset 0, this is the first chain of the
   * ogg file. */
  gst_ogg_demux_seek (ogg, 0);
  chain = gst_ogg_demux_read_chain (ogg);
  if (chain == NULL)
    goto no_first_chain;

  /* read page from end offset, we use this page to check if its serial
   * number is contained in the first chain. If this is the case then
   * this ogg is not a chained ogg and we can skip the scanning. */
  gst_ogg_demux_seek (ogg, ogg->length);
  gst_ogg_demux_get_prev_page (ogg, &og);
  serialno = ogg_page_serialno (&og);

  if (!gst_ogg_chain_has_stream (chain, serialno)) {
    /* the last page is not in the first stream, this means we should
     * find all the chains in this chained ogg. */
    gst_ogg_demux_bisect_forward_serialno (ogg, 0, 0, ogg->length, chain, 0);
  } else {
    /* we still call this function here but with an empty range so that
     * we can reuse the setup code in this routine. */
    gst_ogg_demux_bisect_forward_serialno (ogg, 0, ogg->length, ogg->length,
        chain, 0);
  }
  gst_ogg_demux_collect_info (ogg);

  /* dump our chains and streams */
  gst_ogg_print (ogg);

  return TRUE;

  /*** error cases ***/
no_peer:
  {
    GST_DEBUG ("we don't have a peer");
    return FALSE;
  }
no_length:
  {
    GST_DEBUG ("can't get file length");
    return FALSE;
  }
no_first_chain:
  {
    GST_DEBUG ("can't get first chain");
    return FALSE;
  }
}

/* streaming mode, receive a buffer, parse it, create pads for
 * the serialno, submit pages and packets to the oggpads
 */
static GstFlowReturn
gst_ogg_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstOggDemux *ogg;
  gint ret = -1;
  GstFlowReturn result = GST_FLOW_OK;
  guint serialno;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (pad));

  GST_DEBUG ("chain");
  gst_ogg_demux_submit_buffer (ogg, buffer);

  while (ret != 0 && result == GST_FLOW_OK) {
    ogg_page page;

    ret = ogg_sync_pageout (&ogg->sync, &page);
    if (ret == 0)
      /* need more data */
      break;
    if (ret == -1) {
      /* discontinuity in the pages */
    } else {
      GstOggPad *pad;
      gint64 granule;

      serialno = ogg_page_serialno (&page);
      granule = ogg_page_granulepos (&page);

      GST_LOG_OBJECT (ogg,
          "processing ogg page (serial %08lx, pageno %ld, granule pos %llu, bos %d)",
          serialno, ogg_page_pageno (&page), granule, ogg_page_bos (&page));

      if (ogg_page_bos (&page)) {
        GstOggChain *chain;

        /* first page */
        /* see if we know about the chain already */
        chain = gst_ogg_demux_find_chain (ogg, serialno);
        if (chain) {
          /* we do, activate it as it means we have a non-header */
          gst_ogg_demux_activate_chain (ogg, chain, NULL);
          pad = gst_ogg_demux_find_pad (ogg, serialno);
        } else {
          GstClockTime chain_time;
          GstOggChain *current_chain;

          /* this can only happen in non-seekabe mode */
          if (ogg->seekable)
            goto unknown_chain;

          current_chain = ogg->current_chain;
          if (current_chain) {
            GstClockTime duration;

            /* this was the duration of the previous chain */
            duration = ogg->current_time - current_chain->segment_start;
            /* the new chain time starts at duration + begin_time */
            chain_time = duration + current_chain->begin_time;

            /* remove existing pads */
            gst_ogg_demux_deactivate_current_chain (ogg);
          } else {
            /* non previous chain, start at configured current time */
            chain_time = ogg->current_time;
          }
          if (ogg->building_chain == NULL) {
            GstOggChain *newchain;

            newchain = gst_ogg_chain_new (ogg);
            newchain->offset = 0;
            /* set new chain begin time aligned with end time of old chain */
            newchain->begin_time = chain_time;
            GST_DEBUG ("new chain, begin time %" GST_TIME_FORMAT,
                GST_TIME_ARGS (chain_time));

            /* and this is the one we are building now */
            ogg->building_chain = newchain;
          }
          pad = gst_ogg_chain_new_stream (ogg->building_chain, serialno);
        }
      } else {
        pad = gst_ogg_demux_find_pad (ogg, serialno);
      }
      if (pad) {
        result = gst_ogg_pad_submit_page (pad, &page);
      } else {
        /* no pad, this is pretty fatal. This means an ogg page without bos
         * has been seen for this serialno. could just ignore it too... */
        goto unknown_pad;
      }
    }
  }

  return result;

unknown_chain:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DECODE,
        (NULL), ("unknown ogg chain for serial %08x detected", serialno));
    gst_ogg_demux_send_event (ogg, gst_event_new_eos ());
    return GST_FLOW_ERROR;
  }
unknown_pad:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DECODE,
        (NULL), ("unknown ogg pad for serial %08d detected", serialno));
    gst_ogg_demux_send_event (ogg, gst_event_new_eos ());
    return GST_FLOW_ERROR;
  }
}

static void
gst_ogg_demux_send_event (GstOggDemux * ogg, GstEvent * event)
{
  GstOggChain *chain = ogg->current_chain;

  if (chain) {
    gint i;

    for (i = 0; i < chain->streams->len; i++) {
      GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

      gst_event_ref (event);
      gst_pad_push_event (GST_PAD (pad), event);
    }
  }
  gst_event_unref (event);
}

/* random access code 
 *
 * - first find all the chains and streams by scanning the 
 *   file.
 * - then get and chain buffers, just like the streaming
 *   case.
 * - when seeking, we can use the chain info to perform the
 *   seek.
 */
static void
gst_ogg_demux_loop (GstOggPad * pad)
{
  GstOggDemux *ogg;
  GstFlowReturn ret;
  GstBuffer *buffer;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (pad));

  if (ogg->need_chains) {
    gboolean got_chains;

    /* this is the only place where we write chains */
    GST_CHAIN_LOCK (ogg);
    got_chains = gst_ogg_demux_find_chains (ogg);
    GST_CHAIN_UNLOCK (ogg);
    if (!got_chains)
      goto chain_read_failed;

    ogg->need_chains = FALSE;

    GST_LOCK (ogg);
    ogg->running = TRUE;
    GST_UNLOCK (ogg);

    /* and seek to configured positions without FLUSH */
    gst_ogg_demux_perform_seek (ogg, TRUE, FALSE);
  }

  GST_LOG_OBJECT (ogg, "pull data %lld", ogg->offset);
  if (ogg->offset == ogg->length) {
    ret = GST_FLOW_OK;
    /* segment playback just posts a segment end message instead of
     * pushing out EOS. */
    /* FIXME, need to be done somewhere else where we
     * can check against segment_stop time. */
    if (ogg->segment_play) {
      gst_element_post_message (GST_ELEMENT (ogg),
          gst_message_new_segment_done (GST_OBJECT (ogg), GST_FORMAT_TIME,
              ogg->total_time));
    } else {
      gst_ogg_demux_send_event (ogg, gst_event_new_eos ());
    }
    goto pause;
  }

  ret = gst_pad_pull_range (ogg->sinkpad, ogg->offset, CHUNKSIZE, &buffer);
  if (ret != GST_FLOW_OK)
    goto pause;

  ogg->offset += GST_BUFFER_SIZE (buffer);

  ret = gst_ogg_demux_chain (ogg->sinkpad, buffer);
  if (ret != GST_FLOW_OK)
    goto pause;

  return;

  /* ERRORS */
chain_read_failed:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DEMUX, (NULL), ("could not read chains"));
    ret = GST_FLOW_ERROR;
    goto pause;
  }
pause:
  {
    GST_LOG_OBJECT (ogg, "pausing task, reason %s", gst_flow_get_name (ret));
    gst_pad_pause_task (ogg->sinkpad);
    if (GST_FLOW_IS_FATAL (ret)) {
      gst_ogg_demux_send_event (ogg, gst_event_new_eos ());
      GST_ELEMENT_ERROR (ogg, STREAM, STOPPED,
          (NULL), ("stream stopped, reason %s", gst_flow_get_name (ret)));
    }
    return;
  }
}

static void
gst_ogg_demux_clear_chains (GstOggDemux * ogg)
{
  gint i;

  gst_ogg_demux_deactivate_current_chain (ogg);

  GST_CHAIN_LOCK (ogg);
  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    gst_ogg_chain_free (chain);
  }
  ogg->chains = g_array_set_size (ogg->chains, 0);
  GST_CHAIN_UNLOCK (ogg);
}

/* this function is called when the pad is activated and should start
 * processing data.
 *
 * We check if we can do random access to decide if we work push or
 * pull based.
 */
static gboolean
gst_ogg_demux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

/* this function gets called when we activate ourselves in push mode.
 * We cannot seek (ourselves) in the stream */
static gboolean
gst_ogg_demux_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (sinkpad));

  ogg->seekable = FALSE;

  return TRUE;
}

/* this function gets called when we activate ourselves in pull mode.
 * We can perform  random access to the resource and we start a task
 * to start reading */
static gboolean
gst_ogg_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (sinkpad));

  if (active) {
    ogg->need_chains = TRUE;
    ogg->seekable = TRUE;

    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_ogg_demux_loop,
        sinkpad);
  } else {
    return gst_pad_stop_task (sinkpad);
  }
}

static GstStateChangeReturn
gst_ogg_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstOggDemux *ogg;
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  ogg = GST_OGG_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ogg_sync_init (&ogg->sync);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ogg_sync_reset (&ogg->sync);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  result = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ogg_demux_clear_chains (ogg);
      GST_LOCK (ogg);
      ogg->running = FALSE;
      GST_UNLOCK (ogg);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      ogg_sync_clear (&ogg->sync);
      break;
    default:
      break;
  }
  return result;
}

/*** typefinding **************************************************************/
/* ogg supports its own typefinding because the ogg spec defines that the first
 * packet of an ogg stream must identify the stream. Therefore ogg can use a
 * simplified approach at typefinding.
 */
typedef struct
{
  ogg_packet *packet;
  guint best_probability;
  GstCaps *caps;
}
OggTypeFind;

static guint8 *
ogg_find_peek (gpointer data, gint64 offset, guint size)
{
  OggTypeFind *find = (OggTypeFind *) data;

  /* We don't support negative offset (from stream end); nothing embedded in ogg
   * ever needs them */
  if (offset >= 0 && offset + size <= find->packet->bytes) {
    return ((guint8 *) find->packet->packet) + offset;
  } else {
    return NULL;
  }
}
static void
ogg_find_suggest (gpointer data, guint probability, const GstCaps * caps)
{
  OggTypeFind *find = (OggTypeFind *) data;

  if (probability > find->best_probability) {
    gst_caps_replace (&find->caps, gst_caps_copy (caps));
    find->best_probability = probability;
  }
}
static GstCaps *
gst_ogg_type_find (ogg_packet * packet)
{
  GstTypeFind gst_find;
  OggTypeFind find;
  GList *walk, *type_list = NULL;

  walk = type_list = gst_type_find_factory_get_list ();

  find.packet = packet;
  find.best_probability = 0;
  find.caps = NULL;
  gst_find.data = &find;
  gst_find.peek = ogg_find_peek;
  gst_find.suggest = ogg_find_suggest;
  gst_find.get_length = NULL;

  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      break;
    walk = g_list_next (walk);
  }

  if (find.best_probability > 0)
    return find.caps;

  return NULL;
}

gboolean
gst_ogg_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogg_demux_debug, "oggdemux", 0, "ogg demuxer");
  GST_DEBUG_CATEGORY_INIT (gst_ogg_demux_setup_debug, "oggdemux_setup", 0,
      "ogg demuxer setup stage when parsing pipeline");

  return gst_element_register (plugin, "oggdemux", GST_RANK_PRIMARY,
      GST_TYPE_OGG_DEMUX);
}

/* prints all info about the element */
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_ogg_demux_setup_debug

#ifdef GST_DISABLE_GST_DEBUG

static void
gst_ogg_print (GstOggDemux * ogg)
{
  /* NOP */
}

#else /* !GST_DISABLE_GST_DEBUG */

static void
gst_ogg_print (GstOggDemux * ogg)
{
  guint j, i;

  GST_INFO_OBJECT (ogg, "%u chains", ogg->chains->len);
  GST_INFO_OBJECT (ogg, " total time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ogg->total_time));

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    GST_INFO_OBJECT (ogg, " chain %d (%u streams):", i, chain->streams->len);
    GST_INFO_OBJECT (ogg, "  offset: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT,
        chain->offset, chain->end_offset);
    GST_INFO_OBJECT (ogg, "  begin time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->begin_time));
    GST_INFO_OBJECT (ogg, "  total time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->total_time));
    GST_INFO_OBJECT (ogg, "  segment start: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->segment_start));
    GST_INFO_OBJECT (ogg, "  segment stop:  %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->segment_stop));

    for (j = 0; j < chain->streams->len; j++) {
      GstOggPad *stream = g_array_index (chain->streams, GstOggPad *, j);

      GST_INFO_OBJECT (ogg, "  stream %08lx:", stream->serialno);
      GST_INFO_OBJECT (ogg, "   start time:       %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->start_time));
      GST_INFO_OBJECT (ogg, "   first granulepos: %" G_GINT64_FORMAT,
          stream->first_granule);
      GST_INFO_OBJECT (ogg, "   first time:       %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->first_time));
      GST_INFO_OBJECT (ogg, "   last granulepos:  %" G_GINT64_FORMAT,
          stream->last_granule);
      GST_INFO_OBJECT (ogg, "   last time:        %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->last_time));
      GST_INFO_OBJECT (ogg, "   total time:       %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->total_time));
    }
  }
}
#endif /* GST_DISABLE_GST_DEBUG */
