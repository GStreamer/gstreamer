/*
 * (c) 2006 Christophe Fergeau  <teuf@gnome.org>
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
#include <config.h>
#endif

#include <string.h>
#include "gstxingmux.h"

GST_DEBUG_CATEGORY_STATIC (xing_mux_debug);
#define GST_CAT_DEFAULT xing_mux_debug

GST_BOILERPLATE (GstXingMux, gst_xing_mux, GstElement, GST_TYPE_ELEMENT);

/* Xing Header stuff */
struct _GstXingMuxPriv
{
  guint64 duration;
  guint64 byte_count;
  GList *seek_table;
  gboolean flush;
};

#define GST_XING_FRAME_FIELD   (1 << 0)
#define GST_XING_BYTES_FIELD   (1 << 1)
#define GST_XING_TOC_FIELD     (1 << 2)
#define GST_XING_QUALITY_FIELD (1 << 3)

static const int XING_FRAME_SIZE = 418;

static GstStateChangeReturn
gst_xing_mux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_xing_mux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_xing_mux_sink_event (GstPad * pad, GstEvent * event);


static void
gst_xing_mux_finalize (GObject * obj)
{
  GstXingMux *xing = GST_XING_MUX (obj);

  g_free (xing->priv);
  xing->priv = NULL;
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}


static GstStaticPadTemplate gst_xing_mux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, " "layer = (int) 3"));


static GstStaticPadTemplate gst_xing_mux_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, " "layer = (int) 3"));


static void
gst_xing_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  static GstElementDetails gst_xing_mux_details = {
    "MP3 Xing Muxer",
    "Formatter/Metadata",
    "Adds a Xing header to the beginning of a VBR MP3 file",
    "Christophe Fergeau <teuf@gnome.org>"
  };


  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_xing_mux_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_xing_mux_sink_template));
  gst_element_class_set_details (element_class, &gst_xing_mux_details);
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
}

static void
xing_set_flush (GstXingMux * xing, gboolean flush)
{
  if (xing->priv == NULL) {
    return;
  }
  xing->priv->flush = flush;
}

static void
gst_xing_mux_init (GstXingMux * xing, GstXingMuxClass * xingmux_class)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (xingmux_class);

  /* pad through which data comes in to the element */
  xing->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_setcaps_function (xing->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_chain_function (xing->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xing_mux_chain));
  gst_pad_set_event_function (xing->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xing_mux_sink_event));
  gst_element_add_pad (GST_ELEMENT (xing), xing->sinkpad);

  /* pad through which data goes out of the element */
  xing->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (xing), xing->srcpad);

  xing->priv = g_malloc0 (sizeof (GstXingMuxPriv));
  xing_set_flush (xing, TRUE);
  xing->priv->duration = GST_CLOCK_TIME_NONE;

}

G_GNUC_UNUSED static void
xing_update_data (GstXingMux * xing, gint bytes, guint64 duration)
{
  if (xing->priv == NULL) {
    return;
  }
  xing->priv->byte_count += bytes;

  if (duration == GST_CLOCK_TIME_NONE) {
    return;
  }
  if (xing->priv->duration == GST_CLOCK_TIME_NONE) {
    xing->priv->duration = duration;
  } else {
    xing->priv->duration += duration;
  }
}

static GstBuffer *
xing_generate_header (GstXingMux * xing)
{
  guint32 xing_flags;
  GstBuffer *header;
  guint32 *data;

  /* Dummy header that we will stick at the beginning of our frame
   *
   * 0xffe => synchronization bits
   * 0x1b  => 11010b (11b == MPEG1 | 01b == Layer III | 0b == no CRC)
   * 0x9   => 128kbps
   * 0x00  => 00b == 44100 Hz | 0b == no padding | 0b == private bit
   * 0x44  => 0010b 0010b (00b == stereo | 10b == (unused) mode extension)
   *                      (0b == no copyright bit | 0b == original bit)
   *                      (00b == no emphasis)
   *
   * Such a frame (MPEG1 Layer III) contains 1152 samples, its size is thus:
   * (1152*(128000/8))/44100 = 417.96
   * 
   * There are also 32 bytes (ie 8 32 bits values) to skip after the header 
   * for such frames
   */
  const guint8 mp3_header[4] = { 0xff, 0xfb, 0x90, 0x44 };
  const int SIDE_INFO_SIZE = 32 / sizeof (guint32);

  header = gst_buffer_new_and_alloc (XING_FRAME_SIZE);

  data = (guint32 *) GST_BUFFER_DATA (header);
  memset (data, 0, XING_FRAME_SIZE);
  memcpy (data, mp3_header, 4);
  memcpy (&data[8 + 1], "Xing", 4);

  xing_flags = 0;
  if (xing->priv->duration != GST_CLOCK_TIME_NONE) {
    guint number_of_frames;

    /* The Xing Header contains a NumberOfFrames field, which verifies to:
     * Duration = NumberOfFrames *SamplesPerFrame/SamplingRate
     * SamplesPerFrame and SamplingRate are values for the current frame, 
     * ie 1152 and 44100 in our case.
     */
    number_of_frames = (44100 * xing->priv->duration / GST_SECOND) / 1152;
    data[SIDE_INFO_SIZE + 3] = GUINT32_TO_BE (number_of_frames);

    xing_flags |= GST_XING_FRAME_FIELD;
  }

  if (xing->priv->byte_count != 0) {
    xing_flags |= GST_XING_BYTES_FIELD;
    data[SIDE_INFO_SIZE + 4] = GUINT32_TO_BE (xing->priv->byte_count);
  }

  /* Un-#ifdef when it's implemented :) xing code in VbrTag.c looks like
   * it could be stolen
   */
#if 0
  if (xing->priv->seek_table != NULL) {
    GList *it;

    xing_flags |= GST_XING_TOC_FIELD;
    for (it = xing->priv->seek_table; it != NULL; it = it->next) {
      /* do something */
    }
  }
#endif

  data[SIDE_INFO_SIZE + 2] = GUINT32_TO_BE (xing_flags);
  gst_buffer_set_caps (header, GST_PAD_CAPS (xing->srcpad));
  //  gst_util_dump_mem ((guchar *)data, XING_FRAME_SIZE);
  return header;
}

static gboolean
xing_ready_to_flush (GstXingMux * xing)
{
  if (xing->priv == NULL) {
    return FALSE;
  }
  return xing->priv->flush;
}

static void
xing_push_header (GstXingMux * xing)
{
  GstBuffer *header;
  GstEvent *event;

  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
      0, GST_CLOCK_TIME_NONE, 0);

  gst_pad_push_event (xing->srcpad, event);

  header = xing_generate_header (xing);
  xing_set_flush (xing, FALSE);
  GST_INFO ("Writing real Xing header to beginning of stream");
  gst_pad_push (xing->srcpad, header);
}

static GstFlowReturn
gst_xing_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstXingMux *xing = GST_XING_MUX (GST_OBJECT_PARENT (pad));

  xing_update_data (xing, GST_BUFFER_SIZE (buffer),
      GST_BUFFER_DURATION (buffer));

  if (xing_ready_to_flush (xing)) {
    GST_INFO ("Writing empty Xing header to stream");
    gst_pad_push (xing->srcpad, xing_generate_header (xing));
    xing_set_flush (xing, FALSE);
  }

  return gst_pad_push (xing->srcpad, buffer);
}

static gboolean
gst_xing_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstXingMux *xing;
  gboolean result;

  xing = GST_XING_MUX (gst_pad_get_parent (pad));
  result = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 value, end_value, base;

      gst_event_parse_new_segment (event, &update, &rate, &format,
          &value, &end_value, &base);
      gst_event_unref (event);
      if (format == GST_FORMAT_BYTES && gst_pad_is_linked (xing->srcpad)) {
        GstEvent *new_event;

        GST_INFO ("Adjusting NEW_SEGMENT event by %d", XING_FRAME_SIZE);
        value += XING_FRAME_SIZE;
        if (end_value != -1) {
          end_value += XING_FRAME_SIZE;
        }

        new_event = gst_event_new_new_segment (update, rate, format,
            value, end_value, base);
        result = gst_pad_push_event (xing->srcpad, new_event);
      } else {
        result = FALSE;
      }
    }
      break;

    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (xing, "handling EOS event");
      xing_push_header (xing);
      result = gst_pad_push_event (xing->srcpad, event);
      break;
    default:
      result = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (GST_OBJECT (xing));

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
      memset (xing->priv, 0, sizeof (GstXingMuxPriv));
      xing_set_flush (xing, TRUE);
      break;
    default:
      break;
  }

  return result;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "xingmux", GST_RANK_NONE,
          GST_TYPE_XING_MUX))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (xing_mux_debug, "xingmux", 0, "Xing Header Muxer");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "xingheader",
    "Add a xing header to mp3 encoded data",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
