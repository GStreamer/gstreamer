/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>
#include "qtdemux.h"

/* elementfactory information */
static GstElementDetails 
gst_qtdemux_details = 
{
  "quicktime parser",
  "Parser/Video",
  "Parses a quicktime stream into audio and video substreams",
  VERSION,
  "A.Baguinski <artm@v2.nl>",
  "(C) 2002",
};

static GstCaps* quicktime_type_find (GstBuffer *buf, gpointer private);

/* typefactory for 'quicktime' */
static GstTypeDefinition quicktimedefinition = {
  "qtdemux_video/quicktime",
  "video/quicktime",
  ".mov",
  quicktime_type_find,
};

enum {
  LAST_SIGNAL
};

enum {
  ARG_0
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "qtdemux_sink",
     "video/quicktime",
     NULL
  )
);

/* 
 * so far i only support Photo Jpeg videos and no audio. 
 * after this one works ok, i'll see what's next.
 */
GST_PAD_TEMPLATE_FACTORY (src_video_templ,
  "video_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "qtdemux_src_video",
    "video/jpeg",
      "width", GST_PROPS_INT_RANGE (16, 4096),
      "height", GST_PROPS_INT_RANGE (16, 4096)
  )
);

static GstElementClass *parent_class = NULL;
/* 
 * contains correspondence between atom types and
 * GstQtpAtomType structures 
 */
static GHashTable * gst_qtp_type_registry; 

typedef struct {
  guint32 type;
  GstQtpAtomType atype;
} GstQtpTypePair;

static void gst_qtp_trak_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_tkhd_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_hdlr_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_stsd_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_stts_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_stsc_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_stsz_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_stco_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_mdhd_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);
static void gst_qtp_mdat_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter);

GstQtpTypePair gst_qtp_type_table[] = {
  { GST_MAKE_FOURCC('m','o','o','v'), {GST_QTP_CONTAINER_ATOM,NULL} },
  { GST_MAKE_FOURCC('t','r','a','k'), {GST_QTP_CONTAINER_ATOM,gst_qtp_trak_handler} },
  { GST_MAKE_FOURCC('e','d','t','s'), {GST_QTP_CONTAINER_ATOM,NULL} },
  { GST_MAKE_FOURCC('m','d','i','a'), {GST_QTP_CONTAINER_ATOM,NULL} },
  { GST_MAKE_FOURCC('m','i','n','f'), {GST_QTP_CONTAINER_ATOM,NULL} },
  { GST_MAKE_FOURCC('d','i','n','f'), {GST_QTP_CONTAINER_ATOM,NULL} },
  { GST_MAKE_FOURCC('s','t','b','l'), {GST_QTP_CONTAINER_ATOM,NULL} },
  { GST_MAKE_FOURCC('m','d','a','t'), {0,gst_qtp_mdat_handler} },
  { GST_MAKE_FOURCC('m','v','h','d'), {0,NULL} },
  { GST_MAKE_FOURCC('t','k','h','d'), {0,gst_qtp_tkhd_handler} },
  { GST_MAKE_FOURCC('e','l','s','t'), {0,NULL} },
  { GST_MAKE_FOURCC('m','d','h','d'), {0,gst_qtp_mdhd_handler} },
  { GST_MAKE_FOURCC('h','d','l','r'), {0,gst_qtp_hdlr_handler} },
  { GST_MAKE_FOURCC('v','m','h','d'), {0,NULL} },
  { GST_MAKE_FOURCC('d','r','e','f'), {0,NULL} },
  { GST_MAKE_FOURCC('s','t','t','s'), {0,gst_qtp_stts_handler} },
  { GST_MAKE_FOURCC('s','t','s','d'), {0,gst_qtp_stsd_handler} },
  { GST_MAKE_FOURCC('s','t','s','z'), {0,gst_qtp_stsz_handler} },
  { GST_MAKE_FOURCC('s','t','s','c'), {0,gst_qtp_stsc_handler} },
  { GST_MAKE_FOURCC('s','t','c','o'), {0,gst_qtp_stco_handler} }
};

#define GST_QTP_TYPE_CNT sizeof(gst_qtp_type_table)/sizeof(GstQtpTypePair)

static void gst_qtdemux_class_init (GstQTDemuxClass *klass);
static void gst_qtdemux_init (GstQTDemux *quicktime_demux);
static void gst_qtdemux_loop (GstElement *element);
static GstElementStateReturn gst_qtdemux_change_state (GstElement * element);
static gint gst_guint32_compare(gconstpointer _a, gconstpointer _b);

static GType
gst_qtdemux_get_type (void) 
{
  static GType qtdemux_type = 0;

  if (!qtdemux_type) {
    static const GTypeInfo qtdemux_info = {
      sizeof(GstQTDemuxClass), NULL, NULL,
      (GClassInitFunc)gst_qtdemux_class_init,
      NULL, NULL, sizeof(GstQTDemux), 0,
      (GInstanceInitFunc)gst_qtdemux_init,
    };
    qtdemux_type = g_type_register_static (GST_TYPE_ELEMENT, "GstQTDemux", &qtdemux_info, 0);
  }
  return qtdemux_type;
}

static void
gst_qtdemux_class_init (GstQTDemuxClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  int i;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_qtdemux_change_state;

  gst_qtp_type_registry = g_hash_table_new(g_int_hash,g_int_equal);
  for(i=0;i<GST_QTP_TYPE_CNT;i++) {
    g_hash_table_insert(gst_qtp_type_registry,&(gst_qtp_type_table[i].type),&(gst_qtp_type_table[i].atype));
  }
}

static GstElementStateReturn
gst_qtdemux_change_state (GstElement * element)
{
  GstQTDemux * qtdemux = GST_QTDEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      qtdemux->bs = gst_bytestream_new (qtdemux->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (qtdemux->bs);
      break;
    default:
      break;
  }
  parent_class->change_state (element);
  return GST_STATE_SUCCESS;
}

static void 
gst_qtdemux_init (GstQTDemux *qtdemux) 
{
  guint i;
		
  qtdemux->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_templ), "sink");
  gst_element_set_loop_function (GST_ELEMENT (qtdemux), gst_qtdemux_loop);
  gst_element_add_pad (GST_ELEMENT (qtdemux), qtdemux->sinkpad);

  for (i=0; i<GST_QTDEMUX_MAX_VIDEO_PADS; i++) 
	  qtdemux->video_pad[i] = NULL;
  qtdemux->num_video_pads = 0;

  qtdemux->bs_pos = 0;
  qtdemux->nested = NULL;
  qtdemux->nested_cnt = 0;
  qtdemux->tracks = NULL;
  qtdemux->samples = NULL;
}

static GstCaps*
quicktime_type_find (GstBuffer *buf,
	      gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);

  /* exactly like in the old version */
  if (!strncmp (&data[4], "wide", 4) ||
      !strncmp (&data[4], "moov", 4) ||
      !strncmp (&data[4], "mdat", 4))  {
    return gst_caps_new ("quicktime_type_find",
		         "video/quicktime", 
			 NULL);
  }
  return NULL;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  if (!gst_library_load("gstbytestream")) {
    gst_info("qtdemux: could not load support library 'gstbytestream'\n");
    return FALSE;
  }

  factory = gst_element_factory_new("qtdemux",GST_TYPE_QTDEMUX,
                                   &gst_qtdemux_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_templ));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_video_templ));

  type = gst_type_factory_new (&quicktimedefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "qtdemux",
  plugin_init
};

static gboolean
gst_qtdemux_handle_event (GstQTDemux * qtdemux)
{
  guint32 remaining;
  GstEvent * event;
  GstEventType type;

  gst_bytestream_get_status (qtdemux->bs,&remaining,&event);
  type = event? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_EOS:
      gst_pad_event_default (qtdemux->sinkpad,event);
      break;
    case GST_EVENT_DISCONTINUOUS:
      gst_bytestream_flush_fast (qtdemux->bs, remaining);
    default:
      gst_pad_event_default (qtdemux->sinkpad,event);
      break;
  }
  return TRUE;
}

static gboolean gst_qtp_read_bytes_atom_head(GstQTDemux * qtdemux,GstQtpAtom * atom);
static gboolean gst_qtp_skip(GstQTDemux * qtdemux, guint64 skip);
static gboolean gst_qtp_skip_atom(GstQTDemux * qtdemux, GstQtpAtom * atom);
static gboolean gst_qtp_skip_container(GstQTDemux * qtdemux, guint32 type);

/* new track emerges here */
static GstQtpTrack * track_to_be = NULL;

/* 
 * - gst_qtp_* functions together with gst_qtdemux_loop implement quicktime
 * parser.
 */

static void
gst_qtdemux_loop (GstElement *element)
{
  GstQTDemux * qtdemux = GST_QTDEMUX (element);
  GstQtpAtom atom;
  GstQtpAtomType * atom_type;

  /* ain't we out of the current container? */
  if (qtdemux->nested) {
    GstQtpAtom * current = (GstQtpAtom *) qtdemux->nested->data;
    while (current && (current->size!=0) && (current->start+current->size <= qtdemux->bs_pos)) {
      /* indeed we are! */
      qtdemux->nested = qtdemux->nested->next;
      qtdemux->nested_cnt--;
      /* if atom type has a handler call it with enter=FALSE (i.e. leave) */
      atom_type = g_hash_table_lookup (gst_qtp_type_registry,&(current->type));
      if (atom_type && atom_type->handler) 
	atom_type->handler(qtdemux,current,FALSE);
      free(current);
      current = qtdemux->nested?(GstQtpAtom *) qtdemux->nested->data:NULL;
    }
  }

  gst_qtp_read_bytes_atom_head(qtdemux,&atom);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtdemux_loop: atom(%c%c%c%c,%llu,%llu)\n",GST_FOURCC_TO_CHARSEQ(atom.type),atom.start,atom.size);

  atom_type = g_hash_table_lookup (gst_qtp_type_registry,&atom.type);
  if (!atom_type) {
    gst_qtp_skip_atom(qtdemux,&atom);
    return;
  }

  if (atom_type->flags & GST_QTP_CONTAINER_ATOM) {
    GstQtpAtom * new_atom;
    new_atom = malloc(sizeof(GstQtpAtom));
    memcpy(new_atom,&atom,sizeof(GstQtpAtom));
    qtdemux->nested_cnt++;
    qtdemux->nested = g_slist_prepend (qtdemux->nested, new_atom);
    if (atom_type->handler) 
      atom_type->handler(qtdemux,&atom,TRUE);
  } else {
    /* leaf atom */
    if (atom_type->handler)
      atom_type->handler(qtdemux,&atom,TRUE);
    /*
     * if there wasn't a handler - we skip the whole atom
     * if there was - ensure that next thing read will be after the atom
     * (handler isn't obligated to read anything)
     */
    gst_qtp_skip_atom(qtdemux,&atom);
    return;
  }
}

/* 
 * peeks an atom header,
 * advances qtdemux->bs_pos (cause bytestream won't tell)
 * flushes bytestream
 */
static gboolean
gst_qtp_read_bytes_atom_head(GstQTDemux * qtdemux,GstQtpAtom * atom)
{
  GstByteStream * bs = qtdemux->bs;
  GstQtpAtomMinHeader * amh = NULL;
  guint64 * esize=NULL;

  /* FIXME this can't be right, rewrite with _read */
  do { /* do ... while (event()) is necessary for bytestream events */
    if (!amh) {
      if ((amh = (GstQtpAtomMinHeader*) gst_bytestream_peek_bytes (bs, 8)))  {
	atom->size = GUINT32_FROM_BE(amh->size);
	atom->type = amh->type; /* don't need to turn this around magicly FIXME this can depend on endiannes */
	atom->start = qtdemux->bs_pos;
	gst_bytestream_flush (bs, 8);
	qtdemux->bs_pos += 8;
      }
    }
    if (amh) {
      if (atom->size == 1) { /* need to peek extended size field */
	if ((esize = (guint64*) gst_bytestream_peek_bytes (bs, 8))) {
	  atom->size = GUINT64_FROM_BE(*esize);
	  gst_bytestream_flush (bs, 8);
	  qtdemux->bs_pos += 8;
	  return TRUE;
	}
      } else {
	return TRUE;
      }
    }
  } while (gst_qtdemux_handle_event (qtdemux));
  return TRUE;
}

static void
gst_qtp_read_bytes(GstQTDemux * qtdemux, void * buffer, size_t size)
{
  void * data;
  GstByteStream * bs = qtdemux->bs;
  do {
    if ((data = gst_bytestream_peek_bytes (bs,size))) {
      memcpy(buffer,data,size);
      gst_bytestream_flush(bs,size);
      qtdemux->bs_pos += size;
      return;
    }
  } while (gst_qtdemux_handle_event (qtdemux));
}

static GstBuffer *
gst_qtp_read(GstQTDemux * qtdemux, size_t size)
{
  GstBuffer * buf;
  GstByteStream * bs = qtdemux->bs;
  do {
    if ((buf = gst_bytestream_read (bs,size))) {
      qtdemux->bs_pos += size;
      return buf;
    }
  } while (gst_qtdemux_handle_event (qtdemux));
  return NULL;
}

/*
 * skips some input (e.g. to ignore unknown atom)
 */
static gboolean
gst_qtp_skip(GstQTDemux * qtdemux, guint64 skip)
{
  GstByteStream * bs = qtdemux->bs;
  
  if (skip) {
    gst_bytestream_flush(bs,skip);
    qtdemux->bs_pos += skip;
  }
  return TRUE;
}

/* convenience function for skipping the given atom */
static gboolean
gst_qtp_skip_atom(GstQTDemux * qtdemux, GstQtpAtom * atom)
{
  if (qtdemux->bs_pos < atom->start + atom->size) {
    guint64 skip = atom->start + atom->size - qtdemux->bs_pos;
    return gst_qtp_skip(qtdemux,skip);
  } else 
    return FALSE;
}

/* skips the container with type 'type' if finds it in the nesting stack */
static gboolean
gst_qtp_skip_container(GstQTDemux * qtdemux, guint32 type)
{
  GSList * iter = qtdemux->nested;

  while (iter && ((GstQtpAtom*)(iter->data))->type != type)
    iter = iter->next;

  if (iter) 
    return gst_qtp_skip_atom(qtdemux,(GstQtpAtom*)iter->data);
  else
    return FALSE;
}

static gint 
gst_guint32_compare(gconstpointer a, gconstpointer b)
{
  if ((guint32*)a < (guint32*)b) 
    return -1;
  else if ((guint32*)a > (guint32*)b)
    return 1;
  else
    return 0;
}

static void 
gst_qtp_trak_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  if (enter) { /* enter trak */
    GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_trak_handler: enter\n");
    track_to_be = malloc(sizeof(GstQtpTrack));
    track_to_be->stsd = NULL;
    track_to_be->stts = NULL;
    track_to_be->stsc = NULL;
    track_to_be->stsz = NULL;
    track_to_be->stco = NULL;
    track_to_be->samples = NULL;
    track_to_be->pad = NULL;
  } else { /* leave trak */
    GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_trak_handler: leave\n");
    if (track_to_be) { /* if we didnt discard this track earlier */
      GstQtpStscRec * stsc;
      guint32 * stsz, * stco, offset;
      int chunk,sample,nchunks,nsamples,stsc_idx,nstsc;
      GstCaps * newcaps = NULL;

      /* process sample tables */

      /*
       * FIXME have to check which sample tables are present and which are not
       * and skip the track if there's not enough tables or set default values
       * if some optional tables are missing
       */

      /*
       * FIXME i assume that there's only one of each stsd record and stts
       * record in the tables that's not always true, this must be changed
       * later, as soon as i encounter qt file with bigger tables.
       */
      track_to_be->format = ((GstQtpStsdRec*)GST_BUFFER_DATA(track_to_be->stsd))->format;
      GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_trak_handler: format: %c%c%c%c\n",GST_FOURCC_TO_CHARSEQ(track_to_be->format));
      track_to_be->sample_duration = GUINT32_FROM_BE(((GstQtpSttsRec*)GST_BUFFER_DATA(track_to_be->stts))->duration);
      GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_trak_handler: sample duration: %d\n",track_to_be->sample_duration);
      /* 
       * depending on format we can decide to refuse this track all together 
       * if we don't know what for format that it. 
       */
      switch (track_to_be->format) {
	case GST_MAKE_FOURCC('j','p','e','g'):
	  track_to_be->pad = gst_pad_new_from_template(
	      GST_PAD_TEMPLATE_GET(src_video_templ),
	      g_strdup_printf("video_%02d",qtdemux->num_video_pads++));
	  newcaps = GST_CAPS_NEW(
	      "qtdemux_video_src",
	      "video/jpeg",
	      "width", GST_PROPS_INT(track_to_be->width),
	      "height", GST_PROPS_INT(track_to_be->height));
	  gst_pad_try_set_caps(track_to_be->pad,newcaps);
	  gst_element_add_pad(GST_ELEMENT(qtdemux),track_to_be->pad);
	  break;
      }

      /* 
       * now let's find all about individual samples and put them into samples
       * tree
       */
      if (!qtdemux->samples) { 
	qtdemux->samples = g_tree_new(gst_guint32_compare);
      }
      stsc = (GstQtpStscRec*)GST_BUFFER_DATA(track_to_be->stsc);
      stsz = (guint32*)GST_BUFFER_DATA(track_to_be->stsz);
      stco = (guint32*)GST_BUFFER_DATA(track_to_be->stco);
      nchunks = GST_BUFFER_SIZE(track_to_be->stco)/sizeof(guint32);
      nsamples = GST_BUFFER_SIZE(track_to_be->stsz)/sizeof(guint32);
      nstsc = GST_BUFFER_SIZE(track_to_be->stsc)/sizeof(GstQtpStscRec);

      track_to_be->samples = malloc(nsamples*sizeof(GstQtpSample));
      for(chunk=0,sample=0,stsc_idx=0;
	  chunk<nchunks;
	  chunk++) {
	int i;
	offset = GUINT32_FROM_BE(stco[chunk]);
	if (stsc_idx+1<nstsc && chunk+1==GUINT32_FROM_BE(stsc[stsc_idx+1].first_chunk)) {
	  stsc_idx++;
	}
	for(i=0;i<GUINT32_FROM_BE(stsc[stsc_idx].samples_per_chunk);i++,sample++) {
	  guint32 size = GUINT32_FROM_BE(stsz[sample]);
	  track_to_be->samples[sample].offset = offset;
	  track_to_be->samples[sample].size = size;
	  track_to_be->samples[sample].timestamp = sample*((1000000*track_to_be->sample_duration)/track_to_be->time_scale);
	  track_to_be->samples[sample].track = track_to_be;
	  g_tree_insert(qtdemux->samples,&(track_to_be->samples[sample].offset),&(track_to_be->samples[sample]));
	  offset += size;
	}
      }

      GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_trak_handler: trak added to the list\n");
      qtdemux->tracks = g_list_prepend(qtdemux->tracks,track_to_be);

      gst_buffer_unref(track_to_be->stsd);
      gst_buffer_unref(track_to_be->stts);
      gst_buffer_unref(track_to_be->stsc);
      gst_buffer_unref(track_to_be->stsz);
      gst_buffer_unref(track_to_be->stco);
      track_to_be = 0;
    }
  }
}

/* 
 * weird formats they apple guys are using 
 * weird conversion copied from openquicktime 
 * FIXME either it can be done more beautiful/fast way or this fixme has to go
 */
static float 
fixed32_to_float(guint32 fixed)
{
  unsigned char * data = (unsigned char*)&fixed;
  guint32 a, b, c, d;
  a = data[0];
  b = data[1];
  c = data[2];
  d = data[3];
  a = (a << 8) + b;
  b = (c << 8) + d;
  return (float)a + (float)b / 65536;
}

static void 
gst_qtp_tkhd_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  guint32 wh[2];
  /* if we get here track_to_be must be not NULL */
  g_assert(track_to_be);

  gst_qtp_skip(qtdemux,76); /* don't need those values */
  gst_qtp_read_bytes(qtdemux,wh,8);
  track_to_be->width = (guint32) fixed32_to_float(wh[0]);
  track_to_be->height = (guint32) fixed32_to_float(wh[1]);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_tkhd_handler: track dimmensions: %dx%d\n",track_to_be->width,track_to_be->height);
}

static void
gst_qtp_hdlr_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  guint32 a[3];

  gst_qtp_read_bytes(qtdemux,a,12);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_hdlr_handler: %c%c%c%c %c%c%c%c\n",GST_FOURCC_TO_CHARSEQ(a[1]),GST_FOURCC_TO_CHARSEQ(a[2]));
  if (a[1]==GST_MAKE_FOURCC('m','h','l','r') && a[2]!=GST_MAKE_FOURCC('v','i','d','e')) {
    GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_hdlr_handler: rejecting the track\n");
    /* forget about this track! */
    free(track_to_be);
    track_to_be = NULL;
    gst_qtp_skip_container(qtdemux,GST_MAKE_FOURCC('t','r','a','k'));
  }
  return;
}

static void
gst_qtp_stsd_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  guint32 a[2];
  gst_qtp_read_bytes(qtdemux,a,8);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_stsd_handler: %d entries in the table\n",GUINT32_FROM_BE(a[1]));
  /* just put the rest of the atom into sample description table */
  track_to_be->stsd = gst_qtp_read(qtdemux,atom->start + atom->size - qtdemux->bs_pos);
}

static void
gst_qtp_stts_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  guint32 a[2];
  gst_qtp_read_bytes(qtdemux,a,8);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_stts_handler: %d entries in the table\n",GUINT32_FROM_BE(a[1]));
  track_to_be->stts = gst_qtp_read(qtdemux,GUINT32_FROM_BE(a[1])*sizeof(GstQtpSttsRec));
}
static void
gst_qtp_stsc_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  guint32 a[2];
  gst_qtp_read_bytes(qtdemux,a,8);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_stsc_handler: %d entries in the table\n",GUINT32_FROM_BE(a[1]));
  track_to_be->stsc = gst_qtp_read(qtdemux,GUINT32_FROM_BE(a[1])*sizeof(GstQtpStscRec));
}

static void
gst_qtp_stsz_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  guint32 a[3];
  gst_qtp_read_bytes(qtdemux,a,12);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_stsz_handler: %d entries in the table\n",GUINT32_FROM_BE(a[2]));
  /* FIXME have to chech a[2], it contains size if all samples if they are the same size */
  track_to_be->stsz = gst_qtp_read(qtdemux,GUINT32_FROM_BE(a[2])*sizeof(guint32));
}

static void
gst_qtp_stco_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter)
{
  guint32 a[2];
  gst_qtp_read_bytes(qtdemux,a,8);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_stco_handler: %d entries in the table\n",GUINT32_FROM_BE(a[1]));
  track_to_be->stco = gst_qtp_read(qtdemux,GUINT32_FROM_BE(a[1])*sizeof(guint32));
}

static void
gst_qtp_mdhd_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter) 
{
  guint32 a[4];
  gst_qtp_read_bytes(qtdemux,a,16);
  track_to_be->time_scale = GUINT32_FROM_BE(a[3]);
  GST_INFO (GST_CAT_PLUGIN_INFO,"gst_qtp_mdhd_handler: time scale: %d\n",track_to_be->time_scale);
}

static gboolean
gst_qtp_traverse(gpointer poffs,gpointer value,gpointer data)
{
  GstQtpSample * sample = (GstQtpSample*)value;
  GstQTDemux * qtdemux = (GstQTDemux*)data;

  if (qtdemux->bs_pos < sample->offset) {
    gst_qtp_skip(qtdemux,sample->offset - qtdemux->bs_pos);
    if (sample->track->pad && GST_PAD_IS_CONNECTED(sample->track->pad)) {
      GstBuffer * buf;
      buf = gst_qtp_read(qtdemux,sample->size);
      GST_BUFFER_TIMESTAMP(buf) = sample->timestamp;
      gst_pad_push(sample->track->pad,buf);
    }
  }
  return FALSE; /* == keep going (TRUE to stop) */
}

static void
gst_qtp_mdat_handler (GstQTDemux * qtdemux,GstQtpAtom * atom,gboolean enter) 
{
  /* actually playing */
  g_tree_foreach(qtdemux->samples,gst_qtp_traverse,qtdemux);
}
