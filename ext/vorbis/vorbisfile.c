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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <gst/bytestream/bytestream.h>

GST_DEBUG_CATEGORY_STATIC (vorbisfile_debug);
#define GST_CAT_DEFAULT vorbisfile_debug

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
  GstElement 	 element;

  GstPad 	*sinkpad,
  		*srcpad;
  GstByteStream *bs;

  OggVorbis_File vf;
  gint 		 current_link;

  gboolean 	 restart;
  gboolean 	 need_discont;
  gboolean 	 eos;
  gboolean 	 seek_pending;
  gint64 	 seek_value;
  GstFormat 	 seek_format;
  gboolean 	 seek_accurate;

  gboolean 	 may_eos;
  guint64 	 total_bytes;
  guint64 	 offset;
  gulong	 blocksize;

  GstTagList	*streaminfo;
};

struct _VorbisFileClass {
  GstElementClass parent_class;

};

GType vorbisfile_get_type (void);

GstPadTemplate *gst_vorbisdec_src_template, *gst_vorbisdec_sink_template;

/* elementfactory information */
GstElementDetails vorbisfile_details =
{
  "Ogg Vorbis decoder",
  "Codec/Decoder/Audio",
  "Decodes OGG Vorbis audio using the vorbisfile API",
  "Monty <monty@xiph.org>, "
  "Wim Taymans <wim.taymans@chello.be>",
};

/* VorbisFile signals and args */
enum
{
  LAST_SIGNAL
};

#define DEFAULT_BLOCKSIZE 4096

enum
{
  ARG_0,
  ARG_BLOCKSIZE,
};

static void     gst_vorbisfile_base_init        (gpointer g_class);
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
static gboolean gst_vorbisfile_sink_convert 	(GstPad *pad, 
		            			 GstFormat src_format, 
						 gint64 src_value,
		            			 GstFormat *dest_format, 
						 gint64 *dest_value);
static const GstQueryType*
		gst_vorbisfile_get_query_types 	(GstPad *pad);

static gboolean gst_vorbisfile_src_query 	(GstPad *pad, 
		                                 GstQueryType type,
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
//static guint gst_vorbisfile_signals[LAST_SIGNAL] = { 0 };

static GstFormat logical_stream_format;

GType
vorbisfile_get_type (void)
{
  static GType vorbisfile_type = 0;

  if (!vorbisfile_type) {
    static const GTypeInfo vorbisfile_info = {
      sizeof (VorbisFileClass),
      gst_vorbisfile_base_init,
      NULL,
      (GClassInitFunc) gst_vorbisfile_class_init, NULL, NULL,
      sizeof (VorbisFile), 0,
      (GInstanceInitFunc) gst_vorbisfile_init,
    };

    vorbisfile_type = g_type_register_static (GST_TYPE_ELEMENT, "VorbisFile", 
		                              &vorbisfile_info, 0);

    logical_stream_format = gst_format_register ("logical_stream", "The logical stream");

    GST_DEBUG_CATEGORY_INIT (vorbisfile_debug, "vorbisfile", 0, "vorbis in ogg decoding element");
  }
  return vorbisfile_type;
}

static GstCaps*
vorbis_caps_factory (void)
{
  return gst_caps_new_simple ("application/ogg", NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return gst_caps_new_simple ("audio/x-raw-int",
      "endianness", 	G_TYPE_INT, G_BYTE_ORDER,
      "signed", 	G_TYPE_BOOLEAN, TRUE,
      "width", 		G_TYPE_INT, 16,
      "depth",    	G_TYPE_INT, 16,
      "rate",     	GST_TYPE_INT_RANGE, 11025, 48000,
      "channels", 	GST_TYPE_INT_RANGE, 1, 2,
      NULL);
}

static GstCaps*
raw_caps2_factory (void)
{
  return gst_caps_new_simple ("audio/x-raw-float",
      "width",		G_TYPE_INT, 32,
      "endianness",	G_TYPE_INT, G_BYTE_ORDER,
      "rate",     	GST_TYPE_INT_RANGE, 11025, 48000,
      "channels", 	GST_TYPE_INT_RANGE, 1, 2,
      "buffer-frames",  GST_TYPE_INT_RANGE, 1, G_MAXINT,
      NULL);
}

static void
gst_vorbisfile_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *vorbis_caps, *raw_caps2;

  raw_caps = raw_caps_factory ();
  raw_caps2 = raw_caps2_factory ();
  vorbis_caps = vorbis_caps_factory ();

  gst_vorbisdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                                      GST_PAD_ALWAYS, 
					              vorbis_caps);
  gst_caps_append (raw_caps2, raw_caps);
  gst_vorbisdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                                     GST_PAD_ALWAYS, 
					             raw_caps2);
  gst_element_class_add_pad_template (element_class, gst_vorbisdec_sink_template);
  gst_element_class_add_pad_template (element_class, gst_vorbisdec_src_template);
  gst_element_class_set_details (element_class, &vorbisfile_details);
}

static void
gst_vorbisfile_class_init (VorbisFileClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
    g_param_spec_ulong ("blocksize", "Block size", "Size in bytes to read per buffer",
                        1, G_MAXULONG, DEFAULT_BLOCKSIZE, G_PARAM_READWRITE));

  gobject_class->get_property = gst_vorbisfile_get_property;
  gobject_class->set_property = gst_vorbisfile_set_property;

  gstelement_class->change_state = gst_vorbisfile_change_state;
}

static void
gst_vorbisfile_init (VorbisFile * vorbisfile)
{
  vorbisfile->sinkpad = gst_pad_new_from_template (gst_vorbisdec_sink_template,
		                                   "sink");
  gst_element_add_pad (GST_ELEMENT (vorbisfile), vorbisfile->sinkpad);
  gst_pad_set_formats_function (vorbisfile->sinkpad, gst_vorbisfile_get_formats);
  gst_pad_set_convert_function (vorbisfile->sinkpad, gst_vorbisfile_sink_convert);

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
  gst_pad_use_explicit_caps (vorbisfile->srcpad);

  vorbisfile->total_bytes = 0;
  vorbisfile->offset = 0;
  vorbisfile->seek_pending = 0;
  vorbisfile->need_discont = FALSE;
  vorbisfile->streaminfo = NULL;
  vorbisfile->current_link = -1;
  vorbisfile->blocksize = DEFAULT_BLOCKSIZE;

  GST_FLAG_SET (vorbisfile, GST_ELEMENT_EVENT_AWARE);
}

/* the next four functions are the ov callbacks we provide to vorbisfile
 * which interface between GStreamer's handling of the data flow and
 * vorbis's needs */
static size_t
gst_vorbisfile_read (void *ptr, size_t size, size_t nmemb, void *datasource)
{
  guint32 got_bytes;
  guint8 *data;
  guint32 read_size = size * nmemb;

  VorbisFile *vorbisfile = GST_VORBISFILE (datasource);

  GST_DEBUG ("read %d", read_size);

  /* make sure we don't go to EOS */
  if (!vorbisfile->may_eos && vorbisfile->total_bytes && 
       vorbisfile->offset + read_size > vorbisfile->total_bytes) 
  {
    read_size = vorbisfile->total_bytes - vorbisfile->offset;
  }

  if (read_size == 0 || vorbisfile->eos)
    return 0;
  
  do {
    got_bytes = gst_bytestream_peek_bytes (vorbisfile->bs, &data, read_size);

    GST_DEBUG ("peek returned  %d", got_bytes);

    if (got_bytes == 0) {
      GstEvent *event;
      guint32 avail;
    
      gst_bytestream_get_status (vorbisfile->bs, &avail, &event); 

      switch (GST_EVENT_TYPE (event)) {
	case GST_EVENT_EOS:
	  GST_DEBUG ("eos");
          vorbisfile->eos = TRUE;
          gst_event_unref (event);
          goto done;
	case GST_EVENT_DISCONTINUOUS:
	  GST_DEBUG ("discont");
	  vorbisfile->need_discont = TRUE;
	default:
	  GST_DEBUG ("unknown event %d", GST_EVENT_TYPE (event));
          break;
      }
      gst_event_unref (event);
    }
  } while (got_bytes == 0);

  GST_DEBUG ("read %d got %d bytes", read_size, got_bytes);

  memcpy (ptr, data, got_bytes);
  gst_bytestream_flush_fast (vorbisfile->bs, got_bytes);

  vorbisfile->offset += got_bytes;

done:
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
  
  GST_DEBUG ("seek %" G_GINT64_FORMAT " %d", offset, whence);

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

  vorbisfile->eos = FALSE;
  vorbisfile->offset = pending_offset;
  if (need_total)
    vorbisfile->total_bytes = gst_bytestream_tell (vorbisfile->bs) + offset;

  return 0;
}

static int
gst_vorbisfile_close (void *datasource)
{
  GST_DEBUG ("close");
  return 0;
}

static long
gst_vorbisfile_tell (void *datasource)
{
  VorbisFile *vorbisfile = GST_VORBISFILE (datasource);
  long result;

  result = gst_bytestream_tell (vorbisfile->bs);

  GST_DEBUG ("tell %ld", result);

  return result;
}

ov_callbacks vorbisfile_ov_callbacks = 
{
  gst_vorbisfile_read,
  gst_vorbisfile_seek,
  gst_vorbisfile_close,
  gst_vorbisfile_tell,
};

/* retrieve logical stream properties and put them in streaminfo GstTagList.
 * returns TRUE if tags could be set,
 * FALSE if they couldn't be read somehow */
static gboolean
gst_vorbisfile_update_streaminfo (VorbisFile *vorbisfile, gint link)
{
  OggVorbis_File *vf = &vorbisfile->vf;
  vorbis_info *vi;
  GstTagList *tag_list;

  /* clear old one */
  if (vorbisfile->streaminfo) {
    gst_tag_list_free (vorbisfile->streaminfo);
    vorbisfile->streaminfo = NULL;
  }

  tag_list = gst_tag_list_new ();
  vi = ov_info (vf, link);

  gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
			GST_TAG_ENCODER_VERSION,	vi->version,
			GST_TAG_BITRATE,		ov_bitrate (vf, link),
			GST_TAG_SERIAL,			ov_serialnumber (vf, link),
			NULL);
  if (vi->bitrate_nominal > 0)
    gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
                      GST_TAG_NOMINAL_BITRATE, vi->bitrate_nominal, NULL);
  if (vi->bitrate_lower > 0)
    gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
                      GST_TAG_MINIMUM_BITRATE, vi->bitrate_lower, NULL);
  if (vi->bitrate_upper > 0)
    gst_tag_list_add (tag_list, GST_TAG_MERGE_APPEND,
                      GST_TAG_MAXIMUM_BITRATE, vi->bitrate_upper, NULL);

  vorbisfile->streaminfo = tag_list;

  gst_element_found_tags (GST_ELEMENT (vorbisfile), tag_list);

  return TRUE;
}


static gboolean
gst_vorbisfile_new_link (VorbisFile *vorbisfile, gint link)
{
  vorbis_info *vi = ov_info (&vorbisfile->vf, link);
  GstCaps *caps;

  /* new logical bitstream */
  vorbisfile->current_link = link;
  gst_vorbisfile_update_streaminfo (vorbisfile, link);

  caps = gst_caps_new_simple ("audio/x-raw-int",    
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed",     G_TYPE_BOOLEAN, TRUE,
      "width",      G_TYPE_INT, 16,
      "depth",      G_TYPE_INT, 16,
      "rate",       G_TYPE_INT, vi->rate,
      "channels",   G_TYPE_INT, vi->channels,
      NULL);

  return gst_pad_set_explicit_caps (vorbisfile->srcpad, caps);
}

static void
gst_vorbisfile_loop (GstElement *element)
{
  VorbisFile *vorbisfile = GST_VORBISFILE (element);
  GstBuffer *outbuf;
  long ret;
  gint link;

  /* this function needs to go first since you don't want to be messing
   * with an unset vf ;) */
  if (vorbisfile->restart) {
    vorbisfile->offset = 0;
    vorbisfile->total_bytes = 0;
    vorbisfile->may_eos = FALSE;
    vorbisfile->vf.seekable = gst_bytestream_seek (vorbisfile->bs, 0, 
		                                   GST_SEEK_METHOD_SET);
    GST_DEBUG ("vorbisfile: seekable: %s",
	       vorbisfile->vf.seekable ? "yes" : "no");

    /* open our custom vorbisfile data object with the callbacks we provide */
    if (ov_open_callbacks (vorbisfile, &vorbisfile->vf, NULL, 0, 
			   vorbisfile_ov_callbacks) < 0) {
      GST_ELEMENT_ERROR (element, STREAM, WRONG_TYPE, (NULL), (NULL));
      return;
    }
    vorbisfile->need_discont = TRUE;
    vorbisfile->restart = FALSE;
    vorbisfile->current_link = -1;
  }

  if (vorbisfile->seek_pending) {
    /* get time to seek to in seconds */
    switch (vorbisfile->seek_format) {
      case GST_FORMAT_TIME:
      {
        gdouble seek_to = (gdouble) vorbisfile->seek_value / GST_SECOND;

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
      }
      case GST_FORMAT_DEFAULT:
	if (vorbisfile->seek_accurate) {
          if (ov_pcm_seek (&vorbisfile->vf, vorbisfile->seek_value) == 0) {
            vorbisfile->need_discont = TRUE;
          }
        }
	else {
          if (ov_pcm_seek_page (&vorbisfile->vf, vorbisfile->seek_value) == 0) {
            vorbisfile->need_discont = TRUE;
          }
	}
	break;
      default:
	if (vorbisfile->seek_format == logical_stream_format) {
          gint64 seek_to;
	  
	  seek_to = vorbisfile->vf.offsets[vorbisfile->seek_value];

          if (ov_raw_seek (&vorbisfile->vf, seek_to) == 0) {
            vorbisfile->need_discont = TRUE;
            vorbisfile->current_link = -1;
          }
	  else {
	    g_warning ("raw seek failed");
	  }
	}
	else
	  g_warning ("unknown seek method, implement me !");
	break;
    }
    vorbisfile->seek_pending = FALSE;
  }

  /* we update the caps for each logical stream */
  if (vorbisfile->vf.current_link != vorbisfile->current_link) {
    if (!gst_vorbisfile_new_link (vorbisfile, vorbisfile->vf.current_link)) {
      return;
    }
    return;
  }

  outbuf = gst_buffer_new_and_alloc (vorbisfile->blocksize);

  ret = ov_read (&vorbisfile->vf, 
		 GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf), 
		 (G_BYTE_ORDER == G_LITTLE_ENDIAN ? 0 : 1), 
		 sizeof (gint16), 1, &link);

  if (ret == 0) {
    GST_DEBUG ("eos");
    /* send EOS event */
    /*ov_clear (&vorbisfile->vf);*/
    vorbisfile->restart = TRUE;
    gst_buffer_unref (outbuf);
    /* if the pad is not usable, don't push it out */
    if (GST_PAD_IS_USABLE (vorbisfile->srcpad)) {
      gst_pad_push (vorbisfile->srcpad, 
		    GST_DATA (gst_event_new (GST_EVENT_EOS)));
    }
    gst_element_set_eos (element);
    return;
  }
  else if (ret < 0) {
    g_warning ("vorbisfile: decoding error");
    gst_buffer_unref (outbuf);
    return;
  }
  else {
    GstClockTime time;
    gint64 samples;
    
   /* get stream stats */
    samples = (gint64) (ov_pcm_tell (&vorbisfile->vf));
    time = (GstClockTime) (ov_time_tell (&vorbisfile->vf) * GST_SECOND);

    if (vorbisfile->need_discont) {
      GstEvent *discont;

      vorbisfile->need_discont = FALSE;

      /* if the pad is not usable, don't push it out */
      if (GST_PAD_IS_USABLE (vorbisfile->srcpad)) {

        discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, 
		    			     GST_FORMAT_DEFAULT, samples, NULL); 

        gst_pad_push (vorbisfile->srcpad, GST_DATA (discont));
      }
    }

    GST_BUFFER_SIZE (outbuf) = ret;
    GST_BUFFER_TIMESTAMP (outbuf) = time;
    GST_BUFFER_OFFSET (outbuf) = samples;
    {
      GstClockTime duration;
      GstFormat format;

      format = GST_FORMAT_TIME;
      gst_pad_convert (vorbisfile->srcpad,
			     GST_FORMAT_BYTES, ret,
			     &format, &duration);
      GST_BUFFER_DURATION (outbuf) = duration;
    }

    vorbisfile->may_eos = TRUE;

    if (!vorbisfile->vf.seekable) {
      vorbisfile->total_bytes += ret;
    }
  
    if (GST_PAD_IS_USABLE (vorbisfile->srcpad)) 
      gst_pad_push (vorbisfile->srcpad, GST_DATA (outbuf));
    else
      gst_buffer_unref (outbuf);
  }
}

static const GstFormat*
gst_vorbisfile_get_formats (GstPad *pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    0,
    0
  };
  static GstFormat sink_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    0,
    0
  };

  src_formats[3] = logical_stream_format;
  sink_formats[2] = logical_stream_format;
  
  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}

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
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (vi->channels * 2);
          break;
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
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * bytes_per_sample;
          break;
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
        case GST_FORMAT_BYTES:
	  scale = bytes_per_sample;
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value * scale * vi->rate / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      if (src_format == logical_stream_format) {
	/* because we need to convert relative from 0, we have to add
	 * all pcm totals */
	gint i;
	gint64 count = 0;

        switch (*dest_format) {
          case GST_FORMAT_BYTES:
            res = FALSE;
            break;
          case GST_FORMAT_DEFAULT:
	    if (src_value > vorbisfile->vf.links) {
	      src_value = vorbisfile->vf.links;
	    }
	    for (i = 0; i < src_value; i++) {
	      vi = ov_info (&vorbisfile->vf, i);

	      count += ov_pcm_total (&vorbisfile->vf, i);
	    }
	    *dest_value = count;
            break;
          case GST_FORMAT_TIME:
	  {
	    if (src_value > vorbisfile->vf.links) {
	      src_value = vorbisfile->vf.links;
	    }
	    for (i = 0; i < src_value; i++) {
	      vi = ov_info (&vorbisfile->vf, i);
	      if (vi->rate) 
	        count += ov_pcm_total (&vorbisfile->vf, i) * GST_SECOND / vi->rate;
	      else
	        count += ov_time_total (&vorbisfile->vf, i) * GST_SECOND;
	    }
	    /* we use the pcm totals to get the total time, it's more accurate */
	    *dest_value = count;
            break;
	  }
          default:
            res = FALSE;
	}
      }
      else
        res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_vorbisfile_sink_convert (GstPad *pad, 
		             GstFormat src_format, gint64 src_value,
		             GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  VorbisFile *vorbisfile; 
  
  vorbisfile = GST_VORBISFILE (gst_pad_get_parent (pad));

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          break;
        default:
          if (*dest_format == logical_stream_format) {
          }
	  else
            res = FALSE;
      }
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          break;
        default:
          if (*dest_format == logical_stream_format) {
          }
	  else
            res = FALSE;
      }
    default:
      if (src_format == logical_stream_format) {
        switch (*dest_format) {
          case GST_FORMAT_TIME:
            break;
          case GST_FORMAT_BYTES:
            break;
          default:
            res = FALSE;
        }
      }
      else
        res = FALSE;
      break;
  }

  return res;
}

static const GstQueryType*
gst_vorbisfile_get_query_types (GstPad *pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };
  return types;
}

/* handles queries for location in the stream in the requested format */
static gboolean
gst_vorbisfile_src_query (GstPad *pad, GstQueryType type,
		          GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  VorbisFile *vorbisfile; 
  vorbis_info *vi;
  
  vorbisfile = GST_VORBISFILE (gst_pad_get_parent (pad));

  vi = ov_info (&vorbisfile->vf, -1);

  switch (type) {
    case GST_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
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
        case GST_FORMAT_TIME:
          if (vorbisfile->vf.seekable)
	    *value = (gint64) (ov_time_total (&vorbisfile->vf, -1) * GST_SECOND);
	  else
	    return FALSE;
	  break;
	default:
	  if (*format == logical_stream_format) {
            if (vorbisfile->vf.seekable)
	      *value = vorbisfile->vf.links;
	    else
	     return FALSE;
	  }
	  else
            res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_POSITION:
      switch (*format) {
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
        case GST_FORMAT_DEFAULT:
          if (vorbisfile->vf.seekable)
	    *value = ov_pcm_tell (&vorbisfile->vf);
	  else
            *value = vorbisfile->total_bytes / (vi->channels * 2);
	  break;
        default:
	  if (*format == logical_stream_format) {
            if (vorbisfile->vf.seekable)
	      *value = vorbisfile->current_link;
	    else
	     return FALSE;
	  }
	  else
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
      GstFormat format;
  
      GST_DEBUG ("vorbisfile: handling seek event on pad %s:%s",
		 GST_DEBUG_PAD_NAME (pad));
      if (!vorbisfile->vf.seekable) {
	gst_event_unref (event);
	GST_DEBUG ("vorbis stream is not seekable");
        return FALSE;
      }

      offset = GST_EVENT_SEEK_OFFSET (event);
      format = GST_EVENT_SEEK_FORMAT (event);

      switch (format) {
	case GST_FORMAT_TIME:
	  vorbisfile->seek_pending = TRUE;
	  vorbisfile->seek_value = offset;
	  vorbisfile->seek_format = format;
	  vorbisfile->seek_accurate = GST_EVENT_SEEK_FLAGS (event) 
		                    & GST_SEEK_FLAG_ACCURATE;
	  break;
	case GST_FORMAT_BYTES:
          vi = ov_info (&vorbisfile->vf, -1);
	  if (vi->channels == 0) {
	    GST_DEBUG ("vorbis stream has 0 channels ?");
	    res = FALSE;
	    goto done; 
	  }
          offset /= vi->channels * 2;
	  /* fallthrough */
	case GST_FORMAT_DEFAULT:
	  vorbisfile->seek_pending = TRUE;
	  vorbisfile->seek_value = offset;
	  vorbisfile->seek_format = format;
	  vorbisfile->seek_accurate = GST_EVENT_SEEK_FLAGS (event) 
		                    & GST_SEEK_FLAG_ACCURATE;
	  break;
	default:
	  if (format == logical_stream_format) {
	    vorbisfile->seek_pending = TRUE;
	    vorbisfile->seek_value = offset;
	    vorbisfile->seek_format = format;
	    vorbisfile->seek_accurate = GST_EVENT_SEEK_FLAGS (event) 
		                      & GST_SEEK_FLAG_ACCURATE;
	  }
	  else
	  {
	    GST_DEBUG ("unhandled seek format");
	    res = FALSE;
	  }
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
    case ARG_BLOCKSIZE:
      vorbisfile->blocksize = g_value_get_ulong (value);
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

  switch (prop_id) {
    case ARG_BLOCKSIZE:
      g_value_set_ulong (value, vorbisfile->blocksize);
      break;
    default:
      g_warning ("Unknown property id\n");
  }
}
