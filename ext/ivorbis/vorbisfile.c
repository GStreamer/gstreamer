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
#include <tremor/ivorbiscodec.h>
#include <tremor/ivorbisfile.h>
#include <gst/base/gstadapter.h>

GST_DEBUG_CATEGORY_STATIC (ivorbisfile_debug);
#define GST_CAT_DEFAULT ivorbisfile_debug

#define GST_TYPE_IVORBISFILE                    \
  (ivorbisfile_get_type())
#define GST_IVORBISFILE(obj)                                            \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IVORBISFILE,Ivorbisfile))
#define GST_IVORBISFILE_CLASS(klass)                                    \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IVORBISFILE,IvorbisfileClass))
#define GST_IS_IVORBISFILE(obj)                                 \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IVORBISFILE))
#define GST_IS_IVORBISFILE_CLASS(klass)                           \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IVORBISFILE))

typedef struct _Ivorbisfile Ivorbisfile;
typedef struct _IvorbisfileClass IvorbisfileClass;

struct _Ivorbisfile
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
  GstAdapter *adapter;
  guint64 adapterOffset;

  OggVorbis_File vf;
  gint current_link;

  gboolean restart;
  gboolean need_discont;
  gboolean eos;
  gboolean seek_pending;
  gint64 seek_value;
  GstFormat seek_format;
  gboolean seek_accurate;

  gboolean may_eos;
  guint64 total_bytes;
  guint64 offset;

  gint rate;
  gint channels;
  gint width;

  GstCaps *metadata;
  GstCaps *streaminfo;
};

struct _IvorbisfileClass
{
  GstElementClass parent_class;

};

GType ivorbisfile_get_type (void);

static GstPadTemplate *gst_vorbisdec_src_template, *gst_vorbisdec_sink_template;

/* elementfactory information */
static const GstElementDetails ivorbisfile_details =
GST_ELEMENT_DETAILS ("Ogg Vorbis audio decoder",
    "Codec/Decoder/Audio",
    "Decodes OGG Vorbis audio using the Tremor vorbisfile API",
    "Monty <monty@xiph.org>\n"
    "Wim Taymans <wim.taymans@chello.be>\n"
    "Amaury Jacquot <sxpert@esitcom.org>");

/* Ivorbisfile signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_METADATA,
  ARG_STREAMINFO
};

static void gst_ivorbisfile_base_init (gpointer g_class);
static void gst_ivorbisfile_class_init (IvorbisfileClass * klass);
static void gst_ivorbisfile_init (Ivorbisfile * ivorbisfile);
static void gst_ivorbisfile_finalize (GObject * object);

static GstStateChangeReturn
gst_ivorbisfile_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_ivorbisfile_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_ivorbisfile_sink_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static const GstQueryType *gst_ivorbisfile_get_src_query_types (GstPad * pad);

static gboolean gst_ivorbisfile_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_ivorbisfile_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_ivorbisfile_sink_event (GstPad * pad, GstEvent * event);

static const GstQueryType *gst_ivorbisfile_get_sink_query_types (GstPad * pad);

static gboolean gst_ivorbisfile_sink_query (GstPad * pad, GstQuery * query);

static void gst_ivorbisfile_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_ivorbisfile_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_ivorbisfile_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_ivorbisfile_sink_activate (GstPad * sinkpad);

static gboolean
gst_ivorbisfile_sink_activate_pull (GstPad * sinkpad, gboolean active);

static void gst_ivorbisfile_loop (GstPad * pad);
static GstFlowReturn gst_ivorbisfile_play (GstPad * pad);

static GstElementClass *parent_class = NULL;

static GstFormat logical_stream_format;

GType
ivorbisfile_get_type (void)
{
  static GType ivorbisfile_type = 0;

  if (!ivorbisfile_type) {
    static const GTypeInfo ivorbisfile_info = {
      sizeof (IvorbisfileClass),
      gst_ivorbisfile_base_init,
      NULL,
      (GClassInitFunc) gst_ivorbisfile_class_init, NULL, NULL,
      sizeof (Ivorbisfile), 0,
      (GInstanceInitFunc) gst_ivorbisfile_init,
    };

    ivorbisfile_type = g_type_register_static (GST_TYPE_ELEMENT, "Ivorbisfile",
        &ivorbisfile_info, 0);

    logical_stream_format =
        gst_format_register ("logical_stream", "The logical stream");

    GST_DEBUG_CATEGORY_INIT (ivorbisfile_debug, "ivorbisfile", 0,
        "vorbis in ogg decoding element (integer arithmetic)");
  }
  return ivorbisfile_type;
}

static GstCaps *
vorbis_caps_factory (void)
{
  return gst_caps_new_simple ("application/ogg", NULL);

}

static GstCaps *
raw_caps_factory (void)
{
  return
      gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "rate", GST_TYPE_INT_RANGE, 11025, 48000,
      "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
}

static GstCaps *
raw_caps2_factory (void)
{
  return
      gst_caps_new_simple ("audio/x-raw-float",
      "depth", G_TYPE_INT, 32,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", GST_TYPE_INT_RANGE, 11025, 48000,
      "channels", G_TYPE_INT, 2, NULL);
}


static void
gst_ivorbisfile_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *vorbis_caps, *raw_caps2;

  raw_caps = raw_caps_factory ();
  raw_caps2 = raw_caps2_factory ();
  vorbis_caps = vorbis_caps_factory ();

  /* register sink pads */
  gst_vorbisdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, vorbis_caps);
  gst_caps_append (raw_caps2, raw_caps);
  /* register src pads */
  gst_vorbisdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, raw_caps2);
  gst_element_class_add_pad_template (element_class,
      gst_vorbisdec_sink_template);
  gst_element_class_add_pad_template (element_class,
      gst_vorbisdec_src_template);
  gst_element_class_set_details (element_class, &ivorbisfile_details);
}

static void
gst_ivorbisfile_class_init (IvorbisfileClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->get_property = gst_ivorbisfile_get_property;
  gobject_class->set_property = gst_ivorbisfile_set_property;

  g_object_class_install_property (gobject_class, ARG_METADATA,
      g_param_spec_boxed ("metadata", "Metadata", "(logical) Stream metadata",
          GST_TYPE_CAPS, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_STREAMINFO,
      g_param_spec_boxed ("streaminfo", "stream",
          "(logical) Stream information", GST_TYPE_CAPS, G_PARAM_READABLE));

  gobject_class->finalize = gst_ivorbisfile_finalize;
  gstelement_class->change_state = gst_ivorbisfile_change_state;
}

static void
gst_ivorbisfile_init (Ivorbisfile * ivorbisfile)
{
  ivorbisfile->sinkpad = gst_pad_new_from_template (gst_vorbisdec_sink_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (ivorbisfile), ivorbisfile->sinkpad);

  gst_pad_set_query_type_function (ivorbisfile->sinkpad,
      gst_ivorbisfile_get_sink_query_types);
  gst_pad_set_query_function (ivorbisfile->sinkpad, gst_ivorbisfile_sink_query);

  gst_pad_set_activate_function (ivorbisfile->sinkpad,
      gst_ivorbisfile_sink_activate);
  gst_pad_set_activatepull_function (ivorbisfile->sinkpad,
      gst_ivorbisfile_sink_activate_pull);
  gst_pad_set_chain_function (ivorbisfile->sinkpad, gst_ivorbisfile_chain);
  gst_pad_set_event_function (ivorbisfile->sinkpad, gst_ivorbisfile_sink_event);

  ivorbisfile->srcpad =
      gst_pad_new_from_template (gst_vorbisdec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (ivorbisfile), ivorbisfile->srcpad);

  gst_pad_set_query_type_function (ivorbisfile->srcpad,
      gst_ivorbisfile_get_src_query_types);
  gst_pad_set_query_function (ivorbisfile->srcpad, gst_ivorbisfile_src_query);

  gst_pad_set_event_function (ivorbisfile->srcpad, gst_ivorbisfile_src_event);

  ivorbisfile->adapter = NULL;

  if (ivorbisfile->metadata) {
    ivorbisfile->metadata = NULL;
  }
  if (ivorbisfile->streaminfo) {
    ivorbisfile->streaminfo = NULL;
  }

}


static void
gst_ivorbisfile_finalize (GObject * object)
{
  Ivorbisfile *ivorbisfile;

  ivorbisfile = GST_IVORBISFILE (object);

  if (ivorbisfile->adapter) {
    g_object_unref (ivorbisfile->adapter);
    ivorbisfile->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}



/* the next four functions are the ov callbacks we provide to ivorbisfile
 * which interface between GStreamer's handling of the data flow and
 * vorbis's needs */
static size_t
gst_ivorbisfile_read (void *ptr, size_t size, size_t nmemb, void *datasource)
{
  size_t read_size = size * nmemb;
  guint buf_size = 0;
  Ivorbisfile *ivorbisfile = GST_IVORBISFILE (datasource);
  size_t ret;

  GST_LOG ("read %d", read_size);

  /* make sure we don't go to EOS */

  if (!ivorbisfile->may_eos && ivorbisfile->total_bytes &&
      ivorbisfile->offset + read_size > ivorbisfile->total_bytes) {
    read_size = ivorbisfile->total_bytes - ivorbisfile->offset;
  }

  if (read_size == 0 || ivorbisfile->eos) {
    return 0;
  }

  if (ivorbisfile->adapter) {
    const guint8 *buf = NULL;

    buf_size = gst_adapter_available (ivorbisfile->adapter);

    if (buf_size < read_size) {
      return 0;
    }

    if (buf_size > read_size) {
      buf_size = read_size;
    } else if (buf_size == 0) {
      return 0;
    }


    buf = gst_adapter_peek (ivorbisfile->adapter, buf_size);

    memcpy (ptr, buf, buf_size);

    gst_adapter_flush (ivorbisfile->adapter, buf_size);

  } else {

    GstBuffer *buf = NULL;

    if (GST_FLOW_OK != gst_pad_pull_range (ivorbisfile->sinkpad,
            ivorbisfile->offset, read_size, &buf)) {
      return 0;
    }

    buf_size = GST_BUFFER_SIZE (buf);

    memcpy (ptr, GST_BUFFER_DATA (buf), buf_size);

    gst_buffer_unref (buf);

  }

  ivorbisfile->offset += buf_size;

  ret = buf_size / size;

  return ret;
}

static int
gst_ivorbisfile_seek (void *datasource, ogg_int64_t offset, int whence)
{
  Ivorbisfile *ivorbisfile = GST_IVORBISFILE (datasource);
  guint64 pending_offset = ivorbisfile->offset;
  gboolean need_total = FALSE;


  if (!ivorbisfile->vf.seekable) {
    return -1;
  }

  GST_DEBUG ("seek %" G_GINT64_FORMAT " %d", offset, whence);

  if (whence == SEEK_SET) {
    pending_offset = offset;
    ivorbisfile->adapterOffset = offset;
  } else if (whence == SEEK_CUR) {
    pending_offset += offset;
    ivorbisfile->adapterOffset += offset;
  } else if (whence == SEEK_END) {
    need_total = TRUE;
    pending_offset = ivorbisfile->total_bytes - offset;
    ivorbisfile->adapterOffset = ivorbisfile->total_bytes - offset;
  } else
    return -1;


  ivorbisfile->offset = pending_offset;
  if (need_total)
    ivorbisfile->total_bytes = ivorbisfile->adapterOffset + offset;

  return 0;
}

static int
gst_ivorbisfile_close (void *datasource)
{
  GST_DEBUG ("close");
  return 0;
}

static long
gst_ivorbisfile_tell (void *datasource)
{
  Ivorbisfile *ivorbisfile = GST_IVORBISFILE (datasource);
  long result;

  result = ivorbisfile->adapterOffset;

  GST_DEBUG ("tell %ld", result);

  return result;
}

ov_callbacks ivorbisfile_ov_callbacks = {
  gst_ivorbisfile_read,
  gst_ivorbisfile_seek,
  gst_ivorbisfile_close,
  gst_ivorbisfile_tell,
};

#if 0
/* retrieve the comment field (or tags) and put in metadata GstCaps
 * returns TRUE if caps could be set,
 * FALSE if they couldn't be read somehow */
static gboolean
gst_ivorbisfile_update_metadata (Ivorbisfile * ivorbisfile, gint link)
{
  OggVorbis_File *vf = &ivorbisfile->vf;
  gchar **ptr;
  vorbis_comment *vc;
  GstProps *props = NULL;
  GstPropsEntry *entry;
  gchar *name, *value;

  /* clear old one */
  if (ivorbisfile->metadata) {
    gst_caps_unref (ivorbisfile->metadata);
    ivorbisfile->metadata = NULL;
  }

  /* create props to hold the key/value pairs */
  props = gst_props_empty_new ();

  vc = ov_comment (vf, link);
  ptr = vc->user_comments;
  while (*ptr) {
    value = strstr (*ptr, "=");
    if (value) {
      name = g_strndup (*ptr, value - *ptr);
      entry = gst_props_entry_new (name, GST_PROPS_STRING_TYPE, value + 1);
      gst_props_add_entry (props, (GstPropsEntry *) entry);
    }
    ptr++;
  }
  ivorbisfile->metadata = gst_caps_new ("ivorbisfile_metadata",
      "application/x-gst-metadata", props);

  g_object_notify (G_OBJECT (ivorbisfile), "metadata");

  return TRUE;
}

/* retrieve logical stream properties and put them in streaminfo GstCaps
 * returns TRUE if caps could be set,
 * FALSE if they couldn't be read somehow */
static gboolean
gst_ivorbisfile_update_streaminfo (Ivorbisfile * ivorbisfile, gint link)
{
  OggVorbis_File *vf = &ivorbisfile->vf;
  vorbis_info *vi;
  GstProps *props = NULL;
  GstPropsEntry *entry;

  /* clear old one */
  if (ivorbisfile->streaminfo) {
    gst_caps_unref (ivorbisfile->streaminfo);
    ivorbisfile->streaminfo = NULL;
  }

  /* create props to hold the key/value pairs */
  props = gst_props_empty_new ();

  vi = ov_info (vf, link);
  entry = gst_props_entry_new ("version", GST_PROPS_INT_TYPE, vi->version);
  gst_props_add_entry (props, (GstPropsEntry *) entry);
  entry = gst_props_entry_new ("bitrate_upper", GST_PROPS_INT_TYPE,
      vi->bitrate_upper);
  gst_props_add_entry (props, (GstPropsEntry *) entry);
  entry = gst_props_entry_new ("bitrate_nominal", GST_PROPS_INT_TYPE,
      vi->bitrate_nominal);
  gst_props_add_entry (props, (GstPropsEntry *) entry);
  entry = gst_props_entry_new ("bitrate_lower", GST_PROPS_INT_TYPE,
      vi->bitrate_lower);
  gst_props_add_entry (props, (GstPropsEntry *) entry);
  entry = gst_props_entry_new ("serial", GST_PROPS_INT_TYPE,
      ov_serialnumber (vf, link));
  gst_props_add_entry (props, (GstPropsEntry *) entry);
  entry = gst_props_entry_new ("bitrate", GST_PROPS_INT_TYPE,
      ov_bitrate (vf, link));
  gst_props_add_entry (props, (GstPropsEntry *) entry);

  ivorbisfile->streaminfo = gst_caps_new ("ivorbisfile_streaminfo",
      "application/x-gst-streaminfo", props);

  g_object_notify (G_OBJECT (ivorbisfile), "streaminfo");

  return TRUE;
}
#endif

static gboolean
gst_ivorbisfile_new_link (Ivorbisfile * ivorbisfile, gint link)
{
  vorbis_info *vi = ov_info (&ivorbisfile->vf, link);
  GstCaps *caps;
  gboolean res = TRUE;

  /* new logical bitstream */
  ivorbisfile->current_link = link;

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "rate", G_TYPE_INT, vi->rate, "channels", G_TYPE_INT, vi->channels, NULL);

  ivorbisfile->rate = vi->rate;
  ivorbisfile->channels = vi->channels;
  ivorbisfile->width = 16;

  if (gst_pad_set_caps (ivorbisfile->srcpad, caps) <= 0) {
    res = FALSE;
  }

  gst_caps_unref (caps);

  return TRUE;
}


static gboolean
gst_ivorbisfile_sink_activate (GstPad * sinkpad)
{

  Ivorbisfile *ivorbisfile;

  ivorbisfile = GST_IVORBISFILE (GST_PAD_PARENT (sinkpad));

  if (gst_pad_check_pull_range (sinkpad)) {
    /* FIX ME */
    /* ivorbisfile->vf.seekable = TRUE; */
    ivorbisfile->vf.seekable = FALSE;
    if (ivorbisfile->adapter) {
      gst_adapter_clear (ivorbisfile->adapter);
      g_object_unref (ivorbisfile->adapter);
      ivorbisfile->adapter = NULL;
    }
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {

    if (ivorbisfile->adapter) {
      gst_adapter_clear (ivorbisfile->adapter);
    } else {
      ivorbisfile->adapter = gst_adapter_new ();
    }
    ivorbisfile->vf.seekable = FALSE;
    return gst_pad_activate_push (sinkpad, TRUE);
  }


}


static gboolean
gst_ivorbisfile_sink_activate_pull (GstPad * sinkpad, gboolean active)
{

  gboolean result;

  if (active) {
    /* if we have a scheduler we can start the task */
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_ivorbisfile_loop, sinkpad);
  } else {
    result = gst_pad_stop_task (sinkpad);
  }

  return result;
}



static GstFlowReturn
gst_ivorbisfile_chain (GstPad * pad, GstBuffer * buffer)
{

  Ivorbisfile *ivorbisfile = GST_IVORBISFILE (GST_PAD_PARENT (pad));

  if (NULL == ivorbisfile->adapter) {
    GST_ERROR ("pull expected! Chain func should not be called");
    return GST_FLOW_UNEXPECTED;
  }

  gst_adapter_push (ivorbisfile->adapter, buffer);

  return gst_ivorbisfile_play (pad);

}


static void
gst_ivorbisfile_loop (GstPad * pad)
{
  gst_ivorbisfile_play (pad);
}

static GstFlowReturn
gst_ivorbisfile_play (GstPad * pad)
{
  Ivorbisfile *ivorbisfile = GST_IVORBISFILE (gst_pad_get_parent (pad));
  GstBuffer *outbuf;
  long ret;
  GstClockTime time;
  gint64 samples;
  gint link;
  GstFlowReturn res = GST_FLOW_OK;

  if (ivorbisfile->eos) {
    goto done;
  }

  /* this function needs to go first since you don't want to be messing
   * with an unset vf ;) */
  if (ivorbisfile->restart) {
    gint err;

    if (ivorbisfile->adapter) {
      if (gst_adapter_available (ivorbisfile->adapter) < 40960) {
        goto done;
      }
    }

    ivorbisfile->offset = 0;
    ivorbisfile->total_bytes = 0;
    ivorbisfile->may_eos = FALSE;
    ivorbisfile->adapterOffset = 0;
    GST_DEBUG ("ivorbisfile: seekable: %s\n",
        ivorbisfile->vf.seekable ? "yes" : "no");

    /* open our custom ivorbisfile data object with the callbacks we provide */

    if ((err = ov_open_callbacks (ivorbisfile, &ivorbisfile->vf, NULL, 0,
                ivorbisfile_ov_callbacks)) < 0) {
      GST_ELEMENT_ERROR (ivorbisfile, STREAM, DECODE, (NULL), (NULL));
      goto done;
    }

    ivorbisfile->need_discont = TRUE;
    ivorbisfile->restart = FALSE;
    ivorbisfile->current_link = -1;
  }

  if (ivorbisfile->seek_pending) {
    /* get time to seek to in seconds */

    switch (ivorbisfile->seek_format) {
      case GST_FORMAT_TIME:
      {
        gdouble seek_to = (gdouble) ivorbisfile->seek_value / GST_SECOND;

        if (ivorbisfile->seek_accurate) {
          if (ov_time_seek (&ivorbisfile->vf, seek_to) == 0) {
            ivorbisfile->need_discont = TRUE;
          }
        } else {
          if (ov_time_seek_page (&ivorbisfile->vf, seek_to) == 0) {
            ivorbisfile->need_discont = TRUE;
          }
        }
        break;
      }
      case GST_FORMAT_DEFAULT:
        if (ivorbisfile->seek_accurate) {
          if (ov_pcm_seek (&ivorbisfile->vf, ivorbisfile->seek_value) == 0) {
            ivorbisfile->need_discont = TRUE;
          }
        } else {
          if (ov_pcm_seek_page (&ivorbisfile->vf, ivorbisfile->seek_value) == 0) {
            ivorbisfile->need_discont = TRUE;
          }
        }
        break;
      default:
        if (ivorbisfile->seek_format == logical_stream_format) {
          gint64 seek_to;

          seek_to = ivorbisfile->vf.offsets[ivorbisfile->seek_value];

          if (ov_raw_seek (&ivorbisfile->vf, seek_to) == 0) {
            ivorbisfile->need_discont = TRUE;
            ivorbisfile->current_link = -1;
          } else {
            GST_WARNING ("raw seek failed");
          }
        } else
          GST_WARNING ("unknown seek method, implement me !");
        break;
    }
    ivorbisfile->seek_pending = FALSE;
  }

  /* we update the caps for each logical stream */
  if (ivorbisfile->vf.current_link != ivorbisfile->current_link) {
    if (!gst_ivorbisfile_new_link (ivorbisfile, ivorbisfile->vf.current_link)) {
      GST_ELEMENT_ERROR (ivorbisfile, CORE, NEGOTIATION, (NULL), (NULL));
    }
    goto done;
  }

  do {

    outbuf = gst_buffer_new_and_alloc (4096);

    ret = ov_read (&ivorbisfile->vf,
        (char *) GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf), &link);

    /* get current time for discont and buffer timestamp */
    time = (GstClockTime) (ov_time_tell (&ivorbisfile->vf) * GST_SECOND);

    if (ret == 0) {
      gst_buffer_unref (outbuf);
      if (ivorbisfile->adapter == NULL) {
        ivorbisfile->eos = TRUE;
        ivorbisfile->restart = TRUE;
        gst_pad_push_event (ivorbisfile->srcpad, gst_event_new_eos ());
      }
      goto done;
    } else if (ret < 0) {
      switch (ret) {
        case OV_HOLE:
          GST_WARNING
              ("Vorbisfile encoutered missing or corrupt data in the bitstream."
              " Recovery is normally automatic and"
              " this return code is for informational purposes only.");
          break;
        case OV_EBADLINK:
          GST_WARNING ("The given link exists in the Vorbis data stream,"
              " but is not decipherable due to garbacge or corruption.");
          break;
        default:
          GST_ERROR ("ivorbisfile: decoding error, unexpected ret = %ld", ret);
          break;
      }
      gst_buffer_unref (outbuf);
      goto done;
    } else {
      if (ivorbisfile->need_discont) {
        GstEvent *event;

        ivorbisfile->need_discont = FALSE;

        /* get stream stats */
        samples = (gint64) (ov_pcm_tell (&ivorbisfile->vf));

        event =
            gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, time,
            GST_CLOCK_TIME_NONE, 0);

        gst_pad_push_event (ivorbisfile->srcpad, event);

      }

      if (NULL == GST_PAD_CAPS (ivorbisfile->srcpad)) {
        gst_buffer_unref (outbuf);
        goto done;
      }

      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ivorbisfile->srcpad));

      GST_BUFFER_SIZE (outbuf) = ret;
      /* FIX ME TO SET RIGHT TIMESTAMP
         gint bufsize = ret / (ivorbisfile->width / 8);
         GST_BUFFER_TIMESTAMP (outbuf) = time;
         GST_BUFFER_DURATION (outbuf) =  GST_SECOND * bufsize / (ivorbisfile->rate * ivorbisfile->channels);
       */

      ivorbisfile->may_eos = TRUE;

      if (!ivorbisfile->vf.seekable) {
        ivorbisfile->total_bytes += GST_BUFFER_SIZE (outbuf);
      }

      if (GST_FLOW_OK != (res = gst_pad_push (ivorbisfile->srcpad, outbuf))) {
        goto done;
      }

    }

  } while (TRUE);

done:

  gst_object_unref (ivorbisfile);
  return res;

}

static gboolean
gst_ivorbisfile_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  Ivorbisfile *ivorbisfile;
  vorbis_info *vi;

  ivorbisfile = GST_IVORBISFILE (GST_PAD_PARENT (pad));

  vi = ov_info (&ivorbisfile->vf, -1);
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
            if (src_value > ivorbisfile->vf.links) {
              src_value = ivorbisfile->vf.links;
            }
            for (i = 0; i < src_value; i++) {
              vi = ov_info (&ivorbisfile->vf, i);

              count += ov_pcm_total (&ivorbisfile->vf, i);
            }
            *dest_value = count;
            break;
          case GST_FORMAT_TIME:
          {
            if (src_value > ivorbisfile->vf.links) {
              src_value = ivorbisfile->vf.links;
            }
            for (i = 0; i < src_value; i++) {
              vi = ov_info (&ivorbisfile->vf, i);
              if (vi->rate)
                count +=
                    ov_pcm_total (&ivorbisfile->vf, i) * GST_SECOND / vi->rate;
              else
                count += ov_time_total (&ivorbisfile->vf, i) * GST_SECOND;
            }
            /* we use the pcm totals to get the total time, it's more accurate */
            *dest_value = count;
            break;
          }
          default:
            res = FALSE;
        }
      } else
        res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_ivorbisfile_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      /* peel off input */
      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if ((res = gst_ivorbisfile_sink_convert (pad, src_fmt, src_val,
                  &dest_fmt, &dest_val))) {
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_ivorbisfile_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  Ivorbisfile *ivorbisfile;

  ivorbisfile = GST_IVORBISFILE (GST_PAD_PARENT (pad));

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          break;
        default:
          if (*dest_format == logical_stream_format) {
          } else
            res = FALSE;
      }
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          break;
        default:
          if (*dest_format == logical_stream_format) {
          } else
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
      } else
        res = FALSE;
      break;
  }

  return res;
}

static const GstQueryType *
gst_ivorbisfile_get_sink_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return types;
}

static const GstQueryType *
gst_ivorbisfile_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return types;
}

/* handles queries for location in the stream in the requested format */
static gboolean
gst_ivorbisfile_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  Ivorbisfile *ivorbisfile;
  vorbis_info *vi;

  ivorbisfile = GST_IVORBISFILE (GST_PAD_PARENT (pad));

  vi = ov_info (&ivorbisfile->vf, -1);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {

      GstFormat format;
      GstFormat rformat = GST_FORMAT_TIME;
      gint64 cur;
      GstPad *peer;

      /* save requested format */
      gst_query_parse_position (query, &format, NULL);

      /* query peer for current position in time */
      gst_query_set_position (query, GST_FORMAT_TIME, -1);

      if ((peer = gst_pad_get_peer (ivorbisfile->sinkpad)) == NULL)
        goto error;

      if (!gst_pad_query_position (peer, &rformat, &cur)) {
        GST_LOG_OBJECT (ivorbisfile, "query on peer pad failed");
        gst_object_unref (peer);
        goto error;
      }
      gst_object_unref (peer);

      if (format != rformat) {
        gst_ivorbisfile_src_convert (pad, rformat, cur, &format, &cur);
      }

      switch (format) {
        case GST_FORMAT_DEFAULT:
          if (ivorbisfile->vf.seekable)
            cur = ov_pcm_tell (&ivorbisfile->vf);
          else
            cur = ivorbisfile->total_bytes / (vi->channels * 2);
          break;
        case GST_FORMAT_TIME:
          if (ivorbisfile->vf.seekable)
            cur = (gint64) (ov_time_tell (&ivorbisfile->vf) * GST_SECOND);
          else
            cur = ivorbisfile->total_bytes * GST_SECOND
                / (vi->rate * vi->channels * 2);
          break;
        case GST_FORMAT_BYTES:
          if (ivorbisfile->vf.seekable)
            cur = ov_pcm_tell (&ivorbisfile->vf) * vi->channels * 2;
          else
            cur = ivorbisfile->total_bytes;
          break;
        default:
          if (format == logical_stream_format) {
            if (ivorbisfile->vf.seekable)
              cur = ivorbisfile->current_link;
            else
              return FALSE;
          } else
            res = FALSE;
          break;
      }

      gst_query_set_position (query, format, cur);

      break;
    }
    case GST_QUERY_DURATION:
    {

      GstFormat format;
      GstFormat rformat = GST_FORMAT_TIME;
      gint64 cur;
      GstPad *peer;

      /* save requested format */
      gst_query_parse_position (query, &format, NULL);

      /* query peer for current position in time */
      gst_query_set_position (query, GST_FORMAT_TIME, -1);

      if ((peer = gst_pad_get_peer (ivorbisfile->sinkpad)) == NULL)
        goto error;

      if (!gst_pad_query_position (peer, &rformat, &cur)) {
        GST_LOG_OBJECT (ivorbisfile, "query on peer pad failed");
        gst_object_unref (peer);
        goto error;
      }
      gst_object_unref (peer);

      if (format != rformat) {
        gst_ivorbisfile_src_convert (pad, rformat, cur, &format, &cur);
      }

      switch (format) {
        case GST_FORMAT_DEFAULT:
          if (ivorbisfile->vf.seekable)
            cur = ov_pcm_total (&ivorbisfile->vf, -1);
          else
            return FALSE;
          break;
        case GST_FORMAT_BYTES:
          if (ivorbisfile->vf.seekable)
            cur = ov_pcm_total (&ivorbisfile->vf, -1) * vi->channels * 2;
          else
            return FALSE;
          break;
        case GST_FORMAT_TIME:
          if (ivorbisfile->vf.seekable)
            cur = (gint64) (ov_time_total (&ivorbisfile->vf, -1) * GST_SECOND);
          else
            return FALSE;
          break;
        default:
          if (format == logical_stream_format) {
            if (ivorbisfile->vf.seekable)
              cur = ivorbisfile->vf.links;
            else
              return FALSE;
          } else
            res = FALSE;
          break;
      }

      gst_query_set_position (query, format, cur);

      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      /* peel off input */
      gst_query_parse_convert (query, &src_fmt, &src_val, NULL, NULL);
      if ((res = gst_ivorbisfile_src_convert (pad, src_fmt, src_val,
                  &dest_fmt, &dest_val))) {
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;

error:

  return FALSE;

}


static gboolean
gst_ivorbisfile_sink_event (GstPad * pad, GstEvent * event)
{

  Ivorbisfile *ivorbisfile;
  gboolean ret = TRUE;

  ivorbisfile = GST_IVORBISFILE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG ("eos");
      ivorbisfile->eos = TRUE;
      ivorbisfile->restart = TRUE;
      break;
    case GST_EVENT_NEWSEGMENT:
      GST_DEBUG ("discont");
      ivorbisfile->need_discont = TRUE;
      gst_event_unref (event);
      goto done;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);

done:
  gst_object_unref (ivorbisfile);

  return ret;

}


/* handle events on src pad */
static gboolean
gst_ivorbisfile_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  Ivorbisfile *ivorbisfile;

  ivorbisfile = GST_IVORBISFILE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 offset;
      vorbis_info *vi;
      GstFormat format;
      GstSeekFlags flags;

      GST_DEBUG ("ivorbisfile: handling seek event on pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      if (!ivorbisfile->vf.seekable) {
        gst_event_unref (event);
        GST_DEBUG ("vorbis stream is not seekable");
        gst_object_unref (ivorbisfile);
        return FALSE;
      }

      gst_event_parse_seek (event, NULL, &format, &flags, NULL, &offset, NULL,
          NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          ivorbisfile->seek_pending = TRUE;
          ivorbisfile->seek_value = offset;
          ivorbisfile->seek_format = format;
          ivorbisfile->seek_accurate = flags & GST_SEEK_FLAG_ACCURATE;
          break;
        case GST_FORMAT_BYTES:
          vi = ov_info (&ivorbisfile->vf, -1);
          if (vi->channels == 0) {
            GST_DEBUG ("vorbis stream has 0 channels ?");
            res = FALSE;
            goto done;
          }
          offset /= vi->channels * 2;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          ivorbisfile->seek_pending = TRUE;
          ivorbisfile->seek_value = offset;
          ivorbisfile->seek_format = format;
          ivorbisfile->seek_accurate = flags & GST_SEEK_FLAG_ACCURATE;
          break;
        default:
          if (format == logical_stream_format) {
            ivorbisfile->seek_pending = TRUE;
            ivorbisfile->seek_value = offset;
            ivorbisfile->seek_format = format;
            ivorbisfile->seek_accurate = flags & GST_SEEK_FLAG_ACCURATE;
          } else {
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
  gst_object_unref (ivorbisfile);
  return res;
}

static GstStateChangeReturn
gst_ivorbisfile_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  Ivorbisfile *ivorbisfile = GST_IVORBISFILE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ivorbisfile->total_bytes = 0;
      ivorbisfile->offset = 0;
      ivorbisfile->seek_pending = 0;
      ivorbisfile->need_discont = FALSE;
      if (ivorbisfile->metadata) {
        gst_caps_unref (ivorbisfile->metadata);
        ivorbisfile->metadata = NULL;
      }
      if (ivorbisfile->streaminfo) {
        gst_caps_unref (ivorbisfile->streaminfo);
        ivorbisfile->streaminfo = NULL;
      }
      ivorbisfile->current_link = -1;

      ivorbisfile->rate = -1;
      ivorbisfile->channels = -1;
      ivorbisfile->width = -1;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (ivorbisfile->adapter) {
        gst_adapter_clear (ivorbisfile->adapter);
      }
      ivorbisfile->restart = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      ivorbisfile->eos = FALSE;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      ov_clear (&ivorbisfile->vf);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }



  return ret;
}

static void
gst_ivorbisfile_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Ivorbisfile *ivorbisfile;

  g_return_if_fail (GST_IS_IVORBISFILE (object));

  ivorbisfile = GST_IVORBISFILE (object);

  switch (prop_id) {
    default:
      GST_WARNING ("Unknown property id\n");
  }
}

static void
gst_ivorbisfile_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Ivorbisfile *ivorbisfile;

  g_return_if_fail (GST_IS_IVORBISFILE (object));

  ivorbisfile = GST_IVORBISFILE (object);

  switch (prop_id) {
    case ARG_METADATA:
      g_value_set_boxed (value, ivorbisfile->metadata);
      break;
    case ARG_STREAMINFO:
      g_value_set_boxed (value, ivorbisfile->streaminfo);
      break;
    default:
      GST_WARNING ("Unknown property id\n");
  }
}
