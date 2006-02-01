/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2001 Thomas <thomas@apestaart.org>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * adder.c: Adder element, N in, one out, samples are added
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
/* Element-Checklist-Version: 5 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstadder.h"
#include <gst/audio/audio.h>
#include <string.h>             /* strcmp */

/* highest positive/lowest negative x-bit value we can use for clamping */
#define MAX_INT_32  ((gint32) (0x7fffffff))
#define MAX_INT_16  ((gint16) (0x7fff))
#define MAX_INT_8   ((gint8)  (0x7f))
#define MAX_UINT_32 ((guint32)(0xffffffff))
#define MAX_UINT_16 ((guint16)(0xffff))
#define MAX_UINT_8  ((guint8) (0xff))

#define MIN_INT_32  ((gint32) (0x80000000))
#define MIN_INT_16  ((gint16) (0x8000))
#define MIN_INT_8   ((gint8)  (0x80))
#define MIN_UINT_32 ((guint32)(0x00000000))
#define MIN_UINT_16 ((guint16)(0x0000))
#define MIN_UINT_8  ((guint8) (0x00))

GST_DEBUG_CATEGORY_STATIC (gst_adder_debug);
#define GST_CAT_DEFAULT gst_adder_debug

/* elementfactory information */
static GstElementDetails adder_details = GST_ELEMENT_DETAILS ("Adder",
    "Generic/Audio",
    "Add N audio channels together",
    "Thomas <thomas@apestaart.org>");

static GstStaticPadTemplate gst_adder_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
        GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS)
    );

static GstStaticPadTemplate gst_adder_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
        GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS)
    );

static void gst_adder_class_init (GstAdderClass * klass);
static void gst_adder_init (GstAdder * adder);
static void gst_adder_dispose (GObject * object);

static GstPad *gst_adder_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused);
static GstStateChangeReturn gst_adder_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_adder_setcaps (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_adder_collected (GstCollectPads * pads,
    gpointer user_data);

static GstElementClass *parent_class = NULL;

GType
gst_adder_get_type (void)
{
  static GType adder_type = 0;

  if (!adder_type) {
    static const GTypeInfo adder_info = {
      sizeof (GstAdderClass), NULL, NULL,
      (GClassInitFunc) gst_adder_class_init, NULL, NULL,
      sizeof (GstAdder), 0,
      (GInstanceInitFunc) gst_adder_init,
    };

    adder_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAdder",
        &adder_info, 0);
    GST_DEBUG_CATEGORY_INIT (gst_adder_debug, "adder", 0,
        "audio channel mixing element");
  }
  return adder_type;
}

#define MAKE_FUNC(name,type,ttype,min,max)                      \
static void name (type *out, type *in, gint bytes) {            \
  gint i;                                                       \
  for (i = 0; i < bytes / sizeof (type); i++)                   \
    out[i] = CLAMP ((ttype)out[i] + (ttype)in[i], min, max);    \
}

MAKE_FUNC (add_int32, gint32, gint64, MIN_INT_32, MAX_INT_32)
    MAKE_FUNC (add_int16, gint16, gint32, MIN_INT_16, MAX_INT_16)
    MAKE_FUNC (add_int8, gint8, gint16, MIN_INT_8, MAX_INT_8)
    MAKE_FUNC (add_uint32, guint32, guint64, MIN_UINT_32, MAX_UINT_32)
    MAKE_FUNC (add_uint16, guint16, guint32, MIN_UINT_16, MAX_UINT_16)
    MAKE_FUNC (add_uint8, guint8, guint16, MIN_UINT_8, MAX_UINT_8)
    MAKE_FUNC (add_float64, gdouble, gdouble, -1.0, 1.0)
    MAKE_FUNC (add_float32, gfloat, gfloat, -1.0, 1.0)

     static gboolean gst_adder_setcaps (GstPad * pad, GstCaps * caps)
{
  GstAdder *adder;
  GList *pads;
  GstStructure *structure;
  const char *media_type;

  adder = GST_ADDER (GST_PAD_PARENT (pad));

  /* see if the other pads can accept the format */
  GST_OBJECT_LOCK (adder);
  pads = GST_ELEMENT (adder)->pads;
  while (pads) {
    GstPad *otherpad = GST_PAD (pads->data);

    if (otherpad != pad) {
      gst_caps_replace (&GST_PAD_CAPS (otherpad), caps);
    }
    pads = g_list_next (pads);
  }
  GST_OBJECT_UNLOCK (adder);

  /* parse caps now */
  structure = gst_caps_get_structure (caps, 0);
  media_type = gst_structure_get_name (structure);
  if (strcmp (media_type, "audio/x-raw-int") == 0) {
    GST_DEBUG_OBJECT (adder, "parse_caps sets adder to format int");
    adder->format = GST_ADDER_FORMAT_INT;
    gst_structure_get_int (structure, "width", &adder->width);
    gst_structure_get_int (structure, "depth", &adder->depth);
    gst_structure_get_int (structure, "endianness", &adder->endianness);
    gst_structure_get_boolean (structure, "signed", &adder->is_signed);

    if (adder->endianness != G_BYTE_ORDER)
      goto not_supported;

    switch (adder->width) {
      case 8:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int8 : (GstAdderFunction) add_uint8);
        break;
      case 16:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int16 : (GstAdderFunction) add_uint16);
        break;
      case 32:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int32 : (GstAdderFunction) add_uint32);
        break;
      default:
        goto not_supported;
    }
  } else if (strcmp (media_type, "audio/x-raw-float") == 0) {
    GST_DEBUG_OBJECT (adder, "parse_caps sets adder to format float");
    adder->format = GST_ADDER_FORMAT_FLOAT;
    gst_structure_get_int (structure, "width", &adder->width);

    switch (adder->width) {
      case 32:
        adder->func = (GstAdderFunction) add_float32;
        break;
      case 64:
        adder->func = (GstAdderFunction) add_float64;
        break;
      default:
        goto not_supported;
    }
  } else {
    goto not_supported;
  }

  gst_structure_get_int (structure, "channels", &adder->channels);
  gst_structure_get_int (structure, "rate", &adder->rate);

  return TRUE;

not_supported:
  {
    return FALSE;
  }
}

static gboolean
gst_adder_query (GstPad * pad, GstQuery * query)
{
  GstAdder *adder = GST_ADDER (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_position (query, GST_FORMAT_TIME, adder->timestamp);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, GST_FORMAT_DEFAULT, adder->offset);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
      /* FIXME: what to do about the length? query all pads upstream and
       * pick the longest length? or the shortest length? or what? */
    case GST_QUERY_DURATION:
      res = FALSE;
      break;
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (adder);
  return res;
}

static void
gst_adder_class_init (GstAdderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = gst_adder_dispose;

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_sink_template));
  gst_element_class_set_details (gstelement_class, &adder_details);

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->request_new_pad = gst_adder_request_new_pad;
  gstelement_class->change_state = gst_adder_change_state;
}

static void
gst_adder_init (GstAdder * adder)
{
  adder->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_adder_src_template), "src");
  gst_pad_set_getcaps_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_setcaps_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_adder_setcaps));
  gst_pad_set_query_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_adder_query));
  gst_element_add_pad (GST_ELEMENT (adder), adder->srcpad);

  adder->format = GST_ADDER_FORMAT_UNSET;
  adder->numpads = 0;
  adder->func = NULL;

  /* keep track of the sinkpads requested */
  adder->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (adder->collect, gst_adder_collected, adder);
}

static void
gst_adder_dispose (GObject * object)
{
  GstAdder *adder = GST_ADDER (object);

  gst_object_unref (adder->collect);
  adder->collect = NULL;
}

static GstPad *
gst_adder_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
{
  gchar *name;
  GstAdder *adder;
  GstPad *newpad;

  g_return_val_if_fail (GST_IS_ADDER (element), NULL);

  if (templ->direction != GST_PAD_SINK)
    goto not_sink;

  adder = GST_ADDER (element);

  name = g_strdup_printf ("sink%d", adder->numpads);
  newpad = gst_pad_new_from_template (templ, name);

  gst_pad_set_getcaps_function (newpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_setcaps_function (newpad, GST_DEBUG_FUNCPTR (gst_adder_setcaps));
  gst_collect_pads_add_pad (adder->collect, newpad, sizeof (GstCollectData));
  if (!gst_element_add_pad (GST_ELEMENT (adder), newpad))
    goto could_not_add;

  adder->numpads++;

  return newpad;

  /* errors */
not_sink:
  {
    g_warning ("gstadder: request new pad that is not a SINK pad\n");
    return NULL;
  }
could_not_add:
  {
    gst_collect_pads_remove_pad (adder->collect, newpad);
    gst_object_unref (newpad);
    return NULL;
  }
}

static GstFlowReturn
gst_adder_collected (GstCollectPads * pads, gpointer user_data)
{
  /*
   * combine channels by adding sample values
   * basic algorithm :
   * - this function is called when all pads have a buffer
   * - get available bytes on all pads.
   * - repeat for each input pad :
   *   - read available bytes, copy or add to target buffer
   *   - if there's an EOS event, remove the input channel
   * - push out the output buffer
   */
  GstAdder *adder;
  guint size;
  GSList *collected;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gpointer outbytes;

  adder = GST_ADDER (user_data);

  /* get available bytes for reading */
  size = gst_collect_pads_available (pads);
  if (size == 0)
    return GST_FLOW_OK;

  outbuf = NULL;
  outbytes = NULL;

  if (adder->func == NULL)
    goto not_negotiated;

  GST_LOG_OBJECT (adder,
      "starting to cycle through channels, collecting %d bytes", size);

  for (collected = pads->data; collected; collected = g_slist_next (collected)) {
    GstCollectData *data;
    guint8 *bytes;
    guint len;

    data = (GstCollectData *) collected->data;

    GST_LOG_OBJECT (adder, "looking into channel %p", data);

    /* get pointer to copy size bytes */
    len = gst_collect_pads_read (pads, data, &bytes, size);
    if (len == 0)
      continue;

    GST_LOG_OBJECT (adder, " copying %d bytes (format %d,%d)",
        len, adder->format, adder->width);
    GST_LOG_OBJECT (adder, " from channel %p from input data %p", data, bytes);

    if (outbuf == NULL) {
      /* first buffer, alloc size bytes */
      outbuf = gst_buffer_new_and_alloc (size);
      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (adder->srcpad));
      outbytes = GST_BUFFER_DATA (outbuf);

      memset (outbytes, 0, size);

      /* and copy the data into it */
      memcpy (outbytes, bytes, len);
    } else {
      /* other buffers, need to add them */
      adder->func ((gpointer) outbytes, (gpointer) bytes, len);
    }
    gst_collect_pads_flush (pads, data, len);
  }

  /* set timestamps on the output buffer */
  {
    guint64 samples;
    guint64 duration;

    /* width is in bits and we need bytes */
    samples = size / ((adder->width / 8) * adder->channels);
    duration = samples * GST_SECOND / adder->rate;

    GST_BUFFER_TIMESTAMP (outbuf) = adder->timestamp;
    GST_BUFFER_OFFSET (outbuf) = adder->offset;

    adder->offset += samples;
    adder->timestamp = adder->offset * GST_SECOND / adder->rate;

    GST_BUFFER_DURATION (outbuf) = adder->timestamp -
        GST_BUFFER_TIMESTAMP (outbuf);
  }

  /* send it out */
  GST_LOG_OBJECT (adder, "pushing outbuf");
  ret = gst_pad_push (adder->srcpad, outbuf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstStateChangeReturn
gst_adder_change_state (GstElement * element, GstStateChange transition)
{
  GstAdder *adder;
  GstStateChangeReturn ret;

  adder = GST_ADDER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      adder->timestamp = 0;
      adder->offset = 0;
      gst_collect_pads_start (adder->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (adder->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "adder", GST_RANK_NONE, GST_TYPE_ADDER)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "adder",
    "Adds multiple streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
