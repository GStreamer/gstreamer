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
 * @see_also: #GstMultipartMux
 *
 * MultipartDemux uses the Content-type field of incoming buffers to demux and 
 * push data to dynamic source pads. Most of the time multipart streams are 
 * sequential JPEG frames generated from a live source such as a network source
 * or a camera.
 *
 * The output buffers of the multipartdemux typically have no timestamps and are
 * usually played as fast as possible (at the rate that the source provides the
 * data).
 *
 * the content in multipart files is separated with a boundary string that can
 * be configured specifically with the #GstMultipartDemux:boundary property
 * otherwise it will be autodetected.
 *
 * <refsect2>
 * <title>Sample pipelines</title>
 * |[
 * gst-launch filesrc location=/tmp/test.multipart ! multipartdemux ! jpegdec ! ffmpegcolorspace ! ximagesink
 * ]| a simple pipeline to demux a multipart file muxed with #GstMultipartMux
 * containing JPEG frames.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "multipartdemux.h"

GST_DEBUG_CATEGORY_STATIC (gst_multipart_demux_debug);
#define GST_CAT_DEFAULT gst_multipart_demux_debug

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_AUTOSCAN		FALSE
#define DEFAULT_BOUNDARY		NULL
#define DEFAULT_SINGLE_STREAM	FALSE

enum
{
  PROP_0,
  PROP_AUTOSCAN,
  PROP_BOUNDARY,
  PROP_SINGLE_STREAM
};

static GstStaticPadTemplate multipart_demux_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src_%d",
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

/* convert from mime types to gst structure names. Add more when needed. The
 * mime-type is stored as lowercase */
static const GstNamesMap gstnames[] = {
  /* RFC 2046 says audio/basic is mulaw, mono, 8000Hz */
  {"audio/basic", "audio/x-mulaw, channels=1, rate=8000"},
  {"audio/g726-16",
      "audio/x-adpcm, bitrate=16000, layout=g726, channels=1, rate=8000"},
  {"audio/g726-24",
      "audio/x-adpcm, bitrate=24000, layout=g726, channels=1, rate=8000"},
  {"audio/g726-32",
      "audio/x-adpcm, bitrate=32000, layout=g726, channels=1, rate=8000"},
  {"audio/g726-40",
      "audio/x-adpcm, bitrate=40000, layout=g726, channels=1, rate=8000"},
  /* Panasonic Network Cameras non-standard types */
  {"audio/g726",
      "audio/x-adpcm, bitrate=32000, layout=g726, channels=1, rate=8000"},
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

  gst_element_class_add_static_pad_template (element_class,
      &multipart_demux_sink_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &multipart_demux_src_template_factory);
  gst_element_class_set_details_simple (element_class, "Multipart demuxer",
      "Codec/Demuxer",
      "demux multipart streams",
      "Wim Taymans <wim.taymans@gmail.com>, Sjoerd Simons <sjoerd@luon.net>");
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
          DEFAULT_BOUNDARY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTOSCAN,
      g_param_spec_boolean ("autoscan", "autoscan",
          "Try to autofind the prefix (deprecated unused, see boundary)",
          DEFAULT_AUTOSCAN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMultipartDemux::single-stream:
   *
   * Assume that there is only one stream whose content-type will
   * not change and emit no-more-pads as soon as the first boundary
   * content is parsed, decoded, and pads are linked.
   *
   * Since: 0.10.31
   */
  g_object_class_install_property (gobject_class, PROP_SINGLE_STREAM,
      g_param_spec_boolean ("single-stream", "Single Stream",
          "Assume that there is only one stream whose content-type will not change and emit no-more-pads as soon as the first boundary content is parsed, decoded, and pads are linked",
          DEFAULT_SINGLE_STREAM, G_PARAM_READWRITE));

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
  multipart->singleStream = DEFAULT_SINGLE_STREAM;
}

static void
gst_multipart_pad_free (GstMultipartPad * mppad)
{
  g_free (mppad->mime);
  g_free (mppad);
}

static void
gst_multipart_demux_finalize (GObject * object)
{
  GstMultipartDemux *demux = GST_MULTIPART_DEMUX (object);

  g_object_unref (demux->adapter);
  g_free (demux->boundary);
  g_free (demux->mime_type);
  g_slist_foreach (demux->srcpads, (GFunc) gst_multipart_pad_free, NULL);
  g_slist_free (demux->srcpads);

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

static GstFlowReturn
gst_multipart_combine_flows (GstMultipartDemux * demux, GstMultipartPad * pad,
    GstFlowReturn ret)
{
  GSList *walk;

  /* store the value */
  pad->last_ret = ret;

  /* any other error that is not-linked can be returned right
   * away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
    GstMultipartPad *opad = (GstMultipartPad *) walk->data;

    ret = opad->last_ret;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  return ret;
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
    mppad->last_ret = GST_FLOW_OK;

    demux->srcpads = g_slist_prepend (demux->srcpads, mppad);
    demux->numpads++;

    gst_pad_set_active (pad, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (demux), pad);

    if (created) {
      *created = TRUE;
    }

    if (demux->singleStream) {
      gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
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

static guint
get_mime_len (const guint8 * data, guint maxlen)
{
  guint8 *x;

  x = (guint8 *) data;
  while (*x != '\0' && *x != '\r' && *x != '\n' && *x != ';') {
    x++;
  }
  return x - data;
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
      guint mime_len;

      /* only take the mime type up to the first ; if any. After ; there can be
       * properties that we don't handle yet. */
      mime_len = get_mime_len (pos + 14, len - 14);

      g_free (multipart->mime_type);
      multipart->mime_type = g_ascii_strdown ((gchar *) pos + 14, mime_len);
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
  GstClockTime timestamp;
  gint size = 1;
  GstFlowReturn res;

  multipart = GST_MULTIPART_DEMUX (gst_pad_get_parent (pad));
  adapter = multipart->adapter;

  res = GST_FLOW_OK;

  timestamp = GST_BUFFER_TIMESTAMP (buf);

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

    /* Invalidate header info */
    multipart->header_completed = FALSE;
    multipart->content_length = -1;

    if (G_UNLIKELY (datalen <= 0)) {
      GST_DEBUG_OBJECT (multipart, "skipping empty content.");
      gst_adapter_flush (adapter, size - datalen);
    } else {
      srcpad =
          gst_multipart_find_pad_by_mime (multipart,
          multipart->mime_type, &created);
      outbuf = gst_adapter_take_buffer (adapter, datalen);
      gst_adapter_flush (adapter, size - datalen);

      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (srcpad->pad));
      if (created) {
        GstTagList *tags;

        /* Push new segment, first buffer has 0 timestamp */
        gst_pad_push_event (srcpad->pad,
            gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));

        tags =
            gst_tag_list_new_full (GST_TAG_CONTAINER_FORMAT, "Multipart", NULL);
        gst_pad_push_event (srcpad->pad, gst_event_new_tag (tags));

        GST_BUFFER_TIMESTAMP (outbuf) = 0;
      } else {
        GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      }
      GST_DEBUG_OBJECT (multipart,
          "pushing buffer with timestamp %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
      GST_DEBUG_OBJECT (multipart, "buffer has caps %" GST_PTR_FORMAT,
          GST_BUFFER_CAPS (outbuf));
      res = gst_pad_push (srcpad->pad, outbuf);
      res = gst_multipart_combine_flows (multipart, srcpad, res);
      if (res != GST_FLOW_OK)
        break;
    }
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
    case PROP_SINGLE_STREAM:
      filter->singleStream = g_value_get_boolean (value);
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
    case PROP_SINGLE_STREAM:
      g_value_set_boolean (value, filter->singleStream);
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
