/* GStreamer TTA plugin
 * (c) 2004 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * gstttaparse.c: TTA file parser
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

#include <gst/gst.h>

#include <math.h>

#include "gstttaparse.h"
#include "ttadec.h"
#include "crc32.h"

GST_DEBUG_CATEGORY_STATIC (gst_tta_parse_debug);
#define GST_CAT_DEFAULT gst_tta_parse_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ttafile")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tta, "
        "width = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, " "rate = (int) [ 8000, 96000 ]")
    );

static void gst_tta_parse_class_init (GstTtaParseClass * klass);
static void gst_tta_parse_base_init (GstTtaParseClass * klass);
static void gst_tta_parse_init (GstTtaParse * ttaparse);

static void gst_tta_parse_chain (GstPad * pad, GstData * in);

static GstElementClass *parent = NULL;

GType
gst_tta_parse_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstTtaParseClass),
      (GBaseInitFunc) gst_tta_parse_base_init,
      NULL,
      (GClassInitFunc) gst_tta_parse_class_init,
      NULL,
      NULL,
      sizeof (GstTtaParse),
      0,
      (GInstanceInitFunc) gst_tta_parse_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstTtaParse", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_tta_parse_base_init (GstTtaParseClass * klass)
{
  static GstElementDetails plugin_details = {
    "TTA file parser",
    "Codec/Demuxer/Audio",
    "Parses TTA files",
    "Arwed v. Merkatz <v.merkatz@gmx.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_tta_parse_dispose (GObject * object)
{
  GstTtaParse *ttaparse = GST_TTA_PARSE (object);

  g_free (ttaparse->index);

  G_OBJECT_CLASS (parent)->dispose (object);
}

static void
gst_tta_parse_class_init (GstTtaParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_tta_parse_dispose;
}

static gboolean
gst_tta_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstTtaParse *ttaparse = GST_TTA_PARSE (gst_pad_get_parent (pad));

  if (type == GST_QUERY_TOTAL) {
    if (*format == GST_FORMAT_TIME) {
      if ((ttaparse->data_length == 0) || (ttaparse->samplerate == 0)) {
        *value = 0;
        return FALSE;
      }
      *value =
          ((gdouble) ttaparse->data_length / (gdouble) ttaparse->samplerate) *
          GST_SECOND;
      GST_DEBUG_OBJECT (ttaparse, "got queried for time, returned %lli",
          *value);
      return TRUE;
    }
  } else {
    return gst_pad_query_default (pad, type, format, value);
  }
  return FALSE;
}

static gboolean
gst_tta_src_event (GstPad * pad, GstEvent * event)
{
  GstTtaParse *ttaparse = GST_TTA_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      if (GST_EVENT_SEEK_FORMAT (event) == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (ttaparse, "got seek event");
        GstEvent *seek_event;
        guint64 time = GST_EVENT_SEEK_OFFSET (event);
        guint64 seek_frame = time / (FRAME_TIME * 1000000000);
        guint64 seekpos = ttaparse->index[seek_frame].pos;

        GST_DEBUG_OBJECT (ttaparse, "seeking to %u", (guint) seekpos);
        seek_event =
            gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_SET |
            GST_SEEK_FLAG_ACCURATE, seekpos);
        gst_event_unref (event);
        if (gst_pad_send_event (GST_PAD_PEER (ttaparse->sinkpad), seek_event)) {
          gst_pad_event_default (ttaparse->srcpad,
              gst_event_new (GST_EVENT_FLUSH));
          return TRUE;
        } else {
          GST_LOG_OBJECT (ttaparse, "seek failed");
          return FALSE;
        }
      } else {
        return gst_pad_send_event (pad, event);
      }
      break;
    }
    default:
      return gst_pad_send_event (pad, event);
      break;
  }
}

static void
gst_tta_parse_init (GstTtaParse * ttaparse)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ttaparse);

  ttaparse->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");

  ttaparse->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_explicit_caps (ttaparse->srcpad);
  gst_pad_set_query_function (ttaparse->srcpad, gst_tta_src_query);
  gst_pad_set_event_function (ttaparse->srcpad, gst_tta_src_event);

  gst_element_add_pad (GST_ELEMENT (ttaparse), ttaparse->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ttaparse), ttaparse->srcpad);
  gst_pad_set_chain_function (ttaparse->sinkpad, gst_tta_parse_chain);

  ttaparse->silent = FALSE;
  ttaparse->header_parsed = FALSE;
  ttaparse->partialbuf = NULL;
  ttaparse->seek_ok = FALSE;
  ttaparse->current_frame = 0;
  ttaparse->data_length = 0;
  ttaparse->samplerate = 0;

  GST_FLAG_SET (ttaparse, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_tta_handle_event (GstPad * pad, GstBuffer * buffer)
{
  GstEvent *event = GST_EVENT (buffer);
  GstTtaParse *ttaparse = GST_TTA_PARSE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (ttaparse, "got some event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
    {
      GstEvent *discont;
      guint64 offset = GST_EVENT_DISCONT_OFFSET (event, 0).value;
      int i;

      GST_DEBUG_OBJECT (ttaparse, "discont with offset: %u", offset);
      for (i = 0; i < ttaparse->num_frames; i++) {
        if (offset == ttaparse->index[i].pos) {
          GST_DEBUG_OBJECT (ttaparse, "setting current frame to %i", i);
          discont = gst_event_new_discontinuous (FALSE,
              GST_FORMAT_TIME, ttaparse->index[i].time, NULL);
          gst_event_unref (event);
          gst_buffer_unref (ttaparse->partialbuf);
          ttaparse->partialbuf = NULL;
          ttaparse->current_frame = i;
          gst_pad_event_default (pad, gst_event_new (GST_EVENT_FLUSH));
          gst_pad_event_default (pad, discont);
          GST_DEBUG_OBJECT (ttaparse, "sent discont event");
          return;
        }
      }
      break;
    }
    default:
      gst_pad_event_default (pad, event);
      break;
  }
}

static void
gst_tta_parse_chain (GstPad * pad, GstData * in)
{
  GstTtaParse *ttaparse;
  GstBuffer *outbuf, *buf = GST_BUFFER (in);
  guchar *data;
  gint i;
  guint64 size, offset = 0;
  GstCaps *caps;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  ttaparse = GST_TTA_PARSE (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_TTA_PARSE (ttaparse));

  if (GST_IS_EVENT (buf)) {
    gst_tta_handle_event (pad, buf);
    return;
  }

  if (ttaparse->partialbuf) {
    GstBuffer *newbuf;

    newbuf = gst_buffer_merge (ttaparse->partialbuf, buf);
    gst_buffer_unref (buf);
    gst_buffer_unref (ttaparse->partialbuf);
    ttaparse->partialbuf = newbuf;
  } else {
    ttaparse->partialbuf = buf;
  }

  size = GST_BUFFER_SIZE (ttaparse->partialbuf);
  data = GST_BUFFER_DATA (ttaparse->partialbuf);
  if (!ttaparse->header_parsed) {
    if ((*data == 'T') && (*(data + 1) == 'T') && (*(data + 2) == 'A')) {
      double frame_length;
      int num_frames;
      guint32 datasize = 0;
      guint32 crc;

      offset = offset + 4;
      offset = offset + 2;
      ttaparse->channels = GST_READ_UINT16_LE (data + offset);
      offset = offset + 2;
      ttaparse->bits = GST_READ_UINT16_LE (data + offset);
      offset += 2;
      ttaparse->samplerate = GST_READ_UINT32_LE (data + offset);
      frame_length = FRAME_TIME * ttaparse->samplerate;
      offset += 4;
      ttaparse->data_length = GST_READ_UINT32_LE (data + offset);
      offset += 4;
      num_frames = (ttaparse->data_length / frame_length) + 1;
      crc = crc32 (data, 18);
      if (crc != GST_READ_UINT32_LE (data + offset)) {
        GST_WARNING_OBJECT (ttaparse, "Header CRC wrong!");
      }
      offset += 4;
      GST_INFO_OBJECT (ttaparse,
          "channels: %u, bits: %u, samplerate: %u, data_length: %u, num_frames: %u",
          ttaparse->channels, ttaparse->bits, ttaparse->samplerate,
          ttaparse->data_length, num_frames);
      ttaparse->index =
          (GstTtaIndex *) g_malloc (num_frames * sizeof (GstTtaIndex));
      ttaparse->num_frames = num_frames;
      for (i = 0; i < num_frames; i++) {
        ttaparse->index[i].size = GST_READ_UINT32_LE (data + offset);
        ttaparse->index[i].pos = GST_BUFFER_OFFSET (ttaparse->partialbuf) + (num_frames) * 4 + 4 + datasize + 22;       // 22 == header size, +4 for the TTA1
        ttaparse->index[i].time = i * FRAME_TIME * 1000000000;
        offset += 4;
        datasize += ttaparse->index[i].size;
      }
      GST_DEBUG_OBJECT (ttaparse, "Datasize: %u", datasize);
      crc = crc32 (data + 22, num_frames * 4);
      if (crc != GST_READ_UINT32_LE (data + offset)) {
        GST_WARNING_OBJECT (ttaparse, "Seek table CRC wrong!");
      } else {
        ttaparse->seek_ok = TRUE;
        /*
           g_print("allowing seeking!\n");
           g_print("dumping index:\n");
           for (i = 0; i < ttaparse->num_frames; i++) {
           g_print("frame %u: offset = %llu, time=%llu, size=%u\n",
           i,
           ttaparse->index[i].pos,
           ttaparse->index[i].time,
           ttaparse->index[i].size);
           }
         */
      }
      offset += 4;
      ttaparse->header_parsed = TRUE;
      caps = gst_caps_new_simple ("audio/x-tta",
          "width", G_TYPE_INT, ttaparse->bits,
          "channels", G_TYPE_INT, ttaparse->channels,
          "rate", G_TYPE_INT, ttaparse->samplerate, NULL);
      gst_pad_set_explicit_caps (ttaparse->srcpad, caps);
    }
  }

  i = ttaparse->current_frame;
  while (size - offset >= ttaparse->index[i].size) {
    guint32 crc;

    crc = crc32 (data + offset, ttaparse->index[i].size - 4);
    if (crc != GST_READ_UINT32_LE (data + offset + ttaparse->index[i].size - 4)) {
      GST_WARNING_OBJECT (ttaparse, "Frame %u corrupted :(", i);
      GST_WARNING_OBJECT (ttaparse, "calculated crc: %u, got crc: %u", crc,
          GST_READ_UINT32_LE (data + offset + ttaparse->index[i].size - 4));
    }
    outbuf =
        gst_buffer_create_sub (ttaparse->partialbuf, offset,
        ttaparse->index[i].size - 4);
    GST_BUFFER_TIMESTAMP (outbuf) = ttaparse->index[i].time;
    if (ttaparse->current_frame + 1 == ttaparse->num_frames) {
      guint32 samples =
          ttaparse->data_length % (gint64) (ttaparse->samplerate * FRAME_TIME);
      gdouble frametime = (gdouble) samples / (gdouble) ttaparse->samplerate;

      GST_BUFFER_DURATION (outbuf) = (guint64) (frametime * GST_SECOND);
    } else {
      GST_BUFFER_DURATION (outbuf) = FRAME_TIME * 1000000000;
    }
    gst_pad_push (ttaparse->srcpad, GST_DATA (outbuf));
    offset += ttaparse->index[i].size;
    ttaparse->current_frame++;
    i = ttaparse->current_frame;
  }

  if (size - offset > 0) {
    glong remainder = size - offset;

    outbuf = gst_buffer_create_sub (ttaparse->partialbuf, offset, remainder);
    gst_buffer_unref (ttaparse->partialbuf);
    ttaparse->partialbuf = outbuf;
  } else {
    gst_buffer_unref (ttaparse->partialbuf);
    ttaparse->partialbuf = NULL;
  }

}

gboolean
gst_tta_parse_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "ttaparse",
          GST_RANK_PRIMARY, GST_TYPE_TTA_PARSE)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_tta_parse_debug, "ttaparse", 0,
      "tta file parser");

  return TRUE;
}
