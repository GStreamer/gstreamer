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
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "gstsmoothwave.h"

/* SmoothWave signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static void gst_smoothwave_base_init (gpointer g_class);
static void gst_smoothwave_class_init (GstSmoothWaveClass * klass);
static void gst_smoothwave_init (GstSmoothWave * smoothwave);
static void gst_smoothwave_dispose (GObject * object);
static GstStateChangeReturn gst_sw_change_state (GstElement * element,
    GstStateChange transition);
static void gst_smoothwave_chain (GstPad * pad, GstData * _data);
static GstPadLinkReturn gst_sw_sinklink (GstPad * pad, const GstCaps * caps);
static GstPadLinkReturn gst_sw_srclink (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

/*static guint gst_smoothwave_signals[LAST_SIGNAL] = { 0 }; */

#if G_BYTE_ORDER == G_BIG_ENDIAN
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "
        "green_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "
        "blue_mask = (int) " GST_VIDEO_BYTE4_MASK_32 ", "
        "width = (int)512, "
        "height = (int)256, " "framerate = " GST_VIDEO_FPS_RANGE)
    );
#else
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "
        "green_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "
        "blue_mask = (int) " GST_VIDEO_BYTE1_MASK_32 ", "
        "width = (int)512, "
        "height = (int)256, " "framerate = " GST_VIDEO_FPS_RANGE)
    );
#endif

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true")
    );

GType
gst_smoothwave_get_type (void)
{
  static GType smoothwave_type = 0;

  if (!smoothwave_type) {
    static const GTypeInfo smoothwave_info = {
      sizeof (GstSmoothWaveClass),
      gst_smoothwave_base_init,
      NULL,
      (GClassInitFunc) gst_smoothwave_class_init,
      NULL,
      NULL,
      sizeof (GstSmoothWave),
      0,
      (GInstanceInitFunc) gst_smoothwave_init,
    };

    smoothwave_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSmoothWave",
        &smoothwave_info, 0);
  }
  return smoothwave_type;
}

static void
gst_smoothwave_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Smooth waveform",
      "Visualization",
      "Fading grayscale waveform display",
      "Erik Walthinsen <omega@cse.ogi.edu>");
}

static void
gst_smoothwave_class_init (GstSmoothWaveClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_smoothwave_dispose;
  element_class->change_state = gst_sw_change_state;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
}

static void
gst_smoothwave_init (GstSmoothWave * smoothwave)
{
  int i;

  smoothwave->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  smoothwave->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (smoothwave), smoothwave->sinkpad);
  gst_pad_set_chain_function (smoothwave->sinkpad, gst_smoothwave_chain);
  gst_pad_set_link_function (smoothwave->sinkpad, gst_sw_sinklink);

  gst_element_add_pad (GST_ELEMENT (smoothwave), smoothwave->srcpad);
  gst_pad_set_link_function (smoothwave->srcpad, gst_sw_srclink);

  GST_OBJECT_FLAG_SET (smoothwave, GST_ELEMENT_EVENT_AWARE);

  smoothwave->adapter = gst_adapter_new ();

  smoothwave->width = 512;
  smoothwave->height = 256;

#define SPLIT_PT 96

  /* Fade in blue up to the split point */
  for (i = 0; i < SPLIT_PT; i++)
    smoothwave->palette[i] = (255 * i / SPLIT_PT);

  /* After the split point, fade out blue and fade in red */
  for (; i < 256; i++) {
    gint val = (i - SPLIT_PT) * 255 / (255 - SPLIT_PT);

    smoothwave->palette[i] = (255 - val) | (val << 16);
  }

  smoothwave->imagebuffer = g_malloc (smoothwave->width * smoothwave->height);
  memset (smoothwave->imagebuffer, 0, smoothwave->width * smoothwave->height);

  smoothwave->fps = 0;
  smoothwave->sample_rate = 0;
  smoothwave->audio_basetime = GST_CLOCK_TIME_NONE;
  smoothwave->samples_consumed = 0;
}

inline guchar *
draw_line (guchar * cur_pos, gint diff_y, gint stride)
{
  gint j;

  if (diff_y > 0) {
    for (j = diff_y; j > 0; j--) {
      cur_pos += stride;
      *cur_pos = 0xff;
    }
  } else if (diff_y < 0) {
    for (j = diff_y; j < 0; j++) {
      cur_pos -= stride;
      *cur_pos = 0xff;
    }
  } else {
    *cur_pos = 0xff;
  }
  return cur_pos;
}

static void
gst_smoothwave_dispose (GObject * object)
{
  GstSmoothWave *sw = GST_SMOOTHWAVE (object);

  if (sw->adapter != NULL) {
    g_object_unref (sw->adapter);
    sw->adapter = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstPadLinkReturn
gst_sw_sinklink (GstPad * pad, const GstCaps * caps)
{
  GstSmoothWave *sw = GST_SMOOTHWAVE (GST_OBJECT_PARENT (pad));
  GstStructure *structure;

  g_return_val_if_fail (sw != NULL, GST_PAD_LINK_REFUSED);

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "channels", &sw->channels) ||
      !gst_structure_get_int (structure, "rate", &sw->sample_rate))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_sw_srclink (GstPad * pad, const GstCaps * caps)
{
  GstSmoothWave *sw = GST_SMOOTHWAVE (GST_OBJECT_PARENT (pad));
  GstStructure *structure;

  g_return_val_if_fail (sw != NULL, GST_PAD_LINK_REFUSED);

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &sw->width) ||
      !gst_structure_get_int (structure, "height", &sw->height) ||
      !gst_structure_get_double (structure, "framerate", &sw->fps))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static void
gst_smoothwave_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSmoothWave *smoothwave;
  guint32 bytesperread;
  gint samples_per_frame;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  smoothwave = GST_SMOOTHWAVE (GST_OBJECT_PARENT (pad));

  if (GST_IS_EVENT (_data)) {
    GstEvent *event = GST_EVENT (_data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
        gint64 value = 0;

        gst_event_discont_get_value (event, GST_FORMAT_TIME, &value);
        gst_adapter_clear (smoothwave->adapter);
        smoothwave->audio_basetime = value;
        smoothwave->samples_consumed = 0;
      }
      default:
        gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

  if (!GST_PAD_IS_USABLE (smoothwave->srcpad)) {
    gst_buffer_unref (buf);
    return;
  }
  if (smoothwave->audio_basetime == GST_CLOCK_TIME_NONE)
    smoothwave->audio_basetime = GST_BUFFER_TIMESTAMP (buf);
  if (smoothwave->audio_basetime == GST_CLOCK_TIME_NONE)
    smoothwave->audio_basetime = 0;

  bytesperread = smoothwave->width * smoothwave->channels * sizeof (gint16);
  samples_per_frame = smoothwave->sample_rate / smoothwave->fps;

  gst_adapter_push (smoothwave->adapter, buf);
  while (gst_adapter_available (smoothwave->adapter) > MAX (bytesperread,
          samples_per_frame * smoothwave->channels * sizeof (gint16))) {
    guint32 *ptr;
    gint i;
    gint qheight;
    const gint16 *samples =
        (const guint16 *) gst_adapter_peek (smoothwave->adapter, bytesperread);
    gint stride = smoothwave->width;

    /* First draw the new waveform */
    if (smoothwave->channels == 2) {
      guchar *cur_pos[2];
      gint prev_y[2];

      qheight = smoothwave->height / 4;
      prev_y[0] = (gint32) (*samples) * qheight / 32768;
      samples++;
      prev_y[1] = (gint32) (*samples) * qheight / 32768;
      samples++;
      cur_pos[0] = smoothwave->imagebuffer + ((prev_y[0] + qheight) * stride);
      cur_pos[1] =
          smoothwave->imagebuffer + ((prev_y[1] +
              (3 * smoothwave->height / 4)) * stride);
      *(cur_pos[0]) = 0xff;
      *(cur_pos[1]) = 0xff;

      for (i = 1; i < smoothwave->width; i++) {
        gint diff_y = (gint) (*samples) * qheight / 32768 - prev_y[0];

        samples++;
        cur_pos[0] = draw_line (cur_pos[0], diff_y, stride);
        cur_pos[0]++;
        prev_y[0] += diff_y;

        diff_y = (gint) (*samples) * qheight / 32768 - prev_y[1];
        samples++;
        cur_pos[1] = draw_line (cur_pos[1], diff_y, stride);
        cur_pos[1]++;
        prev_y[1] += diff_y;
      }
    } else {
      qheight = smoothwave->height / 2;
      guchar *cur_pos;
      gint prev_y;

      prev_y = (gint32) (*samples) * qheight / 32768;
      samples++;
      cur_pos = smoothwave->imagebuffer + ((prev_y + qheight) * stride);
      *cur_pos = 0xff;
      for (i = 1; i < smoothwave->width; i++) {
        gint diff_y = (gint) (*samples) * qheight / 32768 - prev_y;

        samples++;
        cur_pos = draw_line (cur_pos, diff_y, stride);
        cur_pos++;
        prev_y += diff_y;
      }
    }

    /* Now fade stuff out */
    ptr = (guint32 *) smoothwave->imagebuffer;
    for (i = 0; i < (smoothwave->width * smoothwave->height) / 4; i++) {
      if (*ptr)
        *ptr -= ((*ptr & 0xf0f0f0f0ul) >> 4) + ((*ptr & 0xe0e0e0e0ul) >> 5);
      ptr++;
    }

    {
      guint32 *out;
      guchar *in;
      GstBuffer *bufout;

      bufout =
          gst_buffer_new_and_alloc (smoothwave->width * smoothwave->height * 4);

      GST_BUFFER_TIMESTAMP (bufout) =
          smoothwave->audio_basetime +
          (GST_SECOND * smoothwave->samples_consumed / smoothwave->sample_rate);
      GST_BUFFER_DURATION (bufout) = GST_SECOND / smoothwave->fps;
      out = (guint32 *) GST_BUFFER_DATA (bufout);
      in = smoothwave->imagebuffer;

      for (i = 0; i < (smoothwave->width * smoothwave->height); i++) {
        *out++ = smoothwave->palette[*in++];    // t | (t << 8) | (t << 16) | (t << 24);
      }
      gst_pad_push (smoothwave->srcpad, GST_DATA (bufout));
    }
    smoothwave->samples_consumed += samples_per_frame;
    gst_adapter_flush (smoothwave->adapter,
        samples_per_frame * smoothwave->channels * sizeof (gint16));
  }
}

static GstStateChangeReturn
gst_sw_change_state (GstElement * element, GstStateChange transition)
{
  GstSmoothWave *sw = GST_SMOOTHWAVE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      sw->audio_basetime = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (sw->adapter);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      sw->channels = 0;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  if (!gst_element_register (plugin, "smoothwave", GST_RANK_NONE,
          GST_TYPE_SMOOTHWAVE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "smoothwave",
    "Fading greyscale waveform display",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
