/* GStreamer FAAD (Free AAC Decoder) plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include "gstfaad.h"

GST_PAD_TEMPLATE_FACTORY (sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "faad_mpeg_templ",
    "audio/mpeg",
      "systemstream", GST_PROPS_BOOLEAN (FALSE),
      "mpegversion",  GST_PROPS_LIST (
                        GST_PROPS_INT (2),
                        GST_PROPS_INT (4)
                      )
  )
);

GST_PAD_TEMPLATE_FACTORY (src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "faad_int_templ",
    "audio/x-raw-int",
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (TRUE),
      "width",      GST_PROPS_LIST (
		      GST_PROPS_INT (16),
		      GST_PROPS_INT (24),
		      GST_PROPS_INT (32)
		    ),
      "depth",      GST_PROPS_LIST (
		      GST_PROPS_INT (16),
		      GST_PROPS_INT (24),
		      GST_PROPS_INT (32)
		    ),
      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
      "channels",   GST_PROPS_INT_RANGE (1, 6)
  ),
  GST_CAPS_NEW (
    "faad_float_templ",
    "audio/x-raw-float",
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "depth",      GST_PROPS_LIST (
		      GST_PROPS_INT (32), /* float  */
		      GST_PROPS_INT (64)  /* double */
		    ),
      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
      "channels",   GST_PROPS_INT_RANGE (1, 6)
  )
);

static void     gst_faad_base_init    (GstFaadClass *klass);
static void     gst_faad_class_init   (GstFaadClass *klass);
static void     gst_faad_init         (GstFaad      *faad);

static GstPadLinkReturn
                gst_faad_sinkconnect  (GstPad       *pad,
				       GstCaps      *caps);
static GstPadLinkReturn
                gst_faad_srcconnect   (GstPad       *pad,
				       GstCaps      *caps);
static GstCaps *gst_faad_srcgetcaps   (GstPad       *pad,
				       GstCaps      *caps);
static void     gst_faad_chain        (GstPad       *pad,
				       GstData      *data);
static GstElementStateReturn
                gst_faad_change_state (GstElement   *element);

static GstElementClass *parent_class = NULL;
/* static guint gst_faad_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_faad_get_type (void)
{
  static GType gst_faad_type = 0;

  if (!gst_faad_type) {
    static const GTypeInfo gst_faad_info = {
      sizeof (GstFaadClass),      
      (GBaseInitFunc) gst_faad_base_init,
      NULL,
      (GClassInitFunc) gst_faad_class_init,
      NULL,
      NULL,
      sizeof(GstFaad),
      0,
      (GInstanceInitFunc) gst_faad_init,
    };

    gst_faad_type = g_type_register_static (GST_TYPE_ELEMENT,
					    "GstFaad",
					    &gst_faad_info, 0);
  }

  return gst_faad_type;
}

static void
gst_faad_base_init (GstFaadClass *klass)
{
  GstElementDetails gst_faad_details = {
    "Free AAC Decoder (FAAD)",
    "Codec/Audio/Decoder",
    "Free MPEG-2/4 AAC decoder",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (src_template));
  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (sink_template));

  gst_element_class_set_details (element_class, &gst_faad_details);
}

static void
gst_faad_class_init (GstFaadClass *klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_faad_change_state;
}

static void
gst_faad_init (GstFaad *faad)
{
  faad->handle = NULL;
  faad->samplerate = -1;
  faad->channels = -1;

  GST_FLAG_SET (faad, GST_ELEMENT_EVENT_AWARE);

  faad->sinkpad = gst_pad_new_from_template (
	GST_PAD_TEMPLATE_GET (sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (faad), faad->sinkpad);
  gst_pad_set_chain_function (faad->sinkpad, gst_faad_chain);
  gst_pad_set_link_function (faad->sinkpad, gst_faad_sinkconnect);

  faad->srcpad = gst_pad_new_from_template (
	GST_PAD_TEMPLATE_GET (src_template), "src");
  gst_element_add_pad (GST_ELEMENT (faad), faad->srcpad);
  gst_pad_set_link_function (faad->srcpad, gst_faad_srcconnect);

  /* This was originally intended as a getcaps() function, but
   * in the end, we needed a srcconnect() function, so this is
   * not really useful. However, srcconnect() uses it, so it is
   * still there... */
  /*gst_pad_set_getcaps_function (faad->srcpad, gst_faad_srcgetcaps);*/
}

static GstPadLinkReturn
gst_faad_sinkconnect (GstPad  *pad,
		      GstCaps *caps)
{
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  /* oh, we really don't care what's in here. We'll
   * get AAC audio (MPEG-2/4) anyway, so why bother? */
  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_faad_srcgetcaps (GstPad  *pad,
		     GstCaps *caps)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));

  if (faad->handle != NULL &&
      faad->channels != -1 && faad->samplerate != -1) {
    faacDecConfiguration *conf;
    GstCaps *caps;

    conf = faacDecGetCurrentConfiguration (faad->handle);

    switch (conf->outputFormat) {
      case FAAD_FMT_16BIT:
        caps = GST_CAPS_NEW ("faad_src_int16",
			     "audio/x-raw-int",
			       "signed", GST_PROPS_BOOLEAN (TRUE),
			       "width",  GST_PROPS_INT (16),
			       "depth",  GST_PROPS_INT (16));
        break;
      case FAAD_FMT_24BIT:
        caps = GST_CAPS_NEW ("faad_src_int24",
			     "audio/x-raw-int",
			       "signed", GST_PROPS_BOOLEAN (TRUE),
			       "width",  GST_PROPS_INT (24),
			       "depth",  GST_PROPS_INT (24));
        break;
      case FAAD_FMT_32BIT:
        caps = GST_CAPS_NEW ("faad_src_int32",
			     "audio/x-raw-int",
			       "signed", GST_PROPS_BOOLEAN (TRUE),
			       "width",  GST_PROPS_INT (32),
			       "depth",  GST_PROPS_INT (32));
        break;
      case FAAD_FMT_FLOAT:
        caps = GST_CAPS_NEW ("faad_src_float32",
			     "audio/x-raw-float",
			       "depth",  GST_PROPS_INT (32));
        break;
      case FAAD_FMT_DOUBLE:
        caps = GST_CAPS_NEW ("faad_src_float64",
			     "audio/x-raw-float",
			       "depth",  GST_PROPS_INT (64));
        break;
      default:
        caps = GST_CAPS_NONE;
        break;
    }

    if (caps) {
      GstPropsEntry *samplerate, *channels, *endianness;

      if (faad->samplerate != -1) {
        samplerate = gst_props_entry_new ("rate",
		GST_PROPS_INT (faad->samplerate));
      } else {
        samplerate = gst_props_entry_new ("rate",
		GST_PROPS_INT_RANGE (8000, 96000));
      }
      gst_props_add_entry (caps->properties, samplerate);

      if (faad->channels != -1) {
        channels = gst_props_entry_new ("channels",
		GST_PROPS_INT (faad->channels));
      } else {
        channels = gst_props_entry_new ("channels",
		GST_PROPS_INT_RANGE (1, 6));
      }
      gst_props_add_entry (caps->properties, channels);

      endianness = gst_props_entry_new ("endianness",
		GST_PROPS_INT (G_BYTE_ORDER));
      gst_props_add_entry (caps->properties, endianness);
    }

    return caps;
  }

  return gst_pad_template_get_caps (
	GST_PAD_TEMPLATE_GET (src_template));
}

static GstPadLinkReturn
gst_faad_srcconnect (GstPad  *pad,
		     GstCaps *caps)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  GstCaps *t;

  if (!faad->handle ||
      (faad->samplerate == -1 || faad->channels == -1)) {
    return GST_PAD_LINK_DELAYED;
  }

  /* we do samplerate/channels ourselves */
  for (t = caps; t != NULL; t = t->next) {
    gst_props_remove_entry_by_name (t->properties, "rate");
    gst_props_remove_entry_by_name (t->properties, "channels");
  }

  /* go through list */
  caps = gst_caps_normalize (caps);
  for ( ; caps != NULL; caps = caps->next) {
    const gchar *mimetype = gst_caps_get_mime (caps);
    gint depth = 0, fmt = 0;

    if (!strcmp (mimetype, "audio/x-raw-int")) {
      gint width = 0;

      if (gst_caps_has_fixed_property (caps, "depth") &&
          gst_caps_has_fixed_property (caps, "width"))
        gst_caps_get (caps, "depth", &depth,
			    "width", &width, NULL);
      if (depth != width)
        continue;

      switch (depth) {
        case 16:
          fmt = FAAD_FMT_16BIT;
          break;
        case 24:
          fmt = FAAD_FMT_24BIT;
          break;
        case 32:
          fmt = FAAD_FMT_32BIT;
          break;
      }
    } else {
      if (gst_caps_has_fixed_property (caps, "depth"))
        gst_caps_get_int (caps, "depth", &depth);

      switch (depth) {
        case 32:
          fmt = FAAD_FMT_FLOAT;
          break;
        case 64:
          fmt = FAAD_FMT_DOUBLE;
          break;
      }
    }

    if (fmt) {
      GstCaps *newcaps;
      faacDecConfiguration *conf;

      conf = faacDecGetCurrentConfiguration (faad->handle);
      conf->outputFormat = fmt;
      faacDecSetConfiguration (faad->handle, conf);
      /* FIXME: handle return value, how? */

      newcaps = gst_faad_srcgetcaps (pad, NULL);
      g_assert (GST_CAPS_IS_FIXED (newcaps));
      if (gst_pad_try_set_caps (pad, newcaps) > 0) {
        faad->bps = depth / 8;
        return GST_PAD_LINK_DONE;
      }
    }
  }

  return GST_PAD_LINK_REFUSED;
}

static void
gst_faad_chain (GstPad  *pad,
		GstData *data)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  GstBuffer *buf, *outbuf;
  faacDecFrameInfo info;
  void *out;

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        gst_element_set_eos (GST_ELEMENT (faad));
        gst_pad_push (faad->srcpad, data);
        return;
      default:
	gst_pad_event_default (pad, event);
        return;
    }
  }

  buf = GST_BUFFER (data);

  if (faad->samplerate == -1 || faad->channels == -1) {
    gulong samplerate;
    guchar channels;

    faacDecInit (faad->handle,
		 GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf),
		 &samplerate, &channels);
    faad->samplerate = samplerate;
    faad->channels = channels;
    if (gst_faad_srcconnect (faad->srcpad,
			     gst_pad_get_allowed_caps (faad->srcpad)) <= 0) {
      gst_element_error (GST_ELEMENT (faad),
			 "Failed to negotiate output format with next element");
      gst_buffer_unref (buf);
      return;
    }
  }

  out = faacDecDecode (faad->handle, &info,
		       GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  if (info.error) {
    gst_element_error (GST_ELEMENT (faad),
		       "Failed to decode buffer: %s",
		       faacDecGetErrorMessage (info.error));
    gst_buffer_unref (buf);
    return;
  }

  if (info.samplerate != faad->samplerate ||
      info.channels != faad->channels) {
    faad->samplerate = info.samplerate;
    faad->channels = info.channels;
    if (gst_faad_srcconnect (faad->srcpad,
			     gst_pad_get_allowed_caps (faad->srcpad)) <= 0) {
      gst_element_error (GST_ELEMENT (faad),
			 "Failed to re-negotiate format with next element");
      gst_buffer_unref (buf);
      return;
    }
  }

  /* FIXME: did it handle the whole buffer? */
  outbuf = gst_buffer_new_and_alloc (info.samples * faad->bps);
  /* ugh */
  memcpy (GST_BUFFER_DATA (outbuf), out, GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  gst_buffer_unref (buf);
  gst_pad_push (faad->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_faad_change_state (GstElement *element)
{
  GstFaad *faad = GST_FAAD (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!(faad->handle = faacDecOpen ()))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      faad->samplerate = -1;
      faad->channels = -1;
      break;
    case GST_STATE_READY_TO_NULL:
      faacDecClose (faad->handle);
      faad->handle = NULL;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "faad",
			       GST_RANK_PRIMARY,
			       GST_TYPE_FAAD);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "faad",
  "Free AAC Decoder (FAAD)",
  plugin_init,
  VERSION,
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN
)
