/* GStreamer buffer-frames conversion plugin
 * Copyright (C) 2004 Andy Wingo <wingo at pobox dot com>
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
#include "plugin.h"

#define GSTPLUGIN_TYPE_BUFFER_FRAMES_CONVERT \
  (gstplugin_buffer_frames_convert_get_type())
#define BUFFER_FRAMES_CONVERT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSTPLUGIN_TYPE_BUFFER_FRAMES_CONVERT, BufferFramesConvert))

typedef struct _BufferFramesConvert BufferFramesConvert;
typedef struct _BufferFramesConvertClass BufferFramesConvertClass;

struct _BufferFramesConvert
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  gint in_buffer_samples;
  gint out_buffer_samples;

  gboolean passthrough;
  GstBuffer *buf_out;
  gint64 offset;
  gint samples_out_remaining;
};

struct _BufferFramesConvertClass
{
  GstElementClass parent_class;
};

static GstElementDetails details = {
  "buffer-frames conversion",
  "Filter/Converter/Audio",
  "Convert between different values of the buffer-frames stream property",
  "Andy Wingo <wingo at pobox.com>"
};

static GstStaticPadTemplate sink_static_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float,"
        "rate = (int) [ 1, MAX ],"
        "channels = (int) [ 1, MAX ],"
        "endianness = (int) BYTE_ORDER,"
        "width = (int) 32," "buffer-frames = (int) [ 0, MAX ]")
    );

static GstStaticPadTemplate src_static_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float,"
        "rate = (int) [ 1, MAX ],"
        "channels = (int) [ 1, MAX ],"
        "endianness = (int) BYTE_ORDER,"
        "width = (int) 32," "buffer-frames = (int) [ 0, MAX ]")
    );

static void buffer_frames_convert_class_init (BufferFramesConvertClass * klass);
static void buffer_frames_convert_base_init (BufferFramesConvertClass * klass);
static void buffer_frames_convert_init (BufferFramesConvert * this);

static GstElementStateReturn buffer_frames_convert_change_state (GstElement *
    element);

static GstCaps *buffer_frames_convert_getcaps (GstPad * pad);
static GstPadLinkReturn buffer_frames_convert_link (GstPad * pad,
    const GstCaps * caps);

static void buffer_frames_convert_chain (GstPad * sinkpad, GstData * _data);

static GstElementClass *parent_class = NULL;

GType
gstplugin_buffer_frames_convert_get_type (void)
{
  static GType buffer_frames_convert_type = 0;

  if (!buffer_frames_convert_type) {
    static const GTypeInfo buffer_frames_convert_info = {
      sizeof (BufferFramesConvertClass),
      (GBaseInitFunc) buffer_frames_convert_base_init,
      NULL,
      (GClassInitFunc) buffer_frames_convert_class_init,
      NULL,
      NULL,
      sizeof (BufferFramesConvert),
      0,
      (GInstanceInitFunc) buffer_frames_convert_init,
    };

    buffer_frames_convert_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstBufferFramesConvert",
        &buffer_frames_convert_info, 0);
  }
  return buffer_frames_convert_type;
}

static void
buffer_frames_convert_base_init (BufferFramesConvertClass * klass)
{
  GstElementClass *eclass = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (eclass, &details);
  gst_element_class_add_pad_template
      (eclass, gst_static_pad_template_get (&src_static_template));
  gst_element_class_add_pad_template
      (eclass, gst_static_pad_template_get (&sink_static_template));
}

static void
buffer_frames_convert_class_init (BufferFramesConvertClass * klass)
{
  GstElementClass *eclass = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  eclass->change_state = buffer_frames_convert_change_state;
}

static void
buffer_frames_convert_init (BufferFramesConvert * this)
{
  this->sinkpad = gst_pad_new_from_template
      (gst_static_pad_template_get (&sink_static_template), "sink");
  gst_element_add_pad (GST_ELEMENT (this), this->sinkpad);
  gst_pad_set_link_function (this->sinkpad, buffer_frames_convert_link);
  gst_pad_set_getcaps_function (this->sinkpad, buffer_frames_convert_getcaps);
  gst_pad_set_chain_function (this->sinkpad, buffer_frames_convert_chain);

  this->srcpad = gst_pad_new_from_template
      (gst_static_pad_template_get (&src_static_template), "src");
  gst_element_add_pad (GST_ELEMENT (this), this->srcpad);
  gst_pad_set_link_function (this->srcpad, buffer_frames_convert_link);
  gst_pad_set_getcaps_function (this->srcpad, buffer_frames_convert_getcaps);

  this->in_buffer_samples = -1;
  this->out_buffer_samples = -1;

  this->buf_out = NULL;
  this->samples_out_remaining = 0;
}

static GstElementStateReturn
buffer_frames_convert_change_state (GstElement * element)
{
  BufferFramesConvert *this = (BufferFramesConvert *) element;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      this->offset = 0;
      break;

    case GST_STATE_PAUSED_TO_READY:
      if (this->buf_out)
        gst_buffer_unref (this->buf_out);
      this->buf_out = NULL;
      this->samples_out_remaining = 0;
      break;

    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);
  return GST_STATE_SUCCESS;
}

static GstCaps *
buffer_frames_convert_getcaps (GstPad * pad)
{
  BufferFramesConvert *this;
  GstPad *otherpad;
  GstCaps *ret;
  int i;

  this = BUFFER_FRAMES_CONVERT (GST_OBJECT_PARENT (pad));

  otherpad = pad == this->srcpad ? this->sinkpad : this->srcpad;
  ret = gst_pad_get_allowed_caps (otherpad);

  for (i = 0; i < gst_caps_get_size (ret); i++)
    gst_structure_set (gst_caps_get_structure (ret, i),
        "buffer-frames", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);

  GST_DEBUG ("allowed caps %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstPadLinkReturn
buffer_frames_convert_link (GstPad * pad, const GstCaps * caps)
{
  BufferFramesConvert *this;
  GstCaps *othercaps;
  GstPad *otherpad;
  GstPadLinkReturn ret;
  GstStructure *sinkstructure, *srcstructure;
  gint numchannels;

  this = BUFFER_FRAMES_CONVERT (GST_OBJECT_PARENT (pad));

  otherpad = pad == this->srcpad ? this->sinkpad : this->srcpad;

  /* first try to act as a passthrough */
  ret = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_SUCCESSFUL (ret)) {
    this->passthrough = TRUE;
    return ret;
  }

  /* then try to set unfixed buffer-frames */
  othercaps = gst_caps_copy (caps);
  gst_caps_set_simple (othercaps, "buffer-frames", GST_TYPE_INT_RANGE, 0,
      G_MAXINT, NULL);
  ret = gst_pad_try_set_caps_nonfixed (otherpad, othercaps);
  if (GST_PAD_LINK_FAILED (ret))
    return ret;

  /* it's ok, let's record our data */
  sinkstructure =
      gst_caps_get_structure (pad == this->sinkpad ? caps : othercaps, 0);
  srcstructure =
      gst_caps_get_structure (pad == this->srcpad ? caps : othercaps, 0);
  gst_structure_get_int (sinkstructure, "buffer-frames",
      &this->in_buffer_samples);
  gst_structure_get_int (srcstructure, "buffer-frames",
      &this->out_buffer_samples);

  gst_structure_get_int (sinkstructure, "channels", &numchannels);
  this->in_buffer_samples *= numchannels;
  this->out_buffer_samples *= numchannels;

  if (this->out_buffer_samples == 0)
    this->passthrough = TRUE;

  return GST_PAD_LINK_OK;
}

static void
buffer_frames_convert_chain (GstPad * pad, GstData * _data)
{
  BufferFramesConvert *this;
  GstBuffer *buf_in, *buf_out;
  gfloat *data_in;
  gfloat *data_out;
  gint i, samples_in, samples_in_remaining, samples_out_remaining,
      out_buffer_samples;

  this = (BufferFramesConvert *) GST_OBJECT_PARENT (pad);

  if (this->passthrough) {
    gst_pad_push (this->srcpad, _data);
    return;
  }

  buf_in = (GstBuffer *) _data;
  data_in = (gfloat *) GST_BUFFER_DATA (buf_in);
  samples_in = samples_in_remaining =
      GST_BUFFER_SIZE (buf_in) / sizeof (gfloat);
  out_buffer_samples = this->out_buffer_samples;

  /* deal with any leftover buffer */
  if (this->buf_out) {
    samples_out_remaining = this->samples_out_remaining;
    buf_out = this->buf_out;
    data_out = (gfloat *) GST_BUFFER_DATA (buf_out);
    data_out += out_buffer_samples - samples_out_remaining;

    i = MIN (samples_out_remaining, samples_in_remaining);
    samples_in_remaining -= i;
    samples_out_remaining -= i;
    while (i--)
      *(data_out++) = *(data_in++);

    if (!samples_out_remaining) {
      this->buf_out = NULL;
      this->samples_out_remaining = 0;
      gst_pad_push (this->srcpad, (GstData *) buf_out);
    } else {
      /* we used up the incoming samples, but didn't fill our buffer */
      this->samples_out_remaining = samples_out_remaining;
      gst_buffer_unref (buf_in);
      return;
    }
  }

  /* use a fast subbuffer while we can */
  while (samples_in_remaining > out_buffer_samples) {
    buf_out = gst_buffer_create_sub (buf_in,
        (samples_in - samples_in_remaining) * sizeof (gfloat),
        out_buffer_samples * sizeof (gfloat));
    data_in += out_buffer_samples;
    samples_in_remaining -= out_buffer_samples;
    gst_pad_push (this->srcpad, (GstData *) buf_out);
  }

  /* if there's an event coming next, just push what we have */
  if (this->in_buffer_samples && samples_in != this->in_buffer_samples
      && samples_in_remaining) {
    buf_out =
        gst_buffer_create_sub (buf_in,
        (samples_in - samples_in_remaining) * sizeof (gfloat),
        samples_in_remaining * sizeof (gfloat));
    gst_pad_push (this->srcpad, (GstData *) buf_out);
  } else {
    /* otherwise make a leftover buffer if it's necessary */
    if (samples_in_remaining) {
      buf_out =
          gst_pad_alloc_buffer (this->srcpad, 0,
          out_buffer_samples * sizeof (gfloat));
      data_out = (gfloat *) GST_BUFFER_DATA (buf_out);
      this->buf_out = buf_out;
      this->samples_out_remaining = out_buffer_samples - samples_in_remaining;
      while (samples_in_remaining--)
        *(data_out++) = *(data_in++);
    }
  }

  gst_buffer_unref (buf_in);
}
