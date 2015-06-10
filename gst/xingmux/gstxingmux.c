/*
 * Copyright (c) 2006 Christophe Fergeau  <teuf@gnome.org>
 * Copyright (c) 2008 Sebastian Dröge  <slomo@circular-chaos.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Xing SDK: http://www.mp3-tech.org/programmer/sources/vbrheadersdk.zip */


/**
 * SECTION:element-xingmux
 *
 * xingmux adds a Xing header to MP3 files. This contains information about the duration and size
 * of the file and a seek table and is very useful for getting an almost correct duration and better
 * seeking on VBR MP3 files.
 * 
 * This element will remove any existing Xing, LAME or VBRI headers from the beginning of the file.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 audiotestsrc num-buffers=1000 ! audioconvert ! lamemp3enc ! xingmux ! filesink location=test.mp3
 * gst-launch-1.0 filesrc location=test.mp3 ! xingmux ! filesink location=test2.mp3
 * gst-launch-1.0 filesrc location=test.mp3 ! mp3parse ! xingmux ! filesink location=test2.mp3
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "gstxingmux.h"

GST_DEBUG_CATEGORY_STATIC (xing_mux_debug);
#define GST_CAT_DEFAULT xing_mux_debug

#define gst_xing_mux_parent_class parent_class
G_DEFINE_TYPE (GstXingMux, gst_xing_mux, GST_TYPE_ELEMENT);

/* Xing Header stuff */
#define GST_XING_FRAME_FIELD   (1 << 0)
#define GST_XING_BYTES_FIELD   (1 << 1)
#define GST_XING_TOC_FIELD     (1 << 2)
#define GST_XING_QUALITY_FIELD (1 << 3)

typedef struct _GstXingSeekEntry
{
  gint64 timestamp;
  gint byte;
} GstXingSeekEntry;

static inline GstXingSeekEntry *
gst_xing_seek_entry_new (void)
{
  return g_slice_new (GstXingSeekEntry);
}

static inline void
gst_xing_seek_entry_free (GstXingSeekEntry * entry)
{
  g_slice_free (GstXingSeekEntry, entry);
}

static void gst_xing_mux_finalize (GObject * obj);
static GstStateChangeReturn
gst_xing_mux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_xing_mux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_xing_mux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStaticPadTemplate gst_xing_mux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, " "layer = (int) [ 1, 3 ]"));


static GstStaticPadTemplate gst_xing_mux_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, " "layer = (int) [ 1, 3 ]"));
static const guint mp3types_bitrates[2][3][16] = {
  {
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,}
      },
  {
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,}
      },
};

static const guint mp3types_freqs[3][3] = { {44100, 48000, 32000},
{22050, 24000, 16000},
{11025, 12000, 8000}
};

static gboolean
parse_header (guint32 header, guint * ret_size, guint * ret_spf,
    gulong * ret_rate)
{
  guint length, spf;
  gulong samplerate, bitrate, layer, padding;
  gint lsf, mpg25;

  if ((header & 0xffe00000) != 0xffe00000) {
    g_warning ("invalid sync");
    return FALSE;
  }

  if (((header >> 19) & 3) == 0x01) {
    g_warning ("invalid MPEG version");
    return FALSE;
  }

  if (((header >> 17) & 3) == 0x00) {
    g_warning ("invalid MPEG layer");
    return FALSE;
  }

  if (((header >> 12) & 0xf) == 0xf || ((header >> 12) & 0xf) == 0x0) {
    g_warning ("invalid bitrate");
    return FALSE;
  }

  if (((header >> 10) & 0x3) == 0x3) {
    g_warning ("invalid sampling rate");
    return FALSE;
  }

  if (header & 0x00000002) {
    g_warning ("invalid emphasis");
    return FALSE;
  }

  if (header & (1 << 20)) {
    lsf = (header & (1 << 19)) ? 0 : 1;
    mpg25 = 0;
  } else {
    lsf = 1;
    mpg25 = 1;
  }

  layer = 4 - ((header >> 17) & 0x3);

  bitrate = (header >> 12) & 0xF;
  bitrate = mp3types_bitrates[lsf][layer - 1][bitrate] * 1000;
  if (bitrate == 0)
    return FALSE;

  samplerate = (header >> 10) & 0x3;
  samplerate = mp3types_freqs[lsf + mpg25][samplerate];

  padding = (header >> 9) & 0x1;

  switch (layer) {
    case 1:
      length = 4 * ((bitrate * 12) / samplerate + padding);
      break;
    case 2:
      length = (bitrate * 144) / samplerate + padding;
      break;
    default:
    case 3:
      length = (bitrate * 144) / (samplerate << lsf) + padding;
      break;
  }

  if (layer == 1)
    spf = 384;
  else if (layer == 2 || lsf == 0)
    spf = 1152;
  else
    spf = 576;

  if (ret_size)
    *ret_size = length;
  if (ret_spf)
    *ret_spf = spf;
  if (ret_rate)
    *ret_rate = samplerate;

  return TRUE;
}

static guint
get_xing_offset (guint32 header)
{
  guint mpeg_version = (header >> 19) & 0x3;
  guint channel_mode = (header >> 6) & 0x3;

  if (mpeg_version == 0x3) {
    if (channel_mode == 0x3) {
      return 0x11;
    } else {
      return 0x20;
    }
  } else {
    if (channel_mode == 0x3) {
      return 0x09;
    } else {
      return 0x11;
    }
  }
}

static gboolean
has_xing_header (guint32 header, GstBuffer * buffer, gsize size)
{
  gboolean ret;
  GstMapInfo map;
  guint8 *data;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  data = map.data;
  data += 4;
  data += get_xing_offset (header);

  if (memcmp (data, "Xing", 4) == 0 ||
      memcmp (data, "Info", 4) == 0 || memcmp (data, "VBRI", 4) == 0)
    ret = TRUE;
  else
    ret = FALSE;

  gst_buffer_unmap (buffer, &map);
  return ret;
}

static GstBuffer *
generate_xing_header (GstXingMux * xing)
{
  guint8 *xing_flags;
  guint32 xing_flags_tmp = 0;
  GstBuffer *xing_header;
  GstMapInfo map;
  guchar *data;

  guint32 header;
  guint32 header_be;
  guint size, spf, xing_offset;
  gulong rate;
  guint bitrate = 0x00;

  gint64 duration;
  gint64 byte_count;

  header = xing->first_header;

  /* Set bitrate and choose lowest possible size */
  do {
    bitrate++;

    header &= 0xffff0fff;
    header |= bitrate << 12;

    if (!parse_header (header, &size, &spf, &rate)) {
      GST_ERROR ("Failed to parse header!");
      return NULL;
    }
    xing_offset = get_xing_offset (header);
  } while (size < (4 + xing_offset + 4 + 4 + 4 + 4 + 100) && bitrate < 0xe);

  if (bitrate == 0xe) {
    GST_ERROR ("No usable bitrate found!");
    return NULL;
  }

  xing_header = gst_buffer_new_and_alloc (size);

  gst_buffer_map (xing_header, &map, GST_MAP_WRITE);
  data = map.data;
  memset (data, 0, size);
  header_be = GUINT32_TO_BE (header);
  memcpy (data, &header_be, 4);

  data += 4;
  data += xing_offset;

  memcpy (data, "Xing", 4);
  data += 4;

  xing_flags = data;
  data += 4;

  if (xing->duration != GST_CLOCK_TIME_NONE) {
    duration = xing->duration;
  } else {
    GstFormat fmt = GST_FORMAT_TIME;

    if (!gst_pad_peer_query_duration (xing->sinkpad, fmt, &duration))
      duration = GST_CLOCK_TIME_NONE;
  }

  if (duration != GST_CLOCK_TIME_NONE) {
    guint32 number_of_frames;

    /* The Xing Header contains a NumberOfFrames field, which verifies to:
     * Duration = NumberOfFrames *SamplesPerFrame/SamplingRate
     * SamplesPerFrame and SamplingRate are values for the current frame. 
     */
    number_of_frames = gst_util_uint64_scale (duration, rate, GST_SECOND) / spf;
    number_of_frames += 1;      /* Xing Header Frame */
    GST_DEBUG ("Setting number of frames to %u", number_of_frames);
    number_of_frames = GUINT32_TO_BE (number_of_frames);
    memcpy (data, &number_of_frames, 4);
    xing_flags_tmp |= GST_XING_FRAME_FIELD;
    data += 4;
  }

  if (xing->byte_count != 0) {
    byte_count = xing->byte_count;
  } else {
    GstFormat fmt = GST_FORMAT_BYTES;

    if (!gst_pad_peer_query_duration (xing->sinkpad, fmt, &byte_count))
      byte_count = 0;
    if (byte_count == -1)
      byte_count = 0;
  }

  if (byte_count != 0) {
    guint32 nbytes;

    if (byte_count > G_MAXUINT32) {
      GST_DEBUG ("Too large stream: %" G_GINT64_FORMAT " > %u bytes",
          byte_count, G_MAXUINT32);
    } else {
      nbytes = byte_count;
      GST_DEBUG ("Setting number of bytes to %u", nbytes);
      nbytes = GUINT32_TO_BE (nbytes);
      memcpy (data, &nbytes, 4);
      xing_flags_tmp |= GST_XING_BYTES_FIELD;
      data += 4;
    }
  }

  if (xing->seek_table != NULL && byte_count != 0
      && duration != GST_CLOCK_TIME_NONE) {
    GList *it;
    gint percent = 0;

    xing_flags_tmp |= GST_XING_TOC_FIELD;

    GST_DEBUG ("Writing seek table");
    for (it = xing->seek_table; it != NULL && percent < 100; it = it->next) {
      GstXingSeekEntry *entry = (GstXingSeekEntry *) it->data;
      gint64 pos;
      guchar byte;

      while ((entry->timestamp * 100) / duration >= percent) {
        pos = (entry->byte * 256) / byte_count;
        GST_DEBUG ("  %d %% -- %" G_GINT64_FORMAT " 1/256", percent, pos);
        byte = (guchar) pos;
        memcpy (data, &byte, 1);
        data++;
        percent++;
      }
    }

    if (percent < 100) {
      guchar b;
      gint i;

      memcpy (&b, data - 1, 1);

      for (i = percent; i < 100; i++) {
        GST_DEBUG ("  %d %% -- %d 1/256", i, b);
        memcpy (data, &b, 1);
        data++;
      }
    }
  }

  GST_DEBUG ("Setting Xing flags to 0x%x\n", xing_flags_tmp);
  xing_flags_tmp = GUINT32_TO_BE (xing_flags_tmp);
  memcpy (xing_flags, &xing_flags_tmp, 4);
  gst_buffer_unmap (xing_header, &map);
  return xing_header;
}

static void
gst_xing_mux_class_init (GstXingMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_xing_mux_finalize);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_xing_mux_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_xing_mux_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_xing_mux_sink_template));

  GST_DEBUG_CATEGORY_INIT (xing_mux_debug, "xingmux", 0, "Xing Header Muxer");

  gst_element_class_set_static_metadata (gstelement_class, "MP3 Xing muxer",
      "Formatter/Muxer/Metadata",
      "Adds a Xing header to the beginning of a VBR MP3 file",
      "Christophe Fergeau <teuf@gnome.org>");
}

static void
gst_xing_mux_finalize (GObject * obj)
{
  GstXingMux *xing = GST_XING_MUX (obj);

  if (xing->adapter) {
    g_object_unref (xing->adapter);
    xing->adapter = NULL;
  }

  if (xing->seek_table) {
    g_list_foreach (xing->seek_table, (GFunc) gst_xing_seek_entry_free, NULL);
    g_list_free (xing->seek_table);
    xing->seek_table = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
xing_reset (GstXingMux * xing)
{
  xing->duration = GST_CLOCK_TIME_NONE;
  xing->byte_count = 0;

  gst_adapter_clear (xing->adapter);

  if (xing->seek_table) {
    g_list_foreach (xing->seek_table, (GFunc) gst_xing_seek_entry_free, NULL);
    g_list_free (xing->seek_table);
    xing->seek_table = NULL;
  }

  xing->sent_xing = FALSE;
}


static void
gst_xing_mux_init (GstXingMux * xing)
{
  /* pad through which data comes in to the element */
  xing->sinkpad =
      gst_pad_new_from_static_template (&gst_xing_mux_sink_template, "sink");
  gst_pad_set_chain_function (xing->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xing_mux_chain));
  gst_pad_set_event_function (xing->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xing_mux_sink_event));
  GST_PAD_SET_PROXY_CAPS (xing->sinkpad);
  gst_element_add_pad (GST_ELEMENT (xing), xing->sinkpad);

  /* pad through which data goes out of the element */
  xing->srcpad =
      gst_pad_new_from_static_template (&gst_xing_mux_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (xing), xing->srcpad);

  xing->adapter = gst_adapter_new ();

  xing_reset (xing);
}

static GstFlowReturn
gst_xing_mux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstXingMux *xing = GST_XING_MUX (parent);
  GstFlowReturn ret = GST_FLOW_OK;

  gst_adapter_push (xing->adapter, buffer);

  while (gst_adapter_available (xing->adapter) >= 4) {
    const guchar *data;
    guint32 header;
    GstBuffer *outbuf;
    GstClockTime duration;
    guint size, spf;
    gulong rate;
    GstXingSeekEntry *seek_entry;

    data = gst_adapter_map (xing->adapter, 4);
    header = GST_READ_UINT32_BE (data);
    gst_adapter_unmap (xing->adapter);

    if (!parse_header (header, &size, &spf, &rate)) {
      GST_DEBUG ("Lost sync, resyncing");
      gst_adapter_flush (xing->adapter, 1);
      continue;
    }

    if (gst_adapter_available (xing->adapter) < size)
      break;

    outbuf = gst_adapter_take_buffer (xing->adapter, size);

    if (!xing->sent_xing) {
      if (has_xing_header (header, outbuf, size)) {
        GST_LOG_OBJECT (xing, "Dropping old Xing header");
        gst_buffer_unref (outbuf);
        continue;
      } else {
        GstBuffer *xing_header;
        guint64 xing_header_size;

        xing->first_header = header;

        xing_header = generate_xing_header (xing);

        if (xing_header == NULL) {
          GST_ERROR ("Can't generate Xing header");
          gst_buffer_unref (outbuf);
          return GST_FLOW_ERROR;
        }

        xing_header_size = gst_buffer_get_size (xing_header);

        if ((ret = gst_pad_push (xing->srcpad, xing_header)) != GST_FLOW_OK) {
          GST_ERROR_OBJECT (xing, "Failed to push Xing header: %s",
              gst_flow_get_name (ret));
          gst_buffer_unref (xing_header);
          gst_buffer_unref (outbuf);
          return ret;
        }

        xing->byte_count += xing_header_size;
        xing->sent_xing = TRUE;
      }
    }

    seek_entry = gst_xing_seek_entry_new ();
    seek_entry->timestamp =
        (xing->duration == GST_CLOCK_TIME_NONE) ? 0 : xing->duration;
    /* Workaround for parsers checking that the first seek table entry is 0 */
    seek_entry->byte = (seek_entry->timestamp == 0) ? 0 : xing->byte_count;
    xing->seek_table = g_list_append (xing->seek_table, seek_entry);

    duration = gst_util_uint64_scale_ceil (spf, GST_SECOND, rate);

    GST_BUFFER_TIMESTAMP (outbuf) =
        (xing->duration == GST_CLOCK_TIME_NONE) ? 0 : xing->duration;
    GST_BUFFER_DURATION (outbuf) = duration;
    GST_BUFFER_OFFSET (outbuf) = xing->byte_count;
    xing->byte_count += gst_buffer_get_size (outbuf);
    GST_BUFFER_OFFSET_END (outbuf) = xing->byte_count;

    if (xing->duration == GST_CLOCK_TIME_NONE)
      xing->duration = duration;
    else
      xing->duration += duration;

    if ((ret = gst_pad_push (xing->srcpad, outbuf)) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (xing, "Failed to push MP3 frame: %s",
          gst_flow_get_name (ret));
      return ret;
    }
  }

  return ret;
}

static gboolean
gst_xing_mux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstXingMux *xing;
  gboolean result;

  xing = GST_XING_MUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      if (xing->sent_xing) {
        GST_ERROR ("Already sent Xing header, dropping NEWSEGMENT event!");
        gst_event_unref (event);
        result = FALSE;
      } else {
        GstSegment segment;

        gst_event_copy_segment (event, &segment);

        if (segment.format == GST_FORMAT_BYTES) {
          result = gst_pad_push_event (xing->srcpad, event);
        } else {

          gst_event_unref (event);
          gst_segment_init (&segment, GST_FORMAT_BYTES);
          event = gst_event_new_segment (&segment);

          result = gst_pad_push_event (xing->srcpad, event);
        }
      }
      break;

    case GST_EVENT_EOS:{
      GstEvent *n_event;

      GST_DEBUG_OBJECT (xing, "handling EOS event");

      if (xing->sent_xing) {
        GstSegment segment;

        gst_segment_init (&segment, GST_FORMAT_BYTES);
        n_event = gst_event_new_segment (&segment);

        if (G_UNLIKELY (!gst_pad_push_event (xing->srcpad, n_event))) {
          GST_WARNING
              ("Failed to seek to position 0 for pushing the Xing header");
        } else {
          GstBuffer *header;
          GstFlowReturn ret;

          header = generate_xing_header (xing);

          if (header == NULL) {
            GST_ERROR ("Can't generate Xing header");
          } else {

            GST_INFO ("Writing real Xing header to beginning of stream");

            if ((ret = gst_pad_push (xing->srcpad, header)) != GST_FLOW_OK)
              GST_WARNING ("Failed to push updated Xing header: %s\n",
                  gst_flow_get_name (ret));
          }
        }
      }
      result = gst_pad_push_event (xing->srcpad, event);
      break;
    }
    default:
      result = gst_pad_event_default (pad, parent, event);
      break;
  }

  return result;
}


static GstStateChangeReturn
gst_xing_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstXingMux *xing;
  GstStateChangeReturn result;

  xing = GST_XING_MUX (element);

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      xing_reset (xing);
      break;
    default:
      break;
  }

  return result;
}
