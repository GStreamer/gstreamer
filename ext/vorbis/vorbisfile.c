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
#include <libs/gst/bytestream/bytestream.h>

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
};

struct _VorbisFileClass {
  GstElementClass parent_class;
};

GType vorbisfile_get_type(void);

extern GstPadTemplate *dec_src_template, *dec_sink_template;

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
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_COMMENT,
  ARG_VENDOR,
  ARG_VERSION,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_BITRATE_UPPER,
  ARG_BITRATE_NOMINAL,
  ARG_BITRATE_LOWER,
  ARG_BITRATE_WINDOW,
};

static void 		gst_vorbisfile_class_init 	(VorbisFileClass *klass);
static void 		gst_vorbisfile_init 		(VorbisFile *vorbisfile);

static GstElementStateReturn
			gst_vorbisfile_change_state 	(GstElement *element);

static gboolean 	gst_vorbisfile_src_query 	(GstPad *pad, GstPadQueryType type,
		        	 			 GstFormat *format, gint64 *value);
static gboolean 	gst_vorbisfile_src_event 	(GstPad *pad, GstEvent *event);

static void 		gst_vorbisfile_get_property 	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static void 		gst_vorbisfile_loop 		(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_vorbisfile_signals[LAST_SIGNAL] = { 0 }; */

GType
vorbisfile_get_type (void)
{
  static GType vorbisfile_type = 0;

  if (!vorbisfile_type) {
    static const GTypeInfo vorbisfile_info = {
      sizeof (VorbisFileClass), 
      NULL,
      NULL,
      (GClassInitFunc) gst_vorbisfile_class_init,
      NULL,
      NULL,
      sizeof (VorbisFile),
      0,
      (GInstanceInitFunc) gst_vorbisfile_init,
    };

    vorbisfile_type = g_type_register_static (GST_TYPE_ELEMENT, "VorbisFile", &vorbisfile_info, 0);
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

  g_object_class_install_property (gobject_class, ARG_COMMENT,
    g_param_spec_string ("comment", "Comment", "The comment tags for this vorbis stream",
                         "", G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_VENDOR,
    g_param_spec_string ("vendor", "Vendor", "The vendor for this vorbis stream",
                         "", G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_VERSION,
    g_param_spec_int ("version", "Version", "The version",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_CHANNELS,
    g_param_spec_int ("channels", "Channels", "The number of channels",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_RATE,
    g_param_spec_int ("rate", "Rate", "The samplerate",
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

  gstelement_class->change_state = gst_vorbisfile_change_state;
}

static void
gst_vorbisfile_init (VorbisFile * vorbisfile)
{
  vorbisfile->sinkpad = gst_pad_new_from_template (dec_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (vorbisfile), vorbisfile->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (vorbisfile), gst_vorbisfile_loop);
  vorbisfile->srcpad = gst_pad_new_from_template (dec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (vorbisfile), vorbisfile->srcpad);
  gst_pad_set_query_function (vorbisfile->srcpad, gst_vorbisfile_src_query);
  gst_pad_set_event_function (vorbisfile->srcpad, gst_vorbisfile_src_event);

  vorbisfile->convsize = 4096;
  vorbisfile->total_out = 0;
  vorbisfile->total_bytes = 0;
  vorbisfile->offset = 0;
  vorbisfile->seek_pending = 0;
  vorbisfile->need_discont = FALSE;
}

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
            return 0;
	  }
	  break;
	case GST_EVENT_DISCONTINUOUS:
	  GST_DEBUG (0, "discont");
	  vorbisfile->need_discont = TRUE;
	default:
          break;
      }
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

static void
gst_vorbisfile_loop (GstElement *element)
{
  VorbisFile *vorbisfile = GST_VORBISFILE (element);
  GstBuffer *outbuf;
  long ret;

  if (vorbisfile->restart) {
    vorbisfile->current_section = 0;
    vorbisfile->offset = 0;
    vorbisfile->total_bytes = 0;
    vorbisfile->may_eos = FALSE;
    vorbisfile->vf.seekable = gst_bytestream_seek (vorbisfile->bs, 0, GST_SEEK_METHOD_SET);

    if (ov_open_callbacks (vorbisfile, &vorbisfile->vf, NULL, 0, vorbisfile_ov_callbacks) < 0) {
      gst_element_error (element, "this is not a vorbis file");
      return;
    }
    vorbisfile->need_discont = TRUE;
    vorbisfile->restart = FALSE;
  }

  if (vorbisfile->seek_pending) {
    switch (vorbisfile->seek_format) {
      case GST_FORMAT_TIME:
	if (vorbisfile->seek_accurate) {
          if (ov_time_seek (&vorbisfile->vf, (gdouble) vorbisfile->seek_value / GST_SECOND) == 0) {
            vorbisfile->need_discont = TRUE;
          }
        }
	else {
          if (ov_time_seek_page (&vorbisfile->vf, (gdouble) vorbisfile->seek_value / GST_SECOND) == 0) {
            vorbisfile->need_discont = TRUE;
          }
	}
	break;
      case GST_FORMAT_UNITS:
	if (vorbisfile->seek_accurate) {
          if (ov_pcm_seek (&vorbisfile->vf, (gdouble) vorbisfile->seek_value / GST_SECOND) == 0) {
            vorbisfile->need_discont = TRUE;
          }
        }
	else {
          if (ov_pcm_seek_page (&vorbisfile->vf, (gdouble) vorbisfile->seek_value / GST_SECOND) == 0) {
            vorbisfile->need_discont = TRUE;
          }
	}
	break;
      default:
	break;
    }
    vorbisfile->seek_pending = FALSE;
  }

  if (vorbisfile->need_discont) {
    GstEvent *discont;
    GstClockTime time;
    gint64 samples;

    /* get stream stats */
    time = (gint64) (ov_time_tell (&vorbisfile->vf) * GST_SECOND);
    samples = (gint64) (ov_pcm_tell (&vorbisfile->vf));

    discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, 
		    				  GST_FORMAT_UNITS, samples, NULL); 

    vorbisfile->need_discont = FALSE;
    gst_pad_push (vorbisfile->srcpad, GST_BUFFER (discont));
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) = g_malloc (4096);
  GST_BUFFER_SIZE (outbuf) = 4096;

  ret = ov_read (&vorbisfile->vf, GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf), 
		  0, 2, 1, &vorbisfile->current_section);

  if (ret == 0) {
    GST_DEBUG (0, "eos");
    //ov_clear (&vorbisfile->vf);
    vorbisfile->restart = TRUE;
    gst_pad_push (vorbisfile->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
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
      
      gst_pad_try_set_caps (vorbisfile->srcpad,
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
                                     ));
    }

    vorbisfile->may_eos = TRUE;
    if (vorbisfile->vf.seekable) {
      GST_BUFFER_TIMESTAMP (outbuf) = (gint64) (ov_time_tell (&vorbisfile->vf) * GST_SECOND);
    }
    else {
      GST_BUFFER_TIMESTAMP (outbuf) = 0;
    }
  
    gst_pad_push (vorbisfile->srcpad, outbuf);
  }
}

static gboolean
gst_vorbisfile_src_query (GstPad *pad, GstPadQueryType type,
		          GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  VorbisFile *vorbisfile; 
		  
  vorbisfile = GST_VORBISFILE (gst_pad_get_parent (pad));

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_UNITS:
	  *value = ov_pcm_total (&vorbisfile->vf, -1);
	  break;
        case GST_FORMAT_BYTES:
          res = FALSE;
	  break;
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
	  *value = (gint64) (ov_time_total (&vorbisfile->vf, -1) * GST_SECOND);
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
	  *value = (gint64) (ov_time_tell (&vorbisfile->vf) * GST_SECOND);
	  break;
        case GST_FORMAT_UNITS:
	  *value = ov_pcm_tell (&vorbisfile->vf);
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
		    
static gboolean
gst_vorbisfile_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  VorbisFile *vorbisfile; 
		  
  vorbisfile = GST_VORBISFILE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (!vorbisfile->vf.seekable)
        return FALSE;

      switch (GST_EVENT_SEEK_FORMAT (event)) {
	case GST_FORMAT_TIME:
	  vorbisfile->seek_pending = TRUE;
	  vorbisfile->seek_value = GST_EVENT_SEEK_OFFSET (event);
	  vorbisfile->seek_format = GST_FORMAT_TIME;
	  vorbisfile->seek_accurate = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_ACCURATE;
	  break;
	case GST_FORMAT_UNITS:
	  vorbisfile->seek_pending = TRUE;
	  vorbisfile->seek_value = GST_EVENT_SEEK_OFFSET (event);
	  vorbisfile->seek_format = GST_FORMAT_UNITS;
	  vorbisfile->seek_accurate = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_ACCURATE;
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
gst_vorbisfile_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  VorbisFile *vorbisfile;
	      
  g_return_if_fail (GST_IS_VORBISFILE (object));

  vorbisfile = GST_VORBISFILE (object);

  switch (prop_id) {
    case ARG_COMMENT:
      g_value_set_string (value, "comment");
      break;
    case ARG_VENDOR:
      break;
    case ARG_VERSION:
      break;
    case ARG_CHANNELS:
      break;
    case ARG_RATE:
      break;
    case ARG_BITRATE_UPPER:
      break;
    case ARG_BITRATE_NOMINAL:
      break;
    case ARG_BITRATE_LOWER:
      break;
    case ARG_BITRATE_WINDOW:
      break;
  }
}
