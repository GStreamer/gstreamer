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
#include <gst/gst.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <gst/bytestream/bytestream.h>

#define GST_TYPE_VORBISFILE \
  (vorbisfile_get_type())
#define GST_VORBISFILE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VORBISFILE,VorbisFile))
#define GST_VORBISFILE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VORBISFILE,VorbisFileClass))
#define GST_IS_VORBISFILE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VORBISFILE))
#define GST_IS_VORBISFILE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VORBISFILE))

typedef struct _VorbisFile VorbisFile;
typedef struct _VorbisFileClass VorbisFileClass;

struct _VorbisFile {
  GstElement element;

  GstPad *sinkpad,*srcpad;
  GstByteStream *bs;

  OggVorbis_File vf;
  gint current_section;

  gboolean restart;
  gboolean need_discont;
  gboolean get_metadata;		/* TRUE if we request metadata */
  gboolean have_metadata;	/* TRUE if we already have this */
  gboolean have_metadata_technical;	/* TRUE if we already have this */
  gboolean eos;
  gboolean seek_pending;
  gint64 seek_value;
  GstFormat seek_format;
  gboolean seek_accurate;

  gboolean may_eos;
  int16_t convsize;
  guint64 total_out;
  guint64 total_bytes;
  guint64 offset;

  int link;			/* stream id of logical bitstream */
  int streams;			/* number of streams in physical bitstream */
  int version;
  int channels;
  long samplerate;
  long bitrate;

  long bitrate_upper;
  long bitrate_nominal;
  long bitrate_lower;
  /* long bitrate_window; 	not set in 1.0 */

  GstCaps *metadata;
};

struct _VorbisFileClass {
  GstElementClass parent_class;

    /* signals */
    void (*have_metadata) (GstElement *element);
};

GType vorbisfile_get_type (void);

extern GstPadTemplate *gst_vorbisdec_src_template, *gst_vorbisdec_sink_template;

/* elementfactory information */
GstElementDetails vorbisfile_details = 
{
  "Ogg Vorbis decoder",
  "Codec/Audio/Decoder",
  "Decodes OGG Vorbis audio using the vorbisfile API",
  VERSION,
  "Monty <monty@xiph.org>, " 
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* VorbisFile signals and args */
enum
{
  /* FILL ME */
  SIGNAL_HAVE_METADATA,		/* FIXME: maybe separate out in
				    - descriptive
				    - technical
				      - easy
				      - hard (expensive)
				*/
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_METADATA,
  ARG_VENDOR,
  ARG_VERSION,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_STREAMS,
  ARG_LINK,
  ARG_BITRATE,
  ARG_BITRATE_UPPER,
  ARG_BITRATE_NOMINAL,
  ARG_BITRATE_LOWER,
  ARG_BITRATE_WINDOW,
};

static void
		gst_vorbisfile_class_init 	(VorbisFileClass *klass);
static void 	gst_vorbisfile_init 		(VorbisFile *vorbisfile);

static GstElementStateReturn
		gst_vorbisfile_change_state 	(GstElement *element);

static const 
GstFormat* 	gst_vorbisfile_get_formats 	(GstPad *pad);
static gboolean gst_vorbisfile_src_convert 	(GstPad *pad, 
		                                 GstFormat src_format, 
						 gint64 src_value,
		           			 GstFormat *dest_format, 
						 gint64 *dest_value);
static const GstPadQueryType*
		gst_vorbisfile_get_query_types 	(GstPad *pad);

static gboolean gst_vorbisfile_src_query 	(GstPad *pad, 
		                                 GstPadQueryType type,
		        	 		 GstFormat *format, 
						 gint64 *value);
static const 
GstEventMask*	gst_vorbisfile_get_event_masks 	(GstPad *pad);
static gboolean gst_vorbisfile_src_event 	(GstPad *pad, GstEvent *event);

static void 	gst_vorbisfile_get_property 	(GObject *object, 
		            			 guint prop_id, 
						 GValue *value, 
						 GParamSpec *pspec);
static void 	gst_vorbisfile_set_property 	(GObject *object, 
		            			 guint prop_id, 
						 const GValue *value, 
						 GParamSpec *pspec);

static void 	gst_vorbisfile_loop 		(GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_vorbisfile_signals[LAST_SIGNAL] = { 0 };

GType
vorbisfile_get_type (void)
{
  static GType vorbisfile_type = 0;

  if (!vorbisfile_type) {
    static const GTypeInfo vorbisfile_info = {
      sizeof (VorbisFileClass), NULL, NULL,
      (GClassInitFunc) gst_vorbisfile_class_init, NULL, NULL,
      sizeof (VorbisFile), 0,
      (GInstanceInitFunc) gst_vorbisfile_init,
    };

    vorbisfile_type = g_type_register_static (GST_TYPE_ELEMENT, "VorbisFile", 
		                              &vorbisfile_info, 0);
  }
  return vorbisfile_type;
}

static void
gst_vorbisfile_class_init (VorbisFileClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata",
                         GST_TYPE_CAPS, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_VENDOR,
    g_param_spec_string ("vendor", "Vendor", 
	                 "The vendor for this vorbis stream",
                         "", G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_VERSION,
    g_param_spec_int ("version", "Version", "The version",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_CHANNELS,
    g_param_spec_int ("channels", "Channels", "The number of channels",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_STREAMS,
    g_param_spec_int ("streams", "Streams", 
	              "Number of logical bitstreams in the physical bitstream",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_LINK,
    g_param_spec_int ("link", "Link", 
	              "The link id of the current logical bitstream",
                       -1, G_MAXINT, 0, G_PARAM_READWRITE));
   g_object_class_install_property (gobject_class, ARG_RATE,
    g_param_spec_int ("rate", "Rate", "The samplerate",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
    g_param_spec_int ("bitrate", "Bitrate", 
	              "(average) bitrate in bits per second",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
   g_object_class_install_property (gobject_class, ARG_BITRATE_UPPER,
    g_param_spec_int ("bitrate_upper", "bitrate_upper", "bitrate_upper",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE_NOMINAL,
    g_param_spec_int ("bitrate_nominal", "bitrate_nominal", "bitrate_nominal",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE_LOWER,
    g_param_spec_int ("bitrate_lower", "bitrate_lower", "bitrate_lower",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE_WINDOW,
    g_param_spec_int ("bitrate_window", "bitrate_window", "bitrate_window",
                       0, G_MAXINT, 0, G_PARAM_READABLE));

  gobject_class->get_property = gst_vorbisfile_get_property;
  gobject_class->set_property = gst_vorbisfile_set_property;

  gst_vorbisfile_signals[SIGNAL_HAVE_METADATA] =
    g_signal_new ("have_metadata", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (VorbisFileClass, have_metadata), 
		  NULL, NULL, 
		  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gstelement_class->change_state = gst_vorbisfile_change_state;
}

static void
gst_vorbisfile_init (VorbisFile * vorbisfile)
{
  vorbisfile->sinkpad = gst_pad_new_from_template (gst_vorbisdec_sink_template,
		                                   "sink");
  gst_element_add_pad (GST_ELEMENT (vorbisfile), vorbisfile->sinkpad);
  gst_pad_set_convert_function (vorbisfile->sinkpad, NULL);

  gst_element_set_loop_function (GST_ELEMENT (vorbisfile), gst_vorbisfile_loop);
  vorbisfile->srcpad = gst_pad_new_from_template (gst_vorbisdec_src_template, 
		                                  "src");
  gst_element_add_pad (GST_ELEMENT (vorbisfile), vorbisfile->srcpad);
  gst_pad_set_formats_function (vorbisfile->srcpad, gst_vorbisfile_get_formats);
  gst_pad_set_query_type_function (vorbisfile->srcpad, 
		                   gst_vorbisfile_get_query_types);
  gst_pad_set_query_function (vorbisfile->srcpad, gst_vorbisfile_src_query);
  gst_pad_set_event_mask_function (vorbisfile->srcpad, 
		                   gst_vorbisfile_get_event_masks);
  gst_pad_set_event_function (vorbisfile->srcpad, gst_vorbisfile_src_event);
  gst_pad_set_convert_function (vorbisfile->srcpad, gst_vorbisfile_src_convert);

  vorbisfile->convsize = 4096;
  vorbisfile->total_out = 0;
  vorbisfile->total_bytes = 0;
  vorbisfile->offset = 0;
  vorbisfile->seek_pending = 0;
  vorbisfile->need_discont = FALSE;
  vorbisfile->get_metadata = TRUE;
  vorbisfile->link = -1;	/* by default, ask for entire bitstream */
  vorbisfile->metadata = NULL;
}

/* the next four functions are the ov callbacks we provide to vorbisfile
 * which interface between GStreamer's handling of the data flow and
 * vorbis's needs */
static size_t
gst_vorbisfile_read (void *ptr, size_t size, size_t nmemb, void *datasource)
{
  guint32 got_bytes = 0;
  guint8 *data;
  size_t read_size = size * nmemb;

  VorbisFile *vorbisfile = GST_VORBISFILE (datasource);

  GST_DEBUG (0, "read %d", read_size);

  /* make sure we don't go to EOS */
  if (!vorbisfile->may_eos && vorbisfile->total_bytes && 
       vorbisfile->offset + read_size > vorbisfile->total_bytes) 
  {
    read_size = vorbisfile->total_bytes - vorbisfile->offset;
  }

  if (read_size == 0 || vorbisfile->eos)
    return 0;
  
  while (got_bytes == 0) {
    got_bytes = gst_bytestream_peek_bytes (vorbisfile->bs, &data, read_size);
    if (got_bytes < read_size) {
      GstEvent *event;
      guint32 avail;
    
      gst_bytestream_get_status (vorbisfile->bs, &avail, &event); 

      switch (GST_EVENT_TYPE (event)) {
	case GST_EVENT_EOS:
	  GST_DEBUG (0, "eos");
          vorbisfile->eos = TRUE;
          if (avail == 0) {
            gst_event_unref (event);
            return 0;
	  }
	  break;
	case GST_EVENT_DISCONTINUOUS:
	  GST_DEBUG (0, "discont");
	  vorbisfile->need_discont = TRUE;
	default:
          break;
      }
      gst_event_unref (event);
      if (avail > 0) 
        got_bytes = gst_bytestream_peek_bytes (vorbisfile->bs, &data, avail);
      else
	got_bytes = 0;
    }
  }

  memcpy (ptr, data, got_bytes);
  gst_bytestream_flush_fast (vorbisfile->bs, got_bytes);

  vorbisfile->offset += got_bytes;

  return got_bytes / size;
}

static int
gst_vorbisfile_seek (void *datasource, int64_t offset, int whence)
{
  VorbisFile *vorbisfile = GST_VORBISFILE (datasource);
  GstSeekType method;
  guint64 pending_offset = vorbisfile->offset;
  gboolean need_total = FALSE;


  if (!vorbisfile->vf.seekable) {
    return -1;
  }
  
  GST_DEBUG (0, "seek %lld %d", offset, whence);

  if (whence == SEEK_SET) {
    method = GST_SEEK_METHOD_SET;
    pending_offset = offset;
  }
  else if (whence == SEEK_CUR) {
    method = GST_SEEK_METHOD_CUR;
    pending_offset += offset;
  }
  else if (whence == SEEK_END) {
    method = GST_SEEK_METHOD_END;
    need_total = TRUE;
    pending_offset = vorbisfile->total_bytes - offset;
  }
  else 
    return -1;
  
  if (!gst_bytestream_seek (vorbisfile->bs, offset, method))
    return -1;

  vorbisfile->offset = pending_offset;
  if (need_total)
    vorbisfile->total_bytes = gst_bytestream_tell (vorbisfile->bs) + offset;

  return 0;
}

static int
gst_vorbisfile_close (void *datasource)
{
  GST_DEBUG (0, "close");
  return 0;
}

static long
gst_vorbisfile_tell (void *datasource)
{
  VorbisFile *vorbisfile = GST_VORBISFILE (datasource);
  long result;

  result = gst_bytestream_tell (vorbisfile->bs);

  GST_DEBUG (0, "tell %ld", result);

  return result;
}

ov_callbacks vorbisfile_ov_callbacks = 
{
  gst_vorbisfile_read,
  gst_vorbisfile_seek,
  gst_vorbisfile_close,
  gst_vorbisfile_tell,
};

/* retrieve the comment field and put tags in metadata GstCaps
 * returns TRUE if caps were still valid or could be set,
 * FALSE if they couldn't be read somehow */
static gboolean
gst_vorbisfile_get_metadata (VorbisFile *vorbisfile)
{
  OggVorbis_File *vf = &vorbisfile->vf;
  char **ptr;
  int link = vorbisfile->link;
  GstProps *props = NULL;
  GstPropsEntry *entry;
  gchar *name, *value;

  /* check if we need to update or not */
  if (vorbisfile->have_metadata)
    return TRUE;

  /* clear old one */
  if (vorbisfile->metadata) {
    gst_caps_unref (vorbisfile->metadata);
    vorbisfile->metadata = NULL;
  }

  ptr = ov_comment (vf, link)->user_comments;
  if (! (*ptr)) return FALSE;
  props = gst_props_empty_new ();

  while (*ptr) {
    value = strstr (*ptr, "=");
    if (value) {
      *value = 0;
      ++value;
      name = *ptr;
      entry = gst_props_entry_new (name, GST_PROPS_STRING_TYPE, value);
      gst_props_add_entry (props, (GstPropsEntry *) entry);
    }
    ++ptr;
  }

  vorbisfile->have_metadata = TRUE;
  vorbisfile->metadata = gst_caps_new ("vorbisfile_metadata",
		                       "application/x-gst-metadata",
		                       props);
  return TRUE;
}

/* retrieve techical metadata and update vorbisfile with it */
/* FIXME: write me */
/* FIXME: make this less more expensive; only update it if the
 * link id has changed */
static void
gst_vorbisfile_metadata_get_technical (VorbisFile *vorbisfile)
{
  OggVorbis_File *vf = &vorbisfile->vf;
  //vorbis_info *vi;
  //int link = vorbisfile->link;

  /* check if we need to update or not */
  if (vorbisfile->have_metadata_technical)
    return;

  /* FIXME: clear old one */

  /* get number of streams */
  vorbisfile->streams = ov_streams (vf);
}
static void
gst_vorbisfile_loop (GstElement *element)
{
  VorbisFile *vorbisfile = GST_VORBISFILE (element);
  OggVorbis_File *vf = &vorbisfile->vf;
  GstBuffer *outbuf;
  long ret;
  GstClockTime time;
  gint64 samples;

  /* this function needs to go first since you don't want to be messing
   * with an unset vf ;) */
  if (vorbisfile->restart) {
    vorbisfile->current_section = 0;
    vorbisfile->offset = 0;
    vorbisfile->total_bytes = 0;
    vorbisfile->may_eos = FALSE;
    vorbisfile->vf.seekable = gst_bytestream_seek (vorbisfile->bs, 0, 
		                                   GST_SEEK_METHOD_SET);

    /* open our custom vorbisfile data object with the callbacks we provide */
    if (ov_open_callbacks (vorbisfile, &vorbisfile->vf, NULL, 0, 
			   vorbisfile_ov_callbacks) < 0) {
      gst_element_error (element, "this is not a vorbis file");
      return;
    }
    vorbisfile->need_discont = TRUE;
    vorbisfile->restart = FALSE;
  }

  /* get relevant data if asked for */
  if (vorbisfile->get_metadata) {
    vorbis_info *vi;
    double time_total;
    ogg_int64_t raw_total;
    int link = vorbisfile->link;

    if (gst_vorbisfile_get_metadata (vorbisfile))
      g_object_notify (G_OBJECT (vorbisfile), "metadata");


    /* get stream metadata */
    /* FIXME: due to a compiler bug, the actual vorbis bitrate function 
     * doesn't return a good number ... */
    vorbisfile->bitrate = ov_bitrate (vf, link);
    if (vorbisfile->bitrate == OV_EINVAL)
      g_warning ("OV_EINVAL: vf not open\n");
    if (vorbisfile->bitrate == OV_FALSE)
      g_warning ("OV_FALSE: non-seekable and no bitrate values set\n");

    /* ... so we calculate it by hand, which should be done better
     * by avoiding headers and stuff */
     time_total = ov_time_total (vf, link);
    raw_total = ov_raw_total (vf, link);
    g_print ("total time: %f\n", time_total);
    g_print ("abr: %d\n", 
	     (gint) ((guint64) raw_total / (guint64) time_total) * 8);
    vi = ov_info (vf, link);
    vorbisfile->version = vi->version;
    vorbisfile->samplerate = vi->rate;
    vorbisfile->channels = vi->channels;
    vorbisfile->bitrate_upper = vi->bitrate_upper;
    vorbisfile->bitrate_nominal = vi->bitrate_nominal;
    vorbisfile->bitrate_lower = vi->bitrate_lower;
    g_print ("Bitstream is %d channel, %ld Hz, %d version, %ld bitrate\n", 
	     vi->channels, vi->rate, vi->version, vorbisfile->bitrate);
    g_print ("bitrate: %f\n", (double) vorbisfile->bitrate);
    /* fire the signal saying we have metadata */
    g_signal_emit (G_OBJECT(vorbisfile), 
		   gst_vorbisfile_signals[SIGNAL_HAVE_METADATA], 0);
    vorbisfile->get_metadata = FALSE;
  }
 
  if (vorbisfile->seek_pending) {
    /* get time to seek to in seconds */
    gdouble seek_to = (gdouble) vorbisfile->seek_value / GST_SECOND;

    switch (vorbisfile->seek_format) {
      case GST_FORMAT_TIME:
	if (vorbisfile->seek_accurate) {
          if (ov_time_seek (&vorbisfile->vf, seek_to) == 0) {
            vorbisfile->need_discont = TRUE;
          }
        }
	else {
          if (ov_time_seek_page (&vorbisfile->vf, seek_to) == 0) {
            vorbisfile->need_discont = TRUE;
          }
	}
	break;
      case GST_FORMAT_UNITS:
	if (vorbisfile->seek_accurate) {
          if (ov_pcm_seek (&vorbisfile->vf, seek_to) == 0) {
            vorbisfile->need_discont = TRUE;
          }
        }
	else {
          if (ov_pcm_seek_page (&vorbisfile->vf, seek_to) == 0) {
            vorbisfile->need_discont = TRUE;
          }
	}
	break;
      default:
	g_warning ("unknown seek method, implement me !");
	break;
    }
    vorbisfile->seek_pending = FALSE;
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) = g_malloc (4096);
  GST_BUFFER_SIZE (outbuf) = 4096;

  /* get current time for discont and buffer timestamp */
  time = (GstClockTime) (ov_time_tell (&vorbisfile->vf) * GST_SECOND);

  ret = ov_read (&vorbisfile->vf, 
		 GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf), 
		 (G_BYTE_ORDER == G_LITTLE_ENDIAN ? 0 : 1), 
		 sizeof (gint16), 1, &vorbisfile->current_section);

  if (vorbisfile->need_discont) {
    GstEvent *discont;

    vorbisfile->need_discont = FALSE;

    /* if the pad is not usable, don't push it out */
    if (GST_PAD_IS_USABLE (vorbisfile->srcpad)) {
      /* get stream stats */
      samples = (gint64) (ov_pcm_tell (&vorbisfile->vf));

      discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, 
		    			     GST_FORMAT_UNITS, samples, NULL); 

      gst_pad_push (vorbisfile->srcpad, GST_BUFFER (discont));
    }
  }

  if (ret == 0) {
    GST_DEBUG (0, "eos");
    /* send EOS event */
    /*FIXME: should we do this or not ?
    ov_clear (&vorbisfile->vf);*/
    vorbisfile->restart = TRUE;
    gst_buffer_unref (outbuf);
    /* if the pad is not usable, don't push it out */
    if (GST_PAD_IS_USABLE (vorbisfile->srcpad)) 
      gst_pad_push (vorbisfile->srcpad, 
		    GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (element);
    return;
  }
  else if (ret < 0) {
    g_warning ("vorbisfile: decoding error");
    gst_buffer_unref (outbuf);
  }
  else {
    GST_BUFFER_SIZE (outbuf) = ret;

    if (!GST_PAD_CAPS (vorbisfile->srcpad)) {
      vorbis_info *vi = ov_info (&vorbisfile->vf, -1);
      
      if (gst_pad_try_set_caps (vorbisfile->srcpad,
                   GST_CAPS_NEW ("vorbisdec_src",
                                   "audio/raw",    
                                     "format",     GST_PROPS_STRING ("int"),
                                     "law",        GST_PROPS_INT (0),
                                     "endianness", GST_PROPS_INT (G_BYTE_ORDER),
                                     "signed",     GST_PROPS_BOOLEAN (TRUE),
                                     "width",      GST_PROPS_INT (16),
                                     "depth",      GST_PROPS_INT (16),
                                     "rate",       GST_PROPS_INT (vi->rate),
                                     "channels",   GST_PROPS_INT (vi->channels)
                                )) <= 0) 
      {
        gst_buffer_unref (outbuf);
	gst_element_error (GST_ELEMENT (vorbisfile), 
			   "could not negotiate format");
	return;
      }
    }

    vorbisfile->may_eos = TRUE;

    GST_BUFFER_TIMESTAMP (outbuf) = time;

    if (!vorbisfile->vf.seekable) {
      vorbisfile->total_bytes += GST_BUFFER_SIZE (outbuf);
    }
  
    if (GST_PAD_IS_USABLE (vorbisfile->srcpad)) 
      gst_pad_push (vorbisfile->srcpad, outbuf);
    else
      gst_buffer_unref (outbuf);
  }
}

static const GstFormat*
gst_vorbisfile_get_formats (GstPad *pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_UNITS,
    0
  };
  return formats;
}

/* handles conversion of location offsets between two formats based on
 * src caps */
static gboolean
gst_vorbisfile_src_convert (GstPad *pad, 
		            GstFormat src_format, gint64 src_value,
		            GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  VorbisFile *vorbisfile; 
  vorbis_info *vi;
  
  vorbisfile = GST_VORBISFILE (gst_pad_get_parent (pad));

  vi = ov_info (&vorbisfile->vf, -1);
  bytes_per_sample = vi->channels * 2;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_UNITS:
          *dest_value = src_value / (vi->channels * 2);
          break;
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * vi->rate;

          if (byterate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / byterate;
          break;
        }
        default:
          res = FALSE;
      }
    case GST_FORMAT_UNITS:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
	  if (vi->rate == 0)
	    return FALSE;
	  *dest_value = src_value * GST_SECOND / vi->rate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
	  scale = bytes_per_sample;
        case GST_FORMAT_UNITS:
	  *dest_value = src_value * scale * vi->rate / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static const GstPadQueryType*
gst_vorbisfile_get_query_types (GstPad *pad)
{
  static const GstPadQueryType types[] = {
    GST_PAD_QUERY_TOTAL,
    GST_PAD_QUERY_POSITION,
    0
  };
  return types;
}

/* handles queries for location in the stream in the requested format */
static gboolean
gst_vorbisfile_src_query (GstPad *pad, GstPadQueryType type,
		          GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  VorbisFile *vorbisfile; 
  vorbis_info *vi;
  
  vorbisfile = GST_VORBISFILE (gst_pad_get_parent (pad));

  vi = ov_info (&vorbisfile->vf, -1);

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_UNITS:
          if (vorbisfile->vf.seekable)
	    *value = ov_pcm_total (&vorbisfile->vf, -1);
	  else
	    return FALSE;
	  break;
        case GST_FORMAT_BYTES:
          if (vorbisfile->vf.seekable)
	    *value = ov_pcm_total (&vorbisfile->vf, -1) * vi->channels * 2;
	  else
	    return FALSE;
	  break;
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          if (vorbisfile->vf.seekable)
	    *value = (gint64) (ov_time_total (&vorbisfile->vf, -1) * GST_SECOND);
	  else
	    return FALSE;
	  break;
	default:
          res = FALSE;
          break;
      }
      break;
    }
    case GST_PAD_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          if (vorbisfile->vf.seekable)
	    *value = (gint64) (ov_time_tell (&vorbisfile->vf) * GST_SECOND);
	  else
            *value = vorbisfile->total_bytes * GST_SECOND 
		                             / (vi->rate * vi->channels * 2);
	  break;
        case GST_FORMAT_BYTES:
          if (vorbisfile->vf.seekable)
	    *value = ov_pcm_tell (&vorbisfile->vf) * vi->channels * 2;
	  else
            *value = vorbisfile->total_bytes;
	  break;
        case GST_FORMAT_UNITS:
          if (vorbisfile->vf.seekable)
	    *value = ov_pcm_tell (&vorbisfile->vf);
	  else
            *value = vorbisfile->total_bytes / (vi->channels * 2);
	  break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static const GstEventMask*
gst_vorbisfile_get_event_masks (GstPad *pad)
{
  static const GstEventMask masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_ACCURATE },
    { 0, }
  };
  return masks;
}

/* handle events on src pad */
static gboolean
gst_vorbisfile_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  VorbisFile *vorbisfile; 
		  
  vorbisfile = GST_VORBISFILE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 offset;
      vorbis_info *vi;
  
      if (!vorbisfile->vf.seekable) {
	gst_event_unref (event);
        return FALSE;
      }

      offset = GST_EVENT_SEEK_OFFSET (event);

      switch (GST_EVENT_SEEK_FORMAT (event)) {
	case GST_FORMAT_TIME:
	  vorbisfile->seek_pending = TRUE;
	  vorbisfile->seek_value = offset;
	  vorbisfile->seek_format = GST_FORMAT_TIME;
	  vorbisfile->seek_accurate = GST_EVENT_SEEK_FLAGS (event) 
		                    & GST_SEEK_FLAG_ACCURATE;
	  break;
	case GST_FORMAT_BYTES:
          vi = ov_info (&vorbisfile->vf, -1);
	  if (vi->channels * 2 == 0) {
	    res = FALSE;
	    goto done; 
	  }
          offset /= vi->channels * 2;
	  /* fallthrough */
	case GST_FORMAT_UNITS:
	  vorbisfile->seek_pending = TRUE;
	  vorbisfile->seek_value = offset;
	  vorbisfile->seek_format = GST_FORMAT_UNITS;
	  vorbisfile->seek_accurate = GST_EVENT_SEEK_FLAGS (event) 
		                    & GST_SEEK_FLAG_ACCURATE;
	  break;
	default:
	  res = FALSE;
	  break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

done:
  gst_event_unref (event);
  return res;
}

static GstElementStateReturn
gst_vorbisfile_change_state (GstElement *element)
{
  VorbisFile *vorbisfile = GST_VORBISFILE (element);
  
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      vorbisfile->restart = TRUE;
      vorbisfile->bs = gst_bytestream_new (vorbisfile->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      vorbisfile->eos = FALSE;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      ov_clear (&vorbisfile->vf);
      gst_bytestream_destroy (vorbisfile->bs);
      break;
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return GST_STATE_SUCCESS;
}

static void
gst_vorbisfile_set_property (GObject *object, guint prop_id, 
		             const GValue *value, GParamSpec *pspec)
{
  VorbisFile *vorbisfile;
	      
  g_return_if_fail (GST_IS_VORBISFILE (object));

  vorbisfile = GST_VORBISFILE (object);

  switch (prop_id) {
    case ARG_METADATA:
      break;
    case ARG_LINK:
      vorbisfile->link = g_value_get_int (value);
      break;
    default:
      g_warning ("Unknown property id\n");
  }
}

static void 
gst_vorbisfile_get_property (GObject *object, guint prop_id, 
		             GValue *value, GParamSpec *pspec)
{
  VorbisFile *vorbisfile;
	      
  g_return_if_fail (GST_IS_VORBISFILE (object));

  vorbisfile = GST_VORBISFILE (object);

  /* FIXME: the reupdate could be expensive */
  switch (prop_id) {
    case ARG_METADATA:
      gst_vorbisfile_get_metadata (vorbisfile);
      g_value_set_boxed (value, vorbisfile->metadata);
      break;
    case ARG_VENDOR:
      break;
    case ARG_VERSION:
      break;
    case ARG_CHANNELS:
      break;
    case ARG_RATE:
      break;
    case ARG_STREAMS:
      gst_vorbisfile_metadata_get_technical (vorbisfile);
      g_value_set_int (value, vorbisfile->streams);
      break;
    case ARG_LINK:
      break;
    case ARG_BITRATE:
      break;
    case ARG_BITRATE_UPPER:
      break;
    case ARG_BITRATE_NOMINAL:
      break;
    case ARG_BITRATE_LOWER:
      break;
    case ARG_BITRATE_WINDOW:
      break;
    default:
      g_warning ("Unknown property id\n");
  }
}
