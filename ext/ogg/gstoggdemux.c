/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstid3tagsetter.c: plugin for reading / modifying id3 tags
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
/* memcpy - if someone knows a way to get rid of it, please speak up */
#include <string.h> 

GST_DEBUG_CATEGORY_STATIC (gst_ogg_demux_debug);
#define GST_CAT_DEFAULT gst_ogg_demux_debug

#define GST_TYPE_OGG_DEMUX (gst_ogg_demux_get_type()) 
#define GST_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_OGG_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OGG_DEMUX, GstOggDemux))
#define GST_IS_OGG_DEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OGG_DEMUX))
#define GST_IS_OGG_DEMUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OGG_DEMUX))

typedef struct _GstOggDemux GstOggDemux;
typedef struct _GstOggDemuxClass GstOggDemuxClass;

typedef struct {
  GstPad *		pad; /* reference for this pad is held by element we belong to */

  int			serial;
  ogg_stream_state	stream;
  guint64		offset; /* end offset of last buffer */
} GstOggPad;

struct _GstOggDemux {
  GstElement		element;

  /* pads */
  GstPad *		sinkpad;
  GSList *		srcpads; /* list of GstOggPad */

  /* ogg stuff */
  ogg_sync_state	sync;
};

struct _GstOggDemuxClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_ogg_demux_details = GST_ELEMENT_DETAILS (
  "ogg demuxer",
  "Codec/Demuxer",
  "demux ogg streams (info about ogg: http://xiph.org)",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>"
);


/* signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static GstStaticPadTemplate ogg_demux_src_template_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate ogg_demux_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("application/ogg")
);


static void		gst_ogg_demux_base_init		(gpointer		g_class);
static void		gst_ogg_demux_class_init	(gpointer		g_class,
							 gpointer		class_data);
static void		gst_ogg_demux_init		(GTypeInstance *	instance,
							 gpointer		g_class);
static void		gst_ogg_demux_dispose		(GObject *		object);

static gboolean		gst_ogg_demux_src_event		(GstPad *		pad, 
							 GstEvent *		event);
static const GstEventMask* gst_ogg_demux_get_event_masks (GstPad *		pad);
static const GstQueryType* gst_ogg_demux_get_query_types (GstPad *		pad);

static gboolean		gst_ogg_demux_src_query		(GstPad *		pad,
							 GstQueryType		type,
							 GstFormat *		format, 
							 gint64 *		value);

static void		gst_ogg_demux_chain		(GstPad *		pad,
							 GstData *		buffer);

static GstElementStateReturn gst_ogg_demux_change_state	(GstElement *		element);

static GstOggPad *	gst_ogg_pad_new			(GstOggDemux *		ogg,
							 int			serial_no);
static void		gst_ogg_pad_remove		(GstOggDemux *		ogg,
							 GstOggPad *		ogg_pad);
static void		gst_ogg_pad_reset		(GstOggDemux *		ogg,
							 GstOggPad *		pad);
static void		gst_ogg_demux_push		(GstOggDemux *		ogg,
							 ogg_page *		page);
static void		gst_ogg_pad_push		(GstOggDemux *		ogg,
							 GstOggPad *		ogg_pad);

static GstCaps *	gst_ogg_type_find		(ogg_packet *		packet);
  
	
static GstElementClass *parent_class = NULL;
/* static guint gst_ogg_demux_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_ogg_demux_get_type (void)
{
  static GType ogg_demux_type = 0;

  if (!ogg_demux_type) {
    static const GTypeInfo ogg_demux_info = {
      sizeof (GstOggDemuxClass),
      gst_ogg_demux_base_init,
      NULL,
      gst_ogg_demux_class_init,
      NULL,
      NULL,
      sizeof (GstOggDemux),
      0,
      gst_ogg_demux_init,
    };
    
    ogg_demux_type = g_type_register_static(GST_TYPE_ELEMENT, "GstOggDemux", &ogg_demux_info, 0);
  }
  return ogg_demux_type;
}

static void
gst_ogg_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (element_class, &gst_ogg_demux_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_src_template_factory));
}
static void
gst_ogg_demux_class_init (gpointer g_class, gpointer class_data)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  
  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_ogg_demux_change_state;

  gobject_class->dispose = gst_ogg_demux_dispose;
}
static void
gst_ogg_demux_init (GTypeInstance *instance, gpointer g_class)
{
  GstOggDemux *ogg = GST_OGG_DEMUX (instance);
  
  /* create the sink pad */
  ogg->sinkpad = gst_pad_new_from_template(
      gst_static_pad_template_get (&ogg_demux_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (ogg), ogg->sinkpad);
  gst_pad_set_chain_function (ogg->sinkpad, GST_DEBUG_FUNCPTR (gst_ogg_demux_chain));

  /* initalize variables */
  ogg->srcpads = NULL; 
  
  GST_FLAG_SET (ogg, GST_ELEMENT_EVENT_AWARE);
}
static void
gst_ogg_demux_dispose (GObject *object)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (object);

  /* srcpads are removed when going to READY */
  g_assert (ogg->srcpads == NULL);
}

static const GstEventMask*
gst_ogg_demux_get_event_masks (GstPad *pad)
{
  static const GstEventMask gst_ogg_demux_src_event_masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET |
                      GST_SEEK_FLAG_FLUSH },
    { 0, }
  };
  return gst_ogg_demux_src_event_masks;
}
static const GstQueryType*
gst_ogg_demux_get_query_types (GstPad *pad)
{
  static const GstQueryType gst_ogg_demux_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };
  return gst_ogg_demux_src_query_types;
}

static gboolean
gst_ogg_demux_src_query (GstPad *pad, GstQueryType type,
		       GstFormat *format, gint64 *value)
{
  gboolean res = FALSE;
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL: {
      break;
    }
    case GST_QUERY_POSITION:
      break;
    default:
      break;
  }
  return res;
}

static gboolean
gst_ogg_demux_src_event (GstPad *pad, GstEvent *event)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      break;
    default:
      break;
  }

  gst_event_unref (event);
  return FALSE;
}
static void
gst_ogg_demux_handle_event (GstPad *pad, GstEvent *event)
{
  GstOggDemux *ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));
  
  ogg=ogg;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      break;
    case GST_EVENT_EOS:
      if (ogg->srcpads)
	GST_WARNING_OBJECT (ogg, "got EOS in unfinished ogg stream");
      while (ogg->srcpads) {
	gst_ogg_pad_remove (ogg, (GstOggPad *) ogg->srcpads->data);
      }
      gst_element_set_eos (GST_ELEMENT (ogg));
      break;
    default:
      gst_pad_event_default (pad, event);
      break;
  }
  return;
}
static void
gst_ogg_demux_chain (GstPad *pad, GstData *buffer)
{
  GstOggDemux *ogg;
  guint8 *data;
  int pageout_ret = 1;

  /* handle events */
  if (GST_IS_EVENT (buffer)) {
    gst_ogg_demux_handle_event (pad, GST_EVENT (buffer));
    return;
  }

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  data = (guint8 *) ogg_sync_buffer(&ogg->sync, GST_BUFFER_SIZE (buffer));
  memcpy (data, GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
  if (ogg_sync_wrote (&ogg->sync, GST_BUFFER_SIZE (buffer)) != 0) {
    gst_data_unref (buffer);
    gst_element_error (GST_ELEMENT (ogg), "ogg_sync_wrote failed");
    return;
  }
  gst_data_unref (buffer);
  while (pageout_ret != 0) {
    ogg_page page;
    
    pageout_ret = ogg_sync_pageout (&ogg->sync, &page);
    switch (pageout_ret) {
      case -1:
	/* FIXME: need some kind of discont here, we don't know any values */
	break;
      case 0:
	break;
      case 1:
	gst_ogg_demux_push (ogg, &page);
	break;
      default:
	GST_WARNING_OBJECT (ogg, "unknown return value %d from ogg_sync_pageout", pageout_ret);
	pageout_ret = 0;
	break;
    }
  }
  return;
}
static GstOggPad *
gst_ogg_pad_new (GstOggDemux *ogg, int serial)
{
  GstOggPad *ret = g_new (GstOggPad, 1);

  ret->serial = serial;
  if (ogg_stream_init (&ret->stream, serial) != 0) {
    GST_ERROR_OBJECT (ogg, "Could not initialize ogg_stream struct for serial %d.", serial);
    g_free (ret);
    return NULL;
  }
  ret->pad = NULL;
  ret->offset = 0;
  GST_LOG_OBJECT (ogg, "created new ogg src %p for stream with serial %d", ret, serial);

  return ret;
}
static void
gst_ogg_pad_remove (GstOggDemux *ogg, GstOggPad *pad)
{
  ogg->srcpads = g_slist_remove (ogg->srcpads, pad);
  if (pad->pad) {
    gst_pad_push (pad->pad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_remove_pad (GST_ELEMENT (ogg), pad->pad);
  }
  if (ogg_stream_clear (&pad->stream) != 0)
    GST_ERROR_OBJECT (ogg, "ogg_stream_clear (serial %d) did not return 0, ignoring this error", pad->serial);
  GST_LOG_OBJECT (ogg, "free ogg src %p for stream with serial %d", pad, pad->serial);
  g_free (pad);
}
static void
gst_ogg_demux_push (GstOggDemux *ogg, ogg_page* page)
{
  GSList *walk;
  GstOggPad *cur;

  /* find the stream */
  walk = ogg->srcpads;
  while (walk) {
    cur = (GstOggPad *) walk->data;
    if (cur->serial == ogg_page_serialno (page)) {
      goto br;
    }
    walk = g_slist_next (walk);
  }
  cur = NULL;
br:
  /* now we either have a stream (cur) or not */
  if (ogg_page_bos (page)) {
    if (cur) {
      /* huh? */
      GST_WARNING_OBJECT (ogg, "Ignoring error: ogg page declared as BOS while stream already existed.");
    } else {
      /* FIXME: monitor if we are still in creation stage? */
      cur = gst_ogg_pad_new (ogg, ogg_page_serialno (page));
      if (!cur) {
	gst_element_error (GST_ELEMENT (ogg), "Creating ogg_stream struct failed.");
      }
      ogg->srcpads = g_slist_prepend (ogg->srcpads, cur);
    }
  }
  if (cur == NULL) {
    gst_element_error (GST_ELEMENT (ogg), "invalid ogg stream serial no");
    return;
  }
  if (ogg_stream_pagein (&cur->stream, page) != 0) {
    GST_WARNING_OBJECT (ogg, "ogg stream choked on page (serial %d), resetting stream", cur->serial);
    gst_ogg_pad_reset (ogg, cur);
    return;
  }
  gst_ogg_pad_push (ogg, cur);
#if 0
  /* Removing pads while PLAYING doesn't work with current schedulers */
  /* remove from list, as this will never be called again */
  if (ogg_page_eos (page)) {
    gst_ogg_pad_remove (ogg, cur);
  }
#endif
}
static void
gst_ogg_pad_push (GstOggDemux *ogg, GstOggPad *pad)
{
  ogg_packet packet;
  int ret;
  GstBuffer *buf;

  while (TRUE) {
    ret = ogg_stream_packetout (&pad->stream, &packet);
    switch (ret) {
      case 0:
	return;
      case -1:
	gst_ogg_pad_reset (ogg, pad);
	break;
      case 1: {
	if (!pad->pad) {
	  GstCaps *caps = gst_ogg_type_find (&packet);
	  gchar *name = g_strdup_printf ("serial %d", pad->serial);
	  
	  if (caps == NULL) {
	    gst_element_error (GST_ELEMENT (ogg),
		"couldn't determine stream type from media");
	    return;
	  }
	  pad->pad = gst_pad_new_from_template (
	      gst_static_pad_template_get (&ogg_demux_src_template_factory),
	      name);
	  g_free (name);
	  gst_pad_set_event_function (pad->pad, GST_DEBUG_FUNCPTR (gst_ogg_demux_src_event));
	  gst_pad_set_event_mask_function (pad->pad, GST_DEBUG_FUNCPTR (gst_ogg_demux_get_event_masks));
	  gst_pad_set_query_function (pad->pad, GST_DEBUG_FUNCPTR (gst_ogg_demux_src_query));
	  gst_pad_set_query_type_function (pad->pad, GST_DEBUG_FUNCPTR (gst_ogg_demux_get_query_types));
	  gst_pad_use_explicit_caps (pad->pad);
	  gst_pad_set_explicit_caps (pad->pad, caps);
	  gst_pad_set_active (pad->pad, TRUE);
	  gst_element_add_pad (GST_ELEMENT (ogg), pad->pad);
	}
	/* optimization: use a bufferpool containing the ogg packet */
	buf = gst_buffer_new_and_alloc (packet.bytes);
	memcpy (buf->data, packet.packet, packet.bytes);
	GST_BUFFER_OFFSET (buf) = pad->offset;
	pad->offset = GST_BUFFER_OFFSET_END (buf) = packet.granulepos;
	gst_pad_push (pad->pad, GST_DATA (buf));
	break;
      }
      default:
	GST_ERROR_OBJECT (ogg, "invalid return value %d for ogg_stream_packetout, resetting stream", ret);
	gst_ogg_pad_reset (ogg, pad);
	break;
    }
  }
}
static void
gst_ogg_pad_reset (GstOggDemux *ogg, GstOggPad *pad)
{
  ogg_stream_reset (&pad->stream);
  pad->offset = GST_BUFFER_OFFSET_NONE;
  /* FIXME: need a discont here */
}
static GstElementStateReturn
gst_ogg_demux_change_state (GstElement *element)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      ogg_sync_init (&ogg->sync);
      break;
    case GST_STATE_READY_TO_PAUSED:
      ogg_sync_reset (&ogg->sync);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      while (ogg->srcpads) {
	gst_ogg_pad_remove (ogg, (GstOggPad *) ogg->srcpads->data);
      }
      break;
    case GST_STATE_READY_TO_NULL:
      ogg_sync_clear (&ogg->sync);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return parent_class->change_state (element);
}

/*** typefinding **************************************************************/
/* ogg supports its own typefinding because the ogg spec defines that the first
 * packet of an ogg stream must identify the stream. Therefore ogg can use a
 * simplified approach at typefinding.
 */
typedef struct {
  ogg_packet *	packet;
  guint		best_probability;
  GstCaps *	caps;
} OggTypeFind;
static guint8 *
ogg_find_peek (gpointer data, gint64 offset, guint size)
{
  OggTypeFind *find = (OggTypeFind *) data;
  
  if (offset + size <= find->packet->bytes) {
    return ((guint8 *) find->packet->packet) + offset;
  } else {
    return NULL;
  }
}
static void
ogg_find_suggest (gpointer data, guint probability, const GstCaps *caps)
{
  OggTypeFind *find = (OggTypeFind *) data;
  
  if (probability > find->best_probability) {
    gst_caps_replace (&find->caps, gst_caps_copy (caps));
    find->best_probability = probability;
  }
}
static GstCaps *
gst_ogg_type_find (ogg_packet *packet)
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

static gboolean
plugin_init (GstPlugin *plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogg_demux_debug, "oggdemux", 0, "ogg demuxer");

  return gst_element_register (plugin, "oggdemux", GST_RANK_PRIMARY, GST_TYPE_OGG_DEMUX);
}
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ogg",
  "ogg stream manipulation (info about ogg: http://xiph.org)",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
