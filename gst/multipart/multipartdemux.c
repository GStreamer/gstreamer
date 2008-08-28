/* GStreamer
 * Copyright (C) 2006 Sjoerd Simons <sjoerd@luon.net>
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstmultipartdemux.c: multipart stream demuxer
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
 * SECTION:element-multipartdemux
 * @short_description: Demuxer that takes a multipart digital stream as input
 * and demuxes one or many digital streams from it.
 * @see_also: #GstMultipartMux
 *
 * <refsect2>
 * <para>
 * MultipartDemux uses the Content-type field of incoming buffers to demux and 
 * push data to dynamic source pads. Most of the time multipart streams are 
 * sequential JPEG frames generated from a live source such as a network source
 * or a camera.
 * </para>
 * <title>Sample pipelines</title>
 * <para>
 * Here is a simple pipeline to demux a multipart file muxed with
 * #GstMultipartMux containing JPEG frames:
 * <programlisting>
 * gst-launch filesrc location=/tmp/test.multipart ! multipartdemux ! jpegdec ! ffmpegcolorspace ! ximagesink
 * </programlisting>
 * </para>
 * <para>
 * The output buffers of the multipartdemux typically have no timestamps and are usually 
 * played as fast as possible (at the rate that the source provides the data).
 * </para>
 * <para>
 * the content in multipart files is separated with a boundary string that can be 
 * configured specifically with the "boundary" property otherwise it will be 
 * autodetected. 
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "multipartdemux.h"

GST_DEBUG_CATEGORY_STATIC (gst_multipart_demux_debug);
#define GST_CAT_DEFAULT gst_multipart_demux_debug

/* elementfactory information */
static const GstElementDetails gst_multipart_demux_details =
GST_ELEMENT_DETAILS ("Multipart demuxer",
    "Codec/Demuxer",
    "demux multipart streams",
    "Wim Taymans <wim@fluendo.com>, Sjoerd Simons <sjoerd@luon.net>");


/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_AUTOSCAN	FALSE
#define DEFAULT_BOUNDARY	NULL

enum
{
  PROP_0,
  PROP_AUTOSCAN,
  PROP_BOUNDARY
};

static GstStaticPadTemplate multipart_demux_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate multipart_demux_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("multipart/x-mixed-replace")
    );

typedef struct
{
  const gchar *key;
  const gchar *val;
} GstNamesMap;

/* convert from mime types to gst structure names. Add more when needed. */
static const GstNamesMap gstnames[] = {
  /* RFC 2046 says audio/basic is mulaw, mono, 8000Hz */
  {"audio/basic", "audio/x-mulaw, channels=1, rate=8000"},
  {"audio/G726-16", "audio/x-adpcm, bitrate=16000"},
  {"audio/G726-24", "audio/x-adpcm, bitrate=24000"},
  {"audio/G726-32", "audio/x-adpcm, bitrate=32000"},
  {"audio/G726-40", "audio/x-adpcm, bitrate=40000"},
  {NULL, NULL}
};


static GstFlowReturn gst_multipart_demux_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn gst_multipart_demux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_multipart_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_multipart_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_multipart_demux_finalize (GObject * object);

GST_BOILERPLATE (GstMultipartDemux, gst_multipart_demux, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_multipart_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&multipart_demux_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&multipart_demux_src_template_factory));
  gst_element_class_set_details (element_class, &gst_multipart_demux_details);
}

static void
gst_multipart_demux_class_init (GstMultipartDemuxClass * klass)
{
  int i;

  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_multipart_demux_finalize;
  gobject_class->set_property = gst_multipart_set_property;
  gobject_class->get_property = gst_multipart_get_property;

  g_object_class_install_property (gobject_class, PROP_BOUNDARY,
      g_param_spec_string ("boundary", "Boundary",
          "The boundary string separating data, automatic if NULL",
          DEFAULT_BOUNDARY, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_AUTOSCAN,
      g_param_spec_boolean ("autoscan", "autoscan",
          "Try to autofind the prefix (deprecated unused, see boundary)",
          DEFAULT_AUTOSCAN, G_PARAM_READWRITE));

  /* populate gst names and mime types pairs */
  klass->gstnames = g_hash_table_new (g_str_hash, g_str_equal);
  for (i = 0; gstnames[i].key; i++) {
    g_hash_table_insert (klass->gstnames, (gpointer) gstnames[i].key,
        (gpointer) gstnames[i].val);
  }

  gstelement_class->change_state = gst_multipart_demux_change_state;
}

static void
gst_multipart_demux_init (GstMultipartDemux * multipart,
    GstMultipartDemuxClass * g_class)
{
  /* create the sink pad */
  multipart->sinkpad =
      gst_pad_new_from_static_template (&multipart_demux_sink_template_factory,
      "sink");
  gst_element_add_pad (GST_ELEMENT_CAST (multipart), multipart->sinkpad);
  gst_pad_set_chain_function (multipart->sinkpad,
      GST_DEBUG_FUNCPTR (gst_multipart_demux_chain));

  multipart->adapter = gst_adapter_new ();
  multipart->boundary = DEFAULT_BOUNDARY;
  multipart->mime_type = NULL;
  multipart->content_length = -1;
  multipart->header_completed = FALSE;
  multipart->scanpos = 0;
  multipart->autoscan = DEFAULT_AUTOSCAN;
}


static void
gst_multipart_demux_finalize (GObject * object)
{
  GstMultipartDemux *demux = GST_MULTIPART_DEMUX (object);

  g_object_unref (demux->adapter);
  g_free (demux->boundary);
  g_free (demux->mime_type);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const gchar *
gst_multipart_demux_get_gstname (GstMultipartDemux * demux, gchar * mimetype)
{
  GstMultipartDemuxClass *klass;
  const gchar *gstname;

  klass = GST_MULTIPART_DEMUX_GET_CLASS (demux);

  /* use hashtable to convert to gst name */
  gstname = g_hash_table_lookup (klass->gstnames, mimetype);
  if (gstname == NULL) {
    /* no gst name mapping, use mime type */
    gstname = mimetype;
  }
  GST_DEBUG_OBJECT (demux, "gst name for %s is %s", mimetype, gstname);
  return gstname;
}

static GstMultipartPad *
gst_multipart_find_pad_by_mime (GstMultipartDemux * demux, gchar * mime,
    gboolean * created)
{
  GSList *walk;

  walk = demux->srcpads;
  while (walk) {
    GstMultipartPad *pad = (GstMultipartPad *) walk->data;

    if (!strcmp (pad->mime, mime)) {
      if (created) {
        *created = FALSE;
      }
      return pad;
    }

    walk = walk->next;
  }
  /* pad not found, create it */
  {
    GstPad *pad;
    GstMultipartPad *mppad;
    gchar *name;
    const gchar *capsname;
    GstCaps *caps;

    mppad = g_new0 (GstMultipartPad, 1);

    GST_DEBUG_OBJECT (demux, "creating pad with mime: %s", mime);

    name = g_strdup_printf ("src_%d", demux->numpads);
    pad =
        gst_pad_new_from_static_template (&multipart_demux_src_template_factory,
        name);
    g_free (name);

    /* take the mime type, convert it to the caps name */
    capsname = gst_multipart_demux_get_gstname (demux, mime);
    caps = gst_caps_from_string (capsname);
    GST_DEBUG_OBJECT (demux, "caps for pad: %s", capsname);
    gst_pad_use_fixed_caps (pad);
    gst_pad_set_caps (pad, caps);
    gst_caps_unref (caps);

    mppad->pad = pad;
    mppad->mime = g_strdup (mime);

    demux->srcpads = g_slist_prepend (demux->srcpads, mppad);
    demux->numpads++;

    gst_pad_set_active (pad, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (demux), pad);

    if (created) {
      *created = TRUE;
    }

    return mppad;
  }
}

static gboolean
get_line_end (const guint8 * data, const guint8 * dataend, guint8 ** end,
    guint8 ** next)
{
  guint8 *x;
  gboolean foundr = FALSE;

  for (x = (guint8 *) data; x < dataend; x++) {
    if (*x == '\r') {
      foundr = TRUE;
    } else if (*x == '\n') {
      *end = x - (foundr ? 1 : 0);
      *next = x + 1;
      return TRUE;
    }
  }
  return FALSE;
}

static gint
multipart_parse_header (GstMultipartDemux * multipart)
{
  const guint8 *data;
  const guint8 *dataend;
  gchar *boundary;
  int boundary_len;
  int datalen;
  guint8 *pos;
  guint8 *end, *next;

  datalen = gst_adapter_available (multipart->adapter);
  data = gst_adapter_peek (multipart->adapter, datalen);
  dataend = data + datalen;

  /* Skip leading whitespace, pos endposition should at least leave space for
   * the boundary and a \n */
  for (pos = (guint8 *) data; pos < dataend - 4 && g_ascii_isspace (*pos);
      pos++);

  if (pos >= dataend - 4) {
    return MULTIPART_NEED_MORE_DATA;
  }

  if (G_UNLIKELY (pos[0] != '-' || pos[1] != '-')) {
    GST_DEBUG_OBJECT (multipart, "No boundary available");
    goto wrong_header;
  }

  /* First the boundary */
  if (!get_line_end (pos, dataend, &end, &next))
    return MULTIPART_NEED_MORE_DATA;

  /* Ignore the leading -- */
  boundary_len = end - pos - 2;
  boundary = (gchar *) pos + 2;
  if (boundary_len < 1) {
    GST_DEBUG_OBJECT (multipart, "No boundary available");
    goto wrong_header;
  }

  if (G_UNLIKELY (multipart->boundary == NULL)) {
    /* First time we see the boundary, copy it */
    multipart->boundary = g_strndup (boundary, boundary_len);
    multipart->boundary_len = boundary_len;
  } else if (G_UNLIKELY (boundary_len != multipart->boundary_len)) {
    /* Something odd is going on, either the boundary indicated EOS or it's
     * invalid */
    if (G_UNLIKELY (boundary_len == multipart->boundary_len + 2 &&
            !strncmp (boundary, multipart->boundary, multipart->boundary_len) &&
            !strncmp (boundary + multipart->boundary_len, "--", 2))) {
      return MULTIPART_DATA_EOS;
    }
    GST_DEBUG_OBJECT (multipart,
        "Boundary length doesn't match detected boundary (%d <> %d",
        boundary_len, multipart->boundary_len);
    goto wrong_header;
  } else if (G_UNLIKELY (strncmp (boundary, multipart->boundary, boundary_len))) {
    GST_DEBUG_OBJECT (multipart, "Boundary doesn't match previous boundary");
    goto wrong_header;
  }


  pos = next;
  while (get_line_end (pos, dataend, &end, &next)) {
    guint len = end - pos;

    if (len == 0) {
      /* empty line, data starts behind us */
      GST_DEBUG_OBJECT (multipart,
          "Parsed the header - boundary: %s, mime-type: %s, content-length: %d",
          multipart->boundary, multipart->mime_type, multipart->content_length);
      return next - data;
    }

    if (len >= 14 && !g_ascii_strncasecmp ("content-type:", (gchar *) pos, 13)) {
      g_free (multipart->mime_type);
      multipart->mime_type = g_strndup ((gchar *) pos + 14, len - 14);
    } else if (len >= 15 &&
        !g_ascii_strncasecmp ("content-length:", (gchar *) pos, 15)) {
      multipart->content_length =
          g_ascii_strtoull ((gchar *) pos + 15, NULL, 10);
    }
    pos = next;
  }
  GST_DEBUG_OBJECT (multipart, "Need more data for the header");
  return MULTIPART_NEED_MORE_DATA;

wrong_header:
  {
    GST_ELEMENT_ERROR (multipart, STREAM, DEMUX, (NULL),
        ("Boundary not found in the multipart header"));
    return MULTIPART_DATA_ERROR;
  }
}

static gint
multipart_find_boundary (GstMultipartDemux * multipart, gint * datalen)
{
  /* Adaptor is positioned at the start of the data */
  const guint8 *data, *pos;
  const guint8 *dataend;
  gint len;

  if (multipart->content_length >= 0) {
    /* fast path, known content length :) */
    len = multipart->content_length;
    if (gst_adapter_available (multipart->adapter) >= len + 2) {
      *datalen = len;
      data = gst_adapter_peek (multipart->adapter, len + 1);

      /* If data[len] contains \r then assume a newline is \r\n */
      if (data[len] == '\r')
        len += 2;
      else if (data[len] == '\n')
        len += 1;
      /* Don't check if boundary is actually there, but let the header parsing 
       * bail out if it isn't */
      return len;
    } else {
      /* need more data */
      return MULTIPART_NEED_MORE_DATA;
    }
  }

  len = gst_adapter_available (multipart->adapter);
  if (len == 0)
    return MULTIPART_NEED_MORE_DATA;
  data = gst_adapter_peek (multipart->adapter, len);
  dataend = data + len;

  for (pos = data + multipart->scanpos;
      pos <= dataend - multipart->boundary_len - 2; pos++) {
    if (*pos == '-' && pos[1] == '-' &&
        !strncmp ((gchar *) pos + 2,
            multipart->boundary, multipart->boundary_len)) {
      /* Found the boundary! Check if there was a newline before the boundary */
      len = pos - data;
      if (pos - 2 > data && pos[-2] == '\r')
        len -= 2;
      else if (pos - 1 > data && pos[-1] == '\n')
        len -= 1;
      *datalen = len;

      multipart->scanpos = 0;
      return pos - data;
    }
  }
  multipart->scanpos = pos - data;
  return MULTIPART_NEED_MORE_DATA;
}

static GstFlowReturn
gst_multipart_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstMultipartDemux *multipart;
  GstAdapter *adapter;
  gint size = 1;
  GstFlowReturn res;

  multipart = GST_MULTIPART_DEMUX (gst_pad_get_parent (pad));
  adapter = multipart->adapter;

  res = GST_FLOW_OK;

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (adapter);
  }
  gst_adapter_push (adapter, buf);

  while (gst_adapter_available (adapter) > 0) {
    GstMultipartPad *srcpad;
    GstBuffer *outbuf;
    gboolean created;
    gint datalen;

    if (G_UNLIKELY (!multipart->header_completed)) {
      if ((size = multipart_parse_header (multipart)) < 0) {
        goto nodata;
      } else {
        gst_adapter_flush (adapter, size);
        multipart->header_completed = TRUE;
      }
    }
    if ((size = multipart_find_boundary (multipart, &datalen)) < 0) {
      goto nodata;
    }
    srcpad =
        gst_multipart_find_pad_by_mime (multipart,
        multipart->mime_type, &created);
    outbuf = gst_adapter_take_buffer (adapter, datalen);
    gst_adapter_flush (adapter, size - datalen);

    /* Invalidate header info */
    multipart->header_completed = FALSE;
    multipart->content_length = -1;

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (srcpad->pad));
    if (created) {
      GstEvent *event;

      /* Push new segment, first buffer has 0 timestamp */
      event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0);
      gst_pad_push_event (srcpad->pad, event);
      GST_BUFFER_TIMESTAMP (outbuf) = 0;
    } else {
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
    }
    GST_DEBUG_OBJECT (multipart,
        "pushing buffer with timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
    GST_DEBUG_OBJECT (multipart, "buffer has caps %s",
        gst_caps_to_string (GST_BUFFER_CAPS (outbuf)));
    res = gst_pad_push (srcpad->pad, outbuf);
    if (res != GST_FLOW_OK)
      break;
  }

nodata:
  gst_object_unref (multipart);

  if (G_UNLIKELY (size == MULTIPART_DATA_ERROR))
    return GST_FLOW_ERROR;
  if (G_UNLIKELY (size == MULTIPART_DATA_EOS))
    return GST_FLOW_UNEXPECTED;

  return res;
}

static GstStateChangeReturn
gst_multipart_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMultipartDemux *multipart;
  GstStateChangeReturn ret;

  multipart = GST_MULTIPART_DEMUX (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      multipart->header_completed = FALSE;
      g_free (multipart->boundary);
      multipart->boundary = NULL;
      g_free (multipart->mime_type);
      multipart->mime_type = NULL;
      gst_adapter_clear (multipart->adapter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}


static void
gst_multipart_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultipartDemux *filter;

  g_return_if_fail (GST_IS_MULTIPART_DEMUX (object));
  filter = GST_MULTIPART_DEMUX (object);

  switch (prop_id) {
    case PROP_BOUNDARY:
      /* Not really that usefull anymore as we can reliably autoscan */
      g_free (filter->boundary);
      filter->boundary = g_value_dup_string (value);
      if (filter->boundary != NULL) {
        filter->boundary_len = strlen (filter->boundary);
      }
      break;
    case PROP_AUTOSCAN:
      filter->autoscan = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multipart_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultipartDemux *filter;

  g_return_if_fail (GST_IS_MULTIPART_DEMUX (object));
  filter = GST_MULTIPART_DEMUX (object);

  switch (prop_id) {
    case PROP_BOUNDARY:
      g_value_set_string (value, filter->boundary);
      break;
    case PROP_AUTOSCAN:
      g_value_set_boolean (value, filter->autoscan);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}



gboolean
gst_multipart_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_multipart_demux_debug,
      "multipartdemux", 0, "multipart demuxer");

  return gst_element_register (plugin, "multipartdemux", GST_RANK_PRIMARY,
      GST_TYPE_MULTIPART_DEMUX);
}
